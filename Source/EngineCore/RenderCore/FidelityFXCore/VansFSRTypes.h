#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

namespace VansGraphics
{
	enum class VansFSRMode : std::uint32_t
	{
		MatchViewport = 0,
		NativeAA = 1,
		Quality = 2,
		Balanced = 3,
		Performance = 4
	};

	enum class VansFSRResetReason : std::uint32_t
	{
		None = 0,
		FirstFrame = 1u << 0,
		SceneChange = 1u << 1,
		CameraCut = 1u << 2,
		ContextRecreated = 1u << 3,
		RenderSizeChange = 1u << 4,
		DisplaySizeChange = 1u << 5,
		ModeChange = 1u << 6,
		FrameDiscontinuity = 1u << 7,
		Manual = 1u << 8
	};

	constexpr VansFSRResetReason operator|(VansFSRResetReason lhs, VansFSRResetReason rhs)
	{
		return static_cast<VansFSRResetReason>(
			static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
	}

	inline VansFSRResetReason& operator|=(VansFSRResetReason& lhs, VansFSRResetReason rhs)
	{
		lhs = lhs | rhs;
		return lhs;
	}

	struct VansFSRFrameInput
	{
		VkImage color = VK_NULL_HANDLE;
		VkImageCreateInfo colorCreateInfo{};
		VkImage depth = VK_NULL_HANDLE;
		VkImageCreateInfo depthCreateInfo{};
		VkImage motionVectors = VK_NULL_HANDLE;
		VkImageCreateInfo motionVectorsCreateInfo{};

		VkImage reactive = VK_NULL_HANDLE;
		VkImageCreateInfo reactiveCreateInfo{};
		VkImage transparencyAndComposition = VK_NULL_HANDLE;
		VkImageCreateInfo transparencyAndCompositionCreateInfo{};
		VkImage exposure = VK_NULL_HANDLE;
		VkImageCreateInfo exposureCreateInfo{};

		std::uint32_t renderWidth = 0;
		std::uint32_t renderHeight = 0;
		std::uint32_t displayWidth = 0;
		std::uint32_t displayHeight = 0;

		float cameraFovAngleVerticalRadians = 0.0f;
		float cameraNear = 0.0f;
		float cameraFar = 0.0f;
		float viewSpaceToMetersFactor = 1.0f;

		float jitterPixelX = 0.0f;
		float jitterPixelY = 0.0f;
		float frameTimeDeltaMs = 16.6667f;
		float preExposure = 1.0f;

		bool reset = false;
	};

	struct VansFSRDiagnostics
	{
		bool contextReady = false;
		bool lastDispatchSucceeded = false;
		bool lastDispatchReset = false;
		bool debugCheckerEnabled = false;
		std::uint32_t lastCreateReturnCode = 0;
		std::uint32_t lastQueryReturnCode = 0;
		std::uint32_t lastDispatchReturnCode = 0;
		std::uint32_t lastReactiveReturnCode = 0;
		std::uint64_t successfulDispatchCount = 0;
		std::uint64_t failedDispatchCount = 0;
		std::uint64_t generatedReactiveMaskCount = 0;
		std::uint64_t gpuMemoryUsageBytes = 0;
		std::uint64_t gpuMemoryAliasableBytes = 0;
		std::int32_t jitterPhaseCount = 0;
		std::string lastError;
	};
}
