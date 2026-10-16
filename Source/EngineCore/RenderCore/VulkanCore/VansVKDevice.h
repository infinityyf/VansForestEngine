#pragma once
#include <unordered_set>
#include <atomic>
#include "../BRDFData/VansLightCookie.h"
#include "../VansGraphicsDevice.h"
#include "../VansCameraFrameData.h"
#include "../VansRenderRuntimeConfig.h"
#include "../VansDrawSubmission.h"
#include "../ShadowCore/VansPunctualShadowFrameState.h"
#include "vulkan/vulkan.h"
#include "VansVKSurface.h"
#include "VansVKBuffer.h"
#include "VansVKDrawInstanceArena.h"
#include "VansProjectRenderPassRuntime.h"
#include "VansRuntimeRenderTextureCatalog.h"
#include "VansMainCameraVisibilityState.h"
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "VansVKSecondaryCommandContext.h"
#include "VansRenderGraph.h"
#include "VansRenderGraphVulkanSync.h"
#include "VansFrameSubmitOrchestrator.h"
#include "VansPipelineCacheService.h"
#include "VansShader.h"
#include "../RayTracingCore/VansRayTracing.h"
#include <vector>

#include "../../ScriptCore/VansCommonUtils.h"
#include "../FidelityFXCore/VansFSR.h"
#include "../DLSSCore/VansDLSS.h"
#include "../UpscalingCore/VansUpscalerManager.h"
#include "../UpscalingCore/VansUpscaleResolutionPolicy.h"
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
	struct VansFrameResourceSelection
	{
		std::uint32_t frameSlot = 0;
		std::uint32_t gbufferTargetSlot = 0;
	};
	

	class VansVKDevice: public VansGraphicsDevice
	{
	private :
		VansCameraDataGPU m_CameraData{};
		VansCameraDataGPU m_CameraDataBeforePendingFrame{};
		bool m_CameraFramePendingSubmission = false;
		bool m_GIReceiverVisibilityFrameRecorded = false;
		bool m_AmbientSkyCacheFrameRecorded = false;
		bool m_NearMediaFrameRecorded = false;
		bool m_CloudFrameRecorded = false;
		bool m_AtmosphereFrameRecorded = false;
		glm::mat4 m_UnjitteredCameraProjection{ 1.0f };
		VansTemporalJitter m_CameraTemporalJitter{};
        VansGILightingContentChanges m_GILightingContentChanges;
        std::vector<VkDescriptorImageInfo> m_LightCookieDescriptors;
        std::vector<VkDescriptorSet> m_LightCookieDescriptorSets;
        std::unordered_set<std::string> m_InvalidLightCookies;
        bool UploadLightCookies(const VansLightCookieFrame& frame);

		VansVKDrawInstanceArena m_DrawInstanceArena;
		VansProjectRenderPassRuntime m_ProjectRenderPassRuntime;
		VansRuntimeRenderTextureCatalog m_RenderTextureCatalog;
		std::atomic<bool> m_RenderTextureCatalogRequested{false};
		std::uint32_t m_CameraRenderFrameIndex = 0;
		std::uint32_t m_CurrentCameraFrameIndex = 0;
		bool m_CameraFrameResourcesReady = false;
		bool m_LightFrameResourcesReady = false;
		// Backend-owned value copy. Never retain the producer packet or scene pointers
		// across the main-thread -> render-backend boundary.
		VansRenderViewSnapshot m_CurrentRenderView{};
		VansRenderSceneFrameSnapshot m_CurrentRenderSceneSnapshot{};
		VansCascadeShadowDrawPlan m_CurrentCascadeShadowDrawPlan;
		VansPunctualShadowDrawPlan m_CurrentPunctualShadowDrawPlan;
		VansRenderFrameTimingSnapshot m_CurrentRenderTiming{};
		VansRenderWorld m_RenderWorld;
		std::vector<std::uint64_t> m_CurrentTransformKeys;
		std::vector<std::uint32_t> m_CurrentTransformIndices;
		VansPunctualShadowFrameState m_PunctualShadowFrameState;
		VansMainCameraVisibilityState m_MainCameraVisibilityState;
		bool m_HasCurrentRenderView = false;

		bool InitializeCameraFrameResources();
		void DestroyCameraFrameResources();
		bool InitializeLightFrameResources();
		void DestroyLightFrameResources();
		bool UploadRenderLightFrameData(const VansRenderLightFrameData& frameData);

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
			VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VkPipelineStageFlags shaderReadStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

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
		VansDLSS m_DLSSController;
		VansUpscalerManager m_UpscalerManager;
		VansVKImage m_UpscalerOutputImage;
		VkExtent2D m_UpscalerOutputExtent{ 0, 0 };
		// Renderer-owned target resolution. Editor Scene panels only scale the
		// produced image and must never feed their panel dimensions back here.
		VkExtent2D m_RequestedUpscalerOutputExtent{ 0, 0 };
		bool m_UpscalerConfigDirty = false;
		bool m_FSREnabled = false;
		bool m_DLSSEnabled = false;
		bool m_PresentFinalDisplayToSwapchain = false;
		std::uint64_t m_RecordedTemporalFrameNumber = 0;

		bool BuildFSRFrameInput(VansFSRFrameInput& output);
		bool BuildDLSSDispatch(VansStreamlineDLSSDispatch& output);
		bool PrepareDLSSDispatchResources(VansVKCommandBuffer& commandBuffer);
		void RecordFSRMasks(VansVKCommandBuffer& commandBuffer, VansFSRFrameInput& input);
		void RestoreFSRDepthLayout(VansVKCommandBuffer& commandBuffer);
		bool RecordFSRFallbackUpscale(VansVKCommandBuffer& commandBuffer);
		void RecordDisplayPostProcess(VansVKCommandBuffer& commandBuffer);
		void RecordFinalDisplayToSwapchain();
		void ResolvePreparedFrameHistory(bool submitted, bool graphExecuted = false);

		bool InitializeFSR();
		bool InitializeDLSS();
		bool EnsureUpscalerOutputImage(const VkExtent2D& outputExtent);
		void CleanupUpscalerOutputImage();

		void CleanupFSR();
		void CleanupDLSS();
		void CleanupUpscalerResources();

		VkExtent2D CalculateUpscalerOutputExtent() const;
		VkExtent2D CalculateUpscalerRenderExtent() const;
		void RecreateSceneResolutionResources(const VkExtent2D& renderExtent);
		void ProcessPendingUpscalerConfig();
		void DeactivateFailedFSRContext();
		bool ApplyFrameContextRingStateAtIdle(uint32_t framesInFlight);
		void ApplyAsyncComputeStateAtIdle(bool enabled);

	public:

		void BeginUIRenderPass() override;

		void EndUIRenderPass() override;

		bool CanRecordCurrentFrame() const override { return CurrentFrameContext().frameSubmitSucceeded; }
		bool CurrentFrameRequiresSurfaceResize() const override { return CurrentFrameContext().surfaceResizeRequired; }
		bool HasDeviceLost() const override { return m_DeviceLost; }
		VansGpuMemorySnapshot QueryGpuMemorySnapshot() const override;

		/// 运行时 UI pass（Noesis → FSR 输出图像）
		void BeginSceneUIRenderPass();
		void EndSceneUIRenderPass();

		/// 返回 Scene UI pass 的 VkRenderPass 句柄，供 Noesis RenderDevice 懒编译 PSO 使用
		VkRenderPass GetSceneUIRenderPassHandle();

		/// Return the final display-processed image for editor sampling.
		VansVKImage& GetFinalDisplayImage();
		VansVKImage& GetFSRReactiveMaskImage() { return m_FSRController.GetReactiveMaskImage(); }
		VansVKImage& GetFSRTransparencyAndCompositionImage()
		{
			return m_FSRController.GetTransparencyAndCompositionImage();
		}

		bool GetTemporalUpscaleJitterOffset(
			uint32_t frameIndex,
			float& outPixelX,
			float& outPixelY) override;
		float GetTemporalUpscaleMipBias() const override;

		VansUpscalerSelectionChange RequestUpscalerConfig(
			const VansUpscalerConfig& config,
			uint32_t outputWidth,
			uint32_t outputHeight);
		VansUpscalerSelectionChange ApplyRenderRuntimeConfig(
			const VansRenderRuntimeConfig& config,
			uint32_t outputWidth,
			uint32_t outputHeight);
		// Project opening is a renderer safe point. Commit the requested backend
		// and resolution before any scene-owned resources are created at that size.
		void CommitRenderRuntimeConfigAtSafePoint()
		{
			ProcessPendingUpscalerConfig();
		}
		void RequestUpscalerHistoryReset(VansUpscalerResetReason reason)
		{
			m_UpscalerManager.GetHistory().RequestReset(reason);
		}
		void SetRuntimeSwapchainPresentationEnabled(bool enabled) { m_PresentFinalDisplayToSwapchain = enabled; }
		const VansUpscalerConfig& GetDesiredUpscalerConfig() const
		{
			return m_UpscalerManager.GetDesiredConfig();
		}
		const VansUpscalerConfig& GetEffectiveUpscalerConfig() const
		{
			return m_UpscalerManager.GetEffectiveConfig();
		}
		VansUpscalerFallbackReason GetUpscalerFallbackReason() const
		{
			return m_UpscalerManager.GetFallbackReason();
		}
		const std::string& GetUpscalerFallbackMessage() const
		{
			return m_UpscalerManager.GetFallbackMessage();
		}
		VansUpscalerRuntimeDiagnostics GetUpscalerDiagnostics() const;
		bool HasFSRContextDestructionFailure() const
		{
			return m_FSRController.HasContextDestructionFailure();
		}
		VansUpscalerCapabilities GetUpscalerCapabilities(VansUpscalerBackend backend) const;
		bool IsParallelCommandRecordingEnabled() const { return m_EnableParallelCommandRecording; }
		uint32_t GetConfiguredFramesInFlight() const { return m_ConfiguredFramesInFlight; }
		uint32_t GetObservedSubmittedFramePeak() const
		{
			return m_ObservedSubmittedFramePeak.load(std::memory_order_acquire);
		}
		uint32_t GetEffectiveFramesInFlight() const
		{
			return IsFrameContextRingActive() ? m_ConfiguredFramesInFlight : 1u;
		}
		bool IsAsyncComputeRequested() const { return m_AsyncComputeRequested; }
		bool IsAsyncComputeEnabled() const { return m_AsyncComputeEnabled; }
		const VansQueueCapabilities& GetQueueCapabilities() const { return m_QueueCapabilities; }
		const VansAtmosphereQualityConfig& GetAtmosphereQualityConfig() const { return m_AtmosphereQualityConfig; }
		const VansNearMediaQualityConfig& GetNearMediaQualityConfig() const { return m_NearMediaQualityConfig; }
		const VansCloudShadowQualityConfig& GetCloudShadowQualityConfig() const { return m_CloudShadowQualityConfig; }
		bool ApplyCommandRecordingSettings(
			bool parallelEnabled,
			uint32_t framesInFlight,
			bool asyncComputeEnabled);
		VkExtent2D GetUpscalerOutputExtent() const { return CalculateUpscalerOutputExtent(); }

		// 窗口大小改变时重建交换链和UI渲染pass
		bool OnWindowResize(uint32_t width, uint32_t height) override;

	public:

		//初始化被渲染的数据
		bool BeforeRendering() override;
		void PrepareRenderingFrame() override;
		VansRenderSubmissionPrepareResult PrepareRenderSubmission(
			VansRenderFrameSubmission& submission) override;

		void Rendering() override;

		void Present() override;
		//释放被渲染数据
		void AfterRendering() override;

		bool WaitForIdle() override { return WaitForDevice(); }
		VansVKBuffer& GetCameraDataBuffer(std::uint32_t slot) { return m_FrameContextRingSlots.at(slot).cameraDataBuffer; }
		VansVKBuffer& GetLightDataBuffer(std::uint32_t slot) { return m_FrameContextRingSlots.at(slot).lightDataBuffer; }
		VansVKBuffer& GetLightCookieDataBuffer(std::uint32_t slot) { return m_FrameContextRingSlots.at(slot).lightCookieDataBuffer; }
		VansVKDrawInstanceArena& GetDrawInstanceArena() { return m_DrawInstanceArena; }
		const VansVKDrawInstanceArena& GetDrawInstanceArena() const { return m_DrawInstanceArena; }
		VansProjectRenderPassRuntime& ProjectRenderPassRuntime() { return m_ProjectRenderPassRuntime; }
		const VansProjectRenderPassRuntime& ProjectRenderPassRuntime() const { return m_ProjectRenderPassRuntime; }
		bool ShouldDrawProjectPassProxy(VansRenderProxyHandle proxy) { return m_MainCameraVisibilityState.ShouldDraw(proxy); }
		std::vector<VansRuntimeRenderTextureInfo> QueryRuntimeRenderTextures()
		{ m_RenderTextureCatalogRequested.store(true,std::memory_order_release); return m_RenderTextureCatalog.CaptureSnapshot(); }
		void RefreshRuntimeRenderTextureCatalog();
		bool RegisterRuntimeRenderTextureSource(VansRuntimeRenderTextureCatalog::Source source, std::string& error)
		{ return m_RenderTextureCatalog.RegisterSource(std::move(source),error); }
		void UnregisterRuntimeRenderTextureSource(const std::string& key) { m_RenderTextureCatalog.UnregisterSource(key); }
		// 仅清空诊断目录；调用者已保证 GPU 空闲及渲染线程停止消费源资源。
		void ClearRuntimeRenderTextureCatalogAtIdle() { m_RenderTextureCatalog.Clear(); }
		VansVKImage* ResolveRuntimeRenderTexture(const std::string& key, std::uint64_t generation, bool previousFrame = false)
		{ return m_RenderTextureCatalog.Resolve(key,generation,GetFrameResourceSlot(),GetCurrentGBufferTargetSlot(),previousFrame); }
		std::uint64_t GetFrameRecordingSerial() const { return CurrentFrameContext().frameNumber; }
		VansResolvedRenderTexture ResolveSampledRenderTexture(const std::string& key)
		{ return m_RenderTextureCatalog.ResolveSampled(key,GetFrameResourceSlot(),GetCurrentGBufferTargetSlot()); }
		bool ShouldDrawMainCameraProxy(VansRenderProxyHandle proxy)
		{
			return m_MainCameraVisibilityState.ShouldDraw(proxy);
		}
		bool IsCurrentRenderProxyEnabled(VansRenderProxyHandle proxy) const
		{
			const VansRenderProxyStaticData* state = m_RenderWorld.Resolve(proxy);
			return state != nullptr && state->enabled;
		}
		const VansRenderTransformFrameData* FindCurrentRenderTransform(
			VansRenderProxyHandle proxy) const;
		const VansRenderPunctualShadowCasterInput* FindCurrentShadowCaster(VansRenderProxyHandle proxy) const
		{
			return m_PunctualShadowFrameState.FindCaster(proxy);
		}
		const VansRenderViewSnapshot& GetCurrentRenderViewSnapshot() const
		{
			return m_CurrentRenderView;
		}
		const VansRenderSceneFrameSnapshot& GetCurrentRenderSceneSnapshot() const
		{
			return m_CurrentRenderSceneSnapshot;
		}
		const VansRenderFrameTimingSnapshot& GetCurrentRenderTimingSnapshot() const
		{
			return m_CurrentRenderTiming;
		}
		VansMainCameraVisibilityDebugSnapshot CaptureMainCameraVisibilityDebugSnapshot() const
		{
			return m_MainCameraVisibilityState.GetDebugSnapshot();
		}
		const VansCameraDataGPU& GetCameraData() const { return m_CameraData; }
		VansTemporalCameraSnapshot CaptureTemporalCameraSnapshot() const;

		const VansRenderGraphDiagnosticsSnapshot& GetCurrentRenderGraphDiagnostics() const { return m_CurrentRenderGraphDiagnostics; }
		const std::string& GetCurrentRenderGraphDebugSummary() const;
		const VansFrameContext& GetCurrentFrameContext() const { return CurrentFrameContext(); }
		std::uint32_t GetCurrentGBufferTargetSlot() const { return CurrentFrameContext().gbufferTargetSlot; }
		VansFrameResourceSelection GetCurrentFrameResourceSelection() const
		{ return { GetFrameResourceSlot(), GetCurrentGBufferTargetSlot() }; }
		void RecordOverlaySampledImage(VkImage image, VkImageLayout layout,
			VkPipelineStageFlags stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VkAccessFlags access = VK_ACCESS_SHADER_READ_BIT, bool write = false);
        // 与本帧实际提交槽一致；单帧配置使用槽 0。
        std::uint32_t GetFrameResourceSlot() const
        { return m_ActiveFrameContextSlot ? m_ActiveFrameContextSlot->slotIndex : 0u; }
		void RequestPunctualShadowDebugPreview()
		{
			m_PunctualShadowFrameState.RequestDebugPreview();
		}
		VansPunctualShadowDebugSnapshot CapturePunctualShadowDebugSnapshot() const
		{
			return m_PunctualShadowFrameState.CaptureDebugSnapshot();
		}
		std::uint32_t GetPunctualShadowTotalAtlasPages() const
		{
			return m_PunctualShadowFrameState.GetTotalAtlasPages();
		}
		void EnqueueDeferredDelete(std::function<void()> destroy);
		void RetireAfterCurrentFrame(std::function<void()> destroy) override;
		bool DrainDeferredDeletesAfterDeviceIdle();

		void InitializeGpuProfiler() override;

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
		const VkPhysicalDeviceFeatures& GetDeviceFeatures() const { return m_DeviceFeatures2.features; }

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
		void PrepareResolutionDependentRenderingData();

		// IES profile 纹理数组：在场景加载完成后调用，创建 GPU 资源并上传所有已解析的 IES profile
		void PrepareIESProfileData();

		
		void DrawShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd);

		void DrawSkyMotionVectorPass(VansVKCommandBuffer& commandBuffer);

		void DrawPunctualShadowMap(uint32_t atlasIndex);
		bool RecordShadowMapParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex);

		void DrawSceneGBuffer(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		bool RecordSceneGBufferParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex = 0);
		bool RecordDecalPassParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex);

		// 拆分后的渲染 pass：
		//   DrawSceneRawOpaqueLighting — Deferred 只写入未经大气的 RawOpaqueSceneColor
		//   DrawSceneTransparentPost — ForwardOpaquePreAtmosphere + Transparent + Particles + PostProcess（读 SceneColor，写 PostProcess 输出）
		// 大气合成随后写 SceneColor，透明、水面和头发只消费合成后的颜色。
		void DrawSceneRawOpaqueLighting(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void DrawSceneTransparentPost(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void DrawHairDepthResolve(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void DrawHairLighting(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
        void UpdateHairDebugPreview(VansRenderPassManager* passes, VansVKCommandBuffer& commandBuffer);
        void RequestHairDebugPreview() { m_HairDebugRequested.store(true, std::memory_order_release); }
        VansVKImage* GetHairDebugPreview() {
            return m_HairDebugReady.load(std::memory_order_acquire) ? &m_HairDebugImage : nullptr;
        }
		void DrawHairComposite(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void ClearHairVisibilityResources(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void PrepareHairVisibilityForLighting(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void BuildSceneColorPyramid(
			VansVKImage& source,
			VansVKImage& target,
			VansVKCommandBuffer& commandBuffer,
			VkPipelineStageFlags consumerStages);
		void CopyOpaqueSceneColorForTransmission(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void PrepareWaterBackgroundPyramid(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void ResolveDepthOfFieldIntoSceneColor(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		void PrepareSceneColorForTransparentPass(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
		VkDescriptorSetLayout GetHairPassLayout() const { return m_HairLightingPassLayout; }
		VkDescriptorSet GetHairPassDescriptorSet() const
		{
			const std::uint32_t slot = GetFrameResourceSlot();
			return slot < m_HairLightingPassSets.size() ? m_HairLightingPassSets[slot] : VK_NULL_HANDLE;
		}
		VkDescriptorSetLayout GetTransmissionGlassPassLayout() const { return m_TransmissionGlassPassLayout; }
		VkDescriptorSet GetTransmissionGlassPassDescriptorSet(std::uint32_t slot) const
		{
			return slot < m_TransmissionGlassPassSets.size()
				? m_TransmissionGlassPassSets[slot] : VK_NULL_HANDLE;
		}

		VkDeviceAddress GetAccelerationAddress(VkAccelerationStructureDeviceAddressInfoKHR* addressInfo);

		VkDeviceAddress GetBufferAddress(VkBufferDeviceAddressInfo* bufferInfo);

		void GetAccelerationStructureBuildSizes(VkAccelerationStructureBuildGeometryInfoKHR* buildInfo, uint32_t* maxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* buildSizeInfo);

		void CreateAccelerationStructure(VkAccelerationStructureCreateInfoKHR* createInfo, VkAccelerationStructureKHR* as);
		void DestroyAccelerationStructure(VkAccelerationStructureKHR as);

		static PFN_vkGetDeviceProcAddr GetDeviceProcAddr();
		static double GetTimestampPeriodMs(VkPhysicalDevice physicalDevice);
		static uint32_t GetQueueFamilyTimestampValidBits(
			VkPhysicalDevice physicalDevice,
			uint32_t queueFamilyIndex);
		static bool CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo& createInfo, VkQueryPool& pool);
		static void DestroyQueryPool(VkDevice device, VkQueryPool& pool);
		static void ResetQueryPool(VkDevice device, VkQueryPool pool, uint32_t firstQuery, uint32_t queryCount);
		static void CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool pool, uint32_t query);
		static void CmdBeginDebugLabel(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT& labelInfo);
		static void CmdEndDebugLabel(VkCommandBuffer commandBuffer);
		static VkResult GetQueryPoolResults(VkDevice device, VkQueryPool pool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* data, VkDeviceSize stride, VkQueryResultFlags flags);

	public:
		VansRayTracing& GetRayTracingContext() { return rayTracingContext; }
        VansGIFrameWork GetGIFrameWork() const { return m_GIFrameWorkPublication->Read(); }
        VansGIMemorySnapshot GetGIMemorySnapshot() const { return m_GIMemoryPublication->Read(); }
        std::shared_ptr<VansGIMemoryPublication> GetGIMemoryPublication() const { return m_GIMemoryPublication; }
		uint32_t GetAmbientSkyCacheDebugMode() const { return m_AmbientSkyCacheDebugMode; }
		void SetAmbientSkyCacheDebugMode(uint32_t mode) { m_AmbientSkyCacheDebugMode = std::min(mode, 3u); }

		/// 场景卸载时调用：重置所有渲染 Feature 的 descriptor set 一次性写入标记，
		/// 使下次场景加载后重新绑定运行时纹理，避免引用已销毁的 VkImageView。
		void ResetFeatureDescriptorSets()
		{
			++m_FeatureDescriptorGeneration;
		}

		void UpdateGIData(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);
		void UpdateAmbientSkyCache(VansVKCommandBuffer& computeCmd);

		void UpdateHZB(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateMainCameraHiZCull(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateSSR(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateScreenSpaceShadow(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdatePunctualShadowDebugPreview(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void UpdateAtmosphereStaticLuts(VansVKCommandBuffer& commandBuffer);
		void UpdateCloudShadow(VansVKCommandBuffer& commandBuffer);
		void UpdateAtmosphereViewLuts(VansVKCommandBuffer& commandBuffer);
		void UpdateLocalMedia(VansVKCommandBuffer& commandBuffer);
		void CompositeAtmosphere(VansVKCommandBuffer& commandBuffer);
		void UpdateVolumetricCloud(VansVKCommandBuffer& commandBuffer);

		// TileLight Build pass: culls lights per tile each frame
		void BuildTileLightLists(VansVKCommandBuffer& cmd);

		// 后处理 Compute Pass：自动曝光与 Bloom
		void UpdateExposure(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);
		void UpdateDepthOfField(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);
		void UpdateBloom(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);
		// 上传逐帧曝光参数，并在配置变化时更新 Bloom / DOF 参数。
		void UploadPostProcessProfileIfDirty();
		void ProcessPendingGISettings();
		// 调用方保证位于渲染安全点；结构修改须先等待在途 GPU 工作完成。
		bool ApplyGISettingsAtSafePoint(const VansGISettings& settings,
            const std::shared_ptr<const VansGIVoxelMaterialInputs>& materials, bool forceRebuild = false);

	private:

		uint64_t m_FeatureDescriptorGeneration = 1;
		std::array<uint64_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_GIDataDescSetGeneration{};
		std::array<uint32_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_GIDataGBufferTargetSlot{};
		uint64_t m_GTAODescSetGeneration = 0;
		uint64_t m_HZBDescSetGeneration = 0;
		uint64_t m_HIZSeedDescSetGeneration = 0;
		uint64_t m_OcclusionHZBDescSetGeneration = 0;
		uint64_t m_OcclusionHIZSeedDescSetGeneration = 0;
		uint64_t m_MainCameraHiZCullDescSetGeneration = 0;
		std::array<uint64_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_SSRDescSetGeneration{};
		uint64_t m_TileLightBuildDescSetGeneration = 0;
		std::array<uint64_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_PPExposureDescSetGeneration{};
		std::array<uint64_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_PPDepthOfFieldDescSetGeneration{};
		std::array<VkImageView, VansCommandRecordingConfig::MaximumFramesInFlight> m_DepthOfFieldGBufferViews{};
		uint64_t m_PPBloomDescSetGeneration = 0;
		uint64_t m_PPBloomShapeDescSetGeneration = 0;
		uint64_t m_ScreenSpaceShadowDescSetGeneration = 0;

		VkDescriptorSetLayout m_HairCompositePassLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_HairCompositePassSets;
		bool m_HairCompositeDescriptorsReady = false;
		VkDescriptorSetLayout m_HairLightingPassLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_HairLightingPassSets;
		bool m_HairLightingDescriptorsReady = false;
		std::array<VkImageView, VansCommandRecordingConfig::MaximumFramesInFlight> m_HairOpaqueDepthViews{};
		std::array<std::uint64_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_HairOpaqueDepthGenerations{};
        std::atomic<bool> m_HairDebugRequested{false};
        std::atomic<bool> m_HairDebugReady{false};
        VansVKImage m_HairDebugImage;
        VkDescriptorSetLayout m_HairDebugLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_HairDebugSets;
		VkDescriptorSetLayout m_TransmissionGlassPassLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_TransmissionGlassPassSets;
		bool m_TransmissionGlassDescriptorsReady = false;
		std::array<VkImageView, VansCommandRecordingConfig::MaximumFramesInFlight> m_TransmissionOpaqueDepthViews{};
		std::array<std::uint64_t, VansCommandRecordingConfig::MaximumFramesInFlight> m_TransmissionOpaqueDepthGenerations{};

		void SetupHairLightingDescriptors(VansRenderPassManager* renderPassManager);
		void UpdateHairFrameTargetDescriptors(VansRenderPassManager* renderPassManager);
		void DestroyHairLightingDescriptors();
		void SetupHairCompositeDescriptors(VansRenderPassManager* renderPassManager);
		void DestroyHairCompositeDescriptors();
		void SetupTransmissionGlassDescriptors(VansRenderPassManager* renderPassManager);
		void UpdateTransmissionGlassDescriptors(VansRenderPassManager* renderPassManager);
		bool BindFrameOpaqueDepthReaders(VansRenderPassManager* renderPassManager,
			std::uint32_t frameSlot, std::uint32_t gbufferTargetSlot);
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
		void UpdateSSGIProbeCache(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);
        void UpdateGIReceiverVisibility(VansVKCommandBuffer& command, const glm::vec2& jitterDelta);


		void TemporalFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);

		void AtrousFilterSSGI(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& computeCmd);


	private:

		void UpdateGIDataDescriptorSets(VansRenderPassManager* renderPassManager);
		void UploadSSGIParams(const VansGISettings& settings);

		void UpdateHIZSeedDescriptorSet(VansRenderPassManager* renderPassManager);

		void UpdateHZBDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateOcclusionHIZSeedDescriptorSet(VansRenderPassManager* renderPassManager);

		void UpdateOcclusionHZBDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateMainCameraHiZCullDescriptorSets(VansRenderPassManager* renderPassManager);

		bool UpdateSSRDescriptorSets(VansRenderPassManager* renderPassManager);

		void UpdateScreenSpaceShadowSets(VansRenderPassManager* renderPassManager);

		void UpdateTileLightBuildSets();

		// 后处理 Compute Pass descriptor set 写入（一次性）
		void UpdateExposureDescriptorSets(VansRenderPassManager* renderPassManager);
		bool UpdateDepthOfFieldDescriptorSets(VansRenderPassManager* renderPassManager);
		void UpdateBloomDescriptorSets(VansRenderPassManager* renderPassManager);
		void UpdateBloomShapeDescriptorSets();

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

		void PrepareGTAORenderData();
        void UpdateGTAODescriptorSets(VansRenderPassManager* renderPassManager);
        void UpdateGTAODepth(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
        void UpdateGTAOMain(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);
        void DenoiseGTAO(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer);

		void PrepareSSGIRenderData();

		void PrepareHZBRenderData();

		void PrepareScreenSpaceShadowRenderData();

		void PreparePunctualShadowDebugRenderData();

		void PrepareSSRRenderData();

		void PrepareTileLightData();



		// 后处理 Compute Pass RT 与 Shader 准备
		void PreparePostProcessRenderData();
		void PrepareAmbientSkyCacheRenderData();

	private:
		static constexpr uint32_t kMaxFrameContextsInFlight = VansCommandRecordingConfig::MaximumFramesInFlight;

		struct VansAsyncFrameCommands
		{
			VansVKCommandBuffer shadowMaps;
			VansVKCommandBuffer gbuffer;
			VansVKCommandBuffer gbufferMaterial;
			VansVKCommandBuffer gtaoRaw;
			VansVKCommandBuffer graphicsScreen;
			VansVKCommandBuffer vegetation;
			VansVKCommandBuffer earlyAux;
			VansVKCommandBuffer asyncAtmosphere;
			VansVKCommandBuffer asyncHZB;
			VansVKCommandBuffer rayTracing;
			VansVKCommandBuffer giData;
			std::array<VansVKCommandBuffer*, 11> All()
			{
				return { &shadowMaps, &gbuffer, &gbufferMaterial, &gtaoRaw,
					&graphicsScreen, &vegetation, &earlyAux, &asyncAtmosphere,
					&asyncHZB, &rayTracing, &giData };
			}
		};

		struct VansFrameContextRingSlot
		{
			uint32_t slotIndex = 0;
			VansFrameContext frameContext;
			VansVKBuffer cameraDataBuffer;
			VansVKBuffer lightDataBuffer;
			VansVKBuffer lightCookieDataBuffer;
			VansVKCommandBuffer graphicsCommandBuffer;
			VansVKCommandBuffer projectPreludeCommandBuffer;
			VansFrameSubmitOrchestrator projectPreludeSubmit;
			VansQueueTimelinePoint projectPreludeCompletion;
			VansAsyncFrameCommands asyncCommands;
			std::unique_ptr<VansVKSecondaryCommandContext> shadowSecondaryCommands;
			std::unique_ptr<VansVKSecondaryCommandContext> gbufferSecondaryCommands;
			std::unique_ptr<VansVKSecondaryCommandContext> decalSecondaryCommands;
			bool shadowSecondaryNeedsReset = false;
			bool gbufferSecondaryNeedsReset = false;
			bool decalSecondaryNeedsReset = false;
			VkSemaphore imageAcquiredSemaphore = VK_NULL_HANDLE;
			bool gpuWorkPending = false;
			std::uint64_t profilerFrameIndex = UINT64_MAX;
            VansGIConsumerFrameWork giConsumerWork;
			bool submittedWithAsyncCompute = false;
			VansFrameSubmitOrchestrator submitGraph;
			VansDeferredDeleteQueue deferredDeletes;
		};

		bool IsFrameContextRingActive() const;
		bool EnsureFrameContextRingResources();
		bool RecreateSwapchainPresentSemaphores();
		bool RetireSubmittedFrame();
		VansFrameSubmitOrchestrator& CurrentFrameSubmitGraph();
		VansFrameContextRingSlot& CurrentFrameSlot();
		const VansFrameContextRingSlot& CurrentFrameSlot() const;
		VansFrameContext& CurrentFrameContext();
		const VansFrameContext& CurrentFrameContext() const;
		VansAsyncFrameCommands& CurrentAsyncFrameCommands();
		bool CreateAsyncFrameCommands(VansAsyncFrameCommands& commands);
		void DestroyAsyncFrameCommands(VansAsyncFrameCommands& commands);
		bool ResetSubmittedAsyncFrameCommands(VansAsyncFrameCommands& commands);
		void DestroyFrameContextRingResources();
		bool BeginFrameContextRingFrame();
		bool SubmitProjectRenderPrelude();
		bool WaitForFrameContextRingSlot(VansFrameContextRingSlot& slot);
        void RetireGIConsumerWork(VansFrameContextRingSlot& slot);
		void RecordSubmittedFrameDepth();
		void BindFrameSyncResources(VansFrameContextRingSlot& slot);
		VansVKCommandBuffer& CurrentGraphicsCommandBuffer();
		const VansVKCommandBuffer& CurrentGraphicsCommandBuffer() const;
		VansDeferredDeleteQueue& CurrentDeferredDeleteQueue();

		//用于渲染GPU上进行同步
		uint64_t m_RenderFrameNumber = 0;

		uint32_t m_SwapChainImageIndex;
		enum class SwapchainAcquireState { None, AwaitingGpuWait, GpuWaitSubmitted, RebuildRequired };
		SwapchainAcquireState m_SwapchainAcquireState = SwapchainAcquireState::None;
		uint32_t m_AcquiredFrameContextSlotIndex = 0;

		bool m_AsyncComputeRequested = false;
		VansAtmosphereQualityConfig m_AtmosphereQualityConfig;
		VansNearMediaQualityConfig m_NearMediaQualityConfig;
		VansCloudShadowQualityConfig m_CloudShadowQualityConfig;
		bool m_AsyncComputeEnabled = false;
		VansQueueCapabilities m_QueueCapabilities;
		bool m_EnableParallelCommandRecording = true;
		bool m_FrameContextRingResourcesReady = false;
		bool m_DeviceLost = false;
		uint32_t m_ConfiguredFramesInFlight = 1;
		uint32_t m_CurrentFrameContextSlotIndex = 0;
		VansFrameContextRingSlot* m_ActiveFrameContextSlot = nullptr;
		std::array<VansFrameContextRingSlot, kMaxFrameContextsInFlight> m_FrameContextRingSlots;
		struct OverlaySampledImage
		{
			VkImage image = VK_NULL_HANDLE;
			VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkPipelineStageFlags stages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			VkAccessFlags access = VK_ACCESS_SHADER_READ_BIT;
			bool write = false;
		};
		std::vector<OverlaySampledImage> m_OverlaySampledImages;
		VansQueueTimelines m_FrameQueueTimelines;
		VansCrossFrameResourceLedger m_CrossFrameResourceLedger;
		std::atomic<uint32_t> m_ObservedSubmittedFramePeak{ 0 };
		std::vector<VkFence> m_SwapchainImageInFlightFences;
		std::vector<VkSemaphore> m_SwapchainImageRenderFinishedSemaphores;
		uint32_t m_ParallelRecordThreadCount = 4;
		uint32_t m_MinDrawsPerSecondary = 32;

		VkPhysicalDeviceProperties m_DeviceProperties;
		bool m_CalibratedTimestampsEnabled = false;
		
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

		// Internal synchronous setup/upload work; frame rendering uses the active slot.
		VansVKCommandBuffer m_MaintenanceGraphicsCommandBuffer;

		// Points to the command buffer scene draw calls should record into.
		// Maintenance work outside a frame uses its own command buffer. Rendering
		// binds the active slot and may switch to a pass-specific async buffer.
		VansVKCommandBuffer* m_pActiveCommandBuffer = &m_MaintenanceGraphicsCommandBuffer;
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
        std::shared_ptr<VansGIMemoryPublication> m_GIMemoryPublication = std::make_shared<VansGIMemoryPublication>();
        std::shared_ptr<VansGIFrameWorkPublication> m_GIFrameWorkPublication = std::make_shared<VansGIFrameWorkPublication>();
        VansGIConsumerFrameWork m_RecordedGIConsumerWork;
        VansRayTracing rayTracingContext{m_GIMemoryPublication, m_GIFrameWorkPublication};
		uint32_t m_AmbientSkyCacheDebugMode = 0u;
		
		VansVKCommandBuffer m_ImmediateGraphicsCommandBuffer;

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
		bool InitializeParallelCommandRecording();
		void DestroyParallelCommandRecording();
		bool ResetGBufferSecondaryCommandBuffersIfNeeded(VansFrameContextRingSlot& slot);
		void ResetAsyncFrameCommandBuffersAfterFailure();
		void RefreshAsyncComputeState();
		void NotifyPunctualShadowJobsSubmitted();
	public:
		VansVKDevice(VkExtent2D resolution, INativeWindowProvider* nativeWindowProvider = nullptr)
		{
			m_RenderWidth = resolution.width;
			m_RenderHeight = resolution.height;
			m_GraphicsAPI = GRAPHICS_API::VULKAN;
			m_NativeWindowProvider = nativeWindowProvider;
			m_VulkanInitialized = VulkanSetUp(resolution);
			
		}

		~VansVKDevice() override
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
