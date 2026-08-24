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

	// 最终渲染输出分辨率。0x0 表示继续跟随宿主窗口，兼容未声明该字段的旧项目。
	struct VansRenderOutputConfig
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		bool UsesWindowExtent() const { return width == 0 && height == 0; }
		bool HasExplicitExtent() const { return width > 0 && height > 0; }
	};

	// Renderer-owned configuration boundary shared by editor and packaged runtime.
	// Project serialization maps to this value type; render backends never depend
	// on ProjectSystem or editor DTOs.
	struct VansRenderRuntimeConfig
	{
		VansUpscalerConfig upscaler;
		VansCommandRecordingConfig commandRecording;
		VansRenderOutputConfig output;
	};
}
