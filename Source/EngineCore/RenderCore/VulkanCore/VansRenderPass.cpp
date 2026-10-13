#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansRenderPass.h"
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "VansVKSurface.h"
#include "VansRenderGraphVulkanSync.h"
#include "../VansRenderBootstrapSettings.h"
#include "../../Util/VansLog.h"
#include <iostream>
#include <algorithm>
#include <vector>
#include <stdexcept>

VansGraphics::VansRenderPassManager* VansGraphics::VansRenderPassManager::instance = nullptr;

VkImageUsageFlags VansGraphics::VansRenderPassManager::GetSceneColorImageUsageFlags()
{
	return VansRenderGraphVulkanSyncMapper::BuildImageUsage({
		VansRenderResourceUsage::ColorAttachmentWrite,
		VansRenderResourceUsage::SampledRead,
		VansRenderResourceUsage::StorageWrite,
		VansRenderResourceUsage::TransferSrc,
		VansRenderResourceUsage::TransferDst
	});
}

namespace
{
	bool EndSubmitAndResetOneTimeCommand(
		VansGraphics::VansVKCommandBuffer& commandBuffer,
		VkQueue& queue,
		VkDevice& device,
		const char* context)
	{
		if (!commandBuffer.EndCommandBufferRecord())
		{
			VANS_LOG_ERROR(context << ": failed to end one-time command buffer.");
			return false;
		}
		// These barriers establish the initial layout for images that the very
		// next frame uses.  Wait before resetting/reusing the one-time command
		// buffer; otherwise the GPU can observe UNDEFINED while the tracker says
		// GENERAL/SHADER_READ_ONLY during first submit.
		if (!VansGraphics::VansVKCommandBuffer::SubmitCommands(
			queue,
			device,
			{ commandBuffer.GetVKCommandBuffer() },
			{},
			{},
			commandBuffer.m_CommandBufferFinishSubmitFence,
			true))
		{
			VANS_LOG_ERROR(context << ": failed to submit one-time command buffer.");
			return false;
		}
		if (!commandBuffer.ResetCommandBuffer(false))
		{
			VANS_LOG_ERROR(context << ": failed to reset one-time command buffer.");
			return false;
		}
		return true;
	}
}

void VansGraphics::VansVKRenderPass::CreateRenderPass(VkDevice& logic_device, std::vector<VkAttachmentDescription>& attachments, std::vector<SubpassParameters>& subpass_params, std::vector<VkSubpassDependency>& subpass_dependency, const VkExtent2D& resolution)
{
	//防止y flip问题
	//https://www.saschawillems.de/blog/2019/03/29/flipping-the-vulkan-viewport/
	m_RenderPassViewport = 
	{
			0.0f,
			(float)resolution.height,
			(float)resolution.width,
			-(float)resolution.height,
			0.0f,
			1.0f
	};
	m_RenderPassScissor =
	{
		{0,0},
		{resolution.width,resolution.height}
	};

	m_AttachmentDescs.clear();
	for (auto attachment : attachments)
	{
		m_AttachmentDescs.push_back(attachment);
	}

	m_SubpassDescs.clear();
	for (auto& subpass_description : subpass_params)
	{
		// Vulkan 要求 count 为 0 时对应指针为 nullptr。
		// 尤其 pResolveAttachments：若没有 MSAA resolve，却传入空 vector 的 data()，
		// 驱动可能仍按 colorAttachmentCount 读取无效 resolve attachment，导致 subpass RT 行为未定义。
		const VkAttachmentReference* inputAttachments = subpass_description.InputAttachments.empty()
			? nullptr : subpass_description.InputAttachments.data();
		const VkAttachmentReference* colorAttachments = subpass_description.ColorAttachments.empty()
			? nullptr : subpass_description.ColorAttachments.data();
		const VkAttachmentReference* resolveAttachments = subpass_description.ResolveAttachments.empty()
			? nullptr : subpass_description.ResolveAttachments.data();
		const uint32_t* preserveAttachments = subpass_description.PreserveAttachments.empty()
			? nullptr : subpass_description.PreserveAttachments.data();

		m_SubpassDescs.push_back(
			{
				0,
				subpass_description.PipelineType,
				static_cast<uint32_t>(subpass_description.InputAttachments.size()),
				inputAttachments,
				static_cast<uint32_t>(subpass_description.ColorAttachments.size()),
				colorAttachments,
				resolveAttachments,
				subpass_description.DepthStencilAttachment,
				static_cast<uint32_t>(subpass_description.PreserveAttachments.size()),
				preserveAttachments
			}
		);
	}

	m_SubpassDependencies.clear();
	for (auto dependency : subpass_dependency)
	{
		m_SubpassDependencies.push_back(dependency);
	}

	VkRenderPassCreateInfo render_pass_create_info = 
	{
		 VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		 nullptr,
		 0,
		 static_cast<uint32_t>(m_AttachmentDescs.size()),
		 m_AttachmentDescs.data(),
		 static_cast<uint32_t>(m_SubpassDescs.size()),
		 m_SubpassDescs.data(),
		 static_cast<uint32_t>(m_SubpassDependencies.size()),
		 m_SubpassDependencies.data()
	};
	VkResult result = VansGraphics::vkCreateRenderPass(logic_device, &render_pass_create_info, nullptr, &m_RenderPass);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Could not create a render pass.");
	}
}

void VansGraphics::VansVKRenderPass::DestroyRenderPass(VkDevice& logic_device)
{
	for (int i = 0; i < m_FrameBuffers.size(); i++)
	{
		m_FrameBuffers[i].DestroyFrameBuffer(logic_device);
	}

	if (VK_NULL_HANDLE != m_RenderPass)
	{
		VansGraphics::vkDestroyRenderPass(logic_device, m_RenderPass, nullptr);
		m_RenderPass = VK_NULL_HANDLE;
	}
}

void VansGraphics::VansFrameBuffer::CreateFrameBuffer(VkDevice& logic_device, VkRenderPass& render_pass, const std::vector<VkImageView>& image_views, VkExtent3D framebuffer_size)
{
	if (logic_device == VK_NULL_HANDLE || render_pass == VK_NULL_HANDLE ||
		framebuffer_size.width == 0 || framebuffer_size.height == 0 || framebuffer_size.depth == 0)
	{
		VANS_LOG_ERROR("[VansFrameBuffer] Refusing framebuffer creation with an invalid device, render pass, or extent.");
		return;
	}
	for (std::size_t attachmentIndex = 0; attachmentIndex < image_views.size(); ++attachmentIndex)
	{
		if (image_views[attachmentIndex] == VK_NULL_HANDLE)
		{
			VANS_LOG_ERROR("[VansFrameBuffer] Refusing framebuffer creation: attachment "
				<< attachmentIndex << " is VK_NULL_HANDLE (attachments=" << image_views.size()
				<< ", extent=" << framebuffer_size.width << "x" << framebuffer_size.height << ").");
			return;
		}
	}
	VkFramebufferCreateInfo framebuffer_create_info = 
	{
		 VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		 nullptr,
		 0,
		 render_pass,
		 static_cast<uint32_t>(image_views.size()),
		 image_views.data(),
		 framebuffer_size.width,
		 framebuffer_size.height,
		 framebuffer_size.depth
	};

	VkResult result = VansGraphics::vkCreateFramebuffer(logic_device, &framebuffer_create_info, nullptr, &m_FrameBuffer);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Could not create a framebuffer.");
	}
}

void VansGraphics::VansFrameBuffer::DestroyFrameBuffer(VkDevice& logic_device)
{
	if (m_FrameBuffer != VK_NULL_HANDLE)
	{
		VansGraphics::vkDestroyFramebuffer(logic_device, m_FrameBuffer, nullptr);
		m_FrameBuffer = VK_NULL_HANDLE;
	}
}

VansGraphics::VansRenderPassManager::VansRenderPassManager()
{

}

bool VansGraphics::VansRenderPassManager::CreateGBufferFrameTarget(std::uint32_t slot)
{
	if (slot >= m_GBufferFrameTargets.size() || m_LogicDevice == VK_NULL_HANDLE ||
		m_GBufferExtent.width == 0 || m_GBufferExtent.height == 0)
		return false;
	GBufferFrameTargets& targets = m_GBufferFrameTargets[slot];
	const VkExtent3D extent{ m_GBufferExtent.width, m_GBufferExtent.height, 1 };
	const VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const bool created =
		targets.depth.CreateVulkanImage(m_LogicDevice, extent, VK_FORMAT_D32_SFLOAT_S8_UINT,
			1, 1, VK_IMAGE_TYPE_2D, depthUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true) &&
		targets.normal.CreateVulkanImage(m_LogicDevice, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
			1, 1, VK_IMAGE_TYPE_2D, colorUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true) &&
		targets.motionVector.CreateVulkanImage(m_LogicDevice, extent, VK_FORMAT_R16G16_SFLOAT,
			1, 1, VK_IMAGE_TYPE_2D, colorUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true) &&
		targets.gbuffer0.CreateVulkanImage(m_LogicDevice, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
			1, 1, VK_IMAGE_TYPE_2D, colorUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true) &&
		targets.gbuffer1.CreateVulkanImage(m_LogicDevice, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
			1, 1, VK_IMAGE_TYPE_2D, colorUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true) &&
		targets.gbuffer2.CreateVulkanImage(m_LogicDevice, extent, VK_FORMAT_R32G32B32A32_SFLOAT,
			1, 1, VK_IMAGE_TYPE_2D, colorUsage, VK_SAMPLE_COUNT_1_BIT, false, false, true);
	if (!created)
		DestroyGBufferFrameTarget(slot);
	else
		++m_GBufferFrameTargetGeneration;
	return created;
}

