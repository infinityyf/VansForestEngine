#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "VansVKMemoryManager.h"
#include "VansVKDescriptorManager.h"
#include "VansRenderPass.h"
#include "VansMesh.h"
#include "VansShader.h"


#include "../VansScene.h"
#include "../../Configration/VansConfigration.h"
//#if defined FOREST_EDITOR
#include "../../EditorCore/VansEditorWindow.h"
#include "../../VansTimer.h"
#include <iostream>




namespace VansGraphics
{
	std::vector<const char*> RayTracingDeviceExtensions =
	{
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	};

	//get call avaliable extensions
	bool VansVKDevice::CheckAvaliableInstanceExtensions(std::vector<VkExtensionProperties>& available_extensions)
	{
		uint32_t extensions_count = 0;
		VkResult result = VK_SUCCESS;
		result = vkEnumerateInstanceExtensionProperties(nullptr, &extensions_count, nullptr);
		if ((result != VK_SUCCESS) || (extensions_count == 0))
		{
			std::cout << "Could not get the number of Instance extensions." <<
				std::endl;
			return false;
		}

		available_extensions.resize(extensions_count);
		result = vkEnumerateInstanceExtensionProperties(nullptr, &extensions_count, &available_extensions[0]);
		if ((result != VK_SUCCESS) || (extensions_count == 0))
		{
			std::cout << "Could not enumerate Instance extensions." << std::endl;
			return false;
		}
		return true;
	}

	bool VansVKDevice::CheckAvaliableInstanceLayer(std::vector<VkLayerProperties>& available_layers)
	{
		uint32_t layer_count;
		VkResult result = VK_SUCCESS;
		result = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
		if ((result != VK_SUCCESS) || (layer_count == 0))
		{
			std::cout << "Could not get the number of Instance layers." <<
				std::endl;
			return false;
		}
		available_layers.resize(layer_count);
		result = vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
		if ((result != VK_SUCCESS) || (layer_count == 0))
		{
			std::cout << "Could not enumerate Instance extensions." << std::endl;
			return false;
		}
		return true;
	}

	bool VansVKDevice::IsExtensionSupported(const std::vector<VkExtensionProperties>& available_extensions, char const* desire_extension)
	{
		for (auto& extension : available_extensions)
		{
			if (strcmp(extension.extensionName, desire_extension) == 0)
			{
				std::cout << "Extension named '" << desire_extension << "' is supported."
					<< std::endl;
				return true;
			}
		}
		std::cout << "Extension named '" << desire_extension << "' is not supported."
			<< std::endl;
		return false;
	}

	bool VansVKDevice::IsLayersSupported(const std::vector<VkLayerProperties>& available_layers, char const* desire_layer)
	{
		for (auto& layer : available_layers)
		{
			if (strcmp(layer.layerName, desire_layer) == 0)
			{
				std::cout << "Layer named '" << desire_layer << "' is supported."
					<< std::endl;
				return true;
			}
		}
		std::cout << "Layer named '" << desire_layer << "' is not supported."
			<< std::endl;
		return false;
	}

	void VansVKDevice::RequestDeviceQueue(uint32_t queue_family_index, uint32_t queue_index, VkQueue& queue)
	{
		vkGetDeviceQueue(m_VansVKLogicDevice, queue_family_index, queue_index, &queue);
	}

	bool VansVKDevice::CheckAvaliableDeviceExtensions(VkPhysicalDevice device,std::vector<VkExtensionProperties>& available_extensions)
	{
		uint32_t extensions_count = 0;
		VkResult result = VK_SUCCESS;
		result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensions_count, nullptr);
		if ((result != VK_SUCCESS) || (extensions_count == 0))
		{
			std::cout << "Could not get the number of device extensions." << std::endl;
			return false;
		}


