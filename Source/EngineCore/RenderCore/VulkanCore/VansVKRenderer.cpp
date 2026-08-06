#include "VansVKDevice.h"
#include "VansRenderPass.h"
#include "VansRenderPassCatalog.h"
#include "VansRenderGraphVulkanSync.h"
#include "VansVKDescriptorManager.h"
#include "VansDescriptorSetLayouts.h"
#include "VansVKSecondaryCommandContext.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansShaderManager.h"
#include "../WaterCore/VansWaterSystem.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansJobSystem.h"
#include "../../Util/VansProfiler.h"
#include "../../VansTimer.h"
#include "../../RuntimeCore/VansFramePhase.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RuntimeUI/Public/VansUISystem.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <thread>
#include <utility>

namespace VansGraphics
{
	extern PFN_vkWaitForFences vkWaitForFences;

	namespace
	{
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
			&& m_MotionVectorSecondaryCommandContext && m_MotionVectorSecondaryCommandContext->IsReady()
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
		if ((!m_MotionVectorSecondaryCommandContext || !m_MotionVectorSecondaryCommandContext->IsReady())
			&& !createContext(m_MotionVectorSecondaryCommandContext, "MotionVector"))
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
		if (m_MotionVectorSecondaryCommandContext)
		{
			m_MotionVectorSecondaryCommandContext->Destroy(m_VansVKLogicDevice);
			m_MotionVectorSecondaryCommandContext.reset();
		}
		if (m_DecalSecondaryCommandContext)
		{
			m_DecalSecondaryCommandContext->Destroy(m_VansVKLogicDevice);
			m_DecalSecondaryCommandContext.reset();
		}
		m_ShadowSecondaryCommandBuffersNeedReset = false;
		m_MotionVectorSecondaryCommandBuffersNeedReset = false;
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
		resetIfNeeded(m_MotionVectorSecondaryCommandContext, m_MotionVectorSecondaryCommandBuffersNeedReset);
		resetIfNeeded(m_SecondaryCommandContext, m_GBufferSecondaryCommandBuffersNeedReset);
		resetIfNeeded(m_DecalSecondaryCommandContext, m_DecalSecondaryCommandBuffersNeedReset);
		return result;
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

		// FSR targets the Scene viewport rather than the swapchain. Before the
		// first viewport measurement, keep the swapchain extent as a fallback.
		if (m_FSRMode == VansFSRMode::MatchViewport &&
			m_RequestedSceneViewportExtent.width == 0)
		{
			m_RequestedSceneViewportExtent = newDisplayExtent;
			m_FSRConfigDirty = true;
		}

		VANS_LOG("OnWindowResize: display=" << newDisplayExtent.width << "x" << newDisplayExtent.height
			<< "  render=" << m_RenderWidth << "x" << m_RenderHeight);
	}

	void VansVKDevice::BeforeRendering()
	{
		CreateVKSemaphore(m_SwapChainImageAcquiredSemaphore);
		CreateVKSemaphore(m_CommandBufferReadyToPresentSemaphore);
		CreateVKSemaphore(m_ShadowDoneSemaphore);
		CreateVKSemaphore(m_GBufferDoneSemaphore);
		CreateVKSemaphore(m_AsyncComputeDoneSemaphore);

		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->SetupVansDeferredRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansPunctualShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansMotionVectorRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, { m_RenderWidth, m_RenderHeight });
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
		// FSR must be initialised regardless so that VansSceneWindow has a valid
		// image object (even if its contents are black).
		InitializeFSR();
		PrepareFSRDispatchInputData(3.14f / 2, 0.01f, 100.0f);

		// Scene UI pass 必须在 FSR 初始化之后创建，此时 FSR 输出图像已存在。
		renderPassManager->SetupVansSceneUIRenderPass(
			m_VansVKLogicDevice,
			m_FSRController.GetTempFSRImage().GetImageView(),
			m_FSRController.GetDisplayExtent());

		// 鍒濆鍖栬繍琛屾椂 UI 瀛愮郴缁燂紙Noesis锛夛紝鍦?Vulkan 璁惧鍜屾覆鏌撻€氶亾鍏ㄩ儴灏辩华鍚庤皟鐢?
		{
			VansRuntime::VansUIInitDesc uiDesc{};
			uiDesc.m_Width  = m_FSRController.GetDisplayExtent().width;
			uiDesc.m_Height = m_FSRController.GetDisplayExtent().height;
			VansRuntime::VansUISystem::Get().InitializeWithDevice(uiDesc, this);
		}
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
		m_CurrentFrameContext.shadowSubmitted = false;
		m_CurrentFrameContext.gbufferSubmitted = false;
		m_CurrentFrameContext.asyncComputeSubmitted = false;

		VansRenderPassCatalog::BuildCompatibilityFramePlan(
			m_CurrentFramePlan,
			*m_Scene,
			m_CurrentFrameContext.frameNumber);

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
			m_CurrentBarrierPlan = VansRenderGraphBarrierPlanner::BuildBarrierPlan(m_CurrentCompiledRenderGraph);
			m_CurrentVulkanSyncPlan = VansRenderGraphVulkanSyncMapper::BuildSyncPlan(m_CurrentBarrierPlan);

			std::vector<std::string> requiredFeatures;
			std::vector<std::string> conditionallyDisabledFeatures;
			VansRenderPassCatalog::GetPreservedFeatureAuditList(
				*m_Scene,
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
			&& !m_UseAsyncCompute
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
		m_CurrentFrameContext.graphicsCmd = &m_VansVKCommandBuffer;
		m_CurrentFrameContext.shadowCmd = m_UseAsyncCompute ? &m_VansVKShadowCommandBuffer : &m_VansVKCommandBuffer;
		m_CurrentFrameContext.gbufferCmd = m_UseAsyncCompute ? &m_VansVKGBufferCommandBuffer : &m_VansVKCommandBuffer;
		m_CurrentFrameContext.asyncComputeCmd = m_UseAsyncCompute ? &m_VansVKRayTracingCommandBuffer : &m_VansVKCommandBuffer;
		m_CurrentFrameContext.imageAcquiredSemaphore = m_SwapChainImageAcquiredSemaphore;
		m_CurrentFrameContext.renderFinishedSemaphore = m_CommandBufferReadyToPresentSemaphore;
		m_CurrentFrameContext.shadowFinishedSemaphore = m_ShadowDoneSemaphore;
		m_CurrentFrameContext.gbufferFinishedSemaphore = m_GBufferDoneSemaphore;
		m_CurrentFrameContext.asyncComputeFinishedSemaphore = m_AsyncComputeDoneSemaphore;
		m_CurrentFrameContext.graphicsFence = m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.shadowFence = m_VansVKShadowCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.gbufferFence = m_VansVKGBufferCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.asyncComputeFence = m_VansVKRayTracingCommandBuffer.m_CommandBufferFinishSubmitFence;
	}

