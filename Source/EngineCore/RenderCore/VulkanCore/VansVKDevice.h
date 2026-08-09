#pragma once
#include "../VansGraphicsDevice.h"
#include "vulkan/vulkan.h"
#include "VansVKSurface.h"
#include "VansVKBuffer.h"
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "VansVKSecondaryCommandContext.h"
#include "VansRenderGraph.h"
#include "VansRenderGraphVulkanSync.h"
#include "VansPipelineCacheService.h"
#include "VansShader.h"
#include "../RayTracingCore/VansRayTracing.h"
#include <vector>

#include "../../ScriptCore/VansCommonUtils.h"
#include "../FidelityFXCore/VansFSR.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace VansGraphics
{
	class INativeWindowProvider;
	class VansVKImage;

	struct VansTextureMipChainUpload
	{
		VansVKImage* destImage = nullptr;
		const void* data = nullptr;
		int dataSize = 0;
		std::vector<VkBufferImageCopy> regions;
		VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	};

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
		VkDeviceSize m_FrameStageBufferOffset = 0;
		VkDeviceSize m_FrameStageBufferBaseOffset = 0;
		VkDeviceSize m_FrameStageBufferCapacity = 0;

		//梭有cmd都写在这
	public :

		bool SetDeviceBufferData(VansVKBuffer& dest_buffer, void* data, int data_offset, int data_size, VkDeviceSize buffer_offset, VkDeviceSize buffer_size);

		bool SetDeviceImageData(VansVKImage& dest_image, VansVKCommandBuffer& cmd, void* data, int data_offset, int data_size, VkOffset3D image_offset, VkExtent3D image_size, int mip_level, int layer_level);
		bool SetDeviceImageData(VansVKImage& dest_image, VansVKCommandBuffer& cmd, void* data, int data_offset, int data_size, VkOffset3D image_offset, VkExtent3D image_size, int mip_level, int layer_level, VkImageLayout finalLayout);
		bool SetDeviceImageMipChainData(VansVKImage& destImage,
			VansVKCommandBuffer& cmd,
			const void* data,
			int dataSize,
			const std::vector<VkBufferImageCopy>& regions,
			VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		bool SubmitTextureMipChainUploadBatch(
			VansVKCommandBuffer& cmd,
			const std::vector<VansTextureMipChainUpload>& uploads);

		// 每帧开始时重置临时上传分配器。该接口只重置 CPU 侧 offset，调用前必须确保上一帧图形提交已完成。
		void ResetFrameStageUploadAllocator();

		// 将图片数据写入本帧 staging ring，并把 copy/barrier 记录到已 Begin 的 command buffer。
		// 不执行 vkQueueSubmit / fence wait，专用于视频等高频逐帧上传。
		bool RecordDeviceImageData(VansVKImage& destImage,
			VansVKCommandBuffer& cmd,
			const void* data,
			int dataSize,
			VkOffset3D imageOffset,
			VkExtent3D imageSize,
			int mipLevel,
			int layerLevel,
			VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		bool RecordDeviceImageBufferData(VansVKImage& destImage,
			VansVKCommandBuffer& cmd,
			VansVKBuffer& sourceBuffer,
			VkDeviceSize sourceOffset,
			VkOffset3D imageOffset,
			VkExtent3D imageSize,
			int mipLevel,
			int layerLevel,
			VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	private:

		VansFSR m_FSRController;

		//用于dispatch的信息
		FSRInput m_FSRInput;
		VansFSRMode m_FSRMode = VansFSRMode::MatchViewport;
		VkExtent2D m_RequestedSceneViewportExtent{ 0, 0 };
		bool m_FSRConfigDirty = false;
		bool m_PresentFSROutputToSwapchain = false;

		void PrepareFSRDispatchInputData(float fovy, float nearPlane, float farPlane);

		void DispatchFSRUpscale();
		void RecordFSROutputToSwapchain();

		void InitializeFSR();

		void CleanupFSR();

		VkExtent2D CalculateFSROutputExtent() const;
		void ProcessPendingFSRConfig();

	public:

		void BeginUIRenderPass() override;

		void EndUIRenderPass() override;

		bool CanRecordCurrentFrame() const override { return m_CurrentFrameContext.frameSubmitSucceeded; }

		/// 运行时 UI pass（Noesis → FSR 输出图像）
		void BeginSceneUIRenderPass();
		void EndSceneUIRenderPass();

		/// 返回 Scene UI pass 的 VkRenderPass 句柄，供 Noesis RenderDevice 懒编译 PSO 使用
		VkRenderPass GetSceneUIRenderPassHandle();

		/// Return the FSR-upscaled output image (display resolution) for editor sampling.
		VansVKImage& GetFSROutputImage() { return m_FSRController.GetTempFSRImage(); }

		// 查询 FSR 内置抖动偏移（像素空间 [-0.5, 0.5]），供 VansCamera 替代 Halton 序列
		bool GetFSRJitterOffset(uint32_t frameIndex, float& outPixelX, float& outPixelY) override;
		float GetUpscaleMipBias() const override;

		void RequestFSRConfig(VansFSRMode mode, uint32_t viewportWidth, uint32_t viewportHeight, float sharpness);
		void SetRuntimeSwapchainPresentationEnabled(bool enabled) { m_PresentFSROutputToSwapchain = enabled; }
		VansFSRMode GetFSRMode() const { return m_FSRMode; }
		float GetFSRSharpness() const { return m_FSRController.GetSharpness(); }
		bool IsParallelCommandRecordingEnabled() const { return m_EnableParallelCommandRecording; }
		void SetParallelCommandRecordingEnabled(bool enabled) { m_EnableParallelCommandRecording = enabled; }
		bool IsFrameContextRingEnabled() const { return m_EnableFrameContextRing; }
		uint32_t GetConfiguredFramesInFlight() const { return m_ConfiguredFramesInFlight; }
		void SetFrameContextRingEnabled(bool enabled, uint32_t framesInFlight);
		VkExtent2D GetRequestedSceneViewportExtent() const { return m_RequestedSceneViewportExtent; }

		// 窗口大小改变时重建交换链和UI渲染pass
		void OnWindowResize(uint32_t width, uint32_t height) override;

	public:

		//初始化被渲染的数据
		void BeforeRendering() override;
		void PrepareRenderingFrame() override
		{
			ProcessPendingFSRConfig();
			m_PipelineCacheService.TickPersistence();
		}

		void Rendering() override;

		void Present() override;
		//释放被渲染数据
		void AfterRendering() override;

		bool WaitForIdle() override { return WaitForDevice(); }

		const VansRenderGraphDiagnosticsSnapshot& GetCurrentRenderGraphDiagnostics() const { return m_CurrentRenderGraphDiagnostics; }
		const std::string& GetCurrentRenderGraphDebugSummary() const;
		const VansFrameContext& GetCurrentFrameContext() const { return m_CurrentFrameContext; }
		void EnqueueDeferredDelete(std::function<void()> destroy);

		void InitializeGpuProfiler() override;

		void EndGpuProfilerFrame() override;

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
		VansPipelineCacheService& GetPipelineCacheService() { return m_PipelineCacheService; }

		VkInstance GetInstance() { return m_VansVKInstance; }

		//获取device properties
		VkPhysicalDeviceProperties GetDeviceProperties() { return m_DeviceProperties; }

		VkPhysicalDeviceRayTracingPipelinePropertiesKHR GetRayTracingProperties() { return m_RayTracingProperties; }
		VkDeviceSize GetAccelerationStructureScratchAlignment() const
		{
			return m_AccelerationProps.minAccelerationStructureScratchOffsetAlignment > 0
				? m_AccelerationProps.minAccelerationStructureScratchOffsetAlignment
				: 1;
		}

		//获取graphics queue
		VkQueue& GetGraphicsQueue() { return m_VansVKGraphicsQueue; };

		//获取surface
		VansVKSurface& GetSurface() { return m_VansVKSurface; }

		VansVKCommandBuffer& GetCommandBuffer() { return *m_pActiveCommandBuffer; }

		VansVKCommandBuffer& GetImmediateGraphicsCommandBuffer() { return m_ImmediateGraphicsCommandBuffer; }

		void SetNativeWindowProvider(INativeWindowProvider* provider) { m_NativeWindowProvider = provider; }

		GlobalStateData& GetGlobalRenderStateData() { return m_globalRenderStateData; }

		uint32_t GetGraphicsQueueFamilyIndex() { return m_GraphicsQueueFamilyIndex; }

		uint32_t GetComputeQueueFamilyIndex() { return m_ComputeQueueFamilyIndex; }

		uint32_t GetPresentQueueFamilyIndex() { return m_PresentQueueFamilyIndex; }

		const std::vector<uint32_t>& GetSharingQueueFamilyIndices() const { return m_SharingQueueFamilyIndices; }

		void PrepareRenderingData();

		// IES profile 纹理数组：在场景加载完成后调用，创建 GPU 资源并上传所有已解析的 IES profile
		void PrepareIESProfileData();

		
		void DrawShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd);

		void DrawMotionVectorPass(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd);

		void DrawPunctualShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd, uint32_t atlasIndex);
		bool RecordShadowMapParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex);

		void DrawSceneForward(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);

		void DrawSceneGBuffer(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		bool RecordSceneGBufferParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex = 0);
		bool RecordMotionVectorPassParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex = 0);
		bool RecordDecalPassParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex = 0);

		// 拆分后的渲染 pass：
		//   DrawSceneDeferredSkybox  — ScreenSpaceFeature + Deferred + SkyBox（写入 SceneColor）
		//   DrawSceneTransparentPost — ForwardOpaqueAfterDeferred + Transparent + Particles + PostProcess（读 SceneColor，写 PostProcess 输出）
		// 设计文档 §6.1 Pass 6 = DrawSceneDeferredSkybox，Pass 10-12 = DrawSceneTransparentPost
		void DrawSceneDeferredSkybox(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void DrawSceneTransparentPost(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void DrawHairLighting(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void DrawHairComposite(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void ClearHairOITResources(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void PrepareHairOITForResolve(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void CopyOpaqueSceneColorForTransmission(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		VkDescriptorSetLayout GetHairOITPassLayout() const { return m_HairLightingPassLayout; }
		VkDescriptorSet GetHairOITPassDescriptorSet() const
		{
			return m_HairLightingPassSets.empty() ? VK_NULL_HANDLE : m_HairLightingPassSets[0];
		}
		VkDescriptorSetLayout GetTransmissionGlassPassLayout() const { return m_TransmissionGlassPassLayout; }
		VkDescriptorSet GetTransmissionGlassPassDescriptorSet() const
		{
			return m_TransmissionGlassPassSets.empty() ? VK_NULL_HANDLE : m_TransmissionGlassPassSets[0];
		}

		VkDeviceAddress GetAccelerationAddress(VkAccelerationStructureDeviceAddressInfoKHR* addressInfo);

		VkDeviceAddress GetBufferAddress(VkBufferDeviceAddressInfo* bufferInfo);

		void GetAccelerationStructureBuildSizes(VkAccelerationStructureBuildGeometryInfoKHR* buildInfo, uint32_t* maxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* buildSizeInfo);

		void CreateAccelerationStructure(VkAccelerationStructureCreateInfoKHR* createInfo, VkAccelerationStructureKHR* as);
		void DestroyAccelerationStructure(VkAccelerationStructureKHR as);

		static PFN_vkGetDeviceProcAddr GetDeviceProcAddr();
		static double GetTimestampPeriodMs(VkPhysicalDevice physicalDevice);
		static bool CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo& createInfo, VkQueryPool& pool);
		static void DestroyQueryPool(VkDevice device, VkQueryPool& pool);
		static void CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool pool, uint32_t firstQuery, uint32_t queryCount);
		static void CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool pool, uint32_t query);
		static void CmdBeginDebugLabel(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT& labelInfo);
		static void CmdEndDebugLabel(VkCommandBuffer commandBuffer);
		static VkResult GetQueryPoolResults(VkDevice device, VkQueryPool pool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* data, VkDeviceSize stride, VkQueryResultFlags flags);

	public:
		VansRayTracing& GetRayTracingContext() { return rayTracingContext; }

		/// 场景卸载时调用：重置所有渲染 Feature 的 descriptor set 一次性写入标记，
		/// 使下次场景加载后重新绑定运行时纹理，避免引用已销毁的 VkImageView。
		void ResetFeatureDescriptorSets()
		{
			++m_FeatureDescriptorGeneration;
		}

		void UpdateGIData(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateHZB(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateMainCameraHiZCull(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateSSR(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateScreenSpaceShadow(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdatePunctualShadowDebugPreview(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateVolumetricFog(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateFogLightInjection(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd, uint32_t frameIdx);

		void UpdateFogRayMarch(VansVKCommandBuffer& computeCmd, uint32_t frameIdx);

		// 体积云 1/4 分辨率光线步进（Compute Pass，在 Deferred 之前执行）
		void UpdateCloudRayMarch(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		// TileLight Build pass: culls lights per tile each frame
		void BuildTileLightLists(VansVKCommandBuffer& cmd);

		// 后处理 Compute Pass：自动曝光与 Bloom
		void UpdateExposure(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);
		void UpdateBloom(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);
		// 上传逐帧曝光参数，并在配置变化时更新 Bloom 参数。
		void UploadPostProcessProfileIfDirty();
		void ProcessPendingGISettings();

	private:

		uint64_t m_FeatureDescriptorGeneration = 1;
		uint64_t m_GIDataDescSetGeneration = 0;
		uint64_t m_HZBDescSetGeneration = 0;
		uint64_t m_HIZSeedDescSetGeneration = 0;
		uint64_t m_OcclusionHZBDescSetGeneration = 0;
		uint64_t m_OcclusionHIZSeedDescSetGeneration = 0;
		uint64_t m_MainCameraHiZCullDescSetGeneration = 0;
		uint64_t m_SSRDescSetGeneration = 0;
		uint64_t m_VolumetricFogDescSetGeneration = 0;
		uint64_t m_FogLightInjectionDescSetGeneration = 0;
		uint64_t m_FogRayMarchDescSetGeneration = 0;
		uint64_t m_TileLightBuildDescSetGeneration = 0;
		uint64_t m_PPExposureDescSetGeneration = 0;
		uint64_t m_PPBloomDescSetGeneration = 0;
		uint64_t m_CloudRayMarchDescSetGeneration = 0;
		uint64_t m_ScreenSpaceShadowDescSetGeneration = 0;

		VkDescriptorSetLayout m_HairCompositePassLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_HairCompositePassSets;
		bool m_HairCompositeDescriptorsReady = false;
		VkDescriptorSetLayout m_HairLightingPassLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_HairLightingPassSets;
		bool m_HairLightingDescriptorsReady = false;
		VkDescriptorSetLayout m_TransmissionGlassPassLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_TransmissionGlassPassSets;
		bool m_TransmissionGlassDescriptorsReady = false;

		void SetupHairLightingDescriptors(VansRenderPassManager* renderPassManager);
		void DestroyHairLightingDescriptors();
		void SetupHairCompositeDescriptors(VansRenderPassManager* renderPassManager);
		void DestroyHairCompositeDescriptors();
		void SetupTransmissionGlassDescriptors(VansRenderPassManager* renderPassManager);
		void DestroyTransmissionGlassDescriptors();

		bool IsFeatureDescriptorCurrent(uint64_t generation) const
		{
			return generation == m_FeatureDescriptorGeneration;
		}

		void MarkFeatureDescriptorCurrent(uint64_t& generation)
		{
			generation = m_FeatureDescriptorGeneration;
		}

		void UpdateSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void TemporalFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void BilateralFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);
		void AtrousFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void BilateralFilterSSAO(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

	private:

		void MaybeDumpGIDebugFrame(VansRenderPassManager* renderPassManager);

		void UpdateGIDataDescriptorSets(VansRenderPassManager* renderPassManager);
		void UploadSSGIParamsFromGISettings();

		void UpdateHIZSeedDescriptorSet(VansRenderPassManager* renderPassManager);

		void UpdateHZBDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateOcclusionHIZSeedDescriptorSet(VansRenderPassManager* renderPassManager);

		void UpdateOcclusionHZBDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateMainCameraHiZCullDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateSSRDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateScreenSpaceShadowSets(VansRenderPassManager* renderPassManager);

		void UpdateVolumetricFogSets(VansRenderPassManager* renderPassManager);

		void UpdateFogLightInjectionSets(VansRenderPassManager* renderPassManager);

		void UpdateFogRayMarchSets();

		// 体积云描述符集写入（一次性，场景加载后首帧调用）
		void UpdateCloudRayMarchSets(VansRenderPassManager* renderPassManager);

		void UpdateTileLightBuildSets();

		// 后处理 Compute Pass descriptor set 写入（一次性）
		void UpdateExposureDescriptorSets(VansRenderPassManager* renderPassManager);
		void UpdateBloomDescriptorSets(VansRenderPassManager* renderPassManager);

	private:

		void UpdateRayTracing(VansVKCommandBuffer& computeCmd);

	public:

		// ── GPU 资源准备方法（场景加载时由 VansScene 调用） ───────────

		void PreparePBRMaterialData();

		void PrepareInstanceTransformData();

		void PrepareRayTracingData();

	private:

		//记录全局的渲染参数，需要和相机绑定
		GlobalStateData m_globalRenderStateData;

		void PrepareSkyRenderData();

		void PrepareSSAORenderData();

		void PrepareSSGIRenderData();

		void PrepareHZBRenderData();

		void PrepareScreenSpaceShadowRenderData();

		void PreparePunctualShadowDebugRenderData();

		void PrepareSSRRenderData();

		void PrepareVolumetricData();

		// 体积云 RT 纹理、Shader、描述符集初始化（场景加载时调用）
		void PrepareCloudRenderData();

		void PrepareTileLightData();

		void PrepareBilaterFilterData();

		void PrepareGlobalIllumiationData();

		// 后处理 Compute Pass RT 与 Shader 准备
		void PreparePostProcessRenderData();

	private:
		static constexpr uint32_t kMaxFrameContextsInFlight = 2;

		struct VansFrameContextRingSlot
		{
			uint32_t slotIndex = 0;
			uint64_t frameNumber = 0;
			uint32_t swapchainImageIndex = 0;
			VansVKCommandBuffer graphicsCommandBuffer;
			VkSemaphore imageAcquiredSemaphore = VK_NULL_HANDLE;
			bool gpuWorkPending = false;
			bool commandBufferRecording = false;
			bool frameSubmitSucceeded = true;
			VansDeferredDeleteQueue deferredDeletes;
		};

		bool IsFrameContextRingActive() const;
		bool EnsureFrameContextRingResources();
		bool RecreateFrameContextPresentSemaphores();
		void DestroyFrameContextRingResources();
		bool BeginFrameContextRingFrame();
		bool WaitForFrameContextRingSlot(VansFrameContextRingSlot& slot);
		bool WaitForFrameContextRingSlotGpuIdle(const VansFrameContextRingSlot& slot);
		void BindCurrentFrameContextToLegacyResources();
		void BindCurrentFrameContextToSlot(VansFrameContextRingSlot& slot);
		VansVKCommandBuffer& CurrentGraphicsCommandBuffer();
		const VansVKCommandBuffer& CurrentGraphicsCommandBuffer() const;
		VansDeferredDeleteQueue& CurrentDeferredDeleteQueue();

		//用于渲染GPU上进行同步
		uint64_t m_RenderFrameNumber = 0;

		uint32_t m_SwapChainImageIndex;

		VkSemaphore m_SwapChainImageAcquiredSemaphore;

		VkSemaphore m_CommandBufferReadyToPresentSemaphore;

		// Semaphores for shadow-parallel async path
		VkSemaphore m_ShadowDoneSemaphore        = VK_NULL_HANDLE;
		VkSemaphore m_GBufferDoneSemaphore       = VK_NULL_HANDLE;
		// BuildTileLightLists runs on compute queue (different family) in parallel with
		// Shadow + GBuffer. CB2 waits on this before starting the Deferred pass.
		VkSemaphore m_AsyncComputeDoneSemaphore  = VK_NULL_HANDLE;

		// Set to true to enable shadow-parallel async path.
		bool m_UseAsyncCompute = false;
		bool m_EnableParallelCommandRecording = true;
		bool m_EnableFrameContextRing = false;
		bool m_FrameContextRingResourcesReady = false;
		uint32_t m_ConfiguredFramesInFlight = 1;
		uint32_t m_CurrentFrameContextSlotIndex = 0;
		VansFrameContextRingSlot* m_ActiveFrameContextSlot = nullptr;
		std::array<VansFrameContextRingSlot, kMaxFrameContextsInFlight> m_FrameContextRingSlots;
		std::vector<VkFence> m_SwapchainImageInFlightFences;
		std::vector<VkSemaphore> m_SwapchainImageRenderFinishedSemaphores;
		VkFence m_LastSubmittedGraphicsFence = VK_NULL_HANDLE;
		bool m_LastSubmittedGraphicsFencePending = false;
		bool m_ShadowSecondaryCommandBuffersNeedReset = false;
		bool m_MotionVectorSecondaryCommandBuffersNeedReset = false;
		bool m_GBufferSecondaryCommandBuffersNeedReset = false;
		bool m_DecalSecondaryCommandBuffersNeedReset = false;
		uint32_t m_ParallelRecordThreadCount = 4;
		uint32_t m_MinDrawsPerSecondary = 32;

		// True when a second Graphics Queue was successfully acquired for shadow rendering.
		bool m_HasDedicatedShadowQueue = false;

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
		VansPipelineCacheService m_PipelineCacheService;

		//queues
		VkQueue m_VansVKGraphicsQueue;
		VkQueue m_VansVKComputeQueue;
		VkQueue m_VansVKShadowQueue = VK_NULL_HANDLE;

		//command buffer
		VansVKCommandBuffer m_VansVKCommandBuffer;

		VansVKCommandBuffer m_VansVKShadowCommandBuffer;

		// CB1 异步路径专用：录制 MotionVector + GBuffer，与 Shadow CB 并行提交，
		// 避免 CB1 结束后 CPU stall 等 fence 才能让 m_VansVKCommandBuffer 录制 CB2。
		VansVKCommandBuffer m_VansVKGBufferCommandBuffer;

		VansVKCommandBuffer m_VansVKRayTracingCommandBuffer;

		// Points to the command buffer scene draw calls should record into.
		// Defaults to m_VansVKCommandBuffer; switched temporarily to
		// m_VansVKShadowCommandBuffer during shadow CB recording in async path.
		VansVKCommandBuffer* m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
		VansFrameContext m_CurrentFrameContext;
		VansRenderFramePlan m_CurrentFramePlan;
		VansCompiledRenderGraph m_CurrentCompiledRenderGraph;
		VansRenderGraphBarrierPlan m_CurrentBarrierPlan;
		VansVulkanRenderGraphSyncPlan m_CurrentVulkanSyncPlan;
		VansRenderFeatureAuditResult m_CurrentFeatureAudit;
		VansRenderGraphDiagnosticsSnapshot m_CurrentRenderGraphDiagnostics;
		uint64_t m_CurrentRenderGraphTopologyHash = 0;
		uint64_t m_CurrentRenderGraphTopologyRevision = 0;
		bool m_HasCompiledRenderGraphTopology = false;
		mutable std::string m_CurrentRenderGraphDebugSummary;
		mutable uint64_t m_CurrentRenderGraphDebugSummaryRevision = 0;
		VansRayTracing rayTracingContext;
		
		VansVKCommandBuffer m_ImmediateGraphicsCommandBuffer;
		std::unique_ptr<VansVKSecondaryCommandContext> m_ShadowSecondaryCommandContext;
		std::unique_ptr<VansVKSecondaryCommandContext> m_MotionVectorSecondaryCommandContext;
		std::unique_ptr<VansVKSecondaryCommandContext> m_SecondaryCommandContext;
		std::unique_ptr<VansVKSecondaryCommandContext> m_DecalSecondaryCommandContext;

		INativeWindowProvider* m_NativeWindowProvider = nullptr;
		bool m_VulkanInitialized = false;
		
	private:
		std::vector<uint32_t> m_SharingQueueFamilyIndices;

		//recored all supported queue before device create
		uint32_t m_GraphicsQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		uint32_t m_ComputeQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		uint32_t m_PresentQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

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

		void BuildCurrentRenderFramePlan(VansRenderPassManager* renderPassManager);
		void BindCurrentFrameSyncResources();
		void FlushCurrentFrameDeferredDeletes();
		bool InitializeParallelCommandRecording();
		void DestroyParallelCommandRecording();
		bool ResetGBufferSecondaryCommandBuffersIfNeeded();
	public:
		VansVKDevice(VkExtent2D resolution, INativeWindowProvider* nativeWindowProvider = nullptr)
		{
			m_RenderWidth = resolution.width;
			m_RenderHeight = resolution.height;
			m_GraphicsAPI = GRAPHICS_API::VULKAN;
			m_NativeWindowProvider = nativeWindowProvider;
			m_VulkanInitialized = VulkanSetUp(resolution);
			
		}

		~VansVKDevice()
		{
			m_GraphicsAPI = GRAPHICS_API::INVALIDE;
			if (m_VulkanInitialized)
				VulkanDestroy();
		}

		bool IsInitialized() const { return m_VulkanInitialized; }

		// vkQueueWaitIdle wait for all command buffer in this queue
		bool WaitForQueue(VkQueue queue);

		bool WaitForDevice();

		// 渲染分辨率访问器（供 VansWaterSystem 等子系统查询）
		uint32_t GetRenderWidth()  const { return m_RenderWidth; }
		uint32_t GetRenderHeight() const { return m_RenderHeight; }

	};
}
