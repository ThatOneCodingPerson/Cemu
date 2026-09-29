#include "Cafe/HW/Latte/Renderer/RendererOuputShader.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"
#include "config/ActiveSettings.h"

const std::string RendererOutputShader::s_copy_shader_source =
R"(
void outputShader()
{
	colorOut0 = vec4(texture(textureSrc, passUV).rgb,1.0);
}
)";

const std::string RendererOutputShader::s_copy_shader_source_mtl =
R"(#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float2 uv;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> textureSrc [[texture(0)]], sampler samplr [[sampler(0)]]) {
	return float4(textureSrc.sample(samplr, in.uv).rgb, 1.0);
}
)";

const std::string RendererOutputShader::s_bicubic_shader_source =
R"(
vec4 cubic(float x)
{
	float x2 = x * x;
	float x3 = x2 * x;
	vec4 w;
	w.x = -x3 + 3 * x2 - 3 * x + 1;
	w.y = 3 * x3 - 6 * x2 + 4;
	w.z = -3 * x3 + 3 * x2 + 3 * x + 1;
	w.w = x3;
	return w / 6.0;
}

vec4 bcFilter(vec2 uv, vec4 texelSize)
{
	vec2 pixel = uv*texelSize.zw - 0.5;
	vec2 pixelFrac = fract(pixel);
	vec2 pixelInt = pixel - pixelFrac;

	vec4 xcubic = cubic(pixelFrac.x);
	vec4 ycubic = cubic(pixelFrac.y);

	vec4 c = vec4(pixelInt.x - 0.5, pixelInt.x + 1.5, pixelInt.y - 0.5, pixelInt.y + 1.5);
	vec4 s = vec4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
	vec4 offset = c + vec4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;

	vec4 sample0 = texture(textureSrc, vec2(offset.x, offset.z) * texelSize.xy);
	vec4 sample1 = texture(textureSrc, vec2(offset.y, offset.z) * texelSize.xy);
	vec4 sample2 = texture(textureSrc, vec2(offset.x, offset.w) * texelSize.xy);
	vec4 sample3 = texture(textureSrc, vec2(offset.y, offset.w) * texelSize.xy);

	float sx = s.x / (s.x + s.y);
	float sy = s.z / (s.z + s.w);

	return mix(
		mix(sample3, sample2, sx),
		mix(sample1, sample0, sx), sy);
}

void outputShader(){
	vec4 texelSize = vec4( 1.0 / textureSrcResolution.xy, textureSrcResolution.xy);
	colorOut0 = vec4(bcFilter(passUV, texelSize).rgb,1.0);
}
)";

const std::string RendererOutputShader::s_bicubic_shader_source_mtl =
R"(#include <metal_stdlib>
using namespace metal;

float4 cubic(float x) {
	float x2 = x * x;
	float x3 = x2 * x;
	float4 w;
	w.x = -x3 + 3 * x2 - 3 * x + 1;
	w.y = 3 * x3 - 6 * x2 + 4;
	w.z = -3 * x3 + 3 * x2 + 3 * x + 1;
	w.w = x3;
	return w / 6.0;
}

float4 bcFilter(texture2d<float> textureSrc, sampler samplr, float2 texcoord, float2 texscale) {
	float fx = fract(texcoord.x);
	float fy = fract(texcoord.y);
	texcoord.x -= fx;
	texcoord.y -= fy;

	float4 xcubic = cubic(fx);
	float4 ycubic = cubic(fy);

	float4 c = float4(texcoord.x - 0.5, texcoord.x + 1.5, texcoord.y - 0.5, texcoord.y + 1.5);
	float4 s = float4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
	float4 offset = c + float4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;

	float4 sample0 = textureSrc.sample(samplr, float2(offset.x, offset.z) * texscale);
	float4 sample1 = textureSrc.sample(samplr, float2(offset.y, offset.z) * texscale);
	float4 sample2 = textureSrc.sample(samplr, float2(offset.x, offset.w) * texscale);
	float4 sample3 = textureSrc.sample(samplr, float2(offset.y, offset.w) * texscale);

	float sx = s.x / (s.x + s.y);
	float sy = s.z / (s.z + s.w);

	return mix(
		mix(sample3, sample2, sx),
		mix(sample1, sample0, sx), sy);
}

