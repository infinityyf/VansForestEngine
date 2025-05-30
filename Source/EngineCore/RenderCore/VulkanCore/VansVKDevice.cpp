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
		renderPassManager->SetupVansRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer , m_VansVKGraphicsQueue, m_VansVKSurface);

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

		m_globalRenderStateData.viewport = m_Viewport;
		m_globalRenderStateData.scissor = m_Scissor;

		//开始record渲染指令
		auto renderPassManager = VansRenderPassManager::GetInstance();

		//record command buffer
		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		
		m_VansVKCommandBuffer.ClearColor(renderPassManager->m_ColorImage,
			{
				0.0f,0.0f,0.0f,0.0f
			}
		);
		m_VansVKCommandBuffer.ClearDepthStencil(renderPassManager->m_DepthImage, {0,0});

		VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();

		//绘制指令
		renderPassManager->BeginRenderPass(cmd, { {0,0}, m_VansVKSurface.m_VansVKSwapChainImageExtent }, m_globalRenderStateData, m_SwapChainImageIndex);

		//apply camera
		m_VansVKCommandBuffer.SetViewport(0, { m_Viewport });
		m_VansVKCommandBuffer.SetScissor(0, { m_Scissor });

		//绘制天空盒
		m_Scene->DrawSkyBoxNode();

		m_Scene->DrawOpaqueNodes();

		//切换进行present
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);

		m_Scene->DrawPostProcessNodes();
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

		//设置渲染范围
		InitViewPortScissor();

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
		};

		std::vector<char const*> desired_instance_layers =
		{
			"VK_LAYER_KHRONOS_validation",
			"VK_LAYER_RENDERDOC_Capture"
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
			VK_KHR_MAINTENANCE1_EXTENSION_NAME
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


	void VansVKDevice::PrepareRenderingData()
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
		manager->m_BRDFIntegralLUT->LoadTexture(m_VansVKCommandBuffer, "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Textures/BRDFIntegralLUT.png");


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
			1,
			VK_SHADER_STAGE_COMPUTE_BIT,
			nullptr
		};
		//PBR预卷积 compute shader 描述符
		VkDescriptorSetLayout m_PreConvSetLayout;
		std::vector<VkDescriptorSet> m_PreConvtDescriptorSets;
		VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ samplerCubeBinding,uavCubeBinding0,uavCubeBinding1 }, m_PreConvSetLayout);
		VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_PreConvSetLayout }, m_PreConvtDescriptorSets);

		//更新描述符
		VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
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
		VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
			{
				m_PreConvtDescriptorSets[0],
				VansVKDescriptorManager::m_UAVTexture1SetBinding,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				{
					{
						manager->m_PreConvSpecular->GetImage().GetSampler(),
						manager->m_PreConvSpecular->GetImage().GetImageView(),
						VK_IMAGE_LAYOUT_GENERAL
					}
				}
			}
		);
		VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();

		//record command buffer
		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		m_VansVKCommandBuffer.EnsureComputeShader(*m_PreConvDiffuseShader, { m_PreConvSetLayout });
		m_VansVKCommandBuffer.DispatchCompute(*m_PreConvDiffuseShader, 512, 512, 1, m_PreConvtDescriptorSets);

		m_VansVKCommandBuffer.EnsureComputeShader(*m_PreConvSpecularShader, { m_PreConvSetLayout });
		//生成多个mip
		m_VansVKCommandBuffer.DispatchCompute(*m_PreConvSpecularShader, 512, 512, 1, m_PreConvtDescriptorSets);

		//切换layout

		//end record
		m_VansVKCommandBuffer.EndCommandBufferRecord();
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {});
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
	}

	void VansVKDevice::InitViewPortScissor()
	{
		//防止y flip问题
		//https://www.saschawillems.de/blog/2019/03/29/flipping-the-vulkan-viewport/
		m_Viewport =
		{
			0.0f,
			(float)m_RawResolution.height,
			(float)m_RawResolution.width,
			-(float)m_RawResolution.height,
			0.0f,
			1.0f
		};

		m_Scissor =
		{
			{0,0},
			{m_RawResolution.width,m_RawResolution.height}
		};
	}
}