void VansGraphics::VansRenderPassManager::DestroyGBufferFrameTarget(std::uint32_t slot)
{
	GBufferFrameTargets& targets = m_GBufferFrameTargets.at(slot);
	targets.depth.DestroyVulkanImage(m_LogicDevice);
	targets.normal.DestroyVulkanImage(m_LogicDevice);
	targets.motionVector.DestroyVulkanImage(m_LogicDevice);
	targets.gbuffer0.DestroyVulkanImage(m_LogicDevice);
	targets.gbuffer1.DestroyVulkanImage(m_LogicDevice);
	targets.gbuffer2.DestroyVulkanImage(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::RecordGBufferFrameTargetInitialLayouts(
	VansVKCommandBuffer& commandBuffer, std::uint32_t slot)
{
	GBufferFrameTargets& targets = m_GBufferFrameTargets.at(slot);
	VansVKImage* images[] = {
		&targets.depth, &targets.normal, &targets.motionVector,
		&targets.gbuffer0, &targets.gbuffer1, &targets.gbuffer2
	};
	for (VansVKImage* image : images)
		image->SetImageMemoryBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			{ image->m_VansVKImage, VK_ACCESS_NONE, VK_ACCESS_NONE,
				image->m_ImageLayout, VK_IMAGE_LAYOUT_GENERAL,
				VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image->m_ImageAspect });
}

bool VansGraphics::VansRenderPassManager::SetGBufferFrameTargetCount(
	std::uint32_t frameTargetCount, VansVKCommandBuffer& commandBuffer, VkQueue& queue)
{
	if (frameTargetCount == 0 || frameTargetCount > m_GBufferFrameTargets.size() ||
		m_GBufferFrameTargetCount == 0 || m_LogicDevice == VK_NULL_HANDLE ||
		m_GBufferExtent.width == 0 || m_GBufferExtent.height == 0 ||
		m_VansGBufferPass.m_RenderPass == VK_NULL_HANDLE)
		return false;

	auto releaseLast = [&]()
	{
		const std::uint32_t slot = --m_GBufferFrameTargetCount;
		VansVKRenderPass* depthPasses[] = {
			&m_VansTransparentPass, &m_VansSkyMotionVectorPass, &m_VansWaterGBufferPass
		};
		for (VansVKRenderPass* pass : depthPasses)
		{
			if (pass->m_FrameBuffers.size() > slot)
			{
				pass->m_FrameBuffers[slot].DestroyFrameBuffer(m_LogicDevice);
				pass->m_FrameBuffers.pop_back();
			}
		}
		if (m_VansGBufferPass.m_FrameBuffers.size() > slot)
		{
			m_VansGBufferPass.m_FrameBuffers[slot].DestroyFrameBuffer(m_LogicDevice);
			m_VansGBufferPass.m_FrameBuffers.pop_back();
		}
		DestroyGBufferFrameTarget(slot);
	};
	while (m_GBufferFrameTargetCount > frameTargetCount)
		releaseLast();

	const std::uint32_t originalCount = m_GBufferFrameTargetCount;
	while (m_GBufferFrameTargetCount < frameTargetCount)
	{
		const std::uint32_t slot = m_GBufferFrameTargetCount;
		if (!CreateGBufferFrameTarget(slot))
			return false;
		auto createFramebuffer = [&](VansVKRenderPass& pass,
			const std::vector<VkImageView>& views)
		{
			if (pass.m_RenderPass == VK_NULL_HANDLE)
				return true;
			if (pass.m_FrameBuffers.size() != slot)
				return false;
			pass.m_FrameBuffers.resize(slot + 1);
			pass.m_FrameBuffers[slot].CreateFrameBuffer(m_LogicDevice, pass.m_RenderPass,
				views, { m_GBufferExtent.width, m_GBufferExtent.height, 1 });
			return pass.m_FrameBuffers[slot].GetFrameBuffer() != VK_NULL_HANDLE;
		};
		bool created = createFramebuffer(m_VansGBufferPass,
			{ GetNormal(slot).GetImageView(), GetGbuffer0(slot).GetImageView(),
			  GetGbuffer1(slot).GetImageView(), GetGbuffer2(slot).GetImageView(),
			  GetMotionVector(slot).GetImageView(), GetDepth(slot).GetDepthStencilView() });
		if (created)
			created = createFramebuffer(m_VansTransparentPass,
				{ m_ColorImage.GetImageView(), GetDepth(slot).GetDepthStencilView() });
		if (created)
			created = createFramebuffer(m_VansSkyMotionVectorPass,
				{ GetMotionVector(slot).GetImageView(), GetDepth(slot).GetDepthStencilView() });
		if (created)
			created = createFramebuffer(m_VansWaterGBufferPass,
				{ m_WaterGBufNormalImage.GetImageView(), m_WaterGBufScatterImage.GetImageView(),
				  m_WaterGBufAbsorptionImage.GetImageView(), m_WaterGBufLinearDepthImage.GetImageView(),
				  GetDepth(slot).GetDepthStencilView() });
		if (created && commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			RecordGBufferFrameTargetInitialLayouts(commandBuffer, slot);
			created = EndSubmitAndResetOneTimeCommand(commandBuffer, queue, m_LogicDevice,
				"SetGBufferFrameTargetCount");
		}
		else
			created = false;
		if (!created)
		{
			++m_GBufferFrameTargetCount;
			releaseLast();
			while (m_GBufferFrameTargetCount > originalCount)
				releaseLast();
			return false;
		}
		++m_GBufferFrameTargetCount;
	}
	if (m_GBufferFrameTargetCount != originalCount)
		VANS_LOG("[RenderPass] GBuffer frame targets=" << m_GBufferFrameTargetCount
			<< " extent=" << m_GBufferExtent.width << "x" << m_GBufferExtent.height);
	return true;
}

VansGraphics::VansRenderPassManager::FrameTargetCounts
VansGraphics::VansRenderPassManager::GetFrameTargetCounts() const
{
	return {
		GetGBufferFrameTargetCount(),
		GetDecalFrameTargetCount(),
		GetHairFrameTargetCount(),
		GetOpaqueSceneColorTargetCount(),
		GetRawOpaqueSceneColorTargetCount()
	};
}

bool VansGraphics::VansRenderPassManager::GrowFrameTargetsAtIdle(
	std::uint32_t frameTargetCount, VansVKCommandBuffer& commandBuffer, VkQueue& queue)
{
	const FrameTargetCounts previous = GetFrameTargetCounts();
	const auto fail = [&](const char* targetName)
	{
		VANS_LOG_ERROR("[RenderPass] Failed to allocate " << targetName
			<< " frame targets for requested slots.");
		return false;
	};
	if (previous.gbuffer != 0 && frameTargetCount > previous.gbuffer &&
		!SetGBufferFrameTargetCount(frameTargetCount, commandBuffer, queue))
		return fail("GBuffer");
	if (previous.decal != 0 && frameTargetCount > previous.decal &&
		!SetDecalFrameTargetCount(frameTargetCount))
		return fail("Decal");
	if (previous.hair != 0 && frameTargetCount > previous.hair &&
		!SetHairFrameTargetCount(frameTargetCount))
		return fail("Hair");
	if (previous.opaqueSceneColor != 0 && frameTargetCount > previous.opaqueSceneColor &&
		!SetOpaqueSceneColorTargetCount(frameTargetCount))
		return fail("OpaqueSceneColor");
	if (previous.rawOpaqueSceneColor != 0 && frameTargetCount > previous.rawOpaqueSceneColor &&
		!SetRawOpaqueSceneColorTargetCount(frameTargetCount, commandBuffer, queue))
		return fail("RawOpaqueSceneColor");
	return true;
}

void VansGraphics::VansRenderPassManager::ShrinkFrameTargetsAtIdle(
	const FrameTargetCounts& targetCounts, VansVKCommandBuffer& commandBuffer, VkQueue& queue)
{
	if (targetCounts.rawOpaqueSceneColor != 0 &&
		GetRawOpaqueSceneColorTargetCount() > targetCounts.rawOpaqueSceneColor)
		SetRawOpaqueSceneColorTargetCount(targetCounts.rawOpaqueSceneColor, commandBuffer, queue);
	if (targetCounts.opaqueSceneColor != 0 &&
		GetOpaqueSceneColorTargetCount() > targetCounts.opaqueSceneColor)
		SetOpaqueSceneColorTargetCount(targetCounts.opaqueSceneColor);
	if (targetCounts.hair != 0 && GetHairFrameTargetCount() > targetCounts.hair)
		SetHairFrameTargetCount(targetCounts.hair);
	if (targetCounts.decal != 0 && GetDecalFrameTargetCount() > targetCounts.decal)
		SetDecalFrameTargetCount(targetCounts.decal);
	if (targetCounts.gbuffer != 0 && GetGBufferFrameTargetCount() > targetCounts.gbuffer)
		SetGBufferFrameTargetCount(targetCounts.gbuffer, commandBuffer, queue);
}

void VansGraphics::VansRenderPassManager::ShrinkFrameTargetsAtIdle(
	std::uint32_t frameTargetCount, VansVKCommandBuffer& commandBuffer, VkQueue& queue)
{
	FrameTargetCounts targets = GetFrameTargetCounts();
	if (targets.gbuffer != 0) targets.gbuffer = frameTargetCount;
	if (targets.decal != 0) targets.decal = frameTargetCount;
	if (targets.hair != 0) targets.hair = frameTargetCount;
	if (targets.opaqueSceneColor != 0) targets.opaqueSceneColor = frameTargetCount;
	if (targets.rawOpaqueSceneColor != 0) targets.rawOpaqueSceneColor = frameTargetCount;
	ShrinkFrameTargetsAtIdle(targets, commandBuffer, queue);
}

VansGraphics::VansRenderPassManager* VansGraphics::VansRenderPassManager::GetInstance()
{
	if (instance == nullptr)
	{
		instance = new VansRenderPassManager();
	}
	return instance;
}

void VansGraphics::VansRenderPassManager::SetupVansDeferredRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue,
	const VkExtent2D& renderResolution, std::uint32_t frameTargetCount)
{
	VkExtent2D resolution = renderResolution;
	m_LogicDevice = logic_device;
	m_GBufferExtent = resolution;

	// 创建主渲染目标。RenderPass 已拆分，但 RT 仍集中在这里创建，避免其它 pass 依赖顺序变化。
	m_ColorImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		GetSceneColorImageUsageFlags(),
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);
	m_DiffuseExitantRadianceHistoryImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);
	if (!m_RawOpaqueSceneColorImages[0].CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
	))
		throw std::runtime_error("Cannot allocate RawOpaqueSceneColor frame target");
	m_RawOpaqueSceneColorExtent = resolution;
	m_RawOpaqueSceneColorTargetCount = 1;
	uint32_t opaqueSceneMipCount = 1u;
	for (uint32_t extent = resolution.width > resolution.height ? resolution.width : resolution.height;
		extent > 1u; extent >>= 1u)
	{
		++opaqueSceneMipCount;
	}
	m_OpaqueSceneColorExtent = resolution;
	m_OpaqueSceneColorMipCount = opaqueSceneMipCount;
	m_WaterBackgroundPyramidImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		opaqueSceneMipCount,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
	);
	if (!CreateGBufferFrameTarget(0))
		throw std::runtime_error("Cannot allocate GBuffer frame target");
	m_GBufferFrameTargetCount = 1;
