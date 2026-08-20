#pragma once

#include "UpscalingCore/VansUpscalerTypes.h"

#include <cstdint>

namespace VansGraphics
{
	struct VansCommandRecordingConfig
	{
		bool parallelEnabled = true;
		bool frameContextRingEnabled = false;
		std::uint32_t framesInFlight = 2;
		bool asyncComputeEnabled = false;
	};

	// Renderer-owned configuration boundary shared by editor and packaged runtime.
	// Project serialization maps to this value type; render backends never depend
	// on ProjectSystem or editor DTOs.
	struct VansRenderRuntimeConfig
	{
		VansUpscalerConfig upscaler;
		VansCommandRecordingConfig commandRecording;
	};
}
