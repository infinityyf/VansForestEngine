#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "VansVKMemoryManager.h"
#include "VansVKDescriptorManager.h"
#include "VansRenderPass.h"
#include "VansMesh.h"
#include "VansShader.h"
#include "../VansScene.h"
#include "../../Configration/VansConfigration.h"
#include "../../EditorCore/VansEditorWindow.h"
#include "../../VansTimer.h"
#include <iostream>
#include <cstring>

namespace VansGraphics
{
	std::vector<const char*> RayTracingDeviceExtensions =
	{
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
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

	bool VansVKDevice::CheckAvaliableDeviceExtensions(VkPhysicalDevice device, std::vector<VkExtensionProperties>& available_extensions)
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

			m_Features12.pNext = &m_AcceralteFeature;
		}

		//支持struct
		m_ScalarBlockFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
		m_ScalarBlockFeature.scalarBlockLayout = VK_TRUE;
		m_ScalarBlockFeature.pNext = &m_Features11;

		m_DescriptorIndexingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		m_DescriptorIndexingFeature.pNext = &m_ScalarBlockFeature;
		m_DescriptorIndexingFeature.runtimeDescriptorArray = VK_TRUE;
		m_DescriptorIndexingFeature.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		m_DescriptorIndexingFeature.descriptorBindingPartiallyBound = VK_TRUE;
		m_DescriptorIndexingFeature.descriptorBindingVariableDescriptorCount = VK_TRUE;


		m_DeviceFeatures2.pNext = &m_DescriptorIndexingFeature;
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
				 nullptr
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
		CreateVKFence(false, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_VansVKRayTracingCommandBuffer.CreateVulkanCommandBuffer(*this, m_ComputeQueueFamilyIndex, params);
		if (!result)
		{
			std::cout << "create m_VansVKRayTracingCommandBuffer failed" << std::endl;
			return false;
		}
		CreateVKFence(false, m_VansVKRayTracingCommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_VansEditorCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params);
		if (!result)
		{
			std::cout << "create m_VansEditorCommandBuffer failed" << std::endl;
			return false;
		}
		CreateVKFence(false, m_VansEditorCommandBuffer.m_CommandBufferFinishSubmitFence);


		VansVKMemoryManager::GetInstance()->BindDevice(m_VansVKCommandBuffer.GetVKCommandBuffer(), *this);
		VansVKDescriptorManager::GetInstance()->BindDevice(m_VansVKPhysicalDevice, m_VansVKLogicDevice, m_VansVKCommandBuffer.GetVKCommandBuffer());
		VansVKDescriptorManager::GetInstance()->CreateDescriptorPool(true);

		m_StageBuffer.CreatVulkanBuffer(m_VansVKLogicDevice, 1024 * 1024 * 512, VK_FORMAT_R32_SFLOAT, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		return true;
	}

	bool VansVKDevice::DestroyVulkanLogicDevice()
	{
		VansVKDescriptorManager::GetInstance()->DestroyDescriptorPool();

		m_StageBuffer.DestroyVulkanBuffer(m_VansVKLogicDevice);

		DestroyVKFence(m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_VansVKRayTracingCommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_VansEditorCommandBuffer.m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansVKRayTracingCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansEditorCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);

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
		if (!PrepareVulkanLibrary())
		{
			return false;
		}

		std::vector<char const*> desired_instance_extrensions =
		{
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
		};

		auto vansConfigration = VansConfigration::GetInstance();
#ifdef _DEBUG
		if (!vansConfigration->GetSupportRayTracing())
		{
			desired_instance_layers.push_back("VK_LAYER_RENDERDOC_Capture");
		}
#endif

		if (!CreateVulkanInstance(desired_instance_extrensions, desired_instance_layers))
		{
			return false;
		}

		if (!LoadVulkanInstanceLevelFunctions(m_VansVKInstance))
		{
			return false;
		}

		if (!LoadVulkanInstanceLevelFunctionFromExtension(m_VansVKInstance, desired_instance_extrensions))
		{
			return false;
		}

		if (!m_VansVKSurface.CreateVulkanPresentSurface(m_VansVKInstance, VansGraphics::VansEditorWindow::m_VansEditorWindow.m_VansGraphicsHandle))
		{
			return false;
		}

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

		if (!LoadVulkanDeviceLevelFunctions(m_VansVKLogicDevice))
		{
			return false;
		}

		if (!LoadVulkanDeviceLevelFunctionFromExtension(m_VansVKLogicDevice, desired_device_extrensions))
		{
			return false;
		}

		if (!m_VansVKSurface.CreateVulkanSwapChain(m_VansVKPhysicalDevice, m_VansVKLogicDevice))
		{
			return false;
		}

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
}