	void VansVKDevice::BindCurrentFrameContextToSlot(VansFrameContextRingSlot& slot)
	{
		m_ActiveFrameContextSlot = &slot;
		m_CurrentFrameContext.frameNumber = slot.frameNumber;
		m_CurrentFrameContext.swapchainImageIndex = slot.swapchainImageIndex;
		m_CurrentFrameContext.graphicsCmd = &slot.graphicsCommandBuffer;
		m_CurrentFrameContext.shadowCmd = &slot.graphicsCommandBuffer;
		m_CurrentFrameContext.gbufferCmd = &slot.graphicsCommandBuffer;
		m_CurrentFrameContext.asyncComputeCmd = &slot.graphicsCommandBuffer;
		m_CurrentFrameContext.imageAcquiredSemaphore = slot.imageAcquiredSemaphore;
		m_CurrentFrameContext.renderFinishedSemaphore =
			slot.swapchainImageIndex < m_SwapchainImageRenderFinishedSemaphores.size()
			? m_SwapchainImageRenderFinishedSemaphores[slot.swapchainImageIndex]
			: VK_NULL_HANDLE;
		m_CurrentFrameContext.shadowFinishedSemaphore = m_ShadowDoneSemaphore;
		m_CurrentFrameContext.gbufferFinishedSemaphore = m_GBufferDoneSemaphore;
		m_CurrentFrameContext.asyncComputeFinishedSemaphore = m_AsyncComputeDoneSemaphore;
		m_CurrentFrameContext.graphicsFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.shadowFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.gbufferFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.asyncComputeFence = slot.graphicsCommandBuffer.m_CommandBufferFinishSubmitFence;
		m_CurrentFrameContext.frameSubmitSucceeded = slot.frameSubmitSucceeded;
		m_CurrentFrameContext.shadowSubmitted = false;
		m_CurrentFrameContext.gbufferSubmitted = false;
		m_CurrentFrameContext.asyncComputeSubmitted = false;
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

	void VansVKDevice::SetFrameContextRingEnabled(bool enabled, uint32_t framesInFlight)
	{
		m_ConfiguredFramesInFlight = std::clamp(framesInFlight, 1u, kMaxFrameContextsInFlight);
		const bool shouldEnable = enabled && m_ConfiguredFramesInFlight > 1;
		if (m_EnableFrameContextRing == shouldEnable && (!shouldEnable || m_FrameContextRingResourcesReady))
			return;

		WaitForDevice();
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

		if (!m_Scene->IsSceneReady())
		{
			VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);

			// No scene loaded yet 鈥?begin the command buffer so the UI render
			// pass (recorded by DrawEditorWindows) can still be appended.
			// Present() will end the recording and submit.
			if (!CurrentGraphicsCommandBuffer().BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
			{
				m_CurrentFrameContext.frameSubmitSucceeded = false;
				VANS_LOG_ERROR("[VansVKDevice] Failed to begin main command buffer without scene.");
			}
			return;
		}

		VANS_SET_FRAME_PHASE(VansFramePhase::RenderPrep);
		m_Scene->UpdateSceneData();
		ProcessPendingGISettings();
		VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);

		auto renderPassManager = VansRenderPassManager::GetInstance();
		BuildCurrentRenderFramePlan(renderPassManager);

		if (!m_UseAsyncCompute)
		{
			VansVKCommandBuffer& frameGraphicsCommandBuffer = CurrentGraphicsCommandBuffer();
			m_pActiveCommandBuffer = &frameGraphicsCommandBuffer;
			// 鈹€鈹€ Original single-submit path 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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

			// 褰曞埗鏈抚瑙嗛绾圭悊涓婁紶锛屽悎骞跺埌涓诲浘褰㈡彁浜わ紝閬垮厤 Video::TickAll 鍚屾绛夊緟銆?
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::VideoTextureUpload,
				[&]() { m_Scene->RecordVideoUploads(frameGraphicsCommandBuffer); });

			// Upload cloth simulation results from staging buffers to device-local vertex buffers
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::ClothVertexUpload,
				[&]()
				{
					VANS_PROFILE_SCOPE("Vulkan::RecordClothVertexUploads", Vans::ProfileCategory::CommandRecord);
					m_Scene->RecordClothVertexUploads(frameGraphicsCommandBuffer);
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
			// 重置本帧 GPU Profiler 查询池。
#if VANS_PROFILER_ENABLED
			Vans::VansGpuProfiler::Get().BeginFrame(cmd);
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
				if (!m_Scene->GetLightManager()->GetPunctualShadowManager().HasRenderJobs(atlasIndex))
					continue;
				RecordFrameGraphicsPass(
					m_CurrentFramePlan,
					VansRenderPassNames::PunctualShadow,
					"Punctual light Shadow Pass",
					renderPassManager,
					renderPassManager->m_VansPunctualShadowPass,
					frameGraphicsCommandBuffer,
					m_globalRenderStateData,
					[&]() { DrawPunctualShadowMap(renderPassManager, cmd, atlasIndex); },
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
				VansRenderPassNames::MotionVector,
				"Motion Vector Pass",
				cmd,
				[&]()
				{
					if (!RecordMotionVectorPassParallel(renderPassManager, frameGraphicsCommandBuffer))
					{
						RecordFrameGraphicsPassNoGpuScope(
							m_CurrentFramePlan,
							VansRenderPassNames::MotionVector,
							renderPassManager,
							renderPassManager->m_VansMotionVectorPass,
							frameGraphicsCommandBuffer,
							m_globalRenderStateData,
							[&]() { DrawMotionVectorPass(renderPassManager, cmd); });
					}
				});

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
				// 鈽?TileLight Build锛堜緷璧栫浉鏈虹煩闃?+ 鍏夋簮 SSBO锛屽湪 UpdateHZB 鍓嶅畬鎴愶級
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::TileLightBuild, [&]() { BuildTileLightLists(frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::HZB, [&]() { UpdateHZB(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::PunctualShadowDebug, [&]() { UpdatePunctualShadowDebugPreview(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::ScreenSpaceShadow, [&]() { UpdateScreenSpaceShadow(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::RayTracing, [&]() { UpdateRayTracing(frameGraphicsCommandBuffer); });
				// GIPointLight writes the current probe SH before SSGI consumes it.
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
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::GIData, [&]() { UpdateGIData(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::SSR, [&]() { UpdateSSR(renderPassManager, frameGraphicsCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::VolumetricFog, [&]() { UpdateVolumetricFog(renderPassManager, frameGraphicsCommandBuffer); });
				// 体积云 1/4 分辨率光线步进：在 Deferred pass 前完成，结果由 SkyBox.frag 合成。
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::CloudRayMarch, [&]() { UpdateCloudRayMarch(renderPassManager, frameGraphicsCommandBuffer); });

				// 鍗曢槦鍒楄矾寰勪腑锛孲SR / SSGI / Fog 绛?compute 缁撴灉闅忓悗浼氳 Deferred fragment 璇诲彇銆?				// 杩欓噷琛ュ厖 compute shader 鍐欏叆鍒?fragment shader 璇诲彇鐨勬樉寮忓彲瑙佹€т緷璧栥€?
				VkMemoryBarrier computeToFragmentBarrier = {};
				computeToFragmentBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				computeToFragmentBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				computeToFragmentBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				frameGraphicsCommandBuffer.PipelineBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					{ computeToFragmentBarrier });
			}

			// 鈹€鈹€ 璁捐鏂囨。 Pass 6锛欴eferred + SkyBox锛堝啓 SceneColor锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::DeferredSkybox,
				"Deferred Skybox Pass",
				renderPassManager,
				renderPassManager->GetVansDeferredSkyboxPass(),
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSceneDeferredSkybox(renderPassManager, frameGraphicsCommandBuffer); });

			// Custom shaders with depthWrite=true are automatically routed here.
			// This pass writes SceneColor and the main scene depth before water coverage is generated.
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::ForwardOpaqueAfterDeferred,
				"Forward Opaque After Deferred Pass",
				renderPassManager,
				renderPassManager->GetVansForwardOpaqueAfterDeferredPass(),
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawForwardOpaqueAfterDeferredNodes(); });

			// Generate water coverage only after opaque custom materials have populated main depth.
			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterGBuffer))
			{
				auto* waterSys = m_Scene->GetWaterSystem();
				if (waterSys != nullptr)
				{
					auto* camera = m_Scene->GetCamera();
					glm::vec3 camPos = glm::vec3(camera->GetPosition());
					glm::mat4 viewMatrix = camera->GetViewMatrix();
					glm::mat4 vpMatrix = camera->GetProjectiveMatrix() * viewMatrix;
					glm::vec3 mainLightDir = glm::vec3(0.35f, 1.0f, 0.25f);
					glm::vec3 mainLightColor = glm::vec3(1.0f);
					auto& dirLights = m_Scene->GetLightManager()->GetDirectionLights();
					if (!dirLights.empty())
					{
						const auto celestialState = VansLightManager::ComputeCelestialLightingState(dirLights[0]);
						mainLightDir = glm::normalize(celestialState.direction);
						mainLightColor = celestialState.color * celestialState.intensity;
					}
					waterSys->Update(static_cast<float>(VansTimer::GetDeltaTime()), camPos, viewMatrix,
						vpMatrix, mainLightDir, mainLightColor);
					RecordFrameGpuStep(
						m_CurrentFramePlan,
						VansRenderPassNames::WaterWaveCompute,
						"Water Wave Compute",
						cmd,
						[&]()
						{
							waterSys->UpdateWaveSimulation(frameGraphicsCommandBuffer,
								static_cast<float>(VansTimer::GetDeltaTime()));
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
			// Water effects consume the coverage generated against the updated main depth.
			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterPreCompute))
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

			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::HairVisibility))
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

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::HairLighting,
				"Hair Lighting Pass",
				renderPassManager,
				renderPassManager->GetVansHairLightingPass(),
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawHairLighting(renderPassManager, frameGraphicsCommandBuffer); });

			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::ExposureBloom,
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
					UpdateExposure(renderPassManager, frameGraphicsCommandBuffer);
					UpdateBloom(renderPassManager, frameGraphicsCommandBuffer);

					VkMemoryBarrier postProcessComputeToFragment{};
					postProcessComputeToFragment.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					postProcessComputeToFragment.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					postProcessComputeToFragment.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					frameGraphicsCommandBuffer.PipelineBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						{ postProcessComputeToFragment });
				});

			// 鈹€鈹€ 璁捐鏂囨。 Pass 10-12锛歍ransparent + PostProcess锛圠OAD SceneColor锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€
			CopyOpaqueSceneColorForTransmission(renderPassManager, frameGraphicsCommandBuffer);
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::TransparentPostProcess,
				"Transparent PostProcess Pass",
				renderPassManager,
				renderPassManager->m_VansRenderPass,
				frameGraphicsCommandBuffer,
				m_globalRenderStateData,
				[&]()
				{
					if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterGBuffer))
					{
						VANS_GPU_SCOPE(cmd, "Water Composite");
						m_Scene->DrawWaterCompositeNode();
					}
					DrawSceneTransparentPost(renderPassManager, frameGraphicsCommandBuffer);
				});
			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
		}
		else
		{
			// 鈹€鈹€ 0. Async Compute CB (BuildTileLightLists 鈫?Compute Queue) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
			// BuildTileLightLists 鍙緷璧栫浉鏈?+ 鍏夋簮 SSBO锛堝抚寮€濮嬪墠宸蹭笂浼狅級锛?
			// 涓?Shadow / GBuffer 娓叉煋鏃犺祫婧愬啿绐侊紝鍙畬鍏ㄥ苟琛屽埌鐙珛璁＄畻闃熷垪銆?
			// m_VansVKRayTracingCommandBuffer 鍦?m_ComputeQueueFamilyIndex 涓婂垱寤猴紝
			// 鎻愪氦鍒?m_VansVKComputeQueue锛堜笉鍚?QueueFamily锛夛紝NSight 灏嗘樉绀虹涓夋潯闃熷垪銆?
			m_pActiveCommandBuffer = &m_VansVKRayTracingCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordAsyncComputeCB", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKRayTracingCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin async compute command buffer.");
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::TileLightBuild,
					[&]() { BuildTileLightLists(m_VansVKRayTracingCommandBuffer); });
				if (!m_VansVKRayTracingCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end async compute command buffer.");
					m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
			}
			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;  // restore
			{
				VANS_PROFILE_SCOPE("Vulkan::QueueSubmit.Compute", Vans::ProfileCategory::VulkanSubmit);
				m_CurrentFrameContext.asyncComputeSubmitted = m_CurrentFrameContext.frameSubmitSucceeded && VansVKCommandBuffer::SubmitCommands(
					m_VansVKComputeQueue, m_VansVKLogicDevice,
					{ m_VansVKRayTracingCommandBuffer.GetVKCommandBuffer() },
					{}, { m_CurrentFrameContext.asyncComputeFinishedSemaphore },
					m_CurrentFrameContext.asyncComputeFence, false);
				if (!m_CurrentFrameContext.asyncComputeSubmitted)
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Async compute frame submit failed.");
					m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
			}

			// 鈹€鈹€ 1. Shadow CB (m_VansVKShadowCommandBuffer 鈫?m_VansVKShadowQueue) 鈹€鈹€鈹€鈹€鈹€鈹€
			// 娉ㄦ剰锛氭 CB 涓嶄娇鐢?VANS_GPU_SCOPE銆俛sync 璺緞涓?query pool reset 鍦?CB2锛?
			// 鑻?Shadow CB 鍏堝悜 pool 鍐欐椂闂存埑銆丆B2 鍐?reset 閲嶅啓锛孨Sight 浼氬洜
			// query slot 琚悓涓€ queue 閲嶅鍐欏叆锛坮eset 涔嬪墠宸插啓锛夎€岃Е鍙?crash銆?
			// Shadow 涓嶇瓑寰?AsyncCompute semaphore锛歴hadow 浣跨敤涓婁竴甯х殑钂欑毊椤剁偣鏁版嵁锛?
			// 涓?BuildTileLightLists 鏃犺祫婧愪緷璧栵紝鍙笌 AsyncCompute CB 骞惰銆?
			m_pActiveCommandBuffer = &m_VansVKShadowCommandBuffer;
			VkCommandBuffer shadowCmd = m_VansVKShadowCommandBuffer.GetVKCommandBuffer();
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordShadowCB", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKShadowCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin shadow command buffer.");
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
				{
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
								m_VansVKShadowCommandBuffer,
								m_globalRenderStateData,
								[&]() { DrawShadowMap(renderPassManager, shadowCmd); },
								cascade);
						});
				}
				m_globalRenderStateData.cascadeIndex = -1;
				}
				for (uint32_t atlasIndex = 0; atlasIndex < VANS_PUNCTUAL_SHADOW_ATLAS_COUNT; ++atlasIndex)
				{
					if (!m_Scene->GetLightManager()->GetPunctualShadowManager().HasRenderJobs(atlasIndex))
						continue;
					RecordFrameGraphicsPassNoGpuScope(
						m_CurrentFramePlan,
						VansRenderPassNames::PunctualShadow,
						renderPassManager,
						renderPassManager->m_VansPunctualShadowPass,
						m_VansVKShadowCommandBuffer,
						m_globalRenderStateData,
						[&]() { DrawPunctualShadowMap(renderPassManager, shadowCmd, atlasIndex); },
						static_cast<int>(atlasIndex));
				}
				RecordFrameGraphicsPassNoGpuScope(
					m_CurrentFramePlan,
					VansRenderPassNames::HairDeepOpacity,
					renderPassManager,
					renderPassManager->GetVansHairDeepOpacityPass(),
					m_VansVKShadowCommandBuffer,
					m_globalRenderStateData,
					[&]() { m_Scene->DrawHairDeepOpacityNodes(VansShaderManager::Get().FindGraphicsShader("HairDeepOpacity")); });
				if (!m_VansVKShadowCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end shadow command buffer.");
					if (m_CurrentFrameContext.asyncComputeSubmitted)
					{
						VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.asyncComputeFence);
						m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
					}
					m_VansVKShadowCommandBuffer.ResetCommandBuffer(false);
					ResetGBufferSecondaryCommandBuffersIfNeeded();
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
			}
			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;  // restore active CB
			{
				VANS_PROFILE_SCOPE("Vulkan::QueueSubmit.Shadow", Vans::ProfileCategory::VulkanSubmit);
				m_CurrentFrameContext.shadowSubmitted = m_CurrentFrameContext.frameSubmitSucceeded && VansVKCommandBuffer::SubmitCommands(
					m_VansVKShadowQueue, m_VansVKLogicDevice,
					{ m_VansVKShadowCommandBuffer.GetVKCommandBuffer() },
					{}, { m_CurrentFrameContext.shadowFinishedSemaphore },
					m_CurrentFrameContext.shadowFence, false);
				if (!m_CurrentFrameContext.shadowSubmitted)
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Shadow frame submit failed.");
					if (m_CurrentFrameContext.asyncComputeSubmitted)
					{
						VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.asyncComputeFence);
						m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
					}
					m_VansVKShadowCommandBuffer.ResetCommandBuffer(false);
					ResetGBufferSecondaryCommandBuffersIfNeeded();
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
			}

			// 鈹€鈹€ 2. Graphics CB1 (ClothUpload + VegCompute + MotionVec + GBuffer) 鈹€鈹€鈹€鈹€
			// 浣跨敤鐙珛鐨?m_VansVKGBufferCommandBuffer锛岄伩鍏?CB1 鎻愪氦鍚?CPU 绛?fence
			// 鎵嶈兘閲嶇敤 m_VansVKCommandBuffer 褰曞埗 CB2锛堟秷闄?CPU stall锛夈€?
			m_pActiveCommandBuffer = &m_VansVKGBufferCommandBuffer;
			VkCommandBuffer cmd = m_VansVKGBufferCommandBuffer.GetVKCommandBuffer();
			{
				VANS_PROFILE_SCOPE("Vulkan::RecordGBufferCB", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKGBufferCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin GBuffer command buffer.");
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
				// 瑙嗛鍦?GBuffer 涔嬪墠涓婁紶锛岀‘淇濇潗璐ㄩ噰鏍峰埌鏈抚鏂板抚銆?
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::VideoTextureUpload,
					[&]() { m_Scene->RecordVideoUploads(m_VansVKGBufferCommandBuffer); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::ClothVertexUpload,
					[&]() { m_Scene->RecordClothVertexUploads(m_VansVKGBufferCommandBuffer); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::VegetationCompute,
					[&]() { m_Scene->RecordVegetationCompute(m_VansVKGBufferCommandBuffer); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::MainCameraHiZCull,
					[&]() { UpdateMainCameraHiZCull(renderPassManager, m_VansVKGBufferCommandBuffer); });
			// 注意：此 CB 同样不使用 VANS_GPU_SCOPE，原因同 Shadow CB。
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::MotionVector,
				[&]()
				{
					if (!RecordMotionVectorPassParallel(renderPassManager, m_VansVKGBufferCommandBuffer))
					{
						RecordFrameGraphicsPassNoGpuScope(
							m_CurrentFramePlan,
							VansRenderPassNames::MotionVector,
							renderPassManager,
							renderPassManager->m_VansMotionVectorPass,
							m_VansVKGBufferCommandBuffer,
							m_globalRenderStateData,
							[&]() { DrawMotionVectorPass(renderPassManager, cmd); });
					}
				});
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::GBuffer,
				[&]()
				{
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
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::Decal,
				[&]()
				{
					if (!RecordDecalPassParallel(renderPassManager, m_VansVKGBufferCommandBuffer))
					{
						RecordFrameGraphicsPassNoGpuScope(
							m_CurrentFramePlan,
							VansRenderPassNames::Decal,
							renderPassManager,
							renderPassManager->GetVansDecalPass(),
							m_VansVKGBufferCommandBuffer,
							m_globalRenderStateData,
							[&]() { m_Scene->DrawDecalNodes(); });
					}
				});
				if (!m_VansVKGBufferCommandBuffer.EndCommandBufferRecord())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to end GBuffer command buffer.");
					if (m_CurrentFrameContext.asyncComputeSubmitted)
					{
						VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.asyncComputeFence);
						m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
					}
					if (m_CurrentFrameContext.shadowSubmitted)
					{
						VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.shadowFence);
						m_VansVKShadowCommandBuffer.ResetCommandBuffer(false);
					}
					m_VansVKGBufferCommandBuffer.ResetCommandBuffer(false);
					ResetGBufferSecondaryCommandBuffersIfNeeded();
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
			}
			{
				VANS_PROFILE_SCOPE("Vulkan::QueueSubmit.GBuffer", Vans::ProfileCategory::VulkanSubmit);
				m_CurrentFrameContext.gbufferSubmitted = m_CurrentFrameContext.frameSubmitSucceeded && VansVKCommandBuffer::SubmitCommands(
					m_VansVKGraphicsQueue, m_VansVKLogicDevice,
					{ m_VansVKGBufferCommandBuffer.GetVKCommandBuffer() },
					{}, { m_CurrentFrameContext.gbufferFinishedSemaphore },
					m_CurrentFrameContext.gbufferFence, false);
				if (!m_CurrentFrameContext.gbufferSubmitted)
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] GBuffer frame submit failed.");
					if (m_CurrentFrameContext.asyncComputeSubmitted)
					{
						VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.asyncComputeFence);
						m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
					}
					if (m_CurrentFrameContext.shadowSubmitted)
					{
						VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.shadowFence);
						m_VansVKShadowCommandBuffer.ResetCommandBuffer(false);
					}
					m_VansVKGBufferCommandBuffer.ResetCommandBuffer(false);
					ResetGBufferSecondaryCommandBuffersIfNeeded();
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
			}

			// m_VansVKCommandBuffer 尚未提交，无需 CPU fence 等待，直接录制 CB2。
			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
			{
				VANS_PROFILE_SCOPE("Vulkan::BeginCommandBuffer.CB2", Vans::ProfileCategory::CommandRecord);
				m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			}
			cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();