struct VertexOut {
    float2 uv;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> textureSrc [[texture(0)]], sampler samplr [[sampler(0)]]) {
    float2 textureSrcResolution = float2(textureSrc.get_width(), textureSrc.get_height());
	return float4(bcFilter(textureSrc, samplr, in.uv * textureSrcResolution, float2(1.0, 1.0) / textureSrcResolution).rgb, 1.0);
}
)";

const std::string RendererOutputShader::s_hermite_shader_source =
R"(
// https://www.shadertoy.com/view/MllSzX

vec3 CubicHermite (vec3 A, vec3 B, vec3 C, vec3 D, float t)
{
	float t2 = t*t;
    float t3 = t*t*t;
    vec3 a = -A/2.0 + (3.0*B)/2.0 - (3.0*C)/2.0 + D/2.0;
    vec3 b = A - (5.0*B)/2.0 + 2.0*C - D / 2.0;
    vec3 c = -A/2.0 + C/2.0;
   	vec3 d = B;

    return a*t3 + b*t2 + c*t + d;
}


vec3 BicubicHermiteTexture(vec2 uv, vec4 texelSize)
{
	vec2 pixel = uv*texelSize.zw + 0.5;
	vec2 frac = fract(pixel);
    pixel = floor(pixel) / texelSize.zw - vec2(texelSize.xy/2.0);

	vec4 doubleSize = texelSize*2.0;

	vec3 C00 = texture(textureSrc, pixel + vec2(-texelSize.x ,-texelSize.y)).rgb;
    vec3 C10 = texture(textureSrc, pixel + vec2( 0.0        ,-texelSize.y)).rgb;
    vec3 C20 = texture(textureSrc, pixel + vec2( texelSize.x ,-texelSize.y)).rgb;
    vec3 C30 = texture(textureSrc, pixel + vec2( doubleSize.x,-texelSize.y)).rgb;

    vec3 C01 = texture(textureSrc, pixel + vec2(-texelSize.x , 0.0)).rgb;
    vec3 C11 = texture(textureSrc, pixel + vec2( 0.0        , 0.0)).rgb;
    vec3 C21 = texture(textureSrc, pixel + vec2( texelSize.x , 0.0)).rgb;
    vec3 C31 = texture(textureSrc, pixel + vec2( doubleSize.x, 0.0)).rgb;

    vec3 C02 = texture(textureSrc, pixel + vec2(-texelSize.x , texelSize.y)).rgb;
    vec3 C12 = texture(textureSrc, pixel + vec2( 0.0        , texelSize.y)).rgb;
    vec3 C22 = texture(textureSrc, pixel + vec2( texelSize.x , texelSize.y)).rgb;
    vec3 C32 = texture(textureSrc, pixel + vec2( doubleSize.x, texelSize.y)).rgb;

    vec3 C03 = texture(textureSrc, pixel + vec2(-texelSize.x , doubleSize.y)).rgb;
    vec3 C13 = texture(textureSrc, pixel + vec2( 0.0        , doubleSize.y)).rgb;
    vec3 C23 = texture(textureSrc, pixel + vec2( texelSize.x , doubleSize.y)).rgb;
    vec3 C33 = texture(textureSrc, pixel + vec2( doubleSize.x, doubleSize.y)).rgb;

    vec3 CP0X = CubicHermite(C00, C10, C20, C30, frac.x);
    vec3 CP1X = CubicHermite(C01, C11, C21, C31, frac.x);
    vec3 CP2X = CubicHermite(C02, C12, C22, C32, frac.x);
    vec3 CP3X = CubicHermite(C03, C13, C23, C33, frac.x);

    return CubicHermite(CP0X, CP1X, CP2X, CP3X, frac.y);
}