#ifdef _DEBUG
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_ColorImage.GetImage());
	nameInfo.pObjectName = "ColorImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_RawOpaqueSceneColorImages[0].GetImage());
	nameInfo.pObjectName = "RawOpaqueSceneColorImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_DiffuseExitantRadianceHistoryImage.GetImage());
	nameInfo.pObjectName = "DiffuseExitantRadianceHistory";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_WaterBackgroundPyramidImage.GetImage());
	nameInfo.pObjectName = "WaterBackgroundPyramidImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(GetDepth(0).GetImage());
	nameInfo.pObjectName = "DepthImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(GetNormal(0).GetImage());
	nameInfo.pObjectName = "NormalImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(GetMotionVector(0).GetImage());
	nameInfo.pObjectName = "MotionVectorImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(GetGbuffer0(0).GetImage());
	nameInfo.pObjectName = "GBuffer0Image";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(GetGbuffer1(0).GetImage());
	nameInfo.pObjectName = "GBuffer1Image";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(GetGbuffer2(0).GetImage());
	nameInfo.pObjectName = "GBuffer2Image";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);
#endif

	// GBuffer pass：只写本帧 GBuffer / Depth，结束后立刻允许 compute 读取。
	std::vector<VkAttachmentDescription> gbufferAttachmentDescs =
	{
		// loadOp=CLEAR 时使用 UNDEFINED 作为 initialLayout：不关心旧内容，避免第 2+ 帧 layout 不匹配绘定义行为
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		// D32_SFLOAT_S8_UINT: 32位浮点深度+8位 stencil；同样用 UNDEFINED 作为 initialLayout
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
	};

	VkAttachmentReference gbufferDepthAttachment = { 5, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	std::vector<SubpassParameters> gbufferSubpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{
				{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
			},
			{},
			&gbufferDepthAttachment,
			{}
		}
	};
	std::vector<VkSubpassDependency> gbufferDependencies =
	{
		{
			VK_SUBPASS_EXTERNAL,
			0,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			0,
			VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};
	m_VansGBufferPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 1.0f, 0 },
	};
	m_VansGBufferPass.CreateRenderPass(logic_device, gbufferAttachmentDescs, gbufferSubpassParams, gbufferDependencies, resolution);
	m_VansGBufferPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> gbufferViews =
	{
		GetNormal(0).GetImageView(),
		GetGbuffer0(0).GetImageView(),
		GetGbuffer1(0).GetImageView(),
		GetGbuffer2(0).GetImageView(),
		GetMotionVector(0).GetImageView(),
		GetDepth(0).GetDepthStencilView()	// depth+stencil combined view：支持后续开启 stencil ops
	};
	m_VansGBufferPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansGBufferPass.m_RenderPass, gbufferViews, { resolution.width, resolution.height, 1 });
	if (m_VansGBufferPass.m_FrameBuffers[0].GetFrameBuffer() == VK_NULL_HANDLE ||
		!SetGBufferFrameTargetCount(frameTargetCount, command_buffer, queue))
		throw std::runtime_error("Cannot allocate GBuffer frame targets and framebuffers");

	// 延迟光照只写 RawOpaqueSceneColor 与 diffuse-exitant history。
	std::vector<VkAttachmentDescription> rawOpaqueLightingAttachmentDescs =
	{
		// 附件 0：SceneColor（CLEAR，先显式初始化为 shader-read，再由子通道切到 color attachment）
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		// 附件 1：clean diffuse exitant radiance.  SSGI samples this next frame;
		// it is separate from SceneColor to avoid temporal GI feedback from fog,
		// specular or post processing.
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		// 附件 2：Depth（LOAD，场景深度由 GBuffer pass 写入，供深度测试读取）
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
	};
	VkAttachmentReference rawOpaqueLightingDepthRef = { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> rawOpaqueLightingSubpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{},
			&rawOpaqueLightingDepthRef,
			{}
		}
	};
	std::vector<VkSubpassDependency> rawOpaqueLightingDependencies =
	{
		// GBuffer / Compute → Raw Opaque Lighting
		{
			VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		// Raw Opaque Lighting → Atmosphere Composite
		{
			0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};
	m_VansRawOpaqueLightingPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 1.0f, 0 },
	};
	m_VansRawOpaqueLightingPass.CreateRenderPass(logic_device, rawOpaqueLightingAttachmentDescs, rawOpaqueLightingSubpassParams, rawOpaqueLightingDependencies, resolution);
	m_VansRawOpaqueLightingPass.m_FrameBuffers.resize(1);
	{
		std::vector<VkImageView> rawOpaqueLightingViews =
		{
			m_RawOpaqueSceneColorImages[0].GetImageView(),
			m_DiffuseExitantRadianceHistoryImage.GetImageView(),
			GetDepth(0).GetDepthStencilView()
		};
		m_VansRawOpaqueLightingPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansRawOpaqueLightingPass.m_RenderPass, rawOpaqueLightingViews, { resolution.width, resolution.height, 1 });
	}

	// The main forward pass only composites transparent content into HDR SceneColor.
	// Display conversion is a dedicated display-resolution pass after FSR.
	// 大气合成前的表面阶段共享同一附件契约：Forward Opaque 与 Water Composite
	// 分别打开独立渲染阶段，并在 RawOpaqueSceneColor 与深度附件上保留已有内容。
	std::vector<VkAttachmentDescription> forwardOpaqueAttachmentDescs =
	{
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
	};
	VkAttachmentReference forwardOpaqueDepthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	std::vector<SubpassParameters> forwardOpaqueSubpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{},
			&forwardOpaqueDepthRef,
			{}
		}
	};
	std::vector<VkSubpassDependency> forwardOpaqueDependencies =
	{
		{
			VK_SUBPASS_EXTERNAL,
			0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			0,
			VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};
	m_VansPreAtmosphereSurfacePass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 1.0f, 0 },
	};
	m_VansPreAtmosphereSurfacePass.CreateRenderPass(logic_device, forwardOpaqueAttachmentDescs, forwardOpaqueSubpassParams, forwardOpaqueDependencies, resolution);
	m_VansPreAtmosphereSurfacePass.m_FrameBuffers.resize(1);
	{
		std::vector<VkImageView> forwardOpaqueViews =
		{
			m_RawOpaqueSceneColorImages[0].GetImageView(),
			GetDepth(0).GetDepthStencilView()
		};
		m_VansPreAtmosphereSurfacePass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansPreAtmosphereSurfacePass.m_RenderPass, forwardOpaqueViews, { resolution.width, resolution.height, 1 });
	}

	std::vector<VkAttachmentDescription> transparentAttachmentDescs =
	{
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
	};
	VkAttachmentReference transparentDepthAttachment = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> transparentSubpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{},
			&transparentDepthAttachment,
			{}
		}
	};
	std::vector<VkSubpassDependency> transparentDependencies =
	{
		{
			VK_SUBPASS_EXTERNAL,
			0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			0,
			VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};
	m_VansTransparentPass.m_ClearValues = {};
	m_VansTransparentPass.CreateRenderPass(logic_device, transparentAttachmentDescs, transparentSubpassParams, transparentDependencies, resolution);
	m_VansTransparentPass.m_FrameBuffers.resize(m_GBufferFrameTargetCount);
	for (std::uint32_t slot = 0; slot < m_GBufferFrameTargetCount; ++slot)
		m_VansTransparentPass.m_FrameBuffers[slot].CreateFrameBuffer(logic_device,
			m_VansTransparentPass.m_RenderPass,
			{ m_ColorImage.GetImageView(), GetDepth(slot).GetDepthStencilView() },
			{ resolution.width, resolution.height, 1 });

	m_LogicDevice = logic_device;
	if (!SetOpaqueSceneColorTargetCount(frameTargetCount))
		throw std::runtime_error("Cannot allocate OpaqueSceneColor frame targets");
	if (!SetRawOpaqueSceneColorTargetCount(frameTargetCount, command_buffer, queue))
		throw std::runtime_error("Cannot allocate RawOpaqueSceneColor frame targets");

	//record command buffer
	command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	//设置colordepoth的layout
	m_ColorImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_ColorImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_ColorImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_ColorImage.m_ImageAspect
		});
	// RawOpaqueSceneColor is consumed by the following forward/atmosphere passes.
	// Keep its tracked layout concrete before the first render pass instead of
	// relying on an implicit UNDEFINED transition, which leaves validation and
	// the VansVKImage tracker disagreeing after recreation.
	m_RawOpaqueSceneColorImages[0].SetImageMemoryBarrier(command_buffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		{
			m_RawOpaqueSceneColorImages[0].m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			m_RawOpaqueSceneColorImages[0].m_ImageLayout,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_RawOpaqueSceneColorImages[0].m_ImageAspect
		});

	// The first SSGI dispatch happens before the first Deferred pass writes this
	// attachment. Initialise the clean history explicitly so frame zero cannot
	// sample undefined radiance.
	m_DiffuseExitantRadianceHistoryImage.SetImageMemoryBarrier(command_buffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		{
			m_DiffuseExitantRadianceHistoryImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			m_DiffuseExitantRadianceHistoryImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_DiffuseExitantRadianceHistoryImage.m_ImageAspect
		});
	VkClearColorValue clearDiffuseExitant{};
	command_buffer.ClearColorImage(m_DiffuseExitantRadianceHistoryImage,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, clearDiffuseExitant);
	m_DiffuseExitantRadianceHistoryImage.SetImageMemoryBarrier(command_buffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		{
			m_DiffuseExitantRadianceHistoryImage.m_VansVKImage,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_DiffuseExitantRadianceHistoryImage.m_ImageAspect
		});

	RecordGBufferFrameTargetInitialLayouts(command_buffer, 0);
	EndSubmitAndResetOneTimeCommand(command_buffer, queue, logic_device, "SetupVansGBufferRenderPass");
}

