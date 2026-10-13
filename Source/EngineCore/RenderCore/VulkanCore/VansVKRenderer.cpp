#include "VansVKDevice.h"
#include "../AtmosphereCore/VansAtmosphereSystem.h"
#include "../AtmosphereCore/VansNearMediaSystem.h"
#include "../CloudCore/VansVolumetricCloudSystem.h"
#include "VansRenderPass.h"
#include "VansRenderPassCatalog.h"
#include "VansRenderGraphVulkanSync.h"
#include "VansVKDescriptorManager.h"
#include "VansDescriptorSetLayouts.h"
#include "VansVKMemoryManager.h"
#include "VansVKSecondaryCommandContext.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansShaderManager.h"
#include "../VegetationCore/VansVegetationCollection.h"
#include "../VegetationCore/VansVegetationSystem.h"
#include "../WaterCore/VansWaterSystem.h"
#include "../WeatherCore/VansRainRenderSystem.h"
#include "../VansRenderBootstrapSettings.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansJobSystem.h"
#include "../../Util/VansProfiler.h"
#include "../../RuntimeCore/VansFramePhase.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RuntimeUI/Public/VansUISystem.h"
#include <algorithm>
#include <thread>
#include <utility>
#include <stdexcept>

namespace VansGraphics
{
	extern PFN_vkWaitForFences vkWaitForFences;
	extern PFN_vkResetFences vkResetFences;

	namespace
	{
		class VansRenderWorkerContractScope final
		{
		public:
			VansRenderWorkerContractScope()
			{
#ifdef _DEBUG
				m_PreviousRole = g_CurrentThreadRole;
				m_PreviousPhase = g_CurrentFramePhase;
#endif
				VANS_INIT_RENDER_WORKER_THREAD();
				VANS_SET_FRAME_PHASE(VansFramePhase::ParallelGPURecord);
			}

			~VansRenderWorkerContractScope()
			{
#ifdef _DEBUG
				g_CurrentFramePhase = m_PreviousPhase;
				g_CurrentThreadRole = m_PreviousRole;
#endif
			}

		private:
#ifdef _DEBUG
			VansThreadRole m_PreviousRole = VansThreadRole::Unknown;
			VansFramePhase m_PreviousPhase = VansFramePhase::GameLogic;
#endif
		};

		void AppendSSGIFrameResources(VansFrameSubmitNode& node,
			VansMaterialManager* materials, VkPipelineStageFlags stages)
		{
			if (materials == nullptr)
				return;
			constexpr std::pair<const char*, const char*> images[] = {
				{"SSGIResult", VansMaterialManager::RT_SSGI_RESULT},
				{"SSGIProbeCacheRadiance", VansMaterialManager::RT_SSGI_PROBE_CACHE_RADIANCE},
				{"SSGIProbeCacheSurface", VansMaterialManager::RT_SSGI_PROBE_CACHE_SURFACE},
				{"SSGITemporalA", VansMaterialManager::RT_SSGI_TEMPORAL_A},
				{"SSGITemporalB", VansMaterialManager::RT_SSGI_TEMPORAL_B},
				{"SSGIMomentsA", VansMaterialManager::RT_SSGI_MOMENTS_A},
				{"SSGIMomentsB", VansMaterialManager::RT_SSGI_MOMENTS_B},
				{"SSGISurfaceHistoryA", VansMaterialManager::RT_SSGI_SURFACE_HISTORY_A},
				{"SSGISurfaceHistoryB", VansMaterialManager::RT_SSGI_SURFACE_HISTORY_B},
				{"SSGIAtrousA", VansMaterialManager::RT_SSGI_ATROUS_A},
				{"SSGIFilterResult", VansMaterialManager::RT_SSGI_FILTER_RESULT}
			};
			for (const auto& [name, runtimeName] : images)
			{
				auto* texture = materials->GetRuntimeRenderTexture(runtimeName);
				if (texture == nullptr)
					continue;
				node.resources.push_back({name, stages,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
					VK_IMAGE_LAYOUT_GENERAL, true, true, true, false,
					VansPhysicalResourceIdentity::ForImage(texture->GetImage().GetImage())});
			}
		}

		bool HasPunctualShadowJobsForAtlas(
			const VansRenderSceneFrameSnapshot& snapshot,
			uint32_t atlasIndex)
		{
			return std::any_of(
				snapshot.punctualShadowJobs.begin(),
				snapshot.punctualShadowJobs.end(),
				[atlasIndex](const VansPunctualShadowRenderJob& job)
				{
					return job.atlasIndex == atlasIndex;
				});
		}

		bool IsIsolatedDeferredDebugOutput(
			const VansRenderSceneFrameSnapshot& snapshot)
		{
			return snapshot.reflectionProbeIsolatedDebugOutput || (snapshot.gi.prepared &&
				snapshot.gi.settings.probeOnlyDeferredOutput);
		}

		constexpr uint64_t kRenderGraphFnvOffsetBasis = 14695981039346656037ull;
		constexpr uint64_t kRenderGraphFnvPrime = 1099511628211ull;

		void HashRenderGraphByte(uint64_t& hash, uint8_t value)
		{
			hash ^= static_cast<uint64_t>(value);
			hash *= kRenderGraphFnvPrime;
		}

		void HashRenderGraphUInt64(uint64_t& hash, uint64_t value)
		{
			for (uint32_t byteIndex = 0; byteIndex < 8; ++byteIndex)
			{
				HashRenderGraphByte(hash, static_cast<uint8_t>((value >> (byteIndex * 8)) & 0xffu));
			}
		}

		void HashRenderGraphString(uint64_t& hash, const std::string& value)
		{
			HashRenderGraphUInt64(hash, static_cast<uint64_t>(value.size()));
			for (const char character : value)
			{
				HashRenderGraphByte(hash, static_cast<uint8_t>(character));
			}
		}

		void HashRenderGraphAccess(uint64_t& hash, const VansRenderResourceAccess& access)
		{
			HashRenderGraphString(hash, access.name);
			HashRenderGraphUInt64(hash, access.resourceId);
			HashRenderGraphUInt64(hash, static_cast<uint64_t>(access.usage));
		}

		uint64_t BuildRenderFramePlanTopologyHash(const VansRenderFramePlan& framePlan)
		{
			uint64_t hash = kRenderGraphFnvOffsetBasis;
			const auto& passes = framePlan.GetPasses();
			HashRenderGraphUInt64(hash, static_cast<uint64_t>(passes.size()));

			for (const auto& pass : passes)
			{
				HashRenderGraphString(hash, pass.name);
				HashRenderGraphUInt64(hash, pass.passId);
				HashRenderGraphUInt64(hash, static_cast<uint64_t>(pass.queue));
				HashRenderGraphUInt64(hash, pass.resizeDependent ? 1ull : 0ull);
				HashRenderGraphUInt64(hash, pass.allowAsyncCompute ? 1ull : 0ull);
				HashRenderGraphUInt64(hash, pass.enabled ? 1ull : 0ull);

				HashRenderGraphUInt64(hash, static_cast<uint64_t>(pass.reads.size()));
				for (const auto& read : pass.reads)
				{
					HashRenderGraphAccess(hash, read);
				}

				HashRenderGraphUInt64(hash, static_cast<uint64_t>(pass.writes.size()));
				for (const auto& write : pass.writes)
				{
					HashRenderGraphAccess(hash, write);
				}

				HashRenderGraphUInt64(hash, static_cast<uint64_t>(pass.preservedFeatures.size()));
				for (const auto& feature : pass.preservedFeatures)
				{
					HashRenderGraphString(hash, feature);
				}
			}

			return hash;
		}

		void RebindCompiledRenderGraphPassDescs(
			VansCompiledRenderGraph& graph,
			const VansRenderFramePlan& framePlan)
		{
			const auto& passes = framePlan.GetPasses();
			for (auto& pass : graph.passes)
			{
				pass.desc = pass.index < passes.size() ? &passes[pass.index] : nullptr;
			}
		}

		void RefreshRenderGraphFrameNumbers(
			uint64_t frameNumber,
			VansCompiledRenderGraph& graph,
			VansRenderGraphBarrierPlan& barrierPlan,
			VansVulkanRenderGraphSyncPlan& syncPlan)
		{
			graph.frameNumber = frameNumber;
			barrierPlan.frameNumber = frameNumber;
			syncPlan.frameNumber = frameNumber;
		}

		bool EnsureKnownFramePassName(const char* passName)
		{
			if (VansRenderPassCatalog::IsKnownPassName(passName))
			{
				return true;
			}

			static std::vector<std::string> loggedUnknownPassNames;
			const std::string safePassName = passName != nullptr ? passName : "<null>";
			if (std::find(loggedUnknownPassNames.begin(), loggedUnknownPassNames.end(), safePassName) == loggedUnknownPassNames.end())
			{
				loggedUnknownPassNames.emplace_back(safePassName);
				VANS_LOG_ERROR("FramePlan execution references unknown render pass id: " << safePassName);
			}
			return false;
		}

		bool IsFramePassEnabled(const VansRenderFramePlan& framePlan, const char* passName)
		{
			if (!EnsureKnownFramePassName(passName))
			{
				return false;
			}
			return framePlan.FindPass(passName) != nullptr;
		}

		template <typename DrawFunc>
		void RecordFrameGraphicsPass(
			const VansRenderFramePlan& framePlan,
			const char* passName,
			const char* gpuScopeName,
			VansRenderPassManager* renderPassManager,
			VansVKRenderPass& renderPass,
			VansVKCommandBuffer& commandBuffer,
			GlobalStateData& globalStateData,
			DrawFunc&& drawFunc,
			int framebufferIndex = 0)
		{
			if (!IsFramePassEnabled(framePlan, passName))
			{
				return;
			}

			VkCommandBuffer cmd = commandBuffer.GetVKCommandBuffer();
			VANS_GPU_SCOPE(cmd, gpuScopeName);
			renderPassManager->BeginRenderPass(renderPass, commandBuffer, globalStateData, framebufferIndex);
			drawFunc();
			renderPassManager->EndRenderPass(commandBuffer, globalStateData);
		}

		template <typename DrawFunc>
		void RecordFrameGraphicsPassNoGpuScope(
			const VansRenderFramePlan& framePlan,
			const char* passName,
			VansRenderPassManager* renderPassManager,
			VansVKRenderPass& renderPass,
			VansVKCommandBuffer& commandBuffer,
			GlobalStateData& globalStateData,
			DrawFunc&& drawFunc,
			int framebufferIndex = 0)
		{
			if (!IsFramePassEnabled(framePlan, passName))
			{
				return;
			}

			renderPassManager->BeginRenderPass(renderPass, commandBuffer, globalStateData, framebufferIndex);
			drawFunc();
			renderPassManager->EndRenderPass(commandBuffer, globalStateData);
		}

		template <typename RecordFunc>
		void RecordFrameStep(
			const VansRenderFramePlan& framePlan,
			const char* passName,
			RecordFunc&& recordFunc)
		{
			if (!IsFramePassEnabled(framePlan, passName))
			{
				return;
			}

			recordFunc();
		}

		template <typename RecordFunc>
		void RecordFrameGpuStep(
			const VansRenderFramePlan& framePlan,
			const char* passName,
			const char* gpuScopeName,
			VkCommandBuffer cmd,
			RecordFunc&& recordFunc)
		{
			if (!IsFramePassEnabled(framePlan, passName))
			{
				return;
			}

			VANS_GPU_SCOPE(cmd, gpuScopeName);
			recordFunc();
		}
	}