void outputShader(){
	vec4 texelSize = vec4( 1.0 / textureSrcResolution.xy, textureSrcResolution.xy);
	colorOut0 = vec4(BicubicHermiteTexture(passUV, texelSize), 1.0);
}
)";

const std::string RendererOutputShader::s_hermite_shader_source_mtl =
R"(#include <metal_stdlib>
using namespace metal;

// https://www.shadertoy.com/view/MllSzX

float3 CubicHermite(float3 A, float3 B, float3 C, float3 D, float t) {
	float t2 = t*t;
    float t3 = t*t*t;
    float3 a = -A/2.0 + (3.0*B)/2.0 - (3.0*C)/2.0 + D/2.0;
    float3 b = A - (5.0*B)/2.0 + 2.0*C - D / 2.0;
    float3 c = -A/2.0 + C/2.0;
   	float3 d = B;

    return a*t3 + b*t2 + c*t + d;
}


float3 BicubicHermiteTexture(texture2d<float> textureSrc, sampler samplr, float2 uv, float4 texelSize) {
	float2 pixel = uv*texelSize.zw + 0.5;
	float2 frac = fract(pixel);
    pixel = floor(pixel) / texelSize.zw - float2(texelSize.xy/2.0);

	float4 doubleSize = texelSize*texelSize;

	float3 C00 = textureSrc.sample(samplr, pixel + float2(-texelSize.x ,-texelSize.y)).rgb;
    float3 C10 = textureSrc.sample(samplr, pixel + float2( 0.0        ,-texelSize.y)).rgb;
    float3 C20 = textureSrc.sample(samplr, pixel + float2( texelSize.x ,-texelSize.y)).rgb;
    float3 C30 = textureSrc.sample(samplr, pixel + float2( doubleSize.x,-texelSize.y)).rgb;

    float3 C01 = textureSrc.sample(samplr, pixel + float2(-texelSize.x , 0.0)).rgb;
    float3 C11 = textureSrc.sample(samplr, pixel + float2( 0.0        , 0.0)).rgb;
    float3 C21 = textureSrc.sample(samplr, pixel + float2( texelSize.x , 0.0)).rgb;
    float3 C31 = textureSrc.sample(samplr, pixel + float2( doubleSize.x, 0.0)).rgb;

    float3 C02 = textureSrc.sample(samplr, pixel + float2(-texelSize.x , texelSize.y)).rgb;
    float3 C12 = textureSrc.sample(samplr, pixel + float2( 0.0        , texelSize.y)).rgb;
    float3 C22 = textureSrc.sample(samplr, pixel + float2( texelSize.x , texelSize.y)).rgb;
    float3 C32 = textureSrc.sample(samplr, pixel + float2( doubleSize.x, texelSize.y)).rgb;

    float3 C03 = textureSrc.sample(samplr, pixel + float2(-texelSize.x , doubleSize.y)).rgb;
    float3 C13 = textureSrc.sample(samplr, pixel + float2( 0.0        , doubleSize.y)).rgb;
    float3 C23 = textureSrc.sample(samplr, pixel + float2( texelSize.x , doubleSize.y)).rgb;
    float3 C33 = textureSrc.sample(samplr, pixel + float2( doubleSize.x, doubleSize.y)).rgb;

    float3 CP0X = CubicHermite(C00, C10, C20, C30, frac.x);
    float3 CP1X = CubicHermite(C01, C11, C21, C31, frac.x);
    float3 CP2X = CubicHermite(C02, C12, C22, C32, frac.x);
    float3 CP3X = CubicHermite(C03, C13, C23, C33, frac.x);

    return CubicHermite(CP0X, CP1X, CP2X, CP3X, frac.y);
}

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

fragment float4 main0(VertexOut in [[stage_in]], texture2d<float> textureSrc [[texture(0)]], sampler samplr [[sampler(0)]], constant float2& outputResolution [[buffer(0)]]) {
	float4 texelSize = float4(1.0 / outputResolution.xy, outputResolution.xy);
	return float4(BicubicHermiteTexture(textureSrc, samplr, in.uv, texelSize), 1.0);
}
)";

