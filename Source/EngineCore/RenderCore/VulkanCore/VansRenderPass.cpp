#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansRenderPass.h"
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "VansVKSurface.h"
#include <iostream>
#include <vector>

VansVulkan::VansRenderPassManager* VansVulkan::VansRenderPassManager::instance = nullptr;

void VansVulkan::VansVKRenderPass::CreateRenderPass(VkDevice& logic_device, std::vector<VkAttachmentDescription>& attachments, std::vector<SubpassParameters>& subpass_params, std::vector<VkSubpassDependency>& subpass_dependency)
{
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
		std::cout << "Could not create a render pass." << std::endl;
	}
}

void VansVulkan::VansVKRenderPass::DestroyRenderPass(VkDevice& logic_device)
{
	if (VK_NULL_HANDLE != m_RenderPass)
	{
		vkDestroyRenderPass(logic_device, m_RenderPass, nullptr);
		m_RenderPass = VK_NULL_HANDLE;
	}
}

void VansVulkan::VansFrameBuffer::CreateFrameBuffer(VkDevice& logic_device, VkRenderPass& render_pass, const std::vector<VkImageView>& image_views, VkExtent3D framebuffer_size)
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
		std::cout << "Could not create a framebuffer." << std::endl;
	}
}

void VansVulkan::VansFrameBuffer::DestroyFrameBuffer(VkDevice& logic_device)
{
	if (m_FrameBuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(logic_device, m_FrameBuffer, nullptr);
		m_FrameBuffer = VK_NULL_HANDLE;
	}
}

VansVulkan::VansRenderPassManager::VansRenderPassManager()
{

}

VansVulkan::VansRenderPassManager* VansVulkan::VansRenderPassManager::GetInstance()
{
	if (instance == nullptr)
	{
		instance = new VansRenderPassManager();
	}
	return instance;
}

//创建一个默认的固定render pass先
void VansVulkan::VansRenderPassManager::SetupVansRenderPass(VkDevice& logic_device, VansVKCommandBuffer& command_buffer, VkQueue& queue, VansVKSurface& surface)
{
	std::vector<VkAttachmentDescription> attachments_descriptions = 
	{
		{
			0,
			VK_FORMAT_R16G16B16A16_UNORM,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
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
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		},
		{
			0,
			VK_FORMAT_R8G8B8A8_SRGB,
			VK_SAMPLE_COUNT_1_BIT,
			VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,//第二个subpass使用，直接present
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
		// #1 subpass
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
					2,
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
		 }
	};

	m_ClearValues = 
	{
		{ 0.0f, 0.0f, 0.0f, 1.0f },
		{ 1.0f, 0 },
		{ 0.0f, 0.0f, 0.0f, 1.0f },
	};

	m_VansRenderPass.CreateRenderPass(logic_device, attachments_descriptions, subpass_parameters, subpass_dependencies);

	//创建color,depth
	//这里先创建renderpass,只是指定了各个attachment的状态，实际数据通过framebuffer来创建
	VkExtent2D resolution = surface.m_VansVKSwapChainImageExtent;
	m_ColorImage.CreateVulkanImage(
		logic_device, 
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_R16G16B16A16_UNORM,
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT
		);
	m_DepthImage.CreateVulkanImage(
		logic_device, 
		{ resolution.width,resolution.height,1 },
		VK_FORMAT_D16_UNORM, 
		1,
		1,
		VK_IMAGE_TYPE_2D,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT
	);

	//framebuffer的image view数量和attachment 的数量需要一致
	//由于包含swap chain image ，所以这里frame buffer数量需要和swapchain imaghe数量一致
	m_FrameBuffers.resize(surface.m_VansVKImageCount);
	for (int swapChainIndex = 0; swapChainIndex < surface.m_VansVKImageCount; swapChainIndex++)
	{
		std::vector<VkImageView> image_views = { m_ColorImage.GetImageView(), m_DepthImage.GetImageView() ,surface.GetSwapChainImageView(swapChainIndex) };
		m_FrameBuffers[swapChainIndex].CreateFrameBuffer(logic_device, m_VansRenderPass.m_RenderPass, image_views, {resolution.width, resolution.height, 1});

	}
	
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

	surface.SetSwapChainImageBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			VK_NULL_HANDLE,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			VK_IMAGE_ASPECT_COLOR_BIT
		});

	//end record
	command_buffer.EndCommandBufferRecord();



	VansVKCommandBuffer::SubmitCommands(queue, logic_device, { command_buffer.GetVKCommandBuffer() }, {}, {});
	command_buffer.ResetCommandBuffer(false);
}

void VansVulkan::VansRenderPassManager::BeginRenderPass(VkCommandBuffer command_buffer,const VkRect2D& render_area, GlobalStateData& global_state_data, int swap_chain_index)
{
	//将当前render pass 记录到globaldata中
	global_state_data.currentRenderPass = m_VansRenderPass.m_RenderPass;
	global_state_data.currentSubpass = 0;

	VkRenderPassBeginInfo render_pass_begin_info = 
	{
		 VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		 nullptr,
		 m_VansRenderPass.m_RenderPass,
		 m_FrameBuffers[swap_chain_index].m_FrameBuffer,
		 render_area,
		 static_cast<uint32_t>(m_ClearValues.size()),
		 m_ClearValues.data()
	};

	vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

void VansVulkan::VansRenderPassManager::NextSubPass(VkCommandBuffer command_buffer, GlobalStateData& global_state_data)
{
	global_state_data.currentSubpass++;
	vkCmdNextSubpass(command_buffer, VK_SUBPASS_CONTENTS_INLINE);
}

void VansVulkan::VansRenderPassManager::EndRenderPass(VkCommandBuffer command_buffer, GlobalStateData& global_state_data)
{
	global_state_data.currentRenderPass = VK_NULL_HANDLE;
	global_state_data.currentSubpass = 0;
	vkCmdEndRenderPass(command_buffer);
}

void VansVulkan::VansRenderPassManager::DestroyRenderPass()
{
	m_ColorImage.DestroyVulkanImage(m_LogicDevice);
	m_DepthImage.DestroyVulkanImage(m_LogicDevice);
	for (int i = 0; i < m_FrameBuffers.size(); i++)
	{
		m_FrameBuffers[i].DestroyFrameBuffer(m_LogicDevice);
	}
	m_VansRenderPass.DestroyRenderPass(m_LogicDevice);
}

void VansVulkan::VansRenderPassManager::ResetFrameBufferImageLayout(VansVKCommandBuffer& command_buffer, VansVKSurface& surface, int swapChainIndex)
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
	m_DepthImage.SetImageMemoryBarrier(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
		{
			m_DepthImage.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
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

	//end record
	command_buffer.EndCommandBufferRecord();
}