bool VansGraphics::VansRenderPassManager::SetOpaqueSceneColorTargetCount(std::uint32_t frameTargetCount)
{
	if (frameTargetCount == 0 || frameTargetCount > m_OpaqueSceneColorImages.size() ||
		m_LogicDevice == VK_NULL_HANDLE || m_OpaqueSceneColorExtent.width == 0 ||
		m_OpaqueSceneColorExtent.height == 0 || m_OpaqueSceneColorMipCount == 0)
		return false;

	while (m_OpaqueSceneColorTargetCount > frameTargetCount)
		m_OpaqueSceneColorImages[--m_OpaqueSceneColorTargetCount].DestroyVulkanImage(m_LogicDevice);

	const std::uint32_t originalCount = m_OpaqueSceneColorTargetCount;
	while (m_OpaqueSceneColorTargetCount < frameTargetCount)
	{
		const std::uint32_t slot = m_OpaqueSceneColorTargetCount;
		VansVKImage& image = m_OpaqueSceneColorImages[slot];
		if (!image.CreateVulkanImage(m_LogicDevice,
			{m_OpaqueSceneColorExtent.width, m_OpaqueSceneColorExtent.height, 1},
			VK_FORMAT_R16G16B16A16_SFLOAT, m_OpaqueSceneColorMipCount, 1,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_SAMPLE_COUNT_1_BIT, false, false, true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		{
			image.DestroyVulkanImage(m_LogicDevice);
			while (m_OpaqueSceneColorTargetCount > originalCount)
				m_OpaqueSceneColorImages[--m_OpaqueSceneColorTargetCount].DestroyVulkanImage(m_LogicDevice);
			return false;
		}
#ifdef _DEBUG
		if (VansGraphics::vkSetDebugUtilsObjectNameEXT)
		{
			VkDebugUtilsObjectNameInfoEXT name{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
			name.objectType = VK_OBJECT_TYPE_IMAGE;
			name.objectHandle = reinterpret_cast<uint64_t>(image.GetImage());
			name.pObjectName = slot == 0 ? "OpaqueSceneColorImage" : "OpaqueSceneColorImage.Slot1";
			VansGraphics::vkSetDebugUtilsObjectNameEXT(m_LogicDevice, &name);
		}
#endif
		++m_OpaqueSceneColorTargetCount;
	}
	return true;
}

bool VansGraphics::VansRenderPassManager::SetRawOpaqueSceneColorTargetCount(
	std::uint32_t frameTargetCount, VansVKCommandBuffer& commandBuffer, VkQueue& queue)
{
	if (frameTargetCount == 0 || frameTargetCount > m_RawOpaqueSceneColorImages.size() ||
		m_RawOpaqueSceneColorTargetCount == 0 || m_LogicDevice == VK_NULL_HANDLE ||
		m_RawOpaqueSceneColorExtent.width == 0 || m_RawOpaqueSceneColorExtent.height == 0)
		return false;

	auto destroyLastSlot = [&]()
	{
		const std::uint32_t slot = --m_RawOpaqueSceneColorTargetCount;
		m_VansRawOpaqueLightingPass.m_FrameBuffers[slot].DestroyFrameBuffer(m_LogicDevice);
		m_VansPreAtmosphereSurfacePass.m_FrameBuffers[slot].DestroyFrameBuffer(m_LogicDevice);
		m_VansRawOpaqueLightingPass.m_FrameBuffers.pop_back();
		m_VansPreAtmosphereSurfacePass.m_FrameBuffers.pop_back();
		m_RawOpaqueSceneColorImages[slot].DestroyVulkanImage(m_LogicDevice);
	};
	while (m_RawOpaqueSceneColorTargetCount > frameTargetCount)
		destroyLastSlot();

	while (m_RawOpaqueSceneColorTargetCount < frameTargetCount)
	{
		const std::uint32_t slot = m_RawOpaqueSceneColorTargetCount;
		VansVKImage& image = m_RawOpaqueSceneColorImages[slot];
		if (!image.CreateVulkanImage(m_LogicDevice,
			{ m_RawOpaqueSceneColorExtent.width, m_RawOpaqueSceneColorExtent.height, 1 },
			VK_FORMAT_R16G16B16A16_SFLOAT, 1, 1, VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_SAMPLE_COUNT_1_BIT, false, false, true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
		{
			image.DestroyVulkanImage(m_LogicDevice);
			return false;
		}
		m_VansRawOpaqueLightingPass.m_FrameBuffers.resize(slot + 1);
		m_VansPreAtmosphereSurfacePass.m_FrameBuffers.resize(slot + 1);
		m_VansRawOpaqueLightingPass.m_FrameBuffers[slot].CreateFrameBuffer(
			m_LogicDevice, m_VansRawOpaqueLightingPass.m_RenderPass,
			{ image.GetImageView(), m_DiffuseExitantRadianceHistoryImage.GetImageView(),
			  GetDepth(slot).GetDepthStencilView() },
			{ m_RawOpaqueSceneColorExtent.width, m_RawOpaqueSceneColorExtent.height, 1 });
		m_VansPreAtmosphereSurfacePass.m_FrameBuffers[slot].CreateFrameBuffer(
			m_LogicDevice, m_VansPreAtmosphereSurfacePass.m_RenderPass,
			{ image.GetImageView(), GetDepth(slot).GetDepthStencilView() },
			{ m_RawOpaqueSceneColorExtent.width, m_RawOpaqueSceneColorExtent.height, 1 });
		if (m_VansRawOpaqueLightingPass.m_FrameBuffers[slot].GetFrameBuffer() == VK_NULL_HANDLE ||
			m_VansPreAtmosphereSurfacePass.m_FrameBuffers[slot].GetFrameBuffer() == VK_NULL_HANDLE)
		{
			++m_RawOpaqueSceneColorTargetCount;
			destroyLastSlot();
			return false;
		}
		if (!commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
		{
			++m_RawOpaqueSceneColorTargetCount;
			destroyLastSlot();
			return false;
		}
		image.SetImageMemoryBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			{ image.m_VansVKImage, VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			  image.m_ImageLayout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			  VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image.m_ImageAspect });
		if (!EndSubmitAndResetOneTimeCommand(commandBuffer, queue, m_LogicDevice,
			"SetRawOpaqueSceneColorTargetCount"))
		{
			++m_RawOpaqueSceneColorTargetCount;
			destroyLastSlot();
			return false;
		}
#ifdef _DEBUG
		if (VansGraphics::vkSetDebugUtilsObjectNameEXT)
		{
			VkDebugUtilsObjectNameInfoEXT name{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
			name.objectType = VK_OBJECT_TYPE_IMAGE;
			name.objectHandle = reinterpret_cast<uint64_t>(image.GetImage());
			name.pObjectName = "RawOpaqueSceneColorImage.Slot1";
			VansGraphics::vkSetDebugUtilsObjectNameEXT(m_LogicDevice, &name);
		}
#endif
		++m_RawOpaqueSceneColorTargetCount;
	}
	return true;
}

void VansGraphics::VansRenderPassManager::SetupVansShadowRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue)
{
	std::vector<VkAttachmentDescription> attachments_descriptions =
	{
		{
			0,
			VK_FORMAT_D32_SFLOAT,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		},
	};

	VkAttachmentReference depth_stencil_attachment =
	{
		 0,
		 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	std::vector<SubpassParameters> subpass_parameters =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{},
			{},
			&depth_stencil_attachment,
			{}
		},
	};

	m_VansShadowPass.m_ClearValues =
	{
		{ 1.0f, 0 },
	};

	std::vector<VkSubpassDependency> subpass_dependencies =
	{
		{
			VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};

	const uint32_t cascadeCount = kVansRenderBootstrapSettings.cascadeCount;
	const uint32_t cascadeSize = kVansRenderBootstrapSettings.cascadeShadowMapSize;
	VkExtent2D resolution = { cascadeSize, cascadeSize };

	m_VansShadowPass.CreateRenderPass(logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies, resolution);

	// One D32 array now serves the depth test, PCSS blocker search and comparison
	// PCF. This removes the duplicate full-resolution R32 color write/read path.
	m_CascadeShadowMapDepthImage.CreateVulkanImage(
		logic_device,
		{ cascadeSize, cascadeSize, 1 },
		VK_FORMAT_D32_SFLOAT,
		1,
		(uint32_t)cascadeCount,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT
	);

	// Create per-layer image views for framebuffer attachments and the raw
	// single-layer GI/ray-tracing readers.
	for (int i = 0; i < cascadeCount; ++i)
	{
		{
			VkImageViewCreateInfo viewInfo = {};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_CascadeShadowMapDepthImage.GetImage();
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = VK_FORMAT_D32_SFLOAT;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = (uint32_t)i;
			viewInfo.subresourceRange.layerCount = 1;
			VansGraphics::vkCreateImageView(logic_device, &viewInfo, nullptr, &m_CascadeDepthLayerViews[i]);
		}
	}

	// Create full-array D32 view for raw and comparison sampling.
	{
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_CascadeShadowMapDepthImage.GetImage();
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		viewInfo.format = VK_FORMAT_D32_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = (uint32_t)cascadeCount;
		VansGraphics::vkCreateImageView(logic_device, &viewInfo, nullptr, &m_CascadeShadowArrayView);
	}

	// Raw nearest sampler is used by blocker search/min-max construction.
	{
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		// PCSS blocker search needs unfiltered raw D32 values; filtering depth
		// before the blocker comparison produces the
		// broad contour bands visible on large penumbrae.
		samplerInfo.magFilter = VK_FILTER_NEAREST;
		samplerInfo.minFilter = VK_FILTER_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		VansGraphics::vkCreateSampler(logic_device, &samplerInfo, nullptr, &m_CascadeShadowSampler);
	}

	// Hardware comparison filtering replaces four explicit depth fetches and
	// comparisons per PCF tap.
	{
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_TRUE;
		samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		VansGraphics::vkCreateSampler(logic_device, &samplerInfo, nullptr, &m_CascadeShadowCompareSampler);
	}

#ifdef _DEBUG
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_CascadeShadowMapDepthImage.GetImage());
	nameInfo.pObjectName = "CascadeShadowMapDepth";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);
#endif

	// Create 4 framebuffers — one per cascade layer
	m_VansShadowPass.m_FrameBuffers.resize(cascadeCount);
	for (int i = 0; i < cascadeCount; ++i)
	{
		std::vector<VkImageView> image_views = {
			m_CascadeDepthLayerViews[i]
		};
		m_VansShadowPass.m_FrameBuffers[i].CreateFrameBuffer(
			logic_device, m_VansShadowPass.m_RenderPass, image_views,
			{ cascadeSize, cascadeSize, 1 });
	}

	m_LogicDevice = logic_device;

	// Match the render-pass initial/final layout before the first cascade draw.
	command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	m_CascadeShadowMapDepthImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_CascadeShadowMapDepthImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_CascadeShadowMapDepthImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_CascadeShadowMapDepthImage.m_ImageAspect
		});

	EndSubmitAndResetOneTimeCommand(command_buffer, queue, logic_device, "SetupVansShadowRenderPass");
}

void VansGraphics::VansRenderPassManager::SetupVansPunctualShadowRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue)
{
	std::vector<VkAttachmentDescription> attachments_descriptions =
	{
		{
			0,
			VK_FORMAT_D32_SFLOAT,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_LOAD,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
		},
	};

	VkAttachmentReference depth_stencil_attachment =
	{
		 0,
		 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	std::vector<SubpassParameters> subpass_parameters =
	{
		// #0 subpass
		//记录在attachemts中的索引，以及对应需要的layout
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{},
			{},
			&depth_stencil_attachment,
			{}
		},
	};

	m_VansPunctualShadowPass.m_ClearValues = {};

	// The atlas is sampled by both graphics and compute consumers between updates.
	// External dependencies make the cached read -> partial depth write -> cached
	// read ownership explicit without transitioning the atlas through GENERAL.
	std::vector<VkSubpassDependency> subpass_dependencies =
	{
		{
			VK_SUBPASS_EXTERNAL,
			0,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_DEPENDENCY_BY_REGION_BIT,
		},
		{
			0,
			VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT,
		},
	};

	VkExtent2D resolution = {
		kVansRenderBootstrapSettings.punctualShadowAtlasWidth,
		kVansRenderBootstrapSettings.punctualShadowAtlasHeight
	};

	m_VansPunctualShadowPass.CreateRenderPass(logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies, resolution);

	// Persistent sampled depth atlas. Dirty blocks are cleared explicitly inside
	// the render pass; LOAD/STORE preserves every clean cached block.
	m_VansPunctualShadowPass.m_FrameBuffers.resize(VANS_PUNCTUAL_SHADOW_ATLAS_COUNT);
	for (uint32_t atlasIndex = 0; atlasIndex < VANS_PUNCTUAL_SHADOW_ATLAS_COUNT; ++atlasIndex)
	{
		VansVKImage& atlas = m_PunctualShadowMapImages[atlasIndex];
		atlas.CreateVulkanImage(
			logic_device,
			{ resolution.width, resolution.height, 1 },
			VK_FORMAT_D32_SFLOAT,
			1,
			1,
			VK_IMAGE_TYPE_2D,
			VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
			VK_SAMPLE_COUNT_1_BIT,
			false,
			false,
			true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			true,
			VK_COMPARE_OP_LESS_OR_EQUAL);

#ifdef _DEBUG
		const std::string atlasName = "PunctualShadowDepthAtlas" + std::to_string(atlasIndex);
		VkDebugUtilsObjectNameInfoEXT nameInfo = {};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
		nameInfo.objectHandle = reinterpret_cast<uint64_t>(atlas.GetImage());
		nameInfo.pObjectName = atlasName.c_str();
		VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);
#endif

		std::vector<VkImageView> imageViews = { atlas.GetImageView() };
		m_VansPunctualShadowPass.m_FrameBuffers[atlasIndex].CreateFrameBuffer(
			logic_device,
			m_VansPunctualShadowPass.m_RenderPass,
			imageViews,
			{ resolution.width, resolution.height, 1 });
	}

	m_LogicDevice = logic_device;

	// Initialize the persistent atlas exactly once. A LOAD render pass may not
	// consume undefined contents on its first frame.
	command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	for (VansVKImage& atlas : m_PunctualShadowMapImages)
	{
		atlas.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{
				atlas.m_VansVKImage,
				VK_ACCESS_NONE,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				atlas.m_ImageLayout,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				atlas.m_ImageAspect
			});
		command_buffer.ClearDepthStencil(atlas, { 1.0f, 0 });
		atlas.SetImageMemoryBarrier(command_buffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			{
				atlas.m_VansVKImage,
				VK_ACCESS_TRANSFER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT,
				atlas.m_ImageLayout,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
				VK_QUEUE_FAMILY_IGNORED,
				VK_QUEUE_FAMILY_IGNORED,
				atlas.m_ImageAspect
			});
	}

	EndSubmitAndResetOneTimeCommand(command_buffer, queue, logic_device, "SetupVansPunctualShadowRenderPass");
}