#if BOOST_PLAT_ANDROID
// AMD FidelityFX Super Resolution 1 (FSR 1), EASU: edge adaptive spatial upsampling in a single pass. This is the
// non-packed 32-bit path of ffx_fsr1.h v1.20210629 and the helpers it uses from ffx_a.h
// (github.com/GPUOpen-Effects/FidelityFX-FSR), ported to plain GLSL for the output pass. The input viewport is the
// whole source texture, so the EASU position constants reduce to passUV * textureSrcResolution - 0.5.
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of
// the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
const std::string RendererOutputShader::s_fsr_easu_shader_source =
R"(
// ffx_a.h approximations; unlike an exact 1/x they stay finite for 0, which EASU relies on in flat areas
float APrxLoRcpF1(float a) { return uintBitsToFloat(uint(0x7ef07ebb) - floatBitsToUint(a)); }
float APrxLoRsqF1(float a) { return uintBitsToFloat(uint(0x5f347d74) - (floatBitsToUint(a) >> uint(1))); }

void FsrEasuTapF(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c)
{
	// rotate the offset by the direction, anisotropy
	vec2 v;
	v.x = (off.x * ( dir.x)) + (off.y * dir.y);
	v.y = (off.x * (-dir.y)) + (off.y * dir.x);
	v *= len;
	// distance^2, limited to the window
	float d2 = min(v.x * v.x + v.y * v.y, clp);
	// approximation of lanczos2 without sin(), rcp() or sqrt()
	float wB = (2.0 / 5.0) * d2 - 1.0;
	float wA = lob * d2 - 1.0;
	wB *= wB;
	wA *= wA;
	wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);
	float w = wB * wA;
	aC += c * w;
	aW += w;
}

// accumulates direction and length for one of the four bilinear positions
void FsrEasuSetF(inout vec2 dir, inout float len, vec2 pp, bool biS, bool biT, bool biU, bool biV,
	float lA, float lB, float lC, float lD, float lE)
{
	float w = 0.0;
	if (biS) w = (1.0 - pp.x) * (1.0 - pp.y);
	if (biT) w = pp.x * (1.0 - pp.y);
	if (biU) w = (1.0 - pp.x) * pp.y;
	if (biV) w = pp.x * pp.y;
	float dc = lD - lC;
	float cb = lC - lB;
	float lenX = APrxLoRcpF1(max(abs(dc), abs(cb)));
	float dirX = lD - lB;
	dir.x += dirX * w;
	lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
	lenX *= lenX;
	len += lenX * w;
	float ec = lE - lC;
	float ca = lC - lA;
	float lenY = APrxLoRcpF1(max(abs(ec), abs(ca)));
	float dirY = lE - lA;
	dir.y += dirY * w;
	lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
	lenY *= lenY;
	len += lenY * w;
}

