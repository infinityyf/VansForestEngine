#pragma once

#include <cstdint>
#include <string>

namespace VansGraphics
{
	enum class VansUpscalerBackend : std::uint8_t
	{
		Off = 0,
		FSR = 1,
		DLSS = 2
	};

	enum class VansUpscaleQualityMode : std::uint8_t
	{
		NativeAA = 0,
		Quality = 1,
		Balanced = 2,
		Performance = 3,
		UltraPerformance = 4
	};

	enum class VansUpscalerFallbackReason : std::uint8_t
	{
		None = 0,
		NotCompiled,
		RuntimeUnavailable,
		UnsupportedDevice,
		DriverOutOfDate,
		MissingRuntimeBinary,
		RuntimeIntegrityRejected,
		ContextCreationFailed,
		DispatchFailed
	};

	enum class VansUpscalerResetReason : std::uint32_t
	{
		None = 0,
		FirstFrame = 1u << 0,
		SceneChange = 1u << 1,
		CameraCut = 1u << 2,
		ContextRecreated = 1u << 3,
		RenderSizeChange = 1u << 4,
		OutputSizeChange = 1u << 5,
		BackendChange = 1u << 6,
		QualityChange = 1u << 7,
		FrameDiscontinuity = 1u << 8,
		ProjectionChange = 1u << 9,
		TimelineSeek = 1u << 10,
		ResumeFromPause = 1u << 11,
		DispatchFault = 1u << 12,
		Manual = 1u << 13
	};

	constexpr VansUpscalerResetReason operator|(
		VansUpscalerResetReason lhs,
		VansUpscalerResetReason rhs)
	{
		return static_cast<VansUpscalerResetReason>(
			static_cast<std::uint32_t>(lhs) |
			static_cast<std::uint32_t>(rhs));
	}

	constexpr VansUpscalerResetReason operator&(
		VansUpscalerResetReason lhs,
		VansUpscalerResetReason rhs)
	{
		return static_cast<VansUpscalerResetReason>(
			static_cast<std::uint32_t>(lhs) &
			static_cast<std::uint32_t>(rhs));
	}

	inline VansUpscalerResetReason& operator|=(
		VansUpscalerResetReason& lhs,
		VansUpscalerResetReason rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	struct VansExtent2D
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		constexpr bool IsValid() const
		{
			return width > 0 && height > 0;
		}

		friend constexpr bool operator==(VansExtent2D lhs, VansExtent2D rhs)
		{
			return lhs.width == rhs.width && lhs.height == rhs.height;
		}

		friend constexpr bool operator!=(VansExtent2D lhs, VansExtent2D rhs)
		{
			return !(lhs == rhs);
		}
	};

	struct VansUpscalerConfig
	{
		VansUpscalerBackend backend = VansUpscalerBackend::FSR;
		VansUpscaleQualityMode quality = VansUpscaleQualityMode::Quality;
		float fsrSharpness = 0.35f;
		bool fsrDebugView = false;

		friend bool operator==(const VansUpscalerConfig& lhs, const VansUpscalerConfig& rhs)
		{
			return lhs.backend == rhs.backend &&
				lhs.quality == rhs.quality &&
				lhs.fsrSharpness == rhs.fsrSharpness &&
				lhs.fsrDebugView == rhs.fsrDebugView;
		}

		friend bool operator!=(const VansUpscalerConfig& lhs, const VansUpscalerConfig& rhs)
		{
			return !(lhs == rhs);
		}
	};

	struct VansUpscalerCapabilities
	{
		VansUpscalerBackend backend = VansUpscalerBackend::Off;
		bool compiledIn = false;
		bool runtimeAvailable = false;
		bool deviceSupported = false;
		std::uint32_t supportedQualityMask = 0;
		std::string featureVersion;
		std::string unavailableReason;

		bool Supports(VansUpscaleQualityMode quality) const
		{
			const std::uint32_t bit = 1u << static_cast<std::uint32_t>(quality);
			return (supportedQualityMask & bit) != 0;
		}
	};

	struct VansUpscalerRuntimeDiagnostics
	{
		VansUpscalerConfig desired;
		VansUpscalerConfig effective;
		VansUpscalerFallbackReason fallbackReason = VansUpscalerFallbackReason::None;
		std::string fallbackMessage;
		VansExtent2D renderExtent;
		VansExtent2D outputExtent;
		float mipBias = 0.0f;
		bool contextReady = false;
		bool lastDispatchSucceeded = false;
		bool lastDispatchReset = false;
		VansUpscalerResetReason pendingResetReasons = VansUpscalerResetReason::FirstFrame;
		std::uint32_t backendCreateCode = 0;
		std::uint32_t backendQueryCode = 0;
		std::uint32_t backendDispatchCode = 0;
		std::uint32_t backendAuxiliaryCode = 0;
		std::uint64_t successfulDispatchCount = 0;
		std::uint64_t failedDispatchCount = 0;
		std::uint64_t auxiliaryDispatchCount = 0;
		std::uint64_t gpuMemoryUsageBytes = 0;
		std::uint64_t gpuMemoryAliasableBytes = 0;
		std::int32_t jitterPhaseCount = 0;
		std::string featureVersion;
		std::string lastError;
	};

	const char* ToString(VansUpscalerBackend backend);
	const char* ToString(VansUpscaleQualityMode quality);
	const char* ToString(VansUpscalerFallbackReason reason);
}
