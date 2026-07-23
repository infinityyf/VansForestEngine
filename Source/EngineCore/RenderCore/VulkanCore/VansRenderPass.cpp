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

	m_ColorAfterPostProcessImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_OpaqueSceneColorImage.GetImage());
	nameInfo.pObjectName = "OpaqueSceneColorImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_DepthImage.GetImage());
	nameInfo.pObjectName = "DepthImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_NormalImage.GetImage());
	nameInfo.pObjectName = "NormalImage";
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
		{ 0, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		// D32_SFLOAT_S8_UINT: 32位浮点深度+8位 stencil；同样用 UNDEFINED 作为 initialLayout
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};

	VkAttachmentReference gbufferDepthAttachment = { 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	std::vector<SubpassParameters> gbufferSubpassParams =
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
		m_DepthImage.GetDepthStencilView()	// depth+stencil combined view：支持后续开启 stencil ops
	};
	m_VansGBufferPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansGBufferPass.m_RenderPass, gbufferViews, { resolution.width, resolution.height, 1 });

	// ── m_VansDeferredSkyboxPass：仅 Deferred + SkyBox ─────────────────────────
	// 从原 m_VansRenderPass 的 Subpass 0 中拆出；SceneColor CLEAR，单子通道。
	// 原始 m_VansRenderPass 继续存在，但改为 Transparent + PostProcess（LOAD SceneColor）。
	std::vector<VkAttachmentDescription> deferredSkyboxAttachmentDescs =
	{
		// 附件 0：SceneColor（CLEAR，UNDEFINED initialLayout — Deferred 从黑色开始写入）
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		// 附件 1：Depth（LOAD，场景深度由 GBuffer pass 写入，供深度测试读取）
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};
	VkAttachmentReference deferredSkyboxDepthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> deferredSkyboxSubpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
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
		{ 1.0f, 0 },
	};
	m_VansDeferredSkyboxPass.CreateRenderPass(logic_device, deferredSkyboxAttachmentDescs, deferredSkyboxSubpassParams, deferredSkyboxDependencies, resolution);
	m_VansDeferredSkyboxPass.m_FrameBuffers.resize(1);
	{
		std::vector<VkImageView> deferredSkyboxViews =
		{
			m_ColorImage.GetImageView(),
			m_DepthImage.GetDepthStencilView()
		};
		m_VansDeferredSkyboxPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansDeferredSkyboxPass.m_RenderPass, deferredSkyboxViews, { resolution.width, resolution.height, 1 });
	}

	// ── m_VansRenderPass（修改后）：Transparent + PostProcess ────────────────
	// 原 Subpass 0（Deferred+SkyBox）已移至 m_VansDeferredSkyboxPass。
	// 此 pass 仅保留透明物体绘制（Subpass 0）和后处理（Subpass 1）。
	// SceneColor 使用 LOAD：加载水面合成后的结果继续绘制透明物体。
	// Deferred/PostProcess pass：Subpass 0 写 lighting color，Subpass 1 做后处理。
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

	std::vector<VkAttachmentDescription> deferredPostAttachmentDescs =
	{
		// SceneColor：LOAD 已有内容（来自水面合成或 Deferred pass 输出），透明物体叠加绘制
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
		  VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
		  VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
		  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		// loadOp=LOAD: depth 从 GBuffer pass finalLayout (SHADER_READ_ONLY_OPTIMAL) 加载
		{ 0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
	};
	// DEPTH_STENCIL_READ_ONLY_OPTIMAL: 允许 depth 在同一 subpass 中同时用于只读深度测试和 shader 采样，
	// 避免 DEPTH_STENCIL_ATTACHMENT_OPTIMAL 与 SHADER_READ_ONLY_OPTIMAL 运行期 layout 冲突
	VkAttachmentReference deferredDepthAttachment = { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> deferredPostSubpassParams =
	{
		// Subpass 0：Transparent + Particles（SceneColor LOAD，继承水面合成结果）
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{ { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{},
			&deferredDepthAttachment,
			{}
		},
		// Subpass 1：PostProcess（读 SceneColor 为 input attachment，写 ColorAfterPostProcess）
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{ { 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } },
			{ { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } },
			{},
			nullptr,
			{}
		}
	};
	std::vector<VkSubpassDependency> deferredPostDependencies =
	{
		// 外部 → Subpass 0（Transparent）：
		// 等待 Water Composite render pass 对 SceneColor 的颜色写入完成（或 Deferred Skybox 如无水面）
		{
			VK_SUBPASS_EXTERNAL,
			0,
			// 来源：Deferred Skybox / Water Composite 的颜色写入 + Compute 写入 + GBuffer 深度写入
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
			// 目标：Transparent fragment 读取 SceneColor + 深度测试
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			0,
			1,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		},
		{
			1,
			VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT
		}
	};
	m_VansRenderPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },  // SceneColor：LOAD 时忽略此值，但数组索引须保留
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 1.0f, 0 },
	};
	m_VansRenderPass.CreateRenderPass(logic_device, deferredPostAttachmentDescs, deferredPostSubpassParams, deferredPostDependencies, resolution);
	m_VansRenderPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> deferredPostViews =
	{
		m_ColorImage.GetImageView(),
		m_ColorAfterPostProcessImage.GetImageView(),
		m_DepthImage.GetDepthStencilView()	// depth+stencil combined view
	};
	m_VansRenderPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansRenderPass.m_RenderPass, deferredPostViews, { resolution.width, resolution.height, 1 });

	m_LogicDevice = logic_device;

	//record command buffer
	command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	//设置colordepoth的layout
	m_ColorImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_NormalImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_MotionVectorImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_ColorAfterPostProcessImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_ColorAfterPostProcessImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_ColorAfterPostProcessImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_ColorAfterPostProcessImage.m_ImageAspect
		});

	m_GBufferImage0.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_GBufferImage1.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_GBufferImage2.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_DepthImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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
			VK_FORMAT_R32_SFLOAT,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		{
			0,
			VK_FORMAT_D32_SFLOAT,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		},
	};

	VkAttachmentReference depth_stencil_attachment =
	{
		 1,
		 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	std::vector<SubpassParameters> subpass_parameters =
	{
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
			&depth_stencil_attachment,
			{}
		},
	};

	m_VansShadowPass.m_ClearValues =
	{
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 0 },
	};

	std::vector<VkSubpassDependency> subpass_dependencies;

	auto vansConfigration = VansConfigration::GetInstance();
	int cascadeCount = vansConfigration->GetCascadeCount();
	uint32_t cascadeSize = (uint32_t)vansConfigration->GetCascadeShadowMapSize();
	VkExtent2D resolution = { cascadeSize, cascadeSize };

	m_VansShadowPass.CreateRenderPass(logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies, resolution);

	// Create cascade shadow color image (4 array layers)
	m_CascadeShadowMapImage.CreateVulkanImage(
		logic_device,
		{ cascadeSize, cascadeSize, 1 },
		VK_FORMAT_R32_SFLOAT,
		1,
		(uint32_t)cascadeCount,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);

	// Create cascade shadow depth image (4 array layers)
	m_CascadeShadowMapDepthImage.CreateVulkanImage(
		logic_device,
		{ cascadeSize, cascadeSize, 1 },
		VK_FORMAT_D32_SFLOAT,
		1,
		(uint32_t)cascadeCount,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT
	);

	// Create per-layer image views for framebuffer attachments
	for (int i = 0; i < cascadeCount; ++i)
	{
		// Color layer view
		{
			VkImageViewCreateInfo viewInfo = {};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_CascadeShadowMapImage.GetImage();
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = VK_FORMAT_R32_SFLOAT;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = (uint32_t)i;
			viewInfo.subresourceRange.layerCount = 1;
			VansGraphics::vkCreateImageView(logic_device, &viewInfo, nullptr, &m_CascadeColorLayerViews[i]);
		}
		// Depth layer view
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

	// Create full-array view (2D_ARRAY) for sampling in deferred pass
	{
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_CascadeShadowMapImage.GetImage();
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		viewInfo.format = VK_FORMAT_R32_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = (uint32_t)cascadeCount;
		VansGraphics::vkCreateImageView(logic_device, &viewInfo, nullptr, &m_CascadeShadowArrayView);
	}

	// Create sampler for cascade shadow array
	{
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		// PCSS performs depth comparisons before bilinear interpolation.  Keep
		// the stored R32 depth unfiltered; filtering depth first produces the
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

#ifdef _DEBUG
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_CascadeShadowMapImage.GetImage());
	nameInfo.pObjectName = "CascadeShadowMap";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_CascadeShadowMapDepthImage.GetImage());
	nameInfo.pObjectName = "CascadeShadowMapDepth";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);
