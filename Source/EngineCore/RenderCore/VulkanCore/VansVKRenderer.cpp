#include "VansVKDevice.h"
#include "VansRenderPass.h"
#include "VansRenderPassCatalog.h"
#include "VansRenderGraphVulkanSync.h"
#include "VansVKDescriptorManager.h"
#include "VansDescriptorSetLayouts.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansShaderManager.h"
#include "../WaterCore/VansWaterSystem.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include "../../VansTimer.h"
#include "../../RuntimeCore/VansFramePhase.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RuntimeUI/Public/VansUISystem.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <utility>

namespace VansGraphics
{
	namespace
	{
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
		renderPassManager->BeginRenderPass(renderPassManager->m_VansUIPass, m_VansVKCommandBuffer, m_globalRenderStateData, m_SwapChainImageIndex);
	}

	void VansVKDevice::EndUIRenderPass()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->EndRenderPass(m_VansVKCommandBuffer, m_globalRenderStateData);
	}

	VkRenderPass VansVKDevice::GetSceneUIRenderPassHandle()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		return renderPassManager->m_VansSceneUIPass.GetRenderPass();
	}

	void VansVKDevice::BeginSceneUIRenderPass()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		// Scene UI pass 只有一个 framebuffer（索引 0）
		renderPassManager->BeginRenderPass(renderPassManager->m_VansSceneUIPass, m_VansVKCommandBuffer, m_globalRenderStateData, 0);
	}

	void VansVKDevice::EndSceneUIRenderPass()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->EndRenderPass(m_VansVKCommandBuffer, m_globalRenderStateData);
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
		// 贴花 Pass：引用 GBuffer 图像（须在 SetupVansDeferredRenderPass 之后调用）
		renderPassManager->SetupVansDecalRenderPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		// 水面 GBuffer Pass：须在 SetupVansDeferredRenderPass 之后调用（依赖已创建的 m_DepthImage）
		renderPassManager->SetupVansWaterGBufferPass(m_VansVKLogicDevice, { m_RenderWidth, m_RenderHeight });
		// 注：水面 descriptor sets 在场景加载时（VansSceneEnvironmentNodeBuilder::AddWaterNode）调用 SetupDescriptors 完成。
		renderPassManager->SetupVansUIRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, m_VansVKSurface,
			{
				m_VansVKSurface.m_VansVKSwapChainImageExtent.width,
				m_VansVKSurface.m_VansVKSwapChainImageExtent.height
			}
		);

		PrepareRenderingData();
		SetupTransmissionGlassDescriptors(renderPassManager);

		// Scene loading is deferred — done via LoadSceneForRendering() from the
		// editor after the user selects a project and opens a scene file.
		// FSR must be initialised regardless so that VansSceneWindow has a valid
		// image object (even if its contents are black).
		InitializeFSR();
		PrepareFSRDispatchInputData(3.14f / 2, 0.01f, 100.0f);

		// Scene UI pass 必须在 FSR 初始化之后创建，此时 FSR 输出图像已存在
		renderPassManager->SetupVansSceneUIRenderPass(
			m_VansVKLogicDevice,
			m_FSRController.GetTempFSRImage().GetImageView(),
			m_FSRController.GetDisplayExtent());

		// 初始化运行时 UI 子系统（Noesis），在 Vulkan 设备和渲染通道全部就绪后调用
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

		FlushCurrentFrameDeferredDeletes();
		m_CurrentFrameContext.frameNumber = ++m_RenderFrameNumber;
		m_CurrentFrameContext.swapchainImageIndex = m_SwapChainImageIndex;
		m_CurrentFrameContext.graphicsCmd = &m_VansVKCommandBuffer;
		m_CurrentFrameContext.shadowCmd = m_UseAsyncCompute ? &m_VansVKShadowCommandBuffer : &m_VansVKCommandBuffer;
		m_CurrentFrameContext.gbufferCmd = m_UseAsyncCompute ? &m_VansVKGBufferCommandBuffer : &m_VansVKCommandBuffer;
		m_CurrentFrameContext.asyncComputeCmd = m_UseAsyncCompute ? &m_VansVKRayTracingCommandBuffer : &m_VansVKCommandBuffer;
		m_CurrentFrameContext.frameSubmitSucceeded = true;
		m_CurrentFrameContext.shadowSubmitted = false;
		m_CurrentFrameContext.gbufferSubmitted = false;
		m_CurrentFrameContext.asyncComputeSubmitted = false;
		BindCurrentFrameSyncResources();

		VansRenderPassCatalog::BuildCompatibilityFramePlan(
			m_CurrentFramePlan,
			*m_Scene,
			m_CurrentFrameContext.frameNumber);

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

		m_CurrentRenderGraphDebugSummary =
			VansRenderGraphDebugDumper::BuildFramePlanSummary(m_CurrentFramePlan)
			+ VansRenderGraphDebugDumper::BuildCompiledGraphSummary(m_CurrentCompiledRenderGraph)
			+ VansRenderGraphDebugDumper::BuildBarrierPlanSummary(m_CurrentBarrierPlan)
			+ VansRenderGraphVulkanSyncDebugDumper::BuildSyncPlanSummary(m_CurrentVulkanSyncPlan)
			+ VansRenderGraphDebugDumper::BuildFeatureAuditSummary(m_CurrentFeatureAudit);

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
				VANS_LOG_ERROR("RenderGraph compatibility debug summary:\n" << m_CurrentRenderGraphDebugSummary);
			}
		}

		m_CurrentFrameContext.framePlan = m_CurrentFramePlan;
	}

	void VansVKDevice::EnqueueDeferredDelete(std::function<void()> destroy)
	{
		m_CurrentFrameContext.deferredDeletes.Enqueue(std::move(destroy));
		m_CurrentFrameContext.pendingDeferredDeleteCount =
			static_cast<uint64_t>(m_CurrentFrameContext.deferredDeletes.Size());
	}

	void VansVKDevice::FlushCurrentFrameDeferredDeletes()
	{
		const uint64_t deleteCount =
			static_cast<uint64_t>(m_CurrentFrameContext.deferredDeletes.Size());
		m_CurrentFrameContext.lastDeferredDeleteFlushCount = deleteCount;
		if (deleteCount > 0)
		{
			m_CurrentFrameContext.deferredDeletes.Flush();
		}
		m_CurrentFrameContext.pendingDeferredDeleteCount =
			static_cast<uint64_t>(m_CurrentFrameContext.deferredDeletes.Size());
	}

	void VansVKDevice::BindCurrentFrameSyncResources()
	{
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

	void VansVKDevice::Rendering()
	{
		VANS_PROFILE_SCOPE("Vulkan::Rendering", Vans::ProfileCategory::CommandRecord);
		BindCurrentFrameSyncResources();

		bool requireImage = false;
		{
			VANS_PROFILE_SCOPE("Vulkan::AcquireSwapchainImage", Vans::ProfileCategory::CommandRecord);
			requireImage = m_VansVKSurface.AcquireVulkanSwapChainImages(
				m_VansVKLogicDevice,
				m_SwapChainImageIndex,
				m_CurrentFrameContext.imageAcquiredSemaphore);
		}
		if (!requireImage)
		{
			VANS_LOG_ERROR("AcquireVulkanSwapChainImages failed");
		}
		ResetFrameStageUploadAllocator();

		if (!m_Scene->IsSceneReady())
		{
			VANS_SET_FRAME_PHASE(VansFramePhase::GPURecord);

			// No scene loaded yet — begin the command buffer so the UI render
			// pass (recorded by DrawEditorWindows) can still be appended.
			// Present() will end the recording and submit.
			if (!m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
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
			// ── Original single-submit path ─────────────────────────────────
			{
				VANS_PROFILE_SCOPE("Vulkan::BeginCommandBuffer", Vans::ProfileCategory::CommandRecord);
				if (!m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to begin main graphics command buffer.");
					return;
				}
			}
			VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();

			// 录制本帧视频纹理上传，合并到主图形提交，避免 Video::TickAll 同步等待。
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::VideoTextureUpload,
				[&]() { m_Scene->RecordVideoUploads(m_VansVKCommandBuffer); });

			// Upload cloth simulation results from staging buffers to device-local vertex buffers
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::ClothVertexUpload,
				[&]()
				{
					VANS_PROFILE_SCOPE("Vulkan::RecordClothVertexUploads", Vans::ProfileCategory::CommandRecord);
					m_Scene->RecordClothVertexUploads(m_VansVKCommandBuffer);
				});

			// Dispatch vegetation bone-sim + skinning compute passes
			RecordFrameStep(
				m_CurrentFramePlan,
				VansRenderPassNames::VegetationCompute,
				[&]()
				{
					VANS_PROFILE_SCOPE("Vulkan::RecordVegetationCompute", Vans::ProfileCategory::CommandRecord);
					m_Scene->RecordVegetationCompute(m_VansVKCommandBuffer);
				});

			// 重置本帧的 GPU Profiler 查询池
#if VANS_PROFILER_ENABLED
			Vans::VansGpuProfiler::Get().BeginFrame(cmd);
#endif

			{
				VANS_GPU_SCOPE(cmd, "Shadow Pass");
				int cascadeCount = VansConfigration::GetInstance()->GetCascadeCount();
				for (int cascade = 0; cascade < cascadeCount; ++cascade)
				{
					m_globalRenderStateData.cascadeIndex = cascade;
					RecordFrameGraphicsPassNoGpuScope(
						m_CurrentFramePlan,
						VansRenderPassNames::CascadeShadow,
						renderPassManager,
						renderPassManager->m_VansShadowPass,
						m_VansVKCommandBuffer,
						m_globalRenderStateData,
						[&]() { DrawShadowMap(renderPassManager, cmd); },
						cascade);
				}
				m_globalRenderStateData.cascadeIndex = -1;
			}

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::PunctualShadow,
				"Punctual light Shadow Pass",
				renderPassManager,
				renderPassManager->m_VansPunctualShadowPass,
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawPunctualShadowMap(renderPassManager, cmd); });

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::HairDeepOpacity,
				"Hair Deep Opacity Pass",
				renderPassManager,
				renderPassManager->GetVansHairDeepOpacityPass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawHairDeepOpacityNodes(VansShaderManager::Get().FindGraphicsShader("HairDeepOpacity")); });

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::MotionVector,
				"Motion Vector Pass",
				renderPassManager,
				renderPassManager->m_VansMotionVectorPass,
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawMotionVectorPass(renderPassManager, cmd); });

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::GBuffer,
				"GBuffer Pass",
				renderPassManager,
				renderPassManager->m_VansGBufferPass,
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSceneGBuffer(renderPassManager, m_VansVKCommandBuffer); });

			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::Decal,
				"Decal Pass",
				renderPassManager,
				renderPassManager->GetVansDecalPass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawDecalNodes(); });

			{
				VANS_GPU_SCOPE(cmd, "Compute Between GBuffer And Deferred");
				// ★ TileLight Build（依赖相机矩阵 + 光源 SSBO，在 UpdateHZB 前完成）
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::TileLightBuild, [&]() { BuildTileLightLists(m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::HZB, [&]() { UpdateHZB(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::ScreenSpaceShadow, [&]() { UpdateScreenSpaceShadow(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::RayTracing, [&]() { UpdateRayTracing(m_VansVKCommandBuffer); });
				// RayTracing/GIPointLight/GISHUpdate 写入 probe SH；SSGI 随后读取当帧可用的分帧结果。
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
				// 体积云 1/4 分辨率光线步进（Deferred pass 前完成，结果由 SkyBox.frag 合成）
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::CloudRayMarch, [&]() { UpdateCloudRayMarch(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::ExposureBloom,
					[&]()
					{
						UploadPostProcessProfileIfDirty();
						UpdateExposure(renderPassManager, m_VansVKCommandBuffer);
						UpdateBloom(renderPassManager, m_VansVKCommandBuffer);
					});

				// 单队列路径中，SSR / SSGI / Fog 等 compute 结果随后会被 Deferred fragment 读取。
				// 这里补充 compute shader 写入到 fragment shader 读取的显式可见性依赖。
				VkMemoryBarrier computeToFragmentBarrier = {};
				computeToFragmentBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				computeToFragmentBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				computeToFragmentBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				m_VansVKCommandBuffer.PipelineBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					{ computeToFragmentBarrier });
			}

			// ── 设计文档 Pass 6：Deferred + SkyBox（写 SceneColor）────────────────────
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::DeferredSkybox,
				"Deferred Skybox Pass",
				renderPassManager,
				renderPassManager->GetVansDeferredSkyboxPass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSceneDeferredSkybox(renderPassManager, m_VansVKCommandBuffer); });

			// Custom shaders with depthWrite=true are automatically routed here.
			// This pass writes SceneColor and the main scene depth before water coverage is generated.
			RecordFrameGraphicsPass(
				m_CurrentFramePlan,
				VansRenderPassNames::ForwardOpaqueAfterDeferred,
				"Forward Opaque After Deferred Pass",
				renderPassManager,
				renderPassManager->GetVansForwardOpaqueAfterDeferredPass(),
				m_VansVKCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawForwardOpaqueAfterDeferredNodes(); });

			// Generate water coverage only after opaque custom materials have populated main depth.
			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterGBuffer))
			{
				auto* waterSys = m_Scene->GetWaterSystem();
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
						mainLightDir = glm::normalize(dirLights[0].m_Direction);
						mainLightColor = dirLights[0].m_Color * dirLights[0].m_Intensity;
					}
					waterSys->Update(static_cast<float>(VansTimer::GetDeltaTime()), camPos, viewMatrix,
						vpMatrix, mainLightDir, mainLightColor);
				}
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

			// Water effects consume the coverage generated against the updated main depth.
			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterPreCompute))
			{
				VANS_GPU_SCOPE(cmd, "Water Pre-Compute");
				auto* waterSys = m_Scene->GetWaterSystem();
				// 延迟绑定 SSR HZB（首次可用时创建 descriptor set）
				auto* matMgr = m_Scene->GetMaterialManager();
				auto* hzbTex = matMgr ? matMgr->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT) : nullptr;
				if (hzbTex)
					waterSys->EnsureSSRDescriptorSet(&hzbTex->GetImage());
				waterSys->DispatchWaterThicknessCS(m_VansVKCommandBuffer);
				waterSys->DispatchWaterSSSScatterCS(m_VansVKCommandBuffer);
				waterSys->DispatchWaterSSR(m_VansVKCommandBuffer);
				waterSys->DispatchRefractionCS(m_VansVKCommandBuffer);
				waterSys->DispatchCausticsCS(m_VansVKCommandBuffer);
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

			// ── 设计文档 Pass 10-12：Transparent + PostProcess（LOAD SceneColor）────────
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
		else
		{
			// ── 0. Async Compute CB (BuildTileLightLists → Compute Queue) ────────────
			// BuildTileLightLists 只依赖相机 + 光源 SSBO（帧开始前已上传），
			// 与 Shadow / GBuffer 渲染无资源冲突，可完全并行到独立计算队列。
			// m_VansVKRayTracingCommandBuffer 在 m_ComputeQueueFamilyIndex 上创建，
			// 提交到 m_VansVKComputeQueue（不同 QueueFamily），NSight 将显示第三条队列。
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

			// ── 1. Shadow CB (m_VansVKShadowCommandBuffer → m_VansVKShadowQueue) ──────
			// 注意：此 CB 不使用 VANS_GPU_SCOPE。async 路径下 query pool reset 在 CB2，
			// 若 Shadow CB 先向 pool 写时间戳、CB2 再 reset 重写，NSight 会因
			// query slot 被同一 queue 重复写入（reset 之前已写）而触发 crash。
			// Shadow 不等待 AsyncCompute semaphore：shadow 使用上一帧的蒙皮顶点数据，
			// 与 BuildTileLightLists 无资源依赖，可与 AsyncCompute CB 并行。
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
					RecordFrameGraphicsPassNoGpuScope(
						m_CurrentFramePlan,
						VansRenderPassNames::CascadeShadow,
						renderPassManager,
						renderPassManager->m_VansShadowPass,
						m_VansVKShadowCommandBuffer,
						m_globalRenderStateData,
						[&]() { DrawShadowMap(renderPassManager, shadowCmd); },
						cascade);
				}
				m_globalRenderStateData.cascadeIndex = -1;
				}
				RecordFrameGraphicsPassNoGpuScope(
					m_CurrentFramePlan,
					VansRenderPassNames::PunctualShadow,
					renderPassManager,
					renderPassManager->m_VansPunctualShadowPass,
					m_VansVKShadowCommandBuffer,
					m_globalRenderStateData,
					[&]() { DrawPunctualShadowMap(renderPassManager, shadowCmd); });
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
					m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
					return;
				}
			}

			// ── 2. Graphics CB1 (ClothUpload + VegCompute + MotionVec + GBuffer) ────
			// 使用独立的 m_VansVKGBufferCommandBuffer，避免 CB1 提交后 CPU 等 fence
			// 才能重用 m_VansVKCommandBuffer 录制 CB2（消除 CPU stall）。
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
				// 视频在 GBuffer 之前上传，确保材质采样到本帧新帧。
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
			// 注意：此 CB 同样不使用 VANS_GPU_SCOPE，原因同 Shadow CB。
			RecordFrameGraphicsPassNoGpuScope(
				m_CurrentFramePlan,
				VansRenderPassNames::MotionVector,
				renderPassManager,
				renderPassManager->m_VansMotionVectorPass,
				m_VansVKGBufferCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawMotionVectorPass(renderPassManager, cmd); });
			RecordFrameGraphicsPassNoGpuScope(
				m_CurrentFramePlan,
				VansRenderPassNames::GBuffer,
				renderPassManager,
				renderPassManager->m_VansGBufferPass,
				m_VansVKGBufferCommandBuffer,
				m_globalRenderStateData,
				[&]() { DrawSceneGBuffer(renderPassManager, m_VansVKGBufferCommandBuffer); });
			RecordFrameGraphicsPassNoGpuScope(
				m_CurrentFramePlan,
				VansRenderPassNames::Decal,
				renderPassManager,
				renderPassManager->GetVansDecalPass(),
				m_VansVKGBufferCommandBuffer,
				m_globalRenderStateData,
				[&]() { m_Scene->DrawDecalNodes(); });
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
			{
				VANS_GPU_SCOPE(cmd, "Compute Between GBuffer And Deferred");
				// BuildTileLightLists 已移至 Async Compute CB（Step 0）单独提交。
				// CB2 通过 m_AsyncComputeDoneSemaphore 等待其完成，
				// Tile 光源缓冲区的写入可见性由信号量保证。
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::HZB, [&]() { UpdateHZB(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::ScreenSpaceShadow, [&]() { UpdateScreenSpaceShadow(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::RayTracing, [&]() { UpdateRayTracing(m_VansVKCommandBuffer); });
				// RayTracing/GIPointLight/GISHUpdate 写入 probe SH；SSGI 随后读取当帧可用的分帧结果。
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
				// 体积云 1/4 分辨率光线步进（Deferred pass 前完成，结果由 SkyBox.frag 合成）
				RecordFrameStep(m_CurrentFramePlan, VansRenderPassNames::CloudRayMarch, [&]() { UpdateCloudRayMarch(renderPassManager, m_VansVKCommandBuffer); });
				RecordFrameStep(
					m_CurrentFramePlan,
					VansRenderPassNames::ExposureBloom,
					[&]()
					{
						UploadPostProcessProfileIfDirty();
						UpdateExposure(renderPassManager, m_VansVKCommandBuffer);
						UpdateBloom(renderPassManager, m_VansVKCommandBuffer);
					});
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
						mainLightDir = glm::normalize(dirLights[0].m_Direction);
						mainLightColor = dirLights[0].m_Color * dirLights[0].m_Intensity;
					}
					waterSys->Update(static_cast<float>(VansTimer::GetDeltaTime()), camPos, viewMatrix,
						vpMatrix, mainLightDir, mainLightColor);
				}
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

			if (IsFramePassEnabled(m_CurrentFramePlan, VansRenderPassNames::WaterPreCompute))
			{
				VANS_GPU_SCOPE(cmd, "Water Pre-Compute");
				auto* waterSys = m_Scene->GetWaterSystem();
				auto* matMgr = m_Scene->GetMaterialManager();
				auto* hzbTex = matMgr ? matMgr->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT) : nullptr;
				if (hzbTex)
					waterSys->EnsureSSRDescriptorSet(&hzbTex->GetImage());
				waterSys->DispatchWaterThicknessCS(m_VansVKCommandBuffer);
				waterSys->DispatchWaterSSSScatterCS(m_VansVKCommandBuffer);
				waterSys->DispatchWaterSSR(m_VansVKCommandBuffer);
				waterSys->DispatchRefractionCS(m_VansVKCommandBuffer);
				waterSys->DispatchCausticsCS(m_VansVKCommandBuffer);
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
			VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();
			auto camera = m_Scene->GetCamera();
			m_FSRInput.jitterPixelX = camera->m_JitterPixelX;
			m_FSRInput.jitterPixelY = camera->m_JitterPixelY;
			m_FSRInput.frameTimeDeltaMs = static_cast<float>(
				std::max(VansTimer::GetRealDeltaTime(), 0.0001) * 1000.0);

			m_FSRController.DispatchUpscale(cmd, m_FSRInput);

			// 将 FSR 输出图像从 compute write 转为 color attachment，
			// 供 Noesis 场景 UI 渲染通道（m_VansSceneUIPass）写入
			VansVKImage& fsrOut = m_FSRController.GetTempFSRImage();
			VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
				fsrOut,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
				fsrOut.GetImageLayout(),
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

			// ── Noesis 运行时 UI 合成到场景色图 ──────────────────────────
			// 1. 每帧逻辑更新（输入分发、动画推进、绑定刷新）
			VansRuntime::VansUISystem::Get().Update(
				static_cast<float>(VansGraphics::VansTimer::GetDeltaTime()));

			// 2. 离屏渲染（渐变、效果等），必须在 BeginRenderPass 之前完成
			VansRuntime::VansUISystem::Get().RenderOffscreen(static_cast<void*>(cmd));

			// 2. 进入场景 UI pass — 在 FSR 图像上叠加 Noesis UI
			//    render pass finalLayout = SHADER_READ_ONLY_OPTIMAL，结束时自动转换
			BeginSceneUIRenderPass();
			VansRuntime::VansUISystem::Get().RenderDocuments(
				static_cast<void*>(GetSceneUIRenderPassHandle()), 1);
			EndSceneUIRenderPass();
			// 此时 FSR 图像已处于 SHADER_READ_ONLY_OPTIMAL，ImGui 场景窗口可直接采样
		});
	}

	void VansVKDevice::Present()
	{
		if (!m_CurrentFrameContext.frameSubmitSucceeded)
		{
			VANS_LOG_ERROR("[VansVKDevice] Skipping present because frame recording failed before submit.");
			return;
		}

		{
			VANS_PROFILE_SCOPE("Vulkan::EndCommandBuffer", Vans::ProfileCategory::VulkanSubmit);
			if (!m_VansVKCommandBuffer.EndCommandBufferRecord())
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
					{ m_VansVKCommandBuffer.GetVKCommandBuffer() },
					wait_semaphore_infos,
					{ m_CurrentFrameContext.renderFinishedSemaphore },
					m_CurrentFrameContext.graphicsFence);
				if (!m_CurrentFrameContext.frameSubmitSucceeded)
				{
					VANS_LOG_ERROR("[VansVKDevice] Graphics frame submit failed.");
				}
			}
			{
				VANS_PROFILE_SCOPE("Vulkan::ResetCommandBuffer", Vans::ProfileCategory::VulkanSubmit);
				if (!m_VansVKCommandBuffer.ResetCommandBuffer(false))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to reset main graphics command buffer.");
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
				}
				if (m_CurrentFrameContext.gbufferSubmitted)
				{
					VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.gbufferFence);
					m_VansVKGBufferCommandBuffer.ResetCommandBuffer(false);
				}
				if (m_CurrentFrameContext.asyncComputeSubmitted)
				{
					VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.asyncComputeFence);
					m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
				}
				return;
			}

			// ── Shadow-Parallel + Async Compute present ──────────────────────────────
			// CB2 waits for: swapchain image acquired + shadow pass done + GBuffer done +
			//               async compute done (BuildTileLightLists on compute queue).
			std::vector<WaitSemaphoreInfo> wait_semaphore_infos = {
				{ m_CurrentFrameContext.imageAcquiredSemaphore,       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
				{ m_CurrentFrameContext.shadowFinishedSemaphore,      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT         },
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

			// CB2 在 GPU 端通过 m_GBufferDoneSemaphore 等待 GBuffer CB，
			// m_VansVKCommandBuffer fence 触发时 GBuffer CB 一定已完成，此处重置安全。
			{
				VANS_PROFILE_WAIT("Vulkan::WaitFence.GBuffer");
				if (!VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_CurrentFrameContext.gbufferFence)
					|| !m_VansVKGBufferCommandBuffer.ResetCommandBuffer(false))
				{
					m_CurrentFrameContext.frameSubmitSucceeded = false;
					VANS_LOG_ERROR("[VansVKDevice] Failed to wait/reset GBuffer command buffer.");
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
			static uint32_t reflectionProbeFrame = 0;
			auto* reflectionProbes = m_Scene->GetReflectionProbeSystem();
			const uint32_t probeFrame = reflectionProbeFrame++;
			const uint32_t faceBudget = reflectionProbes->GetBakeFaceBudget();
			for (uint32_t face = 0; face < faceBudget; ++face)
				reflectionProbes->ProcessBakeQueue(*m_Scene, *this, m_ImmediateGraphicsCommandBuffer, probeFrame);
		}

		auto renderPassManager = VansRenderPassManager::GetInstance();
		{
			VANS_PROFILE_SCOPE("Vulkan::PresentImage", Vans::ProfileCategory::VulkanSubmit);
			m_VansVKSurface.PresentImage(
				m_VansVKLogicDevice,
				m_VansVKGraphicsQueue,
				{ m_CurrentFrameContext.renderFinishedSemaphore },
				m_SwapChainImageIndex);
		}

		renderPassManager->ResetFrameBufferImageLayout(m_VansVKCommandBuffer, m_VansVKSurface, m_SwapChainImageIndex);
		{
			VANS_PROFILE_SCOPE("Vulkan::ResetFrameBufferImageLayoutSubmit", Vans::ProfileCategory::VulkanSubmit);
			if (!VansVKCommandBuffer::SubmitCommands(
				m_VansVKGraphicsQueue,
				m_VansVKLogicDevice,
				{ m_VansVKCommandBuffer.GetVKCommandBuffer() },
				{},
				{},
				m_CurrentFrameContext.graphicsFence)
				|| !m_VansVKCommandBuffer.ResetCommandBuffer(false))
			{
				VANS_LOG_ERROR("[VansVKDevice] Swapchain image layout reset submit failed.");
			}
		}
	}

	void VansVKDevice::AfterRendering()
	{
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
	}

	void VansVKDevice::DrawShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		m_Scene->DrawShadowNodes();
		m_Scene->DrawTerrainNode(true);
	}

	void VansVKDevice::DrawMotionVectorPass(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		m_Scene->DrawMotionVectorNodes();
		m_Scene->DrawTerrainNode(false, true);
	}

	void VansVKDevice::DrawPunctualShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		VansLightManager* lightManager = m_Scene->GetLightManager();

		auto& pointLights = lightManager->GetPointLights();
		int pointLightCount = static_cast<int>(std::min<size_t>(pointLights.size(), lightManager->GetMaxPointLightCount()));
		for (int lightIndex = 0; lightIndex < pointLightCount; lightIndex++)
		{
			if (pointLights[lightIndex].m_ShadowIndex < 0.0f) continue;
			m_Scene->DrawPointShadow(lightIndex);
		}

		auto& spotLights = lightManager->GetSpotLight();
		int spotLightCount = static_cast<int>(std::min<size_t>(spotLights.size(), lightManager->GetMaxSpotLightCount()));
		for (int lightIndex = 0; lightIndex < spotLightCount; lightIndex++)
		{
			if (spotLights[lightIndex].m_ShadowIndex < 0.0f) continue;
			m_Scene->DrawSpotShadow(pointLightCount, lightIndex);
		}

		auto& rectLights = lightManager->GetRectLights();
		int rectLightCount = static_cast<int>(std::min<size_t>(rectLights.size(), lightManager->GetMaxRectLightCount()));
		for (int lightIndex = 0; lightIndex < rectLightCount; lightIndex++)
		{
			if (rectLights[lightIndex].m_ShadowIndex < 0.0f) continue;
			m_Scene->DrawRectShadow(pointLightCount, spotLightCount, lightIndex);
		}
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
		m_Scene->DrawOpaqueNodes();
		m_Scene->DrawTerrainNode();
		m_Scene->DrawVegetationNode();
	}

	// ============================================================
	// DrawSceneDeferredSkybox — 设计文档 Pass 6
	// ScreenSpaceFeature（SSAO 等） + Deferred Lighting + SkyBox → SceneColor
	// 在 m_VansDeferredSkyboxPass 内执行（SceneColor CLEAR）
	// ============================================================
	void VansVKDevice::DrawSceneDeferredSkybox(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		m_Scene->DrawScreenSpaceFeatureNode();
		m_Scene->DeferredShading();
		m_Scene->DrawSkyBoxNode();
	}

	// ============================================================
	// DrawSceneTransparentPost — 设计文档 Pass 10-12
	// ForwardOpaqueAfterDeferred + Transparent + Particles + PostProcess（LOAD SceneColor，继承水面合成结果）
	// 在 m_VansRenderPass（修改后）内执行
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


