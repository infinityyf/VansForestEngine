#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansRenderPass.h"
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "VansVKSurface.h"
#include "../../Configration/VansConfigration.h"
#include "../../Util/VansLog.h"
#include <iostream>
#include <vector>

VansGraphics::VansRenderPassManager* VansGraphics::VansRenderPassManager::instance = nullptr;

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
		m_SubpassDescs.push_back(
			{
				0,
				subpass_description.PipelineType,
				static_cast<uint32_t>(subpass_description.InputAttachments.size()),
				subpass_description.InputAttachments.data(),
				static_cast<uint32_t>(subpass_description.ColorAttachments.size()),
				subpass_description.ColorAttachments.data(),
				subpass_description.ResolveAttachments.data(),
				subpass_description.DepthStencilAttachment,
				static_cast<uint32_t>(subpass_description.PreserveAttachments.size()),
				subpass_description.PreserveAttachments.data()
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
	VkResult result = vkCreateRenderPass(logic_device, &render_pass_create_info, nullptr, &m_RenderPass);
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
		vkDestroyRenderPass(logic_device, m_RenderPass, nullptr);
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

	VkResult result = vkCreateFramebuffer(logic_device, &framebuffer_create_info, nullptr, &m_FrameBuffer);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Could not create a framebuffer.");
	}
}

void VansGraphics::VansFrameBuffer::DestroyFrameBuffer(VkDevice& logic_device)
{
	if (m_FrameBuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(logic_device, m_FrameBuffer, nullptr);
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
	//设置Gbuffer的attachment描述符
	std::vector<VkAttachmentDescription> attachments_descriptions =
	{
		//color， 用于defer shading的结果
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

		//normal
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
		//gbuffer0
		{
			0,
			VK_FORMAT_R16G16B16A16_UNORM,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},

		//gbuffer1
		{
			0,
			VK_FORMAT_R16G16B16A16_UNORM,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		},

		//gbuffer2
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

		//depth
		{
			0,
			VK_FORMAT_D16_UNORM,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		},

		//color after post process
		{
			0,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,//第二个subpass输出，这里不直接present，后续还要后处理
		},
	};

	VkAttachmentReference depth_stencil_attachment =
	{
		 5,
		 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	};

	std::vector<SubpassParameters> subpass_parameters =
	{
		// #0 subpass
		// gbuffer
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{},
			{
				{
					1,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				},
				{
					2,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				},
				{
					3,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				},
				{
					4,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				}
			},
			{},
			&depth_stencil_attachment,
			{}
		},
		// #1 subpass
		// shading
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{
				{
					1,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				},
				{
					2,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				},
				{
					3,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				},
				{
					4,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				},
				{
					5,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			},
			{
				{
					0,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				}
			},
			{},
			& depth_stencil_attachment,
			{}
		},
		// #2 subpass 
		// post process
		{
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			{
				{
					0,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
				}
			},
			{
				{
					6,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
				}
			},
			{},
			nullptr,
			{}
		}
	};

	std::vector<VkSubpassDependency> subpass_dependencies =
	{
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
			 2,
			 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			 VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
			 VK_DEPENDENCY_BY_REGION_BIT
		 }
	};

	m_VansRenderPass.m_ClearValues =
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 0.0f },
		{ 1.0f, 0 },
		{ 0.0f, 0.0f, 0.0f, 1.0f },
	};

	VkExtent2D resolution = renderResolution;
	m_VansRenderPass.CreateRenderPass(logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies, resolution);

	//创建color,depth,GBuffers
	//这里先创建renderpass,只是指定了各个attachment的状态，实际数据通过framebuffer来创建
	
	m_ColorImage.CreateVulkanImage(
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
	m_DepthImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_D16_UNORM,
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
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);

	m_GBufferImage0.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_UNORM,
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

#ifdef _DEBUG
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_ColorImage.GetImage());
	nameInfo.pObjectName = "ColorImage";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_DepthImage.GetImage());
	nameInfo.pObjectName = "DepthImage";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_NormalImage.GetImage());
	nameInfo.pObjectName = "NormalImage";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_GBufferImage0.GetImage());
	nameInfo.pObjectName = "GBuffer0Image";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_GBufferImage1.GetImage());
	nameInfo.pObjectName = "GBuffer1Image";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_GBufferImage2.GetImage());
	nameInfo.pObjectName = "GBuffer2Image";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);
