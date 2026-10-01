#pragma once

void LatteOverlay_init();
void LatteOverlay_render(bool pad_view);
void LatteOverlay_updateStats(double fps, sint32 drawcalls, sint32 fastDrawcalls);

void LatteOverlay_pushNotification(const std::string& text, sint32 duration);

#if BOOST_PLAT_ANDROID
// battery level in percent (-1 = unknown), temperature in 0.1 °C (INT32_MIN = unknown), Android thermal status (-1 = unknown)
void LatteOverlay_setDeviceStatus(sint32 batteryPercent, bool isCharging, sint32 batteryTemperatureTenths, sint32 thermalStatus);
// once per emulated frame, on the GPU thread (for the frame time graph)
void LatteOverlay_recordFrameTime();
// frames shown per second with frame generation, generated ones included. 0 while it's off. GPU thread
void LatteOverlay_setFrameGenShownFps(double fps);
#endif