		available_extensions.resize(extensions_count);
		result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensions_count, &available_extensions[0]);
		if ((result != VK_SUCCESS) || (extensions_count == 0))
		{
			std::cout << "Could not enumerate device extensions." << std::endl;
			return false;
		}
		return true;
	}

	bool VansVKDevice::CheckAvalialeDeviceQueue(VkPhysicalDevice device, uint32_t& queue_family_index, VkQueueFlags desired_capabilty)
	{
		
		//check queue famliy
		uint32_t queue_families_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, nullptr);
		if (queue_families_count == 0)
		{
			std::cout << "Could not get the number of queue families." << std::endl;
			return false;
		}

		std::vector<VkQueueFamilyProperties> queue_families;
		queue_families.resize(queue_families_count);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, &queue_families[0]);
		if (queue_families_count == 0)
		{
			std::cout << "Could not acquire properties of queue families." << std::endl;
			return false;
		}

		for (uint32_t index = 0; index < static_cast<uint32_t>(queue_families.size()); ++index)
		{
			//check device support present surface queue
			if ((desired_capabilty & VK_QUEUE_GRAPHICS_BIT) != 0)
			{
				VkBool32 present_surface_support = VK_FALSE;
				VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, index, m_VansVKSurface.m_VansVKPresentSurface, &present_surface_support);
				if (result != VK_SUCCESS && present_surface_support != VK_TRUE)
				{
					continue;
				}
			}

			if ((queue_families[index].queueCount > 0) &&
				(queue_families[index].queueFlags & desired_capabilty))
			{
				queue_family_index = index;
				return true;
			}
		}
		return false;
	}

	bool VansVKDevice::CheckPhysicDeviceFeature(VkPhysicalDevice device)
	{
		//使用1.2
		m_Features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		m_Features12.pNext = nullptr;

		m_Features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
		m_Features11.pNext = &m_Features12;

		auto vansConfigration = VansConfigration::GetInstance();
		if (vansConfigration->GetSupportRayTracing())
		{
			m_RaytracingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
			m_RaytracingFeature.pNext = nullptr;

			m_AcceralteFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
			m_AcceralteFeature.pNext = &m_RaytracingFeature;

			//static VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{};
			//bufferDeviceAddress.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
			//bufferDeviceAddress.pNext = &acceralteFeature;

			//static VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexing = { };
			//descriptorIndexing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
			//descriptorIndexing.pNext = &bufferDeviceAddress;
			m_Features12.pNext = &m_AcceralteFeature;
		}

		//支持struct
		m_ScalarBlockFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
		m_ScalarBlockFeature.scalarBlockLayout = VK_TRUE;
		m_ScalarBlockFeature.pNext = &m_Features11;


		m_DeviceFeatures2.pNext = &m_ScalarBlockFeature;
		m_DeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

		m_AccelerationProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
		m_AccelerationProps.pNext = nullptr;

		m_RayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
		m_RayTracingProperties.pNext = &m_AccelerationProps;

		m_DeviceProperties2.pNext = &m_RayTracingProperties;
		m_DeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		//set the feature we need for create device
		vkGetPhysicalDeviceFeatures2(device, &m_DeviceFeatures2);
		vkGetPhysicalDeviceProperties2(device, &m_DeviceProperties2);
		return true;
	}


	bool VansVKDevice::WaitForQueue(VkQueue queue)
	{
		VkResult result = vkQueueWaitIdle(queue);
		if (VK_SUCCESS != result)
		{
			std::cout << "Waiting for all operations submitted to queue failed." << std::endl;
			return false;
		}
		return true;
	}

	bool VansVKDevice::WaitForDevice()
	{
		VkResult result = vkDeviceWaitIdle(m_VansVKLogicDevice);
		if (VK_SUCCESS != result) 
		{
			std::cout << "Waiting on a device failed." << std::endl;
			return false;
		}
		return true;
	}

	bool VansVKDevice::SetDeviceBufferData(VansVKBuffer& dest_buffer, void* data, int data_offset, int data_size, VkDeviceSize buffer_offset, VkDeviceSize buffer_size)
	{
		m_StageBuffer.SetBufferData(data, data_offset, data_size);

		//通过command buffer切换buffer的usage

		if (!m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			return false;
		}

		//设置在pipeline中的同步点
		dest_buffer.SetBufferMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				dest_buffer.m_VansVKBuffer, 
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT, 
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED 
			} 
		);

		VansVKMemoryManager::CopyBufferData(m_VansVKCommandBuffer, m_StageBuffer, dest_buffer, { { VkDeviceSize(0),buffer_offset, buffer_size } });

		dest_buffer.SetBufferMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				dest_buffer.m_VansVKBuffer,
				VK_ACCESS_TRANSFER_WRITE_BIT, 
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_QUEUE_FAMILY_IGNORED, 
				VK_QUEUE_FAMILY_IGNORED
			}
		);
		if (!m_VansVKCommandBuffer.EndCommandBufferRecord())
		{
			return false;
		}


		//std::vector<WaitSemaphoreInfo> wait_semaphore_infos;
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, {m_VansVKCommandBuffer.GetVKCommandBuffer()}, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
		return true;
	}

	bool VansVKDevice::SetDeviceImageData(VansVKImage& dest_image, void* data, int data_offset, int data_size, VkOffset3D image_offset, VkExtent3D image_size, int mip_level, int layer_level)
	{
		m_StageBuffer.SetBufferData(data, data_offset, data_size);

		//通过command buffer切换buffer的usage

		if (!m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			return false;
		}

		//设置在pipeline中的同步点
		dest_image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				dest_image.m_VansVKImage,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				dest_image.m_ImageLayout,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				dest_image.m_ImageAspect
			}
		);

		VkImageSubresourceLayers destination_image_subresource = 
		{
			dest_image.m_ImageAspect,
			mip_level,
			layer_level,
			1
		};

		VansVKMemoryManager::CopyBufferToImage(m_VansVKCommandBuffer, m_StageBuffer, dest_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
		{
			{
				0,
				0,
				0,
				destination_image_subresource,
				image_offset,
				image_size,
			} 
		});

		dest_image.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				dest_image.m_VansVKImage,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				dest_image.m_ImageLayout,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				dest_image.m_ImageAspect
			}
		);

		if (!m_VansVKCommandBuffer.EndCommandBufferRecord())
		{
			return false;
		}

		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
		return true;
	}

	void VansVKDevice::PrepareFSRDispatchInputData(float fovy, float nearPlane, float farPlane)
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		auto& depth = renderPassManager->GetDepth();
		auto& motionVector = renderPassManager->GetMotionVector();
		auto& colorAfterPostProcess = renderPassManager->GetColorAfterPostProcess();
		m_FSRInput.color = colorAfterPostProcess.GetImage();
		m_FSRInput.colorCreateInfo = colorAfterPostProcess.GetImageCreateInfo();
		m_FSRInput.depth = depth.GetImage();
		m_FSRInput.depthCreateInfo = depth.GetImageCreateInfo();
		m_FSRInput.motionVectors = motionVector.GetImage();
		m_FSRInput.motionVectorsCreateInfo = motionVector.GetImageCreateInfo();

		m_FSRInput.fovy = fovy;
		m_FSRInput.nearPlane = nearPlane;
		m_FSRInput.farPlane = farPlane;
	}

	void VansVKDevice::DispatchFSRUpscale()
	{
		//获取jitter
		auto camera = m_Scene->GetCamera();
		m_FSRInput.jitterX = camera->m_JitterX;
		m_FSRInput.jitterY = camera->m_JitterY;

		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_FSRController.DispatchUpscale(m_VansVKCommandBuffer.GetVKCommandBuffer(), m_FSRInput);

		//blit to swapchain image
		// Destination: swapchain image (assumed GENERAL or PRESENT). Force to TRANSFER_DST_OPTIMAL.
		VkExtent2D swapchainExtent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
		VkExtent2D fsrTempImageExtent = m_FSRController.GetDisplayExtent();
		m_VansVKSurface.SetSwapChainImageBarrier(
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
				VK_ACCESS_NONE,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, // treat as unknown, transition regardless
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				VK_IMAGE_ASPECT_COLOR_BIT
			},
			m_SwapChainImageIndex);

		// Blit region (entire image)
		VkImageBlit blitRegion{};
		blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blitRegion.srcSubresource.mipLevel = 0;
		blitRegion.srcSubresource.baseArrayLayer = 0;
		blitRegion.srcSubresource.layerCount = 1;
		blitRegion.srcOffsets[0] = { 0, 0, 0 };
		blitRegion.srcOffsets[1] = { (int32_t)fsrTempImageExtent.width, (int32_t)fsrTempImageExtent.height, 1 };

		blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blitRegion.dstSubresource.mipLevel = 0;
		blitRegion.dstSubresource.baseArrayLayer = 0;
		blitRegion.dstSubresource.layerCount = 1;
		blitRegion.dstOffsets[0] = { 0, 0, 0 };
		blitRegion.dstOffsets[1] = { (int32_t)swapchainExtent.width, (int32_t)swapchainExtent.height, 1 };

		vkCmdBlitImage(
			m_VansVKCommandBuffer.GetVKCommandBuffer(),
			m_FSRController.GetTempFSRImage().GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&blitRegion,
			VK_FILTER_LINEAR);

		// Transition swapchain image to PRESENT_SRC_KHR for presentation
		m_VansVKSurface.SetSwapChainImageBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			{
				m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_NONE,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				VK_IMAGE_ASPECT_COLOR_BIT
			},
			m_SwapChainImageIndex);

		m_VansVKCommandBuffer.EndCommandBufferRecord();

		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
	}

	void VansVKDevice::InitializeFSR()
	{
		//这里应获取交换链的分辨率
		VkExtent2D swapChainExtent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
		m_FSRController.InitializeContext(m_VansVKLogicDevice, m_VansVKPhysicalDevice, m_RenderWidth, m_RenderHeight, swapChainExtent.width, swapChainExtent.height);
	}

	void VansVKDevice::CleanupFSR()
	{
		m_FSRController.Cleanup();
	}

	void VansVKDevice::BeforeRendering()
	{
		//创建信号量
		CreateVKSemaphore(m_SwapChainImageAcquiredSemaphore);
		CreateVKSemaphore(m_CommandBufferReadyToPresentSemaphore);

		//用于确保写入的image已经acquire完成
		CreateVKFence(false, m_SwapChainImageAcquiredFence);

		auto renderPassManager = VansRenderPassManager::GetInstance();

		//create renderpass,and frame buffer
		//这里自动创建color和depth
		renderPassManager->SetupVansDeferredRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, m_VansVKSurface, {m_RenderWidth, m_RenderHeight});
		//renderPassManager->SetupVansRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer , m_VansVKGraphicsQueue, m_VansVKSurface);

		//创建阴影pass
		renderPassManager->SetupVansShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);

		//创建精确阴影pass
		renderPassManager->SetupVansPunctualShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);

		//预计算渲染数据
		PrepareRenderingData();

		//加载场景数据
		m_Scene->LoadScene("D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Scenebk.json");
		
		//准备光线追踪数据
		PrepareRayTracingData();

		//准备fsr数据
		InitializeFSR();
		PrepareFSRDispatchInputData(3.14f / 2, 0.01f, 100.0f);
	}

	void VansVKDevice::Rendering()
	{
		//获取swapchain image
		bool requireImage = m_VansVKSurface.AcquireVulkanSwapChainImages(m_VansVKLogicDevice, m_SwapChainImageIndex, m_SwapChainImageAcquiredSemaphore, m_SwapChainImageAcquiredFence);
		if (!requireImage)
		{
			std::cout << "AcquireVulkanSwapChainImages failed" << std::endl;
		}

		//更新场景数据
		//灯光数据
		m_Scene->UpdateSceneData();

		//开始record渲染指令
		auto renderPassManager = VansRenderPassManager::GetInstance();

		//record command buffer
		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		
		VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();

		//绘制阴影
		renderPassManager->BeginRenderPass(renderPassManager->m_VansShadowPass, cmd, m_globalRenderStateData);
		DrawShadowMap(renderPassManager, cmd);
		renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);

		////绘制精确阴影
		//renderPassManager->BeginRenderPass(renderPassManager->m_VansPunctualShadowPass, cmd, m_globalRenderStateData);
		//DrawPunctualShadowMap(renderPassManager, cmd);
		//renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);

		//计算上一帧的HIZ数据
		UpdateHZB(renderPassManager);

		//计算上一帧的ssGI数据
		UpdateGIData(renderPassManager);

		//计算上一帧SSR
		UpdateSSR(renderPassManager);

		//光线追踪
		UpdateRayTracing();


		UpdateVolumetricFog(renderPassManager);

		renderPassManager->BeginRenderPass(renderPassManager->m_VansRenderPass ,cmd, m_globalRenderStateData, m_SwapChainImageIndex);
		DrawSceneDeferred(renderPassManager, cmd);
		//DrawSceneForward(renderPassManager, cmd);
	}

	void VansVKDevice::Present()
	{
		VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();
		auto renderPassManager = VansRenderPassManager::GetInstance();

		//结束Renderpass
		renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);

		//end record
		m_VansVKCommandBuffer.EndCommandBufferRecord();

		//确保image已经从swapchain上acquired成功
		//这里先获取之前设置的同步信息
		std::vector<WaitSemaphoreInfo> wait_semaphore_infos = {};//wait_infos;
		wait_semaphore_infos.push_back(
			{
				m_SwapChainImageAcquiredSemaphore,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			}
		);

		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, { wait_semaphore_infos }, { m_CommandBufferReadyToPresentSemaphore }, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);


		//auto camera = m_Scene->GetCamera();
		//if (camera->GetFrameIndex() % 2 == 0)
		//{
		//	//将结果blit到swapchain image上,如果开启fsr这里插入fsr
		//	renderPassManager->BlitToSwapChainImage(m_VansVKCommandBuffer, m_VansVKSurface, m_SwapChainImageIndex, { m_RenderWidth, m_RenderHeight });

		//	VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {  }, { }, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		//	m_VansVKCommandBuffer.ResetCommandBuffer(false);
		//}
		//else
		{
			DispatchFSRUpscale();
		}

		//并进行present
		m_VansVKSurface.PresentImage(m_VansVKLogicDevice, m_VansVKGraphicsQueue, { m_CommandBufferReadyToPresentSemaphore }, m_SwapChainImageIndex);

		//重置imgae layout
		renderPassManager->ResetFrameBufferImageLayout(m_VansVKCommandBuffer, m_VansVKSurface, m_SwapChainImageIndex);
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
	}

	void VansVKDevice::AfterRendering()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->DestroyRenderPass();

		DestroyVKSemaphore(m_SwapChainImageAcquiredSemaphore);
		DestroyVKSemaphore(m_CommandBufferReadyToPresentSemaphore);
		DestroyVKFence(m_SwapChainImageAcquiredFence);

	}

	void* VansVKDevice::GetNativeGraphicsDevice()
	{
		return &m_VansVKLogicDevice;
	}

	void* VansVKDevice::GetNativeCommandBuffer()
	{
		return &(m_VansVKCommandBuffer.GetVKCommandBuffer());
	}

	bool VansVKDevice::CreateVKFence(bool signaled, VkFence& fence)
	{
		VkFenceCreateInfo fence_create_info = 
		{
			 VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			 nullptr,
			 signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0
		};

		VkResult result = vkCreateFence(m_VansVKLogicDevice, &fence_create_info, nullptr, &fence);
		if (VK_SUCCESS != result) 
		{
			std::cout << "Could not create a fence." << std::endl;
			return false;
		}
		return true;
	}

	bool VansVKDevice::CreateVKSemaphore(VkSemaphore& semaphore)
	{
		VkSemaphoreCreateInfo semaphore_create_info = 
		{
			 VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
			 nullptr,
			 0
		};

		VkResult result = vkCreateSemaphore(m_VansVKLogicDevice, &semaphore_create_info, nullptr, &semaphore);
		if (VK_SUCCESS != result) 
		{
			std::cout << "Could not create a semaphore." << std::endl;
			return false;
		}
		return true;
	}

	void VansVKDevice::DestroyVKFence(VkFence& fence)
	{
		vkDestroyFence(m_VansVKLogicDevice, fence, nullptr);
	}

	void VansVKDevice::DestroyVKSemaphore(VkSemaphore& semaphore)
	{
		vkDestroySemaphore(m_VansVKLogicDevice, semaphore, nullptr);
	}

	bool VansVKDevice::PrepareVulkanLibrary()
	{
		if (!LoadVulkanLibrary())
		{
			return false;
		}
		if (!LoadVulkanExportedFunction())
		{
			return false;
		}
		if (!LoadVulkanGlobalLevelFunctions())
		{
			return false;
		}


		return true;
	}
	bool VansVKDevice::CreateVulkanInstance(std::vector<char const*>& desired_extensions, std::vector<char const*>& desired_layers)
	{
		uint32_t apiVersion = 0;
		vkEnumerateInstanceVersion(&apiVersion);
		printf("Vulkan API version: %d.%d.%d\n",
			VK_VERSION_MAJOR(apiVersion),
			VK_VERSION_MINOR(apiVersion),
			VK_VERSION_PATCH(apiVersion));

		std::vector<VkExtensionProperties> available_extensions;
		if (!CheckAvaliableInstanceExtensions(available_extensions))
		{
			return false;
		}

		for (auto& extension : desired_extensions)
		{
			if (!IsExtensionSupported(available_extensions, extension))
			{
				return false;
			}
		}

		//检查layers
		std::vector<VkLayerProperties> avaliable_layers;
		if (!CheckAvaliableInstanceLayer(avaliable_layers))
		{
			return false;
		}

		for (auto& layer : desired_layers)
		{
			if (!IsLayersSupported(avaliable_layers, layer))
			{
				return false;
			}
		}

		//application info 
		VkApplicationInfo application_info =
		{
			 VK_STRUCTURE_TYPE_APPLICATION_INFO,
			 nullptr,
			 "ForestEngine",
			 VK_MAKE_VERSION(1, 0, 0),
			 "ForestEngine",
			 VK_MAKE_VERSION(1, 0, 0),
			 VK_MAKE_VERSION(1, 2, 0)//api level
		};

		VkInstanceCreateInfo instance_create_info =
		{
			 VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			 nullptr, // instance pNext
			 0,
			 &application_info,
			 static_cast<uint32_t>(desired_layers.size()),
			 desired_layers.size() > 0 ? &desired_layers[0] : nullptr,
			 static_cast<uint32_t>(desired_extensions.size()),
			 desired_extensions.size() > 0 ? &desired_extensions[0] : nullptr
		};

		VkResult result = vkCreateInstance(&instance_create_info, nullptr, &m_VansVKInstance);
		if ((result != VK_SUCCESS) || (m_VansVKInstance == VK_NULL_HANDLE))
		{
			std::cout << "Could not -create Vulkan Instance." << std::endl;
			return false;
		}
		return true;
	}

	bool VansVKDevice::CreateVulkanLogicDevice(std::vector<char const*>& desired_extensions)
	{
		uint32_t devices_count = 0;
		VkResult result = VK_SUCCESS;
		result = vkEnumeratePhysicalDevices(m_VansVKInstance, &devices_count, nullptr);
		if ((result != VK_SUCCESS) || (devices_count == 0))
		{
			std::cout << "Could not get the number of available physical devices." << std::endl;
			return false;
		}

		std::vector<VkPhysicalDevice> available_devices;
		available_devices.resize(devices_count);
		result = vkEnumeratePhysicalDevices(m_VansVKInstance, &devices_count, &available_devices[0]);
		if ((result != VK_SUCCESS) || (devices_count == 0))
		{
			std::cout << "Could not enumerate physical devices." << std::endl;
			return false;
		}

		//select desire device
		for (auto& device : available_devices)
		{
			//check device extension
			std::vector<VkExtensionProperties> available_extensions;
			if (!CheckAvaliableDeviceExtensions(device, available_extensions))
			{
				continue;
			}
			
			bool allExtensionsSupport = true;
			for (auto& extension : desired_extensions)
			{
				if (!IsExtensionSupported(available_extensions, extension))
				{
					allExtensionsSupport = false;
					break;
				}
			}
			if (!allExtensionsSupport)
			{
				continue;
			}


			//check feature
			if (!CheckPhysicDeviceFeature(device))
			{
				continue;
			}
			
			//check queuefamily type support
			if (!CheckAvalialeDeviceQueue(device, m_GraphicsQueueFamilyIndex, VK_QUEUE_GRAPHICS_BIT))
			{
				continue;
			}

			if (!CheckAvalialeDeviceQueue(device, m_ComputeQueueFamilyIndex, VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))
			{
				continue;
			}

			//check device surface support
			//present need a special queue family


			//recored all need queue family index
			std::vector<QueueInfo> queue_infos;
			queue_infos.push_back({ m_GraphicsQueueFamilyIndex, { 1.0f } });
			if (m_GraphicsQueueFamilyIndex != m_ComputeQueueFamilyIndex)
			{
				queue_infos.push_back({ m_ComputeQueueFamilyIndex, { 1.0f } });
			}

			m_VansVKPhysicalDevice = device;
			//create queue
			std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
			for (auto& info : queue_infos)
			{
				queue_create_infos.push_back(
				{
					VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					nullptr,
					0,
					info.FamilyIndex,
					static_cast<uint32_t>(info.Priorities.size()),
					info.Priorities.size() > 0 ? &info.Priorities[0] : nullptr
				});
			};

			//create logical device
			//device has layers , externsions and features
			// Vulkan >= 1.1 uses pNext to enable features, and not pEnabledFeatures
			VkDeviceCreateInfo device_create_info =
			{
				 VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				 &m_DeviceFeatures2,
				 0,
				 static_cast<uint32_t>(queue_create_infos.size()),
				 queue_create_infos.size() > 0 ? &queue_create_infos[0] : nullptr,
				 0,
				 nullptr,
				 static_cast<uint32_t>(desired_extensions.size()),
				 desired_extensions.size() > 0 ? &desired_extensions[0] : nullptr,
				 nullptr// &availableDeviceFeatures
			};

			VkResult result = vkCreateDevice(device, &device_create_info, nullptr, &m_VansVKLogicDevice);
			if ((result != VK_SUCCESS) || (m_VansVKLogicDevice == VK_NULL_HANDLE))
			{
				std::cout << "Could not create logical device." << std::endl;
				return false;
			}

			return true;
		}
		return false;
	}

	bool VansVKDevice::InitVulkanLogicDevice()
	{
		//request queue
		RequestDeviceQueue(m_GraphicsQueueFamilyIndex, 0, m_VansVKGraphicsQueue);
		RequestDeviceQueue(m_ComputeQueueFamilyIndex, 0, m_VansVKComputeQueue);
		
		//init memory manager
		//需要创建一个command buffer
		//应该和swapchain queire的image数量一致
		CommandBufferCreateParams params = 
		{
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			1
		};
		bool result = m_VansVKCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params);
		if (!result)
		{
			std::cout << "create m_VansVKCommandBuffer failed" << std::endl;
			return false;
		}

		result = m_VansVKRayTracingCommandBuffer.CreateVulkanCommandBuffer(*this, m_ComputeQueueFamilyIndex, params);
		if (!result)
		{
			std::cout << "create m_VansVKRayTracingCommandBuffer failed" << std::endl;
			return false;
		}

		//创建fence,用于等待command buffer执行完成
		CreateVKFence(false,VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		CreateVKFence(false,VansVKCommandBuffer::m_RayTracingCommandBufferFinishSubmitFence);

		VansVKMemoryManager::GetInstance()->BindDevice(m_VansVKCommandBuffer.GetVKCommandBuffer() , *this);
		VansVKDescriptorManager::GetInstance()->BindDevice(m_VansVKPhysicalDevice, m_VansVKLogicDevice, m_VansVKCommandBuffer.GetVKCommandBuffer());
		VansVKDescriptorManager::GetInstance()->CreateDescriptorPool(true);

		

		//创建stage buffer用于上传数据
		//VK_MEMORY_PROPERTY_HOST_COHERENT_BIT这个标识不需要flush这个memoryrange就可以让驱动知道这个内存被更改了
		m_StageBuffer.CreatVulkanBuffer(m_VansVKLogicDevice, 1024 * 1024 * 512, VK_FORMAT_R32_SFLOAT,VK_BUFFER_USAGE_TRANSFER_SRC_BIT| VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		return true;
	}

	bool VansVKDevice::DestroyVulkanLogicDevice()
	{

		VansVKDescriptorManager::GetInstance()->DestroyDescriptorPool();

		//销毁stage buffer
		m_StageBuffer.DestroyVulkanBuffer(m_VansVKLogicDevice);

		//销毁command buffer
		m_VansVKCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansVKRayTracingCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);

		DestroyVKFence(VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		DestroyVKFence(VansVKCommandBuffer::m_RayTracingCommandBufferFinishSubmitFence);

		if (m_VansVKLogicDevice)
		{
			vkDestroyDevice(m_VansVKLogicDevice, nullptr);
			m_VansVKLogicDevice = VK_NULL_HANDLE;
		}
		return true;
	}

	bool VansVKDevice::DestroyVulkanInstance()
	{
		if (m_VansVKInstance)
		{
			vkDestroyInstance(m_VansVKInstance, nullptr);
			m_VansVKInstance = VK_NULL_HANDLE;
		}
		return true;
	}

	bool VansVKDevice::VulkanSetUp(VkExtent2D resolution)
	{
		//load lib and function
		if (!PrepareVulkanLibrary())
		{
			return false;
		}

		//get WSI externsion before create instance
		std::vector<char const*> desired_instance_extrensions =
		{
			//用于增强查询物理设备特性、设备属性和格式属性的能力。它通过引入可扩展的结构链机制，使得应用程序可以更灵活地获取设备信息，并在设备创建时启用特定功能
			VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
			VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WIN32_KHR
			VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined VK_USE_PLATFORM_XCB_KHR
			VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined VK_USE_PLATFORM_XLIB_KHR
			VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
#ifdef _DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
		};

		std::vector<char const*> desired_instance_layers =
		{
//#ifdef _DEBUG
//			"VK_LAYER_KHRONOS_validation",
//#endif
		};

		auto vansConfigration = VansConfigration::GetInstance();
#ifdef _DEBUG
		//ray tracing 和 renderdoc是冲突的，同时开启不能创建device
		if (!vansConfigration->GetSupportRayTracing())
		{
			desired_instance_layers.push_back("VK_LAYER_RENDERDOC_Capture");
		}
#endif

		//create instance
		if (!CreateVulkanInstance(desired_instance_extrensions, desired_instance_layers))
		{
			return false;
		}

		//load instance level funxtions
		if (!LoadVulkanInstanceLevelFunctions(m_VansVKInstance))
		{
			return false;
		}

		//load instance level externsion functions， needs loaded before create surface
		if (!LoadVulkanInstanceLevelFunctionFromExtension(m_VansVKInstance, desired_instance_extrensions))
		{
			return false;
		}

		//Create surface
		//#if defined FOREST_EDITOR
		if (!m_VansVKSurface.CreateVulkanPresentSurface(m_VansVKInstance, VansGraphics::VansEditorWindow::m_VansEditorWindow.m_VansGraphicsHandle))
		{
			return false;
		}

		//create device
		//check device extension
		//check device feature 
		//check device queue family
		//check device surface support
		std::vector<char const*> desired_device_extrensions = 
		{
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_MAINTENANCE1_EXTENSION_NAME,
			VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
			VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
		};

		
		if (vansConfigration->GetSupportRayTracing())
		{
			desired_device_extrensions.insert(desired_device_extrensions.end(), RayTracingDeviceExtensions.begin(), RayTracingDeviceExtensions.end());
		}

		if (!CreateVulkanLogicDevice(desired_device_extrensions))
		{
			return false;
		}

		//load device level funtion
		if (!LoadVulkanDeviceLevelFunctions(m_VansVKLogicDevice))
		{
			return false;
		}

		//load device enable extension function
		//support swapchain externsion
		//swapchain containes images , we just aquire from it
		if (!LoadVulkanDeviceLevelFunctionFromExtension(m_VansVKLogicDevice, desired_device_extrensions))
		{
			return false;
		}

		//create swap chain
		if (!m_VansVKSurface.CreateVulkanSwapChain(m_VansVKPhysicalDevice, m_VansVKLogicDevice))
		{
			return false;
		}

		//Init device
		m_RawResolution = resolution;
		if (!InitVulkanLogicDevice())
		{
			return false;
		}

		return true;
	}

	bool VansVKDevice::VulkanDestroy()
	{
		CleanupFSR();
		{
			m_VansVKSurface.DestroyVulkanSwapChain(m_VansVKLogicDevice);
			m_VansVKSurface.DestroyVulkanPresentSurface(m_VansVKInstance);
		}
		DestroyVulkanLogicDevice();
		DestroyVulkanInstance();
		UnloadVulkanLibrary();
		return true;
	}

	void VansVKDevice::PrepareSkyRenderData()
	{
		//计算预卷积环境漫反射贴图
		//创建输入贴图
		VansTexture* texture = new VansTexture();
		texture->LoadCubeTexture(m_VansVKCommandBuffer, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Textures/SkyBox2");


		//创建输出cube
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_PreConvDiffuse = new VansTexture();
		manager->m_PreConvDiffuse->InitTextureWithoutData(m_VansVKCommandBuffer, 512, 512,1, 4, true, false, true);

		//预过滤环境贴图
		manager->m_PreConvSpecular = new VansTexture();
		manager->m_PreConvSpecular->InitTextureWithoutData(m_VansVKCommandBuffer, 512, 512,1, 4, true, true, true);

		//brdf lut
		manager->m_BRDFIntegralLUT = new VansTexture();
		manager->m_BRDFIntegralLUT->LoadTexture(m_VansVKCommandBuffer, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Textures/BRDFIntegralLUT.png", false);

		
		VansVKBuffer prefilterCBBuffer;
		uint32_t mipCount = log2(512);
		float data[4] = { 512,mipCount,512,512 };
		prefilterCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(float) * 4, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		prefilterCBBuffer.SetBufferData(data, 0, sizeof(float) * 4);

		manager->m_SkySHResultBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(float) * 27, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_SkySHResultBuffer.SetBufferData(data, 0, sizeof(float) * 27);


		VkDescriptorSetLayoutBinding samplerLUTBinding =
		{
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding sampleDiffuseConvBinding =
		{
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding sampleSpecularConBinding =
		{
			VansVKDescriptorManager::m_SampleTexture2SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding environmentSHBuffer =
		{
			VansVKDescriptorManager::m_Buffer3SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			1,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ samplerLUTBinding,sampleDiffuseConvBinding,sampleSpecularConBinding,environmentSHBuffer }, manager->m_BRDFInterationTexSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_BRDFInterationTexSetLayout }, manager->m_BRDFInterationTextDescriptorSets);


		//卷积compute shader
		VansComputeShader* m_PreConvDiffuseShader = new VansComputeShader();
		m_PreConvDiffuseShader->InitShader(m_VansVKLogicDevice, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/PreConDiffuseEnvironment");

		VansComputeShader* m_PreConvSpecularShader = new VansComputeShader();
		m_PreConvSpecularShader->InitShader(m_VansVKLogicDevice, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/PreConSpecularEnvironment");


		//创建描述符
		VkDescriptorSetLayoutBinding samplerCubeBinding =
		{
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding uavCubeBinding0 =
		{
			VansVKDescriptorManager::m_UAVTexture0SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding uavCubeBinding1 =
		{
			VansVKDescriptorManager::m_UAVTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			mipCount,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding prefilterCB =
		{
			VansVKDescriptorManager::m_CBuffer3SetBinding,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding shResultBuffer =
		{
			VansVKDescriptorManager::m_Buffer4SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		//PBR预卷积 compute shader 描述符
		VkDescriptorSetLayout m_PreConvSetLayout;
		std::vector<VkDescriptorSet> m_PreConvtDescriptorSets;
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ samplerCubeBinding,uavCubeBinding0,uavCubeBinding1,prefilterCB,shResultBuffer }, m_PreConvSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_PreConvSetLayout }, m_PreConvtDescriptorSets);

		//更新描述符
		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				VansVKDescriptorManager::m_CBuffer3SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						prefilterCBBuffer.GetNativeBuffer(),
						0,
						prefilterCBBuffer.GetBufferSize()
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				VansVKDescriptorManager::m_Buffer4SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				{
					{
						manager->m_SkySHResultBuffer.GetNativeBuffer(),
						0,
						manager->m_SkySHResultBuffer.GetBufferSize()
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture0SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						texture->GetImage().GetSampler(),
						texture->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture0SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						manager->m_PreConvDiffuse->GetImage().GetSampler(),
						manager->m_PreConvDiffuse->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		//设置每个mip的image信息，每个mip分开绑定
		std::vector<VkDescriptorImageInfo> cubeMipImageInfos;
		for (int mipLevel = 0; mipLevel < mipCount; mipLevel++)
		{
			cubeMipImageInfos.push_back(
				{
					manager->m_PreConvSpecular->GetImage().GetSampler(),
					manager->m_PreConvSpecular->GetImage().GetImageMipView(mipLevel),
					VK_IMAGE_LAYOUT_GENERAL
				}
			);
		}
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				cubeMipImageInfos
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		//record command buffer
		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		//除了生成diffuse，这里还生成一下球协
		m_VansVKCommandBuffer.EnsureComputeShader(*m_PreConvDiffuseShader, { m_PreConvSetLayout });
		m_VansVKCommandBuffer.DispatchCompute(*m_PreConvDiffuseShader, 512, 512, 1, m_PreConvtDescriptorSets);

		m_VansVKCommandBuffer.EnsureComputeShader(*m_PreConvSpecularShader, { m_PreConvSetLayout });
		//生成多个mip
		m_VansVKCommandBuffer.DispatchCompute(*m_PreConvSpecularShader, 512, 512, mipCount, m_PreConvtDescriptorSets);

		//天空渲染参数
		manager->m_AtmospherePBRDataBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(VansAtmospherePBRParam), VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

		VkDescriptorSetLayoutBinding stmosphereUnifomBuffer =
		{
			VansVKDescriptorManager::m_AtmosphereBufferSetBinding,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			1,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ stmosphereUnifomBuffer }, manager->m_MaterialAtmosphereDataLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_MaterialAtmosphereDataLayout }, manager->m_MaterialAtmosphereDataDescriptorSets);


		//统一更新公用描述符集
		manager->UpdatePBRLutDescriptorSets();
		manager->UpdateAtmosphereDescriptorSets();

		//将天空相关贴图转换成shader readonlylayout
		manager->m_PreConvSpecular->GetImage().SetImageMemoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{
				manager->m_PreConvSpecular->GetImage().m_VansVKImage,
				VK_ACCESS_NONE,
				VK_ACCESS_NONE,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				manager->m_PreConvSpecular->GetImage().m_ImageAspect
			});
		manager->m_PreConvDiffuse->GetImage().SetImageMemoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			{
				manager->m_PreConvDiffuse->GetImage().m_VansVKImage,
				VK_ACCESS_NONE,
				VK_ACCESS_NONE,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				manager->m_PreConvDiffuse->GetImage().m_ImageAspect
			});

		//end record
		m_VansVKCommandBuffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);

		prefilterCBBuffer.DestroyVulkanBuffer(m_VansVKLogicDevice);
	}

	void VansVKDevice::PrepareSSAORenderData()
	{
		//SSAO结果
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_SSAOResult = new VansTexture();
		manager->m_SSAOResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight,1, 4, false, false, true);

	}

	void VansVKDevice::PrepareSSGIRenderData()
	{
		//SSAO结果
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_SSGIResult = new VansTexture();
		manager->m_SSGIResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth/4, m_RenderHeight/4,1, 4, false, false, true);
		
		//cs
		manager->m_SSGIShader = new VansComputeShader();
		manager->m_SSGIShader->InitShader(m_VansVKLogicDevice, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/SSGI");


		float data[4] = { std::floor(m_RenderWidth / 4) , std::floor(m_RenderHeight / 4), 4.0f/ m_RenderWidth, 4.0f/ m_RenderHeight };
		manager->m_SSGICBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(float) * 4, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		manager->m_SSGICBBuffer.SetBufferData(data, 0, sizeof(float) * 4);

		//资源绑定
		//需要输入
		//normal,depth,color result,天空diffuse
		VkDescriptorSetLayoutBinding normalBinding =
		{
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding depthBinding =
		{
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VkDescriptorSetLayoutBinding colorBinding =
		{
			VansVKDescriptorManager::m_SampleTexture2SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding positionBinding =
		{
			VansVKDescriptorManager::m_SampleTexture3SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding skyDiffuseBinding =
		{
			VansVKDescriptorManager::m_SampleTexture4SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding SSGIResultBinding =
		{
			VansVKDescriptorManager::m_UAVTexture4SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding SSGICBBinding =
		{
			VansVKDescriptorManager::m_CBuffer6SetBinding,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ normalBinding,depthBinding,colorBinding,positionBinding,skyDiffuseBinding,SSGIResultBinding,SSGICBBinding }, manager->m_SSGITexSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_SSGITexSetLayout }, manager->m_SSGIDescriptorSets);
	}

	void VansVKDevice::UpdateSSGI(VansRenderPassManager* renderPassManager)
	{
		uint32_t halfResWidth = std::floor(m_RenderWidth / 4);
		uint32_t halfResHeight = std::floor(m_RenderHeight / 4);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		auto camera = m_Scene->GetCamera();
		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_SSGIShader, { manager->m_SSGITexSetLayout,camera->m_CameraBufferLayout });
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_SSGIShader, halfResWidth, halfResHeight, 1, { manager->m_SSGIDescriptorSets[0],camera ->m_CameraBufferDescriptorSets[0]});
	}

	void VansVKDevice::BilateralFilterSSAO(VansRenderPassManager* renderPassManager)
	{
		uint32_t halfResWidth = std::floor(m_RenderWidth);
		uint32_t halfResHeight = std::floor(m_RenderHeight);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_BilateralFilterShader, { manager->m_BilateralFilterSetLayout});
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_BilateralFilterShader, halfResWidth, halfResHeight, 1, { manager->m_BilateralFilterDescriptorSets[0]});
	}

	void VansVKDevice::UpdateGIDataDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		static bool updatedSets = false;
		if (updatedSets)
		{
			return;
		}
		updatedSets = true;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		//更新描述符
		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				VansVKDescriptorManager::m_CBuffer6SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						manager->m_SSGICBBuffer.GetNativeBuffer(),
						0,
						manager->m_SSGICBBuffer.GetBufferSize()
					}
				}
			}
		);

		//normal
		auto& normal = renderPassManager->GetNormal();
		auto& depth = renderPassManager->GetDepth();
		auto& color = renderPassManager->GetColor();
		auto& positionGbuffer = renderPassManager->GetGbuffer2();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture0SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						normal.GetSampler(),
						normal.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		//depth
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						depth.GetSampler(),
						depth.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		//color
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture2SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						color.GetSampler(),
						color.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture3SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						positionGbuffer.GetSampler(),
						positionGbuffer.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		//sky
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture4SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						manager->m_PreConvDiffuse->GetImage().GetSampler(),
						manager->m_PreConvDiffuse->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture4SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						manager->m_SSGIResult->GetImage().GetSampler(),
						manager->m_SSGIResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		//更新描述符
		VansVKDescriptorManager::GetInstance()->ResetState();


		//color
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture0SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						manager->m_SSAOResult->GetImage().GetSampler(),
						manager->m_SSAOResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		//depth
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						depth.GetSampler(),
						depth.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		//result
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						manager->m_SSAOFilterResult->GetImage().GetSampler(),
						manager->m_SSAOFilterResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	void VansVKDevice::UpdateHZBDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		static bool updatedSets = false;
		if (updatedSets)
		{
			return;
		}
		updatedSets = true;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		for (int mipIndex = 1; mipIndex < manager->m_HIZMipCount; mipIndex++)
		{
			VansVKDescriptorManager::GetInstance()->ResetState();

			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_HZBDescriptorSets[mipIndex - 1],
					VansVKDescriptorManager::m_UAVTextureSetBinding,
					0,
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					{
						{
							manager->m_HZBResult->GetImage().GetSampler(),
							manager->m_HZBResult->GetImage().GetImageMipView(mipIndex - 1),
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
				{
					manager->m_HZBDescriptorSets[mipIndex - 1],
					VansVKDescriptorManager::m_UAVTexture0SetBinding,
					0,
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					{
						{
							manager->m_HZBResult->GetImage().GetSampler(),
							manager->m_HZBResult->GetImage().GetImageMipView(mipIndex),
							VK_IMAGE_LAYOUT_GENERAL
						}
					}
				}
			);

			VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
		}
	}

	void VansVKDevice::UpdateSSRDescriptorSets(VansRenderPassManager* renderPassManager)
	{
		static bool updatedSets = false;
		if (updatedSets)
		{
			return;
		}
		updatedSets = true;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		auto& normal = renderPassManager->GetNormal();
		auto& position = renderPassManager->GetGbuffer2();
		auto& roughness = renderPassManager->GetGbuffer0(); // w通道
		auto& color = renderPassManager->GetColor();
		auto& hiz = manager->m_HZBResult;

		VansVKDescriptorManager::GetInstance()->ResetState();

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture0SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						normal.GetSampler(),
						normal.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						roughness.GetSampler(),
						roughness.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture2SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						position.GetSampler(),
						position.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture3SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ,
				{
					{
						manager->m_HZBResult->GetImage().GetSampler(),
						manager->m_HZBResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture3SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ,
				{
					{
						manager->m_SSRHitInfo->GetImage().GetSampler(),
						manager->m_SSRHitInfo->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRTraceDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture4SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ,
				{
					{
						manager->m_SSRRayPDF->GetImage().GetSampler(),
						manager->m_SSRRayPDF->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		//设置ssr resolve
		VansVKDescriptorManager::GetInstance()->ResetState();

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture0SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						color.GetSampler(),
						color.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						roughness.GetSampler(),
						roughness.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture2SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						normal.GetSampler(),
						normal.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture3SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						position.GetSampler(),
						position.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture3SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ,
				{
					{
						manager->m_SSRHitInfo->GetImage().GetSampler(),
						manager->m_SSRHitInfo->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture4SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ,
				{
					{
						manager->m_SSRRayPDF->GetImage().GetSampler(),
						manager->m_SSRRayPDF->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);

		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRResolveDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture5SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ,
				{
					{
						manager->m_SSRResult->GetImage().GetSampler(),
						manager->m_SSRResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		//设置ssr temporal aa
		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture0SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						manager->m_SSRResult->GetImage().GetSampler(),
						manager->m_SSRResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						position.GetSampler(),
						position.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ,
				{
					{
						manager->m_SSRAAResultA->GetImage().GetSampler(),
						manager->m_SSRAAResultA->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture2SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ,
				{
					{
						manager->m_SSRAAResultB->GetImage().GetSampler(),
						manager->m_SSRAAResultB->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
	
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_SSRAADescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture3SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ,
				{
					{
						manager->m_SSRAAResult->GetImage().GetSampler(),
						manager->m_SSRAAResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	void VansVKDevice::UpdateVolumetricFogSets(VansRenderPassManager* renderPassManager)
	{
		static bool updatedSets = false;
		if (updatedSets)
		{
			return;
		}
		updatedSets = true;

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		auto& position = renderPassManager->GetGbuffer2();
		auto& mainLightShadow = renderPassManager->GetShadowMap();


		VansVKDescriptorManager::GetInstance()->ResetState();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_VolumetricFogDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture0SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						position.GetSampler(),
						position.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_VolumetricFogDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						mainLightShadow.GetSampler(),
						mainLightShadow.GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_VolumetricFogDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ,
				{
					{
						manager->m_VolumetricFogResult->GetImage().GetSampler(),
						manager->m_VolumetricFogResult->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
	}

	void VansVKDevice::UpdateRayTracing()
	{
		//更新push constant数据
		auto camera = m_Scene->GetCamera();
		rayTracingContext.m_RayTracingConstant.cameraPos = camera->GetPosition();
		rayTracingContext.m_RayTracingConstant.cameraDir = camera->GetForward();
		rayTracingContext.m_RayTracingConstant.cameraRight = camera->GetRight();
		rayTracingContext.m_RayTracingConstant.cameraUp = camera->GetUp();
		rayTracingContext.DispatchRayTracing(this, &m_VansVKCommandBuffer, m_Scene);

		VansLightManager* lightManager = m_Scene->GetLightManager();
		VansMaterialManager* materialManager = m_Scene->GetMaterialManager();
		rayTracingContext.UpdateGIProbe(this, &m_VansVKCommandBuffer, lightManager, materialManager);
	}

	void VansVKDevice::PrepareHZBRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_HZBResult = new VansTexture();
		manager->m_HZBResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, 1, false, true, true, MID_PRES_16);

		manager->m_HZBShader = new VansComputeShader();
		manager->m_HZBShader->InitShader(m_VansVKLogicDevice, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/HIZ");

		//计算mip数量
		manager->m_HIZMipCount = 1 + (int)std::floor(std::log2(std::min(m_RenderWidth, m_RenderHeight)));
		manager->m_HZBTexSetLayouts.resize(manager->m_HIZMipCount - 1);
		//创建描述符
		for (int mipIndex = 0; mipIndex < manager->m_HIZMipCount -1; mipIndex++)
		{
			VkDescriptorSetLayoutBinding depthInput =
			{
				VansVKDescriptorManager::m_UAVTextureSetBinding,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				1,
				VK_SHADER_STAGE_COMPUTE_BIT,
				nullptr
			};
			VkDescriptorSetLayoutBinding depthOuput =
			{
				VansVKDescriptorManager::m_UAVTexture0SetBinding,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				1,
				VK_SHADER_STAGE_COMPUTE_BIT,
				nullptr
			};
			VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ depthInput,depthOuput }, manager->m_HZBTexSetLayouts[mipIndex]);
		}
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet(manager->m_HZBTexSetLayouts, manager->m_HZBDescriptorSets);
	}

	void VansVKDevice::PrepareSSRRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_SSRHitInfo = new VansTexture();
		manager->m_SSRHitInfo->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth/2, m_RenderHeight/2,1, 4, false, false, true, MID_PRES_16);

		manager->m_SSRRayPDF = new VansTexture();
		manager->m_SSRRayPDF->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth/2, m_RenderHeight/2,1, 4, false, false, true, HIGH_PRES_32);

		manager->m_SSRResult = new VansTexture();
		manager->m_SSRResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth/2, m_RenderHeight/2,1, 4, false, false, true, HIGH_PRES_32);

		manager->m_SSRAAResultA = new VansTexture();
		manager->m_SSRAAResultA->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);
		manager->m_SSRAAResultB = new VansTexture();
		manager->m_SSRAAResultB->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);
		manager->m_SSRAAResult = new VansTexture();
		manager->m_SSRAAResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);


		manager->m_SSRTraceShader = new VansComputeShader();
		manager->m_SSRTraceShader->InitShader(m_VansVKLogicDevice, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/SSR_TRACE");

		manager->m_SSRResolveShader = new VansComputeShader();
		manager->m_SSRResolveShader->InitShader(m_VansVKLogicDevice, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/SSR_RESOLVE");
		
		manager->m_SSRTemporalAAShader = new VansComputeShader();
		manager->m_SSRTemporalAAShader->InitShader(m_VansVKLogicDevice, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/SSR_TEMPORALAA");

		//需要normal, roughness, hiz
		VkDescriptorSetLayoutBinding normalInput =
		{
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding roughnessInput =
		{
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding positionInput =
		{
			VansVKDescriptorManager::m_SampleTexture2SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding hizInput =
		{
			VansVKDescriptorManager::m_SampleTexture3SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding tranceInfoResult =
		{
			VansVKDescriptorManager::m_UAVTexture3SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding trancePDFResult =
		{
			VansVKDescriptorManager::m_UAVTexture4SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ normalInput,roughnessInput,positionInput,hizInput,tranceInfoResult,trancePDFResult }, manager->m_SSRTraceSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_SSRTraceSetLayout }, manager->m_SSRTraceDescriptorSets);
	
		
		//resolve
		//需要color,roughness，hitinfo, pdf
		VkDescriptorSetLayoutBinding colorInput =
		{
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		roughnessInput =
		{
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		normalInput =
		{
			VansVKDescriptorManager::m_SampleTexture2SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		positionInput =
		{
			VansVKDescriptorManager::m_SampleTexture3SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		tranceInfoResult =
		{
			VansVKDescriptorManager::m_UAVTexture3SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		trancePDFResult =
		{
			VansVKDescriptorManager::m_UAVTexture4SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding resolveResult =
		{
			VansVKDescriptorManager::m_UAVTexture5SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ colorInput,roughnessInput,normalInput,positionInput, tranceInfoResult ,trancePDFResult,resolveResult }, manager->m_SSRResolveSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_SSRResolveSetLayout }, manager->m_SSRResolveDescriptorSets);

		//temporal AA
		colorInput =
		{
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		positionInput =
		{
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding aaResultA =
		{
			VansVKDescriptorManager::m_UAVTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding aaResultB =
		{
			VansVKDescriptorManager::m_UAVTexture2SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding aaResult =
		{
			VansVKDescriptorManager::m_UAVTexture3SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ colorInput,positionInput, tranceInfoResult ,aaResultA,aaResultB,aaResult }, manager->m_SSRAASetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_SSRAASetLayout }, manager->m_SSRAADescriptorSets);
	}

	void VansVKDevice::PrepareVolumetricData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_VolumetricFogResult = new VansTexture();
		manager->m_VolumetricFogResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth / 2, m_RenderHeight / 2, 1, 4, false, false, true, HIGH_PRES_32);

		manager->m_VolumetrcFogShader = new VansComputeShader();
		manager->m_VolumetrcFogShader->InitShader(m_VansVKLogicDevice, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/VolumetricFog");

		VkDescriptorSetLayoutBinding positionInput =
		{
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding mainlightShadow =
		{
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding fogResult =
		{
			VansVKDescriptorManager::m_UAVTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ positionInput,mainlightShadow, fogResult }, manager->m_VolumetricFogSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_VolumetricFogSetLayout }, manager->m_VolumetricFogDescriptorSets);
	}

	void VansVKDevice::PrepareBilaterFilterData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_SSAOFilterResult = new VansTexture();
		manager->m_SSAOFilterResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight,1, 4, false, false, true, MID_PRES_16);

		VkDescriptorSetLayoutBinding colorInput =
		{
			VansVKDescriptorManager::m_SampleTexture0SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding depthInput =
		{
			VansVKDescriptorManager::m_SampleTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		VkDescriptorSetLayoutBinding result =
		{
			VansVKDescriptorManager::m_UAVTexture1SetBinding,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};

		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ colorInput,depthInput,result }, manager->m_BilateralFilterSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_BilateralFilterSetLayout }, manager->m_BilateralFilterDescriptorSets);

		manager->m_BilateralFilterPushConstant = 
		{
			2.0f,
			0.02f,
			3,
			0.01f
		};
		// Spatial sigma (controls distance falloff)
		// Depth sigma (controls depth difference falloff)
		// Filter radius
		// Maximum depth difference to consider

		manager->m_BilateralFilterShader = new VansComputeShader();
		manager->m_BilateralFilterShader->InitShader(m_VansVKLogicDevice, "D:/WorkSpace/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/BilateralFilter");
		manager->m_BilateralFilterShader->SetPushConstant(sizeof(manager->m_BilateralFilterPushConstant));
		manager->m_BilateralFilterShader->SetPushConstantData(&(manager->m_BilateralFilterPushConstant));
	}

	void VansVKDevice::UpdateHZB(VansRenderPassManager* renderPassManager)
	{
		UpdateHZBDescriptorSets(renderPassManager);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		//上一帧场景深度
		auto& depth = renderPassManager->GetDepth();

		//先blit到mip0
		m_VansVKCommandBuffer.BlitImage(depth, 0, manager->m_HZBResult->GetImage(), 0);

		for (int mipIndex = 1; mipIndex < manager->m_HIZMipCount; mipIndex++)
		{
			int threadGroupSizeX = m_RenderWidth >> (mipIndex);
			int threadGroupSizeY = m_RenderHeight >> (mipIndex);
			threadGroupSizeX = std::ceilf(threadGroupSizeX / 16.0f);
			threadGroupSizeY = std::ceilf(threadGroupSizeY / 16.0f);

			m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_HZBShader, { manager->m_HZBTexSetLayouts[mipIndex - 1]});
			m_VansVKCommandBuffer.DispatchCompute(*manager->m_HZBShader, threadGroupSizeX, threadGroupSizeY, 1, { manager->m_HZBDescriptorSets[mipIndex - 1] });
		}
	}

	void VansVKDevice::UpdateSSR(VansRenderPassManager* renderPassManager)
	{
		UpdateSSRDescriptorSets(renderPassManager);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();

		uint32_t halfResWidth = std::floor(m_RenderWidth / 2);
		uint32_t halfResHeight = std::floor(m_RenderHeight / 2);

		auto camera = m_Scene->GetCamera();
		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_SSRTraceShader, { manager->m_SSRTraceSetLayout, camera->m_CameraBufferLayout });
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_SSRTraceShader, halfResWidth, halfResHeight, 1, { manager->m_SSRTraceDescriptorSets[0],camera->m_CameraBufferDescriptorSets[0] });
		
		
		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_SSRResolveShader, { manager->m_SSRResolveSetLayout, camera->m_CameraBufferLayout });
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_SSRResolveShader, halfResWidth, halfResHeight, 1, { manager->m_SSRResolveDescriptorSets[0],camera->m_CameraBufferDescriptorSets[0] });

		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_SSRTemporalAAShader, { manager->m_SSRAASetLayout, camera->m_CameraBufferLayout });
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_SSRTemporalAAShader, halfResWidth, halfResHeight, 1, { manager->m_SSRAADescriptorSets[0],camera->m_CameraBufferDescriptorSets[0] });

	}

	void VansVKDevice::UpdateVolumetricFog(VansRenderPassManager* renderPassManager)
	{
		UpdateVolumetricFogSets(renderPassManager);

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VansLightManager* lightManager = m_Scene->GetLightManager();

		uint32_t halfResWidth = std::floor(m_RenderWidth / 2);
		uint32_t halfResHeight = std::floor(m_RenderHeight / 2);

		auto camera = m_Scene->GetCamera();

		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_VolumetrcFogShader, { manager->m_VolumetricFogSetLayout, camera->m_CameraBufferLayout, lightManager->m_LightDataDescriptorSetLayout });
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_VolumetrcFogShader, halfResWidth, halfResHeight, 1, { manager->m_VolumetricFogDescriptorSets[0],camera->m_CameraBufferDescriptorSets[0],lightManager->m_LightDataDescriptorSets[0]});
	}

	void VansVKDevice::PrepareRenderingData()
	{
		PrepareSkyRenderData();
		
		PrepareSSAORenderData();

		PrepareSSGIRenderData();

		PrepareBilaterFilterData();

		PrepareHZBRenderData();

		PrepareSSRRenderData();

		PrepareVolumetricData();

#ifdef _DEBUG
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		VkDebugUtilsObjectNameInfoEXT nameInfo = {};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_PreConvDiffuse->GetImage().GetImage());
		nameInfo.pObjectName = "PreConvDiffuse";
		vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);

		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_PreConvSpecular->GetImage().GetImage());
		nameInfo.pObjectName = "PreConvSpecular";
		vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);

		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_SSAOResult->GetImage().GetImage());
		nameInfo.pObjectName = "SSAOResult";
		vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);

		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_SSGIResult->GetImage().GetImage());
		nameInfo.pObjectName = "SSGIResult";
		vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);

		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_SSRResult->GetImage().GetImage());
		nameInfo.pObjectName = "SSRResult";
		vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);

		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_SSRHitInfo->GetImage().GetImage());
		nameInfo.pObjectName = "SSRHitInfo";
		vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);

		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_SSRRayPDF->GetImage().GetImage());
		nameInfo.pObjectName = "SSRRayPDF";
		vkSetDebugUtilsObjectNameEXT(m_VansVKLogicDevice, &nameInfo);

