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
	bool VansVKDevice::BuildFSRFrameInput(VansFSRFrameInput& output)
	{
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		VansCamera* camera = m_Scene != nullptr ? m_Scene->GetCamera() : nullptr;
		if (renderPassManager == nullptr || camera == nullptr)
		{
			VANS_LOG_ERROR("[FSR] Cannot build frame input without render passes and an active camera");
			return false;
		}

		auto& depth = renderPassManager->GetDepth();
		auto& motionVector = renderPassManager->GetMotionVector();
		auto& sceneColorHDR = renderPassManager->GetColor();
		const VkExtent2D displayExtent = m_FSRController.GetDisplayExtent();

		output = {};
		output.color = sceneColorHDR.GetImage();
		output.colorCreateInfo = sceneColorHDR.GetImageCreateInfo();
		output.depth = depth.GetImage();
		output.depthCreateInfo = depth.GetImageCreateInfo();
		output.motionVectors = motionVector.GetImage();
		output.motionVectorsCreateInfo = motionVector.GetImageCreateInfo();
		output.renderWidth = m_RenderWidth;
		output.renderHeight = m_RenderHeight;
		output.displayWidth = displayExtent.width;
		output.displayHeight = displayExtent.height;
		output.cameraFovAngleVerticalRadians = glm::radians(camera->GetFov());
		output.cameraNear = camera->GetNearClip();
		output.cameraFar = camera->GetFarClip();
		output.jitterPixelX = camera->m_JitterPixelX;
		output.jitterPixelY = camera->m_JitterPixelY;
		output.frameTimeDeltaMs = static_cast<float>(
			std::clamp(VansTimer::GetRealDeltaTime() * 1000.0, 0.01, 1000.0));
		output.preExposure = 1.0f;
		if (m_Scene != nullptr && m_Scene->GetMaterialManager() != nullptr)
		{
			VansMaterialManager* materialManager = m_Scene->GetMaterialManager();
			VansTexture* exposure = materialManager->GetRuntimeRenderTexture(
				VansMaterialManager::RT_FSR_EXPOSURE);
			if (exposure != nullptr)
			{
				output.exposure = exposure->GetImage().GetImage();
				output.exposureCreateInfo = exposure->GetImage().GetImageCreateInfo();
			}
		}

		m_FSRHistory.ObserveFrame(
			camera->GetFrameIndex(),
			camera,
			output.renderWidth,
			output.renderHeight,
			output.displayWidth,
			output.displayHeight,
			m_FSRMode);
		output.reset = m_FSRHistory.IsResetPending();
		return true;
	}

	bool VansVKDevice::RecordFSRFallbackUpscale(VansVKCommandBuffer& commandBuffer)
	{
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		if (renderPassManager == nullptr)
			return false;

		VansVKImage& source = renderPassManager->GetColor();
		VansVKImage& output = m_FSRController.GetOutputImage();
		const VkImageLayout sourceLayout = source.GetImageLayout();
		const VkImageLayout outputLayout = output.GetImageLayout();
		const VkExtent3D sourceExtent = source.GetImageDimension();
		const VkExtent3D outputExtent = output.GetImageDimension();
		if (sourceExtent.width == 0 || sourceExtent.height == 0 ||
			outputExtent.width == 0 || outputExtent.height == 0)
		{
			return false;
		}

		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			commandBuffer,
			source,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			sourceLayout,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			commandBuffer,
			output,
			outputLayout == VK_IMAGE_LAYOUT_UNDEFINED
				? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
				: VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			outputLayout == VK_IMAGE_LAYOUT_UNDEFINED
				? 0u
				: VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			outputLayout,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkImageBlit region{};
		region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.srcOffsets[1] = {
			static_cast<std::int32_t>(sourceExtent.width),
			static_cast<std::int32_t>(sourceExtent.height),
			1 };
		region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		region.dstOffsets[1] = {
			static_cast<std::int32_t>(outputExtent.width),
			static_cast<std::int32_t>(outputExtent.height),
			1 };
		commandBuffer.BlitImageRegions(
			source.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			output.GetImage(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			{ region },
			VK_FILTER_LINEAR);

		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			commandBuffer,
			source,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			sourceLayout);
		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			commandBuffer,
			output,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		return true;
	}

	void VansVKDevice::RecordFSRMasks(VansVKCommandBuffer& commandBuffer, VansFSRFrameInput& input)
	{
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		if (renderPassManager == nullptr)
			return;

		VansVKImage& opaqueOnly = renderPassManager->GetOpaqueSceneColor();
		VansVKImage& sceneColor = renderPassManager->GetColor();
		VansVKImage& reactive = m_FSRController.GetReactiveMaskImage();
		VansVKImage& transparency = m_FSRController.GetTransparencyAndCompositionImage();

		const VkImageLayout reactiveLayout = reactive.GetImageLayout();
		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			commandBuffer,
			reactive,
			reactiveLayout == VK_IMAGE_LAYOUT_UNDEFINED
				? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
				: VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			reactiveLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0u : VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			reactiveLayout,
			VK_IMAGE_LAYOUT_GENERAL);

		const bool reactiveGenerated = m_FSRController.GenerateReactiveMask(
			commandBuffer.GetVKCommandBuffer(),
			opaqueOnly.GetImage(),
			opaqueOnly.GetImageCreateInfo(),
			sceneColor.GetImage(),
			sceneColor.GetImageCreateInfo());

		const VkImageLayout transparencyLayout = transparency.GetImageLayout();
		if (reactiveGenerated)
		{
			// The opaque/full-color delta is also a conservative approximation for
			// transparency-and-composition pixels until individual material passes
			// write a more selective mask.
			VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
				commandBuffer,
				reactive,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
			VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
				commandBuffer,
				transparency,
				transparencyLayout == VK_IMAGE_LAYOUT_UNDEFINED
					? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
					: VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				transparencyLayout == VK_IMAGE_LAYOUT_UNDEFINED
					? 0u
					: VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				transparencyLayout,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

			VkImageCopy maskCopy{};
			maskCopy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			maskCopy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
			maskCopy.extent = { m_RenderWidth, m_RenderHeight, 1 };
			commandBuffer.CopyImageRegions(
				reactive,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				transparency,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				{ maskCopy });

			VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
				commandBuffer,
				reactive,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_TRANSFER_READ_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_GENERAL);
			VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
				commandBuffer,
				transparency,
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_GENERAL);

			input.reactive = reactive.GetImage();
			input.reactiveCreateInfo = reactive.GetImageCreateInfo();
			input.transparencyAndComposition = transparency.GetImage();
			input.transparencyAndCompositionCreateInfo = transparency.GetImageCreateInfo();
			return;
		}

		// Never expose a stale mask if auto-reactive generation fails.
		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			commandBuffer,
			transparency,
			transparencyLayout == VK_IMAGE_LAYOUT_UNDEFINED
				? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
				: VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			transparencyLayout == VK_IMAGE_LAYOUT_UNDEFINED ? 0u : VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			transparencyLayout,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		VkClearColorValue zeroMask{};
		commandBuffer.ClearColorImage(transparency, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, zeroMask);
		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			commandBuffer,
			transparency,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL);
		input.transparencyAndComposition = transparency.GetImage();
		input.transparencyAndCompositionCreateInfo = transparency.GetImageCreateInfo();
	}

	void VansVKDevice::RecordFinalDisplayToSwapchain()
	{
		VansVKCommandBuffer& frameGraphicsCommandBuffer = CurrentGraphicsCommandBuffer();
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		VansVKImage& finalDisplay = renderPassManager->GetFinalDisplayColor();
		const VkImageLayout previousLayout = finalDisplay.GetImageLayout();

		VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
			frameGraphicsCommandBuffer,
			finalDisplay,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			previousLayout,
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
		const VkExtent3D displayExtent = finalDisplay.GetImageDimension();
		VkImageBlit blitRegion{};
		blitRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blitRegion.srcOffsets[1] = {
			static_cast<int32_t>(displayExtent.width),
			static_cast<int32_t>(displayExtent.height),
			1 };
		blitRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
		blitRegion.dstOffsets[1] = {
			static_cast<int32_t>(swapchainExtent.width),
			static_cast<int32_t>(swapchainExtent.height),
			1 };

		frameGraphicsCommandBuffer.BlitImageRegions(
			finalDisplay.GetImage(),
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
			finalDisplay,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	void VansVKDevice::RecordDisplayPostProcess(VansVKCommandBuffer& commandBuffer)
	{
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		if (renderPassManager == nullptr || m_Scene == nullptr)
			return;

		VansVKImage& fsrHDR = m_FSREnabled
			? m_FSRController.GetOutputImage()
			: renderPassManager->GetColor();
		const VkImageLayout fsrLayout = fsrHDR.GetImageLayout();
		if (fsrLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
				commandBuffer,
				fsrHDR,
				m_FSREnabled
					? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT
					: VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				m_FSREnabled
					? VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT
					: VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				fsrLayout,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		renderPassManager->BeginRenderPass(
			renderPassManager->m_VansDisplayPostProcessPass,
			commandBuffer,
			m_globalRenderStateData,
			0);
		m_Scene->DrawPostProcessNodes();
		renderPassManager->EndRenderPass(commandBuffer, m_globalRenderStateData);
		renderPassManager->GetFinalDisplayColor().SetTrackedImageLayout(
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}

	VansVKImage& VansVKDevice::GetFinalDisplayImage()
	{
		return VansRenderPassManager::GetInstance()->GetFinalDisplayColor();
	}

	bool VansVKDevice::InitializeFSR()
	{
		const VkExtent2D outputExtent = CalculateFSROutputExtent();
		m_FSREnabled = m_FSRController.InitializeContext(
			m_VansVKLogicDevice,
			m_VansVKPhysicalDevice,
			m_RenderWidth,
			m_RenderHeight,
			outputExtent.width,
			outputExtent.height);
		if (!m_FSREnabled)
		{
			VANS_LOG_ERROR("[FSR] Failed to initialize upscaling context; using native HDR display fallback");
		}
		m_FSRHistory.RequestReset(VansFSRResetReason::ContextRecreated);
		return m_FSREnabled;
	}

	void VansVKDevice::CleanupFSR()
	{
		m_FSRController.Cleanup();
		m_FSREnabled = false;
	}

	bool VansVKDevice::GetFSRJitterOffset(uint32_t frameIndex, float& outPixelX, float& outPixelY)
	{
		int32_t phaseCount = m_FSRController.GetJitterPhaseCount();
		if (phaseCount <= 0) return false;
		return m_FSRController.GetJitterOffset(
			static_cast<int32_t>(frameIndex % static_cast<uint32_t>(phaseCount)),
			outPixelX, outPixelY);
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
		case VansFSRMode::Balanced:
			return {
				static_cast<uint32_t>(std::lround(static_cast<double>(m_RenderWidth) * 1.7)),
				static_cast<uint32_t>(std::lround(static_cast<double>(m_RenderHeight) * 1.7)) };
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
				const double renderAspect = static_cast<double>(m_RenderWidth) /
					static_cast<double>(m_RenderHeight);
				const double viewportAspect = static_cast<double>(m_RequestedSceneViewportExtent.width) /
					static_cast<double>(m_RequestedSceneViewportExtent.height);
				VkExtent2D fitted{};
				if (viewportAspect > renderAspect)
				{
					fitted.height = m_RequestedSceneViewportExtent.height;
					fitted.width = static_cast<uint32_t>(std::lround(fitted.height * renderAspect));
				}
				else
				{
					fitted.width = m_RequestedSceneViewportExtent.width;
					fitted.height = static_cast<uint32_t>(std::lround(fitted.width / renderAspect));
				}
				return {
					std::clamp(fitted.width, m_RenderWidth, m_RenderWidth * 3u),
					std::clamp(fitted.height, m_RenderHeight, m_RenderHeight * 3u) };
			}
			{
				const VkExtent2D swapchain = m_VansVKSurface.m_VansVKSwapChainImageExtent;
				if (swapchain.width < m_RenderWidth || swapchain.height < m_RenderHeight)
					return { m_RenderWidth, m_RenderHeight };
				const double renderAspect = static_cast<double>(m_RenderWidth) /
					static_cast<double>(m_RenderHeight);
				const double swapchainAspect = static_cast<double>(swapchain.width) /
					static_cast<double>(swapchain.height);
				if (swapchainAspect > renderAspect)
					return {
						static_cast<uint32_t>(std::lround(swapchain.height * renderAspect)),
						swapchain.height };
				return {
					swapchain.width,
					static_cast<uint32_t>(std::lround(swapchain.width / renderAspect)) };
			}
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
		if (modeChanged)
			m_FSRHistory.RequestReset(VansFSRResetReason::ModeChange);
	}

	void VansVKDevice::ProcessPendingFSRConfig()
	{
		if (!m_FSRConfigDirty)
			return;

		const VkExtent2D requestedExtent = CalculateFSROutputExtent();
		const VkExtent2D currentExtent = m_FSREnabled
			? m_FSRController.GetDisplayExtent()
			: VkExtent2D{ 0, 0 };
		m_FSRConfigDirty = false;
		if (requestedExtent.width == currentExtent.width &&
			requestedExtent.height == currentExtent.height)
		{
			return;
		}

		WaitForDevice();
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->DestroySceneUIRenderPass();
		renderPassManager->DestroyDisplayPostProcessPass();
		CleanupFSR();
		InitializeFSR();
		VansVKImage& displayInput = m_FSREnabled
			? m_FSRController.GetOutputImage()
			: renderPassManager->GetColor();
		renderPassManager->SetupVansDisplayPostProcessPass(
			m_VansVKLogicDevice,
			displayInput,
			requestedExtent);
		renderPassManager->SetupVansSceneUIRenderPass(
			m_VansVKLogicDevice,
			renderPassManager->GetFinalDisplayColor().GetImageView(),
			requestedExtent);
		if (m_Scene != nullptr)
			m_Scene->MarkRenderNodeDescriptorSetsDirty();
		VansRuntime::VansUISystem::Get().SetScreenSize(
			requestedExtent.width,
			requestedExtent.height);

		VANS_LOG("[FSR] output=" << requestedExtent.width << "x" << requestedExtent.height
			<< " render=" << m_RenderWidth << "x" << m_RenderHeight
			<< " mipBias=" << GetUpscaleMipBias());
	}
}