// Sky motion is an overlay on the GBuffer velocity target. Geometry and terrain
// have already written their motion in the GBuffer pass; the loaded scene depth
// limits this pass to untouched far-plane pixels without changing depth.
void VansGraphics::VansRenderPassManager::SetupVansSkyMotionVectorRenderPass(
	VkDevice& logicDevice,
	const VkExtent2D& renderResolution)
{
	std::vector<VkAttachmentDescription> attachmentDescriptions =
	{
		{ 0, VK_FORMAT_R16G16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
	};

	VkAttachmentReference depthAttachment = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	std::vector<SubpassParameters> subpassParameters =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{},
			&depthAttachment,
			{}
		},
	};
	std::vector<VkSubpassDependency> subpassDependencies =
	{
		{
			VK_SUBPASS_EXTERNAL,
			0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			0,
			VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
	};

	m_VansSkyMotionVectorPass.CreateRenderPass(
		logicDevice,
		attachmentDescriptions,
		subpassParameters,
		subpassDependencies,
		renderResolution);
	m_VansSkyMotionVectorPass.m_FrameBuffers.resize(m_GBufferFrameTargetCount);
	for (std::uint32_t slot = 0; slot < m_GBufferFrameTargetCount; ++slot)
		m_VansSkyMotionVectorPass.m_FrameBuffers[slot].CreateFrameBuffer(
			logicDevice, m_VansSkyMotionVectorPass.m_RenderPass,
			{ GetMotionVector(slot).GetImageView(), GetDepth(slot).GetDepthStencilView() },
			{ renderResolution.width, renderResolution.height, 1 });
}

std::vector<VkDescriptorImageInfo> VansGraphics::VansRenderPassManager::GetPunctualShadowDescriptorInfos(
	VkImageLayout layout)
{
	std::vector<VkDescriptorImageInfo> descriptors;
	descriptors.reserve(VANS_PUNCTUAL_SHADOW_ATLAS_COUNT);
	for (VansVKImage& atlas : m_PunctualShadowMapImages)
		descriptors.push_back({ atlas.GetSampler(), atlas.GetImageView(), layout });
	return descriptors;
}

std::vector<VkDescriptorImageInfo> VansGraphics::VansRenderPassManager::GetPunctualShadowRawDescriptorInfos(
	VkSampler rawSampler,
	VkImageLayout layout)
{
	std::vector<VkDescriptorImageInfo> descriptors;
	descriptors.reserve(VANS_PUNCTUAL_SHADOW_ATLAS_COUNT);
	for (auto& shadowAtlas : m_PunctualShadowMapImages)
	{
		descriptors.push_back({ rawSampler, shadowAtlas.GetImageView(), layout });
	}
	return descriptors;
}

void VansGraphics::VansRenderPassManager::SetupVansUIRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surface, const VkExtent2D& renderResolution)
{
	std::vector<VkAttachmentDescription> attachments_descriptions =
	{
		//swapchain image
		{
			0,
			surface.m_VansVKSwapChainImageFormat,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			// 每帧 CLEAR 覆盖整张呈现图像，不依赖首次 UNDEFINED 或后续 PRESENT 的内容。
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		},
	};
	std::vector<SubpassParameters> subpass_parameters =
	{
		// #0 subpass
		//记录在attachemts中的索引，以及对应需要的layout
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{
				{
					0,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				}
			},
			{},
			nullptr,
			{}
		},
	};
	m_VansUIPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },
	};
	std::vector<VkSubpassDependency> subpass_dependencies;
	m_VansUIPass.CreateRenderPass(logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies, renderResolution);
	
	VkExtent2D resolution = renderResolution;
	m_VansUIPass.m_FrameBuffers.resize(surface.m_VansVKImageCount);
	for (int swapChainIndex = 0; swapChainIndex < surface.m_VansVKImageCount; swapChainIndex++)
	{
		std::vector<VkImageView> image_views =
		{
			surface.GetSwapChainImageView(swapChainIndex)
		};
		m_VansUIPass.m_FrameBuffers[swapChainIndex].CreateFrameBuffer(logic_device, m_VansUIPass.m_RenderPass, image_views, { resolution.width, resolution.height, 1 });
	}

	m_LogicDevice = logic_device;
}

void VansGraphics::VansRenderPassManager::SetupVansSceneUIRenderPass(
	VkDevice& logicDevice, VkPhysicalDevice physicalDevice,
	VkImageView finalDisplayImageView, const VkExtent2D& displayExtent)
{
	// Noesis geometry clips use stencil. Keep this attachment independent of
	// scene depth/stencil so UI cannot modify scene effects or depth contents.
	VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
	for (VkFormat candidate : { VK_FORMAT_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT,
		VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM_S8_UINT })
	{
		VkFormatProperties properties{};
		vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
		if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			stencilFormat = candidate;
			break;
		}
	}
	if (stencilFormat == VK_FORMAT_UNDEFINED || !m_SceneUIStencilImage.CreateVulkanImage(logicDevice,
		{ displayExtent.width, displayExtent.height, 1 }, stencilFormat, 1, 1,
		VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_SAMPLE_COUNT_1_BIT))
		throw std::runtime_error("Could not create the runtime UI stencil attachment");
	// FinalDisplayColor is loaded so Noesis can composite over the scene.
	// - LOAD_OP_LOAD：保留场景内容，Noesis 叠加渲染
	// - initialLayout = COLOR_ATTACHMENT_OPTIMAL（调用前已由 barrier 转换）
	// - finalLayout   = SHADER_READ_ONLY_OPTIMAL（供 ImGui 场景窗口采样）
	std::vector<VkAttachmentDescription> attachments_descriptions =
	{
		{
			0,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_LOAD,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		{ 0, stencilFormat, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL },
	};
	VkAttachmentReference stencilRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	std::vector<SubpassParameters> subpass_parameters =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{
				{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
			},
			{},
			&stencilRef,
			{}
		},
	};
	// Color is loaded; only the independent UI stencil attachment is cleared.
	m_VansSceneUIPass.m_ClearValues = { VkClearValue{}, VkClearValue{} };

	std::vector<VkSubpassDependency> subpass_dependencies;
	m_VansSceneUIPass.CreateRenderPass(
		logicDevice, attachments_descriptions, subpass_parameters, subpass_dependencies, displayExtent);

	// Noesis VK render device 使用 clipSpaceYInverted=true（Vulkan 原生 Y-down NDC），
	// 不需要负高度 viewport 翻转，覆盖 CreateRenderPass 写入的负高度 viewport，
	// 改为标准正向 viewport，确保 Noesis 上屏渲染方向正确。
	// 注意：只影响 SceneUI pass，不影响其他使用负高度 viewport 的场景渲染通道。
	m_VansSceneUIPass.m_RenderPassViewport = {
		0.0f,
		0.0f,
		static_cast<float>(displayExtent.width),
		static_cast<float>(displayExtent.height),
		0.0f,
		1.0f
	};

	// FinalDisplayColor is one offscreen image, not a swapchain image array.
	m_VansSceneUIPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> image_views = { finalDisplayImageView, m_SceneUIStencilImage.GetDepthStencilView() };
	m_VansSceneUIPass.m_FrameBuffers[0].CreateFrameBuffer(
		logicDevice, m_VansSceneUIPass.m_RenderPass, image_views,
		{ displayExtent.width, displayExtent.height, 1 });
}

