#include "VansVKDevice.h"
#include "VansRenderPass.h"
#include "VansVKDescriptorManager.h"
#include "../VansScene.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include <iostream>

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
		CreateVKSemaphore(m_AsyncComputeDoneSemaphore);
		CreateVKFence(false, m_SwapChainImageAcquiredFence);

		auto renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->SetupVansDeferredRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, { m_RenderWidth, m_RenderHeight });
		renderPassManager->SetupVansShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansPunctualShadowRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue);
		renderPassManager->SetupVansUIRenderPass(m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue, m_VansVKSurface,
			{
				m_VansVKSurface.m_VansVKSwapChainImageExtent.width,
				m_VansVKSurface.m_VansVKSwapChainImageExtent.height
			}
		);

		PrepareRenderingData();

		auto vansConfigration = VansConfigration::GetInstance();
		std::string projectRoot = vansConfigration->GetProjectRootPath();
		m_Scene->LoadScene((projectRoot + "EngineAssets/Scenebk.json").c_str());

		PreparePBRMaterialData();
		PrepareInstanceTransformData();
		//创建全局的描述符
		m_Scene->CreateGlobalDescriptorSet(m_VansVKLogicDevice);
		m_Scene->CreateNodeDescriptorSets();
		PrepareRayTracingData();
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

		m_Scene->UpdateSceneData();

		auto renderPassManager = VansRenderPassManager::GetInstance();

		if (!m_UseAsyncCompute)
		{
			// ── Original single-submit path ─────────────────────────────────
			m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();

			renderPassManager->BeginRenderPass(renderPassManager->m_VansShadowPass, cmd, m_globalRenderStateData);
			DrawShadowMap(renderPassManager, cmd);
			renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);

			UpdateHZB(renderPassManager, m_VansVKCommandBuffer);
			UpdateGIData(renderPassManager, m_VansVKCommandBuffer);
			UpdateSSR(renderPassManager, m_VansVKCommandBuffer);
			UpdateRayTracing(m_VansVKCommandBuffer);
			UpdateVolumetricFog(renderPassManager, m_VansVKCommandBuffer);

			renderPassManager->BeginRenderPass(renderPassManager->m_VansRenderPass, cmd, m_globalRenderStateData);
			DrawSceneDeferred(renderPassManager, cmd);
			renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
		}
		else
		{
			// ── Async compute: SSR + VolumetricFog on compute queue ──────────
			// Record and submit COMPUTE FIRST so the GPU starts it before shadow
			// even reaches the graphics queue, guaranteeing true overlap.

			// Submit 1: SSR + VolumetricFog — queued to compute queue before shadow is even submitted
			m_VansVKComputeCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

			UpdateSSR(renderPassManager, m_VansVKComputeCommandBuffer);

			m_VansVKComputeCommandBuffer.EndCommandBufferRecord();

			VansVKCommandBuffer::SubmitCommands(
				m_VansVKComputeQueue, m_VansVKLogicDevice,
				{ m_VansVKComputeCommandBuffer.GetVKCommandBuffer() },
				{},
				{ m_AsyncComputeDoneSemaphore },
				m_VansVKComputeCommandBuffer.m_CommandBufferFinishSubmitFence, false);


			// Submit 2: Shadow — submitted AFTER compute is already queued on GPU
			m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
			VkCommandBuffer cmd = m_VansVKCommandBuffer.GetVKCommandBuffer();

			renderPassManager->BeginRenderPass(renderPassManager->m_VansShadowPass, cmd, m_globalRenderStateData);
			DrawShadowMap(renderPassManager, cmd);
			renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);

			UpdateHZB(renderPassManager, m_VansVKCommandBuffer);
			UpdateGIData(renderPassManager, m_VansVKCommandBuffer);
			UpdateVolumetricFog(renderPassManager, m_VansVKCommandBuffer);
			UpdateRayTracing(m_VansVKCommandBuffer);

			renderPassManager->BeginRenderPass(renderPassManager->m_VansRenderPass, cmd, m_globalRenderStateData);
			DrawSceneDeferred(renderPassManager, cmd);
			renderPassManager->EndRenderPass(cmd, m_globalRenderStateData);
		}
	}

	void VansVKDevice::Present()
	{
		m_VansVKCommandBuffer.EndCommandBufferRecord();

		if (!m_UseAsyncCompute)
		{
			// ── Original single-submit present ──────────────────────────────
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
				//{ m_AsyncComputeDoneSemaphore, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT }
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
		DestroyVKSemaphore(m_AsyncComputeDoneSemaphore);
		DestroyVKFence(m_SwapChainImageAcquiredFence);
	}

	void VansVKDevice::DrawShadowMap(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		m_Scene->DrawShadowNodes();
		m_Scene->DrawTerrainNode(true);
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

	void VansVKDevice::DrawSceneDeferred(VansRenderPassManager* renderPassManager, VkCommandBuffer& cmd)
	{
		m_Scene->DrawOpaqueNodes();
		m_Scene->DrawTerrainNode();
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);
		m_Scene->DrawScreenSpaceFeatureNode();
		m_Scene->DeferredShading();
		m_Scene->DrawSkyBoxNode();
		m_Scene->DrawTransParentNodes();
		renderPassManager->NextSubPass(cmd, m_globalRenderStateData);
		m_Scene->DrawPostProcessNodes();
	}
}