#endif

	// Create 4 framebuffers — one per cascade layer
	m_VansShadowPass.m_FrameBuffers.resize(cascadeCount);
	for (int i = 0; i < cascadeCount; ++i)
	{
		std::vector<VkImageView> image_views = {
			m_CascadeColorLayerViews[i],
			m_CascadeDepthLayerViews[i]
		};
		m_VansShadowPass.m_FrameBuffers[i].CreateFrameBuffer(
			logic_device, m_VansShadowPass.m_RenderPass, image_views,
			{ cascadeSize, cascadeSize, 1 });
	}

	m_LogicDevice = logic_device;

	// Transition cascade images to initial layouts
	command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	m_CascadeShadowMapImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_CascadeShadowMapImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_CascadeShadowMapImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_CascadeShadowMapImage.m_ImageAspect
		});
	m_CascadeShadowMapDepthImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_CascadeShadowMapDepthImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_CascadeShadowMapDepthImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
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
	VkExtent2D resolution = { vansConfigration->GetPunctualShadowMapWidth(), vansConfigration->GetPunctualShadowMapHeight() };

	m_VansPunctualShadowPass.CreateRenderPass(logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies, resolution);

	// Persistent sampled depth atlas. Dirty blocks are cleared explicitly inside
	// the render pass; LOAD/STORE preserves every clean cached block.
	m_PunctualShadowMapImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
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
		VK_COMPARE_OP_LESS_OR_EQUAL
	);