#endif

	//framebuffer的image view数量和attachment 的数量需要一致
	m_VansRenderPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> image_views =
	{
		m_ColorImage.GetImageView(),
		m_NormalImage.GetImageView() ,
		m_GBufferImage0.GetImageView(),
		m_GBufferImage1.GetImageView(),
		m_GBufferImage2.GetImageView(),
		m_DepthImage.GetImageView() ,
		m_ColorAfterPostProcessImage.GetImageView()//surface.GetSwapChainImageView(swapChainIndex)
	};
	m_VansRenderPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansRenderPass.m_RenderPass, image_views, { resolution.width, resolution.height, 1 });

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
	//end record
	command_buffer.EndCommandBufferRecord();

	VansVKCommandBuffer::SubmitCommands(queue, logic_device, { command_buffer.GetVKCommandBuffer() }, {}, {}, command_buffer.m_CommandBufferFinishSubmitFence);
	command_buffer.ResetCommandBuffer(false);
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
		VK_FORMAT_D16_UNORM,
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
			vkCreateImageView(logic_device, &viewInfo, nullptr, &m_CascadeColorLayerViews[i]);
		}
		// Depth layer view
		{
			VkImageViewCreateInfo viewInfo = {};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_CascadeShadowMapDepthImage.GetImage();
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = VK_FORMAT_D16_UNORM;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = (uint32_t)i;
			viewInfo.subresourceRange.layerCount = 1;
			vkCreateImageView(logic_device, &viewInfo, nullptr, &m_CascadeDepthLayerViews[i]);
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
		vkCreateImageView(logic_device, &viewInfo, nullptr, &m_CascadeShadowArrayView);
	}

	// Create sampler for cascade shadow array
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
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 1.0f;
		vkCreateSampler(logic_device, &samplerInfo, nullptr, &m_CascadeShadowSampler);
	}

#ifdef _DEBUG
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_CascadeShadowMapImage.GetImage());
	nameInfo.pObjectName = "CascadeShadowMap";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_CascadeShadowMapDepthImage.GetImage());
	nameInfo.pObjectName = "CascadeShadowMapDepth";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);
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

	command_buffer.EndCommandBufferRecord();

	VansVKCommandBuffer::SubmitCommands(queue, logic_device, { command_buffer.GetVKCommandBuffer() }, {}, {}, command_buffer.m_CommandBufferFinishSubmitFence);
	command_buffer.ResetCommandBuffer(false);
}

void VansGraphics::VansRenderPassManager::SetupVansPunctualShadowRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue)
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
			VK_IMAGE_LAYOUT_GENERAL, //render passbegin的layout
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, //这里的final layout会自动切换,render pass结束后的layout
		},
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
			&depth_stencil_attachment,
			{}
		},
	};

	//shadow 默认1
	m_VansPunctualShadowPass.m_ClearValues =
	{
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 0 },
	};

	//不切换subpass
	std::vector<VkSubpassDependency> subpass_dependencies;

	auto vansConfigration = VansConfigration::GetInstance();
	VkExtent2D resolution = { vansConfigration->GetPunctualShadowMapWidth(), vansConfigration->GetPunctualShadowMapHeight() };

	m_VansPunctualShadowPass.CreateRenderPass(logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies, resolution);

	//创建color,depth
	m_PunctualShadowMapImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R32_SFLOAT,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		false,
		false,
		true
	);
	m_PunctualShadowMapDepthImage.CreateVulkanImage(
		logic_device,
		{ resolution.width,resolution.height,1 },
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
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_PunctualShadowMapImage.GetImage());
	nameInfo.pObjectName = "PunctualShadowMap";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);

	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_PunctualShadowMapDepthImage.GetImage());
	nameInfo.pObjectName = "PunctualShadowMapDepth";
	vkSetDebugUtilsObjectNameEXT(logic_device, &nameInfo);
#endif

	m_VansPunctualShadowPass.m_FrameBuffers.resize(1);
	std::vector<VkImageView> image_views = {
			m_PunctualShadowMapImage.GetImageView(),
			m_PunctualShadowMapDepthImage.GetImageView() };
	m_VansPunctualShadowPass.m_FrameBuffers[0].CreateFrameBuffer(logic_device, m_VansPunctualShadowPass.m_RenderPass, image_views, { resolution.width, resolution.height, 1 });

	m_LogicDevice = logic_device;

	//record command buffer
	command_buffer.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	//设置colordepoth的layout
	m_PunctualShadowMapImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_PunctualShadowMapImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_PunctualShadowMapImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_PunctualShadowMapImage.m_ImageAspect
		});
	m_PunctualShadowMapDepthImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_PunctualShadowMapDepthImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			m_PunctualShadowMapDepthImage.m_ImageLayout,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_PunctualShadowMapDepthImage.m_ImageAspect
		});

	//end record
	command_buffer.EndCommandBufferRecord();

	VansVKCommandBuffer::SubmitCommands(queue, logic_device, { command_buffer.GetVKCommandBuffer() }, {}, {}, command_buffer.m_CommandBufferFinishSubmitFence);
	command_buffer.ResetCommandBuffer(false);
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

