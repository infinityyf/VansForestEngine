#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "VansRenderPass.h"
#include "VansRenderGraphVulkanSync.h"
#include "../VansScene.h"
#include "../VansCamera.h"
#include "../VansTemporalProjection.h"
#include "../WaterCore/VansWaterSystem.h"
#include "../UpscalingCore/VansTemporalJitterSequence.h"
#include "../../../Graphics/Vulkan/VansStreamlineRuntime.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace VansGraphics
{
	bool VansVKDevice::PrepareDLSSDispatchResources(VansVKCommandBuffer& commandBuffer)
	{
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		if (renderPassManager == nullptr)
			return false;

		// These are the final layouts declared by the scene-color, GBuffer and
		// motion-vector render passes. Render-pass implicit transitions do not pass
		// through VansVKImage::SetImageMemoryBarrier, so synchronize the resources
		// and refresh their tracked layouts before exporting them to Streamline.
		// Vulkan resource state is mandatory in sl::Resource and must describe the
		// state at the point where slEvaluateFeature records its work.
		auto synchronizeReadOnlyInput = [&](VansVKImage& image,
			VkPipelineStageFlags producerStages,
			VkAccessFlags producerAccess,
			VkImageLayout readOnlyLayout)
		{
			if (image.GetImage() == VK_NULL_HANDLE)
				return false;
			VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
				commandBuffer,
				image,
				producerStages,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				producerAccess,
				VK_ACCESS_SHADER_READ_BIT,
				readOnlyLayout,
				readOnlyLayout);
			return true;
		};

		if (!synchronizeReadOnlyInput(
			renderPassManager->GetColor(),
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) ||
			!synchronizeReadOnlyInput(
				renderPassManager->GetDepth(),
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
					VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) ||
			!synchronizeReadOnlyInput(
				renderPassManager->GetMotionVector(),
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
		{
			return false;
		}

		if (m_Scene != nullptr && m_Scene->GetMaterialManager() != nullptr)
		{
			VansTexture* exposure = m_Scene->GetMaterialManager()->GetRuntimeRenderTexture(
				VansMaterialManager::RT_UPSCALER_EXPOSURE);
			if (exposure != nullptr)
			{
				VansVKImage& exposureImage = exposure->GetImage();
				if (exposureImage.GetImage() == VK_NULL_HANDLE)
					return false;
				VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
					commandBuffer,
					exposureImage,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
					VK_ACCESS_SHADER_WRITE_BIT,
					VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_LAYOUT_GENERAL,
					VK_IMAGE_LAYOUT_GENERAL);
			}
		}
		return true;
	}

	bool VansVKDevice::BuildDLSSDispatch(VansStreamlineDLSSDispatch& output)
	{
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		if (renderPassManager == nullptr || !m_HasCurrentRenderView)
			return false;

		VansVKImage& color = renderPassManager->GetColor();
		VansVKImage& depth = renderPassManager->GetDepth();
		VansVKImage& motion = renderPassManager->GetMotionVector();
		const VkExtent2D outputExtent = CalculateUpscalerOutputExtent();
		const VansTemporalCameraSnapshot cameraSnapshot =
			CaptureTemporalCameraSnapshot();
		output = {};
		output.commandBuffer = CurrentGraphicsCommandBuffer().GetVKCommandBuffer();
		output.color = { color.GetImage(), color.GetImageView(),
			color.GetImageCreateInfo(), color.GetImageLayout() };
		output.depth = { depth.GetImage(), depth.GetImageView(),
			depth.GetImageCreateInfo(), depth.GetImageLayout() };
		output.motionVectors = { motion.GetImage(), motion.GetImageView(),
			motion.GetImageCreateInfo(), motion.GetImageLayout() };
		output.output = { m_UpscalerOutputImage.GetImage(),
			m_UpscalerOutputImage.GetImageView(),
			m_UpscalerOutputImage.GetImageCreateInfo(),
			m_UpscalerOutputImage.GetImageLayout() };
		output.renderWidth = m_RenderWidth;
		output.renderHeight = m_RenderHeight;
		output.outputWidth = outputExtent.width;
		output.outputHeight = outputExtent.height;
		output.frameIndex = cameraSnapshot.frameIndex;
		output.jitterPixels = cameraSnapshot.jitter.samplePixels;
		output.motionVectorScale = { -1.0f, -1.0f };
		output.view = cameraSnapshot.view;
		output.projection = cameraSnapshot.projection;
		output.previousViewProjection = cameraSnapshot.previousViewProjection;
		output.cameraPosition = cameraSnapshot.position;
		output.cameraUp = cameraSnapshot.up;
		output.cameraRight = cameraSnapshot.right;
		output.cameraForward = cameraSnapshot.forward;
		output.cameraNear = cameraSnapshot.nearClip;
		output.cameraFar = cameraSnapshot.farClip;
		output.cameraFovRadians = cameraSnapshot.fovRadians;

		if (m_Scene->GetMaterialManager() != nullptr)
		{
			VansTexture* exposure = m_Scene->GetMaterialManager()->GetRuntimeRenderTexture(
				VansMaterialManager::RT_UPSCALER_EXPOSURE);
			if (exposure != nullptr)
			{
				VansVKImage& image = exposure->GetImage();
				output.exposure = { image.GetImage(), image.GetImageView(),
					image.GetImageCreateInfo(), image.GetImageLayout() };
			}
		}

		auto& history = m_UpscalerManager.GetHistory();
		const VansUpscalerConfig& effective = m_UpscalerManager.GetEffectiveConfig();
		history.ObserveFrame(
			cameraSnapshot.frameIndex, m_CurrentRenderView.cameraIdentity,
			{ m_RenderWidth, m_RenderHeight },
			{ outputExtent.width, outputExtent.height },
			effective.backend, effective.quality);
		output.reset = history.IsResetPending();
		return true;
	}

	bool VansVKDevice::BuildFSRFrameInput(VansFSRFrameInput& output)
	{
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		if (renderPassManager == nullptr || !m_HasCurrentRenderView)
		{
			VANS_LOG_ERROR("[FSR] Cannot build frame input without render passes and an active camera");
			return false;
		}

		auto& depth = renderPassManager->GetDepth();
		auto& motionVector = renderPassManager->GetMotionVector();
		auto& sceneColorHDR = renderPassManager->GetColor();
		const VkExtent2D displayExtent = m_FSRController.GetDisplayExtent();
		const VansTemporalCameraSnapshot cameraSnapshot =
			CaptureTemporalCameraSnapshot();

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
		output.cameraFovAngleVerticalRadians = cameraSnapshot.fovRadians;
		const VansDeviceDepthRange depthRange =
			ExtractVulkanDeviceDepthRange(cameraSnapshot.projection);
		if (!depthRange.valid || !depthRange.finiteFar)
		{
			VANS_LOG_ERROR("[FSR] Cannot derive a finite Vulkan device-depth range from the active projection");
			return false;
		}
		output.cameraNear = depthRange.nearDistance;
		output.cameraFar = depthRange.farDistance;
		const VansTemporalJitter& temporalJitter = cameraSnapshot.jitter;
		output.jitterSamplePixelX = temporalJitter.samplePixels.x;
		output.jitterSamplePixelY = temporalJitter.samplePixels.y;
		output.frameTimeDeltaMs = static_cast<float>(
			std::clamp(m_CurrentRenderTiming.renderDeltaSeconds * 1000.0, 0.01, 1000.0));
		output.preExposure = 1.0f;
		if (m_Scene != nullptr && m_Scene->GetMaterialManager() != nullptr)
		{
			VansMaterialManager* materialManager = m_Scene->GetMaterialManager();
			VansTexture* exposure = materialManager->GetRuntimeRenderTexture(
				VansMaterialManager::RT_UPSCALER_EXPOSURE);
			if (exposure != nullptr)
			{
				output.exposure = exposure->GetImage().GetImage();
				output.exposureCreateInfo = exposure->GetImage().GetImageCreateInfo();
			}
		}

		auto& history = m_UpscalerManager.GetHistory();
		const VansUpscalerConfig& effective = m_UpscalerManager.GetEffectiveConfig();
		history.ObserveFrame(
			cameraSnapshot.frameIndex,
			m_CurrentRenderView.cameraIdentity,
			{ output.renderWidth, output.renderHeight },
			{ output.displayWidth, output.displayHeight },
			effective.backend,
			effective.quality);
		output.reset = history.IsResetPending();
		return true;
	}

	bool VansVKDevice::RecordFSRFallbackUpscale(VansVKCommandBuffer& commandBuffer)
	{
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		if (renderPassManager == nullptr)
			return false;

		VansVKImage& source = renderPassManager->GetColor();
		VansVKImage& output = m_UpscalerOutputImage;
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

		VansVKImage& upscalerOutput = m_UpscalerOutputImage;
		const VkImageLayout outputLayout = upscalerOutput.GetImageLayout();
		if (outputLayout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			VansRenderGraphVulkanSyncRecorder::RecordImageTransition(
				commandBuffer,
				upscalerOutput,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				outputLayout,
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
		const VkExtent2D outputExtent = CalculateUpscalerOutputExtent();
		if (!EnsureUpscalerOutputImage(outputExtent))
			return false;
		m_FSREnabled = m_FSRController.InitializeContext(
			m_VansVKLogicDevice,
			m_VansVKPhysicalDevice,
			m_RenderWidth,
			m_RenderHeight,
			outputExtent.width,
			outputExtent.height,
			m_UpscalerOutputImage);
		if (!m_FSREnabled)
		{
			VANS_LOG_ERROR("[FSR] Failed to initialize upscaling context; using native HDR display fallback");
		}
		m_UpscalerManager.GetHistory().RequestReset(VansUpscalerResetReason::ContextRecreated);
		return m_FSREnabled;
	}

	bool VansVKDevice::InitializeDLSS()
	{
		const VkExtent2D outputExtent = CalculateUpscalerOutputExtent();
		if (!EnsureUpscalerOutputImage(outputExtent))
			return false;
		m_DLSSEnabled = m_DLSSController.InitializeContext(
			m_UpscalerManager.GetEffectiveConfig().quality,
			outputExtent.width,
			outputExtent.height,
			true);
		if (!m_DLSSEnabled)
			VANS_LOG_ERROR("[DLSS] Failed to initialize context");
		m_UpscalerManager.GetHistory().RequestReset(
			VansUpscalerResetReason::ContextRecreated);
		return m_DLSSEnabled;
	}

	bool VansVKDevice::EnsureUpscalerOutputImage(const VkExtent2D& outputExtent)
	{
		if (outputExtent.width == 0 || outputExtent.height == 0)
			return false;
		if (m_UpscalerOutputImage.GetImage() != VK_NULL_HANDLE &&
			m_UpscalerOutputExtent.width == outputExtent.width &&
			m_UpscalerOutputExtent.height == outputExtent.height)
		{
			return true;
		}
		CleanupUpscalerOutputImage();
		if (!m_UpscalerOutputImage.CreateVulkanImage(
			m_VansVKLogicDevice,
			{ outputExtent.width, outputExtent.height, 1 },
			VK_FORMAT_R16G16B16A16_SFLOAT,
			1,
			1,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
				VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_SAMPLE_COUNT_1_BIT,
			false,
			false,
			true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		{
			VANS_LOG_ERROR("[Upscaler] Failed to create stable output image "
				<< outputExtent.width << "x" << outputExtent.height);
			return false;
		}
		m_UpscalerOutputExtent = outputExtent;
		return true;
	}

	void VansVKDevice::CleanupUpscalerOutputImage()
	{
		if (m_UpscalerOutputImage.GetImage() != VK_NULL_HANDLE &&
			m_VansVKLogicDevice != VK_NULL_HANDLE)
		{
			m_UpscalerOutputImage.DestroyVulkanImage(m_VansVKLogicDevice);
		}
		m_UpscalerOutputExtent = { 0, 0 };
	}

	void VansVKDevice::CleanupFSR()
	{
		m_FSRController.Cleanup();
		m_FSREnabled = false;
	}

	void VansVKDevice::CleanupDLSS()
	{
		m_DLSSController.Cleanup();
		m_DLSSEnabled = false;
	}

	bool VansVKDevice::GetTemporalUpscaleJitterOffset(
		uint32_t frameIndex,
		float& outPixelX,
		float& outPixelY)
	{
		outPixelX = 0.0f;
		outPixelY = 0.0f;
		const VansUpscalerBackend backend =
			m_UpscalerManager.GetEffectiveConfig().backend;
		if (backend == VansUpscalerBackend::DLSS && m_DLSSEnabled)
		{
			const VkExtent2D outputExtent = CalculateUpscalerOutputExtent();
			const std::int32_t phaseCount =
				VansTemporalJitterSequence::CalculatePhaseCount(
					m_RenderWidth, outputExtent.width);
			return VansTemporalJitterSequence::Sample(
				frameIndex, phaseCount, outPixelX, outPixelY);
		}
		if (backend != VansUpscalerBackend::FSR || !m_FSREnabled)
			return false;
		int32_t phaseCount = m_FSRController.GetJitterPhaseCount();
		if (phaseCount <= 0) return false;
		return m_FSRController.GetJitterOffset(
			static_cast<int32_t>(frameIndex % static_cast<uint32_t>(phaseCount)),
			outPixelX, outPixelY);
	}

	VkExtent2D VansVKDevice::CalculateUpscalerOutputExtent() const
	{
		if (m_RequestedUpscalerOutputExtent.width > 0 &&
			m_RequestedUpscalerOutputExtent.height > 0)
		{
			return m_RequestedUpscalerOutputExtent;
		}
		const VkExtent2D surfaceExtent = m_VansVKSurface.m_VansVKSwapChainImageExtent;
		if (surfaceExtent.width > 0 && surfaceExtent.height > 0)
			return surfaceExtent;
		return { std::max(m_RenderWidth, 1u), std::max(m_RenderHeight, 1u) };
	}

	VkExtent2D VansVKDevice::CalculateUpscalerRenderExtent() const
	{
		const VkExtent2D output = CalculateUpscalerOutputExtent();
		const VansUpscalerConfig& effective = m_UpscalerManager.GetEffectiveConfig();
		VansExtent2D recommendation;
		if (effective.backend == VansUpscalerBackend::DLSS)
		{
			m_DLSSController.QueryRecommendedRenderExtent(
				effective.quality,
				output.width,
				output.height,
				recommendation);
		}
		const VansUpscaleResolution resolved = VansUpscaleResolutionPolicy::Resolve(
			effective,
			{ output.width, output.height },
			recommendation);
		if (!resolved.valid)
			return output;
		return { resolved.renderExtent.width, resolved.renderExtent.height };
	}

	void VansVKDevice::RecreateSceneResolutionResources(const VkExtent2D& renderExtent)
	{
		if (renderExtent.width == 0 || renderExtent.height == 0)
		{
			VANS_LOG_ERROR("[Upscaler] Refusing scene-resolution resource rebuild with invalid extent "
				<< renderExtent.width << "x" << renderExtent.height);
			return;
		}
		if (renderExtent.width == m_RenderWidth && renderExtent.height == m_RenderHeight)
			return;
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		if (renderPassManager == nullptr || m_Scene == nullptr ||
			m_Scene->GetMaterialManager() == nullptr)
		{
			return;
		}

		DestroyParallelCommandRecording();
		DestroyHairLightingDescriptors();
		DestroyHairCompositeDescriptors();
		DestroyTransmissionGlassDescriptors();
		renderPassManager->DestroySceneResolutionRenderPasses();
		m_Scene->GetMaterialManager()->ClearResolutionDependentRenderData(
			m_VansVKLogicDevice);

		m_RenderWidth = renderExtent.width;
		m_RenderHeight = renderExtent.height;
		renderPassManager->SetupVansDeferredRenderPass(
			m_VansVKLogicDevice, m_VansVKCommandBuffer, m_VansVKGraphicsQueue,
			renderExtent);
		renderPassManager->SetupVansSkyMotionVectorRenderPass(m_VansVKLogicDevice, renderExtent);
		renderPassManager->SetupVansHairDeepOpacityPass(m_VansVKLogicDevice, renderExtent);
		renderPassManager->SetupVansHairVisibilityPass(m_VansVKLogicDevice, renderExtent);
		renderPassManager->SetupVansHairLightingPass(m_VansVKLogicDevice, renderExtent);
		renderPassManager->SetupVansDecalRenderPass(m_VansVKLogicDevice, renderExtent);
		renderPassManager->SetupVansScreenSpaceEffectsPass(m_VansVKLogicDevice, renderExtent);
		renderPassManager->SetupVansWaterGBufferPass(m_VansVKLogicDevice, renderExtent);
		PrepareResolutionDependentRenderingData();
		if (!m_Scene->ReinitializeEnvironmentRendering(
			m_AtmosphereQualityConfig,
			renderExtent.width,
			renderExtent.height))
		{
			VANS_LOG_ERROR("[Atmosphere] Failed to rebuild view-dependent resources");
		}
		if (auto* waterSystem = m_Scene->GetWaterSystem())
		{
			auto* materialManager = m_Scene->GetMaterialManager();
			auto* hzbTexture = materialManager
				? materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_HZB_RESULT)
				: nullptr;
			waterSystem->ReinitializeResolutionResources(
				renderExtent.width,
				renderExtent.height,
				renderPassManager,
				m_Scene->GetGlobalDescriptorSetLayout(),
				m_Scene->GetGlobalDescriptorSet(),
				hzbTexture ? &hzbTexture->GetImage() : nullptr);
		}
		SetupHairLightingDescriptors(renderPassManager);
		SetupHairCompositeDescriptors(renderPassManager);
		SetupTransmissionGlassDescriptors(renderPassManager);
		ResetFeatureDescriptorSets();
		m_Scene->MarkRenderNodeDescriptorSetsDirty();
		m_UpscalerManager.GetHistory().RequestReset(
			VansUpscalerResetReason::RenderSizeChange);
	}

	float VansVKDevice::GetTemporalUpscaleMipBias() const
	{
		const VkExtent2D outputExtent = CalculateUpscalerOutputExtent();
		return VansUpscaleResolutionPolicy::ComputeMipBias(
			{ m_RenderWidth, m_RenderHeight },
			{ outputExtent.width, outputExtent.height });
	}

	VansUpscalerSelectionChange VansVKDevice::RequestUpscalerConfig(
		const VansUpscalerConfig& requestedConfig,
		uint32_t outputWidth,
		uint32_t outputHeight)
	{
		// Project settings can be applied before the presentation surface has
		// established its output extent. Preserve the current renderer fallback
		// instead of turning the unknown extent into a literal 1x1 target.
		if (outputWidth == 0 || outputHeight == 0)
		{
			const VkExtent2D fallbackExtent = CalculateUpscalerOutputExtent();
			if (fallbackExtent.width > 0 && fallbackExtent.height > 0)
			{
				VANS_LOG("[Upscaler] Output extent is not available; preserving "
					<< fallbackExtent.width << "x" << fallbackExtent.height
					<< " while applying configuration");
				outputWidth = fallbackExtent.width;
				outputHeight = fallbackExtent.height;
			}
		}
		outputWidth = std::max(outputWidth, 1u);
		outputHeight = std::max(outputHeight, 1u);
		VansUpscalerCapabilitySet capabilities;
		capabilities.off = GetUpscalerCapabilities(VansUpscalerBackend::Off);
		capabilities.fsr = GetUpscalerCapabilities(VansUpscalerBackend::FSR);
		capabilities.dlss = GetUpscalerCapabilities(VansUpscalerBackend::DLSS);
		const VansUpscalerSelectionChange change =
			m_UpscalerManager.RequestConfig(requestedConfig, capabilities);
		if (!change.accepted)
		{
			VANS_LOG_ERROR("[Upscaler] Rejected configuration: " << change.error);
			return change;
		}

		const bool extentChanged =
			m_RequestedUpscalerOutputExtent.width != outputWidth ||
			m_RequestedUpscalerOutputExtent.height != outputHeight;
		const VansUpscalerConfig& effective = m_UpscalerManager.GetEffectiveConfig();
		const bool sharpnessChanged =
			std::abs(m_FSRController.GetSharpness() - effective.fsrSharpness) > 0.0001f;

		m_RequestedUpscalerOutputExtent = { outputWidth, outputHeight };
		m_FSRController.SetSharpness(effective.fsrSharpness);
		m_FSRController.SetDebugViewEnabled(effective.fsrDebugView);
		m_UpscalerConfigDirty =
			m_UpscalerConfigDirty || change.RequiresContextRebuild() || extentChanged;

		if (sharpnessChanged)
			VANS_LOG("[FSR] RCAS sharpness=" << effective.fsrSharpness);
		return change;
	}

	void VansVKDevice::ApplyRenderRuntimeConfig(
		const VansRenderRuntimeConfig& config,
		uint32_t outputWidth,
		uint32_t outputHeight)
	{
		m_AtmosphereQualityConfig = config.atmosphere;
		m_NearMediaQualityConfig = config.nearMedia;
		m_CloudShadowQualityConfig = config.cloudShadow;
		// 项目可声明独立于宿主窗口的最终输出分辨率；旧项目的 0x0 配置仍跟随窗口。
		const uint32_t requestedOutputWidth = config.output.HasExplicitExtent()
			? config.output.width
			: outputWidth;
		const uint32_t requestedOutputHeight = config.output.HasExplicitExtent()
			? config.output.height
			: outputHeight;
		RequestUpscalerConfig(
			config.upscaler,
			requestedOutputWidth,
			requestedOutputHeight);
		ApplyCommandRecordingSettings(
			config.commandRecording.parallelEnabled,
			config.commandRecording.frameContextRingEnabled,
			config.commandRecording.framesInFlight,
			config.commandRecording.asyncComputeEnabled);
	}

	void VansVKDevice::ProcessPendingUpscalerConfig()
	{
		if (!m_UpscalerConfigDirty)
			return;

		VkExtent2D requestedExtent = CalculateUpscalerOutputExtent();
		const VkExtent2D requestedRenderExtent = CalculateUpscalerRenderExtent();
		const VkExtent2D currentExtent = m_FSREnabled
			? m_FSRController.GetDisplayExtent()
			: VkExtent2D{ 0, 0 };
		m_UpscalerConfigDirty = false;
		const VansUpscalerConfig& effective = m_UpscalerManager.GetEffectiveConfig();
		if (requestedExtent.width == currentExtent.width &&
			requestedExtent.height == currentExtent.height &&
			requestedRenderExtent.width == m_RenderWidth &&
			requestedRenderExtent.height == m_RenderHeight &&
			effective.backend == VansUpscalerBackend::FSR)
		{
			return;
		}

		WaitForDevice();
		auto* renderPassManager = VansRenderPassManager::GetInstance();
		renderPassManager->DestroySceneUIRenderPass();
		renderPassManager->DestroyDisplayPostProcessPass();
		CleanupFSR();
		CleanupDLSS();
		RecreateSceneResolutionResources(requestedRenderExtent);
		if (!EnsureUpscalerOutputImage(requestedExtent))
		{
			m_UpscalerManager.ActivateRuntimeFallback(
				VansUpscalerBackend::Off,
				VansUpscaleQualityMode::NativeAA,
				VansUpscalerFallbackReason::RuntimeUnavailable,
				"Unified upscaler output allocation failed");
			return;
		}
		if (effective.backend == VansUpscalerBackend::DLSS)
		{
			if (!InitializeDLSS())
			{
				m_UpscalerManager.ActivateRuntimeFallback(
					VansUpscalerBackend::FSR,
					effective.quality,
					VansUpscalerFallbackReason::ContextCreationFailed,
					"DLSS context creation failed; using FSR");
				RecreateSceneResolutionResources(CalculateUpscalerRenderExtent());
				if (!InitializeFSR())
				{
					m_UpscalerManager.ActivateRuntimeFallback(
						VansUpscalerBackend::Off,
						VansUpscaleQualityMode::NativeAA,
						VansUpscalerFallbackReason::ContextCreationFailed,
						"DLSS and FSR context creation failed; using native output");
					RecreateSceneResolutionResources(CalculateUpscalerRenderExtent());
				}
			}
		}
		else if (effective.backend == VansUpscalerBackend::FSR)
		{
			if (!InitializeFSR())
			{
				m_UpscalerManager.ActivateRuntimeFallback(
					VansUpscalerBackend::Off,
					VansUpscaleQualityMode::NativeAA,
					VansUpscalerFallbackReason::ContextCreationFailed,
					"FSR context creation failed; using native output");
				requestedExtent = CalculateUpscalerOutputExtent();
				RecreateSceneResolutionResources(CalculateUpscalerRenderExtent());
			}
		}
		else
		{
			m_UpscalerManager.GetHistory().ClearForOffBackend();
		}
		const VansUpscalerConfig& active = m_UpscalerManager.GetEffectiveConfig();
		renderPassManager->SetupVansDisplayPostProcessPass(
			m_VansVKLogicDevice,
			m_UpscalerOutputImage,
			requestedExtent);
		renderPassManager->SetupVansSceneUIRenderPass(
			m_VansVKLogicDevice,
			renderPassManager->GetFinalDisplayColor().GetImageView(),
			requestedExtent);
		if (m_Scene != nullptr)
			m_Scene->MarkRenderNodeDescriptorSetsDirty();

		VANS_LOG("[Upscaler] desired=" << ToString(m_UpscalerManager.GetDesiredConfig().backend)
			<< " effective=" << ToString(active.backend)
			<< " quality=" << ToString(active.quality)
			<< " output=" << requestedExtent.width << "x" << requestedExtent.height
			<< " render=" << m_RenderWidth << "x" << m_RenderHeight
			<< " mipBias=" << GetTemporalUpscaleMipBias());
	}

	VansUpscalerCapabilities VansVKDevice::GetUpscalerCapabilities(
		VansUpscalerBackend backend) const
	{
		VansUpscalerCapabilities capabilities;
		capabilities.backend = backend;
		const auto qualityBit = [](VansUpscaleQualityMode quality)
		{
			return 1u << static_cast<std::uint32_t>(quality);
		};

		switch (backend)
		{
		case VansUpscalerBackend::Off:
			capabilities.compiledIn = true;
			capabilities.runtimeAvailable = true;
			capabilities.deviceSupported = true;
			capabilities.supportedQualityMask =
				qualityBit(VansUpscaleQualityMode::NativeAA);
			capabilities.featureVersion = "Built-in";
			break;
		case VansUpscalerBackend::FSR:
			capabilities.compiledIn = true;
			capabilities.runtimeAvailable = true;
			capabilities.deviceSupported = true;
			capabilities.supportedQualityMask =
				qualityBit(VansUpscaleQualityMode::NativeAA) |
				qualityBit(VansUpscaleQualityMode::Quality) |
				qualityBit(VansUpscaleQualityMode::Balanced) |
				qualityBit(VansUpscaleQualityMode::Performance) |
				qualityBit(VansUpscaleQualityMode::UltraPerformance);
			capabilities.featureVersion = "FidelityFX API";
			break;
		case VansUpscalerBackend::DLSS:
		#if defined(VANS_HAS_STREAMLINE)
			capabilities.compiledIn = true;
			capabilities.runtimeAvailable =
				VansStreamlineRuntime::Get().IsInitialized();
			capabilities.deviceSupported =
				VansStreamlineRuntime::Get().IsDLSSAvailable();
			capabilities.supportedQualityMask =
				qualityBit(VansUpscaleQualityMode::NativeAA) |
				qualityBit(VansUpscaleQualityMode::Quality) |
				qualityBit(VansUpscaleQualityMode::Balanced) |
				qualityBit(VansUpscaleQualityMode::Performance) |
				qualityBit(VansUpscaleQualityMode::UltraPerformance);
			capabilities.featureVersion =
				VansStreamlineRuntime::Get().GetFeatureVersion();
			capabilities.unavailableReason =
				VansStreamlineRuntime::Get().GetUnavailableReason();
		#else
			capabilities.compiledIn = false;
			capabilities.runtimeAvailable = false;
			capabilities.deviceSupported = false;
			capabilities.supportedQualityMask = 0;
			capabilities.featureVersion = "Not compiled";
			capabilities.unavailableReason =
				"Streamline DLSS is disabled in this build";
		#endif
			break;
		default:
			capabilities.unavailableReason = "Unknown upscaler backend";
			break;
		}
		return capabilities;
	}

	VansUpscalerRuntimeDiagnostics VansVKDevice::GetUpscalerDiagnostics() const
	{
		VansUpscalerRuntimeDiagnostics diagnostics;
		diagnostics.desired = m_UpscalerManager.GetDesiredConfig();
		diagnostics.effective = m_UpscalerManager.GetEffectiveConfig();
		diagnostics.fallbackReason = m_UpscalerManager.GetFallbackReason();
		diagnostics.fallbackMessage = m_UpscalerManager.GetFallbackMessage();
		diagnostics.renderExtent = { m_RenderWidth, m_RenderHeight };
		const VkExtent2D outputExtent = CalculateUpscalerOutputExtent();
		diagnostics.outputExtent = { outputExtent.width, outputExtent.height };
		diagnostics.mipBias = GetTemporalUpscaleMipBias();
		diagnostics.pendingResetReasons = m_UpscalerManager.GetHistory().GetPendingReasons();
		diagnostics.featureVersion =
			GetUpscalerCapabilities(diagnostics.effective.backend).featureVersion;

		if (diagnostics.effective.backend == VansUpscalerBackend::FSR ||
			diagnostics.desired.backend == VansUpscalerBackend::FSR)
		{
			const VansFSRDiagnostics& fsr = m_FSRController.GetDiagnostics();
			diagnostics.contextReady = fsr.contextReady;
			diagnostics.lastDispatchSucceeded = fsr.lastDispatchSucceeded;
			diagnostics.lastDispatchReset = fsr.lastDispatchReset;
			diagnostics.backendCreateCode = fsr.lastCreateReturnCode;
			diagnostics.backendQueryCode = fsr.lastQueryReturnCode;
			diagnostics.backendDispatchCode = fsr.lastDispatchReturnCode;
			diagnostics.backendAuxiliaryCode = fsr.lastReactiveReturnCode;
			diagnostics.successfulDispatchCount = fsr.successfulDispatchCount;
			diagnostics.failedDispatchCount = fsr.failedDispatchCount;
			diagnostics.auxiliaryDispatchCount = fsr.generatedReactiveMaskCount;
			diagnostics.gpuMemoryUsageBytes = fsr.gpuMemoryUsageBytes;
			diagnostics.gpuMemoryAliasableBytes = fsr.gpuMemoryAliasableBytes;
			diagnostics.jitterPhaseCount = fsr.jitterPhaseCount;
			diagnostics.lastError = fsr.lastError;
		}
		else if (diagnostics.effective.backend == VansUpscalerBackend::DLSS ||
			diagnostics.desired.backend == VansUpscalerBackend::DLSS)
		{
			const VansDLSSDiagnostics& dlss = m_DLSSController.GetDiagnostics();
			diagnostics.contextReady = dlss.contextReady;
			diagnostics.lastDispatchSucceeded = dlss.lastDispatchSucceeded;
			diagnostics.backendCreateCode = dlss.lastCreateCode;
			diagnostics.backendDispatchCode = dlss.lastDispatchCode;
			diagnostics.successfulDispatchCount = dlss.successfulDispatchCount;
			diagnostics.failedDispatchCount = dlss.failedDispatchCount;
			diagnostics.jitterPhaseCount =
				VansTemporalJitterSequence::CalculatePhaseCount(
					m_RenderWidth, outputExtent.width);
			diagnostics.lastError = dlss.lastError;
		}
		return diagnostics;
	}
}
