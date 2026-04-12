#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansFSR.h"
#include "../../VansTimer.h"
#include "../../RenderCore/VulkanCore/VansVKImage.h"
#include <iostream>

void VansGraphics::VansFSR::InitializeContext(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t renderWidth, uint32_t renderHeight, uint32_t displayWidth, uint32_t displayHeight)
{
	m_UpscalingContext = nullptr;

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
	backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;
	
	ffx::CreateContextDescUpscale createUpscaling{};
	createUpscaling.header.type   = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
	createUpscaling.header.pNext = nullptr;
	createUpscaling.maxUpscaleSize = { displayWidth, displayHeight };
	createUpscaling.maxRenderSize = { renderWidth, renderHeight };
	createUpscaling.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
	createUpscaling.fpMessage = nullptr;

	ffx::ReturnCode retCode = ffx::CreateContext(m_UpscalingContext, nullptr, createUpscaling,backendDesc);

	//std::cout << "FSR Upscaling context creation return code: " << static_cast<uint32_t>(retCode) << std::endl;

	m_TempFSRImage = new VansVKImage();
	m_TempFSRImage->CreateVulkanImage(
		device,
		{ displayWidth,displayHeight,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
	dispatchUpscale.exposure = ffxApiGetResourceVK(nullptr, FfxApiResourceDescription(), FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
	
	// Jitter is calculated earlier in the frame using a callback from the camera update
	dispatchUpscale.jitterOffset.x = -input.jitterX;
	dispatchUpscale.jitterOffset.y = -input.jitterY;
	dispatchUpscale.motionVectorScale.x = 1.0f;
	dispatchUpscale.motionVectorScale.y = 1.0f;
	dispatchUpscale.reset = false;
	dispatchUpscale.enableSharpening = true;
	dispatchUpscale.sharpness = 0.5;

	// Cauldron keeps time in seconds, but FSR expects milliseconds.
	dispatchUpscale.frameTimeDelta = static_cast<float>(VansTimer::GetDeltaTime() * 1000.0);

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

void VansGraphics::VansFSR::Cleanup()
{
	m_TempFSRImage->DestroyVulkanImage(m_Device);
	ffx::DestroyContext(m_UpscalingContext);
}
