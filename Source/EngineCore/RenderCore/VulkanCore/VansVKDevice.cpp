#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "VansVKMemoryManager.h"
#include "VansVKMemoryAllocator.h"
#include "VansVKDescriptorManager.h"
#include "VansRenderDocCapture.h"
#include "VansPipelineRegistry.h"
#include "VansRenderPass.h"
#include "VansMesh.h"
#include "VansShader.h"
#include "../VansShaderManager.h"
#include "../VansScene.h"
#include "../../Configration/VansConfigration.h"
#include "../../Interfaces/INativeWindowProvider.h"
#include "../../VansTimer.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

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
		result = VansGraphics::vkEnumerateInstanceExtensionProperties(nullptr, &extensions_count, nullptr);
		if ((result != VK_SUCCESS) || (extensions_count == 0))
		{
			VANS_LOG_ERROR("Could not get the number of Instance extensions.");
			return false;
		}

		available_extensions.resize(extensions_count);
		result = VansGraphics::vkEnumerateInstanceExtensionProperties(nullptr, &extensions_count, &available_extensions[0]);
		if ((result != VK_SUCCESS) || (extensions_count == 0))
		{
			VANS_LOG_ERROR("Could not enumerate Instance extensions.");
			return false;
		}
		return true;
	}

	bool VansVKDevice::CheckAvaliableInstanceLayer(std::vector<VkLayerProperties>& available_layers)
	{
		uint32_t layer_count;
		VkResult result = VK_SUCCESS;
		result = VansGraphics::vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
		if ((result != VK_SUCCESS) || (layer_count == 0))
		{
			VANS_LOG_ERROR("Could not get the number of Instance layers.");
			return false;
		}
		available_layers.resize(layer_count);
		result = VansGraphics::vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
		if ((result != VK_SUCCESS) || (layer_count == 0))
		{
			VANS_LOG_ERROR("Could not enumerate Instance extensions.");
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
				VANS_LOG("Extension named '" << desire_extension << "' is supported.");
				return true;
			}
		}
		VANS_LOG_WARN("Extension named '" << desire_extension << "' is not supported.");
		return false;
	}

	bool VansVKDevice::IsLayersSupported(const std::vector<VkLayerProperties>& available_layers, char const* desire_layer)
	{
		for (auto& layer : available_layers)
		{
			if (strcmp(layer.layerName, desire_layer) == 0)
			{
				VANS_LOG("Layer named '" << desire_layer << "' is supported.");
				return true;
			}
		}
		VANS_LOG_WARN("Layer named '" << desire_layer << "' is not supported.");
		return false;
	}

	void VansVKDevice::RequestDeviceQueue(uint32_t queue_family_index, uint32_t queue_index, VkQueue& queue)
	{
		VansGraphics::vkGetDeviceQueue(m_VansVKLogicDevice, queue_family_index, queue_index, &queue);
	}

	bool VansVKDevice::CheckAvaliableDeviceExtensions(VkPhysicalDevice device, std::vector<VkExtensionProperties>& available_extensions)
	{
		uint32_t extensions_count = 0;
		VkResult result = VK_SUCCESS;
		result = VansGraphics::vkEnumerateDeviceExtensionProperties(device, nullptr, &extensions_count, nullptr);
		if ((result != VK_SUCCESS) || (extensions_count == 0))
		{
			VANS_LOG_ERROR("Could not get the number of device extensions.");
			return false;
		}


		available_extensions.resize(extensions_count);
		result = VansGraphics::vkEnumerateDeviceExtensionProperties(device, nullptr, &extensions_count, &available_extensions[0]);
		if ((result != VK_SUCCESS) || (extensions_count == 0))
		{
			VANS_LOG_ERROR("Could not enumerate device extensions.");
			return false;
		}
		return true;
	}

	bool VansVKDevice::CheckAvalialeDeviceQueue(VkPhysicalDevice device, uint32_t& queue_family_index, VkQueueFlags desired_capabilty)
	{

		//check queue famliy
		uint32_t queue_families_count = 0;
		VansGraphics::vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, nullptr);
		if (queue_families_count == 0)
		{
			VANS_LOG_ERROR("Could not get the number of queue families.");
			return false;
		}

		std::vector<VkQueueFamilyProperties> queue_families;
		queue_families.resize(queue_families_count);
		VansGraphics::vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_families_count, &queue_families[0]);
		if (queue_families_count == 0)
		{
			VANS_LOG_ERROR("Could not acquire properties of queue families.");
			return false;
		}

		auto queueSupportsDesiredCapability = [&](uint32_t index)
		{
			if (queue_families[index].queueCount == 0)
			{
				return false;
			}

			if ((queue_families[index].queueFlags & desired_capabilty) != desired_capabilty)
			{
				return false;
			}

			if ((desired_capabilty & VK_QUEUE_GRAPHICS_BIT) != 0)
			{
				VkBool32 present_surface_support = VK_FALSE;
				VkResult result = VansGraphics::vkGetPhysicalDeviceSurfaceSupportKHR(device, index, m_VansVKSurface.m_VansVKPresentSurface, &present_surface_support);
				if (result != VK_SUCCESS || present_surface_support != VK_TRUE)
				{
					return false;
				}
			}

			return true;
		};

		// Prefer a dedicated compute family (different from graphics family) when requesting compute.
		if ((desired_capabilty & VK_QUEUE_COMPUTE_BIT) != 0)
		{
			for (uint32_t index = 0; index < static_cast<uint32_t>(queue_families.size()); ++index)
			{
				if (!queueSupportsDesiredCapability(index))
				{
					continue;
				}

				if (index != m_GraphicsQueueFamilyIndex)
				{
					queue_family_index = index;
					return true;
				}
			}
		}

		// Fallback: pick the first valid family.
		for (uint32_t index = 0; index < static_cast<uint32_t>(queue_families.size()); ++index)
		{
			if (!queueSupportsDesiredCapability(index))
			{
				continue;
			}

			queue_family_index = index;
			return true;
		}
		return false;
	}

	bool VansVKDevice::CheckPhysicDeviceFeature(VkPhysicalDevice device)
	{
		// Vulkan 1.1/1.2 promoted features must be requested through the versioned
		// feature structs only. Chaining their legacy extension structs alongside
		// VkPhysicalDeviceVulkan12Features is invalid and can corrupt driver state.
		m_DeviceFeatures2 = {};
		m_Features11 = {};
		m_Features12 = {};
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

		m_DeviceFeatures2.pNext = &m_Features11;
		m_DeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;


		m_AccelerationProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
		m_AccelerationProps.pNext = nullptr;

		m_RayTracingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
		m_RayTracingProperties.pNext = &m_AccelerationProps;

		m_DeviceProperties2.pNext = &m_RayTracingProperties;
		m_DeviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		//set the feature we need for create device
		VansGraphics::vkGetPhysicalDeviceFeatures2(device, &m_DeviceFeatures2);
		VansGraphics::vkGetPhysicalDeviceProperties2(device, &m_DeviceProperties2);
		// Keep the legacy core-properties accessor in sync with the Properties2 query.
		// Several systems use GetDeviceProperties() for Vulkan limits such as buffer
		// alignment and cubemap-array capacity.
		m_DeviceProperties = m_DeviceProperties2.properties;
		return true;
	}

	bool VansVKDevice::WaitForQueue(VkQueue queue)
	{
		VkResult result = VansGraphics::vkQueueWaitIdle(queue);
		if (VK_SUCCESS != result)
		{
			VANS_LOG_ERROR("Waiting for all operations submitted to queue failed.");
			return false;
		}
		return true;
	}

	bool VansVKDevice::WaitForDevice()
	{
		VkResult result = VansGraphics::vkDeviceWaitIdle(m_VansVKLogicDevice);
		if (VK_SUCCESS != result)
		{
			VANS_LOG_ERROR("Waiting on a device failed.");
			return false;
		}

		// VansGraphics::vkDeviceWaitIdle 之后所有已提交的 CB fence 都处于 signaled 状态。
		// 若不在此处 reset，下一帧 async 路径直接用 signaled fence 提交 vkQueueSubmit
		// 会触发 Vulkan Validation Error（NSight 对此会直接 crash）。
		auto resetIfValid = [&](VkFence f)
		{
			if (f != VK_NULL_HANDLE)
				VansGraphics::vkResetFences(m_VansVKLogicDevice, 1, &f);
		};
		resetIfValid(m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
		resetIfValid(m_VansVKShadowCommandBuffer.m_CommandBufferFinishSubmitFence);
		resetIfValid(m_VansVKGBufferCommandBuffer.m_CommandBufferFinishSubmitFence);
		resetIfValid(m_VansVKGraphicsPreCommandBuffer.m_CommandBufferFinishSubmitFence);
		resetIfValid(m_VansVKGraphicsScreenCommandBuffer.m_CommandBufferFinishSubmitFence);
		resetIfValid(m_VansVKRayTracingCommandBuffer.m_CommandBufferFinishSubmitFence);
		resetIfValid(m_VansVKAsyncCloudCommandBuffer.m_CommandBufferFinishSubmitFence);
		resetIfValid(m_VansVKAsyncGICommandBuffer.m_CommandBufferFinishSubmitFence);

		return true;
	}

	void VansVKDevice::SetAsyncComputeEnabled(bool enabled)
	{
		if (m_AsyncComputeRequested == enabled)
			return;
		if (m_VansVKLogicDevice != VK_NULL_HANDLE)
			WaitForDevice();
		m_AsyncComputeRequested = enabled;
		RefreshAsyncComputeState();
	}

	void VansVKDevice::RefreshAsyncComputeState()
	{
		const bool enabled = m_AsyncComputeRequested && m_QueueCapabilities.SupportsAsyncCompute();
		if (enabled == m_AsyncComputeEnabled)
			return;
		m_AsyncComputeEnabled = enabled;
		m_HasCompiledRenderGraphTopology = false;
		VANS_LOG("[VansVKDevice] Async compute requested=" << (m_AsyncComputeRequested ? "true" : "false")
			<< " enabled=" << (m_AsyncComputeEnabled ? "true" : "false")
			<< " graphicsFamily=" << m_QueueCapabilities.graphicsFamily
			<< " computeFamily=" << m_QueueCapabilities.computeFamily
			<< " dedicatedCompute=" << (m_QueueCapabilities.hasDedicatedAsyncComputeQueue ? "true" : "false"));
	}

	VkDeviceAddress VansVKDevice::GetAccelerationAddress(VkAccelerationStructureDeviceAddressInfoKHR* addressInfo)
	{
		return VansGraphics::vkGetAccelerationStructureDeviceAddressKHR(m_VansVKLogicDevice, addressInfo);
	}

	VkDeviceAddress VansVKDevice::GetBufferAddress(VkBufferDeviceAddressInfo* bufferInfo)
	{
		return VansGraphics::vkGetBufferDeviceAddressKHR(m_VansVKLogicDevice, bufferInfo);
	}

	void VansVKDevice::GetAccelerationStructureBuildSizes(VkAccelerationStructureBuildGeometryInfoKHR* buildInfo, uint32_t* maxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* buildSizeInfo)
	{
		VansGraphics::vkGetAccelerationStructureBuildSizesKHR(
			m_VansVKLogicDevice,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			buildInfo,
			maxPrimitiveCounts,
			buildSizeInfo);
	}

	void VansVKDevice::CreateAccelerationStructure(VkAccelerationStructureCreateInfoKHR* createInfo, VkAccelerationStructureKHR* as)
	{
		VkResult result = VansGraphics::vkCreateAccelerationStructureKHR(m_VansVKLogicDevice, createInfo, nullptr, as);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("CreateAccelerationStructure failed");
		}
	}

	void VansVKDevice::DestroyAccelerationStructure(VkAccelerationStructureKHR as)
	{
		if (as != VK_NULL_HANDLE)
		{
			VansGraphics::vkDestroyAccelerationStructureKHR(m_VansVKLogicDevice, as, nullptr);
		}
	}

	PFN_vkGetDeviceProcAddr VansVKDevice::GetDeviceProcAddr()
	{
		return VansGraphics::vkGetDeviceProcAddr;
	}

	double VansVKDevice::GetTimestampPeriodMs(VkPhysicalDevice physicalDevice)
	{
		VkPhysicalDeviceProperties props{};
		VansGraphics::vkGetPhysicalDeviceProperties(physicalDevice, &props);
		return static_cast<double>(props.limits.timestampPeriod) * 1e-6;
	}

	bool VansVKDevice::CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo& createInfo, VkQueryPool& pool)
	{
		VkResult result = VansGraphics::vkCreateQueryPool(device, &createInfo, nullptr, &pool);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to create query pool: " << result);
			pool = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	void VansVKDevice::DestroyQueryPool(VkDevice device, VkQueryPool& pool)
	{
		if (pool != VK_NULL_HANDLE)
		{
			VansGraphics::vkDestroyQueryPool(device, pool, nullptr);
			pool = VK_NULL_HANDLE;
		}
	}

	void VansVKDevice::CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool pool, uint32_t firstQuery, uint32_t queryCount)
	{
		VansGraphics::vkCmdResetQueryPool(commandBuffer, pool, firstQuery, queryCount);
	}

	void VansVKDevice::CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool pool, uint32_t query)
	{
		VansGraphics::vkCmdWriteTimestamp(commandBuffer, pipelineStage, pool, query);
	}

	void VansVKDevice::CmdBeginDebugLabel(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT& labelInfo)
	{
#ifdef _DEBUG
		if (VansGraphics::vkCmdBeginDebugUtilsLabelEXT)
		{
			VansGraphics::vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &labelInfo);
		}
