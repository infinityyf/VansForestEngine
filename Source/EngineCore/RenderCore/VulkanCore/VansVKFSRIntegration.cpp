#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "VansRenderPass.h"
#include "VansRenderGraphVulkanSync.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../../Util/VansLog.h"
#include "../../VansTimer.h"
#include "../../RuntimeUI/Public/VansUISystem.h"
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	void VansVKDevice::PrepareFSRDispatchInputData(float fovy, float nearPlane, float farPlane)
	{
		auto renderPassManager = VansRenderPassManager::GetInstance();
		auto& depth = renderPassManager->GetDepth();
		auto& motionVector = renderPassManager->GetMotionVector();
		auto& colorAfterPostProcess = renderPassManager->GetColorAfterPostProcess();
		m_FSRInput.color = colorAfterPostProcess.GetImage();
		m_FSRInput.colorCreateInfo = colorAfterPostProcess.GetImageCreateInfo();
		m_FSRInput.depth = depth.GetImage();
		m_FSRInput.depthCreateInfo = depth.GetImageCreateInfo();
		m_FSRInput.motionVectors = motionVector.GetImage();
		m_FSRInput.motionVectorsCreateInfo = motionVector.GetImageCreateInfo();

		m_FSRInput.fovy = fovy;
		m_FSRInput.nearPlane = nearPlane;
		m_FSRInput.farPlane = farPlane;
	}

	void VansVKDevice::DispatchFSRUpscale()
	{
		auto camera = m_Scene->GetCamera();
		m_FSRInput.jitterPixelX = camera->m_JitterPixelX;
		m_FSRInput.jitterPixelY = camera->m_JitterPixelY;

		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_FSRController.DispatchUpscale(m_VansVKCommandBuffer.GetVKCommandBuffer(), m_FSRInput);

		VkExtent2D swapchainExtent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
		VkExtent2D fsrTempImageExtent = m_FSRController.GetDisplayExtent();
		m_VansVKSurface.RecordSwapChainImageBarrier(
			m_VansVKCommandBuffer,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
				VK_ACCESS_NONE,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				VK_IMAGE_ASPECT_COLOR_BIT
			});

		VkImageBlit blitRegion{};
		blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blitRegion.srcSubresource.mipLevel = 0;
		blitRegion.srcSubresource.baseArrayLayer = 0;
		blitRegion.srcSubresource.layerCount = 1;
		blitRegion.srcOffsets[0] = { 0, 0, 0 };
		blitRegion.srcOffsets[1] = { (int32_t)fsrTempImageExtent.width, (int32_t)fsrTempImageExtent.height, 1 };

		blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blitRegion.dstSubresource.mipLevel = 0;
		blitRegion.dstSubresource.baseArrayLayer = 0;
		blitRegion.dstSubresource.layerCount = 1;
		blitRegion.dstOffsets[0] = { 0, 0, 0 };
		blitRegion.dstOffsets[1] = { (int32_t)swapchainExtent.width, (int32_t)swapchainExtent.height, 1 };

		m_VansVKCommandBuffer.BlitImageRegions(
			m_FSRController.GetTempFSRImage().GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			{ blitRegion },
			VK_FILTER_LINEAR);

		m_VansVKSurface.RecordSwapChainImageBarrier(
			m_VansVKCommandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			{
				m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_NONE,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				VK_IMAGE_ASPECT_COLOR_BIT
			});

		if (!m_VansVKCommandBuffer.EndCommandBufferRecord()
			|| !VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence)
			|| !m_VansVKCommandBuffer.ResetCommandBuffer(false))
		{
			VANS_LOG_ERROR("[VansVKDevice] FSR output blit submit failed.");
		}
	}

	void VansVKDevice::RecordFSROutputToSwapchain()
	{
		VansVKCommandBuffer& frameGraphicsCommandBuffer = CurrentGraphicsCommandBuffer();
		VansVKImage& fsrOut = m_FSRController.GetTempFSRImage();
		const VkImageLayout previousFSRLayout = fsrOut.GetImageLayout();

		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			frameGraphicsCommandBuffer,
			fsrOut,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			previousFSRLayout,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		m_VansVKSurface.RecordSwapChainImageBarrier(
			frameGraphicsCommandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
				VK_ACCESS_NONE,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				VK_IMAGE_ASPECT_COLOR_BIT
			});

		const VkExtent2D swapchainExtent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
		const VkExtent3D fsrExtent = fsrOut.GetImageDimension();
		VkImageBlit blitRegion{};
		blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blitRegion.srcSubresource.mipLevel = 0;
		blitRegion.srcSubresource.baseArrayLayer = 0;
		blitRegion.srcSubresource.layerCount = 1;
		blitRegion.srcOffsets[0] = { 0, 0, 0 };
		blitRegion.srcOffsets[1] = {
			static_cast<int32_t>(fsrExtent.width),
			static_cast<int32_t>(fsrExtent.height),
			1
		};

		blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blitRegion.dstSubresource.mipLevel = 0;
		blitRegion.dstSubresource.baseArrayLayer = 0;
		blitRegion.dstSubresource.layerCount = 1;
		blitRegion.dstOffsets[0] = { 0, 0, 0 };
		blitRegion.dstOffsets[1] = {
			static_cast<int32_t>(swapchainExtent.width),
			static_cast<int32_t>(swapchainExtent.height),
			1
		};

		frameGraphicsCommandBuffer.BlitImageRegions(
			fsrOut.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			{ blitRegion },
			VK_FILTER_LINEAR);

		m_VansVKSurface.RecordSwapChainImageBarrier(
			frameGraphicsCommandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			{
				m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_NONE,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				VK_IMAGE_ASPECT_COLOR_BIT
			});

		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			frameGraphicsCommandBuffer,
			fsrOut,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VansVKDevice::InitializeFSR()
	{
		const VkExtent2D outputExtent = CalculateFSROutputExtent();
		m_FSRController.InitializeContext(
			m_VansVKLogicDevice,
			m_VansVKPhysicalDevice,
			m_RenderWidth,
			m_RenderHeight,
			outputExtent.width,
			outputExtent.height);
	}

	void VansVKDevice::CleanupFSR()
	{
		m_FSRController.Cleanup();
	}

	bool VansVKDevice::GetFSRJitterOffset(uint32_t frameIndex, float& outPixelX, float& outPixelY)
	{
		int32_t phaseCount = m_FSRController.GetJitterPhaseCount();
		if (phaseCount <= 0) return false;
		m_FSRController.GetJitterOffset(
			static_cast<int32_t>(frameIndex % static_cast<uint32_t>(phaseCount)),
			outPixelX, outPixelY);
		return true;
	}

	VkExtent2D VansVKDevice::CalculateFSROutputExtent() const
	{
		switch (m_FSRMode)
		{
		case VansFSRMode::NativeAA:
			return { m_RenderWidth, m_RenderHeight };
		case VansFSRMode::Quality:
			return {
				static_cast<uint32_t>(std::lround(static_cast<double>(m_RenderWidth) * 1.5)),
				static_cast<uint32_t>(std::lround(static_cast<double>(m_RenderHeight) * 1.5)) };
		case VansFSRMode::Performance:
			return { m_RenderWidth * 2u, m_RenderHeight * 2u };
		case VansFSRMode::MatchViewport:
		default:
			if (m_RequestedSceneViewportExtent.width > 0 && m_RequestedSceneViewportExtent.height > 0)
			{
				// FSR is an upscaler. If the docked viewport is smaller than the
				// fixed render input, keep Native-AA size instead of issuing an
				// unsupported downscale dispatch.
				if (m_RequestedSceneViewportExtent.width < m_RenderWidth ||
					m_RequestedSceneViewportExtent.height < m_RenderHeight)
				{
					return { m_RenderWidth, m_RenderHeight };
				}
				return {
					std::min(m_RequestedSceneViewportExtent.width, m_RenderWidth * 3u),
					std::min(m_RequestedSceneViewportExtent.height, m_RenderHeight * 3u) };
			}
			return m_VansVKSurface.m_VansVKSwapChainImageExtent;
		}
	}

	float VansVKDevice::GetUpscaleMipBias() const
	{
		const VkExtent2D outputExtent = CalculateFSROutputExtent();
		if (outputExtent.width == 0 || m_RenderWidth == 0)
			return 0.0f;

		// AMD recommendation: log2(render/display) - 1. Performance 2x = -2.
		const float ratio = static_cast<float>(m_RenderWidth) /
			static_cast<float>(outputExtent.width);
		return std::clamp(std::log2(std::max(ratio, 0.0001f)) - 1.0f, -3.0f, 0.0f);
	}

	void VansVKDevice::RequestFSRConfig(
		VansFSRMode mode,
		uint32_t viewportWidth,
		uint32_t viewportHeight,
		float sharpness)
	{
		viewportWidth = std::max(viewportWidth, 1u);
		viewportHeight = std::max(viewportHeight, 1u);
		sharpness = std::clamp(sharpness, 0.0f, 1.0f);

		const bool extentChanged =
			m_RequestedSceneViewportExtent.width != viewportWidth ||
			m_RequestedSceneViewportExtent.height != viewportHeight;
		const bool modeChanged = m_FSRMode != mode;
		const bool sharpnessChanged = std::abs(m_FSRController.GetSharpness() - sharpness) > 0.0001f;

		m_RequestedSceneViewportExtent = { viewportWidth, viewportHeight };
		m_FSRMode = mode;
		m_FSRController.SetSharpness(sharpness);
		m_FSRConfigDirty = m_FSRConfigDirty || modeChanged ||
			(mode == VansFSRMode::MatchViewport && extentChanged);

		if (sharpnessChanged)
		{
			VANS_LOG("[FSR] RCAS sharpness=" << sharpness);
		}
	}

	void VansVKDevice::ProcessPendingFSRConfig()
	{
		if (!m_FSRConfigDirty)
			return;

		const VkExtent2D requestedExtent = CalculateFSROutputExtent();
		const VkExtent2D currentExtent = m_FSRController.GetDisplayExtent();
		m_FSRConfigDirty = false;
		if (requestedExtent.width == currentExtent.width &&
			requestedExtent.height == currentExtent.height)
		{
			return;
		}

		WaitForDevice();
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->DestroySceneUIRenderPass();
		CleanupFSR();
		InitializeFSR();
		renderPassManager->SetupVansSceneUIRenderPass(
			m_VansVKLogicDevice,
			m_FSRController.GetTempFSRImage().GetImageView(),
			m_FSRController.GetDisplayExtent());
		VansRuntime::VansUISystem::Get().SetScreenSize(
			m_FSRController.GetDisplayExtent().width,
			m_FSRController.GetDisplayExtent().height);

		VANS_LOG("[FSR] output=" << requestedExtent.width << "x" << requestedExtent.height
			<< " render=" << m_RenderWidth << "x" << m_RenderHeight
			<< " mipBias=" << GetUpscaleMipBias());
	}
}
