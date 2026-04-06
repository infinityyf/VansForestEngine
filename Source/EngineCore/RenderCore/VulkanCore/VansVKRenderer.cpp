#include "VansVKDevice.h"
#include "VansRenderPass.h"
#include "VansVKDescriptorManager.h"
#include "../VansScene.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"
#include "../../ProjectSystem/VansProjectManager.h"
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
		CreateVKEvent(m_AsyncComputeCompletedEvent);
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

			// Reset GPU profiler query pool for this frame
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
				VANS_GPU_SCOPE(cmd, "Motion Vector Pass");
				renderPassManager->BeginRenderPass(renderPassManager->m_VansMotionVectorPass, cmd, m_globalRenderStateData);
				DrawMotionVectorPass(renderPassManager, cmd);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}

			{
				VANS_GPU_SCOPE(cmd, "Post Processing");
				UpdateHZB(renderPassManager, m_VansVKCommandBuffer);
				UpdateGIData(renderPassManager, m_VansVKCommandBuffer);
				UpdateSSR(renderPassManager, m_VansVKCommandBuffer);
				UpdateRayTracing(m_VansVKCommandBuffer);
				UpdateVolumetricFog(renderPassManager, m_VansVKCommandBuffer);
			}

			{
				VANS_GPU_SCOPE(cmd, "Deferred Pass");
				renderPassManager->BeginRenderPass(renderPassManager->m_VansRenderPass, cmd, m_globalRenderStateData);
				DrawSceneDeferred(renderPassManager, m_VansVKCommandBuffer);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}
		}
		else
		{
			// ── Async compute: SSR + VolumetricFog on compute queue ──────────
			// Record and submit COMPUTE FIRST so the GPU starts it before shadow
			// even reaches the graphics queue, guaranteeing true overlap.
			m_VansVKCommandBuffer.ResetEvent(m_AsyncComputeCompletedEvent);

			// Submit 1: SSR + VolumetricFog — queued to compute queue before shadow is even submitted
			m_VansVKComputeCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

			UpdateSSR(renderPassManager, m_VansVKComputeCommandBuffer);
			m_VansVKComputeCommandBuffer.SetEvent(m_AsyncComputeCompletedEvent, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

			m_VansVKComputeCommandBuffer.EndCommandBufferRecord();

			VansVKCommandBuffer::SubmitCommands(
				m_VansVKComputeQueue, m_VansVKLogicDevice,
				{ m_VansVKComputeCommandBuffer.GetVKCommandBuffer() },
				{},
				{},
				m_VansVKComputeCommandBuffer.m_CommandBufferFinishSubmitFence, false);

			// Submit 2: Shadow — submitted AFTER compute is already queued on GPU
			m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();

			{
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
				renderPassManager->BeginRenderPass(renderPassManager->m_VansMotionVectorPass, cmd, m_globalRenderStateData);
				DrawMotionVectorPass(renderPassManager, cmd);
				renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
			}

			UpdateHZB(renderPassManager, m_VansVKCommandBuffer);
			UpdateGIData(renderPassManager, m_VansVKCommandBuffer);
			UpdateVolumetricFog(renderPassManager, m_VansVKCommandBuffer);
			UpdateRayTracing(m_VansVKCommandBuffer);

			// Upload cloth simulation results from staging buffers to device-local vertex buffers
			m_Scene->RecordClothVertexUploads(cmd);

			// Dispatch vegetation bone-sim + skinning compute passes
			m_Scene->RecordVegetationCompute(m_VansVKCommandBuffer);

			renderPassManager->BeginRenderPass(renderPassManager->m_VansRenderPass, cmd, m_globalRenderStateData);
			DrawSceneDeferred(renderPassManager, m_VansVKCommandBuffer);
			renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
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

			// Transition the FSR output image to SHADER_READ_ONLY_OPTIMAL so
			// ImGui can sample it via the Scene-view descriptor set.
			VansVKImage& fsrOut = m_FSRController.GetTempFSRImage();
			fsrOut.SetImageMemoryBarrier(
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{
					fsrOut.GetImage(),
					VK_ACCESS_SHADER_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					fsrOut.GetImageLayout(),
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_QUEUE_FAMILY_IGNORED,
					VK_QUEUE_FAMILY_IGNORED,
					VK_IMAGE_ASPECT_COLOR_BIT
				});
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
			// ── Async compute present ───────────────────────────────────────
			std::vector<WaitSemaphoreInfo> wait_semaphore_infos = {
				{ m_SwapChainImageAcquiredSemaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }
			};

			VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, wait_semaphore_infos, { m_CommandBufferReadyToPresentSemaphore }, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
			m_VansVKCommandBuffer.ResetCommandBuffer(false);

			VansVKCommandBuffer::WaitForFence(m_VansVKLogicDevice, m_VansVKComputeCommandBuffer.m_CommandBufferFinishSubmitFence);
			m_VansVKComputeCommandBuffer.ResetCommandBuffer(false);
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
		DestroyVKEvent(m_AsyncComputeCompletedEvent);
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

		auto pointLights = lightManager->GetPointLights();
		int pointLightCount = pointLights.size();
		for (int lightIndex = 0; lightIndex < pointLightCount; lightIndex++)
		{
			m_Scene->DrawPointShadow(lightIndex);
		}

		auto spotLights = lightManager->GetSpotLight();
		int spotLightCount = spotLights.size();
		for (int lightIndex = 0; lightIndex < spotLightCount; lightIndex++)
		{
			m_Scene->DrawSpotShadow(pointLightCount, lightIndex);
		}
	}

	void VansVKDevice::DrawSceneForward(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		m_Scene->DrawSkyBoxNode();
		m_Scene->DrawOpaqueNodes();
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);
		m_Scene->DrawPostProcessNodes();
	}

	void VansVKDevice::DrawSceneDeferred(VansRenderPassManager* renderPassManager, VansVKCommandBuffer& commandBuffer)
	{
		VkCommandBuffer& cmd = commandBuffer.GetVKCommandBuffer();

		m_Scene->DrawOpaqueNodes();
		m_Scene->DrawTerrainNode();
		m_Scene->DrawVegetationNode();
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);
		m_Scene->DrawScreenSpaceFeatureNode();

		if (m_UseAsyncCompute)
		{
			VkMemoryBarrier asyncComputeBarrier =
			{
				VK_STRUCTURE_TYPE_MEMORY_BARRIER,
				nullptr,
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT
			};

			commandBuffer.WaitEvents(
				{ m_AsyncComputeCompletedEvent },
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				{ asyncComputeBarrier });
		}

		m_Scene->DeferredShading();
		m_Scene->DrawSkyBoxNode();
		m_Scene->DrawTransParentNodes();
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);
		m_Scene->DrawPostProcessNodes();
	}
}
