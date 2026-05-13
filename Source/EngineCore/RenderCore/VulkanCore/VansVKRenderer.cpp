#include "VansVKDevice.h"
#include "VansRenderPass.h"
#include "VansVKDescriptorManager.h"
#include "../VansScene.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include "../../VansTimer.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RuntimeUI/Public/VansUISystem.h"
#include <algorithm>
#include <iostream>
#include <fstream>

namespace VansGraphics
{
	void VansVKDevice::BeginUIRenderPass()
	{
		VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->BeginRenderPass(renderPassManager->m_VansUIPass, cmd, m_globalRenderStateData, m_SwapChainImageIndex);
	}

	void VansVKDevice::EndUIRenderPass()
	{
		VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
	}

	VkRenderPass VansVKDevice::GetSceneUIRenderPassHandle()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		return renderPassManager->m_VansSceneUIPass.GetRenderPass();
	}

	void VansVKDevice::BeginSceneUIRenderPass()
	{
		VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();
		auto renderPassManager = VansRenderPassManager::GetInstance();
		// Scene UI pass 只有一个 framebuffer（索引 0）
		renderPassManager->BeginRenderPass(renderPassManager->m_VansSceneUIPass, cmd, m_globalRenderStateData, 0);
	}

	void VansVKDevice::EndSceneUIRenderPass()
	{
		VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
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

		CleanupFSR();
		m_FSRController.InitializeContext(
			m_VansVKLogicDevice, m_VansVKPhysicalDevice,
			m_RenderWidth, m_RenderHeight,
			newDisplayExtent.width, newDisplayExtent.height
		);
		PrepareFSRDispatchInputData(3.14f / 2, 0.01f, 100.0f);

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
		CreateVKFence(false, m_SwapChainImageAcquiredFence);

		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->SetupVansDeferredRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansPunctualShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansMotionVectorRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansUIRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, m_VansVKSurface,
			{
				m_VansVKSurface.m_VansVKSwapChainImageExtent.width,
				m_VansVKSurface.m_VansVKSwapChainImageExtent.height
			}
		);

		PrepareRenderingData();

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

	void VansVKDevice::Rendering()
	{
		bool requireImage = m_VansVKSurface.AcquireVulkanSwapChainImages(m_VansVKLogicDevice, m_SwapChainImageIndex, m_SwapChainImageAcquiredSemaphore, m_SwapChainImageAcquiredFence);
		if (!requireImage)
		{
			VANS_LOG_ERROR("AcquireVulkanSwapChainImages failed");
		}

		if (!m_Scene->IsSceneReady())
		{
			// No scene loaded yet — begin the command buffer so the UI render
			// pass (recorded by DrawEditorWindows) can still be appended.
			// Present() will end the recording and submit.
			m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			return;
		}

		m_Scene->UpdateSceneData();

		auto renderPassManager = VansRenderPassManager::GetInstance();

		if (!m_UseAsyncCompute)
		{
			// ── Original single-submit path ─────────────────────────────────
			m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();

			// Upload cloth simulation results from staging buffers to device-local vertex buffers
			m_Scene->RecordClothVertexUploads(cmd);

			// Dispatch vegetation bone-sim + skinning compute passes
			m_Scene->RecordVegetationCompute(m_VansVKCommandBuffer);

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
					renderPassManager->BeginRenderPass(renderPassManager->m_VansShadowPass, cmd, m_globalRenderStateData, cascade);
					DrawShadowMap(renderPassManager, cmd);
					renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
				}
				m_globalRenderStateData.cascadeIndex = -1;
			}

			{
				VANS_GPU_SCOPE(cmd, "Punctual light Shadow Pass");
				renderPassManager->BeginRenderPass(renderPassManager->m_VansPunctualShadowPass, cmd, m_globalRenderStateData);
				DrawPunctualShadowMap(renderPassManager, cmd);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}

			{
				VANS_GPU_SCOPE(cmd, "Motion Vector Pass");
				renderPassManager->BeginRenderPass(renderPassManager->m_VansMotionVectorPass, cmd, m_globalRenderStateData);
				DrawMotionVectorPass(renderPassManager, cmd);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}

			{
				VANS_GPU_SCOPE(cmd, "GBuffer Pass");
				renderPassManager->BeginRenderPass(renderPassManager->m_VansGBufferPass, cmd, m_globalRenderStateData);
				DrawSceneGBuffer(renderPassManager, m_VansVKCommandBuffer);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}

			{
				VANS_GPU_SCOPE(cmd, "Compute Between GBuffer And Deferred");
				// ★ TileLight Build（依赖相机矩阵 + 光源 SSBO，在 UpdateHZB 前完成）
				BuildTileLightLists(m_VansVKCommandBuffer);
				UpdateHZB(renderPassManager, m_VansVKCommandBuffer);
				UpdateGIData(renderPassManager, m_VansVKCommandBuffer);
				UpdateSSR(renderPassManager, m_VansVKCommandBuffer);
				UpdateRayTracing(m_VansVKCommandBuffer);
				UpdateVolumetricFog(renderPassManager, m_VansVKCommandBuffer);

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

			{
				VANS_GPU_SCOPE(cmd, "Deferred Pass");
				renderPassManager->BeginRenderPass(renderPassManager->m_VansRenderPass, cmd, m_globalRenderStateData);
				DrawSceneDeferredPost(renderPassManager, m_VansVKCommandBuffer);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}
		}
		else
		{
			// ── 0. Async Compute CB (BuildTileLightLists → Compute Queue) ────────────
			// BuildTileLightLists 只依赖相机 + 光源 SSBO（帧开始前已上传），
			// 与 Shadow / GBuffer 渲染无资源冲突，可完全并行到独立计算队列。
			// m_VansVKRayTracingCommandBuffer 在 m_ComputeQueueFamilyIndex 上创建，
			// 提交到 m_VansVKComputeQueue（不同 QueueFamily），NSight 将显示第三条队列。
			m_pActiveCommandBuffer = &m_VansVKRayTracingCommandBuffer;
			m_VansVKRayTracingCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			BuildTileLightLists(m_VansVKRayTracingCommandBuffer);
			m_VansVKRayTracingCommandBuffer.EndCommandBufferRecord();
			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;  // restore
			VansVKCommandBuffer::SubmitCommands(
				m_VansVKComputeQueue, m_VansVKLogicDevice,
				{ m_VansVKRayTracingCommandBuffer.GetVKCommandBuffer() },
				{}, { m_AsyncComputeDoneSemaphore },
				m_VansVKRayTracingCommandBuffer.m_CommandBufferFinishSubmitFence, false);

			// ── 1. Shadow CB (m_VansVKShadowCommandBuffer → m_VansVKShadowQueue) ──────
			// 注意：此 CB 不使用 VANS_GPU_SCOPE。async 路径下 query pool reset 在 CB2，
			// 若 Shadow CB 先向 pool 写时间戳、CB2 再 reset 重写，NSight 会因
			// query slot 被同一 queue 重复写入（reset 之前已写）而触发 crash。
			// Shadow 不等待 AsyncCompute semaphore：shadow 使用上一帧的蒙皮顶点数据，
			// 与 BuildTileLightLists 无资源依赖，可与 AsyncCompute CB 并行。
			m_pActiveCommandBuffer = &m_VansVKShadowCommandBuffer;
			VkCommandBuffer shadowCmd = m_VansVKShadowCommandBuffer.GetVKCommandBuffer();
			m_VansVKShadowCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			{
				int cascadeCount = VansConfigration::GetInstance()->GetCascadeCount();
				for (int cascade = 0; cascade < cascadeCount; ++cascade)
				{
					m_globalRenderStateData.cascadeIndex = cascade;
					renderPassManager->BeginRenderPass(renderPassManager->m_VansShadowPass, shadowCmd, m_globalRenderStateData, cascade);
					DrawShadowMap(renderPassManager, shadowCmd);
					renderPassManager->EndRenderPass(shadowCmd, m_globalRenderStateData);
				}
				m_globalRenderStateData.cascadeIndex = -1;
			}
			{
				renderPassManager->BeginRenderPass(renderPassManager->m_VansPunctualShadowPass, shadowCmd, m_globalRenderStateData);
				DrawPunctualShadowMap(renderPassManager, shadowCmd);
				renderPassManager->EndRenderPass(shadowCmd, m_globalRenderStateData);
			}
			m_VansVKShadowCommandBuffer.EndCommandBufferRecord();
			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;  // restore active CB
			VansVKCommandBuffer::SubmitCommands(
				m_VansVKShadowQueue, m_VansVKLogicDevice,
				{ m_VansVKShadowCommandBuffer.GetVKCommandBuffer() },
				{}, { m_ShadowDoneSemaphore },
				m_VansVKShadowCommandBuffer.m_CommandBufferFinishSubmitFence, false);

			// ── 2. Graphics CB1 (ClothUpload + VegCompute + MotionVec + GBuffer) ────
			// 使用独立的 m_VansVKGBufferCommandBuffer，避免 CB1 提交后 CPU 等 fence
			// 才能重用 m_VansVKCommandBuffer 录制 CB2（消除 CPU stall）。
			m_pActiveCommandBuffer = &m_VansVKGBufferCommandBuffer;
			VkCommandBuffer cmd = m_VansVKGBufferCommandBuffer.GetVKCommandBuffer();
			m_VansVKGBufferCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			m_Scene->RecordClothVertexUploads(cmd);
			m_Scene->RecordVegetationCompute(m_VansVKGBufferCommandBuffer);
			// 注意：此 CB 同样不使用 VANS_GPU_SCOPE，原因同 Shadow CB。
			{
				renderPassManager->BeginRenderPass(renderPassManager->m_VansMotionVectorPass, cmd, m_globalRenderStateData);
				DrawMotionVectorPass(renderPassManager, cmd);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}
			{
				renderPassManager->BeginRenderPass(renderPassManager->m_VansGBufferPass, cmd, m_globalRenderStateData);
				DrawSceneGBuffer(renderPassManager, m_VansVKGBufferCommandBuffer);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}
			m_VansVKGBufferCommandBuffer.EndCommandBufferRecord();
			VansVKCommandBuffer::SubmitCommands(
				m_VansVKGraphicsQueue, m_VansVKLogicDevice,
				{ m_VansVKGBufferCommandBuffer.GetVKCommandBuffer() },
				{}, { m_GBufferDoneSemaphore },
				m_VansVKGBufferCommandBuffer.m_CommandBufferFinishSubmitFence, false);

			// m_VansVKCommandBuffer 尚未提交，无需 CPU fence 等待，直接录制 CB2。
			m_pActiveCommandBuffer = &m_VansVKCommandBuffer;
			m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
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
				UpdateHZB(renderPassManager, m_VansVKCommandBuffer);
				UpdateGIData(renderPassManager, m_VansVKCommandBuffer);
				UpdateSSR(renderPassManager, m_VansVKCommandBuffer);
				UpdateRayTracing(m_VansVKCommandBuffer);
				UpdateVolumetricFog(renderPassManager, m_VansVKCommandBuffer);
				VkMemoryBarrier computeToFragmentBarrier = {};
				computeToFragmentBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				computeToFragmentBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				computeToFragmentBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				m_VansVKCommandBuffer.PipelineBarrier(
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					{ computeToFragmentBarrier });
			}
			{
				VANS_GPU_SCOPE(cmd, "Deferred Pass");
				renderPassManager->BeginRenderPass(renderPassManager->m_VansRenderPass, cmd, m_globalRenderStateData);
				DrawSceneDeferredPost(renderPassManager, m_VansVKCommandBuffer);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}
			// CB2 remains open — FSR Upscale + SceneUI blocks appended by common code after if/else
		}

		// ── FSR Upscale ─────────────────────────────────────────────────────
		// Dispatch FSR upscale on the current command buffer so the upscaled
		// image is ready before the UI render pass samples it in the editor
		// Scene window.
		{
			VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();
			auto camera = m_Scene->GetCamera();
			m_FSRInput.jitterX = camera->m_JitterX;
			m_FSRInput.jitterY = camera->m_JitterY;

			m_FSRController.DispatchUpscale(cmd, m_FSRInput);

			// 将 FSR 输出图像从 compute write 转为 color attachment，
			// 供 Noesis 场景 UI 渲染通道（m_VansSceneUIPass）写入
			VansVKImage& fsrOut = m_FSRController.GetTempFSRImage();
			fsrOut.SetImageMemoryBarrier(
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				{
					fsrOut.GetImage(),
					VK_ACCESS_SHADER_WRITE_BIT,
					VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					fsrOut.GetImageLayout(),
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED,
					VK_IMAGE_ASPECT_COLOR_BIT
				});

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
		}
	}

	void VansVKDevice::Present()
	{
		m_VansVKCommandBuffer.EndCommandBufferRecord();

		// When no scene is loaded, always use the single-submit path because
		// the async-compute command buffer was never recorded/submitted.
		if (!m_UseAsyncCompute || !m_Scene->IsSceneReady())
		{
			// ── Single-submit present ───────────────────────────────────────
			std::vector<WaitSemaphoreInfo> wait_semaphore_infos = {
				{ m_SwapChainImageAcquiredSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }
			};

			VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, wait_semaphore_infos, { m_CommandBufferReadyToPresentSemaphore }, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
			m_VansVKCommandBuffer.ResetCommandBuffer(false);

		}
		else
		{
			// ── Shadow-Parallel + Async Compute present ──────────────────────────────
			// CB2 waits for: swapchain image acquired + shadow pass done + GBuffer done +
			//               async compute done (BuildTileLightLists on compute queue).
			std::vector<WaitSemaphoreInfo> wait_semaphore_infos = {
				{ m_SwapChainImageAcquiredSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
				{ m_ShadowDoneSemaphore,             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT         },
				{ m_GBufferDoneSemaphore,            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT          },
				{ m_AsyncComputeDoneSemaphore,       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT          },
			};

			VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, wait_semaphore_infos, { m_CommandBufferReadyToPresentSemaphore }, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
			m_VansVKCommandBuffer.ResetCommandBuffer(false);

			// 等待 Shadow CB fence，确保下一帧可安全复用该命令缓冲区。
			VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_VansVKShadowCommandBuffer.m_CommandBufferFinishSubmitFence);
			m_VansVKShadowCommandBuffer.ResetCommandBuffer(false);

			// CB2 在 GPU 端通过 m_GBufferDoneSemaphore 等待 GBuffer CB，
			// m_VansVKCommandBuffer fence 触发时 GBuffer CB 一定已完成，此处重置安全。
			VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_VansVKGBufferCommandBuffer.m_CommandBufferFinishSubmitFence);
			m_VansVKGBufferCommandBuffer.ResetCommandBuffer(false);

			// 同理 AsyncCompute CB（m_VansVKRayTracingCommandBuffer）：
			// CB2 已等待 m_AsyncComputeDoneSemaphore，故其 fence 此时必然已触发。
			VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_VansVKRayTracingCommandBuffer.m_CommandBufferFinishSubmitFence);
			m_VansVKRayTracingCommandBuffer.ResetCommandBuffer(false);
		}

		auto renderPassManager = VansRenderPassManager::GetInstance();
		m_VansVKSurface.PresentImage(m_VansVKLogicDevice, m_VansVKGraphicsQueue, { m_CommandBufferReadyToPresentSemaphore }, m_SwapChainImageIndex);

		renderPassManager->ResetFrameBufferImageLayout(m_VansVKCommandBuffer, m_VansVKSurface, m_SwapChainImageIndex);
		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
	}

	void VansVKDevice::AfterRendering()
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->DestroyRenderPass();

		DestroyVKSemaphore(m_SwapChainImageAcquiredSemaphore);
		DestroyVKSemaphore(m_CommandBufferReadyToPresentSemaphore);
		DestroyVKSemaphore(m_ShadowDoneSemaphore);
		DestroyVKSemaphore(m_GBufferDoneSemaphore);
		DestroyVKSemaphore(m_AsyncComputeDoneSemaphore);
		DestroyVKFence(m_SwapChainImageAcquiredFence);
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
			m_Scene->DrawPointShadow(lightIndex);
		}

		auto& spotLights = lightManager->GetSpotLight();
		int spotLightCount = static_cast<int>(std::min<size_t>(spotLights.size(), lightManager->GetMaxSpotLightCount()));
		for (int lightIndex = 0; lightIndex < spotLightCount; lightIndex++)
		{
			m_Scene->DrawSpotShadow(pointLightCount, lightIndex);
		}

		auto& rectLights = lightManager->GetRectLights();
		int rectLightCount = static_cast<int>(std::min<size_t>(rectLights.size(), lightManager->GetMaxRectLightCount()));
		for (int lightIndex = 0; lightIndex < rectLightCount; lightIndex++)
		{
			m_Scene->DrawRectShadow(pointLightCount, spotLightCount, lightIndex);
		}
	}

	void VansVKDevice::DrawSceneForward(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		m_Scene->DrawSkyBoxNode();
		m_Scene->DrawOpaqueNodes();
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);
		m_Scene->DrawPostProcessNodes();
	}

	void VansVKDevice::DrawSceneGBuffer(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		m_Scene->DrawOpaqueNodes();
		m_Scene->DrawTerrainNode();
		m_Scene->DrawVegetationNode();
	}

	void VansVKDevice::DrawSceneDeferredPost(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		VkCommandBuffer& cmd = commandBuffer.GetVKCommandBuffer();

		m_Scene->DrawScreenSpaceFeatureNode();

		m_Scene->DeferredShading();
		m_Scene->DrawSkyBoxNode();
		m_Scene->DrawTransParentNodes();
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);
		m_Scene->DrawPostProcessNodes();
	}
}