void VansGraphics::VansRenderPassManager::BeginRenderPass(VansVKRenderPass& renderPass,VkCommandBuffer command_buffer, GlobalStateData& global_state_data, int swap_chain_index)
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

	vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	//begin的时候设置viewport和sissor
	vkCmdSetViewport(command_buffer, 0, 1, &renderPass.m_RenderPassViewport);
	vkCmdSetScissor(command_buffer, 0, 1, &renderPass.m_RenderPassScissor);
}

void VansGraphics::VansRenderPassManager::NextSubPass(VkCommandBuffer command_buffer, GlobalStateData& global_state_data)
{
	global_state_data.currentSubpass++;
	vkCmdNextSubpass(command_buffer, VK_SUBPASS_CONTENTS_INLINE);
}

void VansGraphics::VansRenderPassManager::EndRenderPass(VkCommandBuffer command_buffer, GlobalStateData& global_state_data)
{
	global_state_data.currentRenderPass = VK_NULL_HANDLE;
	global_state_data.currentSubpass = 0;
	vkCmdEndRenderPass(command_buffer);
}

void VansGraphics::VansRenderPassManager::BlitToSwapChainImage(VansVKCommandBuffer& command_buffer, VansVKSurface& surface, int swapChainIndex, const VkExtent2D& renderResolution)
{
    // Blit m_ColorAfterPostProcessImage (attachment index 6 in deferred pass framebuffer)
    // into the real swapchain image for presentation.
    // Steps:
    // 1. Transition source to TRANSFER_SRC_OPTIMAL.
    // 2. Transition destination (swapchain) to TRANSFER_DST_OPTIMAL.
    // 3. vkCmdBlitImage
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

    vkCmdBlitImage(
        command_buffer.GetVKCommandBuffer(),
        m_ColorAfterPostProcessImage.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        surface.GetSwapChainImage(swapChainIndex),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blitRegion,
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
	m_DepthImage.DestroyVulkanImage(m_LogicDevice);
	m_MotionVectorImage.DestroyVulkanImage(m_LogicDevice);
	m_ColorAfterPostProcessImage.DestroyVulkanImage(m_LogicDevice);

	m_ShadowMapImage.DestroyVulkanImage(m_LogicDevice);
	m_ShadowMapDepthImage.DestroyVulkanImage(m_LogicDevice);

	// Destroy cascade shadow resources
	for (int i = 0; i < 4; ++i)
	{
		if (m_CascadeColorLayerViews[i] != VK_NULL_HANDLE)
			vkDestroyImageView(m_LogicDevice, m_CascadeColorLayerViews[i], nullptr);
		if (m_CascadeDepthLayerViews[i] != VK_NULL_HANDLE)
			vkDestroyImageView(m_LogicDevice, m_CascadeDepthLayerViews[i], nullptr);
	}
	if (m_CascadeShadowArrayView != VK_NULL_HANDLE)
		vkDestroyImageView(m_LogicDevice, m_CascadeShadowArrayView, nullptr);
	if (m_CascadeShadowSampler != VK_NULL_HANDLE)
		vkDestroySampler(m_LogicDevice, m_CascadeShadowSampler, nullptr);
	m_CascadeShadowMapImage.DestroyVulkanImage(m_LogicDevice);
	m_CascadeShadowMapDepthImage.DestroyVulkanImage(m_LogicDevice);

	m_PunctualShadowMapImage.DestroyVulkanImage(m_LogicDevice);
	m_PunctualShadowMapDepthImage.DestroyVulkanImage(m_LogicDevice);

	m_NormalImage.DestroyVulkanImage(m_LogicDevice);
	m_GBufferImage0.DestroyVulkanImage(m_LogicDevice);
	m_GBufferImage1.DestroyVulkanImage(m_LogicDevice);
	m_GBufferImage2.DestroyVulkanImage(m_LogicDevice);

	m_VansRenderPass.DestroyRenderPass(m_LogicDevice);
	m_VansShadowPass.DestroyRenderPass(m_LogicDevice);
	m_VansPunctualShadowPass.DestroyRenderPass(m_LogicDevice);
	m_VansUIPass.DestroyRenderPass(m_LogicDevice);
}

void VansGraphics::VansRenderPassManager::DestroyUIRenderPass()
{
	m_VansUIPass.DestroyRenderPass(m_LogicDevice);
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

	m_PunctualShadowMapImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_PunctualShadowMapImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			m_PunctualShadowMapImage.m_ImageAspect
		});

	//end record
	command_buffer.EndCommandBufferRecord();
}