void outputShader()
{
	vec2 texelSize = 1.0 / textureSrcResolution;
	// position of 'f' in the source (the upper-left texel of the 2x2 bilinear footprint)
	vec2 pp = passUV * textureSrcResolution - 0.5;
	vec2 fp = floor(pp);
	pp -= fp;
	// 12-tap kernel, gathered in four 2x2 groups:
	//    b c
	//  e f g h
	//  i j k l
	//    n o
	vec2 p0 = (fp + vec2(1.0, -1.0)) * texelSize;
	vec2 p1 = p0 + vec2(-1.0, 2.0) * texelSize;
	vec2 p2 = p0 + vec2(1.0, 2.0) * texelSize;
	vec2 p3 = p0 + vec2(0.0, 4.0) * texelSize;
	vec4 bczzR = textureGather(textureSrc, p0, 0);
	vec4 bczzG = textureGather(textureSrc, p0, 1);
	vec4 bczzB = textureGather(textureSrc, p0, 2);
	vec4 ijfeR = textureGather(textureSrc, p1, 0);
	vec4 ijfeG = textureGather(textureSrc, p1, 1);
	vec4 ijfeB = textureGather(textureSrc, p1, 2);
	vec4 klhgR = textureGather(textureSrc, p2, 0);
	vec4 klhgG = textureGather(textureSrc, p2, 1);
	vec4 klhgB = textureGather(textureSrc, p2, 2);
	vec4 zzonR = textureGather(textureSrc, p3, 0);
	vec4 zzonG = textureGather(textureSrc, p3, 1);
	vec4 zzonB = textureGather(textureSrc, p3, 2);
	// approximate luma (times 2)
	vec4 bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);
	vec4 ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);
	vec4 klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);
	vec4 zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);
	float bL = bczzL.x;
	float cL = bczzL.y;
	float iL = ijfeL.x;
	float jL = ijfeL.y;
	float fL = ijfeL.z;
	float eL = ijfeL.w;
	float kL = klhgL.x;
	float lL = klhgL.y;
	float hL = klhgL.z;
	float gL = klhgL.w;
	float oL = zzonL.z;
	float nL = zzonL.w;
	vec2 dir = vec2(0.0);
	float len = 0.0;
	FsrEasuSetF(dir, len, pp, true, false, false, false, bL, eL, fL, gL, jL);
	FsrEasuSetF(dir, len, pp, false, true, false, false, cL, fL, gL, hL, kL);
	FsrEasuSetF(dir, len, pp, false, false, true, false, fL, iL, jL, kL, nL);
	FsrEasuSetF(dir, len, pp, false, false, false, true, gL, jL, kL, lL, oL);
	// normalize with approximation, and clean up close to zero
	vec2 dir2 = dir * dir;
	float dirR = dir2.x + dir2.y;
	bool zro = dirR < (1.0 / 32768.0);
	dirR = APrxLoRsqF1(dirR);
	dirR = zro ? 1.0 : dirR;
	dir.x = zro ? 1.0 : dir.x;
	dir *= vec2(dirR);
	// {0 to 2} to {0 to 1}, shaped with square
	len = len * 0.5;
	len *= len;
	// stretch the kernel {1.0 vert|horz, to sqrt(2.0) on diagonal}
	float stretch = (dir.x * dir.x + dir.y * dir.y) * APrxLoRcpF1(max(abs(dir.x), abs(dir.y)));
	vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
	// the window shifts from +/-{sqrt(2.0) to slightly beyond 2.0} with the amount of edge
	float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
	float clp = APrxLoRcpF1(lob);
	// accumulation, limited to the min/max of the 4 nearest texels (no ringing)
	vec3 min4 = min(min(min(vec3(ijfeR.z, ijfeG.z, ijfeB.z), vec3(klhgR.w, klhgG.w, klhgB.w)), vec3(ijfeR.y, ijfeG.y, ijfeB.y)),
		vec3(klhgR.x, klhgG.x, klhgB.x));
	vec3 max4 = max(max(max(vec3(ijfeR.z, ijfeG.z, ijfeB.z), vec3(klhgR.w, klhgG.w, klhgB.w)), vec3(ijfeR.y, ijfeG.y, ijfeB.y)),
		vec3(klhgR.x, klhgG.x, klhgB.x));
	vec3 aC = vec3(0.0);
	float aW = 0.0;
	FsrEasuTapF(aC, aW, vec2( 0.0,-1.0) - pp, dir, len2, lob, clp, vec3(bczzR.x, bczzG.x, bczzB.x)); // b
	FsrEasuTapF(aC, aW, vec2( 1.0,-1.0) - pp, dir, len2, lob, clp, vec3(bczzR.y, bczzG.y, bczzB.y)); // c
	FsrEasuTapF(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.x, ijfeG.x, ijfeB.x)); // i
	FsrEasuTapF(aC, aW, vec2( 0.0, 1.0) - pp, dir, len2, lob, clp, vec3(ijfeR.y, ijfeG.y, ijfeB.y)); // j
	FsrEasuTapF(aC, aW, vec2( 0.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.z, ijfeG.z, ijfeB.z)); // f
	FsrEasuTapF(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, vec3(ijfeR.w, ijfeG.w, ijfeB.w)); // e
	FsrEasuTapF(aC, aW, vec2( 1.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.x, klhgG.x, klhgB.x)); // k
	FsrEasuTapF(aC, aW, vec2( 2.0, 1.0) - pp, dir, len2, lob, clp, vec3(klhgR.y, klhgG.y, klhgB.y)); // l
	FsrEasuTapF(aC, aW, vec2( 2.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.z, klhgG.z, klhgB.z)); // h
	FsrEasuTapF(aC, aW, vec2( 1.0, 0.0) - pp, dir, len2, lob, clp, vec3(klhgR.w, klhgG.w, klhgB.w)); // g
	FsrEasuTapF(aC, aW, vec2( 1.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.z, zzonG.z, zzonB.z)); // o
	FsrEasuTapF(aC, aW, vec2( 0.0, 2.0) - pp, dir, len2, lob, clp, vec3(zzonR.w, zzonG.w, zzonB.w)); // n
	// normalize and dering
	colorOut0 = vec4(min(max4, max(min4, aC * vec3(1.0 / aW))), 1.0);
}
)";
#endif

