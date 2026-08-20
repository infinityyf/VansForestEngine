#include "VansUpscalerTypes.h"

namespace VansGraphics
{
	const char* ToString(VansUpscalerBackend backend)
	{
		switch (backend)
		{
		case VansUpscalerBackend::Off: return "Off";
		case VansUpscalerBackend::FSR: return "FSR";
		case VansUpscalerBackend::DLSS: return "DLSS";
		default: return "Unknown";
		}
	}

	const char* ToString(VansUpscaleQualityMode quality)
	{
		switch (quality)
		{
		case VansUpscaleQualityMode::NativeAA: return "NativeAA";
		case VansUpscaleQualityMode::Quality: return "Quality";
		case VansUpscaleQualityMode::Balanced: return "Balanced";
		case VansUpscaleQualityMode::Performance: return "Performance";
		case VansUpscaleQualityMode::UltraPerformance: return "UltraPerformance";
		default: return "Unknown";
		}
	}

	const char* ToString(VansUpscalerFallbackReason reason)
	{
		switch (reason)
		{
		case VansUpscalerFallbackReason::None: return "None";
		case VansUpscalerFallbackReason::NotCompiled: return "NotCompiled";
		case VansUpscalerFallbackReason::RuntimeUnavailable: return "RuntimeUnavailable";
		case VansUpscalerFallbackReason::UnsupportedDevice: return "UnsupportedDevice";
		case VansUpscalerFallbackReason::DriverOutOfDate: return "DriverOutOfDate";
		case VansUpscalerFallbackReason::MissingRuntimeBinary: return "MissingRuntimeBinary";
		case VansUpscalerFallbackReason::RuntimeIntegrityRejected: return "RuntimeIntegrityRejected";
		case VansUpscalerFallbackReason::ContextCreationFailed: return "ContextCreationFailed";
		case VansUpscalerFallbackReason::DispatchFailed: return "DispatchFailed";
		default: return "Unknown";
		}
	}
}
