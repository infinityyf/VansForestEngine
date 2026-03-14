#pragma once
#include "../VansGraphicsDevice.h"
#include "vulkan/vulkan.h"
#include "VansVKSurface.h"
#include "VansVKBuffer.h"
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "VansShader.h"
#include "../RayTracingCore/VansRayTracing.h"
#include <vector>

#include "../../ScriptCore/VansCommonUtils.h"
#include "../FidelityFXCore/VansFSR.h"
namespace VansGraphics
{
	struct QueueInfo 
	{
		uint32_t FamilyIndex;
		std::vector<float> Priorities;
	};

	class VansRenderPassManager;
	

	class VansVKDevice: public VansGraphicsDevice
	{
	private :
		//memory update
		VansVKBuffer m_StageBuffer;

		//梭有cmd都写在这
	public :

		bool SetDeviceBufferData(VansVKBuffer& dest_buffer, void* data, int data_offset, int data_size, VkDeviceSize buffer_offset, VkDeviceSize buffer_size);

		bool SetDeviceImageData(VansVKImage& dest_image, VansVKCommandBuffer& cmd, void* data, int data_offset, int data_size, VkOffset3D image_offset, VkExtent3D image_size, int mip_level, int layer_level);

	private:

		VansFSR m_FSRController;

		//用于dispatch的信息
		FSRInput m_FSRInput;

		void PrepareFSRDispatchInputData(float fovy, float nearPlane, float farPlane);

		void DispatchFSRUpscale();

		void InitializeFSR();

		void CleanupFSR();

	public:

		void BeginUIRenderPass();

		void EndUIRenderPass();

		// 窗口大小改变时重建交换链和UI渲染pass
		void OnWindowResize(uint32_t width, uint32_t height);

	public:

		//初始化被渲染的数据
		void BeforeRendering() override;

		void Rendering() override;

		void Present() override;
		//释放被渲染数据
		void AfterRendering() override;

		void* GetNativeGraphicsDevice() override;

		void* GetNativeCommandBuffer() override;

		bool CreateVKFence(bool signaled, VkFence& fence);

		bool CreateVKSemaphore(VkSemaphore& semaphore);

		bool CreateVKEvent(VkEvent& eventHandle);

		void DestroyVKFence(VkFence& fence);

		void DestroyVKSemaphore(VkSemaphore& semaphore);

		void DestroyVKEvent(VkEvent& eventHandle);

		//获取physics device
		VkPhysicalDevice GetPhysicalDevice() { return m_VansVKPhysicalDevice; }

		//获取logic device
		VkDevice& GetLogicDevice() { return m_VansVKLogicDevice; }

		VkInstance GetInstance() { return m_VansVKInstance; }

		//获取device properties
		VkPhysicalDeviceProperties GetDeviceProperties() { return m_DeviceProperties; }

		VkPhysicalDeviceRayTracingPipelinePropertiesKHR GetRayTracingProperties() { return m_RayTracingProperties; }

		//获取graphics queue
		VkQueue& GetGraphicsQueue() { return m_VansVKGraphicsQueue; };

		//获取surface
		VansVKSurface& GetSurface() { return m_VansVKSurface; }

		VansVKCommandBuffer& GetCommandBuffer() { return m_VansVKCommandBuffer; }

		VansVKCommandBuffer& GetEditorCommandBuffer() { return m_VansEditorCommandBuffer; }

		GlobalStateData& GetGlobalRenderStateData() { return m_globalRenderStateData; }

		uint32_t GetGraphicsQueueFamilyIndex() { return m_GraphicsQueueFamilyIndex; }

		uint32_t GetComputeQueueFamilyIndex() { return m_ComputeQueueFamilyIndex; }

		uint32_t GetPresentQueueFamilyIndex() { return m_PresentQueueFamilyIndex; }

		const std::vector<uint32_t>& GetSharingQueueFamilyIndices() const { return m_SharingQueueFamilyIndices; }

		void PrepareRenderingData();

		
		void DrawShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd);