RendererOutputShader::RendererOutputShader(const std::string& vertex_source, const std::string& fragment_source)
{
    std::string finalFragmentSrc;
    if (g_renderer->GetType() == RendererAPI::Metal)
        finalFragmentSrc = fragment_source;
    else
        finalFragmentSrc = PrependFragmentPreamble(fragment_source);

	m_vertex_shader.reset(g_renderer->shader_create(RendererShader::ShaderType::kVertex, 0, 0, vertex_source, false, false));
	m_fragment_shader.reset(g_renderer->shader_create(RendererShader::ShaderType::kFragment, 0, 0, finalFragmentSrc, false, false));

	m_vertex_shader->PreponeCompilation(true);
	m_fragment_shader->PreponeCompilation(true);

	if (!m_vertex_shader->WaitForCompiled())
		throw std::exception();

	if(!m_fragment_shader->WaitForCompiled())
		throw std::exception();

}

RendererOutputShader::OutputUniformVariables RendererOutputShader::FillUniformBlockBuffer(const LatteTextureView& texture_view, const Vector2i& output_res, const bool padView) const
{
	OutputUniformVariables vars;

	sint32 effectiveWidth, effectiveHeight;
	texture_view.baseTexture->GetEffectiveSize(effectiveWidth, effectiveHeight, 0);
	vars.textureSrcResolution = {(float)effectiveWidth, (float)effectiveHeight};

	vars.nativeResolution = {(float)texture_view.baseTexture->width, (float)texture_view.baseTexture->height};
	vars.outputResolution = output_res;

	vars.applySRGBEncoding = padView ? LatteGPUState.drcBufferUsesSRGB : LatteGPUState.tvBufferUsesSRGB;
	vars.targetGamma = padView ? ActiveSettings::GetDRCGamma() : ActiveSettings::GetTVGamma();
	vars.displayGamma = GetConfig().userDisplayGamma;

	return vars;
}

RendererOutputShader* RendererOutputShader::s_copy_shader;
RendererOutputShader* RendererOutputShader::s_copy_shader_ud;

RendererOutputShader* RendererOutputShader::s_bicubic_shader;
RendererOutputShader* RendererOutputShader::s_bicubic_shader_ud;

RendererOutputShader* RendererOutputShader::s_hermit_shader;
RendererOutputShader* RendererOutputShader::s_hermit_shader_ud;

#if BOOST_PLAT_ANDROID
RendererOutputShader* RendererOutputShader::s_fsr_easu_shader;
RendererOutputShader* RendererOutputShader::s_fsr_easu_shader_ud;
#endif