#ifdef _DEBUG
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_PunctualShadowMapImage.GetImage());
	nameInfo.pObjectName = "PunctualShadowDepthAtlas";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);
#endif

	m_VansPunctualShadowPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> image_views = { m_PunctualShadowMapImage.GetImageView() };
	m_VansPunctualShadowPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansPunctualShadowPass.m_RenderPass, image_views, { resolution.width, resolution.height, 1 });

	m_LogicDevice = logic_device;

	// Initialize the persistent atlas exactly once. A LOAD render pass may not
	// consume undefined contents on its first frame.
	command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	m_PunctualShadowMapImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		{
			m_PunctualShadowMapImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			m_PunctualShadowMapImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_PunctualShadowMapImage.m_ImageAspect
		});
	command_buffer.ClearDepthStencil(m_PunctualShadowMapImage, { 1.0f, 0 });
	m_PunctualShadowMapImage.SetImageMemoryBarrier(
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		{
			m_PunctualShadowMapImage.m_VansVKImage,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			m_PunctualShadowMapImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_PunctualShadowMapImage.m_ImageAspect
		});

	EndSubmitAndResetOneTimeCommand(command_buffer, queue, logic_device, "SetupVansPunctualShadowRenderPass");
}

// ═══════════════════════════════════════════════════════════════════════════
// Motion-Vector render pass
//
// Attachments:
//   0 — m_MotionVectorImage  (R16G16B16A16_SFLOAT) — color output
//   1 — m_MotionVectorDepthImage (D16_UNORM) — dedicated depth
//
// Draws all opaque geometry using the MotionVector shader to produce
// per-pixel screen-space velocity.  Placed after shadow, before compute
// (HZB / GI / SSR) so temporal-reprojection compute shaders can sample it.
// ═══════════════════════════════════════════════════════════════════════════
void VansGraphics::VansRenderPassManager::SetupVansMotionVectorRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, const VkExtent2D& renderResolution)
{
	std::vector<VkAttachmentDescription> attachments_descriptions =
	{
		// Attachment 0: motion-vector color (CLEAR → STORE)
		{
			0,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},
		// Attachment 1: dedicated depth (CLEAR → DONT_CARE)
		{
			0,
			VK_FORMAT_D16_UNORM,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		},
	};

	VkAttachmentReference depth_stencil_attachment =
	{
		 1,
		 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	std::vector<SubpassParameters> subpass_parameters =
	{
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
			&depth_stencil_attachment,
			{}
		},
	};

	m_VansMotionVectorPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 0.0f },  // zero motion for pixels without geometry
		{ 1.0f, 0 },
	};

	std::vector<VkSubpassDependency> subpass_dependencies;

	VkExtent2D resolution = renderResolution;
	m_VansMotionVectorPass.CreateRenderPass(logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies, resolution);

	// Dedicated depth image for the motion-vector pass
	m_MotionVectorDepthImage.CreateVulkanImage(
		logic_device,
		{ resolution.width, resolution.height, 1 },
		VK_FORMAT_D16_UNORM,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT
	);