void VansGraphics::VansRenderPassManager::SetupVansDisplayPostProcessPass(
	VkDevice& logic_device,
	VansVKImage& hdrInput,
	const VkExtent2D& displayExtent)
{
	m_DisplayPostProcessInput = &hdrInput;
	m_FinalDisplayColorImage.CreateVulkanImage(
		logic_device,
		{ displayExtent.width, displayExtent.height, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true);

	std::vector<VkAttachmentDescription> attachments =
	{
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
	};
	std::vector<SubpassParameters> subpasses =
	{
		{ VK_PIPELINE_BIND_POINT_GRAPHICS, {},
		  { { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } }, {}, nullptr, {} }
	};
	std::vector<VkSubpassDependency> dependencies =
	{
		{ VK_SUBPASS_EXTERNAL, 0,
		  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
		  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
		  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT },
		{ 0, VK_SUBPASS_EXTERNAL,
		  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT }
	};
	m_VansDisplayPostProcessPass.m_ClearValues = { { 0.0f, 0.0f, 0.0f, 1.0f } };
	m_VansDisplayPostProcessPass.CreateRenderPass(
		logic_device, attachments, subpasses, dependencies, displayExtent);
	m_VansDisplayPostProcessPass.m_FrameBuffers.resize(1);
	m_VansDisplayPostProcessPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device,
		m_VansDisplayPostProcessPass.m_RenderPass,
		{ m_FinalDisplayColorImage.GetImageView() },
		{ displayExtent.width, displayExtent.height, 1 });
	m_FinalDisplayColorImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_UNDEFINED);
}

void VansGraphics::VansRenderPassManager::SetupVansDecalRenderPass(
    VkDevice& logic_device, VansVKCommandBuffer& commandBuffer, VkQueue& queue,
    const VkExtent2D& renderResolution, std::uint32_t frameTargetCount)
{
    const VkFormat formats[] = { VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM };
    std::vector<VkAttachmentDescription> attachments;
    for (size_t i = 0; i < 3; ++i)
    {
        attachments.push_back({ 0, formats[i], VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
    }
    attachments.push_back({ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL });
    VkAttachmentReference depthRef = { 3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    std::vector<SubpassParameters> subpasses = {{ VK_PIPELINE_BIND_POINT_GRAPHICS, {},
        {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
         {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}}, {}, &depthRef, {} }};
    std::vector<VkSubpassDependency> dependencies = {
        // GBuffer 写入和上一帧的修饰采样必须完成，才能重用修饰附件。
        { VK_SUBPASS_EXTERNAL, 0,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, 0 },
        { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0 }
    };
    m_VansDecalPass.m_ClearValues.assign(3, VkClearValue{});
    m_VansDecalPass.CreateRenderPass(logic_device, attachments, subpasses, dependencies, renderResolution);
    m_LogicDevice = logic_device;
    m_DecalExtent = renderResolution;
    if (!SetDecalFrameTargetCount(frameTargetCount))
        throw std::runtime_error("Could not create the requested decal frame targets");

    m_EmptyDecalImage.CreateVulkanImage(logic_device, {1, 1, 1}, VK_FORMAT_R8G8B8A8_UNORM, 1, 1,
        VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SAMPLE_COUNT_1_BIT, false, false, true, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    commandBuffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    m_EmptyDecalImage.SetImageMemoryBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, { m_EmptyDecalImage.GetImage(), 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, VK_IMAGE_ASPECT_COLOR_BIT });
    commandBuffer.ClearColorImage(m_EmptyDecalImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkClearColorValue{});
    m_EmptyDecalImage.SetImageMemoryBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, { m_EmptyDecalImage.GetImage(), VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, VK_IMAGE_ASPECT_COLOR_BIT });
    EndSubmitAndResetOneTimeCommand(commandBuffer, queue, logic_device, "SetupVansDecalRenderPass");
}

bool VansGraphics::VansRenderPassManager::SetDecalFrameTargetCount(std::uint32_t frameTargetCount)
{
    if (frameTargetCount == 0 || frameTargetCount > m_DecalFrameTargets.size() ||
        m_LogicDevice == VK_NULL_HANDLE || m_VansDecalPass.m_RenderPass == VK_NULL_HANDLE ||
        m_DecalExtent.width == 0 || m_DecalExtent.height == 0)
        return false;

    auto destroyLastSlot = [&]()
    {
        const std::size_t slot = m_VansDecalPass.m_FrameBuffers.size() - 1u;
        m_VansDecalPass.m_FrameBuffers.back().DestroyFrameBuffer(m_LogicDevice);
        m_VansDecalPass.m_FrameBuffers.pop_back();
        DecalFrameTargets& targets = m_DecalFrameTargets[slot];
        targets.color.DestroyVulkanImage(m_LogicDevice);
        targets.normal.DestroyVulkanImage(m_LogicDevice);
        targets.roughness.DestroyVulkanImage(m_LogicDevice);
    };
    while (m_VansDecalPass.m_FrameBuffers.size() > frameTargetCount)
        destroyLastSlot();

    const std::size_t originalCount = m_VansDecalPass.m_FrameBuffers.size();
    const VkFormat formats[] = { VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM };
    while (m_VansDecalPass.m_FrameBuffers.size() < frameTargetCount)
    {
        const std::size_t slot = m_VansDecalPass.m_FrameBuffers.size();
        DecalFrameTargets& targets = m_DecalFrameTargets[slot];
        VansVKImage* images[] = { &targets.color, &targets.normal, &targets.roughness };
        bool created = true;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            if (!images[channel]->CreateVulkanImage(m_LogicDevice,
                { m_DecalExtent.width, m_DecalExtent.height, 1 }, formats[channel], 1, 1,
                VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_1_BIT,
                false, false, true, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE))
            {
                created = false;
                break;
            }
            images[channel]->SetTrackedImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        if (created)
        {
            m_VansDecalPass.m_FrameBuffers.resize(slot + 1u);
            m_VansDecalPass.m_FrameBuffers[slot].CreateFrameBuffer(
                m_LogicDevice, m_VansDecalPass.m_RenderPass,
                { targets.color.GetImageView(), targets.normal.GetImageView(),
                    targets.roughness.GetImageView(), GetDepth(slot).GetDepthStencilView() },
                { m_DecalExtent.width, m_DecalExtent.height, 1 });
            created = m_VansDecalPass.m_FrameBuffers[slot].GetFrameBuffer() != VK_NULL_HANDLE;
        }
        if (!created)
        {
            if (m_VansDecalPass.m_FrameBuffers.size() > slot)
                destroyLastSlot();
            else
            {
                targets.color.DestroyVulkanImage(m_LogicDevice);
                targets.normal.DestroyVulkanImage(m_LogicDevice);
                targets.roughness.DestroyVulkanImage(m_LogicDevice);
            }
            while (m_VansDecalPass.m_FrameBuffers.size() > originalCount)
                destroyLastSlot();
            return false;
        }
    }
    return true;
}



void VansGraphics::VansRenderPassManager::SetupVansHairVisibilityPass(
	VkDevice& logic_device, VkPhysicalDevice physicalDevice, const VkExtent2D& renderResolution)
{
    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R32_SFLOAT, &formatProperties);
    constexpr VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if ((formatProperties.optimalTilingFeatures & requiredFeatures) != requiredFeatures)
        throw std::runtime_error("Hair optical depth requires R32_SFLOAT color blending and storage reads");
    m_HairExtent = renderResolution;

	std::vector<VkAttachmentDescription> attachments =
	{
		{ 0, VK_FORMAT_R32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL },
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
	};

	VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> subpassParams =
	{
		{ VK_PIPELINE_BIND_POINT_GRAPHICS, {}, { {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL} }, {}, &depthRef, {} }
	};

	std::vector<VkSubpassDependency> dependencies =
	{
		{ VK_SUBPASS_EXTERNAL, 0,
		  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT },
		{ 0, VK_SUBPASS_EXTERNAL,
		  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_SHADER_READ_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT },
	};

	m_VansHairVisibilityPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 1.0f, 0 },
	};

	m_VansHairVisibilityPass.CreateRenderPass(logic_device, attachments, subpassParams, dependencies, renderResolution);
}

void VansGraphics::VansRenderPassManager::SetupVansHairDepthResolvePass(
    VkDevice& logic_device, const VkExtent2D& renderResolution)
{
    std::vector<VkAttachmentDescription> attachments = {
        { 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL }
    };
    VkAttachmentReference depth{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    std::vector<SubpassParameters> subpasses = {
        { VK_PIPELINE_BIND_POINT_GRAPHICS, {}, {}, {}, &depth, {} }
    };
    std::vector<VkSubpassDependency> dependencies = {
        { VK_SUBPASS_EXTERNAL, 0,
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
          VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_DEPENDENCY_BY_REGION_BIT },
        { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT }
    };
    m_VansHairDepthResolvePass.CreateRenderPass(logic_device, attachments, subpasses, dependencies, renderResolution);
}

void VansGraphics::VansRenderPassManager::SetupVansHairLightingPass(
	VkDevice& logic_device, const VkExtent2D& renderResolution, std::uint32_t frameTargetCount)
{

	std::vector<VkAttachmentDescription> attachments =
	{
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
	};

	VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> subpassParams =
	{
		{ VK_PIPELINE_BIND_POINT_GRAPHICS, {},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{}, &depthRef, {} }
	};

	std::vector<VkSubpassDependency> dependencies =
	{
		{ VK_SUBPASS_EXTERNAL, 0,
		  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		  0 },
		{ 0, VK_SUBPASS_EXTERNAL,
		  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_SHADER_READ_BIT,
		  0 },
	};

	m_VansHairLightingPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 0.0f },
	};

	m_VansHairLightingPass.CreateRenderPass(logic_device, attachments, subpassParams, dependencies, renderResolution);
	if (!SetHairFrameTargetCount(frameTargetCount))
		throw std::runtime_error("Cannot allocate Hair frame targets");
}

