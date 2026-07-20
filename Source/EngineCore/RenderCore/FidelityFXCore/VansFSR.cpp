#include "VansFSR.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKImage.h"
#include <iostream>
#include <algorithm>

void VansGraphics::VansFSR::InitializeContext(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight)
{
	Cleanup();

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
	createUpscaling.header.type   = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
	createUpscaling.header.pNext = nullptr;
	createUpscaling.maxUpscaleSize = { displayWidth, displayHeight };
	createUpscaling.maxRenderSize = { renderWidth, renderHeight };
	// The current input is the tone-mapped post-process target. It is LDR,
	// un-pre-exposed, and its motion vectors are generated from unjittered
	// matrices. Do not advertise HDR/auto-exposure/jittered-MV semantics.
	createUpscaling.flags = 0;
	createUpscaling.fpMessage = nullptr;

	ffx::ReturnCode retCode = ffx::CreateContext(m_UpscalingContext, nullptr, createUpscaling,backendDesc);

	// 查询 FSR 内置抖动序列相位数量，与缩放比例相关
	ffxQueryDescUpscaleGetJitterPhaseCount jpc{};
	jpc.header.type      = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
	jpc.renderWidth      = renderWidth;
	jpc.displayWidth     = displayWidth;
	jpc.pOutPhaseCount   = &m_JitterPhaseCount;
	ffx::Query(m_UpscalingContext, jpc);

	//std::cout << "FSR Upscaling context creation return code: " << static_cast<uint32_t>(retCode) << std::endl;

	m_TempFSRImage = new VansVKImage();
	m_TempFSRImage->CreateVulkanImage(
		device,
		{ displayWidth,displayHeight,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,  // isCube
		false,  // need_raw_Data
		true    // combined_sampler — needed for ImGui scene-view sampling
	);
}

void VansGraphics::VansFSR::DispatchUpscale(VkCommandBuffer& commandBuffer, FSRInput& input)
{
	ffx::DispatchDescUpscale dispatchUpscale{};

	dispatchUpscale.commandList = commandBuffer;

	dispatchUpscale.color = ffxApiGetResourceVK(
		(void*)input.color, 
		ffxApiGetImageResourceDescriptionVK(input.color,input.colorCreateInfo,0), 
		FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	dispatchUpscale.depth = ffxApiGetResourceVK(
		(void*)input.depth, 
		ffxApiGetImageResourceDescriptionVK(input.depth,input.depthCreateInfo,0), 
		FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	dispatchUpscale.motionVectors = ffxApiGetResourceVK(
		(void*)input.motionVectors, 
		ffxApiGetImageResourceDescriptionVK(input.motionVectors, input.motionVectorsCreateInfo,0), 
		FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	dispatchUpscale.output = ffxApiGetResourceVK(
		(void*)(m_TempFSRImage->GetImage()),
		ffxApiGetImageResourceDescriptionVK(m_TempFSRImage->GetImage(), m_TempFSRImage->GetImageCreateInfo(), 0),
		FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	dispatchUpscale.reactive = ffxApiGetResourceVK(nullptr, FfxApiResourceDescription(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	dispatchUpscale.transparencyAndComposition = ffxApiGetResourceVK(nullptr, FfxApiResourceDescription(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	// The post-process input has already applied engine exposure and tone mapping.
	// FSR therefore receives neither an exposure resource nor pre-exposure.
	dispatchUpscale.exposure = ffxApiGetResourceVK(
		nullptr, FfxApiResourceDescription(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	
	// 抖动偏移：像素空间单位（Camera 已优先从 FSR 内置序列取得）
	dispatchUpscale.jitterOffset.x = -input.jitterPixelX;
	dispatchUpscale.jitterOffset.y = -input.jitterPixelY;
	// Engine MV is currentUV-previousUV, while FSR expects current-to-previous.
	// A negative scale converts both direction and UV units at the API boundary,
	// preserving the convention used by the engine's SSR/SSGI consumers.
	dispatchUpscale.motionVectorScale.x = -static_cast<float>(m_RenderWidth);
	dispatchUpscale.motionVectorScale.y = -static_cast<float>(m_RenderHeight);
	dispatchUpscale.reset = false;
	dispatchUpscale.enableSharpening = m_Sharpness > 0.0f;
	dispatchUpscale.sharpness = m_Sharpness;

	dispatchUpscale.frameTimeDelta = input.frameTimeDeltaMs;

	dispatchUpscale.preExposure = 1.0f;
	dispatchUpscale.renderSize.width = m_RenderWidth;
	dispatchUpscale.renderSize.height = m_RenderHeight;
	dispatchUpscale.upscaleSize.width = m_DisplayWidth;
	dispatchUpscale.upscaleSize.height = m_DisplayHeight;

	// Setup camera params as required
	dispatchUpscale.cameraFovAngleVertical = input.fovy;

	dispatchUpscale.cameraFar = input.farPlane;
	dispatchUpscale.cameraNear = input.nearPlane;
	dispatchUpscale.flags = 0;
	dispatchUpscale.viewSpaceToMetersFactor = 1;

	ffx::ReturnCode retCode = ffx::Dispatch(m_UpscalingContext, dispatchUpscale);

	//std::cout << "FSR Upscaling dispatch return code: " << static_cast<uint32_t>(retCode) << std::endl;
}

void VansGraphics::VansFSR::SetSharpness(float sharpness)
{
	m_Sharpness = std::max(0.0f, std::min(sharpness, 1.0f));
}

void VansGraphics::VansFSR::Cleanup()
{
	if (m_TempFSRImage)
	{
		m_TempFSRImage->DestroyVulkanImage(m_Device);
		delete m_TempFSRImage;
		m_TempFSRImage = nullptr;
	}

	if (m_UpscalingContext)
	{
		ffx::DestroyContext(m_UpscalingContext);
		m_UpscalingContext = nullptr;
	}
}

void VansGraphics::VansFSR::GetJitterOffset(int32_t index, float& outX, float& outY)
{
	ffxQueryDescUpscaleGetJitterOffset jq{};
	jq.header.type   = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
	jq.index         = index;
	jq.phaseCount    = m_JitterPhaseCount;
	jq.pOutX         = &outX;
	jq.pOutY         = &outY;
	ffx::Query(m_UpscalingContext, jq);
}
