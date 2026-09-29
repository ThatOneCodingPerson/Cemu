#pragma once

// Android Dynamic Performance Framework (ADPF) hints for the emulation threads (PPC cores and GPU thread).
// A performance hint session tells the scheduler/CPU governor which threads matter and how long each frame took
// compared to the target, so it can keep them on fast cores at sufficient clocks instead of guessing.
// Available since Android 13 (API 33); does nothing on older versions.
namespace AndroidPerformanceHint
{
	// adds the calling thread to the emulation session
	void AddCurrentThread();

	// called by the GPU thread once per presented TV frame. swapInterval: the title's GX2SetSwapInterval, the number
	// of 60 Hz vsyncs per frame (2 for 30 fps titles); it sets the target frame time
	void ReportFrame(uint32 swapInterval);
} // namespace AndroidPerformanceHint