#else
		(void)commandBuffer;
		(void)labelInfo;
#endif
	}

	void VansVKDevice::CmdEndDebugLabel(VkCommandBuffer commandBuffer)
	{
#ifdef _DEBUG
		if (VansGraphics::vkCmdEndDebugUtilsLabelEXT)
		{
			VansGraphics::vkCmdEndDebugUtilsLabelEXT(commandBuffer);
		}
#else
		(void)commandBuffer;
#endif
	}

	VkResult VansVKDevice::GetQueryPoolResults(VkDevice device, VkQueryPool pool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* data, VkDeviceSize stride, VkQueryResultFlags flags)
	{
		return VansGraphics::vkGetQueryPoolResults(device, pool, firstQuery, queryCount, dataSize, data, stride, flags);
	}

	void VansVKDevice::InitializeGpuProfiler()
	{
#if VANS_PROFILER_ENABLED
		Vans::VansGpuProfiler::Get().Init(
			GetLogicDevice(),
			GetPhysicalDevice(),
			GetGraphicsQueueFamilyIndex());
#endif
	}

	void VansVKDevice::EndGpuProfilerFrame()
	{
#if VANS_PROFILER_ENABLED
		VANS_PROFILER_END_FRAME(GetLogicDevice());
#endif
	}

	void* VansVKDevice::GetNativeGraphicsDevice()
	{
		return &m_VansVKLogicDevice;
	}

	void* VansVKDevice::GetNativeCommandBuffer()
	{
		return &(CurrentGraphicsCommandBuffer().GetVKCommandBuffer());
	}

	bool VansVKDevice::CreateVKFence(bool signaled, VkFence& fence)
	{
		VkFenceCreateInfo fence_create_info =
		{
			 VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
			 nullptr,
			 signaled ? VK_FENCE_CREATE_SIGNALED_BIT : static_cast<VkFenceCreateFlags>(0)
		};

		VkResult result = VansGraphics::vkCreateFence(m_VansVKLogicDevice, &fence_create_info, nullptr, &fence);
		if (VK_SUCCESS != result)
		{
			VANS_LOG_ERROR("Could not create a fence.");
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

		VkResult result = VansGraphics::vkCreateSemaphore(m_VansVKLogicDevice, &semaphore_create_info, nullptr, &semaphore);
		if (VK_SUCCESS != result)
		{
			VANS_LOG_ERROR("Could not create a semaphore.");
			return false;
		}
		return true;
	}

	bool VansVKDevice::CreateVKEvent(VkEvent& eventHandle)
	{
		VkEventCreateInfo event_create_info =
		{
			VK_STRUCTURE_TYPE_EVENT_CREATE_INFO,
			nullptr,
			0
		};

		VkResult result = VansGraphics::vkCreateEvent(m_VansVKLogicDevice, &event_create_info, nullptr, &eventHandle);
		if (VK_SUCCESS != result)
		{
			VANS_LOG_ERROR("Could not create an event.");
			return false;
		}
		return true;
	}

	void VansVKDevice::DestroyVKFence(VkFence& fence)
	{
		if (fence == VK_NULL_HANDLE)
			return;
		VansGraphics::vkDestroyFence(m_VansVKLogicDevice, fence, nullptr);
		fence = VK_NULL_HANDLE;
	}

	void VansVKDevice::DestroyVKSemaphore(VkSemaphore& semaphore)
	{
		if (semaphore == VK_NULL_HANDLE)
			return;
		VansGraphics::vkDestroySemaphore(m_VansVKLogicDevice, semaphore, nullptr);
		semaphore = VK_NULL_HANDLE;
	}

	void VansVKDevice::DestroyVKEvent(VkEvent& eventHandle)
	{
		if (eventHandle == VK_NULL_HANDLE)
			return;
		VansGraphics::vkDestroyEvent(m_VansVKLogicDevice, eventHandle, nullptr);
		eventHandle = VK_NULL_HANDLE;
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
		VansGraphics::vkEnumerateInstanceVersion(&apiVersion);
		VANS_LOG("Vulkan API version: " << VK_VERSION_MAJOR(apiVersion) << "." << VK_VERSION_MINOR(apiVersion) << "." << VK_VERSION_PATCH(apiVersion));

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

#ifdef _DEBUG
		// Chain the messenger create info so the validation layer can report
		// errors that occur during VansGraphics::vkCreateInstance / VansGraphics::vkDestroyInstance.
		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = MakeDebugMessengerCreateInfo();
		instance_create_info.pNext = &debugCreateInfo;
#endif

		VkResult result = VansGraphics::vkCreateInstance(&instance_create_info, nullptr, &m_VansVKInstance);
		if ((result != VK_SUCCESS) || (m_VansVKInstance == VK_NULL_HANDLE))
		{
			VANS_LOG_ERROR("Could not create Vulkan Instance.");
			return false;
		}
		return true;
	}

	bool VansVKDevice::CreateVulkanLogicDevice(std::vector<char const*>& desired_extensions)
	{
		uint32_t devices_count = 0;
		VkResult result = VK_SUCCESS;
		result = VansGraphics::vkEnumeratePhysicalDevices(m_VansVKInstance, &devices_count, nullptr);
		if ((result != VK_SUCCESS) || (devices_count == 0))
		{
			VANS_LOG_ERROR("Could not get the number of available physical devices.");
			return false;
		}

		std::vector<VkPhysicalDevice> available_devices;
		available_devices.resize(devices_count);
		result = VansGraphics::vkEnumeratePhysicalDevices(m_VansVKInstance, &devices_count, &available_devices[0]);
		if ((result != VK_SUCCESS) || (devices_count == 0))
		{
			VANS_LOG_ERROR("Could not enumerate physical devices.");
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
			if (!CheckAvalialeDeviceQueue(device, m_GraphicsQueueFamilyIndex, VK_QUEUE_GRAPHICS_BIT| VK_QUEUE_COMPUTE_BIT))
			{
				continue;
			}

			if (!CheckAvalialeDeviceQueue(device, m_ComputeQueueFamilyIndex, VK_QUEUE_COMPUTE_BIT))
			{
				continue;
			}

			m_SharingQueueFamilyIndices.clear();
			m_SharingQueueFamilyIndices.push_back(m_GraphicsQueueFamilyIndex);
			if (m_ComputeQueueFamilyIndex != m_GraphicsQueueFamilyIndex)
			{
				m_SharingQueueFamilyIndices.push_back(m_ComputeQueueFamilyIndex);
			}
			// Ordinary buffers/images are consumed by graphics and compute queues.
			// The present queue only owns swapchain images and is not selected yet here;
			// adding it used to append an uninitialized family index to every resource.

			//recored all need queue family index
			std::vector<QueueInfo> queue_infos;
			// Request a second graphics queue for parallel shadow rendering when available.
			{
				uint32_t qfCount = 0;
				VansGraphics::vkGetPhysicalDeviceQueueFamilyProperties(device, &qfCount, nullptr);
				std::vector<VkQueueFamilyProperties> qfProps(qfCount);
				VansGraphics::vkGetPhysicalDeviceQueueFamilyProperties(device, &qfCount, qfProps.data());
				if (m_GraphicsQueueFamilyIndex < qfCount &&
					qfProps[m_GraphicsQueueFamilyIndex].queueCount >= 2)
				{
					queue_infos.push_back({ m_GraphicsQueueFamilyIndex, { 1.0f, 1.0f } });
					m_HasDedicatedShadowQueue = true;
				}
				else
				{
					queue_infos.push_back({ m_GraphicsQueueFamilyIndex, { 1.0f } });
					m_HasDedicatedShadowQueue = false;
				}
			}
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

			VkResult result = VansGraphics::vkCreateDevice(device, &device_create_info, nullptr, &m_VansVKLogicDevice);
			if ((result != VK_SUCCESS) || (m_VansVKLogicDevice == VK_NULL_HANDLE))
			{
				VANS_LOG_ERROR("Could not create logical device.");
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
		if (m_HasDedicatedShadowQueue)
		{
			RequestDeviceQueue(m_GraphicsQueueFamilyIndex, 1, m_VansVKShadowQueue);
		}
		else
		{
			m_VansVKShadowQueue = m_VansVKGraphicsQueue;
		}

		m_QueueCapabilities.graphicsFamily = m_GraphicsQueueFamilyIndex;
		m_QueueCapabilities.computeFamily = m_ComputeQueueFamilyIndex;
		m_QueueCapabilities.shadowFamily = m_GraphicsQueueFamilyIndex;
		m_QueueCapabilities.hasDedicatedAsyncComputeQueue =
			m_ComputeQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
			m_ComputeQueueFamilyIndex != m_GraphicsQueueFamilyIndex;
		m_QueueCapabilities.hasDedicatedShadowQueue = m_HasDedicatedShadowQueue;
		m_QueueCapabilities.supportsTimelineSemaphore = m_Features12.timelineSemaphore == VK_TRUE;
		m_FrameSubmitOrchestrator.Bind(
			m_VansVKLogicDevice,
			m_VansVKGraphicsQueue,
			m_VansVKComputeQueue,
			m_VansVKShadowQueue);
		RefreshAsyncComputeState();

		CommandBufferCreateParams params =
		{
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			1
		};
		bool result = m_VansVKCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params);
		if (!result)
		{
			VANS_LOG_ERROR("create m_VansVKCommandBuffer failed");
			return false;
		}
		CreateVKFence(false, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_VansVKRayTracingCommandBuffer.CreateVulkanCommandBuffer(*this, m_ComputeQueueFamilyIndex, params);
		if (!result)
		{
			VANS_LOG_ERROR("create m_VansVKRayTracingCommandBuffer failed");
			return false;
		}
		CreateVKFence(false, m_VansVKRayTracingCommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_VansVKAsyncCloudCommandBuffer.CreateVulkanCommandBuffer(*this, m_ComputeQueueFamilyIndex, params);
		if (!result)
		{
			VANS_LOG_ERROR("create m_VansVKAsyncCloudCommandBuffer failed");
			return false;
		}
		CreateVKFence(false, m_VansVKAsyncCloudCommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_VansVKShadowCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params);
		if (!result)
		{
			VANS_LOG_ERROR("create m_VansVKShadowCommandBuffer failed");
			return false;
		}
		CreateVKFence(false, m_VansVKShadowCommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_VansVKGBufferCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params);
		if (!result)
		{
			VANS_LOG_ERROR("create m_VansVKGBufferCommandBuffer failed");
			return false;
		}
		CreateVKFence(false, m_VansVKGBufferCommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_VansVKGraphicsPreCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params);
		if (!result)
		{
			VANS_LOG_ERROR("create m_VansVKGraphicsPreCommandBuffer failed");
			return false;
		}
		CreateVKFence(false, m_VansVKGraphicsPreCommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_VansVKGraphicsScreenCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params);
		if (!result)
		{
			VANS_LOG_ERROR("create m_VansVKGraphicsScreenCommandBuffer failed");
			return false;
		}
		CreateVKFence(false, m_VansVKGraphicsScreenCommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_VansVKAsyncGICommandBuffer.CreateVulkanCommandBuffer(*this, m_ComputeQueueFamilyIndex, params);
		if (!result)
		{
			VANS_LOG_ERROR("create m_VansVKAsyncGICommandBuffer failed");
			return false;
		}
		CreateVKFence(false, m_VansVKAsyncGICommandBuffer.m_CommandBufferFinishSubmitFence);

		result = m_ImmediateGraphicsCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params);
		if (!result)
		{
			VANS_LOG_ERROR("create m_ImmediateGraphicsCommandBuffer failed");
			return false;
		}
		CreateVKFence(false, m_ImmediateGraphicsCommandBuffer.m_CommandBufferFinishSubmitFence);


		VansVKMemoryManager::GetInstance()->BindDevice(*this);
		// Bring up the VMA allocator before any buffer/image is created.
		if (!VansVKMemoryAllocator::Get().Initialize(*this, VK_API_VERSION_1_2))
		{
			VANS_LOG_ERROR("Failed to initialize VMA allocator.");
			return false;
		}
		VansVKDescriptorManager::GetInstance()->BindDevice(m_VansVKPhysicalDevice, m_VansVKLogicDevice, m_VansVKCommandBuffer.GetVKCommandBuffer());
		VansVKDescriptorManager::GetInstance()->CreateDescriptorPool(true);

		if (!m_StageBuffer.CreatVulkanBuffer(m_VansVKLogicDevice, 1024 * 1024 * 512, VK_FORMAT_R32_SFLOAT, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ||
			!m_StageBuffer.PersistentMap())
		{
			VANS_LOG_ERROR("create persistently mapped frame stage buffer failed");
			return false;
		}

		return true;
	}

	bool VansVKDevice::DestroyVulkanLogicDevice()
	{
		if (m_VansVKLogicDevice != VK_NULL_HANDLE)
		{
			WaitForDevice();
			m_FrameSubmitOrchestrator.Shutdown();
			// Persist cache entries before shader-owned pipeline objects are released.
			m_PipelineCacheService.Flush(VansPipelineCacheFlushReason::Shutdown);
		}

		VansShaderManager::Get().ReleaseLoadedShaderAssets();
		VansPipelineRegistry::Get().Clear();
		m_PipelineCacheService.Shutdown();

		VansVKDescriptorManager::GetInstance()->DestroyDescriptorPool();

		m_StageBuffer.DestroyVulkanBuffer(m_VansVKLogicDevice);
		DestroyFrameContextRingResources();

		DestroyVKFence(m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_VansVKRayTracingCommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_VansVKAsyncCloudCommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_VansVKShadowCommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_VansVKGBufferCommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_VansVKGraphicsPreCommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_VansVKGraphicsScreenCommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_VansVKAsyncGICommandBuffer.m_CommandBufferFinishSubmitFence);
		DestroyVKFence(m_ImmediateGraphicsCommandBuffer.m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansVKRayTracingCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansVKAsyncCloudCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansVKShadowCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansVKGBufferCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansVKGraphicsPreCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansVKGraphicsScreenCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_VansVKAsyncGICommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
		m_ImmediateGraphicsCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);

		// Tear down VMA before destroying the logical device. All buffer/image
		// owners must have released their allocations by this point.
		VansVKMemoryAllocator::Get().Shutdown();

		if (m_VansVKLogicDevice)
		{
			VansGraphics::vkDestroyDevice(m_VansVKLogicDevice, nullptr);
			m_VansVKLogicDevice = VK_NULL_HANDLE;
		}
		return true;
	}

	bool VansVKDevice::DestroyVulkanInstance()
	{
		if (m_VansVKInstance)
		{
			VansGraphics::vkDestroyInstance(m_VansVKInstance, nullptr);
			m_VansVKInstance = VK_NULL_HANDLE;
		}
		return true;
	}

	bool VansVKDevice::VulkanSetUp(VkExtent2D resolution)
	{
		// This is intentionally optional. When launched normally there is no
		// RenderDoc dependency; when RenderDoc injected its DLL, configure its API
		// before creating the Vulkan instance.
		VansRenderDocCapture::Get().Initialize();

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
		// RenderDoc's Vulkan layer is injected by RenderDoc itself. Requiring
		// VK_LAYER_RENDERDOC_Capture here makes ordinary debug launches fail on
		// machines without RenderDoc and incorrectly disables capture with RT on.

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

#ifdef _DEBUG
		SetupDebugMessenger();
#endif

		if (!m_NativeWindowProvider)
		{
			VANS_LOG_ERROR("Vulkan surface creation failed: native window provider is null");
			return false;
		}

		if (!m_VansVKSurface.CreateVulkanPresentSurface(
			m_VansVKInstance,
			static_cast<GLFWwindow*>(m_NativeWindowProvider->GetNativeWindowHandle())))
		{
			return false;
		}

#ifdef _WIN32
		VansRenderDocCapture::Get().BindVulkanWindow(
			static_cast<void*>(m_VansVKInstance),
			static_cast<void*>(glfwGetWin32Window(
				static_cast<GLFWwindow*>(m_NativeWindowProvider->GetNativeWindowHandle()))));
#endif

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

		// Pipeline cache persistence is an optional optimization. Failure must not
		// prevent the editor or a packaged build from starting.
		if (!m_PipelineCacheService.Initialize(m_VansVKPhysicalDevice, m_VansVKLogicDevice))
		{
			VANS_LOG_WARN("[PipelineCache] Persistent cache disabled for this run");
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

	#ifdef _DEBUG
	VkDebugUtilsMessengerCreateInfoEXT VansVKDevice::MakeDebugMessengerCreateInfo()
	{
		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		createInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		// Use OutputDebugStringA – safe to call from any context including inside
		// a Vulkan API call. Avoid engine logger here to prevent re-entrancy.
		createInfo.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void*) -> VkBool32
		{
			// Skip benign layer-naming warnings (e.g. NVIDIA GPU Trace layer)
			if (pCallbackData->pMessageIdName &&
				strcmp(pCallbackData->pMessageIdName, "Loader Message") == 0 &&
				strstr(pCallbackData->pMessage, "does not conform to naming standard"))
			{
				return VK_FALSE;
			}
			const char* prefix = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
				? "[VK ERROR] " : "[VK WARN]  ";
			OutputDebugStringA(prefix);
			OutputDebugStringA(pCallbackData->pMessage);
			OutputDebugStringA("\n");
			return VK_FALSE;
		};
		return createInfo;
	}

	bool VansVKDevice::SetupDebugMessenger()
	{
		VkDebugUtilsMessengerCreateInfoEXT createInfo = MakeDebugMessengerCreateInfo();
		VkResult result = VansGraphics::vkCreateDebugUtilsMessengerEXT(m_VansVKInstance, &createInfo, nullptr, &m_DebugMessenger);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_WARN("Could not create debug utils messenger. Validation output will be unavailable.");
			return false;
		}
		VANS_LOG("Vulkan validation layer debug messenger created.");
		return true;
	}

	void VansVKDevice::DestroyDebugMessenger()
	{
		if (m_DebugMessenger != VK_NULL_HANDLE)
		{
			VansGraphics::vkDestroyDebugUtilsMessengerEXT(m_VansVKInstance, m_DebugMessenger, nullptr);
			m_DebugMessenger = VK_NULL_HANDLE;
		}
	}
	#endif

	bool VansVKDevice::VulkanDestroy()
	{
		WaitForDevice();
		FlushCurrentFrameDeferredDeletes();
		CleanupFSR();
		{
			m_VansVKSurface.DestroyVulkanSwapChain(m_VansVKLogicDevice);
			m_VansVKSurface.DestroyVulkanPresentSurface(m_VansVKInstance);
		}
		DestroyVulkanLogicDevice();
#ifdef _DEBUG
		DestroyDebugMessenger();
#endif
		DestroyVulkanInstance();
		UnloadVulkanLibrary();
		return true;
	}
}