		void DrawPunctualShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd);

		void DrawSceneForward(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd);

		void DrawSceneDeferred(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);

		VkDeviceAddress GetAccelerationAddress(VkAccelerationStructureDeviceAddressInfoKHR* addressInfo);

		VkDeviceAddress GetBufferAddress(VkBufferDeviceAddressInfo* bufferInfo);

		void GetAccelerationStructureBuildSizes(VkAccelerationStructureBuildGeometryInfoKHR* buildInfo, uint32_t* maxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* buildSizeInfo);

		void CreateAccelerationStructure(VkAccelerationStructureCreateInfoKHR* createInfo, VkAccelerationStructureKHR* as);

	public:

		void UpdateGIData(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateHZB(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateSSR(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateVolumetricFog(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateFogLightInjection(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateFogRayMarch(VansVKCommandBuffer& computeCmd);

	private:

		void UpdateSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void TemporalFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void BilateralFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void BilateralFilterSSAO(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

	private:

		void UpdateGIDataDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateHZBDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateSSRDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateVolumetricFogSets(VansRenderPassManager* renderPassManager);

		void UpdateFogLightInjectionSets(VansRenderPassManager* renderPassManager);

		void UpdateFogRayMarchSets();

	private:

		void UpdateRayTracing(VansVKCommandBuffer& computeCmd);

	private:

		//记录全局的渲染参数，需要和相机绑定
		GlobalStateData m_globalRenderStateData;

		void PreparePBRMaterialData();

		void PrepareInstanceTransformData();

		void PrepareSkyRenderData();

		void PrepareSSAORenderData();

		void PrepareSSGIRenderData();

		void PrepareHZBRenderData();

		void PrepareSSRRenderData();

		void PrepareVolumetricData();

		void PrepareBilaterFilterData();

		void PrepareRayTracingData();

		void PrepareGlobalIllumiationData();

	private:

		//用于渲染GPU上进行同步
		uint32_t m_SwapChainImageIndex;

		VkSemaphore m_SwapChainImageAcquiredSemaphore;

		VkFence m_SwapChainImageAcquiredFence;

		VkSemaphore m_CommandBufferReadyToPresentSemaphore;

		// Async compute event used for compute -> graphics synchronization.
		VkEvent m_AsyncComputeCompletedEvent;

		// Set to true to enable async compute.
		bool m_UseAsyncCompute = false;

		VkPhysicalDeviceProperties m_DeviceProperties;
		
		//ray tracing相关的扩展
		VkPhysicalDeviceRayTracingPipelineFeaturesKHR m_RaytracingFeature;
		VkPhysicalDeviceAccelerationStructureFeaturesKHR m_AcceralteFeature;
		VkPhysicalDeviceVulkan12Features m_Features12;
		VkPhysicalDeviceVulkan11Features m_Features11;

		VkPhysicalDeviceScalarBlockLayoutFeatures m_ScalarBlockFeature;
		VkPhysicalDeviceDescriptorIndexingFeatures m_DescriptorIndexingFeature;

		VkPhysicalDeviceAccelerationStructurePropertiesKHR m_AccelerationProps;
		VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_RayTracingProperties;

		VkPhysicalDeviceFeatures2 m_DeviceFeatures2;
		VkPhysicalDeviceProperties2 m_DeviceProperties2;

		VansVKSurface m_VansVKSurface;

		VkInstance m_VansVKInstance;

#ifdef _DEBUG
		VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
#endif

		VkPhysicalDevice m_VansVKPhysicalDevice;

		VkDevice m_VansVKLogicDevice;

		//queues
		VkQueue m_VansVKGraphicsQueue;
		VkQueue m_VansVKComputeQueue;

		//command buffer
		VansVKCommandBuffer m_VansVKCommandBuffer;

		VansVKCommandBuffer m_VansVKComputeCommandBuffer;

		VansVKCommandBuffer m_VansVKRayTracingCommandBuffer;
		VansRayTracing rayTracingContext;
		
		VansVKCommandBuffer m_VansEditorCommandBuffer;
		
	private:
		std::vector<uint32_t> m_SharingQueueFamilyIndices;

		//recored all supported queue before device create
		uint32_t m_GraphicsQueueFamilyIndex;
		uint32_t m_ComputeQueueFamilyIndex;
		uint32_t m_PresentQueueFamilyIndex;

	private:

		//instance related
		std::vector<const char*> m_EnabledInstanceExtensions;

		//device releated
		std::vector<const char*> m_EnabledDeviceExtensions;

		VkPhysicalDeviceProperties m_AvailableDeviceProperties;

		VkPhysicalDeviceFeatures m_AvailableDeviceFeatures;

	private :

		VkExtent2D m_RawResolution;


	private :

		bool PrepareVulkanLibrary();

#ifdef _DEBUG
		static VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo();
		bool SetupDebugMessenger();
		void DestroyDebugMessenger();
#endif

	private:
		//get call avaliable extensions
		bool CheckAvaliableInstanceExtensions(std::vector<VkExtensionProperties>& available_extensions);

		bool CheckAvaliableInstanceLayer(std::vector<VkLayerProperties>& available_layers);

		bool CheckAvaliableDeviceExtensions(VkPhysicalDevice device, std::vector<VkExtensionProperties>& available_extensions);

		bool CheckAvalialeDeviceQueue(VkPhysicalDevice device, uint32_t& queue_family_index, VkQueueFlags desired_capabilty);

		bool CheckPhysicDeviceFeature(VkPhysicalDevice device);

		bool IsExtensionSupported(const std::vector<VkExtensionProperties>& available_extensions, char const* desire_extension);

		bool IsLayersSupported(const std::vector<VkLayerProperties>& available_layers, char const* desire_layer);

		void RequestDeviceQueue(uint32_t queue_family_index, uint32_t queue_index, VkQueue& queue);

		bool CreateVulkanInstance(std::vector<char const*>& desired_extensions, std::vector<char const*>& desired_layers);

		bool CreateVulkanLogicDevice(std::vector<char const*>& desired_extensions);

		bool InitVulkanLogicDevice();

		bool DestroyVulkanLogicDevice();

		bool DestroyVulkanInstance();

		bool VulkanSetUp(VkExtent2D resolution);

		bool VulkanDestroy();
	public:
		VansVKDevice(VkExtent2D resolution)
		{
			m_RenderWidth = resolution.width;
			m_RenderHeight = resolution.height;
			m_GraphicsAPI = GRAPHICS_API::VULKAN;
			VulkanSetUp(resolution);
			
		}

		~VansVKDevice()
		{
			m_GraphicsAPI = GRAPHICS_API::INVALIDE;
			VulkanDestroy();
		}

		// vkQueueWaitIdle wait for all command buffer in this queue
		bool WaitForQueue(VkQueue queue);

		bool WaitForDevice();

	};
}

////pipeline layout
////Pipeline layouts define what types of resources can be accessed by a given pipeline
////创建shader和资源之间的对应关系
////descriptor + pushconstant （不同的shaderstage需要指定offset 和size，并且这个buffer的大小有限制）
////创建一个uniform+image的例子
//std::vector<VkDescriptorSetLayoutBinding> descriptor_set_layout_bindings =
//{
//	 {
//		 0,
//		 VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
//		 1,
//		 VK_SHADER_STAGE_FRAGMENT_BIT,
//		 nullptr
//	 },
//	 {
//		 1,
//		 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
//		 1,
//		 VK_SHADER_STAGE_VERTEX_BIT,
//		 nullptr
//	 }
//};
//VkDescriptorSetLayout descriptor_set_layout;
//bool result = VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(descriptor_set_layout_bindings, descriptor_set_layout);
//if (!result)
//{
//	std::cerr << "create descriptor set layout failed" << std::endl;
//	return false;
//}
////通过指定描述符集的layout到pipelinelayout，就可以绑定对应的描述符集到对应的bind point,但是首先需要创建对应的描述符集
////这里先每次创建，实际上只需要创建一次
//VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ descriptor_set_layout }, descriptor_set);