std::string RendererOutputShader::GetOpenGlVertexSource(bool render_upside_down)
{
	// vertex shader
	std::ostringstream vertex_source;
		vertex_source <<
			R"(#version 420
layout(location = 0) smooth out vec2 passUV;

out gl_PerVertex
{
   vec4 gl_Position;
};

void main(){
	vec2 vPos;
	vec2 vUV;
	int vID = gl_VertexID;
)";

		if (render_upside_down)
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	)";
		}
		else
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	)";
		}

		vertex_source <<
			R"(	passUV = vUV;
	gl_Position = vec4(vPos, 0.0, 1.0);
}
)";
		return vertex_source.str();
}

std::string RendererOutputShader::GetVulkanVertexSource(bool render_upside_down)
{
	// vertex shader
	std::ostringstream vertex_source;
		vertex_source <<
			R"(#version 450
layout(location = 0) out vec2 passUV;
)";
#if BOOST_PLAT_ANDROID
		// Vulkan pre-rotation (SwapchainInfoVk::m_preTransform): quarter turns clockwise, set per pipeline
		vertex_source << "layout(constant_id = 0) const int preRotation = 0;\n";
#endif
		vertex_source <<
			R"(
out gl_PerVertex
{
   vec4 gl_Position;
};

void main(){
	vec2 vPos;
	vec2 vUV;
	int vID = gl_VertexIndex;
)";

		if (render_upside_down)
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,0.0); }
	)";
		}
		else
		{
			vertex_source <<
				R"(	if( vID == 0 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	else if( vID == 1 ) { vPos = vec2(-1.0,1.0); vUV = vec2(0.0,1.0); }
	else if( vID == 2 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 3 ) { vPos = vec2(-1.0,-1.0); vUV = vec2(0.0,0.0); }
	else if( vID == 4 ) { vPos = vec2(1.0,-1.0); vUV = vec2(1.0,0.0); }
	else if( vID == 5 ) { vPos = vec2(1.0,1.0); vUV = vec2(1.0,1.0); }
	)";
		}

#if BOOST_PLAT_ANDROID
		vertex_source <<
			R"(	if( preRotation == 1 ) vPos = vec2(-vPos.y, vPos.x);
	else if( preRotation == 2 ) vPos = -vPos;
	else if( preRotation == 3 ) vPos = vec2(vPos.y, -vPos.x);
)";
#endif
		vertex_source <<
			R"(	passUV = vUV;
	gl_Position = vec4(vPos, 0.0, 1.0);
}
)";
		return vertex_source.str();
}

std::string RendererOutputShader::GetMetalVertexSource(bool render_upside_down)
{
	// vertex shader
	std::ostringstream vertex_source;
		vertex_source <<
			R"(#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut main0(ushort vid [[vertex_id]]) {
	VertexOut out;
	float2 pos;
	if (vid == 0) pos = float2(-1.0, -3.0);
	else if (vid == 1) pos = float2(-1.0, 1.0);
	else if (vid == 2) pos = float2(3.0, 1.0);
	out.uv = pos * 0.5 + 0.5;
	out.uv.y = 1.0 - out.uv.y;
)";

		if (render_upside_down)
		{
			vertex_source <<
				R"(	pos.y = -pos.y;
	)";
		}

		vertex_source <<
			R"(	out.position = float4(pos, 0.0, 1.0);
	return out;
}
)";
		return vertex_source.str();
}

