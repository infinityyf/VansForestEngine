#include "VansFSR.h"

#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKImage.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <codecvt>
#include <cmath>
#include <locale>
#include <string>

namespace
{
	std::string NarrowSDKMessage(const wchar_t* message)
	{
		if (message == nullptr)
			return {};
		try
		{
			std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
			return converter.to_bytes(message);
		}
		catch (...)
		{
			return "<invalid UTF-16 message from FSR SDK>";
		}
	}

	void FSRMessageCallback(std::uint32_t type, const wchar_t* message)
	{
		const std::string text = NarrowSDKMessage(message);
		if (type == FFX_API_MESSAGE_TYPE_ERROR)
			VANS_LOG_ERROR("[FSR SDK] " << text);
		else
			VANS_LOG_WARN("[FSR SDK] " << text);
	}

	bool IsValidImageInput(
		VkImage image,
		const VkImageCreateInfo& createInfo,
		std::uint32_t requiredWidth,
		std::uint32_t requiredHeight)
	{
		return image != VK_NULL_HANDLE &&
			createInfo.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO &&
			createInfo.extent.width >= requiredWidth &&
			createInfo.extent.height >= requiredHeight &&
			createInfo.format != VK_FORMAT_UNDEFINED;
	}
}

namespace VansGraphics
{
	VansFSR::~VansFSR()
	{
		Cleanup();
	}

	bool VansFSR::InitializeContext(
		VkDevice device,
		VkPhysicalDevice physicalDevice,
		std::uint32_t renderWidth,
		std::uint32_t renderHeight,
		std::uint32_t displayWidth,
		std::uint32_t displayHeight)
	{
		Cleanup();
		m_Diagnostics = {};
		m_Diagnostics.debugCheckerEnabled = true;

		if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE ||
			renderWidth == 0 || renderHeight == 0 ||
			displayWidth < renderWidth || displayHeight < renderHeight)
		{
			m_Diagnostics.lastError = "invalid device or render/display extent";
			VANS_LOG_ERROR("[FSR] Context creation rejected: " << m_Diagnostics.lastError);
			return false;
		}

		m_RenderWidth = renderWidth;
		m_RenderHeight = renderHeight;
		m_DisplayWidth = displayWidth;
		m_DisplayHeight = displayHeight;
		m_Device = device;
		m_PhysicalDevice = physicalDevice;

		ffx::CreateBackendVKDesc backendDesc{};
		backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
		backendDesc.header.pNext = nullptr;
		backendDesc.vkDevice = device;
		backendDesc.vkPhysicalDevice = physicalDevice;
		backendDesc.vkDeviceProcAddr = VansVKDevice::GetDeviceProcAddr();

		ffx::CreateContextDescUpscale createUpscaling{};
		createUpscaling.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
		createUpscaling.header.pNext = nullptr;
		createUpscaling.maxUpscaleSize = { displayWidth, displayHeight };
		createUpscaling.maxRenderSize = { renderWidth, renderHeight };
		// SceneColor is upscaled in linear HDR. Exposure and display conversion
		// are deliberately applied by the following display-resolution pass.
		createUpscaling.flags =
			FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE |
			FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
		createUpscaling.fpMessage = FSRMessageCallback;

		const ffx::ReturnCode createResult =
			ffx::CreateContext(m_UpscalingContext, nullptr, createUpscaling, backendDesc);
		m_Diagnostics.lastCreateReturnCode = static_cast<std::uint32_t>(createResult);
		if (createResult != ffx::ReturnCode::Ok)
		{
			m_Diagnostics.lastError = "ffx::CreateContext failed";
			VANS_LOG_ERROR("[FSR] Context creation failed, code="
				<< m_Diagnostics.lastCreateReturnCode);
			m_UpscalingContext = nullptr;
			return false;
		}

		ffxQueryDescUpscaleGetJitterPhaseCount jitterQuery{};
		jitterQuery.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
		jitterQuery.renderWidth = renderWidth;
		jitterQuery.displayWidth = displayWidth;
		jitterQuery.pOutPhaseCount = &m_JitterPhaseCount;
		const ffx::ReturnCode queryResult = ffx::Query(m_UpscalingContext, jitterQuery);
		m_Diagnostics.lastQueryReturnCode = static_cast<std::uint32_t>(queryResult);
		if (queryResult != ffx::ReturnCode::Ok || m_JitterPhaseCount <= 0)
		{
			m_Diagnostics.lastError = "FSR jitter phase query failed";
			VANS_LOG_ERROR("[FSR] Jitter phase query failed, code="
				<< m_Diagnostics.lastQueryReturnCode);
			Cleanup();
			return false;
		}

