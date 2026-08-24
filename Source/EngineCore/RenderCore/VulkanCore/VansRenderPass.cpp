#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansRenderPass.h"
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "VansVKSurface.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include <iostream>
#include <algorithm>
#include <vector>

VansGraphics::VansRenderPassManager* VansGraphics::VansRenderPassManager::instance = nullptr;

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
		if (!VansGraphics::VansVKCommandBuffer::SubmitCommands(
			queue,
			device,
			{ commandBuffer.GetVKCommandBuffer() },
			{},
			{},
			commandBuffer.m_CommandBufferFinishSubmitFence))
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

VansGraphics::VansRenderPassManager* VansGraphics::VansRenderPassManager::GetInstance()
{
	if (instance == nullptr)
	{
		instance = new VansRenderPassManager();
	}
	return instance;
}

void VansGraphics::VansRenderPassManager::SetupVansDeferredRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, const VkExtent2D& renderResolution)
{
	VkExtent2D resolution = renderResolution;

	// 创建主渲染目标。RenderPass 已拆分，但 RT 仍集中在这里创建，避免其它 pass 依赖顺序变化。
	m_ColorImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);
	uint32_t opaqueSceneMipCount = 1u;
	for (uint32_t extent = resolution.width > resolution.height ? resolution.width : resolution.height;
		extent > 1u; extent >>= 1u)
	{
		++opaqueSceneMipCount;
	}
	m_OpaqueSceneColorImage.CreateVulkanImage(
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
	m_DepthImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_D32_SFLOAT_S8_UINT,	// D32S8: 32位浮点深度（消除 D16 z-fighting）+ 8位 stencil
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);

	m_NormalImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);

	m_MotionVectorImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);

	m_GBufferImage0.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,  // SFLOAT：emissive intensity 可超过 1.0
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);

	m_GBufferImage1.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);

	m_GBufferImage2.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R32G32B32A32_SFLOAT, // 世界坐标和线性深度需要 32 位精度，避免远距离水面遮挡误判
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);