std::string RendererOutputShader::PrependFragmentPreamble(const std::string& shaderSrc)
{
	return R"(#version 430
layout(location = 0) smooth in vec2 passUV;
layout(binding = 0) uniform sampler2D textureSrc;
layout(location = 0) out vec4 colorOut0;

#ifdef VULKAN
layout (binding = 1, std140)
#else
layout (binding = 0, std140)
#endif
uniform parameters {
uniform vec2 textureSrcResolution;
uniform vec2 nativeResolution;
uniform vec2 outputResolution;
uniform bool applySRGBEncoding;
uniform float targetGamma;
uniform float displayGamma;
};

float sRGBEncode(float linear)
{
	if(linear <= 0.0031308)
		return 12.92f * linear;
	else
		return 1.055f * pow(linear, 1.0f / 2.4f) - 0.055f;

}

vec3 sRGBEncode(vec3 linear)
{
	return vec3(sRGBEncode(linear.r), sRGBEncode(linear.g), sRGBEncode(linear.b));
}

// fwd. declaration
void outputShader();
void main()
{
	outputShader(); // sets colorOut0
	if(applySRGBEncoding)
		colorOut0 = vec4(sRGBEncode(colorOut0.rgb), 1.0f);

	if (displayGamma > 0.0f)
		colorOut0 = pow(colorOut0, vec4(targetGamma / displayGamma) );
	else
		colorOut0 = vec4( sRGBEncode( pow(colorOut0.rgb, vec3(targetGamma)) ), 1.0f);

}

)" + shaderSrc;
}
void RendererOutputShader::InitializeStatic()
{
    if (g_renderer->GetType() == RendererAPI::Metal)
    {
        std::string vertex_source = GetMetalVertexSource(false);
        std::string vertex_source_ud = GetMetalVertexSource(true);

       	s_copy_shader = new RendererOutputShader(vertex_source, s_copy_shader_source_mtl);
       	s_copy_shader_ud = new RendererOutputShader(vertex_source_ud, s_copy_shader_source_mtl);

       	s_bicubic_shader = new RendererOutputShader(vertex_source, s_bicubic_shader_source_mtl);
       	s_bicubic_shader_ud = new RendererOutputShader(vertex_source_ud, s_bicubic_shader_source_mtl);

       	s_hermit_shader = new RendererOutputShader(vertex_source, s_hermite_shader_source_mtl);
       	s_hermit_shader_ud = new RendererOutputShader(vertex_source_ud, s_hermite_shader_source_mtl);
    }
    else
    {
    	std::string vertex_source, vertex_source_ud;
    	// vertex shader
    	if (g_renderer->GetType() == RendererAPI::OpenGL)
    	{
    		vertex_source = GetOpenGlVertexSource(false);
    		vertex_source_ud = GetOpenGlVertexSource(true);
    	}
    	else if (g_renderer->GetType() == RendererAPI::Vulkan)
    	{
    		vertex_source = GetVulkanVertexSource(false);
    		vertex_source_ud = GetVulkanVertexSource(true);
    	}
    	s_copy_shader = new RendererOutputShader(vertex_source, s_copy_shader_source);
    	s_copy_shader_ud = new RendererOutputShader(vertex_source_ud, s_copy_shader_source);

    	s_bicubic_shader = new RendererOutputShader(vertex_source, s_bicubic_shader_source);
    	s_bicubic_shader_ud = new RendererOutputShader(vertex_source_ud, s_bicubic_shader_source);

    	s_hermit_shader = new RendererOutputShader(vertex_source, s_hermite_shader_source);
    	s_hermit_shader_ud = new RendererOutputShader(vertex_source_ud, s_hermite_shader_source);
#if BOOST_PLAT_ANDROID
    	// optional: without it the FSR setting falls back to bilinear (LatteRenderTarget_copyToBackbuffer)
    	try
    	{
    		s_fsr_easu_shader = new RendererOutputShader(vertex_source, s_fsr_easu_shader_source);
    		s_fsr_easu_shader_ud = new RendererOutputShader(vertex_source_ud, s_fsr_easu_shader_source);
    	}
    	catch (const std::exception&)
    	{
    		cemuLog_log(LogType::Force, "The FSR 1 output shader failed to compile, the bilinear filter is used instead");
    		delete s_fsr_easu_shader;
    		s_fsr_easu_shader = nullptr;
    	}
#endif
    }
}

void RendererOutputShader::ShutdownStatic()
{
	delete s_copy_shader;
	delete s_copy_shader_ud;

	delete s_bicubic_shader;
	delete s_bicubic_shader_ud;

	delete s_hermit_shader;
	delete s_hermit_shader_ud;
#if BOOST_PLAT_ANDROID
	delete s_fsr_easu_shader;
	delete s_fsr_easu_shader_ud;
	s_fsr_easu_shader = s_fsr_easu_shader_ud = nullptr;
#endif
}
