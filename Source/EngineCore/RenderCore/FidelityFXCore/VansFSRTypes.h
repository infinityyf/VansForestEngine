#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>
#include "../UpscalingCore/VansUpscalerTypes.h"

namespace VansGraphics
{
	struct VansFSRDispatchJitter
	{
		float x = 0.0f;
		float y = 0.0f;
	};

	constexpr VansFSRDispatchJitter BuildFSRDispatchJitter(
		const float samplePixelX,
		const float samplePixelY)
	{
		// FidelityFX 官方示例在把查询样本应用到相机后，以相反符号提交
		// dispatch jitterOffset。集中在 API 边界转换，避免相机侧混入 SDK 约定。
		return { -samplePixelX, -samplePixelY };
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

		float jitterSamplePixelX = 0.0f;
		float jitterSamplePixelY = 0.0f;
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
