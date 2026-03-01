#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "VansRenderPass.h"
#include "../VansScene.h"

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
		m_FSRInput.jitterX = camera->m_JitterX;
		m_FSRInput.jitterY = camera->m_JitterY;

		m_VansVKCommandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		m_FSRController.DispatchUpscale(m_VansVKCommandBuffer.GetVKCommandBuffer(), m_FSRInput);

		VkExtent2D swapchainExtent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
		VkExtent2D fsrTempImageExtent = m_FSRController.GetDisplayExtent();
		m_VansVKSurface.SetSwapChainImageBarrier(
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
			},
			m_SwapChainImageIndex);

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

		vkCmdBlitImage(
			m_VansVKCommandBuffer.GetVKCommandBuffer(),
			m_FSRController.GetTempFSRImage().GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			m_VansVKSurface.GetSwapChainImage(m_SwapChainImageIndex),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&blitRegion,
			VK_FILTER_LINEAR);

		m_VansVKSurface.SetSwapChainImageBarrier(
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
			},
			m_SwapChainImageIndex);

		m_VansVKCommandBuffer.EndCommandBufferRecord();

		VansVKCommandBuffer::SubmitCommands(m_VansVKGraphicsQueue, m_VansVKLogicDevice, { m_VansVKCommandBuffer.GetVKCommandBuffer() }, {}, {}, m_VansVKCommandBuffer.m_CommandBufferFinishSubmitFence);
		m_VansVKCommandBuffer.ResetCommandBuffer(false);
	}

	void VansVKDevice::InitializeFSR()
	{
		VkExtent2D swapChainExtent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
		m_FSRController.InitializeContext(m_VansVKLogicDevice, m_VansVKPhysicalDevice, m_RenderWidth, m_RenderHeight, swapChainExtent.width, swapChainExtent.height);
	}

	void VansVKDevice::CleanupFSR()
	{
		m_FSRController.Cleanup();
	}
}