#if VANS_PROFILER_ENABLED
			// BeginFrame 放在 CB2 起点：vkCmdResetQueryPool 与所有 vkCmdWriteTimestamp
			// 均在同一 VkCommandBuffer 句柄（m_VansVKCommandBuffer）内，符合 NSight 要求。
			Vans::VansGpuProfiler::Get().BeginFrame(cmd);
#endif
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::ScreenSpaceEffects,
				"Screen Space Effects Pass",
				renderPassManager,
				renderPassManager->GetVansScreenSpaceEffectsPass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawScreenSpaceFeatureNode(); });

			{
				VANS_GPU_SCOPE(cmd, "Compute Between GBuffer And Deferred");
				// BuildTileLightLists 已移至 Async Compute CB（Step 0）单独提交。
				// CB2 通过 m_AsyncComputeDoneSemaphore 等待其完成，
				// Tile 光源缓冲区的写入可见性由信号量保证。
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::HZB, [&]() { UpdateHZB(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::PunctualShadowDebug, [&]() { UpdatePunctualShadowDebugPreview(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::ScreenSpaceShadow, [&]() { UpdateScreenSpaceShadow(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::RayTracing, [&]() { UpdateRayTracing(m_VansVKCommandBuffer); });
				// GIPointLight writes the current probe SH before SSGI consumes it.
				{
					VkMemoryBarrier giProbeToSSGIBarrier = {};
					giProbeToSSGIBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					giProbeToSSGIBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					giProbeToSSGIBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					m_VansVKCommandBuffer.PipelineBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						{ giProbeToSSGIBarrier });
				}
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::GIData, [&]() { UpdateGIData(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::SSR, [&]() { UpdateSSR(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::VolumetricFog, [&]() { UpdateVolumetricFog(renderPassManager, m_VansVKCommandBuffer); });
				// 体积云 1/4 分辨率光线步进：在 Deferred pass 前完成，结果由 SkyBox.frag 合成。
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::CloudRayMarch, [&]() { UpdateCloudRayMarch(renderPassManager, m_VansVKCommandBuffer); });
				VkMemoryBarrier computeToFragmentBarrier = {};
				computeToFragmentBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				computeToFragmentBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				computeToFragmentBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				m_VansVKCommandBuffer.PipelineBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					{ computeToFragmentBarrier });
			}
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::DeferredSkybox,
				"Deferred Skybox Pass",
				renderPassManager,
				renderPassManager->GetVansDeferredSkyboxPass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSceneDeferredSkybox(renderPassManager, m_VansVKCommandBuffer); });
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::ForwardOpaqueAfterDeferred,
				"Forward Opaque After Deferred Pass",
				renderPassManager,
				renderPassManager->GetVansForwardOpaqueAfterDeferredPass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawForwardOpaqueAfterDeferredNodes(); });

			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterGBuffer))
			{
				auto* waterSys = m_Scene->GetWaterSystem();
				if (waterSys != nullptr)
				{
					auto* camera = m_Scene->GetCamera();
					glm::vec3 camPos = glm::vec3(camera->GetPosition());
					glm::mat4 viewMatrix = camera->GetViewMatrix();
					glm::mat4 vpMatrix = camera->GetProjectiveMatrix() * viewMatrix;
					glm::vec3 mainLightDir = glm::vec3(0.35f, 1.0f, 0.25f);
					glm::vec3 mainLightColor = glm::vec3(1.0f);
					auto& dirLights = m_Scene->GetLightManager()->GetDirectionLights();
					if (!dirLights.empty())
					{
						const auto celestialState = VansLightManager::ComputeCelestialLightingState(dirLights[0]);
						mainLightDir = glm::normalize(celestialState.direction);
						mainLightColor = celestialState.color * celestialState.intensity;
					}
					waterSys->Update(static_cast<float>(VansTimer::GetDeltaTime()), camPos, viewMatrix,
						vpMatrix, mainLightDir, mainLightColor);
				RecordFrameGpuStep(
					m_CurrentFramePlan,
					VansRenderPassNames::WaterWaveCompute,
					"Water Wave Compute",
					cmd,
					[&]()
					{
						waterSys->UpdateWaveSimulation(m_VansVKCommandBuffer,
							static_cast<float>(VansTimer::GetDeltaTime()));
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

			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterPreCompute))
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
			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::HairVisibility))
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

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::HairLighting,
				"Hair Lighting Pass",
				renderPassManager,
				renderPassManager->GetVansHairLightingPass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawHairLighting(renderPassManager, m_VansVKCommandBuffer); });

			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::ExposureBloom,
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
					UpdateExposure(renderPassManager, m_VansVKCommandBuffer);
					UpdateBloom(renderPassManager, m_VansVKCommandBuffer);

					VkMemoryBarrier postProcessComputeToFragment{};
					postProcessComputeToFragment.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
					postProcessComputeToFragment.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
					postProcessComputeToFragment.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
					m_VansVKCommandBuffer.PipelineBarrier(
						VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						{ postProcessComputeToFragment });
				});

			CopyOpaqueSceneColorForTransmission(renderPassManager, m_VansVKCommandBuffer);
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::TransparentPostProcess,
				"Transparent PostProcess Pass",
				renderPassManager,
				renderPassManager->m_VansRenderPass,
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]()
				{
					if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterGBuffer))
					{
						VANS_GPU_SCOPE(cmd, "Water Composite");
						m_Scene->DrawWaterCompositeNode();
					}
					DrawSceneTransparentPost(renderPassManager, m_VansVKCommandBuffer);
				});
		}

		// ── FSR Upscale ─────────────────────────────────────────────────────
		// Dispatch FSR upscale on the current command buffer so the upscaled
		// image is ready before the UI render pass samples it in the editor
		// Scene window.
		RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::FSRRuntimeUI, [&]()
		{
			VANS_PROFILE_SCOPE("Vulkan::RecordFSRAndRuntimeUI", Vans::ProfileCategory::CommandRecord);
			VansVKCommandBuffer& frameGraphicsCommandBuffer = CurrentGraphicsCommandBuffer();
			VkCommandBuffer cmd = frameGraphicsCommandBuffer.GetVKCommandBuffer();
			auto camera = m_Scene->GetCamera();
			m_FSRInput.jitterPixelX = camera->m_JitterPixelX;
			m_FSRInput.jitterPixelY = camera->m_JitterPixelY;
			m_FSRInput.frameTimeDeltaMs = static_cast<float>(
				std::max(VansTimer::GetRealDeltaTime(), 0.0001) * 1000.0);

			m_FSRController.DispatchUpscale(cmd, m_FSRInput);

			// 将 FSR 输出图像从 compute write 转为 color attachment，
			// 供 Noesis 场景 UI 渲染通道（m_VansSceneUIPass）写入。
			VansVKImage& fsrOut = m_FSRController.GetTempFSRImage();
			VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
				frameGraphicsCommandBuffer,
				fsrOut,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				fsrOut.GetImageLayout(),
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

			// ── Noesis 运行时 UI 合成到场景色图 ──────────────────────────
			// 1. 每帧逻辑更新（输入分发、动画推进、绑定刷新）。
			VansRuntime::VansUISystem::Get().Update(
				static_cast<float>(VansGraphics::VansTimer::GetDeltaTime()));

			// 2. 离屏渲染（渐变、效果等），必须在 BeginRenderPass 之前完成。
			VansRuntime::VansUISystem::Get().RenderOffscreen(static_cast<void*>(cmd));

			// 3. 进入场景 UI pass：在 FSR 图像上叠加 Noesis UI。
			//    render pass finalLayout = SHADER_READ_ONLY_OPTIMAL，结束时自动转换。
			BeginSceneUIRenderPass();
			VansRuntime::VansUISystem::Get().RenderDocuments(
				static_cast<void*>(GetSceneUIRenderPassHandle()), 1);
			EndSceneUIRenderPass();
			fsrOut.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			if (m_PresentFSROutputToSwapchain)
				RecordFSROutputToSwapchain();
			// 此时 FSR 图像已处于 SHADER_READ_ONLY_OPTIMAL，ImGui 场景窗口可以直接采样。
		});
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
		if (!m_UseAsyncCompute || !m_Scene->IsSceneReady())
		{
			// ── Single-submit present ───────────────────────────────────────
			std::vector<WaitSemaphoreInfo> wait_semaphore_infos = {
				{ m_CurrentFrameContext.imageAcquiredSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }
			};

			{
				VANS_PROFILE_SCOPE("Vulkan::QueueSubmit.Graphics", Vans::ProfileCategory::VulkanSubmit);
				m_CurrentFrameContext.frameSubmitSucceeded = VansVKCommandBuffer::SubmitCommands(
					m_VansVKGraphicsQueue,
					m_VansVKLogicDevice,
					{ CurrentGraphicsCommandBuffer().GetVKCommandBuffer() },
					wait_semaphore_infos,
					{ m_CurrentFrameContext.renderFinishedSemaphore },
					m_CurrentFrameContext.graphicsFence,
					!IsFrameContextRingActive());
				if (!m_CurrentFrameContext.frameSubmitSucceeded)
				{
					VANS_LOG_ERROR("[VansVKDevice] Graphics frame submit failed.");
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
				|| !m_CurrentFrameContext.shadowSubmitted
				|| !m_CurrentFrameContext.gbufferSubmitted
				|| !m_CurrentFrameContext.asyncComputeSubmitted)
			{
				VANS_LOG_ERROR("[VansVKDevice] Skipping graphics CB2 submit/present because a prerequisite frame submit failed.");
				m_VansVKCommandBuffer.ResetCommandBuffer(false);
				if (m_CurrentFrameContext.shadowSubmitted)
				{
					VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.shadowFence);
					m_VansVKShadowCommandBuffer.ResetCommandBuffer(false);
					if (!m_CurrentFrameContext.gbufferSubmitted)
					{
						ResetGBufferSecondaryCommandBuffersIfNeeded();
					}
				}
				if (m_CurrentFrameContext.gbufferSubmitted)
				{
					VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.gbufferFence);
					m_VansVKGBufferCommandBuffer.ResetCommandBuffer(false);
					ResetGBufferSecondaryCommandBuffersIfNeeded();
				}
				if (m_CurrentFrameContext.asyncComputeSubmitted)
				{
					VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.asyncComputeFence);
					m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
				}
				return;
			}

			// ── Shadow-Parallel + Async Compute present ─────────────────────
			// CB2 waits for: swapchain image acquired + shadow pass done + GBuffer done +
			//               async compute done (BuildTileLightLists on compute queue).
			std::vector<WaitSemaphoreInfo> wait_semaphore_infos = {
				{ m_CurrentFrameContext.imageAcquiredSemaphore,       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
				{ m_CurrentFrameContext.shadowFinishedSemaphore,      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT },
				{ m_CurrentFrameContext.gbufferFinishedSemaphore,     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT          },
				{ m_CurrentFrameContext.asyncComputeFinishedSemaphore,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT          },
			};

			{
				VANS_PROFILE_SCOPE("Vulkan::QueueSubmit.Graphics.CB2", Vans::ProfileCategory::VulkanSubmit);
				m_CurrentFrameContext.frameSubmitSucceeded = VansVKCommandBuffer::SubmitCommands(
					m_VansVKGraphicsQueue,
					m_VansVKLogicDevice,
					{ m_VansVKCommandBuffer.GetVKCommandBuffer() },
					wait_semaphore_infos,
					{ m_CurrentFrameContext.renderFinishedSemaphore },
					m_CurrentFrameContext.graphicsFence);
				if (!m_CurrentFrameContext.frameSubmitSucceeded)
				{
					VANS_LOG_ERROR("[VansVKDevice] Graphics CB2 frame submit failed.");
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

			// 等待 Shadow CB fence，确保下一帧可安全复用该命令缓冲区。
			{
				VANS_PROFILE_WAIT("Vulkan::WaitFence.Shadow");
				if (!VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.shadowFence)
					|| !m_VansVKShadowCommandBuffer.ResetCommandBuffer(false))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to wait/reset shadow command buffer.");
				}
			}

			// CB2 在 GPU 端通过 m_GBufferDoneSemaphore 等待 GBuffer CB；
			// m_VansVKCommandBuffer fence 触发时 GBuffer CB 一定已完成，此处重置安全。
			{
				VANS_PROFILE_WAIT("Vulkan::WaitFence.GBuffer");
				if (!VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.gbufferFence)
					|| !m_VansVKGBufferCommandBuffer.ResetCommandBuffer(false))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to wait/reset GBuffer command buffer.");
				}
				if (!ResetGBufferSecondaryCommandBuffersIfNeeded())
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to reset GBuffer secondary command buffers.");
				}
			}

			// 同理 AsyncCompute CB（m_VansVKRayTracingCommandBuffer）：
			// CB2 已等待 m_AsyncComputeDoneSemaphore，故其 fence 此时必然已触发。
			{
				VANS_PROFILE_WAIT("Vulkan::WaitFence.AsyncCompute");
				if (!VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.asyncComputeFence)
					|| !m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to wait/reset async compute command buffer.");
				}
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
		if (m_FrameContextRingResourcesReady)
			WaitForDevice();
		DestroyParallelCommandRecording();
		DestroyHairLightingDescriptors();
		DestroyHairCompositeDescriptors();
		DestroyTransmissionGlassDescriptors();
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->DestroyRenderPass();

		DestroyVKSemaphore(m_SwapChainImageAcquiredSemaphore);
		DestroyVKSemaphore(m_CommandBufferReadyToPresentSemaphore);
		DestroyVKSemaphore(m_ShadowDoneSemaphore);
		DestroyVKSemaphore(m_GBufferDoneSemaphore);
		DestroyVKSemaphore(m_AsyncComputeDoneSemaphore);
		DestroyFrameContextRingResources();
	}

	void VansVKDevice::DrawShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		VANS_PROFILE_SCOPE("RenderRecord::CascadeShadow", Vans::ProfileCategory::CommandRecord);
		m_Scene->DrawShadowNodes();
		m_Scene->DrawTerrainNode(true);
	}

	void VansVKDevice::DrawMotionVectorPass(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		VANS_PROFILE_SCOPE("RenderRecord::MotionVector", Vans::ProfileCategory::CommandRecord);
		m_Scene->DrawMotionVectorNodes();
		m_Scene->DrawTerrainNode(false, true);
	}

	bool VansVKDevice::RecordMotionVectorPassParallel(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer, int framebufferIndex)
	{
		VANS_PROFILE_SCOPE("RenderRecord::MotionVectorParallel", Vans::ProfileCategory::CommandRecord);
		if (!m_EnableParallelCommandRecording || renderPassManager == nullptr || m_Scene == nullptr || !m_Scene->IsSceneReady())
			return false;
		if (!InitializeParallelCommandRecording() || !m_MotionVectorSecondaryCommandContext || !m_MotionVectorSecondaryCommandContext->IsReady())
			return false;

		const auto& opaqueNodes = m_Scene->GetOpaqueRenderNodes();
		const size_t opaqueNodeCount = opaqueNodes.size();
		if (opaqueNodeCount < static_cast<size_t>(m_MinDrawsPerSecondary * 2u))
			return false;

		const uint32_t availableSecondaries = m_MotionVectorSecondaryCommandContext->GetCommandBufferCount();
		if (availableSecondaries < 3)
			return false;

		VansRenderPassRuntimeInfo runtimeInfo = renderPassManager->GetRenderPassRuntimeInfo(
			renderPassManager->m_VansMotionVectorPass,
			framebufferIndex,
			0);
		if (runtimeInfo.renderPass == VK_NULL_HANDLE || runtimeInfo.framebuffer == VK_NULL_HANDLE)
			return false;

		GlobalStateData passState = m_globalRenderStateData;
		passState.currentRenderPass = runtimeInfo.renderPass;
		passState.currentSubpass = runtimeInfo.subpass;
		passState.viewport = runtimeInfo.viewport;
		passState.scissor = runtimeInfo.scissor;

		for (VansRenderNode* node : opaqueNodes)
		{
			if (node == nullptr || !node->IsEnabled() || node->m_Material == nullptr)
				continue;
			auto* opaque = static_cast<VansCommonRenderNode*>(node);
			VansGraphicsShader* motionVectorShader = node->m_Material->GetPassShader(VansPass::VELOCITY);
			if (!node->PreparePipelineForShader(m_VansVKLogicDevice, passState, motionVectorShader, opaque->m_ShadowDescSetLayouts, opaque->m_ShadowDescSets))
				return false;
		}

		const uint32_t maxOpaqueChunks = availableSecondaries - 1u;
		uint32_t opaqueChunkCount = static_cast<uint32_t>((opaqueNodeCount + m_MinDrawsPerSecondary - 1u) / m_MinDrawsPerSecondary);
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
		const size_t chunkSize = (opaqueNodeCount + opaqueChunkCount - 1u) / opaqueChunkCount;
		for (uint32_t chunkIndex = 0; chunkIndex < opaqueChunkCount; ++chunkIndex)
		{
			const size_t begin = static_cast<size_t>(chunkIndex) * chunkSize;
			const size_t end = (std::min)(opaqueNodeCount, begin + chunkSize);
			VansVKCommandBuffer* secondary = m_MotionVectorSecondaryCommandContext->Get(chunkIndex);
			if (secondary == nullptr)
				return false;
			jobs.emplace_back([this, secondary, passState, inheritanceInfo, begin, end, chunkIndex, &chunkSuccess, &executableBuffers]()
			{
				if (!secondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
					return;
				secondary->SetViewport(0, { passState.viewport });
				secondary->SetScissor(0, { passState.scissor });
				m_Scene->DrawMotionVectorNodeRange(*secondary, passState, begin, end);
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
				m_MotionVectorSecondaryCommandContext->ResetAll(false);
				return false;
			}
		}

		const uint32_t serialSecondaryIndex = opaqueChunkCount;
		VansVKCommandBuffer* serialSecondary = m_MotionVectorSecondaryCommandContext->Get(serialSecondaryIndex);
		if (serialSecondary == nullptr
			|| !serialSecondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
		{
			m_MotionVectorSecondaryCommandContext->ResetAll(false);
			return false;
		}
		serialSecondary->SetViewport(0, { passState.viewport });
		serialSecondary->SetScissor(0, { passState.scissor });
		m_Scene->DrawTerrainNode(*serialSecondary, passState, false, true);
		if (!serialSecondary->EndCommandBufferRecord())
		{
			m_MotionVectorSecondaryCommandContext->ResetAll(false);
			return false;
		}
		executableBuffers[serialSecondaryIndex] = serialSecondary->GetVKCommandBuffer();

		renderPassManager->BeginRenderPass(
			renderPassManager->m_VansMotionVectorPass,
			commandBuffer,
			m_globalRenderStateData,
			framebufferIndex,
			VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
		commandBuffer.ExecuteSecondaryCommandBuffer(executableBuffers);
		renderPassManager->EndRenderPass(commandBuffer, m_globalRenderStateData);
		m_MotionVectorSecondaryCommandBuffersNeedReset = true;
		return true;
	}

	void VansVKDevice::DrawPunctualShadowMap(
		VansRenderPassManager* renderPassManager,
		VkCommandBuffer& cmd,
		uint32_t atlasIndex)
	{
		VANS_PROFILE_SCOPE("RenderRecord::PunctualShadow", Vans::ProfileCategory::CommandRecord);
		VansLightManager* lightManager = m_Scene->GetLightManager();
		if (lightManager == nullptr || m_pActiveCommandBuffer == nullptr)
			return;

		VkClearAttachment clearAttachment{};
		clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		clearAttachment.clearValue.depthStencil = { 1.0f, 0 };
		std::vector<VkClearAttachment> clearAttachments = { clearAttachment };

		const auto& jobs = lightManager->GetPunctualShadowManager().GetRenderJobs();
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

		for (VansRenderNode* node : opaqueNodes)
		{
			if (node == nullptr || !node->IsEnabled() || node->m_Material == nullptr)
				continue;
			auto* opaque = static_cast<VansCommonRenderNode*>(node);
			if (!opaque->m_SupportShadow)
				continue;
			VansGraphicsShader* shadowShader = node->m_Material->GetPassShader(VansPass::SHADOW);
			if (!node->PreparePipelineForShader(m_VansVKLogicDevice, passState, shadowShader, opaque->m_ShadowDescSetLayouts, opaque->m_ShadowDescSets))
				return false;
		}

		const uint32_t maxOpaqueChunks = availableSecondaries - 1u;
		uint32_t opaqueChunkCount = static_cast<uint32_t>((opaqueNodeCount + m_MinDrawsPerSecondary - 1u) / m_MinDrawsPerSecondary);
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

		const size_t chunkSize = (opaqueNodeCount + opaqueChunkCount - 1u) / opaqueChunkCount;
		for (uint32_t chunkIndex = 0; chunkIndex < opaqueChunkCount; ++chunkIndex)
		{
			const size_t begin = static_cast<size_t>(chunkIndex) * chunkSize;
			const size_t end = (std::min)(opaqueNodeCount, begin + chunkSize);
			VansVKCommandBuffer* secondary = m_ShadowSecondaryCommandContext->Get(chunkIndex);
			if (secondary == nullptr)
				return false;
			jobs.emplace_back([this, secondary, passState, inheritanceInfo, begin, end, chunkIndex, &chunkSuccess, &executableBuffers]()
			{
				if (!secondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
					return;
				secondary->SetViewport(0, { passState.viewport });
				secondary->SetScissor(0, { passState.scissor });
				m_Scene->DrawShadowNodeRange(*secondary, passState, begin, end);
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
		m_Scene->DrawHairShadowNodes(*serialSecondary, passState);
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

	void VansVKDevice::DrawSceneForward(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		m_Scene->DrawSkyBoxNode();
		m_Scene->DrawOpaqueNodes();
		renderPassManager->NextSubPass(commandBuffer, m_globalRenderStateData);
		m_Scene->DrawPostProcessNodes();
	}

	void VansVKDevice::DrawSceneGBuffer(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		VANS_PROFILE_SCOPE("RenderRecord::GBuffer", Vans::ProfileCategory::CommandRecord);
		m_Scene->DrawOpaqueNodes(commandBuffer, m_globalRenderStateData);
		m_Scene->DrawTerrainNode(commandBuffer, m_globalRenderStateData);
		m_Scene->DrawVegetationNode(commandBuffer, m_globalRenderStateData);
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

		for (VansRenderNode* node : opaqueNodes)
		{
			if (node == nullptr || !node->IsEnabled())
				continue;
			if (!node->PreparePipelineForDraw(m_VansVKLogicDevice, gbufferState))
			{
				VANS_LOG_ERROR("[VansVKDevice] GBuffer parallel recording disabled for this frame because pipeline warm-up failed.");
				return false;
			}
		}

		const uint32_t maxOpaqueChunks = availableSecondaries - 1u;
		uint32_t opaqueChunkCount = static_cast<uint32_t>((opaqueNodeCount + m_MinDrawsPerSecondary - 1u) / m_MinDrawsPerSecondary);
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

		const size_t chunkSize = (opaqueNodeCount + opaqueChunkCount - 1u) / opaqueChunkCount;
		for (uint32_t chunkIndex = 0; chunkIndex < opaqueChunkCount; ++chunkIndex)
		{
			const size_t begin = static_cast<size_t>(chunkIndex) * chunkSize;
			const size_t end = (std::min)(opaqueNodeCount, begin + chunkSize);
			VansVKCommandBuffer* secondary = m_SecondaryCommandContext->Get(chunkIndex);
			if (secondary == nullptr)
				return false;

			jobs.emplace_back([this, secondary, gbufferState, inheritanceInfo, begin, end, chunkIndex, &chunkSuccess, &executableBuffers]()
			{
				if (!secondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
					return;
				secondary->SetViewport(0, { gbufferState.viewport });
				secondary->SetScissor(0, { gbufferState.scissor });
				m_Scene->DrawOpaqueNodeRange(*secondary, gbufferState, begin, end);
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
		m_Scene->DrawTerrainNode(*serialSecondary, gbufferState);
		m_Scene->DrawVegetationNode(*serialSecondary, gbufferState);
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
		commandBuffer.ExecuteSecondaryCommandBuffer(executableBuffers);
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

		for (VansRenderNode* node : decalNodes)
		{
			if (node == nullptr || !node->IsEnabled())
				continue;
			if (!node->PreparePipelineForDraw(m_VansVKLogicDevice, passState))
				return false;
		}

		uint32_t chunkCount = static_cast<uint32_t>((decalNodeCount + m_MinDrawsPerSecondary - 1u) / m_MinDrawsPerSecondary);
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

		const size_t chunkSize = (decalNodeCount + chunkCount - 1u) / chunkCount;
		for (uint32_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
		{
			const size_t begin = static_cast<size_t>(chunkIndex) * chunkSize;
			const size_t end = (std::min)(decalNodeCount, begin + chunkSize);
			VansVKCommandBuffer* secondary = m_DecalSecondaryCommandContext->Get(chunkIndex);
			if (secondary == nullptr)
				return false;

			jobs.emplace_back([this, secondary, passState, inheritanceInfo, begin, end, chunkIndex, &chunkSuccess, &executableBuffers]()
			{
				if (!secondary->BeginSecondaryCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, inheritanceInfo))
					return;
				secondary->SetViewport(0, { passState.viewport });
				secondary->SetScissor(0, { passState.scissor });
				m_Scene->DrawDecalNodeRange(*secondary, passState, begin, end);
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

	// ============================================================
	// DrawSceneDeferredSkybox — 设计文档 Pass 6
	// Deferred Lighting + SkyBox -> SceneColor. Raw screen-space effects run
	// earlier after GBuffer so their filtered outputs are available here.
	// 在 m_VansDeferredSkyboxPass 内执行（SceneColor CLEAR）。
	// ============================================================
	void VansVKDevice::DrawSceneDeferredSkybox(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		m_Scene->DeferredShading();
		m_Scene->DrawSkyBoxNode();
	}

	// ============================================================
	// DrawSceneTransparentPost — 设计文档 Pass 10-12
	// ForwardOpaqueAfterDeferred + Transparent + Particles + PostProcess（LOAD SceneColor，继承水面合成结果）。
	// 在 m_VansRenderPass 内执行。
	// ============================================================
	void VansVKDevice::DrawSceneTransparentPost(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		DrawHairComposite(renderPassManager, commandBuffer);
		m_Scene->DrawTransParentNodes();
		renderPassManager->NextSubPass(commandBuffer, m_globalRenderStateData);
		m_Scene->DrawPostProcessNodes();
	}

	void VansVKDevice::CopyOpaqueSceneColorForTransmission(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		if (renderPassManager == nullptr || !m_TransmissionGlassDescriptorsReady)
		{
			return;
		}

		VansVKImage& source = renderPassManager->GetColor();
		VansVKImage& target = renderPassManager->GetOpaqueSceneColor();
		const VkImageLayout oldSourceLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		const VkImageLayout oldTargetLayout = target.GetImageLayout();
		const uint32_t mipCount = target.GetImageCreateInfo().mipLevels;
		const VkExtent3D sourceExtent = source.GetImageDimension();

		VkImageMemoryBarrier toCopy[2]{};
		toCopy[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toCopy[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		toCopy[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		toCopy[0].oldLayout = oldSourceLayout;
		toCopy[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		toCopy[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toCopy[0].image = source.GetImage();
		toCopy[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		toCopy[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toCopy[1].srcAccessMask = (oldTargetLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : VK_ACCESS_SHADER_READ_BIT;
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
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			{}, {}, { toShader[0], toShader[1] });

		source.SetTrackedImageLayout(oldSourceLayout);
		target.SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
		descManager->WriteImageDescriptor(
			m_HairLightingPassSets[0],
			HAIR_LIGHTING_BINDING_CASCADE_SHADOW,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{
				renderPassManager->GetCascadeShadowSampler(),
				renderPassManager->GetCascadeShadowArrayView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
			{{
				renderPassManager->GetPunctualShadowMap().GetSampler(),
				renderPassManager->GetPunctualShadowMap().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			}});
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