#endif
	}

	void VansVKDevice::PrepareRayTracingData()
	{
		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		//给所有网格创建一个blas
		m_Scene->BuildRayTracingAS(this, &m_VansVKCommandBuffer);

		m_VansVKCommandBuffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer()}, {}, {}, VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);

		//清空所有的AS temp buffer
		m_Scene->ReleaseASTempBuffer(this);

		//创建ray tracing需要的资源和资源描述符
		rayTracingContext.CreateRayTracingResource(this, &m_VansVKCommandBuffer, m_Scene);
	}

	void VansVKDevice::PrepareGlobalIllumiationData()
	{
		
	}

	void VansVKDevice::DrawShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		m_Scene->DrawShadowNodes();
	}

	void VansVKDevice::DrawPunctualShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		VansLightManager* lightManager = m_Scene->GetLightManager();

		auto pointLights = lightManager->GetPointLights();
		int pointLightCount = pointLights.size();
		//绘制梭有点光源
		for (int lightIndex = 0; lightIndex < pointLightCount; lightIndex++)
		{
			//绘制六次
			m_Scene->DrawPointShadow(lightIndex);
		}

		auto spotLights = lightManager->GetSpotLight();
		int spotLightCount = spotLights.size();
		//绘制梭有聚光灯
		for (int lightIndex = 0; lightIndex < spotLightCount; lightIndex++)
		{
			m_Scene->DrawSpotShadow(pointLightCount , lightIndex);
		}
	}

	void VansVKDevice::DrawSceneForward(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		
		//绘制天空盒
		m_Scene->DrawSkyBoxNode();

		m_Scene->DrawOpaqueNodes();

		//切换进行present
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);

		m_Scene->DrawPostProcessNodes();
	}

	void VansVKDevice::DrawSceneDeferred(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		//绘制GBuffer
		m_Scene->DrawOpaqueNodes();

		//切换进行present
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);

		m_Scene->DrawScreenSpaceFeatureNode();

		m_Scene->DeferredShading();

		m_Scene->DrawSkyBoxNode();

		//切换进行present
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);

		m_Scene->DrawPostProcessNodes();
	}
	
	void VansVKDevice::UpdateGIData(VansRenderPassManager* renderPassManager)
	{
		UpdateGIDataDescriptorSets(renderPassManager);

		UpdateSSGI(renderPassManager);

		BilateralFilterSSAO(renderPassManager);
	}
}