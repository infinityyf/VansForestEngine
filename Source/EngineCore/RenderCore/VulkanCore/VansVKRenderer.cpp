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
#include "../WaterCore/VansWaterSystem.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansJobSystem.h"
#include "../../Util/VansProfiler.h"
#include "../../RuntimeCore/VansFramePhase.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RuntimeUI/Public/VansUISystem.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>
#include <utility>

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

		bool IsDeferredProbeOnlyDebugOutput(
			const VansRenderSceneFrameSnapshot& snapshot)
		{
			return snapshot.gi.prepared &&
				IsGIProbeOnlyDeferredOutputEnabled(snapshot.gi.settings);
		}

		float HalfToFloat(uint16_t value)
		{
			const uint32_t sign = uint32_t(value & 0x8000u) << 16u;
			int32_t exponent = int32_t((value >> 10u) & 0x1fu);
			uint32_t mantissa = value & 0x03ffu;
			uint32_t bits = 0u;
			if (exponent == 0)
			{
				if (mantissa == 0u)
				{
					bits = sign;
				}
				else
				{
					exponent = 1;
					while ((mantissa & 0x0400u) == 0u)
					{
						mantissa <<= 1u;
						--exponent;
					}
					mantissa &= 0x03ffu;
					bits = sign | (uint32_t(exponent + 112) << 23u) | (mantissa << 13u);
				}
			}
			else if (exponent == 31)
			{
				bits = sign | 0x7f800000u | (mantissa << 13u);
			}
			else
			{
				bits = sign | (uint32_t(exponent + 112) << 23u) | (mantissa << 13u);
			}
			float result = 0.0f;
			std::memcpy(&result, &bits, sizeof(result));
			return result;
		}

		uint8_t DisplayByteFromLinearHalf(uint16_t value)
		{
			float linear = HalfToFloat(value);
			if (!std::isfinite(linear) || linear <= 0.0f)
				linear = 0.0f;
			const float mapped = linear / (1.0f + linear);
			return static_cast<uint8_t>(std::clamp(mapped, 0.0f, 1.0f) * 255.0f + 0.5f);
		}

		uint8_t DisplayByteFromLinearFloat(float linear)
		{
			if (!std::isfinite(linear) || linear <= 0.0f)
				linear = 0.0f;
			const float mapped = linear / (1.0f + linear);
			return static_cast<uint8_t>(std::clamp(mapped, 0.0f, 1.0f) * 255.0f + 0.5f);
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
		if (m_SecondaryCommandContext && m_SecondaryCommandContext->IsReady()
			&& m_ShadowSecondaryCommandContext && m_ShadowSecondaryCommandContext->IsReady()
			&& m_DecalSecondaryCommandContext && m_DecalSecondaryCommandContext->IsReady())
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

		if ((!m_ShadowSecondaryCommandContext || !m_ShadowSecondaryCommandContext->IsReady())
			&& !createContext(m_ShadowSecondaryCommandContext, "Shadow"))
			return false;
		if ((!m_SecondaryCommandContext || !m_SecondaryCommandContext->IsReady())
			&& !createContext(m_SecondaryCommandContext, "GBuffer"))
			return false;
		if ((!m_DecalSecondaryCommandContext || !m_DecalSecondaryCommandContext->IsReady())
			&& !createContext(m_DecalSecondaryCommandContext, "Decal"))
			return false;

		VANS_LOG("[VansVKDevice] Parallel command recording initialized. secondariesPerPass=" << secondaryCount);
		return true;
	}

	void VansVKDevice::DestroyParallelCommandRecording()
	{
		if (m_SecondaryCommandContext)
		{
			m_SecondaryCommandContext->Destroy(m_VansVKLogicDevice);
			m_SecondaryCommandContext.reset();
		}
		if (m_ShadowSecondaryCommandContext)
		{
			m_ShadowSecondaryCommandContext->Destroy(m_VansVKLogicDevice);
			m_ShadowSecondaryCommandContext.reset();
		}
		if (m_DecalSecondaryCommandContext)
		{
			m_DecalSecondaryCommandContext->Destroy(m_VansVKLogicDevice);
			m_DecalSecondaryCommandContext.reset();
		}
		m_ShadowSecondaryCommandBuffersNeedReset = false;
		m_GBufferSecondaryCommandBuffersNeedReset = false;
		m_DecalSecondaryCommandBuffersNeedReset = false;
	}

	bool VansVKDevice::ResetGBufferSecondaryCommandBuffersIfNeeded()
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
		resetIfNeeded(m_ShadowSecondaryCommandContext, m_ShadowSecondaryCommandBuffersNeedReset);
		resetIfNeeded(m_SecondaryCommandContext, m_GBufferSecondaryCommandBuffersNeedReset);
		resetIfNeeded(m_DecalSecondaryCommandContext, m_DecalSecondaryCommandBuffersNeedReset);
		return result;
	}

	void VansVKDevice::ResetAsyncFrameCommandBuffersAfterFailure()
	{
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
		m_VansVKShadowMapsCommandBuffer.ResetCommandBuffer(false);
		m_VansVKHairShadowCommandBuffer.ResetCommandBuffer(false);
		m_VansVKGBufferCommandBuffer.ResetCommandBuffer(false);
		m_VansVKGBufferMaterialCommandBuffer.ResetCommandBuffer(false);
		m_VansVKSSAORawCommandBuffer.ResetCommandBuffer(false);
		m_VansVKGraphicsScreenCommandBuffer.ResetCommandBuffer(false);
		m_VansVKVegetationCommandBuffer.ResetCommandBuffer(false);
		m_VansVKEarlyAuxCommandBuffer.ResetCommandBuffer(false);
		m_VansVKAsyncAtmosphereCommandBuffer.ResetCommandBuffer(false);
		m_VansVKAsyncHZBCommandBuffer.ResetCommandBuffer(false);
		m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
		m_VansVKGIDataCommandBuffer.ResetCommandBuffer(false);
		ResetGBufferSecondaryCommandBuffersIfNeeded();
		m_CurrentFrameContext.ssaoRawRecorded = false;
		m_CurrentFrameContext.graphicsScreenRecorded = false;
		m_CurrentFrameContext.shadowMapsRecorded = false;
		m_CurrentFrameContext.hairShadowRecorded = false;
		m_CurrentFrameContext.gbufferRecorded = false;
		m_CurrentFrameContext.gbufferMaterialRecorded = false;
		m_CurrentFrameContext.vegetationRecorded = false;
		m_CurrentFrameContext.earlyAuxRecorded = false;
		m_CurrentFrameContext.asyncAtmosphereRecorded = false;
		m_CurrentFrameContext.asyncHZBRecorded = false;
		m_CurrentFrameContext.rayTracingRecorded = false;
		m_CurrentFrameContext.giDataRecorded = false;
		m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
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

	void VansVKDevice::OnWindowResize(uint32_t width, uint32_t height)
	{
		if (width == 0 || height == 0)
			return;

		WaitForDevice();

		if (!m_VansVKSurface.RecreateSwapChain(m_VansVKPhysicalDevice, m_VansVKLogicDevice))
		{
			VANS_LOG_ERROR("OnWindowResize: swap chain recreation failed.");
			return;
		}

		VkExtent2D newDisplayExtent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
		if (m_FrameContextRingResourcesReady)
		{
			m_SwapchainImageInFlightFences.assign(
				static_cast<size_t>(m_VansVKSurface.m_VansVKImageCount),
				VK_NULL_HANDLE);
			if (!RecreateFrameContextPresentSemaphores())
			{
				VANS_LOG_ERROR("[VansVKDevice] Frame-context ring disabled because swapchain present semaphore recreation failed.");
				DestroyFrameContextRingResources();
				m_EnableFrameContextRing = false;
				m_ConfiguredFramesInFlight = 1;
				BindCurrentFrameContextToLegacyResources();
			}
			else
			{
				m_LastSubmittedGraphicsFence = VK_NULL_HANDLE;
				m_LastSubmittedGraphicsFencePending = false;
				for (VansFrameContextRingSlot& slot : m_FrameContextRingSlots)
				{
					slot.gpuWorkPending = false;
					slot.commandBufferRecording = false;
					slot.graphicsCommandBuffer.ResetCommandBuffer(false);
					slot.deferredDeletes.Flush();
				}
			}
		}

		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->RecreateUIRenderPass(
			m_VansVKCommandBuffer, m_VansVKGraphicsQueue,
			m_VansVKSurface, { newDisplayExtent.width, newDisplayExtent.height }
		);

		// The presentation target establishes the renderer-owned output extent.
		// Embedded editor preview panels only scale this image for display.
		if (m_RequestedUpscalerOutputExtent.width == 0)
		{
			m_RequestedUpscalerOutputExtent = newDisplayExtent;
			m_UpscalerConfigDirty = true;
		}

		VANS_LOG("OnWindowResize: display=" << newDisplayExtent.width << "x" << newDisplayExtent.height
			<< "  render=" << m_RenderWidth << "x" << m_RenderHeight);
	}

	bool VansVKDevice::BeforeRendering()
	{
		if (!CreateVKSemaphore(m_SwapChainImageAcquiredSemaphore) ||
			!CreateVKSemaphore(m_CommandBufferReadyToPresentSemaphore))
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
		renderPassManager->SetupVansDeferredRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansPunctualShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansSkyMotionVectorRenderPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansHairDeepOpacityPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansHairVisibilityPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansHairLightingPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		SetupHairLightingDescriptors(renderPassManager);
		SetupHairCompositeDescriptors(renderPassManager);
		SetupTransmissionGlassDescriptors(renderPassManager);
		// 贴花 Pass：引用 GBuffer 图像，必须在 SetupVansDeferredRenderPass 之后调用。
		renderPassManager->SetupVansDecalRenderPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansScreenSpaceEffectsPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		// 水面 GBuffer Pass：必须在 SetupVansDeferredRenderPass 之后调用，依赖已创建的深度图像。
		renderPassManager->SetupVansWaterGBufferPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		// 注：水面 descriptor sets 在场景加载时由 VansSceneEnvironmentNodeBuilder::AddWaterNode 调用 SetupDescriptors 完成。
		renderPassManager->SetupVansUIRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, m_VansVKSurface,
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
			renderPassManager->GetFinalDisplayColor().GetImageView(),
			fsrDisplayExtent);

		// RuntimeUI frontend initialization is performed by the application on
		// Main after this RT setup has published readiness. This backend only
		// records and tears down renderer-side Noesis work.
		return true;
	}

	void VansVKDevice::BuildCurrentRenderFramePlan(VansRenderPassManager* renderPassManager)
	{
		(void)renderPassManager;

		if (!IsFrameContextRingActive())
		{
			FlushCurrentFrameDeferredDeletes();
			m_CurrentFrameContext.frameNumber = ++m_RenderFrameNumber;
		}
		m_CurrentFrameContext.swapchainImageIndex = m_SwapChainImageIndex;
		if (IsFrameContextRingActive() && m_ActiveFrameContextSlot != nullptr)
			BindCurrentFrameContextToSlot(*m_ActiveFrameContextSlot);
		else
			BindCurrentFrameContextToLegacyResources();
		m_CurrentFrameContext.frameSubmitSucceeded = true;
		m_CurrentFrameContext.ssaoRawRecorded = false;
		m_CurrentFrameContext.graphicsScreenRecorded = false;
		m_CurrentFrameContext.shadowMapsRecorded = false;
		m_CurrentFrameContext.hairShadowRecorded = false;
		m_CurrentFrameContext.gbufferRecorded = false;
		m_CurrentFrameContext.gbufferMaterialRecorded = false;
		m_CurrentFrameContext.vegetationRecorded = false;
		m_CurrentFrameContext.earlyAuxRecorded = false;
		m_CurrentFrameContext.asyncAtmosphereRecorded = false;
		m_CurrentFrameContext.asyncHZBRecorded = false;
		m_CurrentFrameContext.rayTracingRecorded = false;
		m_CurrentFrameContext.giDataRecorded = false;

		VansRenderPassCatalog::BuildCompatibilityFramePlan(
			m_CurrentFramePlan,
			m_CurrentRenderSceneSnapshot.features,
			m_CurrentFrameContext.frameNumber,
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
				m_CurrentFrameContext.frameNumber,
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
		m_CurrentRenderGraphDiagnostics.compiledFrameNumber = m_CurrentFrameContext.frameNumber;
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
		m_CurrentFrameContext.pendingDeferredDeleteCount =
			static_cast<uint64_t>(deleteQueue.Size());
	}

	void VansVKDevice::FlushCurrentFrameDeferredDeletes()
	{
		VansDeferredDeleteQueue& deleteQueue = CurrentDeferredDeleteQueue();
		const uint64_t deleteCount = static_cast<uint64_t>(deleteQueue.Size());
		m_CurrentFrameContext.lastDeferredDeleteFlushCount = deleteCount;
		if (deleteCount > 0)
		{
			deleteQueue.Flush();
		}
		m_CurrentFrameContext.pendingDeferredDeleteCount =
			static_cast<uint64_t>(deleteQueue.Size());
	}

	bool VansVKDevice::IsFrameContextRingActive() const
	{
		return m_EnableFrameContextRing
			&& !m_AsyncComputeEnabled
			&& m_FrameContextRingResourcesReady
			&& m_ConfiguredFramesInFlight > 1;
	}

	VansVKCommandBuffer& VansVKDevice::CurrentGraphicsCommandBuffer()
	{
		return IsFrameContextRingActive() && m_ActiveFrameContextSlot != nullptr
			? m_ActiveFrameContextSlot->graphicsCommandBuffer
			: m_VansVKCommandBuffer;
	}

	const VansVKCommandBuffer& VansVKDevice::CurrentGraphicsCommandBuffer() const
	{
		return IsFrameContextRingActive() && m_ActiveFrameContextSlot != nullptr
			? m_ActiveFrameContextSlot->graphicsCommandBuffer
			: m_VansVKCommandBuffer;
	}

	VansDeferredDeleteQueue& VansVKDevice::CurrentDeferredDeleteQueue()
	{
		return IsFrameContextRingActive() && m_ActiveFrameContextSlot != nullptr
			? m_ActiveFrameContextSlot->deferredDeletes
			: m_CurrentFrameContext.deferredDeletes;
	}

	void VansVKDevice::BindCurrentFrameContextToLegacyResources()
	{
		m_CurrentFrameContext.imageAcquiredSemaphore = m_SwapChainImageAcquiredSemaphore;
		m_CurrentFrameContext.renderFinishedSemaphore = m_CommandBufferReadyToPresentSemaphore;
		m_CurrentFrameContext.graphicsFence = m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.ssaoRawFence = m_VansVKSSAORawCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.graphicsScreenFence = m_VansVKGraphicsScreenCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.shadowMapsFence = m_VansVKShadowMapsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.hairShadowFence = m_VansVKHairShadowCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.gbufferFence = m_VansVKGBufferCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.gbufferMaterialFence = m_VansVKGBufferMaterialCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.vegetationFence = m_VansVKVegetationCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.earlyAuxFence = m_VansVKEarlyAuxCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.asyncAtmosphereFence = m_VansVKAsyncAtmosphereCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.asyncHZBFence = m_VansVKAsyncHZBCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.rayTracingFence = m_VansVKRayTracingCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.giDataFence = m_VansVKGIDataCommandBuffer.m_CommandBufferFinishSubmitFence;
	}

	void VansVKDevice::BindCurrentFrameContextToSlot(VansFrameContextRingSlot& slot)
	{
		m_ActiveFrameContextSlot = &slot;
		m_CurrentFrameContext.frameNumber = slot.frameNumber;
		m_CurrentFrameContext.swapchainImageIndex = slot.swapchainImageIndex;
		m_CurrentFrameContext.imageAcquiredSemaphore = slot.imageAcquiredSemaphore;
		m_CurrentFrameContext.renderFinishedSemaphore =
			slot.swapchainImageIndex < m_SwapchainImageRenderFinishedSemaphores.size()
			? m_SwapchainImageRenderFinishedSemaphores[slot.swapchainImageIndex]
			: VK_NULL_HANDLE;
		m_CurrentFrameContext.graphicsFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.ssaoRawFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.graphicsScreenFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.shadowMapsFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.hairShadowFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.gbufferFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.gbufferMaterialFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.vegetationFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.earlyAuxFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.asyncAtmosphereFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.asyncHZBFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.rayTracingFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.giDataFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.frameSubmitSucceeded = slot.frameSubmitSucceeded;
		m_CurrentFrameContext.ssaoRawRecorded = false;
		m_CurrentFrameContext.graphicsScreenRecorded = false;
		m_CurrentFrameContext.shadowMapsRecorded = false;
		m_CurrentFrameContext.hairShadowRecorded = false;
		m_CurrentFrameContext.gbufferRecorded = false;
		m_CurrentFrameContext.gbufferMaterialRecorded = false;
		m_CurrentFrameContext.vegetationRecorded = false;
		m_CurrentFrameContext.earlyAuxRecorded = false;
		m_CurrentFrameContext.asyncAtmosphereRecorded = false;
		m_CurrentFrameContext.asyncHZBRecorded = false;
		m_CurrentFrameContext.rayTracingRecorded = false;
		m_CurrentFrameContext.giDataRecorded = false;
		m_CurrentFrameContext.pendingDeferredDeleteCount = static_cast<uint64_t>(slot.deferredDeletes.Size());
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

		for (uint32_t i = 0; i < kMaxFrameContextsInFlight; ++i)
		{
			VansFrameContextRingSlot& slot = m_FrameContextRingSlots[i];
			slot.slotIndex = i;
			if (!slot.graphicsCommandBuffer.CreateVulkanCommandBuffer(*this, m_GraphicsQueueFamilyIndex, params))
			{
				VANS_LOG_ERROR("[VansVKDevice] Failed to create frame-context-ring graphics command buffer. slot=" << i);
				DestroyFrameContextRingResources();
				return false;
			}
			if (!CreateVKFence(false, slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence)
				|| !CreateVKSemaphore(slot.imageAcquiredSemaphore))
			{
				VANS_LOG_ERROR("[VansVKDevice] Failed to create frame-context-ring sync resources. slot=" << i);
				DestroyFrameContextRingResources();
				return false;
			}
			slot.gpuWorkPending = false;
			slot.commandBufferRecording = false;
			slot.frameSubmitSucceeded = true;
		}

		m_CurrentFrameContextSlotIndex = 0;
		m_ActiveFrameContextSlot = nullptr;
		m_SwapchainImageInFlightFences.assign(
			static_cast<size_t>(m_VansVKSurface.m_VansVKImageCount),
			VK_NULL_HANDLE);
		if (!RecreateFrameContextPresentSemaphores())
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to create per-swapchain-image present semaphores.");
			DestroyFrameContextRingResources();
			return false;
		}
		m_LastSubmittedGraphicsFence = VK_NULL_HANDLE;
		m_LastSubmittedGraphicsFencePending = false;
		m_FrameContextRingResourcesReady = true;
		VANS_LOG("[VansVKDevice] Frame-context ring resources initialized. framesInFlight=" << m_ConfiguredFramesInFlight);
		return true;
	}

	bool VansVKDevice::RecreateFrameContextPresentSemaphores()
	{
		for (VkSemaphore& semaphore : m_SwapchainImageRenderFinishedSemaphores)
			DestroyVKSemaphore(semaphore);
		m_SwapchainImageRenderFinishedSemaphores.clear();

		m_SwapchainImageRenderFinishedSemaphores.resize(
			static_cast<size_t>(m_VansVKSurface.m_VansVKImageCount),
			VK_NULL_HANDLE);
		for (VkSemaphore& semaphore : m_SwapchainImageRenderFinishedSemaphores)
		{
			if (!CreateVKSemaphore(semaphore))
			{
				for (VkSemaphore& createdSemaphore : m_SwapchainImageRenderFinishedSemaphores)
					DestroyVKSemaphore(createdSemaphore);
				m_SwapchainImageRenderFinishedSemaphores.clear();
				return false;
			}
		}
		return true;
	}

	void VansVKDevice::DestroyFrameContextRingResources()
	{
		if (m_VansVKLogicDevice == VK_NULL_HANDLE)
			return;

		for (VansFrameContextRingSlot& slot : m_FrameContextRingSlots)
		{
			slot.deferredDeletes.Flush();
			DestroyVKSemaphore(slot.imageAcquiredSemaphore);
			DestroyVKFence(slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence);
			slot.graphicsCommandBuffer.DestroyVulkanCommandBuffer(m_VansVKLogicDevice);
			slot.imageAcquiredSemaphore = VK_NULL_HANDLE;
			slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence = VK_NULL_HANDLE;
			slot.gpuWorkPending = false;
			slot.commandBufferRecording = false;
			slot.frameSubmitSucceeded = true;
		}
		for (VkSemaphore& semaphore : m_SwapchainImageRenderFinishedSemaphores)
			DestroyVKSemaphore(semaphore);
		m_SwapchainImageRenderFinishedSemaphores.clear();

		m_SwapchainImageInFlightFences.clear();
		m_ActiveFrameContextSlot = nullptr;
		m_LastSubmittedGraphicsFence = VK_NULL_HANDLE;
		m_LastSubmittedGraphicsFencePending = false;
		m_FrameContextRingResourcesReady = false;
	}

	bool VansVKDevice::WaitForFrameContextRingSlot(VansFrameContextRingSlot& slot)
	{
		if (!slot.gpuWorkPending)
			return true;

		VANS_PROFILE_WAIT("Vulkan::WaitFence.FrameSlot");
		VkFence fence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		if (!VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, fence))
		{
			slot.frameSubmitSucceeded = false;
			return false;
		}

		if (!slot.graphicsCommandBuffer.ResetCommandBuffer(false))
		{
			slot.frameSubmitSucceeded = false;
			return false;
		}

		slot.deferredDeletes.Flush();
		slot.gpuWorkPending = false;
		slot.commandBufferRecording = false;
		slot.frameSubmitSucceeded = true;
		for (VkFence& imageFence : m_SwapchainImageInFlightFences)
		{
			if (imageFence == fence)
				imageFence = VK_NULL_HANDLE;
		}
		if (m_LastSubmittedGraphicsFence == fence)
		{
			m_LastSubmittedGraphicsFence = VK_NULL_HANDLE;
			m_LastSubmittedGraphicsFencePending = false;
		}
		return true;
	}

	bool VansVKDevice::WaitForFrameContextRingSlotGpuIdle(const VansFrameContextRingSlot& slot)
	{
		if (!slot.gpuWorkPending)
			return true;

		const VkFence fence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		if (fence == VK_NULL_HANDLE)
			return true;

		VANS_PROFILE_WAIT("Vulkan::WaitFence.FrameSlotGpuIdle");
		const VkResult result = VansGraphics::vkWaitForFences(
			m_VansVKLogicDevice,
			1,
			&fence,
			VK_TRUE,
			UINT64_MAX);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("[VansVKDevice] Failed to wait frame-context-ring slot GPU idle. VkResult="
				<< static_cast<int>(result));
			return false;
		}
		return true;
	}

	bool VansVKDevice::ApplyCommandRecordingSettings(
		bool parallelEnabled,
		bool frameContextRingEnabled,
		uint32_t framesInFlight,
		bool asyncComputeEnabled)
	{
		const uint32_t configuredFrames =
			std::clamp(framesInFlight, 1u, kMaxFrameContextsInFlight);
		const bool shouldEnableRing =
			frameContextRingEnabled && configuredFrames > 1;
		const bool ringResourcesNeedChange =
			m_EnableFrameContextRing != shouldEnableRing ||
			(shouldEnableRing && !m_FrameContextRingResourcesReady);
		const bool asyncStateNeedsChange =
			m_AsyncComputeRequested != asyncComputeEnabled;
		if ((ringResourcesNeedChange || asyncStateNeedsChange) &&
			m_VansVKLogicDevice != VK_NULL_HANDLE && !WaitForDevice())
		{
			return false;
		}

		m_EnableParallelCommandRecording = parallelEnabled;
		ApplyFrameContextRingStateAtIdle(
			frameContextRingEnabled,
			configuredFrames);
		if (asyncStateNeedsChange)
			ApplyAsyncComputeStateAtIdle(asyncComputeEnabled);
		return true;
	}

	void VansVKDevice::ApplyFrameContextRingStateAtIdle(
		bool enabled,
		uint32_t framesInFlight)
	{
		m_ConfiguredFramesInFlight = std::clamp(framesInFlight, 1u, kMaxFrameContextsInFlight);
		const bool shouldEnable = enabled && m_ConfiguredFramesInFlight > 1;
		if (m_EnableFrameContextRing == shouldEnable && (!shouldEnable || m_FrameContextRingResourcesReady))
			return;

		if (!shouldEnable)
		{
			DestroyFrameContextRingResources();
			m_EnableFrameContextRing = false;
			BindCurrentFrameContextToLegacyResources();
			return;
		}

		m_EnableFrameContextRing = true;
		if (!EnsureFrameContextRingResources())
		{
			m_EnableFrameContextRing = false;
			m_ConfiguredFramesInFlight = 1;
			BindCurrentFrameContextToLegacyResources();
			VANS_LOG_ERROR("[VansVKDevice] Frame-context ring disabled because resource initialization failed.");
		}
	}

	bool VansVKDevice::BeginFrameContextRingFrame()
	{
		if (!EnsureFrameContextRingResources())
			return false;

		VansFrameContextRingSlot& slot = m_FrameContextRingSlots[m_CurrentFrameContextSlotIndex % m_ConfiguredFramesInFlight];
		if (!WaitForFrameContextRingSlot(slot))
			return false;

		slot.frameNumber = ++m_RenderFrameNumber;
		slot.frameSubmitSucceeded = true;
		slot.commandBufferRecording = false;
		m_ActiveFrameContextSlot = &slot;
		BindCurrentFrameContextToSlot(slot);

		// 现阶段 camera/global/pass uniform 仍是单份资源。开启 frame ring 时延迟到下一帧开头等待，
		// 可以避免 Present 内硬阻塞，同时保证 CPU 不会覆盖上一帧 GPU 仍在读取的动态数据。
		if (m_LastSubmittedGraphicsFencePending)
		{
			for (VansFrameContextRingSlot& pendingSlot : m_FrameContextRingSlots)
			{
				if (pendingSlot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence == m_LastSubmittedGraphicsFence)
				{
					if (!WaitForFrameContextRingSlot(pendingSlot))
						return false;
					break;
				}
			}
		}

		VkResult acquireResult = VK_ERROR_INITIALIZATION_FAILED;
		{
			VANS_PROFILE_SCOPE("Vulkan::AcquireSwapchainImage", Vans::ProfileCategory::CommandRecord);
			acquireResult = m_VansVKSurface.AcquireVulkanSwapChainImage(
				m_VansVKLogicDevice,
				slot.swapchainImageIndex,
				slot.imageAcquiredSemaphore);
		}
		if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
		{
			slot.frameSubmitSucceeded = false;
			m_CurrentFrameContext.frameSubmitSucceeded = false;
			VANS_LOG_ERROR("[VansVKDevice] Swapchain image acquisition failed. VkResult=" << static_cast<int>(acquireResult));
			return false;
		}

		if (slot.swapchainImageIndex >= m_SwapchainImageInFlightFences.size())
		{
			m_SwapchainImageInFlightFences.assign(
				static_cast<size_t>(m_VansVKSurface.m_VansVKImageCount),
				VK_NULL_HANDLE);
		}
		if (slot.swapchainImageIndex < m_SwapchainImageInFlightFences.size())
		{
			const VkFence imageFence = m_SwapchainImageInFlightFences[slot.swapchainImageIndex];
			if (imageFence != VK_NULL_HANDLE)
			{
				for (VansFrameContextRingSlot& pendingSlot : m_FrameContextRingSlots)
				{
					if (pendingSlot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence == imageFence)
					{
						if (!WaitForFrameContextRingSlot(pendingSlot))
							return false;
						break;
					}
				}
			}
		}

		m_SwapChainImageIndex = slot.swapchainImageIndex;
		BindCurrentFrameContextToSlot(slot);
		ResetFrameStageUploadAllocator();
		return true;
	}

	void VansVKDevice::BindCurrentFrameSyncResources()
	{
		if (IsFrameContextRingActive() && m_ActiveFrameContextSlot != nullptr)
		{
			BindCurrentFrameContextToSlot(*m_ActiveFrameContextSlot);
			return;
		}
		BindCurrentFrameContextToLegacyResources();
	}

	void VansVKDevice::Rendering()
	{
		VANS_PROFILE_SCOPE("Vulkan::Rendering", Vans::ProfileCategory::CommandRecord);
		BindCurrentFrameSyncResources();
		m_CurrentFrameContext.frameSubmitSucceeded = true;

		if (IsFrameContextRingActive())
		{
			if (!BeginFrameContextRingFrame())
				return;
		}
		else
		{
			VkResult acquireResult = VK_ERROR_INITIALIZATION_FAILED;
			{
				VANS_PROFILE_SCOPE("Vulkan::AcquireSwapchainImage", Vans::ProfileCategory::CommandRecord);
				acquireResult = m_VansVKSurface.AcquireVulkanSwapChainImage(
					m_VansVKLogicDevice,
					m_SwapChainImageIndex,
					m_CurrentFrameContext.imageAcquiredSemaphore);
			}
			if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
			{
				m_CurrentFrameContext.frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Swapchain image acquisition failed. VkResult=" << static_cast<int>(acquireResult));
				return;
			}
			ResetFrameStageUploadAllocator();
		}

		if (!m_CurrentRenderSceneSnapshot.sceneReady)
		{
			VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);

			// No scene loaded yet; begin the command buffer so the UI render
			// pass (recorded by DrawEditorWindows) can still be appended.
			// Present() will end the recording and submit.
			if (!CurrentGraphicsCommandBuffer().BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
			{
				m_CurrentFrameContext.frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Failed to begin main command buffer without scene.");
			}
			return;
		}

		if (!m_HasCurrentRenderView)
		{
			m_CurrentFrameContext.frameSubmitSucceeded = false;
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
			m_CurrentFrameContext.frameNumber);
		m_Scene->PrepareRenderBackendData(
			m_CurrentRenderView,
			m_CurrentRenderSceneSnapshot,
			m_RenderWorld);
		m_DrawInstanceArena.BeginFrame(frameResourceSlot);
		ProcessPendingGISettings();
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
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin main graphics command buffer.");
					return;
				}
			}
			VkCommandBuffer cmd = frameGraphicsCommandBuffer.GetVKCommandBuffer();

			// Record video texture uploads into the main graphics submission.
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::VideoTextureUpload,
				[&]() { m_Scene->RecordVideoUploads(
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
				int cascadeCount = VansConfigration::GetInstance()->GetCascadeCount();
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

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::HairDeepOpacity,
				"Hair Deep Opacity Pass",
				renderPassManager,
				renderPassManager->GetVansHairDeepOpacityPass(),
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawHairDeepOpacityNodes(VansShaderManager::Get().FindGraphicsShader("HairDeepOpacity")); });

			RecordFrameGpuStep(
				m_CurrentFramePlan,
				VansRenderPassNames::GBuffer,
				"GBuffer Pass",
				cmd,
				[&]()
				{
					if (!RecordSceneGBufferParallel(renderPassManager, frameGraphicsCommandBuffer))
					{
						RecordFrameGraphicsPassNoGpuScope(
							m_CurrentFramePlan,
							VansRenderPassNames::GBuffer,
							renderPassManager,
							renderPassManager->m_VansGBufferPass,
							frameGraphicsCommandBuffer,
							m_globalRenderStateData,
							[&]() { DrawSceneGBuffer(renderPassManager, frameGraphicsCommandBuffer); });
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
				[&]() { DrawSkyMotionVectorPass(frameGraphicsCommandBuffer); });

			RecordFrameGpuStep(
				m_CurrentFramePlan,
				VansRenderPassNames::Decal,
				"Decal Pass",
				cmd,
				[&]()
				{
					if (!RecordDecalPassParallel(renderPassManager, frameGraphicsCommandBuffer))
					{
						RecordFrameGraphicsPassNoGpuScope(
							m_CurrentFramePlan,
							VansRenderPassNames::Decal,
							renderPassManager,
							renderPassManager->GetVansDecalPass(),
							frameGraphicsCommandBuffer,
							m_globalRenderStateData,
							[&]() { m_Scene->DrawDecalNodes(); });
					}
				});

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::ScreenSpaceEffects,
				"Screen Space Effects Pass",
				renderPassManager,
				renderPassManager->GetVansScreenSpaceEffectsPass(),
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawScreenSpaceFeatureNode(); });

			{
				VANS_GPU_SCOPE(cmd, "Compute Between GBuffer And Deferred");
				// Build tile-light lists before the HZB update.
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::TileLightBuild, [&]() { BuildTileLightLists(frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::HZB, [&]() { UpdateHZB(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::PunctualShadowDebug, [&]() { UpdatePunctualShadowDebugPreview(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::SSAOFilter, [&]()
				{
					VANS_GPU_SCOPE_LANE(cmd, "SSAO.Bilateral", Vans::VansGpuQueueLane::Graphics);
					BilateralFilterSSAO(renderPassManager, frameGraphicsCommandBuffer);
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
				[&]() { DrawSceneRawOpaqueLighting(renderPassManager, frameGraphicsCommandBuffer); });

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
					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
						m_Scene->DrawForwardOpaquePreAtmosphereNodes();
				});
			// 主深度已经包含 Deferred 与 Forward Opaque；云步进在最近表面终止，
			// 并读取当前帧局部雾 Froxel 处理重叠介质。
			RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::VolumetricCloud,
				[&]() { UpdateVolumetricCloud(frameGraphicsCommandBuffer); });
			// Generate water coverage only after opaque custom materials have populated main depth.
			if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot) &&
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
					const float frameDelta = static_cast<float>(
						m_CurrentRenderTiming.deltaSeconds);
					waterSys->Update(frameDelta, camPos, viewMatrix,
						vpMatrix, mainLightDir, mainLightColor);
					RecordFrameGpuStep(
						m_CurrentFramePlan,
						VansRenderPassNames::WaterWaveCompute,
						"Water Wave Compute",
						cmd,
						[&]()
						{
							waterSys->UpdateWaveSimulation(
								frameGraphicsCommandBuffer, frameDelta);
						});
					RecordFrameGraphicsPass(
						m_CurrentFramePlan,
						VansRenderPassNames::WaterGBuffer,
						"Water GBuffer Pass",
						renderPassManager,
						renderPassManager->GetVansWaterGBufferPass(),
						frameGraphicsCommandBuffer,
						m_globalRenderStateData,
						[&]() { m_Scene->DrawWaterGBufferNode(); });
				}
			}
			if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
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
			if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot) &&
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
					waterSys->DispatchCausticsCS(frameGraphicsCommandBuffer);
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
				});
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::AtmosphereComposite,
				[&]() { CompositeAtmosphere(frameGraphicsCommandBuffer); });
			if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot) &&
				IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::HairVisibility))
			{
				ClearHairOITResources(renderPassManager, frameGraphicsCommandBuffer);
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::HairVisibility,
					"Hair Visibility Pass",
					renderPassManager,
					renderPassManager->GetVansHairVisibilityPass(),
					frameGraphicsCommandBuffer,
					m_globalRenderStateData,
					[&]() { m_Scene->DrawHairVisibilityNodes(); });
				PrepareHairOITForResolve(renderPassManager, frameGraphicsCommandBuffer);
			}

					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::HairLighting,
					"Hair Lighting Pass",
					renderPassManager,
					renderPassManager->GetVansHairLightingPass(),
					frameGraphicsCommandBuffer,
					m_globalRenderStateData,
					[&]() { DrawHairLighting(renderPassManager, frameGraphicsCommandBuffer); });
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
					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
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
					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
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
				[&]() { DrawSceneTransparentPost(renderPassManager, frameGraphicsCommandBuffer); });
		}
		else
		{
			// Vegetation 是 Shadow/GBuffer 唯一的 early-compute 前置条件，单独提交后
			// 两条 graphics queue 无需等待 TileLight 与 MainCameraHiZ。
			m_pActiveCommandBuffer = &m_VansVKVegetationCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordVegetation", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKVegetationCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin vegetation command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				Vans::VansGpuProfiler::Get().BeginQueue(
					m_VansVKVegetationCommandBuffer.GetVKCommandBuffer(),
					Vans::VansGpuQueueLane::Compute);
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::VegetationCompute,
					[&]()
					{
						VANS_GPU_SCOPE_LANE(
							m_VansVKVegetationCommandBuffer.GetVKCommandBuffer(),
							"Vegetation Compute",
							Vans::VansGpuQueueLane::Compute);
						m_Scene->RecordVegetationCompute(m_VansVKVegetationCommandBuffer);
					});
				if (!m_VansVKVegetationCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end vegetation command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.vegetationRecorded = true;

			// TileLight 与上一帧 OcclusionHZB 的读取不依赖 vegetation 输出。
			m_pActiveCommandBuffer = &m_VansVKEarlyAuxCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordEarlyAux", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKEarlyAuxCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
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
							m_VansVKEarlyAuxCommandBuffer.GetVKCommandBuffer(),
							"Tile Light Build",
							Vans::VansGpuQueueLane::Compute);
						BuildTileLightLists(m_VansVKEarlyAuxCommandBuffer);
					});
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::MainCameraHiZCull,
					[&]()
					{
						VANS_GPU_SCOPE_LANE(
							m_VansVKEarlyAuxCommandBuffer.GetVKCommandBuffer(),
							"Main Camera HiZ Cull",
							Vans::VansGpuQueueLane::Compute);
						UpdateMainCameraHiZCull(renderPassManager, m_VansVKEarlyAuxCommandBuffer);
					});
				if (!m_VansVKEarlyAuxCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end early auxiliary command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.earlyAuxRecorded = true;

			m_pActiveCommandBuffer = &m_VansVKAsyncAtmosphereCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordAsyncAtmosphere", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKAsyncAtmosphereCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin async atmosphere command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::AtmosphereStaticLuts,
					[&]() { UpdateAtmosphereStaticLuts(m_VansVKAsyncAtmosphereCommandBuffer); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::CloudShadow,
					[&]() { UpdateCloudShadow(m_VansVKAsyncAtmosphereCommandBuffer); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::AtmosphereViewLuts,
					[&]() { UpdateAtmosphereViewLuts(m_VansVKAsyncAtmosphereCommandBuffer); });
				if (!m_VansVKAsyncAtmosphereCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end async atmosphere command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.asyncAtmosphereRecorded = true;
			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;

			// 级联阴影与两个点光阴影图集在同一条 Shadow Queue 上顺序执行。
			// 两个图集仍是独立图像，但不再让同类型深度绘制争用两条逻辑 graphics queue。
			m_pActiveCommandBuffer = &m_VansVKShadowMapsCommandBuffer;
			VkCommandBuffer shadowCmd = m_VansVKShadowMapsCommandBuffer.GetVKCommandBuffer();
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordShadowMaps", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKShadowMapsCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin shadow-maps command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				{
					Vans::VansGpuProfiler::Get().BeginQueue(shadowCmd, Vans::VansGpuQueueLane::Graphics);
					VANS_GPU_SCOPE_LANE(shadowCmd, "Cascade Shadow", Vans::VansGpuQueueLane::Graphics);
					const int cascadeCount = VansConfigration::GetInstance()->GetCascadeCount();
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
									m_VansVKShadowMapsCommandBuffer,
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
						m_VansVKShadowMapsCommandBuffer,
						m_globalRenderStateData,
						[&, atlasIndex]() { DrawPunctualShadowMap(atlasIndex); },
						static_cast<int>(atlasIndex));
				}
				if (!m_VansVKShadowMapsCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end shadow-maps command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.shadowMapsRecorded = true;

			m_pActiveCommandBuffer = &m_VansVKHairShadowCommandBuffer;
			shadowCmd = m_VansVKHairShadowCommandBuffer.GetVKCommandBuffer();
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordHairShadow", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKHairShadowCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin hair-shadow command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				{
					VANS_GPU_SCOPE_LANE(shadowCmd, "Hair Deep Opacity", Vans::VansGpuQueueLane::Graphics);
					RecordFrameGraphicsPassNoGpuScope(
						m_CurrentFramePlan,
						VansRenderPassNames::HairDeepOpacity,
						renderPassManager,
						renderPassManager->GetVansHairDeepOpacityPass(),
						m_VansVKHairShadowCommandBuffer,
						m_globalRenderStateData,
						[&]() { m_Scene->DrawHairDeepOpacityNodes(VansShaderManager::Get().FindGraphicsShader("HairDeepOpacity")); });
				}
				if (!m_VansVKHairShadowCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end hair-shadow command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.hairShadowRecorded = true;
			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;

			// GBuffer base owns uploads, geometry, depth, and per-surface motion.
			// Sky motion plus optional decals are finalized by the following command buffer.
			m_pActiveCommandBuffer = &m_VansVKGBufferCommandBuffer;
			VkCommandBuffer cmd = m_VansVKGBufferCommandBuffer.GetVKCommandBuffer();
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordGBufferCB", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKGBufferCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin GBuffer command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				Vans::VansGpuProfiler::Get().BeginQueue(cmd, Vans::VansGpuQueueLane::Graphics);
				// Upload video frames before G-buffer rendering.
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::VideoTextureUpload,
					[&]() { m_Scene->RecordVideoUploads(
						m_VansVKGBufferCommandBuffer, m_CurrentRenderSceneSnapshot); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::ClothVertexUpload,
					[&]()
					{
						m_Scene->RecordClothVertexUploads(
							m_VansVKGBufferCommandBuffer,
							m_CurrentRenderSceneSnapshot);
					});
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::GBuffer,
				[&]()
				{
					VANS_GPU_SCOPE(cmd, "GBuffer Pass");
					if (!RecordSceneGBufferParallel(renderPassManager, m_VansVKGBufferCommandBuffer))
					{
						RecordFrameGraphicsPassNoGpuScope(
							m_CurrentFramePlan,
							VansRenderPassNames::GBuffer,
							renderPassManager,
							renderPassManager->m_VansGBufferPass,
							m_VansVKGBufferCommandBuffer,
							m_globalRenderStateData,
							[&]() { DrawSceneGBuffer(renderPassManager, m_VansVKGBufferCommandBuffer); });
					}
				});
				if (!m_VansVKGBufferCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end GBuffer command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.gbufferRecorded = true;

			m_pActiveCommandBuffer = &m_VansVKGBufferMaterialCommandBuffer;
			{
				VkCommandBuffer materialCmd = m_VansVKGBufferMaterialCommandBuffer.GetVKCommandBuffer();
				VANS_PROFILE_SCOPE("Vulkan::RecordGBufferMaterialCB", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKGBufferMaterialCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
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
					m_VansVKGBufferMaterialCommandBuffer,
					m_globalRenderStateData,
					[&]() { DrawSkyMotionVectorPass(m_VansVKGBufferMaterialCommandBuffer); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::Decal,
					[&]()
					{
						VANS_GPU_SCOPE(materialCmd, "Decal Pass");
						if (!RecordDecalPassParallel(renderPassManager, m_VansVKGBufferMaterialCommandBuffer))
						{
							RecordFrameGraphicsPassNoGpuScope(
								m_CurrentFramePlan,
								VansRenderPassNames::Decal,
								renderPassManager,
								renderPassManager->GetVansDecalPass(),
								m_VansVKGBufferMaterialCommandBuffer,
								m_globalRenderStateData,
								[&]() { m_Scene->DrawDecalNodes(); });
						}
					});
				if (!m_VansVKGBufferMaterialCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end GBuffer-material command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.gbufferMaterialRecorded = true;

			// SSAO 原始结果独立提交，SSAO filter 只等待精确的 SSAORawReady 依赖。
			m_pActiveCommandBuffer = &m_VansVKSSAORawCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordSSAORaw", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKSSAORawCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin SSAO-raw command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::ScreenSpaceEffects,
				"Screen Space Effects Pass",
				renderPassManager,
				renderPassManager->GetVansScreenSpaceEffectsPass(),
				m_VansVKSSAORawCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawScreenSpaceFeatureNode(); });
			if (!m_VansVKSSAORawCommandBuffer.EndCommandBufferRecord())
			{
				m_CurrentFrameContext.frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Failed to end SSAO-raw command buffer.");
				ResetAsyncFrameCommandBuffersAfterFailure();
				return;
			}
			m_CurrentFrameContext.ssaoRawRecorded = true;

			m_pActiveCommandBuffer = &m_VansVKAsyncHZBCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordAsyncHZB", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKAsyncHZBCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin async HZB command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				VkMemoryBarrier occlusionReadToWriteBarrier{};
				occlusionReadToWriteBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				occlusionReadToWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
				occlusionReadToWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				m_VansVKAsyncHZBCommandBuffer.PipelineBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					{ occlusionReadToWriteBarrier });
				{
					VANS_GPU_SCOPE_LANE(
						m_VansVKAsyncHZBCommandBuffer.GetVKCommandBuffer(),
						"Async HZB",
						Vans::VansGpuQueueLane::Compute);
					RecordFrameStep(
						m_CurrentFramePlan,
						VansRenderPassNames::HZB,
						[&]() { UpdateHZB(renderPassManager, m_VansVKAsyncHZBCommandBuffer); });
				}
				if (!m_VansVKAsyncHZBCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end async HZB command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.asyncHZBRecorded = true;

			m_pActiveCommandBuffer = &m_VansVKRayTracingCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordRayTracing", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKRayTracingCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin ray-tracing command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				{
					VANS_GPU_SCOPE_LANE(
						m_VansVKRayTracingCommandBuffer.GetVKCommandBuffer(),
						"Ray Tracing",
						Vans::VansGpuQueueLane::Compute);
					RecordFrameStep(
						m_CurrentFramePlan,
						VansRenderPassNames::RayTracing,
						[&]() { UpdateRayTracing(m_VansVKRayTracingCommandBuffer); });
				}
				if (!m_VansVKRayTracingCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end ray-tracing command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.rayTracingRecorded = true;

			m_pActiveCommandBuffer = &m_VansVKGIDataCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordGIData", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKGIDataCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin GI-data command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				VkMemoryBarrier giInputsToSSGIBarrier{};
				giInputsToSSGIBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				giInputsToSSGIBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				giInputsToSSGIBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				m_VansVKGIDataCommandBuffer.PipelineBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					{ giInputsToSSGIBarrier });
				{
					VANS_GPU_SCOPE_LANE(
						m_VansVKGIDataCommandBuffer.GetVKCommandBuffer(),
						"GI Data",
						Vans::VansGpuQueueLane::Compute);
					RecordFrameStep(
						m_CurrentFramePlan,
						VansRenderPassNames::GIData,
						[&]() { UpdateGIData(renderPassManager, m_VansVKGIDataCommandBuffer); });
				}
				if (!m_VansVKGIDataCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end GI-data command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.giDataRecorded = true;

			m_pActiveCommandBuffer = &m_VansVKGraphicsScreenCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordGraphicsScreen", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKGraphicsScreenCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin graphics screen command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
				cmd = m_VansVKGraphicsScreenCommandBuffer.GetVKCommandBuffer();
				{
					VANS_GPU_SCOPE_LANE(cmd, "Graphics Screen Compute", Vans::VansGpuQueueLane::Compute);
					RecordFrameStep(
						m_CurrentFramePlan,
						VansRenderPassNames::PunctualShadowDebug,
						[&]() { UpdatePunctualShadowDebugPreview(renderPassManager, m_VansVKGraphicsScreenCommandBuffer); });
					RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::SSAOFilter, [&]()
					{
						VANS_GPU_SCOPE_LANE(cmd, "SSAO.Bilateral", Vans::VansGpuQueueLane::Compute);
						BilateralFilterSSAO(renderPassManager, m_VansVKGraphicsScreenCommandBuffer);
					});
					RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::ScreenSpaceShadow, [&]() { UpdateScreenSpaceShadow(renderPassManager, m_VansVKGraphicsScreenCommandBuffer); });
					RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::SSR, [&]() { UpdateSSR(renderPassManager, m_VansVKGraphicsScreenCommandBuffer); });
					RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::LocalMedia, [&]() { UpdateLocalMedia(m_VansVKGraphicsScreenCommandBuffer); });
				}
				// ScreenLightingReady semaphore 建立对
				// GraphicsMain fragment 阶段的跨 queue 可见性；compute-only queue
				// 不能在 command buffer 内使用 FRAGMENT_SHADER stage。
				if (!m_VansVKGraphicsScreenCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end graphics screen command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			m_CurrentFrameContext.graphicsScreenRecorded = true;

			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::BeginCommandBuffer.GraphicsMain", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin main graphics command buffer.");
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::RawOpaqueLighting,
				"Raw Opaque Lighting Pass",
				renderPassManager,
				renderPassManager->GetVansRawOpaqueLightingPass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSceneRawOpaqueLighting(renderPassManager, m_VansVKCommandBuffer); });
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::ForwardOpaquePreAtmosphere,
				"Forward Opaque Pre Atmosphere Pass",
				renderPassManager,
				renderPassManager->GetVansPreAtmosphereSurfacePass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]()
				{
					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
						m_Scene->DrawForwardOpaquePreAtmosphereNodes();
				});
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::VolumetricCloud,
				[&]()
				{
					VANS_GPU_SCOPE_LANE(
						m_VansVKCommandBuffer.GetVKCommandBuffer(),
						"Cloud Ray March",
						Vans::VansGpuQueueLane::Graphics);
					UpdateVolumetricCloud(m_VansVKCommandBuffer);
				});
			if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot) &&
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
					const float frameDelta = static_cast<float>(
						m_CurrentRenderTiming.deltaSeconds);
					waterSys->Update(frameDelta, camPos, viewMatrix,
						vpMatrix, mainLightDir, mainLightColor);
				RecordFrameGpuStep(
					m_CurrentFramePlan,
					VansRenderPassNames::WaterWaveCompute,
					"Water Wave Compute",
					cmd,
					[&]()
					{
						waterSys->UpdateWaveSimulation(
							m_VansVKCommandBuffer, frameDelta);
					});
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::WaterGBuffer,
					"Water GBuffer Pass",
					renderPassManager,
					renderPassManager->GetVansWaterGBufferPass(),
					m_VansVKCommandBuffer,
					m_globalRenderStateData,
					[&]() { m_Scene->DrawWaterGBufferNode(); });
				}
			}

			if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameGpuStep(
					m_CurrentFramePlan,
					VansRenderPassNames::WaterSceneColorPyramidPrepare,
					"Water SceneColor Pyramid Prepare",
					m_VansVKCommandBuffer.GetVKCommandBuffer(),
					[&]()
					{
						PrepareWaterBackgroundPyramid(
							renderPassManager, m_VansVKCommandBuffer);
					});
			}

			if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot) &&
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
					waterSys->DispatchWaterThicknessCS(m_VansVKCommandBuffer);
					waterSys->DispatchRefractionCS(m_VansVKCommandBuffer);
					waterSys->DispatchWaterVolumeCS(m_VansVKCommandBuffer);
					waterSys->DispatchWaterVolumeFilterCS(m_VansVKCommandBuffer);
					waterSys->DispatchWaterSSR(m_VansVKCommandBuffer);
					waterSys->DispatchCausticsCS(m_VansVKCommandBuffer);
				}
			}
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::WaterCompositePreAtmosphere,
				"Water Composite Pre Atmosphere Pass",
				renderPassManager,
				renderPassManager->GetVansPreAtmosphereSurfacePass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]()
				{
					VANS_GPU_SCOPE(cmd, "Water Composite Pre Atmosphere");
					m_Scene->DrawWaterCompositeNode();
				});
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::AtmosphereComposite,
				[&]() { CompositeAtmosphere(m_VansVKCommandBuffer); });
			if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot) &&
				IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::HairVisibility))
			{
				ClearHairOITResources(renderPassManager, m_VansVKCommandBuffer);
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::HairVisibility,
					"Hair Visibility Pass",
					renderPassManager,
					renderPassManager->GetVansHairVisibilityPass(),
					m_VansVKCommandBuffer,
					m_globalRenderStateData,
					[&]() { m_Scene->DrawHairVisibilityNodes(); });
				PrepareHairOITForResolve(renderPassManager, m_VansVKCommandBuffer);
			}

					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::HairLighting,
					"Hair Lighting Pass",
					renderPassManager,
					renderPassManager->GetVansHairLightingPass(),
					m_VansVKCommandBuffer,
					m_globalRenderStateData,
					[&]() { DrawHairLighting(renderPassManager, m_VansVKCommandBuffer); });
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
					m_VansVKCommandBuffer.PipelineBarrier(
						VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						{ sceneColorToPostProcess });

					UploadPostProcessProfileIfDirty();
					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
					{
						UpdateDepthOfField(renderPassManager, m_VansVKCommandBuffer);
					}

					VkMemoryBarrier postProcessComputeToFragment{};
					postProcessComputeToFragment.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					postProcessComputeToFragment.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					postProcessComputeToFragment.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					m_VansVKCommandBuffer.PipelineBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						{ postProcessComputeToFragment });
				});

					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
			{
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::TransparentSceneColorPrepare,
					[&]() { PrepareSceneColorForTransparentPass(renderPassManager, m_VansVKCommandBuffer); });
			}
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::TransparentPostProcess,
				"Transparent PostProcess Pass",
				renderPassManager,
				renderPassManager->m_VansTransparentPass,
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSceneTransparentPost(renderPassManager, m_VansVKCommandBuffer); });
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
					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
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
				if (dispatched)
				{
					m_UpscalerManager.GetHistory().OnTemporalDispatchSucceeded();
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
				{
					m_UpscalerManager.GetHistory().OnTemporalDispatchSucceeded();
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

	void VansVKDevice::MaybeDumpGIDebugFrame(VansRenderPassManager* renderPassManager)
	{
		const char* dumpRootEnv = std::getenv("FORESTENGINE_GI_DEBUG_DUMP_DIR");
		if (dumpRootEnv == nullptr || dumpRootEnv[0] == '\0' || renderPassManager == nullptr ||
			m_Scene == nullptr || !m_Scene->IsSceneReady())
		{
			return;
		}
		static uint32_t readyFrameCount = 0u;
		static bool dumped = false;
		if (dumped)
			return;
		++readyFrameCount;
		uint32_t targetFrame = 160u;
		if (const char* frameEnv = std::getenv("FORESTENGINE_GI_DEBUG_DUMP_FRAME"))
		{
			char* endPtr = nullptr;
			const unsigned long parsed = std::strtoul(frameEnv, &endPtr, 10);
			if (endPtr != frameEnv && parsed > 0ul)
				targetFrame = static_cast<uint32_t>(std::min<unsigned long>(parsed, 4096ul));
		}
		if (readyFrameCount < targetFrame)
			return;

		if (IsFrameContextRingActive())
		{
			VANS_LOG("[GIDebugDump] Frame-context ring active; waiting for GPU idle before one-shot readback.");
			if (!WaitForDevice())
			{
				VANS_LOG_ERROR("[GIDebugDump] Failed to wait for GPU idle before readback.");
				dumped = true;
				return;
			}
		}

		namespace fs = std::filesystem;
		const fs::path dumpRoot(dumpRootEnv);
		std::error_code createError;
		fs::create_directories(dumpRoot, createError);
		if (createError)
		{
			VANS_LOG_ERROR("[GIDebugDump] Failed to create dump directory '" << dumpRoot.string()
				<< "': " << createError.message());
			dumped = true;
			return;
		}

		auto dumpImage = [&](const char* label, VansVKImage& image) -> bool
		{
			const VkExtent3D extent = image.GetImageDimension();
			if (extent.width == 0u || extent.height == 0u || image.GetImage() == VK_NULL_HANDLE)
				return false;
			const VkFormat imageFormat = image.GetImageCreateInfo().format;
			const bool isDepth = imageFormat == VK_FORMAT_D32_SFLOAT_S8_UINT ||
				imageFormat == VK_FORMAT_D32_SFLOAT;
			const bool isFloat32 = imageFormat == VK_FORMAT_R32G32B32A32_SFLOAT;
			const bool isHalfFloat = imageFormat == VK_FORMAT_R16G16B16A16_SFLOAT ||
				imageFormat == VK_FORMAT_R16G16_SFLOAT;
			const uint32_t channelCount = isDepth ? 1u :
				(imageFormat == VK_FORMAT_R16G16B16A16_SFLOAT ? 4u :
					(imageFormat == VK_FORMAT_R16G16_SFLOAT ? 2u :
					(imageFormat == VK_FORMAT_R32G32B32A32_SFLOAT ? 4u : 0u)));
			if (channelCount == 0u)
			{
				VANS_LOG_ERROR("[GIDebugDump] Unsupported image format for '" << label << "'.");
				return false;
			}

			const VkDeviceSize componentBytes = (isFloat32 || isDepth) ? sizeof(float) : sizeof(uint16_t);
			const VkDeviceSize pixelBytes = channelCount * componentBytes;
			const VkDeviceSize imageBytes = VkDeviceSize(extent.width) * extent.height * pixelBytes;
			VansVKBuffer readback;
			if (!readback.CreatVulkanBuffer(m_VansVKLogicDevice, imageBytes, VK_FORMAT_R16_UINT,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			{
				VANS_LOG_ERROR("[GIDebugDump] Failed to create readback buffer for '" << label << "'.");
				return false;
			}
			if (!readback.PersistentMap())
			{
				readback.DestroyVulkanBuffer(m_VansVKLogicDevice);
				VANS_LOG_ERROR("[GIDebugDump] Failed to map readback buffer for '" << label << "'.");
				return false;
			}

			VkBufferImageCopy region{};
			const VkImageAspectFlags imageAspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource = { imageAspect, 0, 0, 1 };
			region.imageExtent = { extent.width, extent.height, 1u };
			VansVKCommandBuffer& cmd = m_ImmediateGraphicsCommandBuffer;
			const VkImageLayout oldLayout = image.GetImageLayout();
			cmd.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			image.SetImageMemoryBarrier(cmd,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				{
					image.GetImage(),
					VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
					VK_ACCESS_TRANSFER_READ_BIT,
					oldLayout,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED,
					imageAspect
				});
			VansVKMemoryManager::CopyImageToBuffer(cmd, image, readback,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, { region });
			image.SetImageMemoryBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				{
					image.GetImage(),
					VK_ACCESS_TRANSFER_READ_BIT,
					VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					oldLayout,
					VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED,
					imageAspect
				});
			cmd.EndCommandBufferRecord();
			const bool submitted = VansVKCommandBuffer::SubmitCommands(
				m_VansVKGraphicsQueue, m_VansVKLogicDevice,
				{ cmd.GetVKCommandBuffer() }, {}, {}, cmd.m_CommandBufferFinishSubmitFence);
			cmd.ResetCommandBuffer(false);
			if (!submitted)
			{
				readback.Unmap();
				readback.DestroyVulkanBuffer(m_VansVKLogicDevice);
				VANS_LOG_ERROR("[GIDebugDump] Failed to submit readback for '" << label << "'.");
				return false;
			}
			readback.InvalidateMappedRange(0, imageBytes);

			const void* mapped = readback.GetMappedPtr();
			const uint16_t* half = isHalfFloat ? static_cast<const uint16_t*>(mapped) : nullptr;
			const float* float32 = (isFloat32 || isDepth) ? static_cast<const float*>(mapped) : nullptr;
			bool success = mapped != nullptr;
			std::vector<uint8_t> ppmPixels(size_t(extent.width) * extent.height * 3u);
			double sum[3] = { 0.0, 0.0, 0.0 };
			double alphaSum = 0.0;
			float minValue[3] = {
				std::numeric_limits<float>::max(),
				std::numeric_limits<float>::max(),
				std::numeric_limits<float>::max()
			};
			float maxValue[3] = { 0.0f, 0.0f, 0.0f };
			float minAlpha = std::numeric_limits<float>::max();
			float maxAlpha = 0.0f;
			uint64_t invalidCount = 0u;
			for (uint32_t y = 0u; success && y < extent.height; ++y)
			{
				for (uint32_t x = 0u; x < extent.width; ++x)
				{
					const size_t pixelIndex = size_t(y) * extent.width + x;
					for (uint32_t c = 0u; c < 3u; ++c)
					{
						if (isDepth)
						{
							const float depth = float32[pixelIndex];
							if (!std::isfinite(depth))
							{
								++invalidCount;
							}
							else if (c == 0u)
							{
								sum[0] += depth;
								minValue[0] = std::min(minValue[0], depth);
								maxValue[0] = std::max(maxValue[0], depth);
							}
							const float visibleDepth = std::isfinite(depth) ? std::clamp(depth, 0.0f, 1.0f) : 1.0f;
							ppmPixels[pixelIndex * 3u + c] = static_cast<uint8_t>(
								std::clamp(visibleDepth, 0.0f, 1.0f) * 255.0f + 0.5f);
						}
						else if (c < channelCount)
						{
							const float linear = isFloat32
								? float32[pixelIndex * channelCount + c]
								: HalfToFloat(half[pixelIndex * channelCount + c]);
							if (!std::isfinite(linear))
							{
								++invalidCount;
							}
							else
							{
								sum[c] += linear;
								minValue[c] = std::min(minValue[c], linear);
								maxValue[c] = std::max(maxValue[c], linear);
							}
							ppmPixels[pixelIndex * 3u + c] =
								isFloat32
								? DisplayByteFromLinearFloat(linear)
								: DisplayByteFromLinearHalf(half[pixelIndex * channelCount + c]);
						}
						else
						{
							ppmPixels[pixelIndex * 3u + c] = 0u;
						}
					}
					if (!isDepth && channelCount >= 4u)
					{
						const float alpha = isFloat32
							? float32[pixelIndex * channelCount + 3u]
							: HalfToFloat(half[pixelIndex * channelCount + 3u]);
						if (!std::isfinite(alpha))
						{
							++invalidCount;
						}
						else
						{
							alphaSum += alpha;
							minAlpha = std::min(minAlpha, alpha);
							maxAlpha = std::max(maxAlpha, alpha);
						}
					}
				}
			}

			const fs::path ppmPath = dumpRoot / (std::string(label) + ".ppm");
			const fs::path statsPath = dumpRoot / (std::string(label) + ".txt");
			if (success)
			{
				std::ofstream ppm(ppmPath, std::ios::binary);
				success = static_cast<bool>(ppm);
				if (success)
				{
					ppm << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
					ppm.write(reinterpret_cast<const char*>(ppmPixels.data()),
						static_cast<std::streamsize>(ppmPixels.size()));
					success = static_cast<bool>(ppm);
				}
			}
			if (success)
			{
				const double denom = std::max<double>(double(extent.width) * double(extent.height), 1.0);
				std::ofstream stats(statsPath);
				stats << "label=" << label << '\n'
					<< "size=" << extent.width << "x" << extent.height << '\n'
					<< "channels=" << channelCount << '\n'
					<< "mean_rgb=" << (sum[0] / denom) << ','
					<< (sum[1] / denom) << ',' << (sum[2] / denom) << '\n'
					<< "min_rgb=" << (minValue[0] == std::numeric_limits<float>::max() ? 0.0f : minValue[0]) << ','
					<< (minValue[1] == std::numeric_limits<float>::max() ? 0.0f : minValue[1]) << ','
					<< (minValue[2] == std::numeric_limits<float>::max() ? 0.0f : minValue[2]) << '\n'
					<< "max_rgb=" << maxValue[0] << ',' << maxValue[1] << ',' << maxValue[2] << '\n'
					<< "mean_alpha=" << (alphaSum / denom) << '\n'
					<< "min_alpha=" << (minAlpha == std::numeric_limits<float>::max() ? 0.0f : minAlpha) << '\n'
					<< "max_alpha=" << maxAlpha << '\n'
					<< "invalid_channels=" << invalidCount << '\n'
					<< "display_mapping=reinhard_per_channel\n";
			}

			readback.Unmap();
			readback.DestroyVulkanBuffer(m_VansVKLogicDevice);
			if (success)
			{
				VANS_LOG("[GIDebugDump] Wrote " << ppmPath.string());
			}
			else
			{
				VANS_LOG_ERROR("[GIDebugDump] Failed to write dump for '" << label << "'.");
			}
			return success;
		};

		auto dumpProbeState = [&](const char* label, const VansVKBuffer* stateBuffer) -> bool
		{
			if (stateBuffer == nullptr || stateBuffer->GetNativeBuffer() == VK_NULL_HANDLE ||
				stateBuffer->GetBufferSize() < 48u)
			{
				return false;
			}

			const VkDeviceSize bufferBytes = stateBuffer->GetBufferSize();
			VansVKBuffer readback;
			if (!readback.CreatVulkanBuffer(m_VansVKLogicDevice, bufferBytes, VK_FORMAT_R32_UINT,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
			{
				VANS_LOG_ERROR("[GIDebugDump] Failed to create probe-state readback buffer.");
				return false;
			}
			if (!readback.PersistentMap())
			{
				readback.DestroyVulkanBuffer(m_VansVKLogicDevice);
				VANS_LOG_ERROR("[GIDebugDump] Failed to map probe-state readback buffer.");
				return false;
			}

			VansVKCommandBuffer& cmd = m_ImmediateGraphicsCommandBuffer;
			cmd.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			const_cast<VansVKBuffer*>(stateBuffer)->SetBufferMemoryBarrier(cmd,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				{
					stateBuffer->GetNativeBuffer(),
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
					VK_ACCESS_TRANSFER_READ_BIT,
					VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED
				});
			cmd.CopyBuffer(stateBuffer->GetNativeBuffer(), readback.GetNativeBuffer(), 0, 0, bufferBytes);
			const_cast<VansVKBuffer*>(stateBuffer)->SetBufferMemoryBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
				{
					stateBuffer->GetNativeBuffer(),
					VK_ACCESS_TRANSFER_READ_BIT,
					VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
					VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED
				});
			cmd.EndCommandBufferRecord();
			const bool submitted = VansVKCommandBuffer::SubmitCommands(
				m_VansVKGraphicsQueue, m_VansVKLogicDevice,
				{ cmd.GetVKCommandBuffer() }, {}, {}, cmd.m_CommandBufferFinishSubmitFence);
			cmd.ResetCommandBuffer(false);
			if (!submitted)
			{
				readback.Unmap();
				readback.DestroyVulkanBuffer(m_VansVKLogicDevice);
				VANS_LOG_ERROR("[GIDebugDump] Failed to submit probe-state readback.");
				return false;
			}
			readback.InvalidateMappedRange(0, bufferBytes);

			struct ProbeStateDump
			{
				float relocationAndConfidence[4];
				float distanceStats[4];
				uint32_t metadata[4];
			};
			static_assert(sizeof(ProbeStateDump) == 48u);
			const ProbeStateDump* states = static_cast<const ProbeStateDump*>(readback.GetMappedPtr());
			const size_t stateCount = static_cast<size_t>(bufferBytes / sizeof(ProbeStateDump));
			uint64_t zeroClassification = 0u;
			uint64_t activeClassification = 0u;
			uint64_t inactiveClassification = 0u;
			uint64_t otherClassification = 0u;
			double confidenceSum = 0.0;
			float maxConfidence = 0.0f;
			double updateCountSum = 0.0;
			uint32_t maxUpdateCount = 0u;
			double minFrontDistanceSum = 0.0;
			double meanFrontDistanceSum = 0.0;
			double backfaceRatioSum = 0.0;
			double relocationLengthSum = 0.0;
			float minFrontDistanceMin = std::numeric_limits<float>::max();
			float minFrontDistanceMax = 0.0f;
			float backfaceRatioMax = 0.0f;
			float relocationLengthMax = 0.0f;
			uint64_t nearSurfaceProbeCount = 0u;
			uint64_t backfaceHeavyProbeCount = 0u;
			for (size_t i = 0; states != nullptr && i < stateCount; ++i)
			{
				const uint32_t classification = states[i].metadata[0];
				if (classification == 0u) ++zeroClassification;
				else if (classification == 1u) ++activeClassification;
				else if (classification == 2u) ++inactiveClassification;
				else ++otherClassification;
				const float confidence = states[i].relocationAndConfidence[3];
				if (std::isfinite(confidence))
				{
					confidenceSum += confidence;
					maxConfidence = std::max(maxConfidence, confidence);
				}
				const uint32_t updateCount = states[i].metadata[2];
				updateCountSum += double(updateCount);
				maxUpdateCount = std::max(maxUpdateCount, updateCount);

				const float minFrontDistance = states[i].distanceStats[0];
				const float meanFrontDistance = states[i].distanceStats[1];
				const float backfaceRatio = states[i].distanceStats[2];
				const float rx = states[i].relocationAndConfidence[0];
				const float ry = states[i].relocationAndConfidence[1];
				const float rz = states[i].relocationAndConfidence[2];
				const float relocationLength = std::sqrt(rx * rx + ry * ry + rz * rz);
				if (std::isfinite(minFrontDistance))
				{
					minFrontDistanceSum += minFrontDistance;
					minFrontDistanceMin = std::min(minFrontDistanceMin, minFrontDistance);
					minFrontDistanceMax = std::max(minFrontDistanceMax, minFrontDistance);
					if (minFrontDistance < 0.15f)
						++nearSurfaceProbeCount;
				}
				if (std::isfinite(meanFrontDistance))
					meanFrontDistanceSum += meanFrontDistance;
				if (std::isfinite(backfaceRatio))
				{
					backfaceRatioSum += backfaceRatio;
					backfaceRatioMax = std::max(backfaceRatioMax, backfaceRatio);
					if (backfaceRatio > 0.35f)
						++backfaceHeavyProbeCount;
				}
				if (std::isfinite(relocationLength))
				{
					relocationLengthSum += relocationLength;
					relocationLengthMax = std::max(relocationLengthMax, relocationLength);
				}
			}

			const fs::path statsPath = dumpRoot / (std::string(label) + ".txt");
			std::ofstream stats(statsPath);
			stats << "label=" << label << '\n'
				<< "states=" << stateCount << '\n'
				<< "classification_zero=" << zeroClassification << '\n'
				<< "classification_active=" << activeClassification << '\n'
				<< "classification_inactive=" << inactiveClassification << '\n'
				<< "classification_other=" << otherClassification << '\n'
				<< "mean_confidence=" << (confidenceSum / std::max<double>(double(stateCount), 1.0)) << '\n'
				<< "max_confidence=" << maxConfidence << '\n'
				<< "mean_update_count=" << (updateCountSum / std::max<double>(double(stateCount), 1.0)) << '\n'
				<< "max_update_count=" << maxUpdateCount << '\n'
				<< "mean_min_front_distance=" << (minFrontDistanceSum / std::max<double>(double(stateCount), 1.0)) << '\n'
				<< "min_front_distance_min=" << (minFrontDistanceMin == std::numeric_limits<float>::max() ? 0.0f : minFrontDistanceMin) << '\n'
				<< "min_front_distance_max=" << minFrontDistanceMax << '\n'
				<< "mean_front_distance=" << (meanFrontDistanceSum / std::max<double>(double(stateCount), 1.0)) << '\n'
				<< "mean_backface_ratio=" << (backfaceRatioSum / std::max<double>(double(stateCount), 1.0)) << '\n'
				<< "max_backface_ratio=" << backfaceRatioMax << '\n'
				<< "near_surface_probe_count=" << nearSurfaceProbeCount << '\n'
				<< "backface_heavy_probe_count=" << backfaceHeavyProbeCount << '\n'
				<< "mean_relocation_length=" << (relocationLengthSum / std::max<double>(double(stateCount), 1.0)) << '\n'
				<< "max_relocation_length=" << relocationLengthMax << '\n';
			const bool success = static_cast<bool>(stats);
			readback.Unmap();
			readback.DestroyVulkanBuffer(m_VansVKLogicDevice);
			if (success)
				VANS_LOG("[GIDebugDump] Wrote " << statsPath.string());
			else
				VANS_LOG_ERROR("[GIDebugDump] Failed to write probe-state dump.");
			return success;
		};

		bool wroteAny = false;
		wroteAny = dumpImage("scene_color", renderPassManager->GetColor()) || wroteAny;
		wroteAny = dumpImage("final_display", renderPassManager->GetFinalDisplayColor()) || wroteAny;
		if (m_FSREnabled)
			wroteAny = dumpImage("upscaler", m_UpscalerOutputImage) || wroteAny;
		wroteAny = dumpImage("depth", renderPassManager->GetDepth()) || wroteAny;
		wroteAny = dumpImage("gbuffer_normal", renderPassManager->GetNormal()) || wroteAny;
		wroteAny = dumpImage("gbuffer0_albedo_roughness", renderPassManager->GetGbuffer0()) || wroteAny;
		wroteAny = dumpImage("gbuffer1_material", renderPassManager->GetGbuffer1()) || wroteAny;
		wroteAny = dumpImage("gbuffer2_world_position", renderPassManager->GetGbuffer2()) || wroteAny;
		wroteAny = dumpImage("diffuse_exitant_radiance_history", renderPassManager->GetDiffuseExitantRadianceHistory()) || wroteAny;
		if (VansMaterialManager* materialManager = m_Scene != nullptr ? m_Scene->GetMaterialManager() : nullptr)
		{
			if (VansTexture* screenSpaceShadow = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SCREEN_SPACE_SHADOW_RESULT))
				wroteAny = dumpImage("screen_space_shadow", screenSpaceShadow->GetImage()) || wroteAny;
			if (VansTexture* ssgiResult = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_RESULT))
				wroteAny = dumpImage("ssgi_raw", ssgiResult->GetImage()) || wroteAny;
			if (VansTexture* ssgiProbeCacheRadiance = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_PROBE_CACHE_RADIANCE))
				wroteAny = dumpImage("ssgi_screen_probe_cache_radiance", ssgiProbeCacheRadiance->GetImage()) || wroteAny;
			if (VansTexture* ssgiProbeCacheSurface = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_PROBE_CACHE_SURFACE))
				wroteAny = dumpImage("ssgi_screen_probe_cache_surface", ssgiProbeCacheSurface->GetImage()) || wroteAny;
			if (VansTexture* ssgiTemporalA = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_TEMPORAL_A))
				wroteAny = dumpImage("ssgi_temporal_a", ssgiTemporalA->GetImage()) || wroteAny;
			if (VansTexture* ssgiTemporalB = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_TEMPORAL_B))
				wroteAny = dumpImage("ssgi_temporal_b", ssgiTemporalB->GetImage()) || wroteAny;
			if (VansTexture* ssgiMomentsA = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_MOMENTS_A))
				wroteAny = dumpImage("ssgi_moments_a", ssgiMomentsA->GetImage()) || wroteAny;
			if (VansTexture* ssgiMomentsB = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_MOMENTS_B))
				wroteAny = dumpImage("ssgi_moments_b", ssgiMomentsB->GetImage()) || wroteAny;
			if (VansTexture* ssgiFilter = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SSGI_FILTER_RESULT))
				wroteAny = dumpImage("ssgi_filtered", ssgiFilter->GetImage()) || wroteAny;
		}
		if (VansTexture* irradianceAtlas = rayTracingContext.GetGIRegionIrradianceAtlas(0u))
			wroteAny = dumpImage("gi_irradiance_region0", irradianceAtlas->GetImage()) || wroteAny;
		if (VansTexture* visibilityAtlas = rayTracingContext.GetGIRegionVisibilityAtlas(0u))
			wroteAny = dumpImage("gi_visibility_region0", visibilityAtlas->GetImage()) || wroteAny;
		wroteAny = dumpProbeState("gi_probe_state_region0", rayTracingContext.GetGIRegionProbeStateBuffer(0u)) || wroteAny;
		dumped = true;
		if (wroteAny && std::getenv("FORESTENGINE_GI_DEBUG_DUMP_EXIT") != nullptr)
		{
			VANS_LOG("[GIDebugDump] Exit requested after dump.");
			std::exit(0);
		}
	}

	void VansVKDevice::Present()
	{
		if (!m_CurrentFrameContext.frameSubmitSucceeded)
		{
			VANS_LOG_ERROR("[VansVKDevice] Skipping present because frame recording failed before submit.");
			return;
		}

		if (m_Scene->IsSceneReady())
		{
			VansRenderPassManager::GetInstance()->RecordFrameBufferImageLayoutReset(CurrentGraphicsCommandBuffer());
		}

		{
			VANS_PROFILE_SCOPE("Vulkan::EndCommandBuffer", Vans::ProfileCategory::VulkanSubmit);
			if (!CurrentGraphicsCommandBuffer().EndCommandBufferRecord())
			{
				m_CurrentFrameContext.frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Failed to end main graphics command buffer.");
			}
		}

		// When no scene is loaded, always use the single-submit path because
		// the async-compute command buffer was never recorded/submitted.
		if (!m_AsyncComputeEnabled || !m_Scene->IsSceneReady())
		{
			{
				VANS_PROFILE_SCOPE("Vulkan::QueueSubmit.Graphics", Vans::ProfileCategory::VulkanSubmit);
				m_FrameSubmitOrchestrator.Reset();
				VansFrameSubmitNode graphics;
				graphics.name = "GraphicsFrame";
				graphics.queue = VansQueueRole::Graphics;
				graphics.commandBuffers = { CurrentGraphicsCommandBuffer().GetVKCommandBuffer() };
				graphics.externalWaits = {
					{ m_CurrentFrameContext.imageAcquiredSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }
				};
				graphics.externalSignals = { m_CurrentFrameContext.renderFinishedSemaphore };
				graphics.fence = m_CurrentFrameContext.graphicsFence;
				graphics.waitForCompletion = !IsFrameContextRingActive();
				m_FrameSubmitOrchestrator.AddNode(std::move(graphics));
				m_CurrentFrameContext.frameSubmitSucceeded = m_FrameSubmitOrchestrator.Execute();
				if (m_CurrentFrameContext.frameSubmitSucceeded)
					NotifyPunctualShadowJobsSubmitted();
				if (!m_CurrentFrameContext.frameSubmitSucceeded)
				{
					VANS_LOG_ERROR("[VansVKDevice] Graphics frame submit failed: "
						<< m_FrameSubmitOrchestrator.GetLastError());
				}
			}
			if (IsFrameContextRingActive() && m_ActiveFrameContextSlot != nullptr)
			{
				m_ActiveFrameContextSlot->gpuWorkPending = m_CurrentFrameContext.frameSubmitSucceeded;
				m_ActiveFrameContextSlot->frameSubmitSucceeded = m_CurrentFrameContext.frameSubmitSucceeded;
				m_ActiveFrameContextSlot->commandBufferRecording = false;
				m_LastSubmittedGraphicsFence = m_CurrentFrameContext.graphicsFence;
				m_LastSubmittedGraphicsFencePending = m_CurrentFrameContext.frameSubmitSucceeded;
				if (m_CurrentFrameContext.frameSubmitSucceeded
					&& m_SwapChainImageIndex < m_SwapchainImageInFlightFences.size())
				{
					m_SwapchainImageInFlightFences[m_SwapChainImageIndex] = m_CurrentFrameContext.graphicsFence;
				}
				if (!m_CurrentFrameContext.frameSubmitSucceeded)
				{
					CurrentGraphicsCommandBuffer().ResetCommandBuffer(false);
				}
			}
			else
			{
				VANS_PROFILE_SCOPE("Vulkan::ResetCommandBuffer", Vans::ProfileCategory::VulkanSubmit);
				if (!m_VansVKCommandBuffer.ResetCommandBuffer(false))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to reset main graphics command buffer.");
				}
				if (!ResetGBufferSecondaryCommandBuffersIfNeeded())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to reset GBuffer secondary command buffers.");
				}
			}

		}
		else
		{
			if (!m_CurrentFrameContext.frameSubmitSucceeded
				|| !m_CurrentFrameContext.ssaoRawRecorded
				|| !m_CurrentFrameContext.graphicsScreenRecorded
				|| !m_CurrentFrameContext.shadowMapsRecorded
				|| !m_CurrentFrameContext.hairShadowRecorded
				|| !m_CurrentFrameContext.gbufferRecorded
				|| !m_CurrentFrameContext.gbufferMaterialRecorded
				|| !m_CurrentFrameContext.vegetationRecorded
				|| !m_CurrentFrameContext.earlyAuxRecorded
				|| !m_CurrentFrameContext.asyncAtmosphereRecorded
				|| !m_CurrentFrameContext.asyncHZBRecorded
				|| !m_CurrentFrameContext.rayTracingRecorded
				|| !m_CurrentFrameContext.giDataRecorded)
			{
				VANS_LOG_ERROR("[VansVKDevice] Skipping async frame submit because a prerequisite command buffer was not recorded.");
				ResetAsyncFrameCommandBuffersAfterFailure();
				return;
			}

			{
				VANS_PROFILE_SCOPE("Vulkan::QueueSubmit.AsyncFrameGraph", Vans::ProfileCategory::VulkanSubmit);
				m_FrameSubmitOrchestrator.Reset();

				VansFrameSubmitNode vegetation;
				vegetation.name = "Vegetation";
				vegetation.queue = VansQueueRole::Compute;
				vegetation.commandBuffers = { m_VansVKVegetationCommandBuffer.GetVKCommandBuffer() };
				vegetation.signals = { VansSyncPoint::VegetationReady };
				vegetation.resources = {
					{ "VegetationDrawData", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, true, true, false }
				};
				vegetation.fence = m_CurrentFrameContext.vegetationFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(vegetation));

				VansFrameSubmitNode earlyAux;
				earlyAux.name = "EarlyAux";
				earlyAux.queue = VansQueueRole::Compute;
				earlyAux.commandBuffers = { m_VansVKEarlyAuxCommandBuffer.GetVKCommandBuffer() };
				earlyAux.resources = {
					{ "MainCameraCullObjects", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false },
					{ "MainCameraVisibility", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, true, true, true },
					{ "OcclusionHZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "TileLightLists", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, true, false, false }
				};
				earlyAux.fence = m_CurrentFrameContext.earlyAuxFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(earlyAux));

				VansFrameSubmitNode asyncAtmosphere;
				asyncAtmosphere.name = "AsyncAtmosphere";
				asyncAtmosphere.queue = VansQueueRole::Compute;
				asyncAtmosphere.commandBuffers = { m_VansVKAsyncAtmosphereCommandBuffer.GetVKCommandBuffer() };
				asyncAtmosphere.resources = {
					{ "AtmosphereTransmittance", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "AtmosphereMultiScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "AtmosphereSkyView", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "AtmosphereAerialScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "AtmosphereAerialClearScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "AtmosphereAerialOpticalDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "CloudShadow", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false }
				};
				asyncAtmosphere.fence = m_CurrentFrameContext.asyncAtmosphereFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(asyncAtmosphere));

				VansFrameSubmitNode shadowMaps;
				shadowMaps.name = "ShadowMaps";
				shadowMaps.queue = VansQueueRole::Graphics;
				shadowMaps.commandBuffers = { m_VansVKShadowMapsCommandBuffer.GetVKCommandBuffer() };
				shadowMaps.waits = {
					{ VansSyncPoint::VegetationReady, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT }
				};
				shadowMaps.signals = { VansSyncPoint::ShadowMapsReady };
				shadowMaps.resources = {
					{ "VegetationDrawData", VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
						VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false },
					{ "CascadeShadowMap", VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, true, false },
					{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, true, false },
					{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, true, false }
				};
				shadowMaps.fence = m_CurrentFrameContext.shadowMapsFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(shadowMaps));

				VansFrameSubmitNode hairShadow;
				hairShadow.name = "HairShadow";
				hairShadow.queue = VansQueueRole::Graphics;
				hairShadow.commandBuffers = { m_VansVKHairShadowCommandBuffer.GetVKCommandBuffer() };
				hairShadow.signals = { VansSyncPoint::HairShadowReady };
				hairShadow.resources = {
					{ "HairDeepOpacity", VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, true, false }
				};
				hairShadow.fence = m_CurrentFrameContext.hairShadowFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(hairShadow));

				VansFrameSubmitNode gbuffer;
				gbuffer.name = "GBuffer";
				gbuffer.queue = VansQueueRole::Graphics;
				gbuffer.commandBuffers = { m_VansVKGBufferCommandBuffer.GetVKCommandBuffer() };
				gbuffer.waits = {
					{ VansSyncPoint::VegetationReady, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT }
				};
				gbuffer.signals = { VansSyncPoint::DepthReady };
				gbuffer.resources = {
					{ "VegetationDrawData", VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
						VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, true, false },
					{ "GBufferData", VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false },
					{ "MotionVector", VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false },
					{ "Depth", VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, true, false, false }
				};
				gbuffer.fence = m_CurrentFrameContext.gbufferFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(gbuffer));

				VansFrameSubmitNode gbufferMaterial;
				gbufferMaterial.name = "GBufferMaterial";
				gbufferMaterial.queue = VansQueueRole::Graphics;
				gbufferMaterial.commandBuffers = { m_VansVKGBufferMaterialCommandBuffer.GetVKCommandBuffer() };
				gbufferMaterial.signals = { VansSyncPoint::GBufferMaterialReady };
				gbufferMaterial.resources = {
					{ "GBufferData", VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false },
					{ "MotionVector", VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, false, false },
					{ "Depth", VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, false, false }
				};
				gbufferMaterial.fence = m_CurrentFrameContext.gbufferMaterialFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(gbufferMaterial));

				VansFrameSubmitNode ssaoRaw;
				ssaoRaw.name = "SSAORaw";
				ssaoRaw.queue = VansQueueRole::Graphics;
				ssaoRaw.commandBuffers = { m_VansVKSSAORawCommandBuffer.GetVKCommandBuffer() };
				ssaoRaw.signals = { VansSyncPoint::SSAORawReady };
				ssaoRaw.resources = {
					{ "GBufferData", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, false, false },
					{ "Depth", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, false, false },
					{ "SSAORaw", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false }
				};
				ssaoRaw.fence = m_CurrentFrameContext.ssaoRawFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(ssaoRaw));

				VansFrameSubmitNode asyncHZB;
				asyncHZB.name = "AsyncHZB";
				asyncHZB.queue = VansQueueRole::Compute;
				asyncHZB.commandBuffers = { m_VansVKAsyncHZBCommandBuffer.GetVKCommandBuffer() };
				asyncHZB.waits = {
					{ VansSyncPoint::DepthReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT }
				};
				asyncHZB.resources = {
					{ "Depth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, false, false },
					{ "HZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "OcclusionHZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false }
				};
				asyncHZB.fence = m_CurrentFrameContext.asyncHZBFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(asyncHZB));

				VansFrameSubmitNode rayTracing;
				rayTracing.name = "RayTracing";
				rayTracing.queue = VansQueueRole::Compute;
				rayTracing.commandBuffers = { m_VansVKRayTracingCommandBuffer.GetVKCommandBuffer() };
				rayTracing.waits = {
					{ VansSyncPoint::GBufferMaterialReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR }
				};
				rayTracing.resources = {
					{ "GBufferData", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
						VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, false, false },
					{ "RayTracingGI", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
						VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, true, true, true, false }
				};
				rayTracing.fence = m_CurrentFrameContext.rayTracingFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(rayTracing));

				VansFrameSubmitNode giData;
				giData.name = "GIData";
				giData.queue = VansQueueRole::Compute;
				giData.commandBuffers = { m_VansVKGIDataCommandBuffer.GetVKCommandBuffer() };
				giData.waits = {
					{ VansSyncPoint::ShadowMapsReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT }
				};
				giData.resources = {
					{ "GBufferData", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, false, false },
					{ "CascadeShadowMap", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "HZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "RayTracingGI", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "PreConvolvedDiffuseEnvironment", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "GIData", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false }
				};
				giData.fence = m_CurrentFrameContext.giDataFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(giData));

				VansFrameSubmitNode graphicsScreen;
				graphicsScreen.name = "GraphicsScreen";
				graphicsScreen.queue = VansQueueRole::Compute;
				graphicsScreen.commandBuffers = { m_VansVKGraphicsScreenCommandBuffer.GetVKCommandBuffer() };
				// HZB/RT/GI/TileLight 已在同一 compute queue 上有序完成；这里只等待
				// Graphics queue 生产的 SSAO raw，避免让重型 compute 相互竞争。
				graphicsScreen.waits = {
					{ VansSyncPoint::SSAORawReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT }
				};
				graphicsScreen.signals = { VansSyncPoint::ScreenLightingReady };
				graphicsScreen.resources = {
					{ "GBufferData", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, false, false },
					{ "Depth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, false, false },
					{ "CascadeShadowMap", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "PunctualShadowDebugPreview", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false },
					{ "HZB", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "TileLightLists", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, false, false },
					{ "CascadeShadowMinMax", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false },
					{ "ScreenLightingData", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false },
					{ "SSAORaw", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, false, false },
					{ "SSAO", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, false, false },
					{ "AtmosphereTransmittance", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "AtmosphereMultiScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "AtmosphereSkyView", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "PreConvolvedDiffuseEnvironment", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "CloudShadow", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "LocalMediaInjection", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "LocalMediaScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false },
					{ "LocalMediaOpticalDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, true, true, false }
				};
				graphicsScreen.fence = m_CurrentFrameContext.graphicsScreenFence;
				m_FrameSubmitOrchestrator.AddNode(std::move(graphicsScreen));

				VansFrameSubmitNode graphicsMain;
				graphicsMain.name = "GraphicsMain";
				graphicsMain.queue = VansQueueRole::Graphics;
				graphicsMain.commandBuffers = { m_VansVKCommandBuffer.GetVKCommandBuffer() };
				graphicsMain.waits = {
					{ VansSyncPoint::ScreenLightingReady, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT },
					{ VansSyncPoint::HairShadowReady, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT }
				};
				graphicsMain.externalWaits = {
					{ m_CurrentFrameContext.imageAcquiredSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }
				};
				graphicsMain.externalSignals = { m_CurrentFrameContext.renderFinishedSemaphore };
				graphicsMain.resources = {
					{ "GBufferData", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, false, false },
					{ "Depth", VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
						VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
						true, true, false, false },
					{ "CascadeShadowMap", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "PunctualShadowAtlas0", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "PunctualShadowAtlas1", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "HairDeepOpacity", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, false, true, false },
					{ "TileLightLists", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_UNDEFINED, false, false, false, false },
					{ "AtmosphereTransmittance", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "AtmosphereMultiScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "AtmosphereSkyView", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "AtmosphereAerialScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "AtmosphereAerialClearScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "AtmosphereAerialOpticalDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
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
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "LocalMediaInjection", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "LocalMediaScattering", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "LocalMediaOpticalDepth", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "GIData", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, true, false },
					{ "SSAO", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, false, false },
					{ "ScreenLightingData", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
						VK_IMAGE_LAYOUT_GENERAL, true, false, false, false },
					{ "SceneColor", VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true, true, true, false }
				};
				graphicsMain.fence = m_CurrentFrameContext.graphicsFence;
				graphicsMain.waitForCompletion = true;
				m_FrameSubmitOrchestrator.AddNode(std::move(graphicsMain));

				m_CurrentFrameContext.frameSubmitSucceeded = m_FrameSubmitOrchestrator.Execute();
				if (m_CurrentFrameContext.frameSubmitSucceeded)
					NotifyPunctualShadowJobsSubmitted();
				if (!m_CurrentFrameContext.frameSubmitSucceeded)
				{
					VANS_LOG_ERROR("[VansVKDevice] Async frame graph submit failed: " << m_FrameSubmitOrchestrator.GetLastError());
					ResetAsyncFrameCommandBuffersAfterFailure();
					return;
				}
			}
			{
				VANS_PROFILE_SCOPE("Vulkan::ResetCommandBuffer.CB2", Vans::ProfileCategory::VulkanSubmit);
				if (!m_VansVKCommandBuffer.ResetCommandBuffer(false))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to reset graphics CB2 command buffer.");
				}
			}

			{
				VANS_PROFILE_SCOPE("Vulkan::ResetAsyncFrameCommandBuffers", Vans::ProfileCategory::VulkanSubmit);
				std::vector<VkFence> submittedFences = {
					m_CurrentFrameContext.shadowMapsFence,
					m_CurrentFrameContext.hairShadowFence,
					m_CurrentFrameContext.gbufferFence,
					m_CurrentFrameContext.gbufferMaterialFence,
					m_CurrentFrameContext.ssaoRawFence,
					m_CurrentFrameContext.graphicsScreenFence,
					m_CurrentFrameContext.vegetationFence,
					m_CurrentFrameContext.earlyAuxFence,
					m_CurrentFrameContext.asyncAtmosphereFence,
					m_CurrentFrameContext.asyncHZBFence,
					m_CurrentFrameContext.rayTracingFence,
					m_CurrentFrameContext.giDataFence
				};
				if (VansGraphics::vkResetFences(
					m_VansVKLogicDevice,
					static_cast<uint32_t>(submittedFences.size()),
					submittedFences.data()) != VK_SUCCESS)
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to reset async frame fences.");
				}

				auto resetCommandBuffer = [&](const char* name, VansVKCommandBuffer& commandBuffer)
				{
					if (commandBuffer.ResetCommandBuffer(false))
						return;
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to reset " << name << " command buffer.");
				};
				resetCommandBuffer("shadow maps", m_VansVKShadowMapsCommandBuffer);
				resetCommandBuffer("hair-shadow", m_VansVKHairShadowCommandBuffer);
				resetCommandBuffer("GBuffer", m_VansVKGBufferCommandBuffer);
				resetCommandBuffer("GBuffer-material", m_VansVKGBufferMaterialCommandBuffer);
				resetCommandBuffer("SSAO-raw", m_VansVKSSAORawCommandBuffer);
				resetCommandBuffer("graphics-screen", m_VansVKGraphicsScreenCommandBuffer);
				resetCommandBuffer("vegetation", m_VansVKVegetationCommandBuffer);
				resetCommandBuffer("early auxiliary", m_VansVKEarlyAuxCommandBuffer);
				resetCommandBuffer("async atmosphere", m_VansVKAsyncAtmosphereCommandBuffer);
				resetCommandBuffer("async HZB", m_VansVKAsyncHZBCommandBuffer);
				resetCommandBuffer("ray-tracing", m_VansVKRayTracingCommandBuffer);
				resetCommandBuffer("GI-data", m_VansVKGIDataCommandBuffer);
			}
			if (!ResetGBufferSecondaryCommandBuffersIfNeeded())
			{
				m_CurrentFrameContext.frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Failed to reset GBuffer secondary command buffers.");
			}
		}

		if (!m_CurrentFrameContext.frameSubmitSucceeded)
		{
			VANS_LOG_ERROR("[VansVKDevice] Skipping present because frame submit/reset failed.");
			return;
		}

		// Probe capture samples the directional shadow map. Run it only after this
		// frame's graphics work has completed, while the map and its matching light
		// matrices are still in SHADER_READ_ONLY_OPTIMAL.
		if (m_Scene->IsSceneReady()
			&& IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::ReflectionProbeBakeQueue))
		{
			if (IsFrameContextRingActive() && m_ActiveFrameContextSlot != nullptr)
			{
				// ReflectionProbe bake samples this frame's shadow map. Wait for GPU completion here,
				// but keep the frame slot pending until the next reuse so present synchronization
				// and per-slot deferred deletes remain intact.
				if (!WaitForFrameContextRingSlotGpuIdle(*m_ActiveFrameContextSlot))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to wait frame slot before reflection probe bake.");
					return;
				}
			}
			static uint32_t reflectionProbeFrame = 0;
			auto* reflectionProbes = m_Scene->GetReflectionProbeSystem();
			const uint32_t probeFrame = reflectionProbeFrame++;
			const uint32_t faceBudget = reflectionProbes->GetBakeFaceBudget();
			for (uint32_t face = 0; face < faceBudget; ++face)
				reflectionProbes->ProcessBakeQueue(*m_Scene, *this, m_ImmediateGraphicsCommandBuffer, probeFrame);
		}

		MaybeDumpGIDebugFrame(VansRenderPassManager::GetInstance());

		{
			VANS_PROFILE_SCOPE("Vulkan::PresentImage", Vans::ProfileCategory::VulkanSubmit);
			const VkResult presentResult = m_VansVKSurface.PresentImage(
				m_VansVKGraphicsQueue,
				{ m_CurrentFrameContext.renderFinishedSemaphore },
				m_SwapChainImageIndex);
			if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR)
			{
				m_CurrentFrameContext.frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Queue present failed. VkResult=" << static_cast<int>(presentResult));
			}
		}

		if (IsFrameContextRingActive())
		{
			m_CurrentFrameContextSlotIndex =
				(m_CurrentFrameContextSlotIndex + 1) % m_ConfiguredFramesInFlight;
		}
	}

	void VansVKDevice::AfterRendering()
	{
		// Noesis IRenderer instances are RT-affine and must be shut down before
		// their render passes and Vulkan device resources are destroyed.
		VansRuntime::VansUISystem::Get().ShutdownRendering();
		const bool frameContextGpuWorkPending = std::any_of(
			m_FrameContextRingSlots.begin(),
			m_FrameContextRingSlots.end(),
			[](const VansFrameContextRingSlot& slot)
			{
				return slot.gpuWorkPending;
			});
		if (m_FrameContextRingResourcesReady && frameContextGpuWorkPending)
			WaitForDevice();
		DestroyParallelCommandRecording();
		DestroyHairLightingDescriptors();
		DestroyHairCompositeDescriptors();
		DestroyTransmissionGlassDescriptors();
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->DestroyRenderPass();
		m_DrawInstanceArena.Destroy(m_VansVKLogicDevice);
		m_MainCameraVisibilityState.ReleaseGpuResources(m_VansVKLogicDevice);
		DestroyLightFrameResources();
		DestroyCameraFrameResources();

		DestroyVKSemaphore(m_SwapChainImageAcquiredSemaphore);
		DestroyVKSemaphore(m_CommandBufferReadyToPresentSemaphore);
		DestroyFrameContextRingResources();
	}

	void VansVKDevice::DrawShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		VANS_PROFILE_SCOPE("RenderRecord::CascadeShadow", Vans::ProfileCategory::CommandRecord);
		m_Scene->DrawShadowNodes();
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
		for (const VansPunctualShadowRenderJob& job : jobs)
		{
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
			m_Scene->DrawPunctualShadowJob(job);
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
		if (!InitializeParallelCommandRecording() || !m_ShadowSecondaryCommandContext || !m_ShadowSecondaryCommandContext->IsReady())
			return false;

		const auto& opaqueNodes = m_Scene->GetOpaqueRenderNodes();
		const size_t opaqueNodeCount = opaqueNodes.size();
		if (opaqueNodeCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		const uint32_t availableSecondaries = m_ShadowSecondaryCommandContext->GetCommandBufferCount();
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
		if (!m_Scene->BuildShadowDrawSubmission(passState, submission))
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
			VansVKCommandBuffer* secondary = m_ShadowSecondaryCommandContext->Get(chunkIndex);
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
			m_ShadowSecondaryCommandContext->ResetAll(false);
			return false;
		}
		for (uint8_t success : chunkSuccess)
		{
			if (!success)
			{
				m_ShadowSecondaryCommandContext->ResetAll(false);
				return false;
			}
		}

		const uint32_t serialSecondaryIndex = opaqueChunkCount;
		VansVKCommandBuffer* serialSecondary = m_ShadowSecondaryCommandContext->Get(serialSecondaryIndex);
		if (serialSecondary == nullptr
			|| !serialSecondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
		{
			m_ShadowSecondaryCommandContext->ResetAll(false);
			return false;
		}
		serialSecondary->SetViewport(0, { passState.viewport });
		serialSecondary->SetScissor(0, { passState.scissor });
		m_Scene->DrawVegetationShadowNode(*serialSecondary, passState);
		m_Scene->DrawTerrainNode(*serialSecondary, passState, true);
		if (!serialSecondary->EndCommandBufferRecord())
		{
			m_ShadowSecondaryCommandContext->ResetAll(false);
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
		m_ShadowSecondaryCommandBuffersNeedReset = true;
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
		if (!InitializeParallelCommandRecording() || !m_SecondaryCommandContext || !m_SecondaryCommandContext->IsReady())
			return false;

		const auto& opaqueNodes = m_Scene->GetOpaqueRenderNodes();
		const size_t opaqueNodeCount = opaqueNodes.size();
		if (opaqueNodeCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		const uint32_t availableSecondaries = m_SecondaryCommandContext->GetCommandBufferCount();
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
			VansVKCommandBuffer* secondary = m_SecondaryCommandContext->Get(chunkIndex);
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
			m_SecondaryCommandContext->ResetAll(false);
			return false;
		}
		for (uint8_t success : chunkSuccess)
		{
			if (!success)
			{
				VANS_LOG_ERROR("[VansVKDevice] GBuffer worker recording failed; falling back to inline recording.");
				m_SecondaryCommandContext->ResetAll(false);
				return false;
			}
		}

		const uint32_t serialSecondaryIndex = opaqueChunkCount;
		VansVKCommandBuffer* serialSecondary = m_SecondaryCommandContext->Get(serialSecondaryIndex);
		if (serialSecondary == nullptr)
		{
			m_SecondaryCommandContext->ResetAll(false);
			return false;
		}
		if (!serialSecondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
		{
			m_SecondaryCommandContext->ResetAll(false);
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
			m_SecondaryCommandContext->ResetAll(false);
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
		{
			VkCommandBuffer primaryCmd = commandBuffer.GetVKCommandBuffer();
			VANS_GPU_SCOPE(primaryCmd, "GBuffer Opaque");
			commandBuffer.ExecuteSecondaryCommandBuffer(executableBuffers);
		}
		commandBuffer.ExecuteSecondaryCommandBuffer(serialExecutableBuffer);
		renderPassManager->EndRenderPass(commandBuffer, m_globalRenderStateData);
		m_GBufferSecondaryCommandBuffersNeedReset = true;
		return true;
	}

	bool VansVKDevice::RecordDecalPassParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex)
	{
		VANS_PROFILE_SCOPE("RenderRecord::DecalParallel", Vans::ProfileCategory::CommandRecord);
		if (!m_EnableParallelCommandRecording || renderPassManager == nullptr || m_Scene == nullptr || !m_Scene->IsSceneReady())
			return false;
		if (!InitializeParallelCommandRecording() || !m_DecalSecondaryCommandContext || !m_DecalSecondaryCommandContext->IsReady())
			return false;

		const auto& decalNodes = m_Scene->GetDecalRenderNodes();
		const size_t decalNodeCount = decalNodes.size();
		if (decalNodeCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		const uint32_t availableSecondaries = m_DecalSecondaryCommandContext->GetCommandBufferCount();
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
			VansVKCommandBuffer* secondary = m_DecalSecondaryCommandContext->Get(chunkIndex);
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
				m_DecalSecondaryCommandContext->ResetAll(false);
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
		m_DecalSecondaryCommandBuffersNeedReset = true;
		return true;
	}

	void VansVKDevice::UpdateAtmosphereStaticLuts(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* atmosphere = m_Scene->GetAtmosphereSystem())
			atmosphere->RecordStaticLutUpdates(
				commandBuffer, m_CurrentFrameContext.frameNumber);
	}

	void VansVKDevice::UpdateCloudShadow(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* clouds = m_Scene->GetVolumetricCloudSystem())
			clouds->RecordShadow(commandBuffer);
	}

	void VansVKDevice::UpdateAtmosphereViewLuts(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* atmosphere = m_Scene->GetAtmosphereSystem())
			atmosphere->RecordViewLutUpdates(commandBuffer);
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
			nearMedia->Record(commandBuffer);
		}
	}

	void VansVKDevice::UpdateVolumetricCloud(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* clouds = m_Scene->GetVolumetricCloudSystem())
			clouds->RecordRayMarch(commandBuffer);
	}

	void VansVKDevice::CompositeAtmosphere(VansVKCommandBuffer& commandBuffer)
	{
		if (!m_Scene)
			return;
		if (auto* atmosphere = m_Scene->GetAtmosphereSystem())
			atmosphere->RecordComposite(commandBuffer);
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
					if (!IsDeferredProbeOnlyDebugOutput(m_CurrentRenderSceneSnapshot))
		{
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
			renderPassManager->GetRawOpaqueSceneColor(),
			renderPassManager->GetOpaqueSceneColor(),
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
			renderPassManager->GetRawOpaqueSceneColor(),
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

		if (renderPassManager == nullptr)
		{
			return;
		}

		VansDescriptorSetLayoutFactory::CreateAndAllocate_HairLighting(
			m_HairLightingPassLayout, m_HairLightingPassSets);

		if (m_HairLightingPassSets.empty())
		{
			return;
		}

		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->BeginDescriptorUpdate();
		descManager->WriteImageDescriptor(
			m_HairLightingPassSets[0],
			HAIR_LIGHTING_BINDING_OIT_HEAD,
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{
				VK_NULL_HANDLE,
				renderPassManager->GetHairOITHead().GetImageView(),
				VK_IMAGE_LAYOUT_GENERAL
			}});
		descManager->WriteBufferDescriptor(
			m_HairLightingPassSets[0],
			HAIR_LIGHTING_BINDING_OIT_NODES,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ renderPassManager->GetHairOITNodeBuffer().GetNativeBuffer(), 0, renderPassManager->GetHairOITNodeBuffer().GetBufferSize() }});
		descManager->WriteBufferDescriptor(
			m_HairLightingPassSets[0],
			HAIR_LIGHTING_BINDING_OIT_COUNTER,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ renderPassManager->GetHairOITCounterBuffer().GetNativeBuffer(), 0, renderPassManager->GetHairOITCounterBuffer().GetBufferSize() }});
		descManager->WriteImageDescriptor(
			m_HairLightingPassSets[0],
			HAIR_LIGHTING_BINDING_DEEP_OPACITY,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				renderPassManager->GetHairDeepOpacity().GetSampler(),
				renderPassManager->GetHairDeepOpacity().GetImageView(),
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
			}});
		descManager->WriteImageDescriptor(
			m_HairLightingPassSets[0],
			HAIR_LIGHTING_BINDING_CASCADE_SHADOW,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				renderPassManager->GetCascadeShadowSampler(),
				renderPassManager->GetCascadeShadowArrayView(),
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
			}});
		descManager->CommitDescriptorUpdates();
		m_HairLightingDescriptorsReady = true;
	}

	void VansVKDevice::ClearHairOITResources(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		if (renderPassManager == nullptr)
			return;

		VansVKImage& headImage = renderPassManager->GetHairOITHead();
		const VkImageLayout oldHeadLayout = headImage.GetImageLayout();
		VkImageMemoryBarrier toClear{};
		toClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toClear.srcAccessMask = (oldHeadLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
		toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toClear.oldLayout = oldHeadLayout;
		toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toClear.image = headImage.GetImage();
		toClear.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		commandBuffer.PipelineBarrier(
			(oldHeadLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			{}, {}, { toClear });

		VkClearColorValue clearHead{};
		clearHead.uint32[0] = 0xffffffffu;
		commandBuffer.ClearColorImage(headImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, clearHead);

		VkImageMemoryBarrier toShader{};
		toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toShader.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toShader.image = headImage.GetImage();
		toShader.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{}, {}, { toShader });
		headImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);

		commandBuffer.FillBuffer(renderPassManager->GetHairOITCounterBuffer().GetNativeBuffer(), 0, sizeof(uint32_t), 0u);
		VkBufferMemoryBarrier counterBarrier{};
		counterBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		counterBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		counterBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		counterBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		counterBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		counterBarrier.buffer = renderPassManager->GetHairOITCounterBuffer().GetNativeBuffer();
		counterBarrier.offset = 0;
		counterBarrier.size = renderPassManager->GetHairOITCounterBuffer().GetBufferSize();
		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{}, { counterBarrier }, {});

		VkBufferMemoryBarrier nodeWriteBarrier{};
		nodeWriteBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		nodeWriteBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		nodeWriteBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		nodeWriteBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		nodeWriteBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		nodeWriteBarrier.buffer = renderPassManager->GetHairOITNodeBuffer().GetNativeBuffer();
		nodeWriteBarrier.offset = 0;
		nodeWriteBarrier.size = renderPassManager->GetHairOITNodeBuffer().GetBufferSize();
		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{}, { nodeWriteBarrier }, {});
	}

	void VansVKDevice::PrepareHairOITForResolve(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		if (renderPassManager == nullptr)
			return;

		VansVKImage& headImage = renderPassManager->GetHairOITHead();
		VkImageMemoryBarrier headBarrier{};
		headBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		headBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		headBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		headBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		headBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		headBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		headBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		headBarrier.image = headImage.GetImage();
		headBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VkBufferMemoryBarrier bufferBarriers[2]{};
		bufferBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufferBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		bufferBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		bufferBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarriers[0].buffer = renderPassManager->GetHairOITNodeBuffer().GetNativeBuffer();
		bufferBarriers[0].offset = 0;
		bufferBarriers[0].size = renderPassManager->GetHairOITNodeBuffer().GetBufferSize();

		bufferBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		bufferBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		bufferBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		bufferBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferBarriers[1].buffer = renderPassManager->GetHairOITCounterBuffer().GetNativeBuffer();
		bufferBarriers[1].offset = 0;
		bufferBarriers[1].size = renderPassManager->GetHairOITCounterBuffer().GetBufferSize();

		commandBuffer.PipelineBarrier(
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{}, { bufferBarriers[0], bufferBarriers[1] }, { headBarrier });
	}

	void VansVKDevice::DestroyHairLightingDescriptors()
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->DestroyDescriptorSet(m_HairLightingPassSets);
		descManager->DestroyDescriptorSetLayout(m_HairLightingPassLayout);
		m_HairLightingDescriptorsReady = false;
	}

	void VansVKDevice::SetupHairCompositeDescriptors(VansRenderPassManager* renderPassManager)
	{
		DestroyHairCompositeDescriptors();

		if (renderPassManager == nullptr)
		{
			return;
		}

		VansDescriptorSetLayoutFactory::CreateAndAllocate_HairComposite(
			m_HairCompositePassLayout, m_HairCompositePassSets);

		if (m_HairCompositePassSets.empty())
		{
			return;
		}

		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->BeginDescriptorUpdate();
		descManager->WriteImageDescriptor(
			m_HairCompositePassSets[0],
			HAIR_COMP_BINDING_COLOR,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				renderPassManager->GetHairColor().GetSampler(),
				renderPassManager->GetHairColor().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
		descManager->CommitDescriptorUpdates();
		m_HairCompositeDescriptorsReady = true;
	}

	void VansVKDevice::DestroyHairCompositeDescriptors()
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->DestroyDescriptorSet(m_HairCompositePassSets);
		descManager->DestroyDescriptorSetLayout(m_HairCompositePassLayout);
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
			m_TransmissionGlassPassLayout, m_TransmissionGlassPassSets);

		if (m_TransmissionGlassPassSets.empty())
		{
			return;
		}

		auto* descManager = VansVKDescriptorManager::GetInstance();
		VansTexture* ssrReflection = nullptr;
		if (m_Scene != nullptr && m_Scene->GetMaterialManager() != nullptr)
		{
			ssrReflection = m_Scene->GetMaterialManager()->GetRuntimeRenderTexture(VansMaterialManager::RT_SSRAA_RESULT);
		}
		descManager->BeginDescriptorUpdate();
		descManager->WriteImageDescriptor(
			m_TransmissionGlassPassSets[0],
			TRANSMISSION_GLASS_BINDING_OPAQUE_SCENE_COLOR,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				renderPassManager->GetOpaqueSceneColor().GetSampler(),
				renderPassManager->GetOpaqueSceneColor().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
		descManager->WriteImageDescriptor(
			m_TransmissionGlassPassSets[0],
			TRANSMISSION_GLASS_BINDING_SSR_REFLECTION,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				ssrReflection != nullptr ? ssrReflection->GetImage().GetSampler() : renderPassManager->GetOpaqueSceneColor().GetSampler(),
				ssrReflection != nullptr ? ssrReflection->GetImage().GetImageView() : renderPassManager->GetOpaqueSceneColor().GetImageView(),
				ssrReflection != nullptr ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
		descManager->WriteImageDescriptor(
			m_TransmissionGlassPassSets[0],
			TRANSMISSION_GLASS_BINDING_OPAQUE_DEPTH,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				renderPassManager->GetDepth().GetSampler(),
				renderPassManager->GetDepth().GetImageView(),
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
			}});
		descManager->WriteImageDescriptor(
			m_TransmissionGlassPassSets[0],
			TRANSMISSION_GLASS_BINDING_CASCADE_SHADOW,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				renderPassManager->GetCascadeShadowSampler(),
				renderPassManager->GetCascadeShadowArrayView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
		descManager->WriteImageDescriptor(
			m_TransmissionGlassPassSets[0],
			TRANSMISSION_GLASS_BINDING_PUNCTUAL_SHADOW,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			renderPassManager->GetPunctualShadowDescriptorInfos());
		descManager->CommitDescriptorUpdates();
		m_TransmissionGlassDescriptorsReady = true;
	}

	void VansVKDevice::DestroyTransmissionGlassDescriptors()
	{
		auto* descManager = VansVKDescriptorManager::GetInstance();
		descManager->DestroyDescriptorSet(m_TransmissionGlassPassSets);
		descManager->DestroyDescriptorSetLayout(m_TransmissionGlassPassLayout);
		m_TransmissionGlassDescriptorsReady = false;
	}

		void VansVKDevice::DrawHairLighting(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
		{
			if (!m_HairLightingDescriptorsReady || m_HairLightingPassSets.empty())
			{
				return;
			}

			VansGraphicsShader* shader = VansShaderManager::Get().FindGraphicsShader("HairLighting");
			if (shader == nullptr)
			{
				return;
			}

			m_globalRenderStateData.vertexInputBindingDescriptions = nullptr;
			m_globalRenderStateData.vertexInputAttributeDescriptions = nullptr;

			std::vector<VkDescriptorSetLayout> layouts = { m_Scene->GetGlobalDescriptorSetLayout(), m_HairLightingPassLayout };
			std::vector<VkDescriptorSet> sets = { m_Scene->GetGlobalDescriptorSet(), m_HairLightingPassSets[0] };

			commandBuffer.EnsureGraphicsShader(*shader, m_globalRenderStateData, layouts);
			commandBuffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *shader, 0, sets, {});
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
		std::vector<VkDescriptorSet> sets = { m_Scene->GetGlobalDescriptorSet(), m_HairCompositePassSets[0] };

		commandBuffer.EnsureGraphicsShader(*shader, m_globalRenderStateData, layouts);
		commandBuffer.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *shader, 0, sets, {});
		commandBuffer.BindGraphicsPipeline(*shader->GetGraphicsPipeline());
		commandBuffer.Draw(3, 1, 0, 0);
	}

}