	void VansVKDevice::BeginUIRenderPass()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->BeginRenderPass(renderPassManager->m_VansUIPass, CurrentGraphicsCommandBuffer(), m_globalRenderStateData, m_SwapChainImageIndex);
	}

	void VansVKDevice::EndUIRenderPass()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->EndRenderPass(CurrentGraphicsCommandBuffer(), m_globalRenderStateData);
	}

	bool VansVKDevice::InitializeParallelCommandRecording()
	{
		if (!m_EnableParallelCommandRecording)
			return false;
		VansFrameContextRingSlot& slot = CurrentFrameSlot();
		if (slot.gbufferSecondaryCommands && slot.gbufferSecondaryCommands->IsReady()
			&& slot.shadowSecondaryCommands && slot.shadowSecondaryCommands->IsReady()
			&& slot.decalSecondaryCommands && slot.decalSecondaryCommands->IsReady())
			return true;
		if (m_GraphicsQueueFamilyIndex == VK_QUEUE_FAMILY_IGNORED)
			return false;

		const uint32_t hardwareThreads = (std::max)(1u, std::thread::hardware_concurrency());
		m_ParallelRecordThreadCount = (std::min)(m_ParallelRecordThreadCount, (std::max)(1u, hardwareThreads - 1u));
		const uint32_t secondaryCount = m_ParallelRecordThreadCount + 1u;
		auto createContext = [&](std::unique_ptr<VansVKSecondaryCommandContext>& context, const char* name)
		{
			context = std::make_unique<VansVKSecondaryCommandContext>();
			if (!context->Create(*this, m_GraphicsQueueFamilyIndex, secondaryCount))
			{
				context.reset();
				VANS_LOG_ERROR("[VansVKDevice] Parallel command recording initialization failed for " << name << "; pass will use inline recording.");
				return false;
			}
			return true;
		};

		if ((!slot.shadowSecondaryCommands || !slot.shadowSecondaryCommands->IsReady())
			&& !createContext(slot.shadowSecondaryCommands, "Shadow"))
			return false;
		if ((!slot.gbufferSecondaryCommands || !slot.gbufferSecondaryCommands->IsReady())
			&& !createContext(slot.gbufferSecondaryCommands, "GBuffer"))
			return false;
		if ((!slot.decalSecondaryCommands || !slot.decalSecondaryCommands->IsReady())
			&& !createContext(slot.decalSecondaryCommands, "Decal"))
			return false;

		VANS_LOG("[VansVKDevice] Parallel command recording initialized. secondariesPerPass=" << secondaryCount);
		return true;
	}

	void VansVKDevice::DestroyParallelCommandRecording()
	{
		for (VansFrameContextRingSlot& slot : m_FrameContextRingSlots)
		{
			for (auto* context : { &slot.gbufferSecondaryCommands,
				&slot.shadowSecondaryCommands, &slot.decalSecondaryCommands })
			{
				if (*context)
				{
					(*context)->Destroy(m_VansVKLogicDevice);
					context->reset();
				}
			}
			slot.shadowSecondaryNeedsReset = false;
			slot.gbufferSecondaryNeedsReset = false;
			slot.decalSecondaryNeedsReset = false;
		}
	}

	bool VansVKDevice::ResetGBufferSecondaryCommandBuffersIfNeeded(VansFrameContextRingSlot& slot)
	{
		bool result = true;
		auto resetIfNeeded = [&](std::unique_ptr<VansVKSecondaryCommandContext>& context, bool& needReset)
		{
			if (!needReset)
				return;
			if (context)
				result = context->ResetAll(false) && result;
			needReset = false;
		};
		resetIfNeeded(slot.shadowSecondaryCommands, slot.shadowSecondaryNeedsReset);
		resetIfNeeded(slot.gbufferSecondaryCommands, slot.gbufferSecondaryNeedsReset);
		resetIfNeeded(slot.decalSecondaryCommands, slot.decalSecondaryNeedsReset);
		return result;
	}

	void VansVKDevice::ResetAsyncFrameCommandBuffersAfterFailure()
	{
        rayTracingContext.DiscardGIProbeUpdate();
		CurrentGraphicsCommandBuffer().ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().shadowMaps.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().gbuffer.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().gbufferMaterial.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().gtaoRaw.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().graphicsScreen.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().vegetation.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().earlyAux.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().asyncAtmosphere.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().asyncHZB.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().rayTracing.ResetCommandBuffer(false);
		CurrentAsyncFrameCommands().giData.ResetCommandBuffer(false);
		ResetGBufferSecondaryCommandBuffersIfNeeded(CurrentFrameSlot());
		CurrentFrameContext().gtaoRawRecorded = false;
		CurrentFrameContext().graphicsScreenRecorded = false;
		CurrentFrameContext().shadowMapsRecorded = false;
		CurrentFrameContext().gbufferRecorded = false;
		CurrentFrameContext().gbufferMaterialRecorded = false;
		CurrentFrameContext().vegetationRecorded = false;
		CurrentFrameContext().earlyAuxRecorded = false;
		CurrentFrameContext().asyncAtmosphereRecorded = false;
		CurrentFrameContext().atmosphereStaticLutsWritten = false;
		CurrentFrameContext().punctualShadowDebugPreviewWritten = false;
		CurrentFrameContext().hairDebugPreviewWritten = false;
		CurrentFrameContext().fsrReactiveMaskWritten = false;
		CurrentFrameContext().fsrTransparencyMaskWritten = false;
		CurrentFrameContext().asyncHZBRecorded = false;
		CurrentFrameContext().rayTracingRecorded = false;
		CurrentFrameContext().giDataRecorded = false;
		m_pActiveCommandBuffer = &m_MaintenanceGraphicsCommandBuffer;
	}

	VkRenderPass VansVKDevice::GetSceneUIRenderPassHandle()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		return renderPassManager->m_VansSceneUIPass.GetRenderPass();
	}

	void VansVKDevice::BeginSceneUIRenderPass()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		// Scene UI pass 只有一个 framebuffer，固定使用索引 0。
		renderPassManager->BeginRenderPass(renderPassManager->m_VansSceneUIPass, CurrentGraphicsCommandBuffer(), m_globalRenderStateData, 0);
	}

	void VansVKDevice::EndSceneUIRenderPass()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->EndRenderPass(CurrentGraphicsCommandBuffer(), m_globalRenderStateData);
	}

	bool VansVKDevice::OnWindowResize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return false;

		if (m_SwapchainAcquireState == SwapchainAcquireState::AwaitingGpuWait)
		{
			// A failed frame graph can skip the node that waits on image acquire.
			// Consume that binary semaphore before the slot can be reused.
			const VkSemaphore acquireSemaphore =
				m_FrameContextRingSlots[m_AcquiredFrameContextSlotIndex].imageAcquiredSemaphore;
			if (!VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice,
				{}, { { acquireSemaphore, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0 } },
				{}, VK_NULL_HANDLE, false))
			{
				VANS_LOG_ERROR("OnWindowResize: failed to consume outstanding image-acquire semaphore.");
				return false;
			}
			m_SwapchainAcquireState = SwapchainAcquireState::GpuWaitSubmitted;
		}
		if (!WaitForDevice())
		{
			VANS_LOG_ERROR("OnWindowResize: waiting for the device failed.");
			return false;
		}
		// A failed rebuild must be retried before any later image acquire, even
		// when the resize began without an outstanding acquire semaphore.
		m_SwapchainAcquireState = SwapchainAcquireState::RebuildRequired;

		if (!m_VansVKSurface.RecreateSwapChain(m_VansVKPhysicalDevice, m_VansVKLogicDevice))
		{
			VANS_LOG_ERROR("OnWindowResize: swap chain recreation failed.");
			return false;
		}

		VkExtent2D newDisplayExtent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
		if (!RecreateSwapchainPresentSemaphores())
		{
			VANS_LOG_ERROR("OnWindowResize: present semaphore recreation failed.");
			return false;
		}
		m_SwapchainImageInFlightFences.assign(m_VansVKSurface.m_VansVKImageCount, VK_NULL_HANDLE);

		auto renderPassManager = VansRenderPassManager::GetInstance();
		if (!renderPassManager->RecreateUIRenderPass(
			m_MaintenanceGraphicsCommandBuffer, m_VansVKGraphicsQueue,
			m_VansVKSurface, { newDisplayExtent.width, newDisplayExtent.height }
		))
		{
			VANS_LOG_ERROR("OnWindowResize: UI render pass recreation failed.");
			return false;
		}
		m_SwapchainAcquireState = SwapchainAcquireState::None;

		// The presentation target establishes the renderer-owned output extent.
		// Embedded editor preview panels only scale this image for display.
		if (m_RequestedUpscalerOutputExtent.width == 0)
		{
			m_RequestedUpscalerOutputExtent = newDisplayExtent;
			m_UpscalerConfigDirty = true;
		}

		VANS_LOG("OnWindowResize: display=" << newDisplayExtent.width << "x" << newDisplayExtent.height
			<< "  render=" << m_RenderWidth << "x" << m_RenderHeight);
		return true;
	}

	bool VansVKDevice::BeforeRendering()
	{
		m_DeviceLost = false;
		if (!EnsureFrameContextRingResources() ||
			!RecreateSwapchainPresentSemaphores())
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to initialize renderer semaphores.");
			return false;
		}
		const VkExtent2D initialRenderExtent = CalculateUpscalerRenderExtent();
		m_RenderWidth = initialRenderExtent.width;
		m_RenderHeight = initialRenderExtent.height;
		if (!InitializeCameraFrameResources())
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to initialize renderer-owned camera frame resources.");
			return false;
		}
		if (!InitializeLightFrameResources())
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to initialize renderer-owned light frame resources.");
			return false;
		}
		if (!m_DrawInstanceArena.Initialize(m_VansVKLogicDevice))
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to initialize renderer-owned draw-instance resources.");
			return false;
		}

		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->SetupVansDeferredRenderPass(m_VansVKLogicDevice, m_MaintenanceGraphicsCommandBuffer, m_VansVKGraphicsQueue,
			{ m_RenderWidth, m_RenderHeight }, m_ConfiguredFramesInFlight);
		renderPassManager->SetupVansShadowRenderPass(m_VansVKLogicDevice, m_MaintenanceGraphicsCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansPunctualShadowRenderPass(m_VansVKLogicDevice, m_MaintenanceGraphicsCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansSkyMotionVectorRenderPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansHairVisibilityPass(m_VansVKLogicDevice, m_VansVKPhysicalDevice,
			{ m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansHairDepthResolvePass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansHairLightingPass(m_VansVKLogicDevice,
			{ m_RenderWidth, m_RenderHeight }, m_ConfiguredFramesInFlight);
		SetupHairCompositeDescriptors(renderPassManager);
		SetupTransmissionGlassDescriptors(renderPassManager);
		// 贴花只读主深度与接收材质，输出独立修饰附件。
		renderPassManager->SetupVansDecalRenderPass(m_VansVKLogicDevice, m_MaintenanceGraphicsCommandBuffer, m_VansVKGraphicsQueue, { m_RenderWidth, m_RenderHeight }, m_ConfiguredFramesInFlight);
		// 水面 GBuffer Pass：必须在 SetupVansDeferredRenderPass 之后调用，依赖已创建的深度图像。
		renderPassManager->SetupVansWaterGBufferPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		// 注：水面 descriptor sets 在场景加载时由 VansSceneEnvironmentNodeBuilder::BuildWaterNode 调用 SetupDescriptors 完成。
		renderPassManager->SetupVansUIRenderPass(m_VansVKLogicDevice, m_MaintenanceGraphicsCommandBuffer, m_VansVKGraphicsQueue, m_VansVKSurface,
			{
				m_VansVKSurface.m_VansVKSwapChainImageExtent.width,
				m_VansVKSurface.m_VansVKSwapChainImageExtent.height
			}
		);

		PrepareRenderingData();
		SetupTransmissionGlassDescriptors(renderPassManager);

		// Scene loading is deferred: done via LoadSceneForRendering() from the
		// editor after the user selects a project and opens a scene file.
		const VkExtent2D fsrDisplayExtent = CalculateUpscalerOutputExtent();
		if (!EnsureUpscalerOutputImage(fsrDisplayExtent))
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to initialize the upscaler output image.");
			return false;
		}
		bool initialUpscalerRebuildRequired = false;
		if (m_UpscalerManager.GetEffectiveConfig().backend == VansUpscalerBackend::DLSS)
		{
			if (!InitializeDLSS())
			{
				m_UpscalerManager.ActivateRuntimeFallback(
					VansUpscalerBackend::FSR,
					m_UpscalerManager.GetEffectiveConfig().quality,
					VansUpscalerFallbackReason::ContextCreationFailed,
					"DLSS context creation failed during renderer initialization; using FSR");
				m_UpscalerConfigDirty = true;
				initialUpscalerRebuildRequired = true;
			}
		}
		else if (m_UpscalerManager.GetEffectiveConfig().backend == VansUpscalerBackend::FSR)
		{
			if (!InitializeFSR())
			{
				m_UpscalerManager.ActivateRuntimeFallback(
					VansUpscalerBackend::Off,
					VansUpscaleQualityMode::NativeAA,
					VansUpscalerFallbackReason::ContextCreationFailed,
					"FSR context creation failed during renderer initialization; using native output");
				m_UpscalerConfigDirty = true;
				initialUpscalerRebuildRequired = true;
			}
		}
		if (!initialUpscalerRebuildRequired)
		{
			m_UpscalerConfigDirty = false;
			const VansUpscalerConfig& active = m_UpscalerManager.GetEffectiveConfig();
			VANS_LOG("[Upscaler] desired=" << ToString(m_UpscalerManager.GetDesiredConfig().backend)
				<< " effective=" << ToString(active.backend)
				<< " quality=" << ToString(active.quality)
				<< " output=" << fsrDisplayExtent.width << "x" << fsrDisplayExtent.height
				<< " render=" << m_RenderWidth << "x" << m_RenderHeight
				<< " mipBias=" << GetTemporalUpscaleMipBias());
		}
		renderPassManager->SetupVansDisplayPostProcessPass(
			m_VansVKLogicDevice,
			m_UpscalerOutputImage,
			fsrDisplayExtent);

		// Scene UI composites over the display-processed image.
		renderPassManager->SetupVansSceneUIRenderPass(
			m_VansVKLogicDevice,
			m_VansVKPhysicalDevice,
			renderPassManager->GetFinalDisplayColor().GetImageView(),
			fsrDisplayExtent);

		// RuntimeUI frontend initialization is performed by the application on
		// Main after this RT setup has published readiness. This backend only
		// records and tears down renderer-side Noesis work.
		return true;
	}

	void VansVKDevice::BuildCurrentRenderFramePlan(VansRenderPassManager* renderPassManager)
	{
		VANS_PROFILE_SCOPE("Render::BuildRenderFramePlan", Vans::ProfileCategory::RenderPrepare);
		(void)renderPassManager;

		CurrentFrameContext().swapchainImageIndex = m_SwapChainImageIndex;
		BindFrameSyncResources(CurrentFrameSlot());
		CurrentFrameContext().frameSubmitSucceeded = true;

		VansRenderPassCatalog::BuildCompatibilityFramePlan(
			m_CurrentFramePlan,
			m_CurrentRenderSceneSnapshot.features,
			CurrentFrameContext().frameNumber,
			m_AsyncComputeEnabled);

		const uint64_t topologyHash = BuildRenderFramePlanTopologyHash(m_CurrentFramePlan);
		const bool topologyChanged =
			!m_HasCompiledRenderGraphTopology ||
			topologyHash != m_CurrentRenderGraphTopologyHash;

		if (topologyChanged)
		{
			m_CurrentRenderGraphTopologyHash = topologyHash;
			++m_CurrentRenderGraphTopologyRevision;
			m_HasCompiledRenderGraphTopology = true;

			m_CurrentCompiledRenderGraph = VansRenderGraphCompiler::CompileFramePlan(m_CurrentFramePlan);
			std::vector<std::string> asyncCatalogErrors;
			if (!VansRenderPassCatalog::AuditAsyncMigrationContracts(asyncCatalogErrors))
			{
				for (const std::string& error : asyncCatalogErrors)
					m_CurrentCompiledRenderGraph.errors.emplace_back("Async catalog audit: " + error);
			}
			m_CurrentBarrierPlan = VansRenderGraphBarrierPlanner::BuildBarrierPlan(m_CurrentCompiledRenderGraph);
			m_CurrentVulkanSyncPlan = VansRenderGraphVulkanSyncMapper::BuildSyncPlan(m_CurrentBarrierPlan);

			std::vector<std::string> requiredFeatures;
			std::vector<std::string> conditionallyDisabledFeatures;
			VansRenderPassCatalog::GetPreservedFeatureAuditList(
				m_CurrentRenderSceneSnapshot.features,
				requiredFeatures,
				conditionallyDisabledFeatures);
			m_CurrentFeatureAudit = VansRenderFeatureAuditor::AuditFramePlan(
				m_CurrentFramePlan,
				requiredFeatures,
				conditionallyDisabledFeatures);
		}
		else
		{
			RebindCompiledRenderGraphPassDescs(m_CurrentCompiledRenderGraph, m_CurrentFramePlan);
			RefreshRenderGraphFrameNumbers(
				CurrentFrameContext().frameNumber,
				m_CurrentCompiledRenderGraph,
				m_CurrentBarrierPlan,
				m_CurrentVulkanSyncPlan);
		}

		m_CurrentRenderGraphDiagnostics.available = m_HasCompiledRenderGraphTopology;
		m_CurrentRenderGraphDiagnostics.compiledGraphValid = m_CurrentCompiledRenderGraph.IsValid();
		m_CurrentRenderGraphDiagnostics.featureAuditPassed = m_CurrentFeatureAudit.Passed();
		m_CurrentRenderGraphDiagnostics.framePlanPassCount =
			static_cast<uint32_t>(m_CurrentFramePlan.GetPassCount());
		m_CurrentRenderGraphDiagnostics.compiledResourceCount =
			static_cast<uint32_t>(m_CurrentCompiledRenderGraph.resources.size());
		m_CurrentRenderGraphDiagnostics.barrierDependencyCount =
			static_cast<uint32_t>(m_CurrentBarrierPlan.dependencies.size());
		m_CurrentRenderGraphDiagnostics.topologyRevision = m_CurrentRenderGraphTopologyRevision;
		m_CurrentRenderGraphDiagnostics.topologyHash = m_CurrentRenderGraphTopologyHash;
		m_CurrentRenderGraphDiagnostics.compiledFrameNumber = CurrentFrameContext().frameNumber;
		m_CurrentRenderGraphDebugSummary.clear();
		m_CurrentRenderGraphDebugSummaryRevision = 0;

		if (!m_CurrentCompiledRenderGraph.IsValid() || !m_CurrentFeatureAudit.Passed())
		{
			static bool loggedRenderGraphCompatibilityError = false;
			if (!loggedRenderGraphCompatibilityError)
			{
				loggedRenderGraphCompatibilityError = true;
				for (const auto& error : m_CurrentCompiledRenderGraph.errors)
				{
					VANS_LOG_ERROR("RenderGraph compatibility compile error: " << error);
				}
				for (const auto& missingFeature : m_CurrentFeatureAudit.missingFeatures)
				{
					VANS_LOG_ERROR("RenderGraph compatibility audit missing preserved feature: " << missingFeature);
				}
				VANS_LOG_ERROR("RenderGraph compatibility debug summary:\n" << GetCurrentRenderGraphDebugSummary());
			}
		}
	}

	const std::string& VansVKDevice::GetCurrentRenderGraphDebugSummary() const
	{
		if (m_CurrentRenderGraphDebugSummaryRevision == m_CurrentRenderGraphTopologyRevision &&
			!m_CurrentRenderGraphDebugSummary.empty())
		{
			return m_CurrentRenderGraphDebugSummary;
		}

		m_CurrentRenderGraphDebugSummary =
			VansRenderGraphDebugDumper::BuildFramePlanSummary(m_CurrentFramePlan)
			+ VansRenderGraphDebugDumper::BuildCompiledGraphSummary(m_CurrentCompiledRenderGraph)
			+ VansRenderGraphDebugDumper::BuildBarrierPlanSummary(m_CurrentBarrierPlan)
			+ VansRenderGraphVulkanSyncDebugDumper::BuildSyncPlanSummary(m_CurrentVulkanSyncPlan)
			+ VansRenderGraphDebugDumper::BuildFeatureAuditSummary(m_CurrentFeatureAudit);
		m_CurrentRenderGraphDebugSummaryRevision = m_CurrentRenderGraphTopologyRevision;
		return m_CurrentRenderGraphDebugSummary;
	}

	void VansVKDevice::EnqueueDeferredDelete(std::function<void()> destroy)
	{
		VansDeferredDeleteQueue& deleteQueue = CurrentDeferredDeleteQueue();
		deleteQueue.Enqueue(std::move(destroy));
		CurrentFrameContext().pendingDeferredDeleteCount =
			static_cast<uint64_t>(deleteQueue.Size());
	}

	void VansVKDevice::RetireAfterCurrentFrame(std::function<void()> destroy)
	{
		VANS_ASSERT_RENDER_THREAD();
		EnqueueDeferredDelete(std::move(destroy));
	}

	bool VansVKDevice::DrainDeferredDeletesAfterDeviceIdle()
	{
		if (!WaitForDevice() && !m_DeviceLost)
			return false;

		// Releasing one retired owner can enqueue another retirement. Drain to a
		// fixed point while the device is idle, including inactive ring slots.
		constexpr uint32_t kMaxDrainPasses = 64;
		uint64_t flushedCount = 0;
		for (uint32_t pass = 0; pass < kMaxDrainPasses; ++pass)
		{
			bool hadPendingDeletes = false;
			auto flushQueue = [&hadPendingDeletes, &flushedCount](VansDeferredDeleteQueue& queue)
			{
				const uint64_t count = static_cast<uint64_t>(queue.Size());
				if (count == 0)
					return;
				hadPendingDeletes = true;
				flushedCount += count;
				queue.Flush();
			};

			for (VansFrameContextRingSlot& slot : m_FrameContextRingSlots)
				flushQueue(slot.deferredDeletes);

			if (!hadPendingDeletes)
			{
				CurrentFrameContext().lastDeferredDeleteFlushCount = flushedCount;
				CurrentFrameContext().pendingDeferredDeleteCount = 0;
				return true;
			}
		}

		CurrentFrameContext().pendingDeferredDeleteCount = 0;
		for (const VansFrameContextRingSlot& slot : m_FrameContextRingSlots)
			CurrentFrameContext().pendingDeferredDeleteCount +=
				static_cast<uint64_t>(slot.deferredDeletes.Size());
		VANS_LOG_ERROR("[VansVKDevice] Deferred delete drain did not converge after "
			<< kMaxDrainPasses << " passes");
		return false;
	}

	bool VansVKDevice::IsFrameContextRingActive() const
	{
		return m_FrameContextRingResourcesReady;
	}

	VansFrameSubmitOrchestrator& VansVKDevice::CurrentFrameSubmitGraph()
	{
		return CurrentFrameSlot().submitGraph;
	}

	VansVKDevice::VansFrameContextRingSlot& VansVKDevice::CurrentFrameSlot()
	{
		return IsFrameContextRingActive() && m_ActiveFrameContextSlot != nullptr
			? *m_ActiveFrameContextSlot : m_FrameContextRingSlots[0];
	}

	const VansVKDevice::VansFrameContextRingSlot& VansVKDevice::CurrentFrameSlot() const
	{
		return IsFrameContextRingActive() && m_ActiveFrameContextSlot != nullptr
			? *m_ActiveFrameContextSlot : m_FrameContextRingSlots[0];
	}

	VansFrameContext& VansVKDevice::CurrentFrameContext()
	{
		return CurrentFrameSlot().frameContext;
	}

	const VansFrameContext& VansVKDevice::CurrentFrameContext() const
	{
		return CurrentFrameSlot().frameContext;
	}

	VansVKDevice::VansAsyncFrameCommands& VansVKDevice::CurrentAsyncFrameCommands()
	{
		return CurrentFrameSlot().asyncCommands;
	}

	VansVKCommandBuffer& VansVKDevice::CurrentGraphicsCommandBuffer()
	{
		return CurrentFrameSlot().graphicsCommandBuffer;
	}

	const VansVKCommandBuffer& VansVKDevice::CurrentGraphicsCommandBuffer() const
	{
		return CurrentFrameSlot().graphicsCommandBuffer;
	}

	VansDeferredDeleteQueue& VansVKDevice::CurrentDeferredDeleteQueue()
	{
		return CurrentFrameSlot().deferredDeletes;
	}

	void VansVKDevice::RecordOverlaySampledImage(VkImage image, VkImageLayout layout, VkPipelineStageFlags stages, VkAccessFlags access, bool write)
	{
		VANS_ASSERT_RENDER_THREAD();
		if (image == VK_NULL_HANDLE)
			return;
		const auto found = std::find_if(m_OverlaySampledImages.begin(), m_OverlaySampledImages.end(),
			[image](const OverlaySampledImage& resource) { return resource.image == image; });
		if (found == m_OverlaySampledImages.end())
			m_OverlaySampledImages.push_back({ image, layout, stages, access, write });
		else { found->stages |= stages; found->access |= access; found->write |= write; }
	}

	void VansVKDevice::BindFrameSyncResources(VansFrameContextRingSlot& slot)
	{
		m_ActiveFrameContextSlot = &slot;
		VansFrameContext& frame = slot.frameContext;
		const uint32_t imageIndex = frame.swapchainImageIndex;
		frame.imageAcquiredSemaphore = slot.imageAcquiredSemaphore;
		frame.renderFinishedSemaphore =
			imageIndex < m_SwapchainImageRenderFinishedSemaphores.size()
			? m_SwapchainImageRenderFinishedSemaphores[imageIndex] : VK_NULL_HANDLE;
		frame.graphicsFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		frame.gtaoRawFence = slot.asyncCommands.gtaoRaw.m_CommandBufferFinishSubmitFence;
		frame.graphicsScreenFence = slot.asyncCommands.graphicsScreen.m_CommandBufferFinishSubmitFence;
		frame.shadowMapsFence = slot.asyncCommands.shadowMaps.m_CommandBufferFinishSubmitFence;
		frame.gbufferFence = slot.asyncCommands.gbuffer.m_CommandBufferFinishSubmitFence;
		frame.gbufferMaterialFence = slot.asyncCommands.gbufferMaterial.m_CommandBufferFinishSubmitFence;
		frame.vegetationFence = slot.asyncCommands.vegetation.m_CommandBufferFinishSubmitFence;
		frame.earlyAuxFence = slot.asyncCommands.earlyAux.m_CommandBufferFinishSubmitFence;
		frame.asyncAtmosphereFence = slot.asyncCommands.asyncAtmosphere.m_CommandBufferFinishSubmitFence;
		frame.asyncHZBFence = slot.asyncCommands.asyncHZB.m_CommandBufferFinishSubmitFence;
		frame.rayTracingFence = slot.asyncCommands.rayTracing.m_CommandBufferFinishSubmitFence;
		frame.giDataFence = slot.asyncCommands.giData.m_CommandBufferFinishSubmitFence;
		frame.gtaoRawRecorded = false;
		frame.graphicsScreenRecorded = false;
		frame.shadowMapsRecorded = false;
		frame.gbufferRecorded = false;
		frame.gbufferMaterialRecorded = false;
		frame.vegetationRecorded = false;
		frame.earlyAuxRecorded = false;
		frame.asyncAtmosphereRecorded = false;
		frame.atmosphereStaticLutsWritten = false;
		frame.punctualShadowDebugPreviewWritten = false;
		frame.hairDebugPreviewWritten = false;
		frame.fsrReactiveMaskWritten = false;
		frame.fsrTransparencyMaskWritten = false;
		frame.asyncHZBRecorded = false;
		frame.rayTracingRecorded = false;
		frame.giDataRecorded = false;
		frame.pendingDeferredDeleteCount = static_cast<uint64_t>(slot.deferredDeletes.Size());
	}

	bool VansVKDevice::EnsureFrameContextRingResources()
	{
		if (m_FrameContextRingResourcesReady)
			return true;

		CommandBufferCreateParams params =
		{
			VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
			VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			1
		};

		for (uint32_t i = 0; i < m_ConfiguredFramesInFlight; ++i)
		{
			VansFrameContextRingSlot& slot = m_FrameContextRingSlots[i];
			slot.slotIndex = i;
			if (i != 0 && !slot.graphicsCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params))
			{
				VANS_LOG_ERROR("[VansVKDevice] Failed to create frame-context-ring graphics command buffer. slot=" << i);
				DestroyFrameContextRingResources();
				return false;
			}
			if ((i != 0 && !CreateVKFence(false, slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence))
				|| !CreateVKSemaphore(slot.imageAcquiredSemaphore))
			{
				VANS_LOG_ERROR("[VansVKDevice] Failed to create frame-context-ring sync resources. slot=" << i);
				DestroyFrameContextRingResources();
				return false;
			}
			if (i != 0 && !CreateAsyncFrameCommands(slot.asyncCommands))
			{
				VANS_LOG_ERROR("[VansVKDevice] Failed to create frame-context-ring async command buffers. slot=" << i);
				DestroyFrameContextRingResources();
				return false;
			}
			slot.gpuWorkPending = false;
			slot.frameContext.frameSubmitSucceeded = true;
			slot.submittedWithAsyncCompute = false;
		}

		m_CurrentFrameContextSlotIndex = 0;
		m_ActiveFrameContextSlot = nullptr;
		m_SwapchainImageInFlightFences.assign(
			static_cast<size_t>(m_VansVKSurface.m_VansVKImageCount),
			VK_NULL_HANDLE);
		m_FrameContextRingResourcesReady = true;
		VANS_LOG("[VansVKDevice] Frame-context ring resources initialized. framesInFlight=" << m_ConfiguredFramesInFlight);
		return true;
	}

	bool VansVKDevice::RecreateSwapchainPresentSemaphores()
	{
		std::vector<VkSemaphore> replacements(
			static_cast<size_t>(m_VansVKSurface.m_VansVKImageCount),
			VK_NULL_HANDLE);
		for (VkSemaphore& semaphore : replacements)
		{
			if (!CreateVKSemaphore(semaphore))
			{
				for (VkSemaphore& createdSemaphore : replacements)
					DestroyVKSemaphore(createdSemaphore);
				return false;
			}
		}
		m_SwapchainImageRenderFinishedSemaphores.swap(replacements);
		for (VkSemaphore& semaphore : replacements)
			DestroyVKSemaphore(semaphore);
		return true;
	}

	void VansVKDevice::DestroyFrameContextRingResources()
	{
		if (m_VansVKLogicDevice == VK_NULL_HANDLE)
			return;
		DestroyParallelCommandRecording();

		for (VansFrameContextRingSlot& slot : m_FrameContextRingSlots)
		{
            // 此销毁事务已经等待设备空闲，先退役反馈，再销毁它对应的 fence。
            if (slot.gpuWorkPending)
                rayTracingContext.CompleteGIProbeUpdate(slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence);
			slot.deferredDeletes.Flush();
			slot.projectPreludeSubmit.Shutdown();
			DestroyVKFence(slot.projectPreludeCommandBuffer.m_CommandBufferFinishSubmitFence);
			slot.projectPreludeCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
			slot.projectPreludeCompletion = {};
			DestroyVKSemaphore(slot.imageAcquiredSemaphore);
			if (slot.slotIndex != 0)
			{
				DestroyVKFence(slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence);
				slot.graphicsCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
				DestroyAsyncFrameCommands(slot.asyncCommands);
			}
			slot.imageAcquiredSemaphore = VK_NULL_HANDLE;
			slot.gpuWorkPending = false;
			slot.frameContext.frameSubmitSucceeded = true;
			slot.submittedWithAsyncCompute = false;
		}

		m_SwapchainImageInFlightFences.clear();
		m_ActiveFrameContextSlot = nullptr;
		m_FrameContextRingResourcesReady = false;
	}

	bool VansVKDevice::WaitForFrameContextRingSlot(VansFrameContextRingSlot& slot)
	{
		if (slot.projectPreludeSubmit.HasPendingWork())
		{
			if (!slot.projectPreludeSubmit.WaitForCompletion()) return false;
			const auto fence = slot.projectPreludeCommandBuffer.m_CommandBufferFinishSubmitFence;
			if (vkResetFences(m_VansVKLogicDevice, 1, &fence) != VK_SUCCESS) return false;
		}
		const bool hasPendingGraph = slot.submitGraph.HasPendingWork();
		if (slot.gpuWorkPending && !hasPendingGraph)
		{
			slot.frameContext.frameSubmitSucceeded = false;
			VANS_LOG_ERROR("[VansVKDevice] Submitted frame slot has no pending submit graph.");
			return false;
		}
		if (hasPendingGraph && !slot.submitGraph.WaitForCompletion())
		{
			m_DeviceLost = m_DeviceLost || slot.submitGraph.WasDeviceLost();
			slot.frameContext.frameSubmitSucceeded = false;
			return false;
		}
		if (!slot.gpuWorkPending)
		{
			if (!ResetGBufferSecondaryCommandBuffersIfNeeded(slot))
				return false;
			const uint64_t flushCount = static_cast<uint64_t>(slot.deferredDeletes.Size());
			slot.deferredDeletes.Flush();
			if (flushCount > 0)
				slot.frameContext.lastDeferredDeleteFlushCount = flushCount;
			slot.frameContext.pendingDeferredDeleteCount =
				static_cast<uint64_t>(slot.deferredDeletes.Size());
			return true;
		}

		const VkFence fence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		// The submit graph already waited for this final fence. Publish GI feedback
		// before resetting it for the next use of this slot.
		Vans::VansGpuProfiler::Get().MarkSubmittedFrameRetired(slot.profilerFrameIndex);
		slot.profilerFrameIndex = UINT64_MAX;
		rayTracingContext.CompleteGIProbeUpdate(slot.submittedWithAsyncCompute
			? slot.asyncCommands.rayTracing.m_CommandBufferFinishSubmitFence : fence);
		const VkResult resetResult = VansGraphics::vkResetFences(m_VansVKLogicDevice, 1, &fence);
		if (resetResult != VK_SUCCESS)
		{
			m_DeviceLost = m_DeviceLost || resetResult == VK_ERROR_DEVICE_LOST;
			slot.frameContext.frameSubmitSucceeded = false;
			VANS_LOG_ERROR("[VansVKDevice] Failed to reset retired frame slot fence. VkResult="
				<< static_cast<int>(resetResult));
			return false;
		}

		// The final fence covers every node of this slot's submit graph.
		slot.gpuWorkPending = false;

		if (!slot.graphicsCommandBuffer.ResetCommandBuffer(false))
		{
			slot.frameContext.frameSubmitSucceeded = false;
			return false;
		}
		if (slot.submittedWithAsyncCompute &&
			!ResetSubmittedAsyncFrameCommands(slot.asyncCommands))
		{
			slot.frameContext.frameSubmitSucceeded = false;
			return false;
		}
		if (!ResetGBufferSecondaryCommandBuffersIfNeeded(slot))
		{
			slot.frameContext.frameSubmitSucceeded = false;
			return false;
		}

		const uint64_t flushCount = static_cast<uint64_t>(slot.deferredDeletes.Size());
		slot.deferredDeletes.Flush();
		if (flushCount > 0)
			slot.frameContext.lastDeferredDeleteFlushCount = flushCount;
		slot.frameContext.pendingDeferredDeleteCount =
			static_cast<uint64_t>(slot.deferredDeletes.Size());
		slot.frameContext.frameSubmitSucceeded = true;
		slot.submittedWithAsyncCompute = false;
		for (VkFence& imageFence : m_SwapchainImageInFlightFences)
		{
			if (imageFence == fence)
				imageFence = VK_NULL_HANDLE;
		}
		return true;
	}

	void VansVKDevice::RecordSubmittedFrameDepth()
	{
		uint32_t submittedNotRetired = 0;
		for (const VansFrameContextRingSlot& slot : m_FrameContextRingSlots)
			submittedNotRetired += slot.gpuWorkPending ? 1u : 0u;

		const uint32_t previousPeak = m_ObservedSubmittedFramePeak.load(std::memory_order_relaxed);
		if (submittedNotRetired > previousPeak)
		{
			m_ObservedSubmittedFramePeak.store(submittedNotRetired, std::memory_order_release);
			VANS_LOG("[VansVKDevice] Submitted GPU frames not retired peak="
				<< submittedNotRetired << ", configured=" << m_ConfiguredFramesInFlight);
		}
	}

	bool VansVKDevice::ApplyCommandRecordingSettings(
		bool parallelEnabled,
		uint32_t framesInFlight,
		bool asyncComputeEnabled)
	{
		if (!VansCommandRecordingConfig::ValidFramesInFlight(framesInFlight))
		{
			VANS_LOG_ERROR("[VansVKDevice] Invalid framesInFlight=" << framesInFlight);
			return false;
		}
		if (framesInFlight > 1 && m_VansVKLogicDevice != VK_NULL_HANDLE &&
			(m_Features12.timelineSemaphore != VK_TRUE || !m_FrameQueueTimelines.IsReady()))
		{
			VANS_LOG_ERROR("[VansVKDevice] Two GPU frames require timeline semaphore support.");
			return false;
		}
		if (m_SwapchainAcquireState != SwapchainAcquireState::None)
		{
			const VkExtent2D extent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
			if (!OnWindowResize(extent.width, extent.height))
				return false;
		}
		const bool ringResourcesNeedChange =
			m_ConfiguredFramesInFlight != framesInFlight ||
			!m_FrameContextRingResourcesReady;
		const uint32_t previousFramesInFlight = m_ConfiguredFramesInFlight;
		const bool asyncStateNeedsChange =
			m_AsyncComputeRequested != asyncComputeEnabled;
		if ((ringResourcesNeedChange || asyncStateNeedsChange) &&
			m_VansVKLogicDevice != VK_NULL_HANDLE && !WaitForDevice())
		{
			return false;
		}
		// FidelityFX advances its Vulkan resource-view ring once per dispatch.
		// Reactive-mask generation and upscaling therefore need independent
		// contexts when two host frames can be recorded before GPU retirement.
		const bool fsrConcurrencyChanged = m_FSREnabled &&
			framesInFlight != previousFramesInFlight;
		if (fsrConcurrencyChanged &&
			!m_FSRController.ConfigureFrameConcurrency(framesInFlight))
		{
			// A failed SDK destruction can leave its context partially released.
			// Keep the old frame ring and rebuild the output through the normal
			// upscaler configuration path before the settings transaction returns.
			DeactivateFailedFSRContext();
			VANS_LOG_ERROR("[VansVKDevice] Failed to configure FSR frame concurrency.");
			return false;
		}
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		const VansRenderPassManager::FrameTargetCounts previousTargets =
			renderPassManager != nullptr ? renderPassManager->GetFrameTargetCounts()
				: VansRenderPassManager::FrameTargetCounts{};
		const auto rollbackFrameTargetGrowth = [&]()
		{
			if (fsrConcurrencyChanged &&
				!m_FSRController.ConfigureFrameConcurrency(previousFramesInFlight))
			{
				DeactivateFailedFSRContext();
				VANS_LOG_ERROR("[VansVKDevice] Failed to restore FSR frame concurrency after allocation failure.");
			}
			if (renderPassManager != nullptr)
				renderPassManager->ShrinkFrameTargetsAtIdle(previousTargets,
					m_MaintenanceGraphicsCommandBuffer, m_VansVKGraphicsQueue);
		};
		if (renderPassManager != nullptr &&
			!renderPassManager->GrowFrameTargetsAtIdle(framesInFlight,
				m_MaintenanceGraphicsCommandBuffer, m_VansVKGraphicsQueue))
		{
			rollbackFrameTargetGrowth();
			return false;
		}
		if (!ApplyFrameContextRingStateAtIdle(framesInFlight))
		{
			rollbackFrameTargetGrowth();
			return false;
		}
		if (renderPassManager != nullptr)
			renderPassManager->ShrinkFrameTargetsAtIdle(framesInFlight,
				m_MaintenanceGraphicsCommandBuffer, m_VansVKGraphicsQueue);
		if (previousTargets.hair != 0u && previousTargets.hair != framesInFlight)
			UpdateHairFrameTargetDescriptors(renderPassManager);
		if (previousTargets.opaqueSceneColor != 0u &&
			previousTargets.opaqueSceneColor != framesInFlight)
			UpdateTransmissionGlassDescriptors(renderPassManager);
		if (previousTargets.rawOpaqueSceneColor != 0u &&
			previousTargets.rawOpaqueSceneColor != framesInFlight && m_Scene != nullptr)
		{
			if (auto* atmosphere = m_Scene->GetAtmosphereSystem())
				atmosphere->RefreshRawOpaqueSceneColorDescriptors();
		}
		if (previousTargets.decal != 0u && previousTargets.decal != framesInFlight && m_Scene != nullptr)
			m_Scene->MarkRenderNodeDescriptorSetsDirty();
		if (previousTargets.gbuffer != 0u && previousTargets.gbuffer != framesInFlight)
			ResetFeatureDescriptorSets();
		m_EnableParallelCommandRecording = parallelEnabled;
		if (asyncStateNeedsChange)
			ApplyAsyncComputeStateAtIdle(asyncComputeEnabled);
		if (ringResourcesNeedChange || asyncStateNeedsChange)
			m_ObservedSubmittedFramePeak.store(0, std::memory_order_release);
		return true;
	}

	bool VansVKDevice::ApplyFrameContextRingStateAtIdle(uint32_t framesInFlight)
	{
		const uint32_t previousFramesInFlight = m_ConfiguredFramesInFlight;
		if (previousFramesInFlight == framesInFlight && m_FrameContextRingResourcesReady)
			return true;

		if (m_FrameContextRingResourcesReady)
			DestroyFrameContextRingResources();
		m_ConfiguredFramesInFlight = framesInFlight;
		if (!EnsureFrameContextRingResources())
		{
			m_ConfiguredFramesInFlight = previousFramesInFlight;
			if (!EnsureFrameContextRingResources())
				VANS_LOG_ERROR("[VansVKDevice] Failed to restore previous frame-context resources after allocation failure.");
			else
				BindFrameSyncResources(m_FrameContextRingSlots[0]);
			VANS_LOG_ERROR("[VansVKDevice] Frame-context resource initialization failed; previous configuration requested.");
			return false;
		}
		BindFrameSyncResources(m_FrameContextRingSlots[0]);
		return true;
	}

	bool VansVKDevice::BeginFrameContextRingFrame()
	{
		if (!EnsureFrameContextRingResources())
		{
			m_ActiveFrameContextSlot = nullptr;
			m_FrameContextRingSlots[0].frameContext.frameSubmitSucceeded = false;
			m_FrameContextRingSlots[0].frameContext.surfaceResizeRequired = false;
			return false;
		}

		VansFrameContextRingSlot& slot = m_FrameContextRingSlots[m_CurrentFrameContextSlotIndex % m_ConfiguredFramesInFlight];
		m_ActiveFrameContextSlot = &slot;
		slot.frameContext.frameSubmitSucceeded = false;
		slot.frameContext.surfaceResizeRequired = false;
		if (m_SwapchainAcquireState != SwapchainAcquireState::None)
		{
			const VkExtent2D extent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
			if (!OnWindowResize(extent.width, extent.height))
			{
				slot.frameContext.surfaceResizeRequired = true;
				return false;
			}
		}
		if (!WaitForFrameContextRingSlot(slot))
			return false;
		slot.projectPreludeCompletion = {};
		m_OverlaySampledImages.clear();
		if (m_SwapchainImageInFlightFences.size() != m_VansVKSurface.m_VansVKImageCount ||
			m_SwapchainImageRenderFinishedSemaphores.size() != m_VansVKSurface.m_VansVKImageCount)
		{
			slot.frameContext.frameSubmitSucceeded = false;
			VANS_LOG_ERROR("[VansVKDevice] Swapchain image sync resources do not match the current swapchain.");
			return false;
		}

		slot.frameContext.frameNumber = ++m_RenderFrameNumber;
		slot.frameContext.frameSubmitSucceeded = true;
		slot.submittedWithAsyncCompute = false;

		VkResult acquireResult = VK_ERROR_INITIALIZATION_FAILED;
		{
			VANS_PROFILE_SCOPE("Vulkan::AcquireSwapchainImage", Vans::ProfileCategory::CommandRecord);
			acquireResult = m_VansVKSurface.AcquireVulkanSwapChainImage(
				m_VansVKLogicDevice,
				slot.frameContext.swapchainImageIndex,
				slot.imageAcquiredSemaphore);
		}
		if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
		{
			m_DeviceLost = m_DeviceLost || acquireResult == VK_ERROR_DEVICE_LOST;
			slot.frameContext.frameSubmitSucceeded = false;
			slot.frameContext.surfaceResizeRequired = acquireResult == VK_ERROR_OUT_OF_DATE_KHR;
			if (slot.frameContext.surfaceResizeRequired)
				VANS_LOG("[VansVKDevice] Swapchain image is out of date; requesting surface resize.");
			else
				VANS_LOG_ERROR("[VansVKDevice] Swapchain image acquisition failed. VkResult=" << static_cast<int>(acquireResult));
			return false;
		}
		m_SwapchainAcquireState = SwapchainAcquireState::AwaitingGpuWait;
		m_AcquiredFrameContextSlotIndex = slot.slotIndex;
		if (slot.frameContext.swapchainImageIndex >= m_VansVKSurface.m_VansVKImageCount)
		{
			slot.frameContext.frameSubmitSucceeded = false;
			VANS_LOG_ERROR("[VansVKDevice] Acquired swapchain image index is out of range.");
			return false;
		}
		const VkFence imageFence = m_SwapchainImageInFlightFences[slot.frameContext.swapchainImageIndex];
		if (imageFence != VK_NULL_HANDLE)
		{
			bool ownerFound = false;
			for (VansFrameContextRingSlot& pendingSlot : m_FrameContextRingSlots)
			{
				if (pendingSlot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence == imageFence)
				{
					ownerFound = true;
					if (!WaitForFrameContextRingSlot(pendingSlot))
					{
						slot.frameContext.frameSubmitSucceeded = false;
						return false;
					}
					break;
				}
			}
			if (!ownerFound)
			{
				slot.frameContext.frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Acquired swapchain image references an unknown frame fence.");
				return false;
			}
		}

		m_SwapChainImageIndex = slot.frameContext.swapchainImageIndex;
		BindFrameSyncResources(slot);
		ResetFrameStageUploadAllocator();
		return true;
	}

	bool VansVKDevice::SubmitProjectRenderPrelude()
	{
		const auto& input = m_CurrentRenderSceneSnapshot.projectPasses;
		if (!input.configuration || input.configuration->passes.empty())
		{
			m_ProjectRenderPassRuntime.Release(*this);
			return true;
		}
		auto& slot = CurrentFrameSlot();
		auto& command = slot.projectPreludeCommandBuffer;
		if (!command.GetVKCommandBuffer())
		{
			CommandBufferCreateParams params{};
			params.pool_params = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			params.commandbuffer_level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			params.commandbuffer_count = 1;
			if (!command.CreateVulkanCommandBuffer(*this, m_QueueCapabilities.graphicsFamily, params) ||
				!CreateVKFence(false, command.m_CommandBufferFinishSubmitFence))
			{
				command.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
				return false;
			}
			slot.projectPreludeSubmit.Bind(m_VansVKLogicDevice, m_VansVKGraphicsQueue, m_VansVKComputeQueue);
			slot.projectPreludeSubmit.SetQueueTimelines(&m_FrameQueueTimelines);
			slot.projectPreludeSubmit.SetCrossFrameResourceLedger(&m_CrossFrameResourceLedger);
		}
		if (!command.ResetCommandBuffer(false)) return false;
		std::string error;
		if (!m_ProjectRenderPassRuntime.Record(*this, *m_Scene, input, command, slot.slotIndex,
			GetEffectiveFramesInFlight(), {m_RenderWidth,m_RenderHeight}, m_CurrentRenderSceneSnapshot.sceneEpoch, error))
		{
			m_ProjectRenderPassRuntime.ResolveSubmission(false);
			command.ResetCommandBuffer(false);
			VANS_LOG_ERROR("[ProjectRenderPass] Frame rejected; built-in rendering continues: " << error);
			return true;
		}
		if (!m_ProjectRenderPassRuntime.HasRecordedWork()) return true;
		slot.projectPreludeSubmit.Reset();
		VansFrameSubmitNode prelude;
		prelude.name = "BeforeBuiltInRender";
		prelude.commandBuffers = {command.GetVKCommandBuffer()};
		prelude.resources = m_ProjectRenderPassRuntime.RecordedResources();
		prelude.fence = command.m_CommandBufferFinishSubmitFence;
		slot.projectPreludeSubmit.AddNode(std::move(prelude));
		const bool submitted = slot.projectPreludeSubmit.Execute();
		m_ProjectRenderPassRuntime.ResolveSubmission(submitted);
		if (!submitted)
		{
			m_DeviceLost = m_DeviceLost || slot.projectPreludeSubmit.WasDeviceLost();
			VANS_LOG_ERROR("[ProjectRenderPass] Submission failed: " << slot.projectPreludeSubmit.GetLastError());
			return false;
		}
		const auto point = slot.projectPreludeSubmit.GetSubmittedTimelinePoint("BeforeBuiltInRender");
		if (!point) return false;
		slot.projectPreludeCompletion = *point;
		return true;
	}

	void VansVKDevice::Rendering()
	{
		VANS_PROFILE_SCOPE("Vulkan::Rendering", Vans::ProfileCategory::CommandRecord);
		if (!BeginFrameContextRingFrame())
			return;
		m_pActiveCommandBuffer = &CurrentGraphicsCommandBuffer();

		if (!m_CurrentRenderSceneSnapshot.sceneReady)
		{
			VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);

			// No scene loaded yet; begin the command buffer so the UI render
			// pass (recorded by DrawEditorWindows) can still be appended.
			// Present() will end the recording and submit.
			if (!CurrentGraphicsCommandBuffer().BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
			{
				CurrentFrameContext().frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Failed to begin main command buffer without scene.");
			}
			return;
		}

		if (!m_HasCurrentRenderView)
		{
			CurrentFrameContext().frameSubmitSucceeded = false;
			VANS_LOG_ERROR("[VansVKDevice] Rendering called without a prepared render-frame view.");
			return;
		}

		VANS_SET_FRAME_PHASE(VansFramePhase::RenderThreadConsume);
		const std::uint32_t frameResourceSlot =
			m_ActiveFrameContextSlot != nullptr ? m_ActiveFrameContextSlot->slotIndex : 0u;
		m_MainCameraVisibilityState.PrepareFrame(
			m_CurrentRenderView,
			m_CurrentRenderSceneSnapshot,
			frameResourceSlot,
			CurrentFrameContext().frameNumber);
		const auto& projectConfiguration = m_CurrentRenderSceneSnapshot.projectPasses.configuration;
		if (projectConfiguration && !projectConfiguration->passes.empty())
			m_Scene->PrepareRenderObjectFrameData(m_CurrentRenderSceneSnapshot, m_RenderWorld);
		VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);
		if (!SubmitProjectRenderPrelude())
		{
			CurrentFrameContext().frameSubmitSucceeded = false;
			return;
		}
		VANS_SET_FRAME_PHASE(VansFramePhase::RenderThreadConsume);
		// GI 重建会替换资源并标记绑定失效，必须先于本帧节点更新。
		if (m_CurrentRenderSceneSnapshot.terrainInteraction.enabled) RefreshRuntimeRenderTextureCatalog();
		// 否则切换当帧仍会使用上一套已释放的 atlas 和 probe state。
		ProcessPendingGISettings();
		m_Scene->PrepareRenderBackendData(
			m_CurrentRenderView,
			m_CurrentRenderSceneSnapshot,
			m_RenderWorld);
		m_DrawInstanceArena.BeginFrame(frameResourceSlot);
        if (!m_Scene->BindProjectRenderPassOutputs(m_Scene->GetGlobalDescriptorSet()))
            throw std::runtime_error("Project RenderTexture frame descriptor binding failed.");
		VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);

		auto renderPassManager = VansRenderPassManager::GetInstance();
		BuildCurrentRenderFramePlan(renderPassManager);

		if (!m_AsyncComputeEnabled)
		{
			VansVKCommandBuffer& frameGraphicsCommandBuffer = CurrentGraphicsCommandBuffer();
			m_pActiveCommandBuffer = &frameGraphicsCommandBuffer;
			// Original single-submit path.
			{
				VANS_PROFILE_SCOPE("Vulkan::BeginCommandBuffer", Vans::ProfileCategory::CommandRecord);
				if (!frameGraphicsCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin main graphics command buffer.");
					return;
				}
			}
			VkCommandBuffer cmd = frameGraphicsCommandBuffer.GetVKCommandBuffer();

			// Record video texture uploads into the main graphics submission.
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::VideoTextureUpload,
				[&]() { m_Scene->RecordFrameUploads(
					frameGraphicsCommandBuffer, m_CurrentRenderSceneSnapshot); });

			// Upload cloth simulation results from staging buffers to device-local vertex buffers
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::ClothVertexUpload,
				[&]()
				{
					VANS_PROFILE_SCOPE("Vulkan::RecordClothVertexUploads", Vans::ProfileCategory::CommandRecord);
					m_Scene->RecordClothVertexUploads(
						frameGraphicsCommandBuffer,
						m_CurrentRenderSceneSnapshot);
				});

			// Dispatch vegetation bone-sim + skinning compute passes
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::VegetationCompute,
				[&]()
				{
					VANS_PROFILE_SCOPE("Vulkan::RecordVegetationCompute", Vans::ProfileCategory::CommandRecord);
					m_Scene->RecordVegetationCompute(frameGraphicsCommandBuffer);
				});
			// 在本帧第一条 graphics command buffer 中只记录一次 query reset。
