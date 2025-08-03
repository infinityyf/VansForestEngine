#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "../VansGraphicsDevice.h"
#include "VansVKDevice.h"
#include "VansVKMemoryManager.h"
#include "VansVKDescriptorManager.h"
#include "VansRenderPass.h"
#include "VansMesh.h"
#include "VansShader.h"

#include "../VansScene.h"
//#if defined FOREST_EDITOR
#include "../../EditorCore/VansEditorWindow.h"
#include <iostream>


namespace VansVulkan
{

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
			if (strcmp(extension.extensionName, desire_extension) != 0)
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
			if (strcmp(layer.layerName, desire_layer) != 0)
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

	bool VansVKDevice::CheckPhysicDeviceFeature(VkPhysicalDevice device, VkPhysicalDeviceFeatures& features)
	{
		//set the feature we need for create device
		vkGetPhysicalDeviceFeatures(device, &features);
		vkGetPhysicalDeviceProperties(device, &m_DeviceProperties);
		if (features.geometryShader == VK_TRUE)
		{
			features = {};
			features.geometryShader = VK_TRUE;
			return true;
		}
		return false;
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
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, {m_VansVKCommandBuffer.GetVKCommandBuffer()}, {}, {});
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

		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {});
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
		return true;
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
		renderPassManager->SetupVansDeferredRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, m_VansVKSurface);
		//renderPassManager->SetupVansRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer , m_VansVKGraphicsQueue, m_VansVKSurface);

		//创建阴影pass
		renderPassManager->SetupVansShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);

		//创建精确阴影pass
		renderPassManager->SetupVansPunctualShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);

		//预计算渲染数据
		PrepareRenderingData();

		//加载场景数据
		m_Scene->LoadScene("C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Scene.json");
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

		//绘制精确阴影
		renderPassManager->BeginRenderPass(renderPassManager->m_VansPunctualShadowPass, cmd, m_globalRenderStateData);
		DrawPunctualShadowMap(renderPassManager, cmd);
		renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);

		//计算上一帧的HIZ数据
		UpdateHZB(renderPassManager);

		//计算上一帧的ssGI数据
		UpdateGIData(renderPassManager);

		//计算上一帧SSR
		UpdateSSR(renderPassManager);

		//clear mrt和color[beginrender pass的时候直接通过clearvalue就clear了，但是前提是frambufferload action是clear]
		//绘制指令
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


		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, { wait_semaphore_infos }, { m_CommandBufferReadyToPresentSemaphore });
		m_VansVKCommandBuffer.ResetCommandBuffer(false);

		//并进行present
		m_VansVKSurface.PresentImage(m_VansVKLogicDevice, m_VansVKGraphicsQueue, { m_CommandBufferReadyToPresentSemaphore }, m_SwapChainImageIndex);

		//重置imgae layout
		renderPassManager->ResetFrameBufferImageLayout(m_VansVKCommandBuffer, m_VansVKSurface, m_SwapChainImageIndex);
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {});
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
			 "ForestEngin",
			 VK_MAKE_VERSION(1, 0, 0),
			 "ForestEngin",
			 VK_MAKE_VERSION(1, 0, 0),
			 VK_MAKE_VERSION(1, 0, 0)
		};

		//instance create info
		//instance has layers and externsions
		VkInstanceCreateInfo instance_create_info =
		{
			 VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
			 nullptr,
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
			VkPhysicalDeviceFeatures availableDeviceFeatures;
			if (!CheckPhysicDeviceFeature(device, availableDeviceFeatures))
			{
				continue;
			}
			
			
			//check queuefamily type support
			if (!CheckAvalialeDeviceQueue(device, m_GraphicsQueueFamilyIndex, VK_QUEUE_GRAPHICS_BIT))
			{
				continue;
			}

			if (!CheckAvalialeDeviceQueue(device, m_ComputeQueueFamilyIndex, VK_QUEUE_COMPUTE_BIT))
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
			VkDeviceCreateInfo device_create_info = 
			{
				 VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				 nullptr,
				 0,
				 static_cast<uint32_t>(queue_create_infos.size()),
				 queue_create_infos.size() > 0 ? &queue_create_infos[0] : nullptr,
				 0,
				 nullptr,
				 static_cast<uint32_t>(desired_extensions.size()),
				 desired_extensions.size() > 0 ? &desired_extensions[0] : nullptr,
				 &availableDeviceFeatures
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
		//创建fence,用于等待command buffer执行完成
		CreateVKFence(false,VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);

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

		DestroyVKFence(VansVKCommandBuffer::m_CommandBufferFinishSubmitFence);

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
#ifdef _DEBUG
			"VK_LAYER_KHRONOS_validation",
			"VK_LAYER_RENDERDOC_Capture",
#endif
		};

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
		};
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
		texture->LoadCubeTexture(m_VansVKCommandBuffer, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Textures/SkyBox");


		//创建输出cube
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_PreConvDiffuse = new VansTexture();
		manager->m_PreConvDiffuse->InitTextureWithoutData(m_VansVKCommandBuffer, 512, 512, 4, true, false, true);

		//预过滤环境贴图
		manager->m_PreConvSpecular = new VansTexture();
		manager->m_PreConvSpecular->InitTextureWithoutData(m_VansVKCommandBuffer, 512, 512, 4, true, true, true);

		//brdf lut
		manager->m_BRDFIntegralLUT = new VansTexture();
		manager->m_BRDFIntegralLUT->LoadTexture(m_VansVKCommandBuffer, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Textures/BRDFIntegralLUT.png", false);

		
		VansVKBuffer prefilterCBBuffer;
		uint32_t mipCount = log2(512);
		float data[4] = { 512,mipCount,512,512 };
		prefilterCBBuffer.CreatVulkanBuffer(
			m_VansVKLogicDevice, sizeof(float) * 4, VK_FORMAT_R32_SFLOAT,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		prefilterCBBuffer.SetBufferData(data, 0, sizeof(float) * 4);


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
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ samplerLUTBinding,sampleDiffuseConvBinding,sampleSpecularConBinding }, manager->m_BRDFInterationTexSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ manager->m_BRDFInterationTexSetLayout }, manager->m_BRDFInterationTextDescriptorSets);


		//卷积compute shader
		VansComputeShader* m_PreConvDiffuseShader = new VansComputeShader();
		m_PreConvDiffuseShader->InitShader(m_VansVKLogicDevice, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/PreConDiffuseEnvironment");

		VansComputeShader* m_PreConvSpecularShader = new VansComputeShader();
		m_PreConvSpecularShader->InitShader(m_VansVKLogicDevice, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/PreConSpecularEnvironment");


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
		//PBR预卷积 compute shader 描述符
		VkDescriptorSetLayout m_PreConvSetLayout;
		std::vector<VkDescriptorSet> m_PreConvtDescriptorSets;
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ samplerCubeBinding,uavCubeBinding0,uavCubeBinding1,prefilterCB }, m_PreConvSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_PreConvSetLayout }, m_PreConvtDescriptorSets);

		//更新描述符
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				VansVKDescriptorManager::m_CBuffer3SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						prefilterCBBuffer.GetMativeBuffer(),
						0,
						prefilterCBBuffer.GetBufferSize()
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
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

		m_VansVKCommandBuffer.EnsureComputeShader(*m_PreConvDiffuseShader, { m_PreConvSetLayout });
		m_VansVKCommandBuffer.DispatchCompute(*m_PreConvDiffuseShader, 512, 512, 1, m_PreConvtDescriptorSets);

		m_VansVKCommandBuffer.EnsureComputeShader(*m_PreConvSpecularShader, { m_PreConvSetLayout });
		//生成多个mip
		m_VansVKCommandBuffer.DispatchCompute(*m_PreConvSpecularShader, 512, 512, mipCount, m_PreConvtDescriptorSets);

		//end record
		m_VansVKCommandBuffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {});
		m_VansVKCommandBuffer.ResetCommandBuffer(false);

		prefilterCBBuffer.DestroyVulkanBuffer(m_VansVKLogicDevice);

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
	}

	void VansVKDevice::PrepareSSAORenderData()
	{
		//SSAO结果
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_SSAOResult = new VansTexture();
		manager->m_SSAOResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 4, false, false, true);

	}

	void VansVKDevice::PrepareSSGIRenderData()
	{
		//SSAO结果
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_SSGIResult = new VansTexture();
		manager->m_SSGIResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 4, false, false, true);
		
		//cs
		manager->m_SSGIShader = new VansComputeShader();
		manager->m_SSGIShader->InitShader(m_VansVKLogicDevice, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/SSGI");


		float data[4] = { m_RenderWidth, m_RenderHeight, 1.0f/ m_RenderWidth, 1.0f/ m_RenderHeight };
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
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		auto camera = m_Scene->GetCamera();
		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_SSGIShader, { manager->m_SSGITexSetLayout,camera->m_CameraBufferLayout });
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_SSGIShader, m_RenderWidth, m_RenderHeight, 1, { manager->m_SSGIDescriptorSets[0],camera ->m_CameraBufferDescriptorSets[0]});
	}

	void VansVKDevice::BilateralFilterSSGI(VansRenderPassManager* renderPassManager)
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_BilateralFilterShader, { manager->m_BilateralFilterSetLayout});
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_BilateralFilterShader, m_RenderWidth, m_RenderHeight, 1, { manager->m_BilateralFilterDescriptorSets[0]});
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
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
			{
				manager->m_SSGIDescriptorSets[0],
				VansVKDescriptorManager::m_CBuffer6SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				{
					{
						manager->m_SSGICBBuffer.GetMativeBuffer(),
						0,
						manager->m_SSGICBBuffer.GetBufferSize()
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
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
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();


		//color
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				manager->m_BilateralFilterDescriptorSets[0],
				VansVKDescriptorManager::m_SampleTexture0SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{
					{
						manager->m_SSGIResult->GetImage().GetSampler(),
						manager->m_SSGIResult->GetImage().GetImageView(),
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
						manager->m_SSGIFilterResult->GetImage().GetSampler(),
						manager->m_SSGIFilterResult->GetImage().GetImageView(),
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
			VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
			VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();

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

		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();

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
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();

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
	}

	void VansVKDevice::PrepareHZBRenderData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_HZBResult = new VansTexture();
		manager->m_HZBResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 1, false, true, true, MID_PRES_16);

		manager->m_HZBShader = new VansComputeShader();
		manager->m_HZBShader->InitShader(m_VansVKLogicDevice, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/HIZ");

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
		manager->m_SSRHitInfo->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 4, false, false, true, MID_PRES_16);

		manager->m_SSRRayPDF = new VansTexture();
		manager->m_SSRRayPDF->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 4, false, false, true, HIGH_PRES_32);

		manager->m_SSRResult = new VansTexture();
		manager->m_SSRResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 4, false, false, true, MID_PRES_16);


		manager->m_SSRTraceShader = new VansComputeShader();
		manager->m_SSRTraceShader->InitShader(m_VansVKLogicDevice, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/SSR_TRACE");

		manager->m_SSRResolveShader = new VansComputeShader();
		manager->m_SSRResolveShader->InitShader(m_VansVKLogicDevice, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/SSR_RESOLVE");
		
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

	}

	void VansVKDevice::PrepareBilaterFilterData()
	{
		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		manager->m_SSGIFilterResult = new VansTexture();
		manager->m_SSGIFilterResult->InitTextureWithoutData(m_VansVKCommandBuffer, m_RenderWidth, m_RenderHeight, 4, false, false, true, MID_PRES_16);

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
			5,
			0.01f
		};

		manager->m_BilateralFilterShader = new VansComputeShader();
		manager->m_BilateralFilterShader->InitShader(m_VansVKLogicDevice, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/BilateralFilter");
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

		auto& normal = renderPassManager->GetNormal();
		auto& position = renderPassManager->GetGbuffer2();
		auto& roughness = renderPassManager->GetGbuffer0(); // w通道
		auto& color = renderPassManager->GetColor();
		auto& hiz = manager->m_HZBResult;

		
		auto camera = m_Scene->GetCamera();
		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_SSRTraceShader, { manager->m_SSRTraceSetLayout, camera->m_CameraBufferLayout });
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_SSRTraceShader, m_RenderWidth, m_RenderHeight, 1, { manager->m_SSRTraceDescriptorSets[0],camera->m_CameraBufferDescriptorSets[0] });
		
		
		m_VansVKCommandBuffer.EnsureComputeShader(*manager->m_SSRResolveShader, { manager->m_SSRResolveSetLayout, camera->m_CameraBufferLayout });
		m_VansVKCommandBuffer.DispatchCompute(*manager->m_SSRResolveShader, m_RenderWidth, m_RenderHeight, 1, { manager->m_SSRResolveDescriptorSets[0],camera->m_CameraBufferDescriptorSets[0] });

	}

	void VansVKDevice::PrepareRenderingData()
	{
		PrepareSkyRenderData();
		
		PrepareSSAORenderData();

		PrepareSSGIRenderData();

		PrepareBilaterFilterData();

		PrepareHZBRenderData();

		PrepareSSRRenderData();

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

		nameInfo.objectHandle = reinterpret_cast<uint64_t>(manager->m_SSGIFilterResult->GetImage().GetImage());
		nameInfo.pObjectName = "SSGIFilterResult";
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

		BilateralFilterSSGI(renderPassManager);
	}
}