#ifdef _DEBUG
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_ColorImage.GetImage());
	nameInfo.pObjectName = "ColorImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_DiffuseExitantRadianceHistoryImage.GetImage());
	nameInfo.pObjectName = "DiffuseExitantRadianceHistory";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_OpaqueSceneColorImage.GetImage());
	nameInfo.pObjectName = "OpaqueSceneColorImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_DepthImage.GetImage());
	nameInfo.pObjectName = "DepthImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_NormalImage.GetImage());
	nameInfo.pObjectName = "NormalImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_MotionVectorImage.GetImage());
	nameInfo.pObjectName = "MotionVectorImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_GBufferImage0.GetImage());
	nameInfo.pObjectName = "GBuffer0Image";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_GBufferImage1.GetImage());
	nameInfo.pObjectName = "GBuffer1Image";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_GBufferImage2.GetImage());
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
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
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
		m_NormalImage.GetImageView(),
		m_GBufferImage0.GetImageView(),
		m_GBufferImage1.GetImageView(),
		m_GBufferImage2.GetImageView(),
		m_MotionVectorImage.GetImageView(),
		m_DepthImage.GetDepthStencilView()	// depth+stencil combined view：支持后续开启 stencil ops
	};
	m_VansGBufferPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansGBufferPass.m_RenderPass, gbufferViews, { resolution.width, resolution.height, 1 });

	// Deferred + SkyBox clears and writes SceneColor plus diffuse-exitant history.
	std::vector<VkAttachmentDescription> deferredSkyboxAttachmentDescs =
	{
		// 附件 0：SceneColor（CLEAR，UNDEFINED initialLayout — Deferred 从黑色开始写入）
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
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
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};
	VkAttachmentReference deferredSkyboxDepthRef = { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> deferredSkyboxSubpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }, { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{},
			&deferredSkyboxDepthRef,
			{}
		}
	};
	std::vector<VkSubpassDependency> deferredSkyboxDependencies =
	{
		// GBuffer / Compute → Deferred Skybox：计算写入 + GBuffer 深度写入完成后再开始
		{
			VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		// Deferred Skybox → 外部（Water Compute / Water GBuffer）：颜色写入完成后可被 Compute 读取
		{
			0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};
	m_VansDeferredSkyboxPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 1.0f, 0 },
	};
	m_VansDeferredSkyboxPass.CreateRenderPass(logic_device, deferredSkyboxAttachmentDescs, deferredSkyboxSubpassParams, deferredSkyboxDependencies, resolution);
	m_VansDeferredSkyboxPass.m_FrameBuffers.resize(1);
	{
		std::vector<VkImageView> deferredSkyboxViews =
		{
			m_ColorImage.GetImageView(),
			m_DiffuseExitantRadianceHistoryImage.GetImageView(),
			m_DepthImage.GetDepthStencilView()
		};
		m_VansDeferredSkyboxPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansDeferredSkyboxPass.m_RenderPass, deferredSkyboxViews, { resolution.width, resolution.height, 1 });
	}

	// The main forward pass only composites transparent content into HDR SceneColor.
	// Display conversion is a dedicated display-resolution pass after FSR.
	// ForwardOpaqueAfterDeferred pass: load SceneColor after deferred and draw opaque forward objects before transparent/post.
	std::vector<VkAttachmentDescription> forwardOpaqueAttachmentDescs =
	{
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
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
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			0,
			VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
				VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};
	m_VansForwardOpaqueAfterDeferredPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 1.0f, 0 },
	};
	m_VansForwardOpaqueAfterDeferredPass.CreateRenderPass(logic_device, forwardOpaqueAttachmentDescs, forwardOpaqueSubpassParams, forwardOpaqueDependencies, resolution);
	m_VansForwardOpaqueAfterDeferredPass.m_FrameBuffers.resize(1);
	{
		std::vector<VkImageView> forwardOpaqueViews =
		{
			m_ColorImage.GetImageView(),
			m_DepthImage.GetDepthStencilView()
		};
		m_VansForwardOpaqueAfterDeferredPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansForwardOpaqueAfterDeferredPass.m_RenderPass, forwardOpaqueViews, { resolution.width, resolution.height, 1 });
	}

	std::vector<VkAttachmentDescription> transparentAttachmentDescs =
	{
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
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
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
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
	m_VansTransparentPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> transparentViews =
	{
		m_ColorImage.GetImageView(),
		m_DepthImage.GetDepthStencilView()
	};
	m_VansTransparentPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansTransparentPass.m_RenderPass, transparentViews, { resolution.width, resolution.height, 1 });

	m_LogicDevice = logic_device;

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

	m_NormalImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_NormalImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_NormalImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_NormalImage.m_ImageAspect
		});

	m_MotionVectorImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_MotionVectorImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_MotionVectorImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_MotionVectorImage.m_ImageAspect
		});

	m_GBufferImage0.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_GBufferImage0.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_GBufferImage0.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_GBufferImage0.m_ImageAspect
		});

	m_GBufferImage1.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_GBufferImage1.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_GBufferImage1.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_GBufferImage1.m_ImageAspect
		});

	m_GBufferImage2.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_GBufferImage2.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_GBufferImage2.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_GBufferImage2.m_ImageAspect
		});

	m_DepthImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_DepthImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_DepthImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_DepthImage.m_ImageAspect
		});
	EndSubmitAndResetOneTimeCommand(command_buffer, queue, logic_device, "SetupVansGBufferRenderPass");
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

	auto vansConfigration = VansConfigration::GetInstance();
	int cascadeCount = vansConfigration->GetCascadeCount();
	uint32_t cascadeSize = (uint32_t)vansConfigration->GetCascadeShadowMapSize();
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

	auto vansConfigration = VansConfigration::GetInstance();
	VkExtent2D resolution = {
		static_cast<uint32_t>((std::max)(vansConfigration->GetPunctualShadowMapWidth(), 1)),
		static_cast<uint32_t>((std::max)(vansConfigration->GetPunctualShadowMapHeight(), 1))
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
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
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
			VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			0,
			VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
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
	m_VansSkyMotionVectorPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> imageViews =
	{
		m_MotionVectorImage.GetImageView(),
		m_DepthImage.GetDepthStencilView()
	};
	m_VansSkyMotionVectorPass.m_FrameBuffers[0].CreateFrameBuffer(
		logicDevice,
		m_VansSkyMotionVectorPass.m_RenderPass,
		imageViews,
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
			VK_IMAGE_LAYOUT_GENERAL,
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
	VkDevice& logicDevice, VkImageView finalDisplayImageView, const VkExtent2D& displayExtent)
{
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
	};
	std::vector<SubpassParameters> subpass_parameters =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{
				{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
			},
			{},
			nullptr,
			{}
		},
	};
	// 无 clear，LOAD_OP_LOAD 时 clearValue 无效
	m_VansSceneUIPass.m_ClearValues = {};

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
	std::vector<VkImageView> image_views = { finalDisplayImageView };
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
	VkDevice& logic_device, const VkExtent2D& renderResolution)
{
	// 贴花 Pass — 3 个颜色附件 + 只读深度附件
	// 颜色附件：Normal / GBuffer0 / GBuffer1
	//   LOAD + STORE：保留 GBuffer pass 写入的内容，贴花以 alpha blend 叠写
	// 深度附件：只读加载，不写入（depthWriteEnable = VK_FALSE in pipeline）
	//   initialLayout = SHADER_READ_ONLY_OPTIMAL（GBuffer pass finalLayout）
	//   finalLayout   = SHADER_READ_ONLY_OPTIMAL（供后续 Deferred pass 采样）
	//   subpass reference layout = DEPTH_STENCIL_READ_ONLY_OPTIMAL
	//   → 允许硬件深度测试（读）同时不破坏深度缓冲内容
	std::vector<VkAttachmentDescription> attachments =
	{
		// 附件 0: Normal（R16G16B16A16_SFLOAT）
		{
			0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
		// 附件 1: GBuffer0 albedo（R16G16B16A16_SFLOAT）
		{
			0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
		// 附件 2: GBuffer1 metallic+AO（R16G16B16A16_SFLOAT），colorWriteMask 在 pipeline 中限制为 R+G
		{
			0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
		// 附件 3: Depth（D32_SFLOAT_S8_UINT）— 只读，供深度测试，不写入
		//   LOAD：保留 GBuffer pass 写入的深度值
		//   STORE：保留深度供后续 Deferred pass 使用
		{
			0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_LOAD,       VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,  VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
	};

	// 深度 subpass reference 使用 DEPTH_STENCIL_READ_ONLY_OPTIMAL：
	// 允许深度测试（读）的同时也允许 fragment shader 通过 sampler 读取深度，
	// 且 pipeline depthWriteEnable=VK_FALSE 保证不写入深度。
	VkAttachmentReference depthRef = { 3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };

	std::vector<SubpassParameters> subpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{
				{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
			},
			{},
			&depthRef,
			{}
		}
	};

	std::vector<VkSubpassDependency> dependencies =
	{
		// GBuffer Pass → Decal Pass：颜色写入 + 深度写入完成后才可开始 decal
		{
			VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		// Decal Pass → Deferred/Compute：贴花写入完成后，后续 pass 可读取 GBuffer
		{
			0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};

	// 贴花 pass 不需要 clear（颜色 LOAD，深度 LOAD）
	m_VansDecalPass.m_ClearValues = {};

	m_VansDecalPass.CreateRenderPass(logic_device, attachments, subpassParams, dependencies, renderResolution);

	// 单一 framebuffer，引用现有 GBuffer 图像的 ImageView + 深度图像的 DepthStencilView
	m_VansDecalPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> fbViews =
	{
		m_NormalImage.GetImageView(),
		m_GBufferImage0.GetImageView(),
		m_GBufferImage1.GetImageView(),
		m_DepthImage.GetDepthStencilView(),   // 只读深度，供硬件深度测试
	};
	m_VansDecalPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device, m_VansDecalPass.m_RenderPass, fbViews,
		{ renderResolution.width, renderResolution.height, 1 });
}

void VansGraphics::VansRenderPassManager::SetupVansScreenSpaceEffectsPass(
	VkDevice& logic_device, const VkExtent2D& renderResolution)
{
	m_LogicDevice = logic_device;

	const VkExtent2D halfResolution =
	{
		(std::max)(1u, renderResolution.width / 2u),
		(std::max)(1u, renderResolution.height / 2u)
	};

	std::vector<VkAttachmentDescription> attachments;
	std::vector<SubpassParameters> subpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{},
			{},
			nullptr,
			{}
		}
	};

	std::vector<VkSubpassDependency> dependencies =
	{
		{
			VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};

	m_VansScreenSpaceEffectsPass.m_ClearValues = {};
	m_VansScreenSpaceEffectsPass.CreateRenderPass(logic_device, attachments, subpassParams, dependencies, halfResolution);
	m_VansScreenSpaceEffectsPass.m_FrameBuffers.resize(1);
	m_VansScreenSpaceEffectsPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device, m_VansScreenSpaceEffectsPass.m_RenderPass, {},
		{ halfResolution.width, halfResolution.height, 1 });
}

// ============================================================
// SetupVansWaterGBufferPass — 水面 GBuffer render pass 初始化
//
// 设计文档 §6.2 "Water GBuffer Pass"：
//   输出 Attachment 0：WaterGBuf_Normal（RG16_SFLOAT）
//   输出 Attachment 1：WaterGBuf_LinearDepth（R32F）
//   深度 Attachment：复用场景深度（TEST 只读，depthWriteEnable=VK_FALSE）
//
// 调用时机：在 SetupVansDeferredRenderPass 之后（须先创建 m_DepthImage）。
// ============================================================
void VansGraphics::VansRenderPassManager::SetupVansHairVisibilityPass(
	VkDevice& logic_device, const VkExtent2D& renderResolution)
{
	static constexpr uint32_t HairOITNodesPerPixel = 8;
	static constexpr VkDeviceSize HairOITNodeStride = 40;
	m_HairOITMaxNodes = renderResolution.width * renderResolution.height * HairOITNodesPerPixel;

	m_HairOITHeadImage.CreateVulkanImage(
		logic_device,
		{ renderResolution.width, renderResolution.height, 1 },
		VK_FORMAT_R32_UINT,
		1, 1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false, false, false);

	m_HairOITNodeBuffer.CreatVulkanBuffer(
		logic_device,
		static_cast<VkDeviceSize>(m_HairOITMaxNodes) * HairOITNodeStride,
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	const uint32_t counterInit[2] = { 0u, m_HairOITMaxNodes };
	m_HairOITCounterBuffer.CreatVulkanBuffer(
		logic_device,
		sizeof(counterInit),
		VK_FORMAT_R32_UINT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_HairOITCounterBuffer.SetBufferData(counterInit, 0, sizeof(counterInit));

	std::vector<VkAttachmentDescription> attachments =
	{
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};

	VkAttachmentReference depthRef = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> subpassParams =
	{
		{ VK_PIPELINE_BIND_POINT_GRAPHICS, {}, {}, {}, &depthRef, {} }
	};

	std::vector<VkSubpassDependency> dependencies =
	{
		{ VK_SUBPASS_EXTERNAL, 0,
		  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT },
		{ 0, VK_SUBPASS_EXTERNAL,
		  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		  VK_ACCESS_SHADER_READ_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT },
	};

	m_VansHairVisibilityPass.m_ClearValues =
	{
		{ 1.0f, 0 },
	};

	m_VansHairVisibilityPass.CreateRenderPass(logic_device, attachments, subpassParams, dependencies, renderResolution);
	m_VansHairVisibilityPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> fbViews =
	{
		m_DepthImage.GetDepthStencilView(),
	};
	m_VansHairVisibilityPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device, m_VansHairVisibilityPass.m_RenderPass, fbViews,
		{ renderResolution.width, renderResolution.height, 1 });
}

void VansGraphics::VansRenderPassManager::SetupVansHairLightingPass(
	VkDevice& logic_device, const VkExtent2D& renderResolution)
{
	m_HairColorImage.CreateVulkanImage(
		logic_device,
		{ renderResolution.width, renderResolution.height, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false, false, true);

	std::vector<VkAttachmentDescription> attachments =
	{
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};

	std::vector<SubpassParameters> subpassParams =
	{
		{ VK_PIPELINE_BIND_POINT_GRAPHICS, {},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{}, nullptr, {} }
	};

	std::vector<VkSubpassDependency> dependencies =
	{
		{ VK_SUBPASS_EXTERNAL, 0,
		  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT },
		{ 0, VK_SUBPASS_EXTERNAL,
		  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_SHADER_READ_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT },
	};

	m_VansHairLightingPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 0.0f },
	};

	m_VansHairLightingPass.CreateRenderPass(logic_device, attachments, subpassParams, dependencies, renderResolution);
	m_VansHairLightingPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> fbViews =
	{
		m_HairColorImage.GetImageView(),
	};
	m_VansHairLightingPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device, m_VansHairLightingPass.m_RenderPass, fbViews,
		{ renderResolution.width, renderResolution.height, 1 });
}

void VansGraphics::VansRenderPassManager::SetupVansHairDeepOpacityPass(
	VkDevice& logic_device, const VkExtent2D& renderResolution)
{
	m_HairDeepOpacityImage.CreateVulkanImage(
		logic_device,
		{ renderResolution.width, renderResolution.height, 1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1, 1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false, false, true);

	std::vector<VkAttachmentDescription> attachments =
	{
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};

	std::vector<SubpassParameters> subpassParams =
	{
		{ VK_PIPELINE_BIND_POINT_GRAPHICS, {},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{}, nullptr, {} }
	};

	std::vector<VkSubpassDependency> dependencies =
	{
		{ VK_SUBPASS_EXTERNAL, 0,
		  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT },
		{ 0, VK_SUBPASS_EXTERNAL,
		  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		  VK_ACCESS_SHADER_READ_BIT,
		  VK_DEPENDENCY_BY_REGION_BIT },
	};

	m_VansHairDeepOpacityPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 0.0f },
	};

	m_VansHairDeepOpacityPass.CreateRenderPass(logic_device, attachments, subpassParams, dependencies, renderResolution);
	m_VansHairDeepOpacityPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> fbViews =
	{
		m_HairDeepOpacityImage.GetImageView(),
	};
	m_VansHairDeepOpacityPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device, m_VansHairDeepOpacityPass.m_RenderPass, fbViews,
		{ renderResolution.width, renderResolution.height, 1 });
}