#if VANS_PROFILER_ENABLED
			Vans::VansGpuProfiler::Get().BeginQueue(
				cmd,
				Vans::VansGpuQueueLane::Graphics);
#endif
			RecordFrameGpuStep(
				m_CurrentFramePlan,
				VansRenderPassNames::MainCameraHiZCull,
				"Main Camera HiZ Cull",
				cmd,
				[&]() { UpdateMainCameraHiZCull(renderPassManager, frameGraphicsCommandBuffer); });

			{
				VANS_GPU_SCOPE(cmd, "Shadow Pass");
				const int cascadeCount = static_cast<int>(kVansRenderBootstrapSettings.cascadeCount);
				for (int cascade = 0; cascade < cascadeCount; ++cascade)
				{
					m_globalRenderStateData.cascadeIndex = cascade;
					RecordFrameStep(
						m_CurrentFramePlan,
						VansRenderPassNames::CascadeShadow,
						[&]()
						{
							// Keep cascade shadow inline until every caster path is audited for
							// parallel recording; incorrect shadow maps create visible artifacts.
							RecordFrameGraphicsPassNoGpuScope(
								m_CurrentFramePlan,
								VansRenderPassNames::CascadeShadow,
								renderPassManager,
								renderPassManager->m_VansShadowPass,
								frameGraphicsCommandBuffer,
								m_globalRenderStateData,
								[&]() { DrawShadowMap(renderPassManager, cmd); },
								cascade);
						});
				}
				m_globalRenderStateData.cascadeIndex = -1;
			}

			for (uint32_t atlasIndex = 0; atlasIndex < VANS_PUNCTUAL_SHADOW_ATLAS_COUNT; ++atlasIndex)
			{
				if (!HasPunctualShadowJobsForAtlas(m_CurrentRenderSceneSnapshot, atlasIndex))
					continue;
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::PunctualShadow,
					"Punctual light Shadow Pass",
					renderPassManager,
					renderPassManager->m_VansPunctualShadowPass,
					frameGraphicsCommandBuffer,
					m_globalRenderStateData,
					[&]() { DrawPunctualShadowMap(atlasIndex); },
					static_cast<int>(atlasIndex));
			}

			RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::SurfaceWeatherRipple,
				[&]()
				{
					if (auto* rain = m_Scene->GetRainRenderSystem())
						rain->GenerateSurfaceRipple(frameGraphicsCommandBuffer);
				});

			RecordFrameGpuStep(
				m_CurrentFramePlan,
				VansRenderPassNames::GBuffer,
				"GBuffer Pass",
				cmd,
				[&]()
				{
					if (!RecordSceneGBufferParallel(renderPassManager, frameGraphicsCommandBuffer,
						static_cast<int>(GetCurrentGBufferTargetSlot())))
					{
						RecordFrameGraphicsPassNoGpuScope(
							m_CurrentFramePlan,
							VansRenderPassNames::GBuffer,
							renderPassManager,
							renderPassManager->m_VansGBufferPass,
							frameGraphicsCommandBuffer,
							m_globalRenderStateData,
							[&]() { DrawSceneGBuffer(renderPassManager, frameGraphicsCommandBuffer); },
							static_cast<int>(GetCurrentGBufferTargetSlot()));
					}
				});

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::SkyMotionVector,
				"Sky Motion Vector Pass",
				renderPassManager,
				renderPassManager->m_VansSkyMotionVectorPass,
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSkyMotionVectorPass(frameGraphicsCommandBuffer); },
				static_cast<int>(GetCurrentGBufferTargetSlot()));

			RecordFrameGpuStep(
				m_CurrentFramePlan,
				VansRenderPassNames::Decal,
				"Decal Pass",
				cmd,
				[&]()
				{
					if (!RecordDecalPassParallel(renderPassManager, frameGraphicsCommandBuffer,
						static_cast<int>(GetFrameResourceSlot())))
					{
						RecordFrameGraphicsPassNoGpuScope(
							m_CurrentFramePlan,
							VansRenderPassNames::Decal,
							renderPassManager,
							renderPassManager->GetVansDecalPass(),
							frameGraphicsCommandBuffer,
							m_globalRenderStateData,
							[&]() { m_Scene->DrawDecalNodes(); },
							static_cast<int>(GetFrameResourceSlot()));
					}
				});

			RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::GTAODepth,
                [&]() { UpdateGTAODepth(renderPassManager, frameGraphicsCommandBuffer); });
            RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::GTAOMain,
                [&]() { UpdateGTAOMain(renderPassManager, frameGraphicsCommandBuffer); });

			{
				VANS_GPU_SCOPE(cmd, "Compute Between GBuffer And Deferred");
				// Build tile-light lists before the HZB update.
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::TileLightBuild, [&]() { BuildTileLightLists(frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::HZB, [&]() { UpdateHZB(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::PunctualShadowDebug, [&]() { UpdatePunctualShadowDebugPreview(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::GTAODenoise, [&]()
				{
					VANS_GPU_SCOPE_LANE(cmd, "GTAO.Denoise", Vans::VansGpuQueueLane::Graphics);
					DenoiseGTAO(renderPassManager, frameGraphicsCommandBuffer);
				});
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::ScreenSpaceShadow, [&]() { UpdateScreenSpaceShadow(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::RayTracing, [&]() { UpdateRayTracing(frameGraphicsCommandBuffer); });
				// DDGI atlas/state writes must be visible before SSGI samples them.
				{
					VkMemoryBarrier giProbeToSSGIBarrier = {};
					giProbeToSSGIBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					giProbeToSSGIBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					giProbeToSSGIBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					frameGraphicsCommandBuffer.PipelineBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						{ giProbeToSSGIBarrier });
				}
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::AtmosphereStaticLuts, [&]() { UpdateAtmosphereStaticLuts(frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::CloudShadow, [&]() { UpdateCloudShadow(frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::AtmosphereViewLuts, [&]() { UpdateAtmosphereViewLuts(frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::GIData, [&]() { UpdateGIData(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::SSR, [&]() { UpdateSSR(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::LocalMedia, [&]() { UpdateLocalMedia(frameGraphicsCommandBuffer); });

				// Make compute outputs visible to the deferred fragment stage.
				VkMemoryBarrier computeToFragmentBarrier = {};
				computeToFragmentBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				computeToFragmentBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				computeToFragmentBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				frameGraphicsCommandBuffer.PipelineBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					{ computeToFragmentBarrier });
			}

			// 延迟光照只写入未经大气的原始不透明 HDR 颜色。
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::RawOpaqueLighting,
				"Raw Opaque Lighting Pass",
				renderPassManager,
				renderPassManager->GetVansRawOpaqueLightingPass(),
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSceneRawOpaqueLighting(renderPassManager, frameGraphicsCommandBuffer); },
				static_cast<int>(GetFrameResourceSlot()));
            if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::RawOpaqueLighting))
            {
                // RenderPass 的隐式 finalLayout 已生效；后续拷贝不能仍按 UNDEFINED 丢弃这些输出。
                renderPassManager->GetRawOpaqueSceneColor(GetFrameResourceSlot()).SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                renderPassManager->GetDiffuseExitantRadianceHistory().SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }

			// Custom shaders with depthWrite=true are automatically routed here.
			// This pass writes SceneColor and the main scene depth before water coverage is generated.
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::ForwardOpaquePreAtmosphere,
				"Forward Opaque Pre Atmosphere Pass",
				renderPassManager,
				renderPassManager->GetVansPreAtmosphereSurfacePass(),
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]()
				{
					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
						m_Scene->DrawForwardOpaquePreAtmosphereNodes();
				}, static_cast<int>(GetFrameResourceSlot()));
			// 主深度已经包含 Deferred 与 Forward Opaque；云步进在最近表面终止，
			// 并读取当前帧局部雾 Froxel 处理重叠介质。
			RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::VolumetricCloud,
				[&]() { UpdateVolumetricCloud(frameGraphicsCommandBuffer); });
			// Generate water coverage only after opaque custom materials have populated main depth.
			if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot) &&
				IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterGBuffer))
			{
				auto* waterSys = m_Scene->GetWaterSystem();
				if (waterSys != nullptr)
				{
					const glm::vec3 camPos = m_CurrentRenderView.position;
					const glm::mat4 viewMatrix = m_CurrentRenderView.view;
					const glm::mat4 vpMatrix =
						m_CurrentRenderView.projection * viewMatrix;
					glm::vec3 mainLightDir = glm::vec3(0.35f, 1.0f, 0.25f);
					glm::vec3 mainLightColor = glm::vec3(1.0f);
					const auto& dirLights =
						m_CurrentRenderSceneSnapshot.light.directionalLights;
					if (!dirLights.empty())
					{
						mainLightDir = glm::normalize(dirLights[0].m_Direction);
						mainLightColor = glm::max(dirLights[0].m_Color, glm::vec3(0.0f)) *
							(std::max)(dirLights[0].m_Intensity, 0.0f);
					}
					// 水面预览使用渲染帧时间，编辑/暂停状态不冻结；其他系统仍使用游戏时间。
					const float waterDelta = static_cast<float>(
						m_CurrentRenderTiming.renderDeltaSeconds);
					waterSys->Update(waterDelta, camPos, viewMatrix,
						vpMatrix, mainLightDir, mainLightColor);
					RecordFrameGpuStep(
						m_CurrentFramePlan,
						VansRenderPassNames::WaterWaveCompute,
						"Water Wave Compute",
						cmd,
						[&]()
						{
							waterSys->UpdateWaveSimulation(
								frameGraphicsCommandBuffer, waterDelta);
						});
					RecordFrameGraphicsPass(
						m_CurrentFramePlan,
						VansRenderPassNames::WaterGBuffer,
						"Water GBuffer Pass",
						renderPassManager,
						renderPassManager->GetVansWaterGBufferPass(),
						frameGraphicsCommandBuffer,
						m_globalRenderStateData,
						[&]() { m_Scene->DrawWaterGBufferNode(); },
						static_cast<int>(GetCurrentGBufferTargetSlot()));
				}
			}
			if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameGpuStep(
					m_CurrentFramePlan,
					VansRenderPassNames::WaterSceneColorPyramidPrepare,
					"Water SceneColor Pyramid Prepare",
					frameGraphicsCommandBuffer.GetVKCommandBuffer(),
					[&]()
					{
						PrepareWaterBackgroundPyramid(
							renderPassManager, frameGraphicsCommandBuffer);
					});
			}
			// Water effects consume the coverage generated against the updated main depth.
			if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot) &&
				IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterPreCompute))
			{
				VANS_GPU_SCOPE(cmd, "Water Pre-Compute");
				auto* waterSys = m_Scene->GetWaterSystem();
				auto* matMgr = m_Scene->GetMaterialManager();
				// 延迟绑定 SSR HZB：首次可用时创建 descriptor set，避免无 HZB 工程启动时崩溃。
				if (waterSys != nullptr)
				{
					auto* hzbTex = matMgr ? matMgr->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT) : nullptr;
					if (hzbTex)
						waterSys->EnsureSSRDescriptorSet(&hzbTex->GetImage());
					waterSys->DispatchWaterThicknessCS(frameGraphicsCommandBuffer);
					waterSys->DispatchRefractionCS(frameGraphicsCommandBuffer);
					waterSys->DispatchWaterVolumeCS(frameGraphicsCommandBuffer);
					waterSys->DispatchWaterVolumeFilterCS(frameGraphicsCommandBuffer);
					waterSys->DispatchWaterSSR(frameGraphicsCommandBuffer);
				}
			}
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::WaterCompositePreAtmosphere,
				"Water Composite Pre Atmosphere Pass",
				renderPassManager,
				renderPassManager->GetVansPreAtmosphereSurfacePass(),
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]()
				{
					VANS_GPU_SCOPE(cmd, "Water Composite Pre Atmosphere");
					m_Scene->DrawWaterCompositeNode();
				}, static_cast<int>(GetFrameResourceSlot()));
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::AtmosphereComposite,
				[&]() { CompositeAtmosphere(frameGraphicsCommandBuffer); });
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::RainPrecipitation,
				"Rain Precipitation Pass",
				renderPassManager,
				renderPassManager->m_VansTransparentPass,
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]()
				{
					VANS_GPU_SCOPE(cmd, "Rain Precipitation");
					if (auto* rain = m_Scene->GetRainRenderSystem())
						rain->Render(frameGraphicsCommandBuffer, m_globalRenderStateData);
				}, static_cast<int>(GetCurrentGBufferTargetSlot()));
			if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot) &&
				IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::HairVisibility))
			{
				ClearHairVisibilityResources(renderPassManager, frameGraphicsCommandBuffer);
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::HairVisibility,
					"Hair Visibility Pass",
					renderPassManager,
					renderPassManager->GetVansHairVisibilityPass(),
					frameGraphicsCommandBuffer,
					m_globalRenderStateData,
					[&]() { m_Scene->DrawHairNodes(VansPass::HAIR_VISIBILITY); },
					static_cast<int>(GetFrameResourceSlot()));
				PrepareHairVisibilityForLighting(renderPassManager, frameGraphicsCommandBuffer);
                RecordFrameGraphicsPass(m_CurrentFramePlan, VansRenderPassNames::HairDepthResolve,
                    "Hair Depth Resolve Pass", renderPassManager,
                    renderPassManager->GetVansHairDepthResolvePass(), frameGraphicsCommandBuffer, m_globalRenderStateData,
                    [&]() { DrawHairDepthResolve(renderPassManager, frameGraphicsCommandBuffer); },
					static_cast<int>(GetFrameResourceSlot()));
			}

					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::HairLighting,
					"Hair Lighting Pass",
					renderPassManager,
					renderPassManager->GetVansHairLightingPass(),
					frameGraphicsCommandBuffer,
					m_globalRenderStateData,
					[&]() { DrawHairLighting(renderPassManager, frameGraphicsCommandBuffer); },
					static_cast<int>(GetFrameResourceSlot()));
                RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::HairDebug, [&]() { UpdateHairDebugPreview(renderPassManager, frameGraphicsCommandBuffer); });
			}

			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::DepthOfFieldPrepare,
				[&]()
				{
					VkMemoryBarrier sceneColorToPostProcess{};
					sceneColorToPostProcess.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					sceneColorToPostProcess.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					sceneColorToPostProcess.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					frameGraphicsCommandBuffer.PipelineBarrier(
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						{ sceneColorToPostProcess });

					UploadPostProcessProfileIfDirty();
					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
					{
						UpdateDepthOfField(renderPassManager, frameGraphicsCommandBuffer);
					}

					VkMemoryBarrier postProcessComputeToFragment{};
					postProcessComputeToFragment.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					postProcessComputeToFragment.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					postProcessComputeToFragment.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					frameGraphicsCommandBuffer.PipelineBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						{ postProcessComputeToFragment });
				});

			// Composite transparent content into HDR SceneColor before FSR.
					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::TransparentSceneColorPrepare,
					[&]() { PrepareSceneColorForTransparentPass(renderPassManager, frameGraphicsCommandBuffer); });
			}
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::TransparentPostProcess,
				"Transparent PostProcess Pass",
				renderPassManager,
				renderPassManager->m_VansTransparentPass,
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSceneTransparentPost(renderPassManager, frameGraphicsCommandBuffer); },
				static_cast<int>(GetCurrentGBufferTargetSlot()));
		}
		else
		{
			// Vegetation 是 Shadow/GBuffer 唯一的 early-compute 前置条件，单独提交后
			// 两条 graphics queue 无需等待 TileLight 与 MainCameraHiZ。
			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().vegetation;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordVegetation", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().vegetation.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin vegetation command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				Vans::VansGpuProfiler::Get().BeginQueue(
					CurrentAsyncFrameCommands().vegetation.GetVKCommandBuffer(),
					Vans::VansGpuQueueLane::Compute);
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::VegetationCompute,
					[&]()
					{
						VANS_GPU_SCOPE_LANE(
							CurrentAsyncFrameCommands().vegetation.GetVKCommandBuffer(),
							"Vegetation Compute",
							Vans::VansGpuQueueLane::Compute);
						m_Scene->RecordVegetationCompute(CurrentAsyncFrameCommands().vegetation);
					});
				if (!CurrentAsyncFrameCommands().vegetation.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end vegetation command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().vegetationRecorded = true;

			// TileLight 与上一帧 OcclusionHZB 的读取不依赖 vegetation 输出。
			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().earlyAux;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordEarlyAux", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().earlyAux.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin early auxiliary command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::TileLightBuild,
					[&]()
					{
						VANS_GPU_SCOPE_LANE(
							CurrentAsyncFrameCommands().earlyAux.GetVKCommandBuffer(),
							"Tile Light Build",
							Vans::VansGpuQueueLane::Compute);
						BuildTileLightLists(CurrentAsyncFrameCommands().earlyAux);
					});
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::MainCameraHiZCull,
					[&]()
					{
						VANS_GPU_SCOPE_LANE(
							CurrentAsyncFrameCommands().earlyAux.GetVKCommandBuffer(),
							"Main Camera HiZ Cull",
							Vans::VansGpuQueueLane::Compute);
						UpdateMainCameraHiZCull(renderPassManager, CurrentAsyncFrameCommands().earlyAux);
					});
				if (!CurrentAsyncFrameCommands().earlyAux.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end early auxiliary command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().earlyAuxRecorded = true;

			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().asyncAtmosphere;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordAsyncAtmosphere", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().asyncAtmosphere.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin async atmosphere command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::AtmosphereStaticLuts,
					[&]() { UpdateAtmosphereStaticLuts(CurrentAsyncFrameCommands().asyncAtmosphere); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::CloudShadow,
					[&]() { UpdateCloudShadow(CurrentAsyncFrameCommands().asyncAtmosphere); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::AtmosphereViewLuts,
					[&]() { UpdateAtmosphereViewLuts(CurrentAsyncFrameCommands().asyncAtmosphere); });
				if (!CurrentAsyncFrameCommands().asyncAtmosphere.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end async atmosphere command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().asyncAtmosphereRecorded = true;
			m_pActiveCommandBuffer = &CurrentGraphicsCommandBuffer();

			// 级联阴影与两个点光阴影图集在同一条 Shadow Queue 上顺序执行。
			// 两个图集仍是独立图像，但不再让同类型深度绘制争用两条逻辑 graphics queue。
			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().shadowMaps;
			VkCommandBuffer shadowCmd = CurrentAsyncFrameCommands().shadowMaps.GetVKCommandBuffer();
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordShadowMaps", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().shadowMaps.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin shadow-maps command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				{
					Vans::VansGpuProfiler::Get().BeginQueue(shadowCmd, Vans::VansGpuQueueLane::Graphics);
					VANS_GPU_SCOPE_LANE(shadowCmd, "Cascade Shadow", Vans::VansGpuQueueLane::Graphics);
					const int cascadeCount = static_cast<int>(kVansRenderBootstrapSettings.cascadeCount);
					for (int cascade = 0; cascade < cascadeCount; ++cascade)
					{
						m_globalRenderStateData.cascadeIndex = cascade;
						RecordFrameStep(
							m_CurrentFramePlan,
							VansRenderPassNames::CascadeShadow,
							[&]()
							{
								// 级联阴影继续内联录制，避免未审计 caster 路径产生可见错误。
								RecordFrameGraphicsPassNoGpuScope(
									m_CurrentFramePlan,
									VansRenderPassNames::CascadeShadow,
									renderPassManager,
									renderPassManager->m_VansShadowPass,
									CurrentAsyncFrameCommands().shadowMaps,
									m_globalRenderStateData,
									[&]() { DrawShadowMap(renderPassManager, shadowCmd); },
									cascade);
							});
					}
					m_globalRenderStateData.cascadeIndex = -1;
				}
				for (uint32_t atlasIndex = 0; atlasIndex < VANS_PUNCTUAL_SHADOW_ATLAS_COUNT; ++atlasIndex)
				{
					if (!HasPunctualShadowJobsForAtlas(m_CurrentRenderSceneSnapshot, atlasIndex))
						continue;
					const char* scopeName = atlasIndex == VANS_PUNCTUAL_SHADOW_PRIMARY_ATLAS_INDEX
						? "Punctual Shadow Atlas 0"
						: "Punctual Shadow Atlas 1";
					VANS_GPU_SCOPE_LANE(shadowCmd, scopeName, Vans::VansGpuQueueLane::Graphics);
					RecordFrameGraphicsPassNoGpuScope(
						m_CurrentFramePlan,
						VansRenderPassNames::PunctualShadow,
						renderPassManager,
						renderPassManager->m_VansPunctualShadowPass,
						CurrentAsyncFrameCommands().shadowMaps,
						m_globalRenderStateData,
						[&, atlasIndex]() { DrawPunctualShadowMap(atlasIndex); },
						static_cast<int>(atlasIndex));
				}
				if (!CurrentAsyncFrameCommands().shadowMaps.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end shadow-maps command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().shadowMapsRecorded = true;

			m_pActiveCommandBuffer = &CurrentGraphicsCommandBuffer();

			// GBuffer base owns uploads, geometry, depth, and per-surface motion.
			// Sky motion plus optional decals are finalized by the following command buffer.
			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().gbuffer;
			VkCommandBuffer cmd = CurrentAsyncFrameCommands().gbuffer.GetVKCommandBuffer();
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordGBufferCB", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().gbuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin GBuffer command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				Vans::VansGpuProfiler::Get().BeginQueue(cmd, Vans::VansGpuQueueLane::Graphics);
				// Upload video frames before G-buffer rendering.
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::VideoTextureUpload,
					[&]() { m_Scene->RecordFrameUploads(
						CurrentAsyncFrameCommands().gbuffer, m_CurrentRenderSceneSnapshot); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::ClothVertexUpload,
					[&]()
					{
						m_Scene->RecordClothVertexUploads(
							CurrentAsyncFrameCommands().gbuffer,
							m_CurrentRenderSceneSnapshot);
					});
			RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::SurfaceWeatherRipple,
				[&]()
				{
					if (auto* rain = m_Scene->GetRainRenderSystem())
						rain->GenerateSurfaceRipple(CurrentAsyncFrameCommands().gbuffer);
				});
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::GBuffer,
				[&]()
				{
					VANS_GPU_SCOPE(cmd, "GBuffer Pass");
					if (!RecordSceneGBufferParallel(renderPassManager, CurrentAsyncFrameCommands().gbuffer,
						static_cast<int>(GetCurrentGBufferTargetSlot())))
					{
						RecordFrameGraphicsPassNoGpuScope(
							m_CurrentFramePlan,
							VansRenderPassNames::GBuffer,
							renderPassManager,
							renderPassManager->m_VansGBufferPass,
							CurrentAsyncFrameCommands().gbuffer,
							m_globalRenderStateData,
							[&]() { DrawSceneGBuffer(renderPassManager, CurrentAsyncFrameCommands().gbuffer); },
							static_cast<int>(GetCurrentGBufferTargetSlot()));
					}
				});
				if (!CurrentAsyncFrameCommands().gbuffer.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end GBuffer command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().gbufferRecorded = true;

			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().gbufferMaterial;
			{
				VkCommandBuffer materialCmd = CurrentAsyncFrameCommands().gbufferMaterial.GetVKCommandBuffer();
				VANS_PROFILE_SCOPE("Vulkan::RecordGBufferMaterialCB", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().gbufferMaterial.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin GBuffer-material command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				Vans::VansGpuProfiler::Get().BeginQueue(materialCmd, Vans::VansGpuQueueLane::Graphics);
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::SkyMotionVector,
					"Sky Motion Vector Pass",
					renderPassManager,
					renderPassManager->m_VansSkyMotionVectorPass,
					CurrentAsyncFrameCommands().gbufferMaterial,
					m_globalRenderStateData,
					[&]() { DrawSkyMotionVectorPass(CurrentAsyncFrameCommands().gbufferMaterial); },
					static_cast<int>(GetCurrentGBufferTargetSlot()));
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::Decal,
					[&]()
					{
						VANS_GPU_SCOPE(materialCmd, "Decal Pass");
						if (!RecordDecalPassParallel(renderPassManager, CurrentAsyncFrameCommands().gbufferMaterial,
							static_cast<int>(GetFrameResourceSlot())))
						{
							RecordFrameGraphicsPassNoGpuScope(
								m_CurrentFramePlan,
								VansRenderPassNames::Decal,
								renderPassManager,
								renderPassManager->GetVansDecalPass(),
								CurrentAsyncFrameCommands().gbufferMaterial,
								m_globalRenderStateData,
								[&]() { m_Scene->DrawDecalNodes(); },
								static_cast<int>(GetFrameResourceSlot()));
						}
					});
				if (!CurrentAsyncFrameCommands().gbufferMaterial.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end GBuffer-material command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().gbufferMaterialRecorded = true;

			// GTAO 原始结果独立提交，GTAO denoise 只等待精确的 GTAORawReady 依赖。
			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().gtaoRaw;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordGTAORaw", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().gtaoRaw.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin GTAO-raw command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::GTAODepth,
                [&]() { UpdateGTAODepth(renderPassManager, CurrentAsyncFrameCommands().gtaoRaw); });
            RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::GTAOMain,
                [&]() { UpdateGTAOMain(renderPassManager, CurrentAsyncFrameCommands().gtaoRaw); });
			if (!CurrentAsyncFrameCommands().gtaoRaw.EndCommandBufferRecord())
			{
				CurrentFrameContext().frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Failed to end GTAO-raw command buffer.");
				ResetAsyncFrameCommandBuffersAfterFailure();
				return;
			}
			CurrentFrameContext().gtaoRawRecorded = true;

			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().asyncHZB;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordAsyncHZB", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().asyncHZB.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin async HZB command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				VkMemoryBarrier occlusionReadToWriteBarrier{};
				occlusionReadToWriteBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				occlusionReadToWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				occlusionReadToWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				CurrentAsyncFrameCommands().asyncHZB.PipelineBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					{ occlusionReadToWriteBarrier });
				{
					VANS_GPU_SCOPE_LANE(
						CurrentAsyncFrameCommands().asyncHZB.GetVKCommandBuffer(),
						"Async HZB",
						Vans::VansGpuQueueLane::Compute);
					RecordFrameStep(
						m_CurrentFramePlan,
						VansRenderPassNames::HZB,
						[&]() { UpdateHZB(renderPassManager, CurrentAsyncFrameCommands().asyncHZB); });
				}
				if (!CurrentAsyncFrameCommands().asyncHZB.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end async HZB command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().asyncHZBRecorded = true;

			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().rayTracing;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordRayTracing", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().rayTracing.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin ray-tracing command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				{
					VANS_GPU_SCOPE_LANE(
						CurrentAsyncFrameCommands().rayTracing.GetVKCommandBuffer(),
						"Ray Tracing",
						Vans::VansGpuQueueLane::Compute);
					RecordFrameStep(
						m_CurrentFramePlan,
						VansRenderPassNames::RayTracing,
						[&]() { UpdateRayTracing(CurrentAsyncFrameCommands().rayTracing); });
				}
				if (!CurrentAsyncFrameCommands().rayTracing.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end ray-tracing command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().rayTracingRecorded = true;

			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().giData;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordGIData", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().giData.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin GI-data command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				VkMemoryBarrier giInputsToSSGIBarrier{};
				giInputsToSSGIBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				giInputsToSSGIBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				giInputsToSSGIBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				CurrentAsyncFrameCommands().giData.PipelineBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					{ giInputsToSSGIBarrier });
				{
					VANS_GPU_SCOPE_LANE(
						CurrentAsyncFrameCommands().giData.GetVKCommandBuffer(),
						"GI Data",
						Vans::VansGpuQueueLane::Compute);
					RecordFrameStep(
						m_CurrentFramePlan,
						VansRenderPassNames::GIData,
						[&]() { UpdateGIData(renderPassManager, CurrentAsyncFrameCommands().giData); });
				}
				if (!CurrentAsyncFrameCommands().giData.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end GI-data command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().giDataRecorded = true;

			m_pActiveCommandBuffer = &CurrentAsyncFrameCommands().graphicsScreen;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordGraphicsScreen", Vans::ProfileCategory::CommandRecord);
				if (!CurrentAsyncFrameCommands().graphicsScreen.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin graphics screen command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				cmd = CurrentAsyncFrameCommands().graphicsScreen.GetVKCommandBuffer();
				{
					VANS_GPU_SCOPE_LANE(cmd, "Graphics Screen Compute", Vans::VansGpuQueueLane::Compute);
					RecordFrameStep(
						m_CurrentFramePlan,
						VansRenderPassNames::PunctualShadowDebug,
						[&]() { UpdatePunctualShadowDebugPreview(renderPassManager, CurrentAsyncFrameCommands().graphicsScreen); });
					RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::GTAODenoise, [&]()
					{
						VANS_GPU_SCOPE_LANE(cmd, "GTAO.Denoise", Vans::VansGpuQueueLane::Compute);
						DenoiseGTAO(renderPassManager, CurrentAsyncFrameCommands().graphicsScreen);
					});
					RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::ScreenSpaceShadow, [&]()
					{
						VANS_GPU_SCOPE_LANE(cmd, "Screen Space Shadow", Vans::VansGpuQueueLane::Compute);
						UpdateScreenSpaceShadow(renderPassManager, CurrentAsyncFrameCommands().graphicsScreen);
					});
					RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::SSR, [&]()
					{
						VANS_GPU_SCOPE_LANE(cmd, "SSR", Vans::VansGpuQueueLane::Compute);
						UpdateSSR(renderPassManager, CurrentAsyncFrameCommands().graphicsScreen);
					});
					RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::LocalMedia, [&]()
					{
						VANS_GPU_SCOPE_LANE(cmd, "Local Media", Vans::VansGpuQueueLane::Compute);
						UpdateLocalMedia(CurrentAsyncFrameCommands().graphicsScreen);
					});
				}
				// ScreenLightingReady semaphore 建立对
				// GraphicsMain fragment 阶段的跨 queue 可见性；compute-only queue
				// 不能在 command buffer 内使用 FRAGMENT_SHADER stage。
				if (!CurrentAsyncFrameCommands().graphicsScreen.EndCommandBufferRecord())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end graphics screen command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			CurrentFrameContext().graphicsScreenRecorded = true;

			m_pActiveCommandBuffer = &CurrentGraphicsCommandBuffer();
			{
				VANS_PROFILE_SCOPE("Vulkan::BeginCommandBuffer.GraphicsMain", Vans::ProfileCategory::CommandRecord);
				if (!CurrentGraphicsCommandBuffer().BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin main graphics command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			cmd = CurrentGraphicsCommandBuffer().GetVKCommandBuffer();
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::RawOpaqueLighting,
				"Raw Opaque Lighting Pass",
				renderPassManager,
				renderPassManager->GetVansRawOpaqueLightingPass(),
				CurrentGraphicsCommandBuffer(),
				m_globalRenderStateData,
				[&]() { DrawSceneRawOpaqueLighting(renderPassManager, CurrentGraphicsCommandBuffer()); },
				static_cast<int>(GetFrameResourceSlot()));
            if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::RawOpaqueLighting))
            {
                // RenderPass 的隐式 finalLayout 已生效；后续拷贝不能仍按 UNDEFINED 丢弃这些输出。
                renderPassManager->GetRawOpaqueSceneColor(GetFrameResourceSlot()).SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                renderPassManager->GetDiffuseExitantRadianceHistory().SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::ForwardOpaquePreAtmosphere,
				"Forward Opaque Pre Atmosphere Pass",
				renderPassManager,
				renderPassManager->GetVansPreAtmosphereSurfacePass(),
				CurrentGraphicsCommandBuffer(),
				m_globalRenderStateData,
				[&]()
				{
					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
						m_Scene->DrawForwardOpaquePreAtmosphereNodes();
				}, static_cast<int>(GetFrameResourceSlot()));
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::VolumetricCloud,
				[&]()
				{
					VANS_GPU_SCOPE_LANE(
						CurrentGraphicsCommandBuffer().GetVKCommandBuffer(),
						"Cloud Ray March",
						Vans::VansGpuQueueLane::Graphics);
					UpdateVolumetricCloud(CurrentGraphicsCommandBuffer());
				});
			if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot) &&
				IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterGBuffer))
			{
				auto* waterSys = m_Scene->GetWaterSystem();
				if (waterSys != nullptr)
				{
					const glm::vec3 camPos = m_CurrentRenderView.position;
					const glm::mat4 viewMatrix = m_CurrentRenderView.view;
					const glm::mat4 vpMatrix =
						m_CurrentRenderView.projection * viewMatrix;
					glm::vec3 mainLightDir = glm::vec3(0.35f, 1.0f, 0.25f);
					glm::vec3 mainLightColor = glm::vec3(1.0f);
					const auto& dirLights =
						m_CurrentRenderSceneSnapshot.light.directionalLights;
					if (!dirLights.empty())
					{
						mainLightDir = glm::normalize(dirLights[0].m_Direction);
						mainLightColor = glm::max(dirLights[0].m_Color, glm::vec3(0.0f)) *
							(std::max)(dirLights[0].m_Intensity, 0.0f);
					}
					// 与同步路径一致，仅水系统使用不受游戏暂停影响的渲染帧时间。
					const float waterDelta = static_cast<float>(
						m_CurrentRenderTiming.renderDeltaSeconds);
					waterSys->Update(waterDelta, camPos, viewMatrix,
						vpMatrix, mainLightDir, mainLightColor);
				RecordFrameGpuStep(
					m_CurrentFramePlan,
					VansRenderPassNames::WaterWaveCompute,
					"Water Wave Compute",
					cmd,
					[&]()
					{
						waterSys->UpdateWaveSimulation(
								CurrentGraphicsCommandBuffer(), waterDelta);
					});
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::WaterGBuffer,
					"Water GBuffer Pass",
					renderPassManager,
					renderPassManager->GetVansWaterGBufferPass(),
					CurrentGraphicsCommandBuffer(),
					m_globalRenderStateData,
					[&]() { m_Scene->DrawWaterGBufferNode(); },
					static_cast<int>(GetCurrentGBufferTargetSlot()));
				}
			}

			if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameGpuStep(
					m_CurrentFramePlan,
					VansRenderPassNames::WaterSceneColorPyramidPrepare,
					"Water SceneColor Pyramid Prepare",
					CurrentGraphicsCommandBuffer().GetVKCommandBuffer(),
					[&]()
					{
						PrepareWaterBackgroundPyramid(
							renderPassManager, CurrentGraphicsCommandBuffer());
					});
			}

			if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot) &&
				IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterPreCompute))
			{
				VANS_GPU_SCOPE(cmd, "Water Pre-Compute");
				auto* waterSys = m_Scene->GetWaterSystem();
				auto* matMgr = m_Scene->GetMaterialManager();
				if (waterSys != nullptr)
				{
					auto* hzbTex = matMgr ? matMgr->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT) : nullptr;
					if (hzbTex)
						waterSys->EnsureSSRDescriptorSet(&hzbTex->GetImage());
					waterSys->DispatchWaterThicknessCS(CurrentGraphicsCommandBuffer());
					waterSys->DispatchRefractionCS(CurrentGraphicsCommandBuffer());
					waterSys->DispatchWaterVolumeCS(CurrentGraphicsCommandBuffer());
					waterSys->DispatchWaterVolumeFilterCS(CurrentGraphicsCommandBuffer());
					waterSys->DispatchWaterSSR(CurrentGraphicsCommandBuffer());
				}
			}
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::WaterCompositePreAtmosphere,
				"Water Composite Pre Atmosphere Pass",
				renderPassManager,
				renderPassManager->GetVansPreAtmosphereSurfacePass(),
				CurrentGraphicsCommandBuffer(),
				m_globalRenderStateData,
				[&]()
				{
					VANS_GPU_SCOPE(cmd, "Water Composite Pre Atmosphere");
					m_Scene->DrawWaterCompositeNode();
				}, static_cast<int>(GetFrameResourceSlot()));
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::AtmosphereComposite,
				[&]() { CompositeAtmosphere(CurrentGraphicsCommandBuffer()); });
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::RainPrecipitation,
				"Rain Precipitation Pass",
				renderPassManager,
				renderPassManager->m_VansTransparentPass,
				CurrentGraphicsCommandBuffer(),
				m_globalRenderStateData,
				[&]()
				{
					VANS_GPU_SCOPE(cmd, "Rain Precipitation");
					if (auto* rain = m_Scene->GetRainRenderSystem())
						rain->Render(CurrentGraphicsCommandBuffer(), m_globalRenderStateData);
				}, static_cast<int>(GetCurrentGBufferTargetSlot()));
			if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot) &&
				IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::HairVisibility))
			{
				ClearHairVisibilityResources(renderPassManager, CurrentGraphicsCommandBuffer());
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::HairVisibility,
					"Hair Visibility Pass",
					renderPassManager,
					renderPassManager->GetVansHairVisibilityPass(),
					CurrentGraphicsCommandBuffer(),
					m_globalRenderStateData,
					[&]() { m_Scene->DrawHairNodes(VansPass::HAIR_VISIBILITY); },
					static_cast<int>(GetFrameResourceSlot()));
				PrepareHairVisibilityForLighting(renderPassManager, CurrentGraphicsCommandBuffer());
                RecordFrameGraphicsPass(m_CurrentFramePlan, VansRenderPassNames::HairDepthResolve,
                    "Hair Depth Resolve Pass", renderPassManager,
                    renderPassManager->GetVansHairDepthResolvePass(), CurrentGraphicsCommandBuffer(), m_globalRenderStateData,
                    [&]() { DrawHairDepthResolve(renderPassManager, CurrentGraphicsCommandBuffer()); },
					static_cast<int>(GetFrameResourceSlot()));
			}

					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::HairLighting,
					"Hair Lighting Pass",
					renderPassManager,
					renderPassManager->GetVansHairLightingPass(),
					CurrentGraphicsCommandBuffer(),
					m_globalRenderStateData,
					[&]() { DrawHairLighting(renderPassManager, CurrentGraphicsCommandBuffer()); },
					static_cast<int>(GetFrameResourceSlot()));
                RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::HairDebug, [&]() { UpdateHairDebugPreview(renderPassManager, CurrentGraphicsCommandBuffer()); });
			}

			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::DepthOfFieldPrepare,
				[&]()
				{
					VkMemoryBarrier sceneColorToPostProcess{};
					sceneColorToPostProcess.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					sceneColorToPostProcess.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
					sceneColorToPostProcess.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					CurrentGraphicsCommandBuffer().PipelineBarrier(
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						{ sceneColorToPostProcess });

					UploadPostProcessProfileIfDirty();
					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
					{
						UpdateDepthOfField(renderPassManager, CurrentGraphicsCommandBuffer());
					}

					VkMemoryBarrier postProcessComputeToFragment{};
					postProcessComputeToFragment.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					postProcessComputeToFragment.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					postProcessComputeToFragment.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					CurrentGraphicsCommandBuffer().PipelineBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						{ postProcessComputeToFragment });
				});

					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::TransparentSceneColorPrepare,
					[&]() { PrepareSceneColorForTransparentPass(renderPassManager, CurrentGraphicsCommandBuffer()); });
			}
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::TransparentPostProcess,
				"Transparent PostProcess Pass",
				renderPassManager,
				renderPassManager->m_VansTransparentPass,
				CurrentGraphicsCommandBuffer(),
				m_globalRenderStateData,
				[&]() { DrawSceneTransparentPost(renderPassManager, CurrentGraphicsCommandBuffer()); },
				static_cast<int>(GetCurrentGBufferTargetSlot()));
		}

		// 后续公共尾部仍会通过 Scene 的便捷绘制入口读取活动命令缓冲。
		// 帧上下文环启用时必须保持绑定当前 slot，不能回退到未开始录制的旧主缓冲。
		m_pActiveCommandBuffer = &CurrentGraphicsCommandBuffer();

		RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::ExposureBloom, [&]()
		{
			VansVKCommandBuffer& postTransparentCommandBuffer = CurrentGraphicsCommandBuffer();
			VkMemoryBarrier sceneColorToPostProcess{};
			sceneColorToPostProcess.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			sceneColorToPostProcess.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			sceneColorToPostProcess.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			postTransparentCommandBuffer.PipelineBarrier(
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				{ sceneColorToPostProcess });
			UploadPostProcessProfileIfDirty();
			UpdateExposure(renderPassManager, postTransparentCommandBuffer);
					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				UpdateBloom(renderPassManager, postTransparentCommandBuffer);
			}
			VkMemoryBarrier postProcessToUpscaler{};
			postProcessToUpscaler.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			postProcessToUpscaler.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			postProcessToUpscaler.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			postTransparentCommandBuffer.PipelineBarrier(
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{ postProcessToUpscaler });
		});

		// ── Unified Temporal Upscale (graphics tail for serial and async) ───
		RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::TemporalUpscale, [&]()
		{
			VANS_PROFILE_SCOPE("Vulkan::RecordTemporalUpscale", Vans::ProfileCategory::CommandRecord);
			VansVKCommandBuffer& frameGraphicsCommandBuffer = CurrentGraphicsCommandBuffer();
			if (m_DLSSEnabled &&
				m_UpscalerManager.GetEffectiveConfig().backend == VansUpscalerBackend::DLSS)
			{
				const VkImageLayout outputLayout = m_UpscalerOutputImage.GetImageLayout();
				VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
					frameGraphicsCommandBuffer,
					m_UpscalerOutputImage,
					outputLayout == VK_IMAGE_LAYOUT_UNDEFINED
						? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
						: VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					outputLayout == VK_IMAGE_LAYOUT_UNDEFINED
						? 0u
						: VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
					outputLayout,
					VK_IMAGE_LAYOUT_GENERAL);
				VansStreamlineDLSSDispatch dispatch;
				const bool dispatched = PrepareDLSSDispatchResources(frameGraphicsCommandBuffer) &&
					BuildDLSSDispatch(dispatch) &&
					m_DLSSController.Dispatch(dispatch);
				// Streamline records pipelines/descriptors outside our binding cache,
				// including on failed evaluation. Force subsequent passes to rebind.
				frameGraphicsCommandBuffer.InvalidateExternalBindings();
				if (dispatched)
				{
					m_RecordedTemporalFrameNumber = CurrentFrameContext().frameNumber;
				}
				else
				{
					m_UpscalerManager.GetHistory().RequestReset(
						VansUpscalerResetReason::DispatchFault);
					if (!RecordFSRFallbackUpscale(frameGraphicsCommandBuffer))
						VANS_LOG_ERROR("[DLSS] Native fallback copy also failed");
					m_UpscalerManager.ActivateRuntimeFallback(
						VansUpscalerBackend::FSR,
						m_UpscalerManager.GetEffectiveConfig().quality,
						VansUpscalerFallbackReason::DispatchFailed,
						"DLSS dispatch failed; switching to FSR");
					m_UpscalerConfigDirty = true;
				}
			}
			else if (m_FSREnabled)
			{
				VansVKImage& fsrOut = m_UpscalerOutputImage;
				const VkImageLayout fsrOutputLayout = fsrOut.GetImageLayout();
				VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
					frameGraphicsCommandBuffer,
					fsrOut,
					fsrOutputLayout == VK_IMAGE_LAYOUT_UNDEFINED
						? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
						: VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					fsrOutputLayout == VK_IMAGE_LAYOUT_UNDEFINED
						? 0u
						: VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
					fsrOutputLayout,
					VK_IMAGE_LAYOUT_GENERAL);
				VansFSRFrameInput fsrInput;
				const bool fsrInputReady = BuildFSRFrameInput(fsrInput);
				if (fsrInputReady)
					RecordFSRMasks(frameGraphicsCommandBuffer, fsrInput);
				const bool fsrDispatched = fsrInputReady &&
					m_FSRController.DispatchUpscale(
						frameGraphicsCommandBuffer.GetVKCommandBuffer(), fsrInput);
				if (fsrDispatched)
					RestoreFSRDepthLayout(frameGraphicsCommandBuffer);
				if (fsrDispatched)
				{
					m_RecordedTemporalFrameNumber = CurrentFrameContext().frameNumber;
				}
				else
				{
					// A failed frame must retain Reset so the next valid dispatch starts cleanly.
					m_UpscalerManager.GetHistory().RequestReset(VansUpscalerResetReason::DispatchFault);
					VANS_LOG_ERROR("[FSR] Upscale failed; temporal reset remains pending");
					if (!RecordFSRFallbackUpscale(frameGraphicsCommandBuffer))
						VANS_LOG_ERROR("[FSR] Fallback upscale copy also failed");
				}
			}
			else if (!RecordFSRFallbackUpscale(frameGraphicsCommandBuffer))
			{
				VANS_LOG_ERROR("[Upscaler] Native output copy failed");
			}
		});

		RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::DisplayPostProcess, [&]()
		{
			VANS_PROFILE_SCOPE("Vulkan::RecordDisplayPostProcess", Vans::ProfileCategory::CommandRecord);
			VansVKCommandBuffer& frameGraphicsCommandBuffer = CurrentGraphicsCommandBuffer();
			RecordDisplayPostProcess(frameGraphicsCommandBuffer);
		});

		RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::RuntimeUI, [&]()
		{
			VANS_PROFILE_SCOPE("Vulkan::RecordRuntimeUI", Vans::ProfileCategory::CommandRecord);
			VkCommandBuffer cmd = CurrentGraphicsCommandBuffer().GetVKCommandBuffer();
			VansVKImage& finalDisplay = renderPassManager->GetFinalDisplayColor();
			VansRuntime::VansUISystem::Get().RenderOffscreen(static_cast<void*>(cmd));
			BeginSceneUIRenderPass();
			VansRuntime::VansUISystem::Get().RenderDocuments(
				static_cast<void*>(GetSceneUIRenderPassHandle()), 1);
			EndSceneUIRenderPass();
			finalDisplay.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			if (m_PresentFinalDisplayToSwapchain)
				RecordFinalDisplayToSwapchain();
			// The final display image is now ready for the editor scene window.
		});
	}


	void VansVKDevice::PrepareRenderingFrame()
	{
		if (m_DeviceLost)
			throw std::runtime_error("Vulkan device was lost");
		// Rendering may have failed after recording the temporal dispatch, in
		// which case RenderSystem never calls Present. Do not consume its reset.
		ResolvePreparedFrameHistory(false);
		// 上采样配置可能重建 GPU 资源；变更帧仍先退役，普通帧把等待留给提交准备的写入边界。
		if (m_UpscalerConfigDirty)
		{
			if (!RetireSubmittedFrame())
				throw std::runtime_error("Failed to retire submitted GPU frame before upscaler reconfiguration");
			ProcessPendingUpscalerConfig();
		}
		m_PipelineCacheService.TickPersistence();
	}

	void VansVKDevice::ResolvePreparedFrameHistory(bool submitted, bool graphExecuted)
	{
		if (m_Scene && m_Scene->GetVegetationCollection())
		{
			const bool vegetationSubmitted = submitted || (graphExecuted &&
				(CurrentFrameSubmitGraph().GetSubmittedTimelinePoint("Vegetation").has_value() ||
				 CurrentFrameSubmitGraph().GetSubmittedTimelinePoint("GraphicsFrame").has_value()));
			m_Scene->GetVegetationCollection()->ForEach([vegetationSubmitted](VansVegetationSystem& system)
				{ system.ResolveGrassStateTransfer(vegetationSubmitted); });
		}
		m_MainCameraVisibilityState.ResolveRecordedFrameSubmission(submitted);
		if (m_CameraFramePendingSubmission)
		{
			if (submitted)
				++m_CameraRenderFrameIndex;
			else
				m_CameraData = m_CameraDataBeforePendingFrame;
			m_CameraFramePendingSubmission = false;
		}
		if (m_GIReceiverVisibilityFrameRecorded)
		{
			if (m_Scene != nullptr && m_Scene->GetMaterialManager() != nullptr)
			{
				auto& frame = m_Scene->GetMaterialManager()->m_GIReceiverVisibility.frame;
				if (submitted)
					++frame;
				else
					frame = 0; // A partially submitted graph may have touched the history buffer.
			}
			m_GIReceiverVisibilityFrameRecorded = false;
		}
		if (m_SSGIFrameRecorded)
		{
			if (m_Scene != nullptr && m_Scene->GetMaterialManager() != nullptr)
			{
				auto& frame = m_Scene->GetMaterialManager()->m_SSGITemporalFrame;
				if (submitted)
					++frame;
				else
					frame = 0; // A partially submitted graph may have updated a history image.
			}
			m_SSGIFrameRecorded = false;
		}
		if (m_AmbientSkyCacheFrameRecorded)
		{
			if (!submitted && m_Scene != nullptr && m_Scene->GetMaterialManager() != nullptr)
			{
				auto* materials = m_Scene->GetMaterialManager();
				// The GIData node may already have written part of the cache. The
				// next frame must clear it before using the recorded ring cursor.
				materials->m_AmbientSkyCacheInitialized = false;
				materials->m_AmbientSkyCacheFrameOffset = 0u;
			}
			m_AmbientSkyCacheFrameRecorded = false;
		}
		if (m_NearMediaFrameRecorded)
		{
			if (!submitted && m_Scene != nullptr)
				if (auto* nearMedia = m_Scene->GetNearMediaSystem())
					nearMedia->DiscardRecordedFrame();
			m_NearMediaFrameRecorded = false;
		}
		if (m_CloudFrameRecorded)
		{
			if (!submitted && m_Scene != nullptr)
				if (auto* clouds = m_Scene->GetVolumetricCloudSystem())
					clouds->DiscardRecordedFrame();
			m_CloudFrameRecorded = false;
		}
		if (m_AtmosphereFrameRecorded)
		{
			if (!submitted && m_Scene != nullptr)
				if (auto* atmosphere = m_Scene->GetAtmosphereSystem())
					atmosphere->DiscardRecordedFrame();
			m_AtmosphereFrameRecorded = false;
		}
		if (m_RecordedTemporalFrameNumber != 0)
		{
			m_UpscalerManager.GetHistory().OnTemporalDispatchSubmission(
				submitted && m_RecordedTemporalFrameNumber == CurrentFrameContext().frameNumber);
			m_RecordedTemporalFrameNumber = 0;
		}
	}

	bool VansVKDevice::ResetSubmittedAsyncFrameCommands(VansAsyncFrameCommands& commands)
	{
		VANS_PROFILE_SCOPE("Vulkan::ResetAsyncFrameCommandBuffers", Vans::ProfileCategory::VulkanSubmit);
		static constexpr std::array<const char*, 11> names = {
			"shadow maps", "GBuffer", "GBuffer-material", "GTAO-raw",
			"graphics-screen", "vegetation", "early auxiliary", "async atmosphere",
			"async HZB", "ray-tracing", "GI-data"
		};
		const auto buffers = commands.All();
		std::array<VkFence, 11> fences{};
		for (std::size_t i = 0; i < buffers.size(); ++i)
			fences[i] = buffers[i]->m_CommandBufferFinishSubmitFence;
		const VkResult resetResult = VansGraphics::vkResetFences(
			m_VansVKLogicDevice,
			static_cast<uint32_t>(fences.size()),
			fences.data());
		if (resetResult != VK_SUCCESS)
		{
			m_DeviceLost = m_DeviceLost || resetResult == VK_ERROR_DEVICE_LOST;
			VANS_LOG_ERROR("[VansVKDevice] Failed to reset async frame fences.");
			return false;
		}
		for (std::size_t i = 0; i < buffers.size(); ++i)
		{
			if (!buffers[i]->ResetCommandBuffer(false))
			{
				VANS_LOG_ERROR("[VansVKDevice] Failed to reset " << names[i] << " command buffer.");
				return false;
			}
		}
		return true;
	}

	bool VansVKDevice::RetireSubmittedFrame()
	{
		VansFrameContextRingSlot& slot = CurrentFrameSlot();
		if (!WaitForFrameContextRingSlot(slot))
		{
			CurrentFrameContext().frameSubmitSucceeded = false;
			return false;
		}
		// Retirement succeeds once the prior GPU work is drained, even if that
		// frame's recording or submit failed. Rendering starts a fresh frame status.
		return true;
	}

	void VansVKDevice::Present()
	{
		if (m_RenderTextureCatalogRequested.load(std::memory_order_acquire)) RefreshRuntimeRenderTextureCatalog();
		VANS_PROFILE_SCOPE("Vulkan::Present", Vans::ProfileCategory::VulkanSubmit);
		m_pActiveCommandBuffer = &m_MaintenanceGraphicsCommandBuffer;
		CurrentFrameSubmitGraph().SetQueueTimelines(
			m_ConfiguredFramesInFlight > 1 && m_FrameQueueTimelines.IsReady()
				? &m_FrameQueueTimelines : nullptr);
		CurrentFrameSubmitGraph().SetCrossFrameResourceLedger(
			m_ConfiguredFramesInFlight > 1 && m_FrameQueueTimelines.IsReady()
				? &m_CrossFrameResourceLedger : nullptr);
		const auto appendGIRegionResources = [this](VansFrameSubmitNode& node,
			VkPipelineStageFlags stages, VkAccessFlags access, bool write)
		{
			const auto& layout = rayTracingContext.GetGIProbeLayoutBuffer();
			if (layout.GetNativeBuffer() != VK_NULL_HANDLE)
				node.resources.push_back({ "GIProbeLayout", stages, access,
					VK_IMAGE_LAYOUT_UNDEFINED, false, write, true, false,
					VansPhysicalResourceIdentity::ForBuffer(layout.GetNativeBuffer()) });
			for (uint32_t index = 0; index < rayTracingContext.GetGIRegionCount(); ++index)
			{
				const std::string suffix = std::to_string(index);
				if (auto* irradiance = rayTracingContext.GetGIRegionIrradianceAtlas(index);
					irradiance && irradiance->GetImage().GetImage() != VK_NULL_HANDLE)
					node.resources.push_back({ "GIRegionIrradiance" + suffix, stages, access,
						VK_IMAGE_LAYOUT_GENERAL, true, write, true, false,
						VansPhysicalResourceIdentity::ForImage(irradiance->GetImage().GetImage()) });
				if (auto* visibility = rayTracingContext.GetGIRegionVisibilityAtlas(index);
					visibility && visibility->GetImage().GetImage() != VK_NULL_HANDLE)
					node.resources.push_back({ "GIRegionVisibility" + suffix, stages, access,
						VK_IMAGE_LAYOUT_GENERAL, true, write, true, false,
						VansPhysicalResourceIdentity::ForImage(visibility->GetImage().GetImage()) });
				if (const auto* state = rayTracingContext.GetGIRegionProbeStateBuffer(index);
					state && state->GetNativeBuffer() != VK_NULL_HANDLE)
					node.resources.push_back({ "GIRegionProbeState" + suffix, stages, access,
						VK_IMAGE_LAYOUT_UNDEFINED, false, write, true, false,
						VansPhysicalResourceIdentity::ForBuffer(state->GetNativeBuffer()) });
			}
		};
		const auto appendTileLightBuffers = [this](VansFrameSubmitNode& node,
			VkPipelineStageFlags stages, VkAccessFlags access, bool write)
		{
			const VansMaterialManager* materials = m_Scene ? m_Scene->GetMaterialManager() : nullptr;
			if (materials == nullptr)
				return;
			const auto appendBuffer = [&](const char* name, const VansVKBuffer& buffer)
			{
				if (buffer.GetNativeBuffer() != VK_NULL_HANDLE)
					node.resources.push_back({ name, stages, access,
						VK_IMAGE_LAYOUT_UNDEFINED, false, write, true, false,
						VansPhysicalResourceIdentity::ForBuffer(buffer.GetNativeBuffer()) });
			};
			const auto& frame = materials->m_TileLightFrames[GetFrameResourceSlot()];
			appendBuffer("TileLightGridBuffer", frame.header);
			appendBuffer("TileLightIndexBuffer", frame.indices);
		};
		const auto appendGIReceiverBuffers = [this](VansFrameSubmitNode& node,
			VkPipelineStageFlags stages, VkAccessFlags access)
		{
			const auto* materials = m_Scene ? m_Scene->GetMaterialManager() : nullptr;
			if (materials == nullptr || !materials->m_GIReceiverVisibility.enabled)
				return;
			materials->m_GIReceiverVisibility.ForEachMutableBuffer(
				[&](const char* name, VkBuffer buffer)
				{
					if (buffer != VK_NULL_HANDLE)
						node.resources.push_back({ name, stages, access,
							VK_IMAGE_LAYOUT_UNDEFINED, false, true, true, false,
							VansPhysicalResourceIdentity::ForBuffer(buffer) });
				});
		};
		struct VegetationFrameBufferAccess
		{
			VkBuffer buffer = VK_NULL_HANDLE;
			VansVegetationSystem::FrameBufferConsumer consumer =
				VansVegetationSystem::FrameBufferConsumer::ComputeOnly;
		};
		std::vector<VegetationFrameBufferAccess> vegetationFrameBuffers;
		if (m_Scene && m_Scene->GetVegetationCollection())
			m_Scene->GetVegetationCollection()->ForEachViewRelevant([&](VansVegetationSystem& system)
			{
				system.ForEachMutableFrameBuffer(GetFrameResourceSlot(), [&](VkBuffer buffer,
					VansVegetationSystem::FrameBufferConsumer consumer)
				{
					vegetationFrameBuffers.push_back({ buffer, consumer });
				});
			});
		const auto appendVegetationBuffers = [&vegetationFrameBuffers](
			VansFrameSubmitNode& node, VkPipelineStageFlags stages,
			VkAccessFlags access, bool write)
		{
			for (std::size_t index = 0; index < vegetationFrameBuffers.size(); ++index)
			{
				const auto& frameBuffer = vegetationFrameBuffers[index];
				using Consumer = VansVegetationSystem::FrameBufferConsumer;
				const bool transferRead = frameBuffer.consumer == Consumer::TransferRead;
				const bool transferWrite = frameBuffer.consumer == Consumer::TransferWrite;
				if (transferRead || transferWrite)
				{
					if (write) node.resources.push_back({ "GrassStateTransfer" + std::to_string(index),
						VK_PIPELINE_STAGE_TRANSFER_BIT,
						static_cast<VkAccessFlags>(transferWrite ? VK_ACCESS_TRANSFER_WRITE_BIT : VK_ACCESS_TRANSFER_READ_BIT),
						VK_IMAGE_LAYOUT_UNDEFINED, false, transferWrite, true, false,
						VansPhysicalResourceIdentity::ForBuffer(frameBuffer.buffer) });
					continue;
				}
				if (!write && frameBuffer.consumer ==
					VansVegetationSystem::FrameBufferConsumer::ComputeOnly)
					continue;
				const bool readOnly = frameBuffer.consumer == Consumer::ReadOnlyDraw;
				node.resources.push_back({ "VegetationFrameBuffer" + std::to_string(index),
					stages, readOnly ? VK_ACCESS_SHADER_READ_BIT : access,
					VK_IMAGE_LAYOUT_UNDEFINED, false, write && !readOnly, true, false,
					VansPhysicalResourceIdentity::ForBuffer(frameBuffer.buffer) });
			}
		};
		const auto appendPhysicalImage = [](VansFrameSubmitNode& node,
			const char* name, VkImage image, VkPipelineStageFlags stages,
			VkAccessFlags access, bool write)
		{
			if (image != VK_NULL_HANDLE)
				node.resources.push_back({ name, stages, access,
					VK_IMAGE_LAYOUT_GENERAL, true, write, true, false,
					VansPhysicalResourceIdentity::ForImage(image) });
		};
		const auto appendSSRResources = [this, &appendPhysicalImage](
			VansFrameSubmitNode& node, VkPipelineStageFlags stages, VkAccessFlags access)
		{
			if (!m_Scene || !IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::SSR))
				return;
			auto* materials = m_Scene->GetMaterialManager();
			if (!materials)
				return;
			constexpr const char* names[] = {
				VansMaterialManager::RT_SSR_HIT_INFO,
				VansMaterialManager::RT_SSR_RAY_PDF,
				VansMaterialManager::RT_SSR_RESULT,
				VansMaterialManager::RT_SSRAA_RESULT_A,
				VansMaterialManager::RT_SSRAA_RESULT_B,
				VansMaterialManager::RT_SSRAA_RESULT
			};
			for (const char* name : names)
				if (auto* texture = materials->GetRuntimeRenderTexture(name))
					appendPhysicalImage(node, name, texture->GetImage().GetImage(), stages, access, true);
			const auto appendBuffer = [&](const char* name, const VansVKBuffer& buffer)
			{
				if (buffer.GetNativeBuffer() != VK_NULL_HANDLE)
					node.resources.push_back({ name, stages, access,
						VK_IMAGE_LAYOUT_UNDEFINED, false, true, true, false,
						VansPhysicalResourceIdentity::ForBuffer(buffer.GetNativeBuffer()) });
			};
			appendBuffer("SSRRayList", materials->m_SSRRayListBuffer);
			appendBuffer("SSRTraceControl", materials->m_SSRTraceControlBuffer);
		};
		const auto appendNearMediaWrites = [this, &appendPhysicalImage](
			VansFrameSubmitNode& node, VkPipelineStageFlags stages, VkAccessFlags access)
		{
			auto* media = m_Scene ? m_Scene->GetNearMediaSystem() : nullptr;
			if (media == nullptr)
				return;
			const auto images = media->GetResourceImages();
			appendPhysicalImage(node, "NearMediaRawInjection", images.rawInjection, stages, access, true);
			appendPhysicalImage(node, "NearMediaMaterialScattering", images.materialScatteringExtinction, stages, access, true);
			appendPhysicalImage(node, "NearMediaMaterialLighting", images.materialLightingPhaseCloud, stages, access, true);
			appendPhysicalImage(node, "NearMediaMaterialEmissive", images.materialEmissiveWeight, stages, access, true);
			appendPhysicalImage(node, "NearMediaParticleActivity0", images.volumetricParticleActivity[0], stages, access, true);
			appendPhysicalImage(node, "NearMediaParticleActivity1", images.volumetricParticleActivity[1], stages, access, true);
			appendPhysicalImage(node, "NearMediaInjection0", images.injection[0], stages, access, true);
			appendPhysicalImage(node, "NearMediaInjection1", images.injection[1], stages, access, true);
			appendPhysicalImage(node, "NearMediaScattering", images.scattering, stages, access, true);
			appendPhysicalImage(node, "NearMediaOpticalDepth", images.opticalDepth, stages, access, true);
		};
		const auto appendNearMediaReads = [this, &appendPhysicalImage](
			VansFrameSubmitNode& node, VkPipelineStageFlags stages, VkAccessFlags access)
		{
			auto* media = m_Scene ? m_Scene->GetNearMediaSystem() : nullptr;
			if (media == nullptr)
				return;
			const auto images = media->GetResourceImages();
			appendPhysicalImage(node, "NearMediaInjection0", images.injection[0], stages, access, false);
			appendPhysicalImage(node, "NearMediaInjection1", images.injection[1], stages, access, false);
			appendPhysicalImage(node, "NearMediaScattering", images.scattering, stages, access, false);
			appendPhysicalImage(node, "NearMediaOpticalDepth", images.opticalDepth, stages, access, false);
		};
		const auto appendCloudViewResources = [this, &appendPhysicalImage](
			VansFrameSubmitNode& node, VkPipelineStageFlags stages, VkAccessFlags access)
		{
			auto* clouds = m_Scene ? m_Scene->GetVolumetricCloudSystem() : nullptr;
			if (clouds == nullptr)
				return;
			const auto images = clouds->GetViewResourceImages();
			appendPhysicalImage(node, "CloudRadiancePhysical", images.radiance, stages, access, true);
			appendPhysicalImage(node, "CloudDepthPhysical", images.depth, stages, access, true);
			appendPhysicalImage(node, "CloudOpticalDepthPhysical", images.opticalDepth, stages, access, true);
		};
		const auto appendWaterResources = [this](VansFrameSubmitNode& node)
		{
			if (m_Scene == nullptr || IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
				return;
			auto* passes = VansRenderPassManager::GetInstance();
			const bool coverage = IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterGBuffer);
			const bool background = IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterSceneColorPyramidPrepare);
			const bool effects = IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterPreCompute);
			const bool composite = IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterCompositePreAtmosphere);
			const auto append = [&node](const char* name, VkImage image, bool write,
				VkImageLayout layout)
			{
				if (image == VK_NULL_HANDLE)
					return;
				const VkAccessFlags access = VK_ACCESS_MEMORY_READ_BIT |
					static_cast<VkAccessFlags>(write ? VK_ACCESS_MEMORY_WRITE_BIT : 0u);
				node.resources.push_back({ name, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
					access,
					layout, true, write, true, false,
					VansPhysicalResourceIdentity::ForImage(image) });
			};
			if (background)
				append("WaterBackgroundPyramid", passes->GetWaterBackgroundPyramid().GetImage(), true,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			auto* water = m_Scene->GetWaterSystem();
			if (water == nullptr || !water->IsInitialized())
				return;
			const auto images = water->GetResourceImages();
			if (coverage)
			{
				append("WaterWaveDisplacement", images.waveDisplacement, true, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterWaveDerivative", images.waveDerivative, true, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterFlowMap", images.flowMap, true, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterFFTSpectrum", images.fftSpectrum, true, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterFFTPing", images.fftPing, true, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterFFTPong", images.fftPong, true, VK_IMAGE_LAYOUT_GENERAL);
			}
			if (coverage || effects || composite)
			{
				append("WaterGBufNormal", passes->GetWaterGBufNormal().GetImage(), coverage,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				append("WaterGBufScatter", passes->GetWaterGBufScatter().GetImage(), coverage,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				append("WaterGBufAbsorption", passes->GetWaterGBufAbsorption().GetImage(), coverage,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				append("WaterGBufLinearDepth", passes->GetWaterGBufLinearDepth().GetImage(), coverage,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
			if (!background && (effects || composite))
				append("WaterBackgroundPyramid", passes->GetWaterBackgroundPyramid().GetImage(), false,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			if (effects || composite)
			{
				append("WaterReflection", images.reflection, effects, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterRefraction", images.refraction, effects, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterThickness", images.thickness, effects, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterVolumeRawColor", images.volumeRawColor, effects, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterVolumeRawTransmittance", images.volumeRawTransmittance, effects, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterVolumeRawDepth", images.volumeRawDepth, effects, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterVolumeColor", images.volumeColor, effects, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterVolumeTransmittance", images.volumeTransmittance, effects, VK_IMAGE_LAYOUT_GENERAL);
				append("WaterVolumeDepth", images.volumeDepth, effects, VK_IMAGE_LAYOUT_GENERAL);
			}
		};
		const auto appendOverlaySampledImages = [this](VansFrameSubmitNode& node)
		{
			for (std::size_t index = 0; index < m_OverlaySampledImages.size(); ++index)
			{
				const OverlaySampledImage& resource = m_OverlaySampledImages[index];
				node.resources.push_back({ "EditorOverlayImage." + std::to_string(index),
					resource.stages, resource.access,
					resource.layout, true, resource.write, true, false,
					VansPhysicalResourceIdentity::ForImage(resource.image) });
			}
		};
		const auto appendPostProcessWrites = [this, &appendPhysicalImage](VansFrameSubmitNode& node)
		{
			if (!m_Scene || !m_Scene->IsSceneReady())
				return;
			auto* materials = m_Scene->GetMaterialManager();
			if (materials == nullptr)
				return;
			const auto append = [&](const char* name)
			{
				if (auto* texture = materials->GetRuntimeRenderTexture(name))
					appendPhysicalImage(node, name, texture->GetImage().GetImage(),
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, true);
			};
			const bool isolatedDebug = IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot);
			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::DepthOfFieldPrepare) &&
				!isolatedDebug && m_CurrentRenderSceneSnapshot.postProcess.enableDepthOfField)
				append(VansMaterialManager::RT_DOF_RESULT);
			if (!IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::ExposureBloom))
				return;
			if (m_CurrentRenderSceneSnapshot.postProcess.enableAutoExposure)
				append(VansMaterialManager::RT_EXPOSURE_LUMINANCE);
			append(VansMaterialManager::RT_EXPOSURE_CURRENT);
			append(VansMaterialManager::RT_UPSCALER_EXPOSURE);
			if (isolatedDebug || !m_CurrentRenderSceneSnapshot.postProcess.enableBloom)
				return;
			constexpr const char* bloomImages[] = {
				VansMaterialManager::RT_BLOOM_PREFILTER,
				VansMaterialManager::RT_BLOOM_MIP0,
				VansMaterialManager::RT_BLOOM_MIP1,
				VansMaterialManager::RT_BLOOM_MIP2,
				VansMaterialManager::RT_BLOOM_MIP3,
				VansMaterialManager::RT_BLOOM_UP_MIP0,
				VansMaterialManager::RT_BLOOM_UP_MIP1,
				VansMaterialManager::RT_BLOOM_UP_MIP2,
				VansMaterialManager::RT_BLOOM_BASE,
				VansMaterialManager::RT_BLOOM_RESULT
			};
			for (const char* name : bloomImages)
				append(name);
		};
		const auto appendHairDebugWrite = [this, &appendPhysicalImage](VansFrameSubmitNode& node)
		{
			if (CurrentFrameContext().hairDebugPreviewWritten)
				appendPhysicalImage(node, "HairDebugPreview", m_HairDebugImage.GetImage(),
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, true);
		};
		const auto appendFSRMaskWrites = [this, &appendPhysicalImage](VansFrameSubmitNode& node)
		{
			if (CurrentFrameContext().fsrReactiveMaskWritten)
				appendPhysicalImage(node, "FSRReactiveMask", m_FSRController.GetReactiveMaskImage().GetImage(),
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, true);
			if (CurrentFrameContext().fsrTransparencyMaskWritten)
				appendPhysicalImage(node, "FSRTransparencyAndComposition",
					m_FSRController.GetTransparencyAndCompositionImage().GetImage(),
					VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, true);
		};
		if (!CurrentFrameContext().frameSubmitSucceeded)
		{
			ResolvePreparedFrameHistory(false);
			rayTracingContext.DiscardGIProbeUpdate();
			VANS_LOG_ERROR("[VansVKDevice] Skipping present because frame recording failed before submit.");
			return;
		}
		if (m_Scene->IsSceneReady())
		{
			VansRenderPassManager::GetInstance()->RecordFrameBufferImageLayoutReset(
				CurrentGraphicsCommandBuffer(), GetFrameResourceSlot(), GetCurrentGBufferTargetSlot());
		}

		{
			VANS_PROFILE_SCOPE("Vulkan::EndCommandBuffer", Vans::ProfileCategory::VulkanSubmit);
			if (!CurrentGraphicsCommandBuffer().EndCommandBufferRecord())
			{
				CurrentFrameContext().frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Failed to end main graphics command buffer.");
			}
		}

		// When no scene is loaded, always use the single-submit path because
		// the async-compute command buffer was never recorded/submitted.
		if (!m_AsyncComputeEnabled || !m_Scene->IsSceneReady())
		{
			{
				VANS_PROFILE_SCOPE("Vulkan::QueueSubmit.Graphics", Vans::ProfileCategory::VulkanSubmit);
				CurrentFrameSubmitGraph().Reset();
				VansFrameSubmitNode graphics;
				graphics.name = "GraphicsFrame";
				graphics.queue = VansQueueRole::Graphics;
				graphics.commandBuffers = { CurrentGraphicsCommandBuffer().GetVKCommandBuffer() };
				graphics.externalWaits = {
					{ CurrentFrameContext().imageAcquiredSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }
				};
				graphics.externalSignals = { { CurrentFrameContext().renderFinishedSemaphore, 0, false } };
				graphics.fence = CurrentFrameContext().graphicsFence;
				if (m_Scene->IsSceneReady())
				{
					auto* renderPasses = VansRenderPassManager::GetInstance();
					const uint32_t gbufferTargetSlot = GetCurrentGBufferTargetSlot();
					constexpr const char* names[] = {
						"GBufferNormal", "GBuffer0", "GBuffer1", "GBuffer2",
						"MotionVector", "Depth"
					};
					const std::array<VkImage, 6> images = {
						renderPasses->GetNormal(gbufferTargetSlot).GetImage(),
						renderPasses->GetGbuffer0(gbufferTargetSlot).GetImage(),
						renderPasses->GetGbuffer1(gbufferTargetSlot).GetImage(),
						renderPasses->GetGbuffer2(gbufferTargetSlot).GetImage(),
						renderPasses->GetMotionVector(gbufferTargetSlot).GetImage(),
						renderPasses->GetDepth(gbufferTargetSlot).GetImage()
					};
					for (std::size_t index = 0; index < images.size(); ++index)
						graphics.resources.push_back({ names[index], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
							VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
							index + 1 == images.size() ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
								: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							true, true, false, false,
							VansPhysicalResourceIdentity::ForImage(images[index]) });
					graphics.resources.push_back({ "DiffuseExitantRadianceHistory",
						VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false,
						VansPhysicalResourceIdentity::ForImage(
							renderPasses->GetDiffuseExitantRadianceHistory().GetImage()) });
					graphics.resources.push_back({ "SceneColor",
						VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false,
						VansPhysicalResourceIdentity::ForImage(renderPasses->GetColor().GetImage()) });
					graphics.resources.push_back({ "UpscalerOutput",
						VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false,
						VansPhysicalResourceIdentity::ForImage(m_UpscalerOutputImage.GetImage()) });
					graphics.resources.push_back({ "FinalDisplayColor",
						VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false,
						VansPhysicalResourceIdentity::ForImage(
							renderPasses->GetFinalDisplayColor().GetImage()) });
					if (auto* rain = m_Scene->GetRainRenderSystem())
					{
						if (auto* ripple = rain->GetSurfaceRippleImage())
							graphics.resources.push_back({ "SurfaceWeatherRippleNormal",
								VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
								VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
								VK_IMAGE_LAYOUT_GENERAL, true, true, true, false,
								VansPhysicalResourceIdentity::ForImage(ripple->GetImage()) });
					}
					if (auto* materials = m_Scene->GetMaterialManager())
					{
						if (!materials->m_GIReceiverVisibility.enabled)
							graphics.resources.push_back({ "GIReceiverVisibilityCurrent",
								VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT,
								VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false,
								VansPhysicalResourceIdentity::ForBuffer(
									materials->m_GIReceiverVisibility.current.GetNativeBuffer()) });
						constexpr const char* runtimeNames[] = {
							VansMaterialManager::RT_HZB_RESULT,
							VansMaterialManager::RT_HZB_OCCLUSION_RESULT,
							VansMaterialManager::RT_GTAO_DEPTH,
							VansMaterialManager::RT_GTAO_EDGES,
							VansMaterialManager::RT_GTAO_RAW,
							VansMaterialManager::RT_GTAO_RESULT,
							VansMaterialManager::RT_CASCADE_SHADOW_MIN_MAX,
							VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT,
							VansMaterialManager::RT_AMBIENT_SKY_CACHE_X,
							VansMaterialManager::RT_AMBIENT_SKY_CACHE_Y,
							VansMaterialManager::RT_AMBIENT_SKY_CACHE_Z
						};
						constexpr const char* resourceNames[] = {
							"HZB", "OcclusionHZB", "GTAODepth",
							"GTAOEdges", "GTAORaw", "GTAO",
							"CascadeShadowMinMax", "ScreenSpaceShadow",
							"AmbientSkyCacheX", "AmbientSkyCacheY", "AmbientSkyCacheZ"
						};
						for (std::size_t index = 0; index < std::size(runtimeNames); ++index)
						{
							if (auto* texture = materials->GetRuntimeRenderTexture(runtimeNames[index]))
								graphics.resources.push_back({ resourceNames[index], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
									VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
									VK_IMAGE_LAYOUT_GENERAL, true, true, false, false,
									VansPhysicalResourceIdentity::ForImage(texture->GetImage().GetImage()) });
						}
						if (auto* preview = materials->GetRuntimeRenderTexture(
							VansMaterialManager::RT_PUNCTUAL_SHADOW_DEBUG_PREVIEW))
							appendPhysicalImage(graphics, "PunctualShadowDebugPreview",
								preview->GetImage().GetImage(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
								VK_ACCESS_SHADER_READ_BIT |
									(CurrentFrameContext().punctualShadowDebugPreviewWritten
										? VK_ACCESS_SHADER_WRITE_BIT : 0u),
								CurrentFrameContext().punctualShadowDebugPreviewWritten);
						AppendSSGIFrameResources(graphics, materials, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
						appendSSRResources(graphics, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
							VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
						appendTileLightBuffers(graphics, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
							VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, true);
						appendVegetationBuffers(graphics, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
							VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, true);
						appendNearMediaWrites(graphics, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
							VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
						appendCloudViewResources(graphics, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
							VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
						appendGIReceiverBuffers(graphics, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
							VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
					}
					appendWaterResources(graphics);
				appendPostProcessWrites(graphics);
				appendHairDebugWrite(graphics);
				appendFSRMaskWrites(graphics);
				appendOverlaySampledImages(graphics);
					appendGIRegionResources(graphics, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, true);
				}
				CurrentFrameSlot().submittedWithAsyncCompute = false;
				CurrentFrameSubmitGraph().AddNode(std::move(graphics));
				CurrentFrameSubmitGraph().AddExternalPrerequisite(CurrentFrameSlot().projectPreludeCompletion);
                CurrentFrameSubmitGraph().AddQueueReadResources(VansQueueRole::Graphics,m_ProjectRenderPassRuntime.ExportedReadResources());
                CurrentFrameSubmitGraph().AddQueueReadResources(VansQueueRole::Graphics,m_Scene->TerrainInteractionReadResources());
				CurrentFrameContext().frameSubmitSucceeded = CurrentFrameSubmitGraph().Execute();
				if (CurrentFrameContext().frameSubmitSucceeded)
					m_SwapchainAcquireState = SwapchainAcquireState::GpuWaitSubmitted;
				m_DeviceLost = m_DeviceLost || CurrentFrameSubmitGraph().WasDeviceLost();
				ResolvePreparedFrameHistory(CurrentFrameContext().frameSubmitSucceeded, true);
				if (CurrentFrameContext().frameSubmitSucceeded)
                {
					NotifyPunctualShadowJobsSubmitted();
                }
                else rayTracingContext.DiscardGIProbeUpdate();
				if (!CurrentFrameContext().frameSubmitSucceeded)
				{
					VANS_LOG_ERROR("[VansVKDevice] Graphics frame submit failed: "
						<< CurrentFrameSubmitGraph().GetLastError());
				}
			}
			{
				VansFrameContextRingSlot& slot = CurrentFrameSlot();
				slot.gpuWorkPending = CurrentFrameContext().frameSubmitSucceeded;
				if (slot.gpuWorkPending)
				{
					slot.profilerFrameIndex = Vans::VansGpuProfiler::Get().GetRecordingFrameIndex();
					RecordSubmittedFrameDepth();
				}
			}
			if (m_ActiveFrameContextSlot != nullptr)
			{
				if (!CurrentFrameContext().frameSubmitSucceeded)
				{
					CurrentGraphicsCommandBuffer().ResetCommandBuffer(false);
				}
			}

		}
		else
		{
			if (!CurrentFrameContext().frameSubmitSucceeded
				|| !CurrentFrameContext().gtaoRawRecorded
				|| !CurrentFrameContext().graphicsScreenRecorded
				|| !CurrentFrameContext().shadowMapsRecorded
				|| !CurrentFrameContext().gbufferRecorded
				|| !CurrentFrameContext().gbufferMaterialRecorded
				|| !CurrentFrameContext().vegetationRecorded
				|| !CurrentFrameContext().earlyAuxRecorded
				|| !CurrentFrameContext().asyncAtmosphereRecorded
				|| !CurrentFrameContext().asyncHZBRecorded
				|| !CurrentFrameContext().rayTracingRecorded
				|| !CurrentFrameContext().giDataRecorded)
			{
				VANS_LOG_ERROR("[VansVKDevice] Skipping async frame submit because a prerequisite command buffer was not recorded.");
				ResolvePreparedFrameHistory(false);
				ResetAsyncFrameCommandBuffersAfterFailure();
				return;
			}

			{
				VANS_PROFILE_SCOPE("Vulkan::QueueSubmit.AsyncFrameGraph", Vans::ProfileCategory::VulkanSubmit);
				CurrentFrameSubmitGraph().Reset();
				auto* renderPasses = VansRenderPassManager::GetInstance();
				const auto cascadeShadowImage = VansPhysicalResourceIdentity::ForImage(
					renderPasses->GetShadowMap().GetImage());
				const auto punctualShadowImage0 = VansPhysicalResourceIdentity::ForImage(
					renderPasses->GetPunctualShadowAtlasImage(0));
				const auto punctualShadowImage1 = VansPhysicalResourceIdentity::ForImage(
					renderPasses->GetPunctualShadowAtlasImage(1));
				const auto cloudShadowImage = VansPhysicalResourceIdentity::ForImage(
					m_Scene->GetVolumetricCloudSystem()
						? m_Scene->GetVolumetricCloudSystem()->GetShadowImage() : VK_NULL_HANDLE);
				const auto atmosphereImages = m_Scene->GetAtmosphereSystem()
					? m_Scene->GetAtmosphereSystem()->GetResourceImages()
					: VansAtmosphereSystem::ResourceImages{};
				const auto transmittanceImage = VansPhysicalResourceIdentity::ForImage(atmosphereImages.transmittance);
				const auto multiScatteringImage = VansPhysicalResourceIdentity::ForImage(atmosphereImages.multiScattering);
				const auto skyViewImage = VansPhysicalResourceIdentity::ForImage(atmosphereImages.skyView);
				const auto aerialScatteringImage = VansPhysicalResourceIdentity::ForImage(atmosphereImages.aerialScattering);
				const auto aerialClearScatteringImage = VansPhysicalResourceIdentity::ForImage(atmosphereImages.aerialClearScattering);
				const auto aerialOpticalDepthImage = VansPhysicalResourceIdentity::ForImage(atmosphereImages.aerialOpticalDepth);
				VkImage surfaceRippleHandle = VK_NULL_HANDLE;
				if (auto* rain = m_Scene->GetRainRenderSystem())
				{
				if (auto* ripple = rain->GetSurfaceRippleImage())
					surfaceRippleHandle = ripple->GetImage();
				}
				const auto surfaceRippleImage = VansPhysicalResourceIdentity::ForImage(surfaceRippleHandle);
				const std::array<VansPhysicalResourceIdentity, 4> gbufferImages = {
					VansPhysicalResourceIdentity::ForImage(renderPasses->GetNormal(GetCurrentGBufferTargetSlot()).GetImage()),
					VansPhysicalResourceIdentity::ForImage(renderPasses->GetGbuffer0(GetCurrentGBufferTargetSlot()).GetImage()),
					VansPhysicalResourceIdentity::ForImage(renderPasses->GetGbuffer1(GetCurrentGBufferTargetSlot()).GetImage()),
					VansPhysicalResourceIdentity::ForImage(renderPasses->GetGbuffer2(GetCurrentGBufferTargetSlot()).GetImage())
				};
				const auto motionVectorImage = VansPhysicalResourceIdentity::ForImage(
				renderPasses->GetMotionVector(GetCurrentGBufferTargetSlot()).GetImage());
				const auto depthImage = VansPhysicalResourceIdentity::ForImage(
				renderPasses->GetDepth(GetCurrentGBufferTargetSlot()).GetImage());
				const auto receiverCurrentBuffer = VansPhysicalResourceIdentity::ForBuffer(
					m_Scene->GetMaterialManager()->m_GIReceiverVisibility.current.GetNativeBuffer());
			const auto diffuseExitantRadianceHistoryImage = VansPhysicalResourceIdentity::ForImage(
				renderPasses->GetDiffuseExitantRadianceHistory().GetImage());
				const auto runtimeImage = [materials = m_Scene->GetMaterialManager()](const char* name)
				{
					auto* texture = materials ? materials->GetRuntimeRenderTexture(name) : nullptr;
					return VansPhysicalResourceIdentity::ForImage(
						texture ? texture->GetImage().GetImage() : VK_NULL_HANDLE);
				};
				const auto hzbImage = runtimeImage(VansMaterialManager::RT_HZB_RESULT);
				const auto occlusionHZBImage = runtimeImage(VansMaterialManager::RT_HZB_OCCLUSION_RESULT);
				const auto gtaoDepthImage = runtimeImage(VansMaterialManager::RT_GTAO_DEPTH);
				const auto gtaoEdgesImage = runtimeImage(VansMaterialManager::RT_GTAO_EDGES);
				const auto gtaoRawImage = runtimeImage(VansMaterialManager::RT_GTAO_RAW);
				const auto gtaoResultImage = runtimeImage(VansMaterialManager::RT_GTAO_RESULT);
				const auto cascadeShadowMinMaxImage = runtimeImage(VansMaterialManager::RT_CASCADE_SHADOW_MIN_MAX);
				const auto screenSpaceShadowImage = runtimeImage(VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT);
				const auto appendGBufferImages = [&gbufferImages](VansFrameSubmitNode& node,
					VkPipelineStageFlags stages, VkAccessFlags access, bool write)
				{
					constexpr const char* names[] = {
						"GBufferNormal", "GBuffer0", "GBuffer1", "GBuffer2"
					};
					for (std::size_t index = 0; index < gbufferImages.size(); ++index)
						node.resources.push_back({ names[index], stages, access,
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, write,
							false, false, gbufferImages[index] });
				};

				VansFrameSubmitNode vegetation;
				vegetation.name = "Vegetation";
				vegetation.queue = VansQueueRole::Compute;
				vegetation.commandBuffers = { CurrentAsyncFrameCommands().vegetation.GetVKCommandBuffer() };
				vegetation.signals = { VansSyncPoint::VegetationReady };
				vegetation.resources = {
					{ "VegetationDrawData", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, true, true, false },
					// GrassCull / TreeCull sample the previous frame's occlusion pyramid.
					// Register that read instead of relying on compute-queue submission order.
					{ "OcclusionHZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, occlusionHZBImage }
				};
				appendVegetationBuffers(vegetation, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
					VK_ACCESS_TRANSFER_WRITE_BIT, true);
				vegetation.fence = CurrentFrameContext().vegetationFence;
				CurrentFrameSubmitGraph().AddNode(std::move(vegetation));

				VansFrameSubmitNode earlyAux;
				earlyAux.name = "EarlyAux";
				earlyAux.queue = VansQueueRole::Compute;
				earlyAux.commandBuffers = { CurrentAsyncFrameCommands().earlyAux.GetVKCommandBuffer() };
				earlyAux.resources = {
					{ "MainCameraCullObjects", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false },
					{ "MainCameraVisibility", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, true, true, true },
					{ "OcclusionHZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, occlusionHZBImage },
					{ "TileLightLists", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, true, false, false }
				};
				appendTileLightBuffers(earlyAux, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_WRITE_BIT, true);
				earlyAux.fence = CurrentFrameContext().earlyAuxFence;
				CurrentFrameSubmitGraph().AddNode(std::move(earlyAux));

				VansFrameSubmitNode asyncAtmosphere;
				asyncAtmosphere.name = "AsyncAtmosphere";
				asyncAtmosphere.queue = VansQueueRole::Compute;
				asyncAtmosphere.commandBuffers = { CurrentAsyncFrameCommands().asyncAtmosphere.GetVKCommandBuffer() };
				const bool staticLutsWritten = CurrentFrameContext().atmosphereStaticLutsWritten;
				const VkAccessFlags staticLutAccess = staticLutsWritten
					? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
					: VK_ACCESS_SHADER_READ_BIT;
				asyncAtmosphere.resources = {
					{ "AtmosphereTransmittance", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						staticLutAccess,
						VK_IMAGE_LAYOUT_GENERAL, true, staticLutsWritten, true, false, transmittanceImage },
					{ "AtmosphereMultiScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						staticLutAccess,
						VK_IMAGE_LAYOUT_GENERAL, true, staticLutsWritten, true, false, multiScatteringImage },
					{ "AtmosphereSkyView", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false, skyViewImage },
					{ "AtmosphereAerialScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false, aerialScatteringImage },
					{ "AtmosphereAerialClearScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false, aerialClearScatteringImage },
					{ "AtmosphereAerialOpticalDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false, aerialOpticalDepthImage },
					{ "CloudShadow", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false, cloudShadowImage }
				};
				asyncAtmosphere.fence = CurrentFrameContext().asyncAtmosphereFence;
				CurrentFrameSubmitGraph().AddNode(std::move(asyncAtmosphere));

				VansFrameSubmitNode shadowMaps;
				shadowMaps.name = "ShadowMaps";
				shadowMaps.queue = VansQueueRole::Graphics;
				shadowMaps.commandBuffers = { CurrentAsyncFrameCommands().shadowMaps.GetVKCommandBuffer() };
				shadowMaps.waits = {
					{ VansSyncPoint::VegetationReady, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT }
				};
				shadowMaps.signals = { VansSyncPoint::ShadowMapsReady };
				shadowMaps.resources = {
					{ "VegetationDrawData", VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
						VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false },
					{ "CascadeShadowMap", VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, true, false, cascadeShadowImage },
					{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, true, false, punctualShadowImage0 },
					{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, true, false, punctualShadowImage1 }
				};
				appendVegetationBuffers(shadowMaps,
					VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
					VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT, false);
				shadowMaps.fence = CurrentFrameContext().shadowMapsFence;
				CurrentFrameSubmitGraph().AddNode(std::move(shadowMaps));


				VansFrameSubmitNode gbuffer;
				gbuffer.name = "GBuffer";
				gbuffer.queue = VansQueueRole::Graphics;
				gbuffer.commandBuffers = { CurrentAsyncFrameCommands().gbuffer.GetVKCommandBuffer() };
				gbuffer.waits = {
					{ VansSyncPoint::VegetationReady, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT }
				};
				gbuffer.signals = { VansSyncPoint::DepthReady };
				gbuffer.resources = {
					{ "VegetationDrawData", VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
						VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false },
					{ "MotionVector", VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false, motionVectorImage },
					{ "Depth", VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, false, false, depthImage },
					{ "SurfaceWeatherRippleNormal",
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false, surfaceRippleImage }
				};
				appendVegetationBuffers(gbuffer,
					VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
					VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT, false);
				appendGBufferImages(gbuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, true);
				gbuffer.fence = CurrentFrameContext().gbufferFence;
				CurrentFrameSubmitGraph().AddNode(std::move(gbuffer));

				VansFrameSubmitNode gbufferMaterial;
				gbufferMaterial.name = "GBufferMaterial";
				gbufferMaterial.queue = VansQueueRole::Graphics;
				gbufferMaterial.commandBuffers = { CurrentAsyncFrameCommands().gbufferMaterial.GetVKCommandBuffer() };
				gbufferMaterial.signals = { VansSyncPoint::GBufferMaterialReady };
				gbufferMaterial.resources = {
					{ "MotionVector", VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false, motionVectorImage },
					{ "Depth", VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, false, false, depthImage },
					{ "SurfaceWeatherRippleNormal", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
						true, false, true, false, surfaceRippleImage }
				};
				appendGBufferImages(gbufferMaterial, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, true);
				gbufferMaterial.fence = CurrentFrameContext().gbufferMaterialFence;
				CurrentFrameSubmitGraph().AddNode(std::move(gbufferMaterial));

				VansFrameSubmitNode gtaoRaw;
				gtaoRaw.name = "GTAORaw";
				gtaoRaw.queue = VansQueueRole::Graphics;
				gtaoRaw.commandBuffers = { CurrentAsyncFrameCommands().gtaoRaw.GetVKCommandBuffer() };
				gtaoRaw.signals = { VansSyncPoint::GTAORawReady };
				gtaoRaw.resources = {
					{ "GTAODepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false, gtaoDepthImage },
					{ "GTAOEdges", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_GENERAL, true, true, false, false, gtaoEdgesImage },
                    { "GTAORaw", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false, gtaoRawImage }
				};
				appendGBufferImages(gtaoRaw, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT, false);
				gtaoRaw.fence = CurrentFrameContext().gtaoRawFence;
				CurrentFrameSubmitGraph().AddNode(std::move(gtaoRaw));

				VansFrameSubmitNode asyncHZB;
				asyncHZB.name = "AsyncHZB";
				asyncHZB.queue = VansQueueRole::Compute;
				asyncHZB.commandBuffers = { CurrentAsyncFrameCommands().asyncHZB.GetVKCommandBuffer() };
				asyncHZB.waits = {
					{ VansSyncPoint::DepthReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT }
				};
				asyncHZB.resources = {
					{ "Depth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, false, false, depthImage },
					{ "HZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false, hzbImage },
					{ "OcclusionHZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false, occlusionHZBImage }
				};
				asyncHZB.fence = CurrentFrameContext().asyncHZBFence;
				CurrentFrameSubmitGraph().AddNode(std::move(asyncHZB));

				VansFrameSubmitNode rayTracing;
				rayTracing.name = "RayTracing";
				rayTracing.queue = VansQueueRole::Compute;
				rayTracing.commandBuffers = { CurrentAsyncFrameCommands().rayTracing.GetVKCommandBuffer() };
				rayTracing.waits = {
					{ VansSyncPoint::GBufferMaterialReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR }
				};
				rayTracing.resources = {
					{ "CascadeShadowMap", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
						VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, cascadeShadowImage },
					{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
						VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, punctualShadowImage0 },
					{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
						VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, punctualShadowImage1 },
					{ "RayTracingGI", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
						VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, true, true, true, false }
				};
				appendGBufferImages(rayTracing,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
					VK_ACCESS_SHADER_READ_BIT, false);
				appendGIRegionResources(rayTracing,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
						VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
					true);
				rayTracing.fence = CurrentFrameContext().rayTracingFence;
				CurrentFrameSubmitGraph().AddNode(std::move(rayTracing));

				VansFrameSubmitNode giData;
				giData.name = "GIData";
				giData.queue = VansQueueRole::Compute;
				giData.commandBuffers = { CurrentAsyncFrameCommands().giData.GetVKCommandBuffer() };
				giData.waits = {
					{ VansSyncPoint::ShadowMapsReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT }
				};
				giData.resources = {
					{ "CascadeShadowMap", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, cascadeShadowImage },
					{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, punctualShadowImage0 },
					{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, punctualShadowImage1 },
					{ "HZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, hzbImage },
					{ "RayTracingGI", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "PreConvolvedDiffuseEnvironment", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "GIData", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false }
					,{ "AmbientSkyCacheX", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false,
						runtimeImage(VansMaterialManager::RT_AMBIENT_SKY_CACHE_X) }
					,{ "AmbientSkyCacheY", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false,
						runtimeImage(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Y) }
					,{ "AmbientSkyCacheZ", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false,
						runtimeImage(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Z) }
				};
				appendGBufferImages(giData, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT, false);
			giData.resources.push_back({ "DiffuseExitantRadianceHistory",
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, true, false,
				diffuseExitantRadianceHistoryImage });
				AppendSSGIFrameResources(giData, m_Scene->GetMaterialManager(),
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
				appendGIRegionResources(giData,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
						VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, false);
				appendGIReceiverBuffers(giData,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
						VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
						VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
				giData.fence = CurrentFrameContext().giDataFence;
				CurrentFrameSubmitGraph().AddNode(std::move(giData));

				VansFrameSubmitNode graphicsScreen;
				graphicsScreen.name = "GraphicsScreen";
				graphicsScreen.queue = VansQueueRole::Compute;
				graphicsScreen.commandBuffers = { CurrentAsyncFrameCommands().graphicsScreen.GetVKCommandBuffer() };
				// HZB/RT/GI/TileLight 已在同一 compute queue 上有序完成；这里只等待
				// Graphics queue 生产的 GTAO raw，避免让重型 compute 相互竞争。
				graphicsScreen.waits = {
					{ VansSyncPoint::GTAORawReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT }
				};
				graphicsScreen.signals = { VansSyncPoint::ScreenLightingReady };
				graphicsScreen.resources = {
					{ "Depth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, false, false, depthImage },
					{ "CascadeShadowMap", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, cascadeShadowImage },
					{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, punctualShadowImage0 },
					{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, punctualShadowImage1 },
					{ "HZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, hzbImage },
					{ "TileLightLists", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, false, false },
					{ "CascadeShadowMinMax", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false, cascadeShadowMinMaxImage },
					{ "ScreenSpaceShadow", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false, screenSpaceShadowImage },
					{ "ScreenLightingData", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false },
					{ "GTAOEdges", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL, true, false, false, false, gtaoEdgesImage },
                    { "GTAORaw", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, false, false, gtaoRawImage },
					{ "GTAO", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false, gtaoResultImage },
					{ "AtmosphereTransmittance", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, transmittanceImage },
					{ "AtmosphereMultiScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, multiScatteringImage },
					{ "AtmosphereSkyView", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, skyViewImage },
					{ "PreConvolvedDiffuseEnvironment", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "CloudShadow", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, cloudShadowImage },
					{ "LocalMediaInjection", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "LocalMediaScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "LocalMediaOpticalDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false }
				};
				if (CurrentFrameContext().punctualShadowDebugPreviewWritten)
				{
					if (auto* materials = m_Scene->GetMaterialManager())
						if (auto* preview = materials->GetRuntimeRenderTexture(
							VansMaterialManager::RT_PUNCTUAL_SHADOW_DEBUG_PREVIEW))
							appendPhysicalImage(graphicsScreen, "PunctualShadowDebugPreview",
								preview->GetImage().GetImage(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
								VK_ACCESS_SHADER_WRITE_BIT, true);
				}
				appendTileLightBuffers(graphicsScreen, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT, false);
				appendNearMediaWrites(graphicsScreen, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
				appendGBufferImages(graphicsScreen, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT, false);
				appendSSRResources(graphicsScreen,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
						VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
						VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
				graphicsScreen.fence = CurrentFrameContext().graphicsScreenFence;
				CurrentFrameSubmitGraph().AddNode(std::move(graphicsScreen));

				VansFrameSubmitNode graphicsMain;
				graphicsMain.name = "GraphicsMain";
				graphicsMain.queue = VansQueueRole::Graphics;
				graphicsMain.commandBuffers = { CurrentGraphicsCommandBuffer().GetVKCommandBuffer() };
				graphicsMain.waits = {
					{ VansSyncPoint::ScreenLightingReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT },
				};
				graphicsMain.externalWaits = {
					{ CurrentFrameContext().imageAcquiredSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }
				};
				graphicsMain.externalSignals = { { CurrentFrameContext().renderFinishedSemaphore, 0, false } };
				graphicsMain.resources = {
					{ "Depth", VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
						VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
						true, true, false, false, depthImage },
					{ "MotionVector", VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
						VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
						true, false, false, false, motionVectorImage },
					{ "SurfaceWeatherRippleNormal",
						VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
						true, false, true, false, surfaceRippleImage },
					{ "CascadeShadowMap", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, cascadeShadowImage },
					{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, punctualShadowImage0 },
					{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false, punctualShadowImage1 },
					{ "TileLightLists", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, false, false },
					{ "AtmosphereTransmittance", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, transmittanceImage },
					{ "AtmosphereMultiScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, multiScatteringImage },
					{ "AtmosphereSkyView", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, skyViewImage },
					{ "AtmosphereAerialScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, aerialScatteringImage },
					{ "AtmosphereAerialClearScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, aerialClearScatteringImage },
					{ "AtmosphereAerialOpticalDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, aerialOpticalDepthImage },
					{ "PreConvolvedDiffuseEnvironment", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "PreConvolvedSpecularEnvironment", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "SkySHCoefficients", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false },
					{ "CloudRadiance", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "CloudDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "CloudOpticalDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "CloudShadow", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false, cloudShadowImage },
					{ "LocalMediaInjection", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "LocalMediaScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "LocalMediaOpticalDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "GIData", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
                    { "RayTracingGI", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "AmbientSkyCacheX", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false,
						runtimeImage(VansMaterialManager::RT_AMBIENT_SKY_CACHE_X) },
					{ "AmbientSkyCacheY", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false,
						runtimeImage(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Y) },
					{ "AmbientSkyCacheZ", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false,
						runtimeImage(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Z) },
					{ "GTAO", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, false, false, gtaoResultImage },
					{ "ScreenLightingData", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, false, false },
					{ "ScreenSpaceShadow", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, false, false, screenSpaceShadowImage },
					{ "SceneColor", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, true, false,
						VansPhysicalResourceIdentity::ForImage(renderPasses->GetColor().GetImage()) }
				};
				appendGBufferImages(graphicsMain, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT, false);
			graphicsMain.resources.push_back({ "DiffuseExitantRadianceHistory",
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, true, false,
				diffuseExitantRadianceHistoryImage });
				graphicsMain.resources.push_back({ "SSGIFilterResult",
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_GENERAL, true, false, true, false,
				runtimeImage(VansMaterialManager::RT_SSGI_FILTER_RESULT) });
				graphicsMain.resources.push_back({ "SSRAAResult",
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_LAYOUT_GENERAL, true, false, true, false,
					runtimeImage(VansMaterialManager::RT_SSRAA_RESULT) });
			graphicsMain.resources.push_back({ "GIReceiverVisibilityCurrent",
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false,
				receiverCurrentBuffer });
				appendTileLightBuffers(graphicsMain, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT, false);
				appendNearMediaReads(graphicsMain,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT);
				appendCloudViewResources(graphicsMain,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
			appendWaterResources(graphicsMain);
			appendPostProcessWrites(graphicsMain);
			appendHairDebugWrite(graphicsMain);
			appendFSRMaskWrites(graphicsMain);
			appendOverlaySampledImages(graphicsMain);
			if (auto* materials = m_Scene->GetMaterialManager())
				if (auto* preview = materials->GetRuntimeRenderTexture(
					VansMaterialManager::RT_PUNCTUAL_SHADOW_DEBUG_PREVIEW))
					appendPhysicalImage(graphicsMain, "PunctualShadowDebugPreview",
						preview->GetImage().GetImage(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						VK_ACCESS_SHADER_READ_BIT, false);
			appendGIRegionResources(graphicsMain, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_SHADER_READ_BIT, false);
			graphicsMain.resources.push_back({ "UpscalerOutput",
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false,
				VansPhysicalResourceIdentity::ForImage(m_UpscalerOutputImage.GetImage()) });
			graphicsMain.resources.push_back({ "FinalDisplayColor",
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false,
				VansPhysicalResourceIdentity::ForImage(
					renderPasses->GetFinalDisplayColor().GetImage()) });
				graphicsMain.fence = CurrentFrameContext().graphicsFence;
				CurrentFrameSlot().submittedWithAsyncCompute = true;
				CurrentFrameSubmitGraph().AddNode(std::move(graphicsMain));

				CurrentFrameSubmitGraph().AddExternalPrerequisite(CurrentFrameSlot().projectPreludeCompletion);
                CurrentFrameSubmitGraph().AddQueueReadResources(VansQueueRole::Graphics,m_ProjectRenderPassRuntime.ExportedReadResources());
                CurrentFrameSubmitGraph().AddQueueReadResources(VansQueueRole::Graphics,m_Scene->TerrainInteractionReadResources());
				CurrentFrameContext().frameSubmitSucceeded = CurrentFrameSubmitGraph().Execute();
				if (CurrentFrameContext().frameSubmitSucceeded)
					m_SwapchainAcquireState = SwapchainAcquireState::GpuWaitSubmitted;
				m_DeviceLost = m_DeviceLost || CurrentFrameSubmitGraph().WasDeviceLost();
				ResolvePreparedFrameHistory(CurrentFrameContext().frameSubmitSucceeded, true);
				VansFrameContextRingSlot& slot = CurrentFrameSlot();
				slot.gpuWorkPending = CurrentFrameContext().frameSubmitSucceeded;
				if (slot.gpuWorkPending)
				{
					slot.profilerFrameIndex = Vans::VansGpuProfiler::Get().GetRecordingFrameIndex();
					RecordSubmittedFrameDepth();
				}
				if (CurrentFrameContext().frameSubmitSucceeded)
                {
					NotifyPunctualShadowJobsSubmitted();
                }
                else rayTracingContext.DiscardGIProbeUpdate();
				if (!CurrentFrameContext().frameSubmitSucceeded)
				{
					VANS_LOG_ERROR("[VansVKDevice] Async frame graph submit failed: " << CurrentFrameSubmitGraph().GetLastError());
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}

		}

		if (!CurrentFrameContext().frameSubmitSucceeded)
		{
			VANS_LOG_ERROR("[VansVKDevice] Skipping present because frame submit/reset failed.");
			return;
		}
		if (CurrentFrameContext().atmosphereStaticLutsWritten && m_Scene)
		{
			if (auto* atmosphere = m_Scene->GetAtmosphereSystem())
				atmosphere->CommitStaticLutUpdate();
		}
		if (m_ActiveFrameContextSlot != nullptr)
		{
			if (m_SwapChainImageIndex < m_SwapchainImageInFlightFences.size())
				m_SwapchainImageInFlightFences[m_SwapChainImageIndex] = CurrentFrameContext().graphicsFence;
		}

		// Probe capture samples the directional shadow map. Run it only after this
		// frame's graphics work has completed, while the map and its matching light
		// matrices are still in SHADER_READ_ONLY_OPTIMAL.
		if (m_Scene->IsSceneReady()
			&& IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::ReflectionProbeBakeQueue))
		{
			static uint32_t reflectionProbeFrame = 0;
			auto* reflectionProbes = m_Scene->GetReflectionProbeSystem();
			// 每帧只更新一次实时队列；没有实际捕获任务时不串行等待整帧 GPU。
			reflectionProbes->UpdateRealtimeProbes(reflectionProbeFrame++);
			if (reflectionProbes->HasCaptureWork())
			{
				if (!RetireSubmittedFrame())
				{
					CurrentFrameContext().frameSubmitSucceeded = false;
					return;
				}
				const uint32_t faceBudget = reflectionProbes->GetBakeFaceBudget();
				for (uint32_t face = 0; face < faceBudget; ++face)
					reflectionProbes->ProcessBakeQueue(*m_Scene, *this, m_ImmediateGraphicsCommandBuffer);
			}
		}

		{
			VANS_PROFILE_SCOPE("Vulkan::PresentImage", Vans::ProfileCategory::VulkanSubmit);
			const VkResult presentResult = m_VansVKSurface.PresentImage(
				m_VansVKGraphicsQueue,
				{ CurrentFrameContext().renderFinishedSemaphore },
				m_SwapChainImageIndex);
			if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR)
				m_SwapchainAcquireState = SwapchainAcquireState::None;
			if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
			{
				m_DeviceLost = m_DeviceLost || presentResult == VK_ERROR_DEVICE_LOST;
				CurrentFrameContext().frameSubmitSucceeded = false;
				CurrentFrameContext().surfaceResizeRequired = presentResult == VK_ERROR_OUT_OF_DATE_KHR;
				if (CurrentFrameContext().surfaceResizeRequired)
					VANS_LOG("[VansVKDevice] Swapchain presentation is out of date; requesting surface resize.");
				else
					VANS_LOG_ERROR("[VansVKDevice] Queue present failed. VkResult=" << static_cast<int>(presentResult));
			}
		}

		m_CurrentFrameContextSlotIndex =
			(m_CurrentFrameContextSlotIndex + 1) % m_ConfiguredFramesInFlight;
	}

	void VansVKDevice::AfterRendering()
	{
		m_pActiveCommandBuffer = &m_MaintenanceGraphicsCommandBuffer;
		WaitForDevice();
		// Noesis IRenderer instances are RT-affine and must be shut down before
		// their render passes and Vulkan device resources are destroyed.
		VansRuntime::VansUISystem::Get().ShutdownRendering();
		DestroyParallelCommandRecording();
		DestroyHairLightingDescriptors();
		DestroyHairCompositeDescriptors();
		DestroyTransmissionGlassDescriptors();
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->DestroyRenderPass();
		// Upscaler contexts own backend GPU resources and must be released on the
		// render thread after GPU quiescence, before bootstrap destroys Vulkan.
		CleanupUpscalerResources();
		m_DrawInstanceArena.Destroy(m_VansVKLogicDevice);
		m_ProjectRenderPassRuntime.ShutdownAtIdle();
		m_RenderTextureCatalog.Clear();
		m_MainCameraVisibilityState.ReleaseGpuResources(m_VansVKLogicDevice);
		DestroyLightFrameResources();
		DestroyCameraFrameResources();

		for (VkSemaphore& semaphore : m_SwapchainImageRenderFinishedSemaphores)
			DestroyVKSemaphore(semaphore);
		m_SwapchainImageRenderFinishedSemaphores.clear();
		DestroyFrameContextRingResources();
	}

	void VansVKDevice::DrawShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		VANS_PROFILE_SCOPE("RenderRecord::CascadeShadow", Vans::ProfileCategory::CommandRecord);
		m_Scene->DrawShadowNodes(m_CurrentCascadeShadowDrawPlan);
		m_Scene->DrawTerrainNode(true);
	}

	void VansVKDevice::DrawSkyMotionVectorPass(VansVKCommandBuffer& commandBuffer)
	{
		VANS_PROFILE_SCOPE("RenderRecord::SkyMotionVector", Vans::ProfileCategory::CommandRecord);
		VansGraphicsShader* shader =
			VansShaderManager::Get().FindGraphicsShader("SkyMotionVector");
		if (shader == nullptr || m_Scene == nullptr)
			return;
		m_globalRenderStateData.vertexInputBindingDescriptions = nullptr;
		m_globalRenderStateData.vertexInputAttributeDescriptions = nullptr;
		std::vector<VkDescriptorSetLayout> layouts = {
			m_Scene->GetGlobalDescriptorSetLayout()
		};
		std::vector<VkDescriptorSet> sets = {
			m_Scene->GetGlobalDescriptorSet()
		};
		if (commandBuffer.EnsureGraphicsShader(
			*shader, m_globalRenderStateData, layouts) == nullptr)
			return;
		commandBuffer.BindDescriptorSets(
			VK_PIPELINE_BIND_POINT_GRAPHICS, *shader, 0, sets, {});
		commandBuffer.BindGraphicsPipeline(*shader->GetGraphicsPipeline());
		commandBuffer.Draw(3, 1, 0, 0);
	}

	void VansVKDevice::DrawPunctualShadowMap(uint32_t atlasIndex)
	{
		VANS_PROFILE_SCOPE("RenderRecord::PunctualShadow", Vans::ProfileCategory::CommandRecord);
		if (m_Scene == nullptr || m_pActiveCommandBuffer == nullptr)
			return;

		VkClearAttachment clearAttachment{};
		clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		clearAttachment.clearValue.depthStencil = { 1.0f, 0 };
		std::vector<VkClearAttachment> clearAttachments = { clearAttachment };

		const auto& jobs = m_CurrentRenderSceneSnapshot.punctualShadowJobs;
		const auto firstAtlasJob = std::find_if(jobs.begin(), jobs.end(),
			[atlasIndex](const VansPunctualShadowRenderJob& job)
			{
				return job.atlasIndex == atlasIndex &&
					job.shadowViewIndex != VANS_INVALID_SHADOW_INDEX;
			});
		if (firstAtlasJob == jobs.end())
			return;
		if (m_CurrentPunctualShadowDrawPlan.size() != jobs.size())
		{
			VANS_LOG_ERROR("[VansVKDevice] Punctual-shadow draw plan does not match the prepared frame");
			CurrentFrameContext().frameSubmitSucceeded = false;
			return;
		}
		for (std::size_t jobIndex = 0; jobIndex < jobs.size(); ++jobIndex)
		{
			const VansPunctualShadowRenderJob& job = jobs[jobIndex];
			if (job.atlasIndex != atlasIndex || job.shadowViewIndex == VANS_INVALID_SHADOW_INDEX)
				continue;
			VkClearRect clearRect{};
			clearRect.rect.offset = {
				static_cast<int32_t>(job.atlasRect.x),
				static_cast<int32_t>(job.atlasRect.y)
			};
			clearRect.rect.extent = { job.atlasRect.width, job.atlasRect.height };
			clearRect.baseArrayLayer = 0;
			clearRect.layerCount = 1;
			std::vector<VkClearRect> clearRects = { clearRect };
			m_pActiveCommandBuffer->ClearAttachment(clearAttachments, clearRects);
			m_Scene->DrawPunctualShadowJob(
				job, m_CurrentPunctualShadowDrawPlan[jobIndex]);
		}
	}

	void VansVKDevice::NotifyPunctualShadowJobsSubmitted()
	{
		m_PunctualShadowFrameState.NotifyRenderJobsSubmitted();
	}

	bool VansVKDevice::RecordShadowMapParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex)
	{
		VANS_PROFILE_SCOPE("RenderRecord::CascadeShadowParallel", Vans::ProfileCategory::CommandRecord);
		if (!m_EnableParallelCommandRecording || renderPassManager == nullptr || m_Scene == nullptr || !m_Scene->IsSceneReady())
			return false;
		if (!InitializeParallelCommandRecording() || !CurrentFrameSlot().shadowSecondaryCommands || !CurrentFrameSlot().shadowSecondaryCommands->IsReady())
			return false;

		const auto& opaqueNodes = m_Scene->GetOpaqueRenderNodes();
		const size_t opaqueNodeCount = opaqueNodes.size();
		if (opaqueNodeCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		const uint32_t availableSecondaries = CurrentFrameSlot().shadowSecondaryCommands->GetCommandBufferCount();
		if (availableSecondaries < 3)
			return false;

		VansRenderPassRuntimeInfo runtimeInfo = renderPassManager->GetRenderPassRuntimeInfo(
			renderPassManager->m_VansShadowPass,
			framebufferIndex,
			0);
		if (runtimeInfo.renderPass == VK_NULL_HANDLE || runtimeInfo.framebuffer == VK_NULL_HANDLE)
			return false;

		GlobalStateData passState = m_globalRenderStateData;
		passState.currentRenderPass = runtimeInfo.renderPass;
		passState.currentSubpass = runtimeInfo.subpass;
		passState.viewport = runtimeInfo.viewport;
		passState.scissor = runtimeInfo.scissor;

		VansDrawSubmissionList submission;
		if (!m_Scene->BuildShadowDrawSubmission(
			passState, submission, m_CurrentCascadeShadowDrawPlan))
			return false;
		const size_t drawBatchCount = submission.batches.size();
		if (drawBatchCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		const uint32_t maxOpaqueChunks = availableSecondaries - 1u;
		uint32_t opaqueChunkCount = static_cast<uint32_t>((drawBatchCount + m_MinDrawsPerSecondary - 1u) / m_MinDrawsPerSecondary);
		opaqueChunkCount = (std::min)(opaqueChunkCount, (std::min)(m_ParallelRecordThreadCount, maxOpaqueChunks));
		if (opaqueChunkCount < 2)
			return false;

		CommandBufferInheritanceInfo inheritanceInfo = {};
		inheritanceInfo.renderPass = runtimeInfo.renderPass;
		inheritanceInfo.subpass = runtimeInfo.subpass;
		inheritanceInfo.framebuffer = runtimeInfo.framebuffer;

		std::vector<VkCommandBuffer> executableBuffers(static_cast<size_t>(opaqueChunkCount) + 1u, VK_NULL_HANDLE);
		std::vector<uint8_t> chunkSuccess(opaqueChunkCount, 0);
		std::vector<Vans::VansJobSystem::Job> jobs;
		jobs.reserve(opaqueChunkCount);

		const size_t chunkSize = (drawBatchCount + opaqueChunkCount - 1u) / opaqueChunkCount;
		for (uint32_t chunkIndex = 0; chunkIndex < opaqueChunkCount; ++chunkIndex)
		{
			const size_t begin = static_cast<size_t>(chunkIndex) * chunkSize;
			const size_t end = (std::min)(drawBatchCount, begin + chunkSize);
			VansVKCommandBuffer* secondary = CurrentFrameSlot().shadowSecondaryCommands->Get(chunkIndex);
			if (secondary == nullptr)
				return false;
			jobs.emplace_back([secondary, passState, inheritanceInfo, begin, end, chunkIndex, &submission, &chunkSuccess, &executableBuffers]()
			{
				VansRenderWorkerContractScope workerContract;
				if (!secondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
					return;
				secondary->SetViewport(0, { passState.viewport });
				secondary->SetScissor(0, { passState.scissor });
				VansDrawSubmission::Record(*secondary, submission, begin, end);
				if (!secondary->EndCommandBufferRecord())
					return;
				executableBuffers[chunkIndex] = secondary->GetVKCommandBuffer();
				chunkSuccess[chunkIndex] = 1;
			});
		}

		auto group = Vans::VansJobSystem::Get().QueueJobGroup(jobs);
		group->Wait();
		if (group->HasErrors())
		{
			CurrentFrameSlot().shadowSecondaryCommands->ResetAll(false);
			return false;
		}
		for (uint8_t success : chunkSuccess)
		{
			if (!success)
			{
				CurrentFrameSlot().shadowSecondaryCommands->ResetAll(false);
				return false;
			}
		}

		const uint32_t serialSecondaryIndex = opaqueChunkCount;
		VansVKCommandBuffer* serialSecondary = CurrentFrameSlot().shadowSecondaryCommands->Get(serialSecondaryIndex);
		if (serialSecondary == nullptr
			|| !serialSecondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
		{
			CurrentFrameSlot().shadowSecondaryCommands->ResetAll(false);
			return false;
		}
		serialSecondary->SetViewport(0, { passState.viewport });
		serialSecondary->SetScissor(0, { passState.scissor });
		m_Scene->DrawVegetationShadowNode(*serialSecondary, passState);
		m_Scene->DrawTerrainNode(*serialSecondary, passState, true);
		if (!serialSecondary->EndCommandBufferRecord())
		{
			CurrentFrameSlot().shadowSecondaryCommands->ResetAll(false);
			return false;
		}
		executableBuffers[serialSecondaryIndex] = serialSecondary->GetVKCommandBuffer();

		renderPassManager->BeginRenderPass(
			renderPassManager->m_VansShadowPass,
			commandBuffer,
			m_globalRenderStateData,
			framebufferIndex,
			VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		commandBuffer.ExecuteSecondaryCommandBuffer(executableBuffers);
		renderPassManager->EndRenderPass(commandBuffer, m_globalRenderStateData);
		CurrentFrameSlot().shadowSecondaryNeedsReset = true;
		return true;
	}

	void VansVKDevice::DrawSceneGBuffer(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		VANS_PROFILE_SCOPE("RenderRecord::GBuffer", Vans::ProfileCategory::CommandRecord);
		VkCommandBuffer cmd = commandBuffer.GetVKCommandBuffer();
		{
			VANS_GPU_SCOPE(cmd, "GBuffer Opaque");
			m_Scene->DrawOpaqueNodes(commandBuffer, m_globalRenderStateData);
		}
		{
			VANS_GPU_SCOPE(cmd, "GBuffer Terrain");
			m_Scene->DrawTerrainNode(commandBuffer, m_globalRenderStateData);
		}
		{
			VANS_GPU_SCOPE(cmd, "GBuffer Vegetation");
			m_Scene->DrawVegetationNode(commandBuffer, m_globalRenderStateData);
		}
	}

	bool VansVKDevice::RecordSceneGBufferParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex)
	{
		VANS_PROFILE_SCOPE("RenderRecord::GBufferParallel", Vans::ProfileCategory::CommandRecord);
		if (!m_EnableParallelCommandRecording || renderPassManager == nullptr || m_Scene == nullptr || !m_Scene->IsSceneReady())
			return false;
		if (!InitializeParallelCommandRecording() || !CurrentFrameSlot().gbufferSecondaryCommands || !CurrentFrameSlot().gbufferSecondaryCommands->IsReady())
			return false;

		const auto& opaqueNodes = m_Scene->GetOpaqueRenderNodes();
		const size_t opaqueNodeCount = opaqueNodes.size();
		if (opaqueNodeCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		const uint32_t availableSecondaries = CurrentFrameSlot().gbufferSecondaryCommands->GetCommandBufferCount();
		if (availableSecondaries < 3)
			return false;

		VansRenderPassRuntimeInfo runtimeInfo = renderPassManager->GetRenderPassRuntimeInfo(
			renderPassManager->m_VansGBufferPass,
			framebufferIndex,
			0);
		if (runtimeInfo.renderPass == VK_NULL_HANDLE || runtimeInfo.framebuffer == VK_NULL_HANDLE)
			return false;

		GlobalStateData gbufferState = m_globalRenderStateData;
		gbufferState.currentRenderPass = runtimeInfo.renderPass;
		gbufferState.currentSubpass = runtimeInfo.subpass;
		gbufferState.viewport = runtimeInfo.viewport;
		gbufferState.scissor = runtimeInfo.scissor;

		VansDrawSubmissionList& submission = m_Scene->GetOpaqueDrawSubmissionScratch();
		if (!m_Scene->BuildOpaqueDrawSubmission(gbufferState, submission))
			return false;
		const size_t drawBatchCount = submission.batches.size();
		if (drawBatchCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		const uint32_t maxOpaqueChunks = availableSecondaries - 1u;
		uint32_t opaqueChunkCount = static_cast<uint32_t>((drawBatchCount + m_MinDrawsPerSecondary - 1u) / m_MinDrawsPerSecondary);
		opaqueChunkCount = (std::min)(opaqueChunkCount, (std::min)(m_ParallelRecordThreadCount, maxOpaqueChunks));
		if (opaqueChunkCount < 2)
			return false;

		CommandBufferInheritanceInfo inheritanceInfo = {};
		inheritanceInfo.renderPass = runtimeInfo.renderPass;
		inheritanceInfo.subpass = runtimeInfo.subpass;
		inheritanceInfo.framebuffer = runtimeInfo.framebuffer;

		std::vector<VkCommandBuffer> executableBuffers;
		executableBuffers.resize(static_cast<size_t>(opaqueChunkCount) + 1u, VK_NULL_HANDLE);
		std::vector<uint8_t> chunkSuccess(opaqueChunkCount, 0);
		std::vector<Vans::VansJobSystem::Job> jobs;
		jobs.reserve(opaqueChunkCount);

		const size_t chunkSize = (drawBatchCount + opaqueChunkCount - 1u) / opaqueChunkCount;
		for (uint32_t chunkIndex = 0; chunkIndex < opaqueChunkCount; ++chunkIndex)
		{
			const size_t begin = static_cast<size_t>(chunkIndex) * chunkSize;
			const size_t end = (std::min)(drawBatchCount, begin + chunkSize);
			VansVKCommandBuffer* secondary = CurrentFrameSlot().gbufferSecondaryCommands->Get(chunkIndex);
			if (secondary == nullptr)
				return false;

			jobs.emplace_back([secondary, gbufferState, inheritanceInfo, begin, end, chunkIndex, &submission, &chunkSuccess, &executableBuffers]()
			{
				VansRenderWorkerContractScope workerContract;
				if (!secondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
					return;
				secondary->SetViewport(0, { gbufferState.viewport });
				secondary->SetScissor(0, { gbufferState.scissor });
				VansDrawSubmission::Record(*secondary, submission, begin, end);
				if (!secondary->EndCommandBufferRecord())
					return;

				executableBuffers[chunkIndex] = secondary->GetVKCommandBuffer();
				chunkSuccess[chunkIndex] = 1;
			});
		}

		auto group = Vans::VansJobSystem::Get().QueueJobGroup(jobs);
		group->Wait();
		if (group->HasErrors())
		{
			VANS_LOG_ERROR("[VansVKDevice] GBuffer worker recording failed: " << group->GetError());
			CurrentFrameSlot().gbufferSecondaryCommands->ResetAll(false);
			return false;
		}
		for (uint8_t success : chunkSuccess)
		{
			if (!success)
			{
				VANS_LOG_ERROR("[VansVKDevice] GBuffer worker recording failed; falling back to inline recording.");
				CurrentFrameSlot().gbufferSecondaryCommands->ResetAll(false);
				return false;
			}
		}

		const uint32_t serialSecondaryIndex = opaqueChunkCount;
		VansVKCommandBuffer* serialSecondary = CurrentFrameSlot().gbufferSecondaryCommands->Get(serialSecondaryIndex);
		if (serialSecondary == nullptr)
		{
			CurrentFrameSlot().gbufferSecondaryCommands->ResetAll(false);
			return false;
		}
		if (!serialSecondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
		{
			CurrentFrameSlot().gbufferSecondaryCommands->ResetAll(false);
			return false;
		}
		serialSecondary->SetViewport(0, { gbufferState.viewport });
		serialSecondary->SetScissor(0, { gbufferState.scissor });
		VkCommandBuffer serialCmd = serialSecondary->GetVKCommandBuffer();
		{
			VANS_GPU_SCOPE(serialCmd, "GBuffer Terrain");
			m_Scene->DrawTerrainNode(*serialSecondary, gbufferState);
		}
		{
			VANS_GPU_SCOPE(serialCmd, "GBuffer Vegetation");
			m_Scene->DrawVegetationNode(*serialSecondary, gbufferState);
		}
		if (!serialSecondary->EndCommandBufferRecord())
		{
			CurrentFrameSlot().gbufferSecondaryCommands->ResetAll(false);
			return false;
		}
		executableBuffers[serialSecondaryIndex] = serialSecondary->GetVKCommandBuffer();

		renderPassManager->BeginRenderPass(
			renderPassManager->m_VansGBufferPass,
			commandBuffer,
			m_globalRenderStateData,
			framebufferIndex,
			VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		std::vector<VkCommandBuffer> serialExecutableBuffer = { executableBuffers.back() };
		executableBuffers.pop_back();
		// SECONDARY_COMMAND_BUFFERS 子通道内，主命令流不能插入 timestamp。
		// 完整 GBuffer 由外层 pass scope 计时；这里仅执行已录制的绘制命令。
		commandBuffer.ExecuteSecondaryCommandBuffer(executableBuffers);
		commandBuffer.ExecuteSecondaryCommandBuffer(serialExecutableBuffer);
		renderPassManager->EndRenderPass(commandBuffer, m_globalRenderStateData);
		CurrentFrameSlot().gbufferSecondaryNeedsReset = true;
		return true;
	}

	bool VansVKDevice::RecordDecalPassParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex)
	{
		VANS_PROFILE_SCOPE("RenderRecord::DecalParallel", Vans::ProfileCategory::CommandRecord);
		if (!m_EnableParallelCommandRecording || renderPassManager == nullptr || m_Scene == nullptr || !m_Scene->IsSceneReady())
			return false;
		if (!InitializeParallelCommandRecording() || !CurrentFrameSlot().decalSecondaryCommands || !CurrentFrameSlot().decalSecondaryCommands->IsReady())
			return false;

		const auto& decalNodes = m_Scene->GetDecalRenderNodes();
		const size_t decalNodeCount = decalNodes.size();
		if (decalNodeCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		const uint32_t availableSecondaries = CurrentFrameSlot().decalSecondaryCommands->GetCommandBufferCount();
		if (availableSecondaries < 2)
			return false;

		VansRenderPassRuntimeInfo runtimeInfo = renderPassManager->GetRenderPassRuntimeInfo(
			renderPassManager->GetVansDecalPass(),
			framebufferIndex,
			0);
		if (runtimeInfo.renderPass == VK_NULL_HANDLE || runtimeInfo.framebuffer == VK_NULL_HANDLE)
			return false;

		GlobalStateData passState = m_globalRenderStateData;
		passState.currentRenderPass = runtimeInfo.renderPass;
		passState.currentSubpass = runtimeInfo.subpass;
		passState.viewport = runtimeInfo.viewport;
		passState.scissor = runtimeInfo.scissor;

		VansDrawSubmissionList submission;
		if (!m_Scene->BuildDecalDrawSubmission(passState, submission))
			return false;
		const size_t drawBatchCount = submission.batches.size();
		if (drawBatchCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		uint32_t chunkCount = static_cast<uint32_t>((drawBatchCount + m_MinDrawsPerSecondary - 1u) / m_MinDrawsPerSecondary);
		chunkCount = (std::min)(chunkCount, (std::min)(m_ParallelRecordThreadCount, availableSecondaries));
		if (chunkCount < 2)
			return false;

		CommandBufferInheritanceInfo inheritanceInfo = {};
		inheritanceInfo.renderPass = runtimeInfo.renderPass;
		inheritanceInfo.subpass = runtimeInfo.subpass;
		inheritanceInfo.framebuffer = runtimeInfo.framebuffer;

		std::vector<VkCommandBuffer> executableBuffers(chunkCount, VK_NULL_HANDLE);
		std::vector<uint8_t> chunkSuccess(chunkCount, 0);
		std::vector<Vans::VansJobSystem::Job> jobs;
		jobs.reserve(chunkCount);

		const size_t chunkSize = (drawBatchCount + chunkCount - 1u) / chunkCount;
		for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
		{
			const size_t begin = static_cast<size_t>(chunkIndex) * chunkSize;
			const size_t end = (std::min)(drawBatchCount, begin + chunkSize);
			VansVKCommandBuffer* secondary = CurrentFrameSlot().decalSecondaryCommands->Get(chunkIndex);
			if (secondary == nullptr)
				return false;

			jobs.emplace_back([secondary, passState, inheritanceInfo, begin, end, chunkIndex, &submission, &chunkSuccess, &executableBuffers]()
			{
				VansRenderWorkerContractScope workerContract;
				if (!secondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
					return;
				secondary->SetViewport(0, { passState.viewport });
				secondary->SetScissor(0, { passState.scissor });
				VansDrawSubmission::Record(*secondary, submission, begin, end);
				if (!secondary->EndCommandBufferRecord())
					return;
				executableBuffers[chunkIndex] = secondary->GetVKCommandBuffer();
				chunkSuccess[chunkIndex] = 1;
			});
		}

		auto group = Vans::VansJobSystem::Get().QueueJobGroup(jobs);
		group->Wait();
		for (uint8_t success : chunkSuccess)
		{
			if (group->HasErrors() || !success)
			{
				CurrentFrameSlot().decalSecondaryCommands->ResetAll(false);
				return false;
			}
		}

		renderPassManager->BeginRenderPass(
			renderPassManager->GetVansDecalPass(),
			commandBuffer,
			m_globalRenderStateData,
			framebufferIndex,
			VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		commandBuffer.ExecuteSecondaryCommandBuffer(executableBuffers);
		renderPassManager->EndRenderPass(commandBuffer, m_globalRenderStateData);
		CurrentFrameSlot().decalSecondaryNeedsReset = true;
		return true;
	}

	void VansVKDevice::UpdateAtmosphereStaticLuts(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* atmosphere = m_Scene->GetAtmosphereSystem())
		{
			m_AtmosphereFrameRecorded = true;
			CurrentFrameContext().atmosphereStaticLutsWritten =
				atmosphere->RecordStaticLutUpdates(
					commandBuffer, CurrentFrameContext().frameNumber);
		}
	}

	void VansVKDevice::UpdateCloudShadow(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* clouds = m_Scene->GetVolumetricCloudSystem())
		{
			m_CloudFrameRecorded = true;
			clouds->RecordShadow(commandBuffer);
		}
	}

	void VansVKDevice::UpdateAtmosphereViewLuts(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* atmosphere = m_Scene->GetAtmosphereSystem())
		{
			m_AtmosphereFrameRecorded = true;
			atmosphere->RecordViewLutUpdates(commandBuffer);
		}
	}

	void VansVKDevice::UpdateLocalMedia(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* nearMedia = m_Scene->GetNearMediaSystem())
		{
			if (m_CurrentRenderView.historyReset !=
				VansRenderViewHistoryReset::None)
				nearMedia->InvalidateHistory();
			m_NearMediaFrameRecorded = true;
			nearMedia->Record(commandBuffer);
		}
	}

	void VansVKDevice::UpdateVolumetricCloud(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* clouds = m_Scene->GetVolumetricCloudSystem())
		{
			m_CloudFrameRecorded = true;
			clouds->RecordRayMarch(commandBuffer);
		}
	}

	void VansVKDevice::CompositeAtmosphere(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* atmosphere = m_Scene->GetAtmosphereSystem())
		{
			m_AtmosphereFrameRecorded = true;
			atmosphere->RecordComposite(commandBuffer);
		}
	}

	// ============================================================
	// 延迟光照只输出未经过介质积分的原始不透明辐亮度。
	// ============================================================
	void VansVKDevice::DrawSceneRawOpaqueLighting(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		m_Scene->DeferredShading();
	}

	// ============================================================
	// Composite transparent content into HDR SceneColor. Display conversion is
	// intentionally deferred until after FSR.
	// ============================================================
	void VansVKDevice::DrawSceneTransparentPost(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
					if (!IsIsolatedDeferredDebugOutput(m_CurrentRenderSceneSnapshot))
		{
			if (m_CurrentRenderSceneSnapshot.features.hasHair)
                DrawHairComposite(renderPassManager, commandBuffer);
			m_Scene->DrawTransParentNodes();
		}
	}

	void VansVKDevice::BuildSceneColorPyramid(
		VansVKImage& source,
		VansVKImage& target,
		VansVKCommandBuffer& commandBuffer,
		VkPipelineStageFlags consumerStages)
	{
		const VkImageLayout oldSourceLayout = source.GetImageLayout();
		const VkImageLayout oldTargetLayout = target.GetImageLayout();
		const uint32_t mipCount = target.GetImageCreateInfo().mipLevels;
		const VkExtent3D sourceExtent = source.GetImageDimension();

		VkImageMemoryBarrier toCopy[2]{};
		toCopy[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toCopy[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		toCopy[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		toCopy[0].oldLayout = oldSourceLayout;
		toCopy[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		toCopy[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[0].image = source.GetImage();
		toCopy[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		toCopy[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toCopy[1].srcAccessMask = (oldTargetLayout == VK_IMAGE_LAYOUT_UNDEFINED)
			? 0
			: (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
		toCopy[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toCopy[1].oldLayout = oldTargetLayout;
		toCopy[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toCopy[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[1].image = target.GetImage();
		toCopy[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1 };

		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			{}, {}, { toCopy[0], toCopy[1] });

		VkImageCopy copyRegion{};
		copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.extent = sourceExtent;
		commandBuffer.CopyImageRegions(source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, { copyRegion });

		int32_t mipWidth = static_cast<int32_t>(sourceExtent.width);
		int32_t mipHeight = static_cast<int32_t>(sourceExtent.height);
		VkImageMemoryBarrier mipBarrier{};
		mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		mipBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		mipBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		mipBarrier.image = target.GetImage();
		mipBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		for (uint32_t mip = 1; mip < mipCount; ++mip)
		{
			mipBarrier.subresourceRange.baseMipLevel = mip - 1;
			mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			mipBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			mipBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			commandBuffer.PipelineBarrier(
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				{}, {}, { mipBarrier });

			const int32_t nextWidth = std::max(1, mipWidth / 2);
			const int32_t nextHeight = std::max(1, mipHeight / 2);
			VkImageBlit blit{};
			blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
			blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1 };
			blit.dstOffsets[1] = { nextWidth, nextHeight, 1 };
			commandBuffer.BlitImageRegions(
				target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				{ blit }, VK_FILTER_LINEAR);

			mipBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			mipBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			mipBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			mipBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			commandBuffer.PipelineBarrier(
				VK_PIPELINE_STAGE_TRANSFER_BIT, consumerStages,
				{}, {}, { mipBarrier });

			mipWidth = nextWidth;
			mipHeight = nextHeight;
		}

		VkImageMemoryBarrier toShader[2]{};
		toShader[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toShader[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		toShader[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		toShader[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		toShader[0].newLayout = oldSourceLayout;
		toShader[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader[0].image = source.GetImage();
		toShader[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		toShader[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toShader[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toShader[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toShader[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toShader[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toShader[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader[1].image = target.GetImage();
		toShader[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mipCount - 1, 1, 0, 1 };

		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			consumerStages | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			{}, {}, { toShader[0], toShader[1] });

		source.SetTrackedImageLayout(oldSourceLayout);
		target.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VansVKDevice::CopyOpaqueSceneColorForTransmission(
		VansRenderPassManager* renderPassManager,
		VansVKCommandBuffer& commandBuffer)
	{
		if (renderPassManager == nullptr)
		{
			return;
		}

		BuildSceneColorPyramid(
			renderPassManager->GetRawOpaqueSceneColor(GetFrameResourceSlot()),
			renderPassManager->GetOpaqueSceneColor(GetFrameResourceSlot()),
			commandBuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

	void VansVKDevice::PrepareWaterBackgroundPyramid(
		VansRenderPassManager* renderPassManager,
		VansVKCommandBuffer& commandBuffer)
	{
		if (renderPassManager == nullptr)
		{
			return;
		}

		BuildSceneColorPyramid(
			renderPassManager->GetRawOpaqueSceneColor(GetFrameResourceSlot()),
			renderPassManager->GetWaterBackgroundPyramid(),
			commandBuffer,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

	void VansVKDevice::ResolveDepthOfFieldIntoSceneColor(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		if (renderPassManager == nullptr || m_Scene == nullptr)
		{
			return;
		}

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		if (manager == nullptr ||
			!m_CurrentRenderSceneSnapshot.postProcess.enableDepthOfField)
		{
			return;
		}

		VansTexture* dofResult = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_DOF_RESULT);
		if (dofResult == nullptr)
		{
			return;
		}

		VansVKImage& source = dofResult->GetImage();
		VansVKImage& target = renderPassManager->GetColor();
		const VkExtent3D sourceExtent = source.GetImageDimension();
		const VkExtent3D targetExtent = target.GetImageDimension();
		const VkExtent3D copyExtent =
		{
			std::min(sourceExtent.width, targetExtent.width),
			std::min(sourceExtent.height, targetExtent.height),
			std::min(sourceExtent.depth, targetExtent.depth)
		};
		if (copyExtent.width == 0 || copyExtent.height == 0 || copyExtent.depth == 0)
		{
			return;
		}

		const VkImageLayout oldSourceLayout = source.GetImageLayout();
		const VkImageLayout oldTargetLayout = target.GetImageLayout();

		VkImageMemoryBarrier toCopy[2]{};
		toCopy[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toCopy[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		toCopy[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		toCopy[0].oldLayout = oldSourceLayout;
		toCopy[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		toCopy[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[0].image = source.GetImage();
		toCopy[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		toCopy[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toCopy[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		toCopy[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toCopy[1].oldLayout = oldTargetLayout;
		toCopy[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toCopy[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[1].image = target.GetImage();
		toCopy[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			{}, {}, { toCopy[0], toCopy[1] });

		VkImageCopy copyRegion{};
		copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		copyRegion.extent = copyExtent;
		commandBuffer.CopyImageRegions(
			source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			{ copyRegion });

		VkImageMemoryBarrier toShader[2]{};
		toShader[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toShader[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		toShader[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		toShader[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		toShader[0].newLayout = oldSourceLayout;
		toShader[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader[0].image = source.GetImage();
		toShader[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		toShader[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toShader[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toShader[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		toShader[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toShader[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toShader[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader[1].image = target.GetImage();
		toShader[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			{}, {}, { toShader[0], toShader[1] });

		source.SetTrackedImageLayout(oldSourceLayout);
		target.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VansVKDevice::PrepareSceneColorForTransparentPass(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		if (renderPassManager == nullptr || m_Scene == nullptr)
		{
			return;
		}

		VansMaterialManager* manager = m_Scene->GetMaterialManager();
		const bool dofEnabled = manager != nullptr &&
			m_CurrentRenderSceneSnapshot.postProcess.enableDepthOfField;
		const bool blurTransmissionBackground =
			manager == nullptr ||
			m_CurrentRenderSceneSnapshot.postProcess.blurTransmissionBackground;

		if (dofEnabled && !blurTransmissionBackground)
		{
			CopyOpaqueSceneColorForTransmission(renderPassManager, commandBuffer);
		}

		ResolveDepthOfFieldIntoSceneColor(renderPassManager, commandBuffer);

		if (!dofEnabled || blurTransmissionBackground)
		{
			CopyOpaqueSceneColorForTransmission(renderPassManager, commandBuffer);
		}
	}

	void VansVKDevice::SetupHairLightingDescriptors(VansRenderPassManager* renderPassManager)
	{
		DestroyHairLightingDescriptors();

		if (renderPassManager == nullptr || m_Scene == nullptr ||
            !m_Scene->GetIESProfileManager()->IsGPUResourcesCreated() ||
            renderPassManager->GetHairFrameTargetCount() == 0 ||
            renderPassManager->GetHairOpticalDepth(0).GetImage() == VK_NULL_HANDLE)
		{
			return;
		}

		VansDescriptorSetLayoutFactory::CreateAndAllocate_HairLighting(
			m_HairLightingPassLayout, m_HairLightingPassSets,
			VansCommandRecordingConfig::MaximumFramesInFlight);

		if (m_HairLightingPassSets.size() != VansCommandRecordingConfig::MaximumFramesInFlight)
		{
			return;
		}

		auto* descManager = VansVKDescriptorManager::GetInstance();
		for (std::uint32_t slot = 0; slot < m_HairLightingPassSets.size(); ++slot)
		{
		const std::uint32_t bufferSlot = slot < renderPassManager->GetHairFrameTargetCount()
			? slot : 0u;
		descManager->BeginDescriptorUpdate();
		descManager->WriteBufferDescriptor(
			m_HairLightingPassSets[slot],
			HAIR_LIGHTING_BINDING_LAYER_DEPTHS,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ renderPassManager->GetHairLayerDepthBuffer(bufferSlot).GetNativeBuffer(), 0,
			   renderPassManager->GetHairLayerDepthBuffer(bufferSlot).GetBufferSize() }});
        descManager->WriteImageDescriptor(m_HairLightingPassSets[slot],
            HAIR_LIGHTING_BINDING_PUNCTUAL_SHADOW, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            renderPassManager->GetPunctualShadowDescriptorInfos());

		descManager->WriteImageDescriptor(
			m_HairLightingPassSets[slot],
			HAIR_LIGHTING_BINDING_CASCADE_SHADOW,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				renderPassManager->GetCascadeShadowSampler(),
				renderPassManager->GetCascadeShadowArrayView(),
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
			}});
        auto* ies = m_Scene->GetIESProfileManager();
        descManager->WriteImageDescriptor(m_HairLightingPassSets[slot], HAIR_LIGHTING_BINDING_IES,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{ ies->GetIESProfileTexture().GetSampler(), ies->GetIESProfileArrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }});
		descManager->WriteImageDescriptor(m_HairLightingPassSets[slot], HAIR_LIGHTING_BINDING_OPAQUE_DEPTH,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{renderPassManager->GetDepth(0).GetSampler(), renderPassManager->GetDepth(0).GetImageView(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}});
		descManager->CommitDescriptorUpdates();

        // Publish readiness only after every shared lighting resource is bound.
        // The owner calls this after GI creation/rebuild or render-size recreation.
        auto* materials = m_Scene->GetMaterialManager();
        auto* sky = materials->m_SkyLighting.DiffuseIrradiance();
        const auto& layout = rayTracingContext.GetGIProbeLayoutBuffer();
        const uint32_t count = rayTracingContext.GetGIRegionCount();
        auto* skyX = materials->GetRuntimeRenderTexture(VansMaterialManager::RT_AMBIENT_SKY_CACHE_X);
        auto* skyY = materials->GetRuntimeRenderTexture(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Y);
        auto* skyZ = materials->GetRuntimeRenderTexture(VansMaterialManager::RT_AMBIENT_SKY_CACHE_Z);
        if (!sky || !skyX || !skyY || !skyZ ||
            (count > 0 && layout.GetNativeBuffer() == VK_NULL_HANDLE) ||
			materials->m_AmbientSkyCacheInfoCBBuffer[slot].GetNativeBuffer() == VK_NULL_HANDLE) return;
        if (count > VANS_SSGI_MAX_GI_REGIONS)
            throw std::runtime_error("Hair GI region count exceeds the shared descriptor contract");
        std::vector<VkDescriptorImageInfo> irradiance, visibility;
        std::vector<VkDescriptorBufferInfo> states;
        for (uint32_t i = 0; i < count; ++i)
        {
            auto* light = rayTracingContext.GetGIRegionIrradianceAtlas(i);
            auto* depth = rayTracingContext.GetGIRegionVisibilityAtlas(i);
            auto* state = rayTracingContext.GetGIRegionProbeStateBuffer(i);
            if (!light || !depth || !state || state->GetNativeBuffer() == VK_NULL_HANDLE)
                throw std::runtime_error("Hair GI region resources are incomplete");
            irradiance.push_back({light->GetImage().GetSampler(), light->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL});
            visibility.push_back({depth->GetImage().GetSampler(), depth->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL});
            states.push_back({state->GetNativeBuffer(), 0, state->GetBufferSize()});
        }
        // Unused array slots must still contain valid descriptors. Shader indexing
        // is bounded by the published layout's region count, never by this padding.
        descManager->BeginDescriptorUpdate();
        if (count > 0)
        {
            irradiance.resize(VANS_SSGI_MAX_GI_REGIONS, irradiance.front());
            visibility.resize(VANS_SSGI_MAX_GI_REGIONS, visibility.front());
            states.resize(VANS_SSGI_MAX_GI_REGIONS, states.front());
            descManager->WriteImageDescriptor(m_HairLightingPassSets[slot], HAIR_LIGHTING_BINDING_GI_IRRADIANCE,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, irradiance);
            descManager->WriteImageDescriptor(m_HairLightingPassSets[slot], HAIR_LIGHTING_BINDING_GI_VISIBILITY,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, visibility);
            descManager->WriteBufferDescriptor(m_HairLightingPassSets[slot], HAIR_LIGHTING_BINDING_GI_STATE,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, states);
            descManager->WriteBufferDescriptor(m_HairLightingPassSets[slot], HAIR_LIGHTING_BINDING_GI_LAYOUT,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{layout.GetNativeBuffer(), 0, layout.GetBufferSize()}});
        }
        descManager->WriteImageDescriptor(m_HairLightingPassSets[slot], HAIR_LIGHTING_BINDING_SKY_DIFFUSE,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{sky->GetImage().GetSampler(), sky->GetImage().GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
        const std::pair<uint32_t, VansTexture*> caches[] = {
            {DEFERRED_BINDING_AMBIENT_SKY_CACHE_X, skyX},
            {DEFERRED_BINDING_AMBIENT_SKY_CACHE_Y, skyY},
            {DEFERRED_BINDING_AMBIENT_SKY_CACHE_Z, skyZ}};
        for (const auto& cache : caches)
            descManager->WriteImageDescriptor(m_HairLightingPassSets[slot], cache.first,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {{cache.second->GetImage().GetSampler(), cache.second->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL}});
        descManager->WriteBufferDescriptor(m_HairLightingPassSets[slot], DEFERRED_BINDING_AMBIENT_SKY_CACHE_PARAMS,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, {{materials->m_AmbientSkyCacheInfoCBBuffer[slot].GetNativeBuffer(), 0,
			materials->m_AmbientSkyCacheInfoCBBuffer[slot].GetBufferSize()}});
		descManager->CommitDescriptorUpdates();
		}
		m_HairLightingDescriptorsReady = true;
		m_HairOpaqueDepthViews.fill(renderPassManager->GetDepth(0).GetImageView());
		m_HairOpaqueDepthGenerations.fill(renderPassManager->GetGBufferFrameTargetGeneration());
	}

	void VansVKDevice::UpdateHairFrameTargetDescriptors(VansRenderPassManager* renderPassManager)
	{
		if (renderPassManager == nullptr || renderPassManager->GetHairFrameTargetCount() == 0)
			return;
		auto* descriptors = VansVKDescriptorManager::GetInstance();
		descriptors->BeginDescriptorUpdate();
		for (std::uint32_t slot = 0; slot < VansCommandRecordingConfig::MaximumFramesInFlight; ++slot)
		{
			const std::uint32_t targetSlot = slot < renderPassManager->GetHairFrameTargetCount()
				? slot : 0u;
			VansVKBuffer& buffer = renderPassManager->GetHairLayerDepthBuffer(targetSlot);
			VansVKImage& color = renderPassManager->GetHairColor(targetSlot);
			VansVKImage& opticalDepth = renderPassManager->GetHairOpticalDepth(targetSlot);
			const VkDescriptorBufferInfo info{buffer.GetNativeBuffer(), 0, buffer.GetBufferSize()};
			if (slot < m_HairLightingPassSets.size())
				descriptors->WriteBufferDescriptor(m_HairLightingPassSets[slot],
					HAIR_LIGHTING_BINDING_LAYER_DEPTHS, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {info});
			if (slot < m_HairCompositePassSets.size())
			{
				descriptors->WriteImageDescriptor(m_HairCompositePassSets[slot],
					HAIR_COMP_BINDING_COLOR, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{{color.GetSampler(), color.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
				descriptors->WriteImageDescriptor(m_HairCompositePassSets[slot],
					HAIR_COMP_BINDING_OPTICAL_DEPTH, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					{{VK_NULL_HANDLE, opticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL}});
			}
			if (slot < m_HairDebugSets.size())
			{
				descriptors->WriteImageDescriptor(m_HairDebugSets[slot], 0,
					VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
					{{VK_NULL_HANDLE, opticalDepth.GetImageView(), VK_IMAGE_LAYOUT_GENERAL}});
				descriptors->WriteBufferDescriptor(m_HairDebugSets[slot], 1,
					VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {info});
				descriptors->WriteImageDescriptor(m_HairDebugSets[slot], 2,
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					{{color.GetSampler(), color.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
			}
		}
		descriptors->CommitDescriptorUpdates();
	}

	void VansVKDevice::ClearHairVisibilityResources(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		if (renderPassManager == nullptr)
			return;

        auto& depths = renderPassManager->GetHairLayerDepthBuffer(GetFrameResourceSlot());
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = depths.GetNativeBuffer();
        barrier.size = depths.GetBufferSize();
        commandBuffer.PipelineBarrier((VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT),
            VK_PIPELINE_STAGE_TRANSFER_BIT, {}, { barrier }, {});
        commandBuffer.FillBuffer(depths.GetNativeBuffer(), 0, depths.GetBufferSize(), 0xffffffffu);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        commandBuffer.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
            (VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT), {}, { barrier }, {});
	}

	void VansVKDevice::PrepareHairVisibilityForLighting(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		if (renderPassManager == nullptr)
			return;

		VkBufferMemoryBarrier bufferBarriers[1]{};
		bufferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufferBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		bufferBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		bufferBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarriers[0].buffer = renderPassManager->GetHairLayerDepthBuffer(GetFrameResourceSlot()).GetNativeBuffer();
		bufferBarriers[0].offset = 0;
		bufferBarriers[0].size = renderPassManager->GetHairLayerDepthBuffer(GetFrameResourceSlot()).GetBufferSize();

		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{}, { bufferBarriers[0] }, {});
	}

    void VansVKDevice::UpdateHairDebugPreview(VansRenderPassManager* passes, VansVKCommandBuffer& cmd)
    {
        if (!m_HairDebugRequested.exchange(false, std::memory_order_acq_rel) ||
            !m_CurrentRenderSceneSnapshot.features.hasHair || !passes || !m_Scene)
            return;
        auto* shader = VansShaderManager::Get().FindComputeShader("HairDebug");
        if (!shader) return;
        if (m_HairDebugImage.GetImage() == VK_NULL_HANDLE)
        {
            const auto sourceSize = passes->GetHairColor(GetFrameResourceSlot()).GetImageDimension();
            const uint32_t longestSide = (std::max)(sourceSize.width, sourceSize.height);
            if (longestSide == 0) return;
            const uint32_t previewSide = (std::min)(longestSide, 960u);
            const uint32_t width = (std::max)(1u, sourceSize.width * previewSide / longestSide);
            const uint32_t height = (std::max)(1u, sourceSize.height * previewSide / longestSide);
            if (!m_HairDebugImage.CreateVulkanImage(m_VansVKLogicDevice, {width, height, 1},
                VK_FORMAT_R8G8B8A8_UNORM, 1, 8, VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT,
                false, false, true, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) ||
				!VansDescriptorSetLayoutFactory::CreateAndAllocate_HairDebug(m_HairDebugLayout,
					m_HairDebugSets, VansCommandRecordingConfig::MaximumFramesInFlight))
            {
                auto* descriptors = VansVKDescriptorManager::GetInstance();
                descriptors->DestroyDescriptorSet(m_HairDebugSets);
                descriptors->ReleaseDescriptorSetLayout(m_HairDebugLayout);
                m_HairDebugImage.DestroyVulkanImage(m_VansVKLogicDevice);
                VANS_LOG_ERROR("[HairDebug] Could not allocate intermediate preview resources");
                return;
            }
            auto* descriptors = VansVKDescriptorManager::GetInstance();
            descriptors->BeginDescriptorUpdate();
            for (std::uint32_t slot = 0; slot < m_HairDebugSets.size(); ++slot)
            {
				const std::uint32_t targetSlot = slot < passes->GetHairFrameTargetCount()
                    ? slot : 0u;
                auto& depths = passes->GetHairLayerDepthBuffer(targetSlot);
                auto& color = passes->GetHairColor(targetSlot);
                descriptors->WriteImageDescriptor(m_HairDebugSets[slot], 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    {{VK_NULL_HANDLE, passes->GetHairOpticalDepth(targetSlot).GetImageView(), VK_IMAGE_LAYOUT_GENERAL}});
                descriptors->WriteBufferDescriptor(m_HairDebugSets[slot], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    {{depths.GetNativeBuffer(), 0, depths.GetBufferSize()}});
                descriptors->WriteImageDescriptor(m_HairDebugSets[slot], 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    {{color.GetSampler(), color.GetImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
                descriptors->WriteImageDescriptor(m_HairDebugSets[slot], 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    {{VK_NULL_HANDLE, m_HairDebugImage.GetImageView(), VK_IMAGE_LAYOUT_GENERAL}});
            }
            descriptors->CommitDescriptorUpdates();
        }
        // 生产结果只读；预览转换输出独立 RGBA8 数组，不把浮点厚度/SSBO 交给 ImGui。
        VkMemoryBarrier source{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        source.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        source.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkImageMemoryBarrier target{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        target.oldLayout = m_HairDebugImage.GetImageLayout();
        target.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        target.srcAccessMask = target.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        target.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        target.srcQueueFamilyIndex = target.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        target.image = m_HairDebugImage.GetImage();
        target.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 8};
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {source}, {}, {target});
        m_HairDebugImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
        cmd.EnsureComputeShader(*shader, {m_Scene->GetGlobalDescriptorSetLayout(), m_HairDebugLayout});
        const auto size = m_HairDebugImage.GetImageDimension();
        cmd.DispatchCompute(*shader, (size.width+7)/8, (size.height+7)/8, 1,
            {m_Scene->GetGlobalDescriptorSet(), m_HairDebugSets[GetFrameResourceSlot()]});
        source.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        source.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, {source});
		CurrentFrameContext().hairDebugPreviewWritten = true;
        m_HairDebugReady.store(true, std::memory_order_release);
    }

	void VansVKDevice::DestroyHairLightingDescriptors()
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
        m_HairDebugReady.store(false, std::memory_order_release);
        m_HairDebugRequested.store(false, std::memory_order_release);
        descManager->DestroyDescriptorSet(m_HairDebugSets);
        descManager->ReleaseDescriptorSetLayout(m_HairDebugLayout);
        m_HairDebugImage.DestroyVulkanImage(m_VansVKLogicDevice);
		descManager->DestroyDescriptorSet(m_HairLightingPassSets);
		descManager->ReleaseDescriptorSetLayout(m_HairLightingPassLayout);
		m_HairLightingDescriptorsReady = false;
		m_HairOpaqueDepthViews.fill(VK_NULL_HANDLE);
		m_HairOpaqueDepthGenerations.fill(0u);
	}

	void VansVKDevice::SetupHairCompositeDescriptors(VansRenderPassManager* renderPassManager)
	{
		DestroyHairCompositeDescriptors();

		if (renderPassManager == nullptr)
		{
			return;
		}

		VansDescriptorSetLayoutFactory::CreateAndAllocate_HairComposite(
			m_HairCompositePassLayout, m_HairCompositePassSets,
			VansCommandRecordingConfig::MaximumFramesInFlight);

		if (m_HairCompositePassSets.size() != VansCommandRecordingConfig::MaximumFramesInFlight)
		{
			return;
		}
		UpdateHairFrameTargetDescriptors(renderPassManager);
		m_HairCompositeDescriptorsReady = true;
	}

	void VansVKDevice::DestroyHairCompositeDescriptors()
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->DestroyDescriptorSet(m_HairCompositePassSets);
		descManager->ReleaseDescriptorSetLayout(m_HairCompositePassLayout);
		m_HairCompositeDescriptorsReady = false;
	}

	void VansVKDevice::SetupTransmissionGlassDescriptors(VansRenderPassManager* renderPassManager)
	{
		DestroyTransmissionGlassDescriptors();

		if (renderPassManager == nullptr)
		{
			return;
		}

		VansDescriptorSetLayoutFactory::CreateAndAllocate_TransmissionGlass(
			m_TransmissionGlassPassLayout, m_TransmissionGlassPassSets,
			VansCommandRecordingConfig::MaximumFramesInFlight);

		if (m_TransmissionGlassPassSets.size() != VansCommandRecordingConfig::MaximumFramesInFlight)
			return;
		UpdateTransmissionGlassDescriptors(renderPassManager);
	}

	void VansVKDevice::UpdateTransmissionGlassDescriptors(VansRenderPassManager* renderPassManager)
	{
		if (renderPassManager == nullptr ||
			m_TransmissionGlassPassSets.size() != VansCommandRecordingConfig::MaximumFramesInFlight ||
			renderPassManager->GetOpaqueSceneColorTargetCount() == 0)
			return;

		auto* descManager = VansVKDescriptorManager::GetInstance();
		VansTexture* ssrReflection = nullptr;
		if (m_Scene != nullptr && m_Scene->GetMaterialManager() != nullptr)
		{
			ssrReflection = m_Scene->GetMaterialManager()->GetRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT);
		}
		descManager->BeginDescriptorUpdate();
		for (std::uint32_t slot = 0; slot < m_TransmissionGlassPassSets.size(); ++slot)
		{
			VansVKImage& opaqueSceneColor = renderPassManager->GetOpaqueSceneColor(
				std::min(slot, renderPassManager->GetOpaqueSceneColorTargetCount() - 1u));
			descManager->WriteImageDescriptor(
				m_TransmissionGlassPassSets[slot],
				TRANSMISSION_GLASS_BINDING_OPAQUE_SCENE_COLOR,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{{opaqueSceneColor.GetSampler(), opaqueSceneColor.GetImageView(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}});
			descManager->WriteImageDescriptor(
				m_TransmissionGlassPassSets[slot],
				TRANSMISSION_GLASS_BINDING_SSR_REFLECTION,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{{
					ssrReflection != nullptr ? ssrReflection->GetImage().GetSampler() : opaqueSceneColor.GetSampler(),
					ssrReflection != nullptr ? ssrReflection->GetImage().GetImageView() : opaqueSceneColor.GetImageView(),
					ssrReflection != nullptr ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}});
			descManager->WriteImageDescriptor(
				m_TransmissionGlassPassSets[slot],
				TRANSMISSION_GLASS_BINDING_OPAQUE_DEPTH,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{{renderPassManager->GetDepth(0).GetSampler(), renderPassManager->GetDepth(0).GetImageView(),
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}});
			descManager->WriteImageDescriptor(
				m_TransmissionGlassPassSets[slot],
				TRANSMISSION_GLASS_BINDING_CASCADE_SHADOW,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				{{renderPassManager->GetCascadeShadowSampler(), renderPassManager->GetCascadeShadowArrayView(),
					VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL}});
			descManager->WriteImageDescriptor(
				m_TransmissionGlassPassSets[slot],
				TRANSMISSION_GLASS_BINDING_PUNCTUAL_SHADOW,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				renderPassManager->GetPunctualShadowDescriptorInfos());
		}
		descManager->CommitDescriptorUpdates();
		m_TransmissionGlassDescriptorsReady = true;
		m_TransmissionOpaqueDepthViews.fill(renderPassManager->GetDepth(0).GetImageView());
		m_TransmissionOpaqueDepthGenerations.fill(renderPassManager->GetGBufferFrameTargetGeneration());
	}

	bool VansVKDevice::BindFrameOpaqueDepthReaders(
		VansRenderPassManager* renderPassManager,
		std::uint32_t frameSlot, std::uint32_t gbufferTargetSlot)
	{
		if (renderPassManager == nullptr ||
			frameSlot >= VansCommandRecordingConfig::MaximumFramesInFlight ||
			gbufferTargetSlot >= renderPassManager->GetGBufferFrameTargetCount())
			return false;
		if ((m_HairLightingDescriptorsReady && frameSlot >= m_HairLightingPassSets.size()) ||
			(m_TransmissionGlassDescriptorsReady && frameSlot >= m_TransmissionGlassPassSets.size()))
			return false;
		VansVKImage& depth = renderPassManager->GetDepth(gbufferTargetSlot);
		const VkImageView view = depth.GetImageView();
		if (view == VK_NULL_HANDLE)
			return false;
		const std::uint64_t targetGeneration = renderPassManager->GetGBufferFrameTargetGeneration();
		const bool updateHair = m_HairLightingDescriptorsReady &&
			(m_HairOpaqueDepthViews[frameSlot] != view ||
				m_HairOpaqueDepthGenerations[frameSlot] != targetGeneration);
		const bool updateGlass = m_TransmissionGlassDescriptorsReady &&
			(m_TransmissionOpaqueDepthViews[frameSlot] != view ||
				m_TransmissionOpaqueDepthGenerations[frameSlot] != targetGeneration);
		if (!updateHair && !updateGlass)
			return true;
		auto* descriptors = VansVKDescriptorManager::GetInstance();
		if (descriptors == nullptr)
			return false;
		const VkDescriptorImageInfo depthInfo{
			depth.GetSampler(), view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
		descriptors->BeginDescriptorUpdate();
		if (updateHair)
			descriptors->WriteImageDescriptor(m_HairLightingPassSets[frameSlot],
				HAIR_LIGHTING_BINDING_OPAQUE_DEPTH,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { depthInfo });
		if (updateGlass)
			descriptors->WriteImageDescriptor(m_TransmissionGlassPassSets[frameSlot],
				TRANSMISSION_GLASS_BINDING_OPAQUE_DEPTH,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, { depthInfo });
		descriptors->CommitDescriptorUpdates();
		if (updateHair) m_HairOpaqueDepthViews[frameSlot] = view;
		if (updateHair) m_HairOpaqueDepthGenerations[frameSlot] = targetGeneration;
		if (updateGlass) m_TransmissionOpaqueDepthViews[frameSlot] = view;
		if (updateGlass) m_TransmissionOpaqueDepthGenerations[frameSlot] = targetGeneration;
		return true;
	}

	void VansVKDevice::DestroyTransmissionGlassDescriptors()
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->DestroyDescriptorSet(m_TransmissionGlassPassSets);
		descManager->ReleaseDescriptorSetLayout(m_TransmissionGlassPassLayout);
		m_TransmissionGlassDescriptorsReady = false;
		m_TransmissionOpaqueDepthViews.fill(VK_NULL_HANDLE);
		m_TransmissionOpaqueDepthGenerations.fill(0u);
	}

		void VansVKDevice::DrawHairLighting(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
		{
			if (!m_HairLightingDescriptorsReady || m_HairLightingPassSets.empty())
			{
				return;
			}

            m_Scene->DrawHairNodes(rayTracingContext.GetGIRegionCount() > 0
                ? VansPass::HAIR_LIGHTING : VansPass::HAIR_LIGHTING_SKY);
		}

    void VansVKDevice::DrawHairDepthResolve(VansRenderPassManager*, VansVKCommandBuffer& commandBuffer)
    {
        if (!m_HairLightingDescriptorsReady || m_HairLightingPassSets.empty()) return;
        auto* shader = VansShaderManager::Get().FindGraphicsShader("HairDepthResolve");
        if (!shader) throw std::runtime_error("HairDepthResolve shader is not registered");
        m_globalRenderStateData.vertexInputBindingDescriptions = nullptr;
        m_globalRenderStateData.vertexInputAttributeDescriptions = nullptr;
        commandBuffer.EnsureGraphicsShader(*shader, m_globalRenderStateData,
            {m_Scene->GetGlobalDescriptorSetLayout(), m_HairLightingPassLayout});
        commandBuffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *shader, 0,
            {m_Scene->GetGlobalDescriptorSet(), m_HairLightingPassSets[GetFrameResourceSlot()]}, {});
        commandBuffer.BindGraphicsPipeline(*shader->GetGraphicsPipeline());
        commandBuffer.Draw(3, 1, 0, 0);
    }

	void VansVKDevice::DrawHairComposite(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		if (!m_HairCompositeDescriptorsReady || m_HairCompositePassSets.empty())
		{
			return;
		}

		VansGraphicsShader* shader = VansShaderManager::Get().FindGraphicsShader("HairComposite");
		if (shader == nullptr)
		{
			return;
		}

		m_globalRenderStateData.vertexInputBindingDescriptions = nullptr;
		m_globalRenderStateData.vertexInputAttributeDescriptions = nullptr;

		std::vector<VkDescriptorSetLayout> layouts = { m_Scene->GetGlobalDescriptorSetLayout(), m_HairCompositePassLayout };
		std::vector<VkDescriptorSet> sets = { m_Scene->GetGlobalDescriptorSet(),
			m_HairCompositePassSets[GetFrameResourceSlot()] };

		commandBuffer.EnsureGraphicsShader(*shader, m_globalRenderStateData, layouts);
		commandBuffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *shader, 0, sets, {});
		commandBuffer.BindGraphicsPipeline(*shader->GetGraphicsPipeline());
		commandBuffer.Draw(3, 1, 0, 0);
	}

}
