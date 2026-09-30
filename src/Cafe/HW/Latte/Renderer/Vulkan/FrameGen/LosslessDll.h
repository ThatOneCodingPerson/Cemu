// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

// Frame generation for Android, ported from the Eden emulator (src/video_core/frame_gen/lossless_dll.h,
// lsfg_translate.h). The shaders come from the user's own copy of Lossless Scaling (Lossless.dll); none are shipped.

#pragma once

#include <array>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace FrameGen
{
	// values are shared with NativeSettings.kt (LosslessDllStatus)
	enum class LosslessStatus : sint32
	{
		Ok = 0,
		NotInstalled = 1,
		UnreadableFile = 2,
		NotPortableExecutable = 3,
		MissingShaders = 4,
	};

	// SPIR-V words by Lossless Scaling shader id
	using ShaderModules = std::map<uint32, std::vector<uint32>>;

	namespace PerformanceShader
	{
		constexpr uint32 MIPMAPS = 255;
		constexpr uint32 GENERATE = 256;
		constexpr std::array<uint32, 4> ALPHA{290, 291, 292, 293};
		constexpr std::array<uint32, 5> BETA{298, 299, 300, 301, 302};
		constexpr std::array<uint32, 5> GAMMA{280, 282, 283, 284, 285};
		constexpr std::array<uint32, 10> DELTA{280, 286, 287, 288, 289, 281, 294, 295, 296, 297};

		// the variants of the shaders that compute in half precision are stored at id + 49
		constexpr uint32 NATIVE_FP16_OFFSET = 49;
	}

	// <user data>/lossless/Lossless.dll
	fs::path GetLosslessDllPath();

	LosslessStatus ValidateLosslessDll(const fs::path& path);
	LosslessStatus GetInstalledLosslessStatus();
	// validates `source` and copies it to GetLosslessDllPath()
	LosslessStatus InstallLosslessDll(const fs::path& source);
	bool RemoveInstalledLosslessDll();

	// reads the installed DLL. The bindings of each module are renumbered in order, starting at 0
	LosslessStatus LoadShaderModules(ShaderModules& outModules);

	// what the modules need from the device, read from their SPIR-V
	struct ShaderRequirements
	{
		uint32 spirvVersion = 0; // 0x00MMmm00
		std::set<uint32> capabilities;
		std::set<std::string> extensions; // SPV_* and OpExtInstImport sets
	};
	ShaderRequirements GetShaderRequirements(const ShaderModules& modules);
}