void VansGraphics::VansRenderPassManager::SetupVansWaterGBufferPass(
	VkDevice& logic_device, const VkExtent2D& renderResolution)
{
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
		// Attachment 0：WaterGBuf_Normal（RG16F，每帧 CLEAR）
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
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
				{ 3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
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
	m_VansWaterGBufferPass.m_FrameBuffers.resize(1);

	std::vector<VkImageView> fbViews =
	{
		m_WaterGBufNormalImage.GetImageView(),
		m_WaterGBufScatterImage.GetImageView(),
		m_WaterGBufAbsorptionImage.GetImageView(),
		m_WaterGBufLinearDepthImage.GetImageView(),
		m_DepthImage.GetDepthStencilView(),         // 主场景深度，只读测试
	};
	m_VansWaterGBufferPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device, m_VansWaterGBufferPass.m_RenderPass, fbViews,
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
	m_VansDeferredSkyboxPass.DestroyRenderPass(m_LogicDevice);
	m_VansScreenSpaceEffectsPass.DestroyRenderPass(m_LogicDevice);
	m_VansForwardOpaqueAfterDeferredPass.DestroyRenderPass(m_LogicDevice);
	m_VansHairVisibilityPass.DestroyRenderPass(m_LogicDevice);
	m_VansHairLightingPass.DestroyRenderPass(m_LogicDevice);
	m_VansHairDeepOpacityPass.DestroyRenderPass(m_LogicDevice);
	m_VansWaterGBufferPass.DestroyRenderPass(m_LogicDevice);
	m_VansSkyMotionVectorPass.DestroyRenderPass(m_LogicDevice);
	m_VansDecalPass.DestroyRenderPass(m_LogicDevice);

	m_ColorImage.DestroyVulkanImage(m_LogicDevice);
	m_DiffuseExitantRadianceHistoryImage.DestroyVulkanImage(m_LogicDevice);
	m_OpaqueSceneColorImage.DestroyVulkanImage(m_LogicDevice);
	m_DepthImage.DestroyVulkanImage(m_LogicDevice);
	m_MotionVectorImage.DestroyVulkanImage(m_LogicDevice);
	m_NormalImage.DestroyVulkanImage(m_LogicDevice);
	m_GBufferImage0.DestroyVulkanImage(m_LogicDevice);
	m_GBufferImage1.DestroyVulkanImage(m_LogicDevice);
	m_GBufferImage2.DestroyVulkanImage(m_LogicDevice);
	m_HairColorImage.DestroyVulkanImage(m_LogicDevice);
	m_HairDeepOpacityImage.DestroyVulkanImage(m_LogicDevice);
	m_HairOITHeadImage.DestroyVulkanImage(m_LogicDevice);
	m_HairOITNodeBuffer.DestroyVulkanBuffer(m_LogicDevice);
	m_HairOITCounterBuffer.DestroyVulkanBuffer(m_LogicDevice);
	m_WaterGBufNormalImage.DestroyVulkanImage(m_LogicDevice);
	m_WaterGBufScatterImage.DestroyVulkanImage(m_LogicDevice);
	m_WaterGBufAbsorptionImage.DestroyVulkanImage(m_LogicDevice);
	m_WaterGBufLinearDepthImage.DestroyVulkanImage(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::DestroyRenderPass()
{
	m_VansDisplayPostProcessPass.DestroyRenderPass(m_LogicDevice);
	m_VansSceneUIPass.DestroyRenderPass(m_LogicDevice);
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
}

void VansGraphics::VansRenderPassManager::DestroyDisplayPostProcessPass()
{
	m_VansDisplayPostProcessPass.DestroyRenderPass(m_LogicDevice);
	m_FinalDisplayColorImage.DestroyVulkanImage(m_LogicDevice);
	m_DisplayPostProcessInput = nullptr;
}

void VansGraphics::VansRenderPassManager::RecreateUIRenderPass(VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surface, const VkExtent2D& renderResolution)
{
	DestroyUIRenderPass();
	SetupVansUIRenderPass(m_LogicDevice, command_buffer, queue, surface, renderResolution);
}

void VansGraphics::VansRenderPassManager::RecordFrameBufferImageLayoutReset(VansVKCommandBuffer& command_buffer)
{
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

	m_NormalImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_NormalImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_NormalImage.m_ImageAspect
		});

	m_GBufferImage0.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_GBufferImage0.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_GBufferImage0.m_ImageAspect
		});

	m_GBufferImage1.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_GBufferImage1.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_GBufferImage1.m_ImageAspect
		});

	m_GBufferImage2.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_GBufferImage2.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_GBufferImage2.m_ImageAspect
		});
	m_HairColorImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{ m_HairColorImage.m_VansVKImage, VK_ACCESS_NONE, VK_ACCESS_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, m_HairColorImage.m_ImageAspect });
	m_HairDeepOpacityImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{ m_HairDeepOpacityImage.m_VansVKImage, VK_ACCESS_NONE, VK_ACCESS_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, m_HairDeepOpacityImage.m_ImageAspect });
	m_DepthImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_DepthImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_DepthImage.m_ImageAspect
		});

	// GBuffer starts from UNDEFINED next frame, so restore the tracked image state
	// to the engine's idle GENERAL layout after all temporal consumers finish.
	m_MotionVectorImage.SetImageMemoryBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_MotionVectorImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_MotionVectorImage.m_ImageAspect
		});

}