		m_OutputImage = std::make_unique<VansVKImage>();
		if (!m_OutputImage->CreateVulkanImage(
			device,
			{ displayWidth, displayHeight, 1 },
			VK_FORMAT_R16G16B16A16_SFLOAT,
			1,
			1,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_SAMPLE_COUNT_1_BIT,
			false,
			false,
			true))
		{
			m_Diagnostics.lastError = "FSR output image creation failed";
			VANS_LOG_ERROR("[FSR] " << m_Diagnostics.lastError);
			Cleanup();
			return false;
		}

		auto createMask = [&](std::unique_ptr<VansVKImage>& image, const char* label)
		{
			image = std::make_unique<VansVKImage>();
			if (!image->CreateVulkanImage(
				device,
				{ renderWidth, renderHeight, 1 },
				VK_FORMAT_R8_UNORM,
				1,
				1,
				VK_IMAGE_TYPE_2D,
				VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
					VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_SAMPLE_COUNT_1_BIT,
				false,
				false,
				true,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
			{
				m_Diagnostics.lastError = std::string("FSR ") + label + " mask creation failed";
				return false;
			}
			return true;
		};
		if (!createMask(m_ReactiveMaskImage, "reactive") ||
			!createMask(m_TransparencyAndCompositionImage, "transparency/composition"))
		{
			VANS_LOG_ERROR("[FSR] " << m_Diagnostics.lastError);
			Cleanup();
			return false;
		}

		FfxApiEffectMemoryUsage memoryUsage{};
		ffx::QueryDescUpscaleGetGPUMemoryUsage memoryQuery{};
		memoryQuery.gpuMemoryUsageUpscaler = &memoryUsage;
		const ffx::ReturnCode memoryResult = ffx::Query(m_UpscalingContext, memoryQuery);
		m_Diagnostics.lastQueryReturnCode = static_cast<std::uint32_t>(memoryResult);
		if (memoryResult == ffx::ReturnCode::Ok)
		{
			m_Diagnostics.gpuMemoryUsageBytes = memoryUsage.totalUsageInBytes;
			m_Diagnostics.gpuMemoryAliasableBytes = memoryUsage.aliasableUsageInBytes;
		}

		m_Diagnostics.contextReady = true;
		m_Diagnostics.jitterPhaseCount = m_JitterPhaseCount;
		VANS_LOG("[FSR] Context ready: render=" << renderWidth << "x" << renderHeight
			<< " output=" << displayWidth << "x" << displayHeight
			<< " jitterPhases=" << m_JitterPhaseCount << " debugChecker=on");
		return true;
	}

	bool VansFSR::DispatchUpscale(VkCommandBuffer commandBuffer, const VansFSRFrameInput& input)
	{
		m_Diagnostics.lastDispatchSucceeded = false;
		m_Diagnostics.lastDispatchReset = input.reset;
		m_Diagnostics.lastError.clear();

		if (!m_Diagnostics.contextReady || m_UpscalingContext == nullptr ||
			m_OutputImage == nullptr || commandBuffer == VK_NULL_HANDLE)
		{
			m_Diagnostics.lastError = "FSR context/output/command buffer is not ready";
			++m_Diagnostics.failedDispatchCount;
			VANS_LOG_ERROR("[FSR] Dispatch rejected: " << m_Diagnostics.lastError);
			return false;
		}

		const bool dimensionsMatchContext =
			input.renderWidth == m_RenderWidth && input.renderHeight == m_RenderHeight &&
			input.displayWidth == m_DisplayWidth && input.displayHeight == m_DisplayHeight;
		const bool cameraValid =
			std::isfinite(input.cameraFovAngleVerticalRadians) &&
			input.cameraFovAngleVerticalRadians > 0.0f &&
			input.cameraFovAngleVerticalRadians < 3.14159265f &&
			std::isfinite(input.cameraNear) && std::isfinite(input.cameraFar) &&
			input.cameraNear > 0.0f && input.cameraFar > input.cameraNear;
		const bool timingValid =
			std::isfinite(input.frameTimeDeltaMs) && input.frameTimeDeltaMs > 0.0f &&
			std::isfinite(input.preExposure) && input.preExposure > 0.0f;
		const bool resourcesValid =
			IsValidImageInput(input.color, input.colorCreateInfo, m_RenderWidth, m_RenderHeight) &&
			IsValidImageInput(input.depth, input.depthCreateInfo, m_RenderWidth, m_RenderHeight) &&
			IsValidImageInput(input.motionVectors, input.motionVectorsCreateInfo, m_RenderWidth, m_RenderHeight);

		if (!dimensionsMatchContext || !cameraValid || !timingValid || !resourcesValid)
		{
			m_Diagnostics.lastError = "invalid per-frame dimensions, camera, timing, or resource metadata";
			++m_Diagnostics.failedDispatchCount;
			VANS_LOG_ERROR("[FSR] Dispatch input validation failed: render="
				<< input.renderWidth << "x" << input.renderHeight
				<< " output=" << input.displayWidth << "x" << input.displayHeight
				<< " fovRad=" << input.cameraFovAngleVerticalRadians
				<< " near=" << input.cameraNear << " far=" << input.cameraFar);
			return false;
		}

		ffx::DispatchDescUpscale dispatch{};
		dispatch.commandList = commandBuffer;
		dispatch.color = ffxApiGetResourceVK(
			reinterpret_cast<void*>(input.color),
			ffxApiGetImageResourceDescriptionVK(input.color, input.colorCreateInfo, 0),
			FFX_API_RESOURCE_STATE_COMPUTE_READ);
		dispatch.depth = ffxApiGetResourceVK(
			reinterpret_cast<void*>(input.depth),
			ffxApiGetImageResourceDescriptionVK(input.depth, input.depthCreateInfo, 0),
			FFX_API_RESOURCE_STATE_COMPUTE_READ);
		dispatch.motionVectors = ffxApiGetResourceVK(
			reinterpret_cast<void*>(input.motionVectors),
			ffxApiGetImageResourceDescriptionVK(input.motionVectors, input.motionVectorsCreateInfo, 0),
			FFX_API_RESOURCE_STATE_COMPUTE_READ);
		dispatch.output = ffxApiGetResourceVK(
			reinterpret_cast<void*>(m_OutputImage->GetImage()),
			ffxApiGetImageResourceDescriptionVK(
				m_OutputImage->GetImage(), m_OutputImage->GetImageCreateInfo(), 0),
			FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

		auto makeOptionalResource = [](VkImage image, const VkImageCreateInfo& createInfo)
		{
			if (image == VK_NULL_HANDLE)
				return ffxApiGetResourceVK(
					nullptr, FfxApiResourceDescription(), FFX_API_RESOURCE_STATE_COMPUTE_READ);
			return ffxApiGetResourceVK(
				reinterpret_cast<void*>(image),
				ffxApiGetImageResourceDescriptionVK(image, createInfo, 0),
				FFX_API_RESOURCE_STATE_COMPUTE_READ);
		};
		dispatch.reactive = makeOptionalResource(input.reactive, input.reactiveCreateInfo);
		dispatch.transparencyAndComposition = makeOptionalResource(
			input.transparencyAndComposition,
			input.transparencyAndCompositionCreateInfo);
		dispatch.exposure = makeOptionalResource(input.exposure, input.exposureCreateInfo);

		// Engine MV 保存 currentUV - previousUV，FSR API 边界统一反向并换算为像素。
		dispatch.jitterOffset.x = -input.jitterPixelX;
		dispatch.jitterOffset.y = -input.jitterPixelY;
		dispatch.motionVectorScale.x = -static_cast<float>(m_RenderWidth);
		dispatch.motionVectorScale.y = -static_cast<float>(m_RenderHeight);
		dispatch.reset = input.reset;
		dispatch.enableSharpening = m_Sharpness > 0.0f;
		dispatch.sharpness = m_Sharpness;
		dispatch.frameTimeDelta = input.frameTimeDeltaMs;
		dispatch.preExposure = input.preExposure;
		dispatch.renderSize = { input.renderWidth, input.renderHeight };
		dispatch.upscaleSize = { input.displayWidth, input.displayHeight };
		dispatch.cameraFovAngleVertical = input.cameraFovAngleVerticalRadians;
		dispatch.cameraFar = input.cameraFar;
		dispatch.cameraNear = input.cameraNear;
		dispatch.viewSpaceToMetersFactor = input.viewSpaceToMetersFactor;
		dispatch.flags = m_DebugViewEnabled
			? FFX_UPSCALE_FLAG_DRAW_DEBUG_VIEW
			: 0u;

		const ffx::ReturnCode result = ffx::Dispatch(m_UpscalingContext, dispatch);
		m_Diagnostics.lastDispatchReturnCode = static_cast<std::uint32_t>(result);
		if (result != ffx::ReturnCode::Ok)
		{
			m_Diagnostics.lastError = "ffx::Dispatch failed";
			++m_Diagnostics.failedDispatchCount;
			VANS_LOG_ERROR("[FSR] Dispatch failed, code=" << m_Diagnostics.lastDispatchReturnCode);
			return false;
		}

		m_OutputImage->SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
		m_Diagnostics.lastDispatchSucceeded = true;
		++m_Diagnostics.successfulDispatchCount;
		return true;
	}

	bool VansFSR::GenerateReactiveMask(
		VkCommandBuffer commandBuffer,
		VkImage opaqueOnly,
		const VkImageCreateInfo& opaqueOnlyCreateInfo,
		VkImage colorPreUpscale,
		const VkImageCreateInfo& colorPreUpscaleCreateInfo)
	{
		if (!m_Diagnostics.contextReady || m_UpscalingContext == nullptr ||
			m_ReactiveMaskImage == nullptr || commandBuffer == VK_NULL_HANDLE ||
			!IsValidImageInput(opaqueOnly, opaqueOnlyCreateInfo, m_RenderWidth, m_RenderHeight) ||
			!IsValidImageInput(colorPreUpscale, colorPreUpscaleCreateInfo, m_RenderWidth, m_RenderHeight))
		{
			m_Diagnostics.lastError = "invalid auto-reactive mask input";
			return false;
		}

		ffx::DispatchDescUpscaleGenerateReactiveMask reactive{};
		reactive.commandList = commandBuffer;
		reactive.colorOpaqueOnly = ffxApiGetResourceVK(
			reinterpret_cast<void*>(opaqueOnly),
			ffxApiGetImageResourceDescriptionVK(opaqueOnly, opaqueOnlyCreateInfo, 0),
			FFX_API_RESOURCE_STATE_COMPUTE_READ);
		reactive.colorPreUpscale = ffxApiGetResourceVK(
			reinterpret_cast<void*>(colorPreUpscale),
			ffxApiGetImageResourceDescriptionVK(colorPreUpscale, colorPreUpscaleCreateInfo, 0),
			FFX_API_RESOURCE_STATE_COMPUTE_READ);
		reactive.outReactive = ffxApiGetResourceVK(
			reinterpret_cast<void*>(m_ReactiveMaskImage->GetImage()),
			ffxApiGetImageResourceDescriptionVK(
				m_ReactiveMaskImage->GetImage(), m_ReactiveMaskImage->GetImageCreateInfo(), 0),
			FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
		reactive.renderSize = { m_RenderWidth, m_RenderHeight };
		reactive.scale = 1.0f;
		reactive.cutoffThreshold = 0.2f;
		reactive.binaryValue = 0.9f;
		reactive.flags = FFX_UPSCALE_AUTOREACTIVEFLAGS_APPLY_THRESHOLD |
			FFX_UPSCALE_AUTOREACTIVEFLAGS_USE_COMPONENTS_MAX;

		const ffx::ReturnCode result = ffx::Dispatch(m_UpscalingContext, reactive);
		m_Diagnostics.lastReactiveReturnCode = static_cast<std::uint32_t>(result);
		if (result != ffx::ReturnCode::Ok)
		{
			m_Diagnostics.lastError = "FSR auto-reactive dispatch failed";
			return false;
		}
		m_ReactiveMaskImage->SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
		++m_Diagnostics.generatedReactiveMaskCount;
		return true;
	}

	void VansFSR::SetSharpness(float sharpness)
	{
		m_Sharpness = std::clamp(sharpness, 0.0f, 1.0f);
	}

	void VansFSR::Cleanup()
	{
		m_Diagnostics.contextReady = false;
		if (m_OutputImage)
		{
			if (m_Device != VK_NULL_HANDLE)
				m_OutputImage->DestroyVulkanImage(m_Device);
			m_OutputImage.reset();
		}
		if (m_ReactiveMaskImage)
		{
			if (m_Device != VK_NULL_HANDLE)
				m_ReactiveMaskImage->DestroyVulkanImage(m_Device);
			m_ReactiveMaskImage.reset();
		}
		if (m_TransparencyAndCompositionImage)
		{
			if (m_Device != VK_NULL_HANDLE)
				m_TransparencyAndCompositionImage->DestroyVulkanImage(m_Device);
			m_TransparencyAndCompositionImage.reset();
		}

		if (m_UpscalingContext)
		{
			const ffx::ReturnCode result = ffx::DestroyContext(m_UpscalingContext);
			if (result != ffx::ReturnCode::Ok)
				VANS_LOG_ERROR("[FSR] Context destruction failed, code="
					<< static_cast<std::uint32_t>(result));
			m_UpscalingContext = nullptr;
		}

		m_JitterPhaseCount = 0;
		m_RenderWidth = 0;
		m_RenderHeight = 0;
		m_DisplayWidth = 0;
		m_DisplayHeight = 0;
		m_Device = VK_NULL_HANDLE;
		m_PhysicalDevice = VK_NULL_HANDLE;
	}

	bool VansFSR::GetJitterOffset(int32_t index, float& outX, float& outY)
	{
		if (!m_Diagnostics.contextReady || m_UpscalingContext == nullptr || m_JitterPhaseCount <= 0)
			return false;

		ffxQueryDescUpscaleGetJitterOffset query{};
		query.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
		query.index = index;
		query.phaseCount = m_JitterPhaseCount;
		query.pOutX = &outX;
		query.pOutY = &outY;
		const ffx::ReturnCode result = ffx::Query(m_UpscalingContext, query);
		m_Diagnostics.lastQueryReturnCode = static_cast<std::uint32_t>(result);
		return result == ffx::ReturnCode::Ok;
	}
}