#ifdef _DEBUG
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_MotionVectorImage.GetImage());
	nameInfo.pObjectName = "MotionVectorImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_MotionVectorDepthImage.GetImage());
	nameInfo.pObjectName = "MotionVectorDepthImage";
	VansGraphics::vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);
#endif

	// Single framebuffer at render resolution
	m_VansMotionVectorPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> image_views = {
		m_MotionVectorImage.GetImageView(),
		m_MotionVectorDepthImage.GetImageView()
	};
	m_VansMotionVectorPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device, m_VansMotionVectorPass.m_RenderPass, image_views,
		{ resolution.width, resolution.height, 1 });

	m_LogicDevice = logic_device;

	// Transition the dedicated depth image to its initial layout
	command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	m_MotionVectorDepthImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_MotionVectorDepthImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_MotionVectorDepthImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_MotionVectorDepthImage.m_ImageAspect
		});
	EndSubmitAndResetOneTimeCommand(command_buffer, queue, logic_device, "SetupVansMotionVectorRenderPass");
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
	VkDevice& logic_device, VkImageView fsrImageView, const VkExtent2D& displayExtent)
{
	// 颜色附件：FSR 输出图像（R16G16B16A16_SFLOAT）
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
		logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies, displayExtent);

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

	// 单个 framebuffer（FSR 图像只有一张，无 swapchain 多帧）
	m_VansSceneUIPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> image_views = { fsrImageView };
	m_VansSceneUIPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device, m_VansSceneUIPass.m_RenderPass, image_views,
		{ displayExtent.width, displayExtent.height, 1 });
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
		VK_FORMAT_R16G16B16A16_SFLOAT,   // RG16F 存储 oct-encoded 水面法线（BA 通道留用）
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
		// Attachment 1：WaterGBuf_WorldPosDepth（RGBA32F，每帧 CLEAR）
		{
			0, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,  VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
		// Attachment 2：主场景深度（LOAD + 只读）。Forward custom opaque 已在此之前写入。
		{
			0, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_LOAD,    VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		},
	};

	// 只读取主深度；WaterGBuffer pipeline 的 depthWriteEnable 必须为 VK_FALSE。
	VkAttachmentReference depthRef = { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	std::vector<SubpassParameters> subpassParams =
	{
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{
				{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
				{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
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
		{ 1e4f, 1e4f, 1e4f, 1e4f },   // WaterGBuf_WorldPosDepth：全部 1e4 = 无水面
	};

	m_VansWaterGBufferPass.CreateRenderPass(logic_device, attachments, subpassParams, dependencies, renderResolution);
	m_VansWaterGBufferPass.m_FrameBuffers.resize(1);

	std::vector<VkImageView> fbViews =
	{
		m_WaterGBufNormalImage.GetImageView(),
		m_WaterGBufLinearDepthImage.GetImageView(),
		m_DepthImage.GetDepthStencilView(),         // 主场景深度，只读测试
	};
	m_VansWaterGBufferPass.m_FrameBuffers[0].CreateFrameBuffer(
		logic_device, m_VansWaterGBufferPass.m_RenderPass, fbViews,
		{ renderResolution.width, renderResolution.height, 1 });
}

void VansGraphics::VansRenderPassManager::BeginRenderPass(VansVKRenderPass& renderPass,VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data, int swap_chain_index)
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

	command_buffer.BeginRenderPass(render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	//begin的时候设置viewport和sissor
	command_buffer.SetViewport(0, { renderPass.m_RenderPassViewport });
	command_buffer.SetScissor(0, { renderPass.m_RenderPassScissor });
}

void VansGraphics::VansRenderPassManager::NextSubPass(VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data)
{
	global_state_data.currentSubpass++;
	command_buffer.NextSubpass(VK_SUBPASS_CONTENTS_INLINE);
}

void VansGraphics::VansRenderPassManager::EndRenderPass(VansVKCommandBuffer& command_buffer, GlobalStateData& global_state_data)
{
	global_state_data.currentRenderPass = VK_NULL_HANDLE;
	global_state_data.currentSubpass = 0;
	command_buffer.EndRenderPass();
}

void VansGraphics::VansRenderPassManager::BlitToSwapChainImage(VansVKCommandBuffer& command_buffer, VansVKSurface& surface, int swapChainIndex, const VkExtent2D& renderResolution)
{
    // Blit m_ColorAfterPostProcessImage (attachment index 6 in deferred pass framebuffer)
    // into the real swapchain image for presentation.
    // Steps:
    // 1. Transition source to TRANSFER_SRC_OPTIMAL.
    // 2. Transition destination (swapchain) to TRANSFER_DST_OPTIMAL.
    // 3. Blit via command buffer wrapper.
    // 4. Transition destination to PRESENT_SRC_KHR again (source back to PRESENT optional).

    VkExtent2D swapchainExtent = surface.m_VansVKSwapChainImageExtent;

    command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    // Destination: swapchain image (assumed GENERAL or PRESENT). Force to TRANSFER_DST_OPTIMAL.
    surface.SetSwapChainImageBarrier(
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        {
            surface.GetSwapChainImage(swapChainIndex),
            VK_ACCESS_NONE,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, // treat as unknown, transition regardless
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            VK_IMAGE_ASPECT_COLOR_BIT
        },
        swapChainIndex);

    // Blit region (entire image)
    VkImageBlit blitRegion{};
    blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.srcSubresource.mipLevel = 0;
    blitRegion.srcSubresource.baseArrayLayer = 0;
    blitRegion.srcSubresource.layerCount = 1;
    blitRegion.srcOffsets[0] = { 0, 0, 0 };
    blitRegion.srcOffsets[1] = { (int32_t)renderResolution.width, (int32_t)renderResolution.height, 1 };

    blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blitRegion.dstSubresource.mipLevel = 0;
    blitRegion.dstSubresource.baseArrayLayer = 0;
    blitRegion.dstSubresource.layerCount = 1;
    blitRegion.dstOffsets[0] = { 0, 0, 0 };
    blitRegion.dstOffsets[1] = { (int32_t)swapchainExtent.width, (int32_t)swapchainExtent.height, 1 };

    command_buffer.BlitImageRegions(
        m_ColorAfterPostProcessImage.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        surface.GetSwapChainImage(swapChainIndex),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        { blitRegion },
        VK_FILTER_LINEAR);

    // Transition swapchain image to PRESENT_SRC_KHR for presentation
    surface.SetSwapChainImageBarrier(
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        {
            surface.GetSwapChainImage(swapChainIndex),
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_NONE,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            VK_IMAGE_ASPECT_COLOR_BIT
        },
        swapChainIndex);

    command_buffer.EndCommandBufferRecord();
}

void VansGraphics::VansRenderPassManager::DestroyRenderPass()
{
	m_ColorImage.DestroyVulkanImage(m_LogicDevice);
	m_OpaqueSceneColorImage.DestroyVulkanImage(m_LogicDevice);
	m_DepthImage.DestroyVulkanImage(m_LogicDevice);
	m_MotionVectorImage.DestroyVulkanImage(m_LogicDevice);
	m_MotionVectorDepthImage.DestroyVulkanImage(m_LogicDevice);
	m_ColorAfterPostProcessImage.DestroyVulkanImage(m_LogicDevice);

	m_ShadowMapImage.DestroyVulkanImage(m_LogicDevice);
	m_ShadowMapDepthImage.DestroyVulkanImage(m_LogicDevice);

	// Destroy cascade shadow resources
	for (int i = 0; i < 4; ++i)
	{
		if (m_CascadeColorLayerViews[i] != VK_NULL_HANDLE)
			VansGraphics::vkDestroyImageView(m_LogicDevice, m_CascadeColorLayerViews[i], nullptr);
		if (m_CascadeDepthLayerViews[i] != VK_NULL_HANDLE)
			VansGraphics::vkDestroyImageView(m_LogicDevice, m_CascadeDepthLayerViews[i], nullptr);
	}
	if (m_CascadeShadowArrayView != VK_NULL_HANDLE)
		VansGraphics::vkDestroyImageView(m_LogicDevice, m_CascadeShadowArrayView, nullptr);
	if (m_CascadeShadowSampler != VK_NULL_HANDLE)
		VansGraphics::vkDestroySampler(m_LogicDevice, m_CascadeShadowSampler, nullptr);
	m_CascadeShadowMapImage.DestroyVulkanImage(m_LogicDevice);
	m_CascadeShadowMapDepthImage.DestroyVulkanImage(m_LogicDevice);

	m_PunctualShadowMapImage.DestroyVulkanImage(m_LogicDevice);

	m_NormalImage.DestroyVulkanImage(m_LogicDevice);
	m_GBufferImage0.DestroyVulkanImage(m_LogicDevice);
	m_GBufferImage1.DestroyVulkanImage(m_LogicDevice);
	m_GBufferImage2.DestroyVulkanImage(m_LogicDevice);
	m_HairColorImage.DestroyVulkanImage(m_LogicDevice);
	m_HairDeepOpacityImage.DestroyVulkanImage(m_LogicDevice);
	m_HairOITHeadImage.DestroyVulkanImage(m_LogicDevice);
	m_HairOITNodeBuffer.DestroyVulkanBuffer(m_LogicDevice);
	m_HairOITCounterBuffer.DestroyVulkanBuffer(m_LogicDevice);

	// 销毁水面 GBuffer 纹理
	m_WaterGBufNormalImage.DestroyVulkanImage(m_LogicDevice);
	m_WaterGBufLinearDepthImage.DestroyVulkanImage(m_LogicDevice);

	m_VansGBufferPass.DestroyRenderPass(m_LogicDevice);
	m_VansRenderPass.DestroyRenderPass(m_LogicDevice);
	m_VansDeferredSkyboxPass.DestroyRenderPass(m_LogicDevice);
	m_VansForwardOpaqueAfterDeferredPass.DestroyRenderPass(m_LogicDevice);
	m_VansHairVisibilityPass.DestroyRenderPass(m_LogicDevice);
	m_VansHairLightingPass.DestroyRenderPass(m_LogicDevice);
	m_VansHairDeepOpacityPass.DestroyRenderPass(m_LogicDevice);
	m_VansWaterGBufferPass.DestroyRenderPass(m_LogicDevice);
	m_VansShadowPass.DestroyRenderPass(m_LogicDevice);
	m_VansPunctualShadowPass.DestroyRenderPass(m_LogicDevice);
	m_VansMotionVectorPass.DestroyRenderPass(m_LogicDevice);
	m_VansUIPass.DestroyRenderPass(m_LogicDevice);
	m_VansSceneUIPass.DestroyRenderPass(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::DestroyUIRenderPass()
{
	m_VansUIPass.DestroyRenderPass(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::DestroySceneUIRenderPass()
{
	m_VansSceneUIPass.DestroyRenderPass(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::RecreateUIRenderPass(VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surface, const VkExtent2D& renderResolution)
{
	DestroyUIRenderPass();
	SetupVansUIRenderPass(m_LogicDevice, command_buffer, queue, surface, renderResolution);
}

void VansGraphics::VansRenderPassManager::ResetFrameBufferImageLayout(VansVKCommandBuffer& command_buffer, VansVKSurface& surface, int swapChainIndex)
{
	//record command buffer
	command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	//设置colordepoth的layout
	m_ColorImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_NormalImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_GBufferImage0.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_GBufferImage1.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	m_GBufferImage2.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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
	m_HairColorImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{ m_HairColorImage.m_VansVKImage, VK_ACCESS_NONE, VK_ACCESS_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, m_HairColorImage.m_ImageAspect });
	m_HairDeepOpacityImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{ m_HairDeepOpacityImage.m_VansVKImage, VK_ACCESS_NONE, VK_ACCESS_NONE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, m_HairDeepOpacityImage.m_ImageAspect });
	m_DepthImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	// Reset motion-vector color image from SHADER_READ_ONLY back to GENERAL
	// so the next frame's motion-vector pass can begin with LOAD_OP_CLEAR.
	m_MotionVectorImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
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

	surface.SetSwapChainImageBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			VK_NULL_HANDLE,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			VK_IMAGE_ASPECT_COLOR_BIT
		}, swapChainIndex);

	m_CascadeShadowMapImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_CascadeShadowMapImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_CascadeShadowMapImage.m_ImageAspect
		});

	//end record
	command_buffer.EndCommandBufferRecord();
}