bool VansGraphics::VansRenderPassManager::SetHairFrameTargetCount(std::uint32_t frameTargetCount)
{
	if (frameTargetCount == 0 || frameTargetCount > m_HairFrameTargets.size() ||
		m_LogicDevice == VK_NULL_HANDLE || m_HairExtent.width == 0 || m_HairExtent.height == 0 ||
		m_VansHairVisibilityPass.m_RenderPass == VK_NULL_HANDLE ||
		m_VansHairDepthResolvePass.m_RenderPass == VK_NULL_HANDLE ||
		m_VansHairLightingPass.m_RenderPass == VK_NULL_HANDLE)
		return false;

	auto destroySlot = [&](std::uint32_t slot)
	{
		if (slot < m_VansHairVisibilityPass.m_FrameBuffers.size())
			m_VansHairVisibilityPass.m_FrameBuffers[slot].DestroyFrameBuffer(m_LogicDevice);
		if (slot < m_VansHairDepthResolvePass.m_FrameBuffers.size())
			m_VansHairDepthResolvePass.m_FrameBuffers[slot].DestroyFrameBuffer(m_LogicDevice);
		if (slot < m_VansHairLightingPass.m_FrameBuffers.size())
			m_VansHairLightingPass.m_FrameBuffers[slot].DestroyFrameBuffer(m_LogicDevice);
		HairFrameTargets& targets = m_HairFrameTargets[slot];
		targets.opticalDepth.DestroyVulkanImage(m_LogicDevice);
		targets.layerDepth.DestroyVulkanImage(m_LogicDevice);
		targets.color.DestroyVulkanImage(m_LogicDevice);
		targets.layerDepthBuffer.DestroyVulkanBuffer(m_LogicDevice);
	};
	auto shrinkTo = [&](std::uint32_t count)
	{
		while (m_HairFrameTargetCount > count)
			destroySlot(--m_HairFrameTargetCount);
		m_VansHairVisibilityPass.m_FrameBuffers.resize(count);
		m_VansHairDepthResolvePass.m_FrameBuffers.resize(count);
		m_VansHairLightingPass.m_FrameBuffers.resize(count);
	};
	if (m_HairFrameTargetCount >= frameTargetCount)
	{
		shrinkTo(frameTargetCount);
		return true;
	}

	const std::uint32_t originalCount = m_HairFrameTargetCount;
	const VkExtent3D imageExtent{m_HairExtent.width, m_HairExtent.height, 1};
	const VkExtent2D passExtent = m_HairExtent;
	// 与 HairVisibilityData.glsl 的 HAIR_LAYER_COUNT 保持一致。
	const VkDeviceSize bufferSize = VkDeviceSize(passExtent.width) * passExtent.height *
		3 * sizeof(std::uint32_t);
	while (m_HairFrameTargetCount < frameTargetCount)
	{
		const std::uint32_t slot = m_HairFrameTargetCount;
		HairFrameTargets& targets = m_HairFrameTargets[slot];
		const bool resourcesReady =
			targets.opticalDepth.CreateVulkanImage(m_LogicDevice, imageExtent, VK_FORMAT_R32_SFLOAT,
				1, 1, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
				VK_SAMPLE_COUNT_1_BIT, false, false, false) &&
			targets.layerDepth.CreateVulkanImage(m_LogicDevice, imageExtent, VK_FORMAT_D32_SFLOAT,
				1, 1, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
				VK_SAMPLE_COUNT_1_BIT) &&
			targets.color.CreateVulkanImage(m_LogicDevice, imageExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
				1, 1, VK_IMAGE_TYPE_2D,
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
					VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
				VK_SAMPLE_COUNT_1_BIT, false, false, true) &&
			targets.layerDepthBuffer.CreatVulkanBuffer(m_LogicDevice, bufferSize, VK_FORMAT_R32_UINT,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (!resourcesReady)
		{
			destroySlot(slot);
			shrinkTo(originalCount);
			return false;
		}
#ifdef _DEBUG
		if (VansGraphics::vkSetDebugUtilsObjectNameEXT)
		{
			VkDebugUtilsObjectNameInfoEXT name{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
			name.objectType = VK_OBJECT_TYPE_IMAGE;
			name.objectHandle = reinterpret_cast<uint64_t>(targets.opticalDepth.GetImage());
			name.pObjectName = slot == 0 ? "Hair.OpticalDepth" : "Hair.OpticalDepth.Slot1";
			VansGraphics::vkSetDebugUtilsObjectNameEXT(m_LogicDevice, &name);
			name.objectHandle = reinterpret_cast<uint64_t>(targets.color.GetImage());
			name.pObjectName = slot == 0 ? "Hair.Color" : "Hair.Color.Slot1";
			VansGraphics::vkSetDebugUtilsObjectNameEXT(m_LogicDevice, &name);
		}
#endif
		m_VansHairVisibilityPass.m_FrameBuffers.resize(slot + 1u);
		m_VansHairDepthResolvePass.m_FrameBuffers.resize(slot + 1u);
		m_VansHairLightingPass.m_FrameBuffers.resize(slot + 1u);
		m_VansHairVisibilityPass.m_FrameBuffers[slot].CreateFrameBuffer(m_LogicDevice,
			m_VansHairVisibilityPass.m_RenderPass,
			{targets.opticalDepth.GetImageView(), GetDepth(slot).GetDepthStencilView()},
			{passExtent.width, passExtent.height, 1});
		m_VansHairDepthResolvePass.m_FrameBuffers[slot].CreateFrameBuffer(m_LogicDevice,
			m_VansHairDepthResolvePass.m_RenderPass, {targets.layerDepth.GetImageView()},
			{passExtent.width, passExtent.height, 1});
		m_VansHairLightingPass.m_FrameBuffers[slot].CreateFrameBuffer(m_LogicDevice,
			m_VansHairLightingPass.m_RenderPass,
			{targets.color.GetImageView(), targets.layerDepth.GetImageView()},
			{passExtent.width, passExtent.height, 1});
		if (m_VansHairVisibilityPass.m_FrameBuffers[slot].GetFrameBuffer() == VK_NULL_HANDLE ||
			m_VansHairDepthResolvePass.m_FrameBuffers[slot].GetFrameBuffer() == VK_NULL_HANDLE ||
			m_VansHairLightingPass.m_FrameBuffers[slot].GetFrameBuffer() == VK_NULL_HANDLE)
		{
			destroySlot(slot);
			m_VansHairVisibilityPass.m_FrameBuffers.resize(slot);
			m_VansHairDepthResolvePass.m_FrameBuffers.resize(slot);
			m_VansHairLightingPass.m_FrameBuffers.resize(slot);
			shrinkTo(originalCount);
			return false;
		}
		++m_HairFrameTargetCount;
	}
	return true;
}

void VansGraphics::VansRenderPassManager::SetupVansWaterGBufferPass(
	VkDevice& logic_device, const VkExtent2D& renderResolution)
{
	// 四个颜色输出与 Water GBuffer shader 对应；主深度只参与只读测试。
	// 创建 Water GBuffer 纹理
	m_WaterGBufNormalImage.CreateVulkanImage(
		logic_device,
		{ renderResolution.width, renderResolution.height, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false, false, true);

	m_WaterGBufScatterImage.CreateVulkanImage(
		logic_device,
		{ renderResolution.width, renderResolution.height, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false, false, true);

	m_WaterGBufAbsorptionImage.CreateVulkanImage(
		logic_device,
		{ renderResolution.width, renderResolution.height, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false, false, true);

	m_WaterGBufLinearDepthImage.CreateVulkanImage(
		logic_device,
		{ renderResolution.width, renderResolution.height, 1 },
		VK_FORMAT_R32G32B32A32_SFLOAT,   // RGBA32F：RGB=世界位置，A=视空间线性深度，远距离遮挡需要完整精度
		1, 1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false, false, true);

	// render pass attachments
	std::vector<VkAttachmentDescription> attachments =
	{
		// Attachment 0：WaterGBuf_Normal（RGBA16F，每帧 CLEAR）
		{
			0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,  VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
		// Attachment 1：WaterGBuf_ScatterThickness（RGBA16F，每帧 CLEAR）
		{
			0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,  VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
		// Attachment 2：WaterGBuf_AbsorptionFoam（RGBA16F，每帧 CLEAR）
		{
			0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,  VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
		// Attachment 3：WaterGBuf_WorldPosDepth（RGBA32F，每帧 CLEAR）
		{
			0, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,  VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
		// Attachment 4：主场景深度（LOAD + 只读）。Forward custom opaque 已在此之前写入。
		{
			0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_LOAD,    VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
		},
	};

	// 只读取主深度；WaterGBuffer pipeline 的 depthWriteEnable 必须为 VK_FALSE。
	VkAttachmentReference depthRef = { 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> subpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{
				{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
			},
			{},
			&depthRef,
			{}
		}
	};

	std::vector<VkSubpassDependency> dependencies =
	{
		// Forward custom opaque → Water GBuffer：主深度写入完成后才可测试水面覆盖率。
		{
			VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		// Water GBuffer → 外部（Pre-Water Compute / Composite）：颜色输出可被采样。
		{
			0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};

	m_VansWaterGBufferPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 0.0f },   // WaterGBuf_Normal：清空为零
		{ 0.02f, 0.04f, 0.06f, 0.0f },
		{ 0.25f, 0.08f, 0.02f, 0.0f },
		{ 1e4f, 1e4f, 1e4f, 1e4f },   // WaterGBuf_WorldPosDepth：全部 1e4 = 无水面
	};

	m_VansWaterGBufferPass.CreateRenderPass(logic_device, attachments, subpassParams, dependencies, renderResolution);
	m_VansWaterGBufferPass.m_FrameBuffers.resize(m_GBufferFrameTargetCount);
	for (std::uint32_t slot = 0; slot < m_GBufferFrameTargetCount; ++slot)
		m_VansWaterGBufferPass.m_FrameBuffers[slot].CreateFrameBuffer(
			logic_device, m_VansWaterGBufferPass.m_RenderPass,
			{ m_WaterGBufNormalImage.GetImageView(), m_WaterGBufScatterImage.GetImageView(),
			  m_WaterGBufAbsorptionImage.GetImageView(), m_WaterGBufLinearDepthImage.GetImageView(),
			  GetDepth(slot).GetDepthStencilView() },
			{ renderResolution.width, renderResolution.height, 1 });
}

VansGraphics::VansRenderPassRuntimeInfo VansGraphics::VansRenderPassManager::GetRenderPassRuntimeInfo(VansVKRenderPass& renderPass, int swap_chain_index, uint32_t subpass)
{
	VansRenderPassRuntimeInfo info = {};
	info.renderPass = renderPass.m_RenderPass;
	info.subpass = subpass;
	info.viewport = renderPass.m_RenderPassViewport;
	info.scissor = renderPass.m_RenderPassScissor;
	if (swap_chain_index >= 0 && swap_chain_index < static_cast<int>(renderPass.m_FrameBuffers.size()))
	{
		info.framebuffer = renderPass.m_FrameBuffers[swap_chain_index].m_FrameBuffer;
	}
	return info;
}

void VansGraphics::VansRenderPassManager::BeginRenderPass(VansVKRenderPass& renderPass,VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data, int swap_chain_index)
{
	BeginRenderPass(renderPass, command_buffer, global_state_data, swap_chain_index, VK_SUBPASS_CONTENTS_INLINE);
}

void VansGraphics::VansRenderPassManager::BeginRenderPass(VansVKRenderPass& renderPass,VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data, int swap_chain_index, VkSubpassContents contents)
{
	//将当前render pass 记录到globaldata中
	global_state_data.currentRenderPass = renderPass.m_RenderPass;
	global_state_data.currentSubpass = 0;

	//设置viewport和scissor创建管线的时候会使用到
	global_state_data.viewport = renderPass.m_RenderPassViewport;
	global_state_data.scissor = renderPass.m_RenderPassScissor;

	VkRenderPassBeginInfo render_pass_begin_info = 
	{
		 VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		 nullptr,
		 renderPass.m_RenderPass,
		 renderPass.m_FrameBuffers[swap_chain_index].m_FrameBuffer,
		 renderPass.m_RenderPassScissor,
		 static_cast<uint32_t>(renderPass.m_ClearValues.size()),
		 renderPass.m_ClearValues.data()
	};

	command_buffer.BeginRenderPass(render_pass_begin_info, contents);

	if (contents == VK_SUBPASS_CONTENTS_INLINE)
	{
		//begin的时候设置viewport和sissor
		command_buffer.SetViewport(0, { renderPass.m_RenderPassViewport });
		command_buffer.SetScissor(0, { renderPass.m_RenderPassScissor });
	}
}

void VansGraphics::VansRenderPassManager::EndRenderPass(VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data)
{
	global_state_data.currentRenderPass = VK_NULL_HANDLE;
	global_state_data.currentSubpass = 0;
	command_buffer.EndRenderPass();
}

void VansGraphics::VansRenderPassManager::DestroySceneResolutionRenderPasses()
{
	// Framebuffers must release attachment views before their backing images.
	m_VansGBufferPass.DestroyRenderPass(m_LogicDevice);
	m_VansTransparentPass.DestroyRenderPass(m_LogicDevice);
	m_VansRawOpaqueLightingPass.DestroyRenderPass(m_LogicDevice);
	m_VansPreAtmosphereSurfacePass.DestroyRenderPass(m_LogicDevice);
	m_VansHairVisibilityPass.DestroyRenderPass(m_LogicDevice);
	m_VansHairDepthResolvePass.DestroyRenderPass(m_LogicDevice);
	m_VansHairLightingPass.DestroyRenderPass(m_LogicDevice);
	m_VansHairVisibilityPass.m_FrameBuffers.clear();
	m_VansHairDepthResolvePass.m_FrameBuffers.clear();
	m_VansHairLightingPass.m_FrameBuffers.clear();
	m_VansWaterGBufferPass.DestroyRenderPass(m_LogicDevice);
	m_VansSkyMotionVectorPass.DestroyRenderPass(m_LogicDevice);
	m_VansDecalPass.DestroyRenderPass(m_LogicDevice);
	m_VansDecalPass.m_FrameBuffers.clear();
	m_DecalExtent = {};

	m_ColorImage.DestroyVulkanImage(m_LogicDevice);
	for (VansVKImage& image : m_RawOpaqueSceneColorImages)
		image.DestroyVulkanImage(m_LogicDevice);
	m_RawOpaqueSceneColorTargetCount = 0;
	m_RawOpaqueSceneColorExtent = {};
	m_DiffuseExitantRadianceHistoryImage.DestroyVulkanImage(m_LogicDevice);
	for (VansVKImage& image : m_OpaqueSceneColorImages)
		image.DestroyVulkanImage(m_LogicDevice);
	m_OpaqueSceneColorTargetCount = 0;
	m_OpaqueSceneColorExtent = {};
	m_OpaqueSceneColorMipCount = 0;
	m_WaterBackgroundPyramidImage.DestroyVulkanImage(m_LogicDevice);
	for (std::uint32_t slot = 0; slot < m_GBufferFrameTargetCount; ++slot)
		DestroyGBufferFrameTarget(slot);
	m_GBufferFrameTargetCount = 0;
	m_GBufferExtent = {};
    for (DecalFrameTargets& targets : m_DecalFrameTargets)
    {
        targets.color.DestroyVulkanImage(m_LogicDevice);
        targets.normal.DestroyVulkanImage(m_LogicDevice);
        targets.roughness.DestroyVulkanImage(m_LogicDevice);
    }
    m_EmptyDecalImage.DestroyVulkanImage(m_LogicDevice);
	for (HairFrameTargets& targets : m_HairFrameTargets)
	{
		targets.opticalDepth.DestroyVulkanImage(m_LogicDevice);
		targets.layerDepth.DestroyVulkanImage(m_LogicDevice);
		targets.color.DestroyVulkanImage(m_LogicDevice);
		targets.layerDepthBuffer.DestroyVulkanBuffer(m_LogicDevice);
	}
	m_HairFrameTargetCount = 0;
	m_HairExtent = {};
	m_WaterGBufNormalImage.DestroyVulkanImage(m_LogicDevice);
	m_WaterGBufScatterImage.DestroyVulkanImage(m_LogicDevice);
	m_WaterGBufAbsorptionImage.DestroyVulkanImage(m_LogicDevice);
	m_WaterGBufLinearDepthImage.DestroyVulkanImage(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::DestroyRenderPass()
{
	m_VansDisplayPostProcessPass.DestroyRenderPass(m_LogicDevice);
	DestroySceneUIRenderPass();
	m_FinalDisplayColorImage.DestroyVulkanImage(m_LogicDevice);
	DestroySceneResolutionRenderPasses();

	m_ShadowMapImage.DestroyVulkanImage(m_LogicDevice);
	m_ShadowMapDepthImage.DestroyVulkanImage(m_LogicDevice);

	// Destroy cascade shadow resources.
	m_VansShadowPass.DestroyRenderPass(m_LogicDevice);
	for (int i = 0; i < 4; ++i)
	{
		if (m_CascadeDepthLayerViews[i] != VK_NULL_HANDLE)
			VansGraphics::vkDestroyImageView(m_LogicDevice, m_CascadeDepthLayerViews[i], nullptr);
	}
	if (m_CascadeShadowArrayView != VK_NULL_HANDLE)
		VansGraphics::vkDestroyImageView(m_LogicDevice, m_CascadeShadowArrayView, nullptr);
	if (m_CascadeShadowSampler != VK_NULL_HANDLE)
		VansGraphics::vkDestroySampler(m_LogicDevice, m_CascadeShadowSampler, nullptr);
	if (m_CascadeShadowCompareSampler != VK_NULL_HANDLE)
		VansGraphics::vkDestroySampler(m_LogicDevice, m_CascadeShadowCompareSampler, nullptr);
	m_CascadeShadowMapDepthImage.DestroyVulkanImage(m_LogicDevice);

	// 每个 framebuffer 引用一个独立 Atlas 的默认 2D view；先销毁 framebuffer，
	// 再释放图像及其 view/sampler。
	m_VansPunctualShadowPass.DestroyRenderPass(m_LogicDevice);
	for (VansVKImage& atlas : m_PunctualShadowMapImages)
		atlas.DestroyVulkanImage(m_LogicDevice);

	m_VansUIPass.DestroyRenderPass(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::DestroyUIRenderPass()
{
	m_VansUIPass.DestroyRenderPass(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::DestroySceneUIRenderPass()
{
	m_VansSceneUIPass.DestroyRenderPass(m_LogicDevice);
	m_SceneUIStencilImage.DestroyVulkanImage(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::DestroyDisplayPostProcessPass()
{
	m_VansDisplayPostProcessPass.DestroyRenderPass(m_LogicDevice);
	m_FinalDisplayColorImage.DestroyVulkanImage(m_LogicDevice);
	m_DisplayPostProcessInput = nullptr;
}

bool VansGraphics::VansRenderPassManager::RecreateUIRenderPass(VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surface, const VkExtent2D& renderResolution)
{
	DestroyUIRenderPass();
	SetupVansUIRenderPass(m_LogicDevice, command_buffer, queue, surface, renderResolution);
	bool ready = surface.m_VansVKImageCount > 0 &&
		m_VansUIPass.m_RenderPass != VK_NULL_HANDLE &&
		m_VansUIPass.m_FrameBuffers.size() == surface.m_VansVKImageCount;
	for (const VansFrameBuffer& frameBuffer : m_VansUIPass.m_FrameBuffers)
		ready = ready && frameBuffer.GetFrameBuffer() != VK_NULL_HANDLE;
	if (!ready)
		DestroyUIRenderPass();
	return ready;
}

void VansGraphics::VansRenderPassManager::RecordFrameBufferImageLayoutReset(
	VansVKCommandBuffer& command_buffer, std::uint32_t frameSlot,
	std::uint32_t gbufferTargetSlot)
{
	VansVKImage& normal = GetNormal(gbufferTargetSlot);
	VansVKImage& gbuffer0 = GetGbuffer0(gbufferTargetSlot);
	VansVKImage& gbuffer1 = GetGbuffer1(gbufferTargetSlot);
	VansVKImage& gbuffer2 = GetGbuffer2(gbufferTargetSlot);
	VansVKImage& depth = GetDepth(gbufferTargetSlot);
	VansVKImage& motionVector = GetMotionVector(gbufferTargetSlot);
	// These transitions belong to the frame that consumed the attachments.
	// Recording them before that frame is submitted preserves ordering without
	// a second queue submission. The swapchain image is intentionally excluded:
	// ownership ends when it reaches PRESENT_SRC_KHR.
	m_ColorImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_ColorImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_ColorImage.m_ImageAspect
		});

	normal.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			normal.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			normal.m_ImageAspect
		});

	gbuffer0.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			gbuffer0.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			gbuffer0.m_ImageAspect
		});

	gbuffer1.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			gbuffer1.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			gbuffer1.m_ImageAspect
		});

	gbuffer2.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			gbuffer2.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			gbuffer2.m_ImageAspect
		});
	VansVKImage& hairColor = GetHairColor(frameSlot);
	hairColor.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{ hairColor.m_VansVKImage, VK_ACCESS_NONE, VK_ACCESS_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, hairColor.m_ImageAspect });
	depth.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			depth.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			depth.m_ImageAspect
		});

	// GBuffer starts from UNDEFINED next frame, so restore the tracked image state
	// to the engine's idle GENERAL layout after all temporal consumers finish.
	motionVector.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			motionVector.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			motionVector.m_ImageAspect
		});

}
