#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKCommandBuffer.h"
#include "VansVKBuffer.h"
#include "VansVKImage.h"
#include "VansMesh.h"
#include "VansShader.h"
#include "VansRenderPass.h"
#include "VansPipeline.h"
#include "VansVKDevice.h"
#include "../../Util/VansLog.h"
#include <iostream>
#include <cassert>

namespace
{
	bool ValidateDescriptorSetLayouts(
		const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts,
		const char* usage)
	{
		for (VkDescriptorSetLayout layout : descriptorSetLayouts)
		{
			if (layout == VK_NULL_HANDLE)
			{
				VANS_LOG_ERROR(usage << " skipped because descriptor set layout list contains VK_NULL_HANDLE");
				return false;
			}
		}
		return true;
	}

	bool ValidateDescriptorSets(
		const std::vector<VkDescriptorSet>& descriptorSets,
		const char* usage)
	{
		if (descriptorSets.empty())
		{
			VANS_LOG_ERROR(usage << " skipped because descriptor set list is empty");
			return false;
		}
		for (VkDescriptorSet descriptorSet : descriptorSets)
		{
			if (descriptorSet == VK_NULL_HANDLE)
			{
				VANS_LOG_ERROR(usage << " skipped because descriptor set list contains VK_NULL_HANDLE");
				return false;
			}
		}
		return true;
	}
}

bool VansGraphics::VansVKCommandBuffer::CreateVulkanCommandBuffer(VansVKDevice& device ,uint32_t queue_family, CommandBufferCreateParams& buffer_create_info)
{
	m_VansVKDevice = device.GetLogicDevice();
	//create command pool
	//source memory of command buffers
	//command in command buffer can only be submitted to specified queue family
	VkCommandPoolCreateFlags parameters = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkCommandPoolCreateInfo command_pool_create_info = 
	{
		 VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		 nullptr,
		 buffer_create_info.pool_params,
		 queue_family
	};

	//Command pools cannot be accessed at the same time from multiple threads
	//each application thread on which a command buffer will be recorded should use separate command pools
	VkResult result = VansGraphics::vkCreateCommandPool(m_VansVKDevice, &command_pool_create_info, nullptr, &m_VansVKCommandPool);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Could not create command pool.");
		return false;
	}

	VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	VkCommandBufferAllocateInfo command_buffer_allocate_info = 
	{
		 VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		 nullptr,
		 m_VansVKCommandPool,
		 buffer_create_info.commandbuffer_level,
		 buffer_create_info.commandbuffer_count
	};
	result = VansGraphics::vkAllocateCommandBuffers(m_VansVKDevice, &command_buffer_allocate_info, &m_VansVKCommandBuffer);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Could not allocate command buffers.");
		return false;
	}

	return true;
}


void VansGraphics::VansVKCommandBuffer::DestroyVulkanCommandBuffer(VkDevice& logical_device)
{
	//POOL free alse free the buffer
	if (VK_NULL_HANDLE != m_VansVKCommandPool)
	{
		VansGraphics::vkDestroyCommandPool(logical_device, m_VansVKCommandPool, nullptr);
		m_VansVKCommandPool = VK_NULL_HANDLE;
		m_VansVKCommandBuffer = VK_NULL_HANDLE;
	}

	m_VansVKDevice = VK_NULL_HANDLE;
}

void VansGraphics::VansVKCommandBuffer::ClearColor(VansVKImage& image, const VkClearColorValue& value)
{
	VkImageSubresourceRange image_subresource_range = 
	{
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
	};
	VansGraphics::vkCmdClearColorImage(
		m_VansVKCommandBuffer, 
		image.m_VansVKImage, 
		image.m_ImageLayout,
		&value,
		1,
		&image_subresource_range);
}

void VansGraphics::VansVKCommandBuffer::ClearColorImage(VansVKImage& image, VkImageLayout layout, const VkClearColorValue& value)
{
	VkImageSubresourceRange image_subresource_range =
	{
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
	};
	VansGraphics::vkCmdClearColorImage(
		m_VansVKCommandBuffer,
		image.m_VansVKImage,
		layout,
		&value,
		1,
		&image_subresource_range);
}

void VansGraphics::VansVKCommandBuffer::ClearMRTColor(const std::vector<VansVKImage>& images, const std::vector<VkClearColorValue>& values)
{
	VkImageSubresourceRange image_subresource_range =
	{
		VK_IMAGE_ASPECT_COLOR_BIT,
		0,
		1,
		0,
		1,
	};

	for (int imageIndex = 0; imageIndex < images.size(); imageIndex++)
	{
		auto& image = images[imageIndex];
		VansGraphics::vkCmdClearColorImage(
			m_VansVKCommandBuffer,
			image.m_VansVKImage,
			image.m_ImageLayout,
			&values[imageIndex],
			1,
			&image_subresource_range);
	}
}

void VansGraphics::VansVKCommandBuffer::ClearDepthStencil(VansVKImage& image, const VkClearDepthStencilValue& value)
{
	VkImageSubresourceRange image_subresource_range =
	{
		VK_IMAGE_ASPECT_DEPTH_BIT,
		0,
		1,
		0,
		1,
	};
	VansGraphics::vkCmdClearDepthStencilImage(
		m_VansVKCommandBuffer,
		image.m_VansVKImage,
		image.m_ImageLayout,
		&value,
		1,
		&image_subresource_range);
}

void VansGraphics::VansVKCommandBuffer::ClearAttachment(std::vector<VkClearAttachment>& attachments, std::vector<VkClearRect>& rests)
{
	if (attachments.empty() || rests.empty())
	{
		return;
	}
	VansGraphics::vkCmdClearAttachments(
		m_VansVKCommandBuffer,
		static_cast<uint32_t>(attachments.size()), 
		attachments.data(),
		static_cast<uint32_t>(rests.size()), 
		rests.data());
}


void VansGraphics::VansVKCommandBuffer::UpdatePushConstants(VansVKGraphicsPipeline& pipeline, VkShaderStageFlags flags, uint32_t offset, uint32_t size, void* data)
{
	VansGraphics::vkCmdPushConstants(
		m_VansVKCommandBuffer,
		pipeline.m_VansPipelineLayout,
		flags,
		offset,
		size,
		data);
}

void VansGraphics::VansVKCommandBuffer::SetViewport(uint32_t first_viewport, const std::vector<VkViewport>& viewports)
{

	VansGraphics::vkCmdSetViewport(
		m_VansVKCommandBuffer,
		first_viewport,
		static_cast<uint32_t>(viewports.size()), 
		viewports.data());
}

void VansGraphics::VansVKCommandBuffer::SetScissor(uint32_t first_scissor, const std::vector<VkRect2D>& scissors)
{
	VansGraphics::vkCmdSetScissor(
		m_VansVKCommandBuffer,
		first_scissor,
		static_cast<uint32_t>(scissors.size()), 
		scissors.data());
}

void VansGraphics::VansVKCommandBuffer::BeginRenderPass(const VkRenderPassBeginInfo& render_pass_begin_info, VkSubpassContents contents)
{
	VansGraphics::vkCmdBeginRenderPass(m_VansVKCommandBuffer, &render_pass_begin_info, contents);
}

void VansGraphics::VansVKCommandBuffer::NextSubpass(VkSubpassContents contents)
{
	VansGraphics::vkCmdNextSubpass(m_VansVKCommandBuffer, contents);
}

void VansGraphics::VansVKCommandBuffer::EndRenderPass()
{
	VansGraphics::vkCmdEndRenderPass(m_VansVKCommandBuffer);
}

void VansGraphics::VansVKCommandBuffer::SetLineWidth(float line_width)
{
	VansGraphics::vkCmdSetLineWidth(m_VansVKCommandBuffer, line_width);
}

void VansGraphics::VansVKCommandBuffer::SetDepthBias(float constant_factor, float clamp, float slope_factor)
{
	//clamp:specify the maximal or minimal value of the depth bias
	//slope_factor is a scalar factor applied to a fragment鈥檚 slope in depth bias calculations.
	VansGraphics::vkCmdSetDepthBias(m_VansVKCommandBuffer, constant_factor, clamp, slope_factor);
}

void VansGraphics::VansVKCommandBuffer::SetBlendConstants(float blend_constants[4])
{
	VansGraphics::vkCmdSetBlendConstants(m_VansVKCommandBuffer, blend_constants);
}

void VansGraphics::VansVKCommandBuffer::DrawMesh(VansMesh& mesh, VansGraphicsShader& shader, uint32_t instance_count)
{
	VansVKGraphicsPipeline* pipeline = shader.GetGraphicsPipeline();
	if (pipeline == nullptr)
	{
		VANS_LOG_ERROR("draw skipped because graphics pipeline is null");
		return;
	}
	BindGraphicsPipeline(*pipeline);
	VansGraphics::vkCmdDrawIndexed(
		m_VansVKCommandBuffer,
		mesh.GetIndexCount(),
		instance_count, 
		0,
		0, 
		0);
}

void VansGraphics::VansVKCommandBuffer::DrawMesh(VansMesh& mesh, VansVKGraphicsPipeline& pipeline, uint32_t instance_count)
{
	BindGraphicsPipeline(pipeline);
	VansGraphics::vkCmdDrawIndexed(
		m_VansVKCommandBuffer,
		mesh.GetIndexCount(),
		instance_count,
		0,
		0,
		0);
}

void VansGraphics::VansVKCommandBuffer::DrawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
	VansGraphics::vkCmdDrawIndexedIndirect(m_VansVKCommandBuffer, buffer, offset, drawCount, stride);
}

void VansGraphics::VansVKCommandBuffer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize srcOffset, VkDeviceSize dstOffset, VkDeviceSize size)
{
	VkBufferCopy region = {};
	region.srcOffset = srcOffset;
	region.dstOffset = dstOffset;
	region.size      = size;
	VansGraphics::vkCmdCopyBuffer(m_VansVKCommandBuffer, srcBuffer, dstBuffer, 1, &region);
}

void VansGraphics::VansVKCommandBuffer::FillBuffer(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data)
{
	VansGraphics::vkCmdFillBuffer(m_VansVKCommandBuffer, buffer, offset, size, data);
}

void VansGraphics::VansVKCommandBuffer::ExecuteSecondaryCommandBuffer(std::vector<VkCommandBuffer>& secondary_command_buffers)
{
	VansGraphics::vkCmdExecuteCommands(
		m_VansVKCommandBuffer,
		static_cast<uint32_t>(secondary_command_buffers.size()),
		secondary_command_buffers.data());
}

void VansGraphics::VansVKCommandBuffer::BindMesh(VansMesh& mesh, uint32_t fist_bind, GlobalStateData& global_state_data)
{
	VertexBufferParameters vparam = mesh.GetVertexBufferParameter();
	VansGraphics::vkCmdBindVertexBuffers(
		m_VansVKCommandBuffer,
		fist_bind,
		1,
		&vparam.Buffer,
		&vparam.MemoryOffset);

	IndexBufferParameters iparam = mesh.GetIndexBufferParameter();
	VansGraphics::vkCmdBindIndexBuffer(
		m_VansVKCommandBuffer,
		iparam.Buffer,
		iparam.MemoryOffset,
		iparam.IndexType);

	// ?? mesh ? vertex input data
	global_state_data.vertexInputAttributeDescriptions = &mesh.m_VertexInputAttributeDescriptions;
	global_state_data.vertexInputBindingDescriptions = &mesh.m_VertexInputBindingDescriptions;

}

void VansGraphics::VansVKCommandBuffer::BuildAccelerationStructures(VkAccelerationStructureBuildGeometryInfoKHR* buildInfo, const VkAccelerationStructureBuildRangeInfoKHR* rangeInfo)
{
	const VkAccelerationStructureBuildRangeInfoKHR* rangeInfos[] = { rangeInfo };
	BuildAccelerationStructures(buildInfo, rangeInfos);
}

void VansGraphics::VansVKCommandBuffer::BuildAccelerationStructures(VkAccelerationStructureBuildGeometryInfoKHR* buildInfo, const VkAccelerationStructureBuildRangeInfoKHR* const* rangeInfos)
{
	VansGraphics::vkCmdBuildAccelerationStructuresKHR(m_VansVKCommandBuffer, 1, buildInfo, rangeInfos);
}

VansGraphics::VansVKGraphicsPipeline* VansGraphics::VansVKCommandBuffer::EnsureGraphicsShader(VansGraphicsShader& shader, GlobalStateData& global_state_data, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	if (!ValidateDescriptorSetLayouts(descriptorset_layouts, "graphics pipeline ensure"))
	{
		return nullptr;
	}

	VansVKGraphicsPipeline* pipeline = shader.GetGraphicsPipeline(m_VansVKDevice, global_state_data, descriptorset_layouts);
	if (pipeline == nullptr)
	{
		VANS_LOG_ERROR("pipe get failed");
		return nullptr;
	}
	return pipeline;
}

void VansGraphics::VansVKCommandBuffer::EnsureComputeShader(VansComputeShader& shader, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	// Ensure the compute shader pipeline is ready.
	if (!ValidateDescriptorSetLayouts(descriptorset_layouts, "compute pipeline ensure"))
	{
		return;
	}

	VansVKComputePipeline* pipeline = shader.GetComputePipeline(m_VansVKDevice, descriptorset_layouts);
	if (pipeline == nullptr)
	{
		VANS_LOG_ERROR("compute pipe get failed");
		return;
	}
}

void VansGraphics::VansVKCommandBuffer::DispatchCompute(VansComputeShader& shader, uint32_t x_size, uint32_t y_size, uint32_t z_size, const std::vector<VkDescriptorSet>& descriptor_sets)

{
	DispatchCompute(shader, x_size, y_size, z_size, descriptor_sets,
		shader.GetPushConstantData(), static_cast<uint32_t>(std::max(0, shader.GetPushConstantSize())));
}

void VansGraphics::VansVKCommandBuffer::DispatchCompute(
	VansComputeShader& shader,
	uint32_t x_size,
	uint32_t y_size,
	uint32_t z_size,
	const std::vector<VkDescriptorSet>& descriptor_sets,
	const void* pushConstantData,
	uint32_t pushConstantSize)
{
	VansVKComputePipeline* pipeline = shader.GetComputePipeline();
	if (pipeline == nullptr)
	{
		VANS_LOG_ERROR("dispatch skipped because compute pipeline is null");
		return;
	}
	if (pipeline->m_VansPipelineLayout == VK_NULL_HANDLE)
	{
		VANS_LOG_ERROR("dispatch skipped because compute pipeline layout is null");
		return;
	}
	if (!ValidateDescriptorSets(descriptor_sets, "compute dispatch"))
	{
		return;
	}
	BindComputePipeline(*pipeline);

	// Bind descriptor sets.
	VansGraphics::vkCmdBindDescriptorSets(
		m_VansVKCommandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline->m_VansPipelineLayout,
		0,
		static_cast<uint32_t>(descriptor_sets.size()),
		descriptor_sets.data(),
		0,
		nullptr);

	const uint32_t declaredPushConstantSize = static_cast<uint32_t>(std::max(0, shader.GetPushConstantSize()));
	if (pushConstantSize > declaredPushConstantSize)
	{
		VANS_LOG_ERROR("dispatch skipped because push constant payload exceeds the shader interface");
		return;
	}
	if (pushConstantSize > 0 && pushConstantData != nullptr)
	{
		VansGraphics::vkCmdPushConstants(m_VansVKCommandBuffer, pipeline->m_VansPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
			0, pushConstantSize, pushConstantData);
	}

	pipeline->DispatchCompute(m_VansVKCommandBuffer, x_size, y_size, z_size);
}

void VansGraphics::VansVKCommandBuffer::BlitImage(VansVKImage& source, int source_mip, VansVKImage& target, int target_mip)
{
	VkImageCopy copyRegion = {};
	copyRegion.srcSubresource.aspectMask = source.GetImageAspect();
	copyRegion.srcSubresource.mipLevel = source_mip;
	copyRegion.srcSubresource.baseArrayLayer = 0;
	copyRegion.srcSubresource.layerCount = 1;
	copyRegion.srcOffset = { 0, 0, 0 };

	copyRegion.dstSubresource.aspectMask = target.GetImageAspect();
	copyRegion.dstSubresource.mipLevel = target_mip;
	copyRegion.dstSubresource.baseArrayLayer = 0;
	copyRegion.dstSubresource.layerCount = 1;
	copyRegion.dstOffset = { 0, 0, 0 };

	uint32_t width = std::min(source.GetImageDimension().width, target.GetImageDimension().width);
	uint32_t height = std::min(source.GetImageDimension().height, target.GetImageDimension().height);
	copyRegion.extent.width = width;
	copyRegion.extent.height = height;
	copyRegion.extent.depth = 1;

	VansGraphics::vkCmdCopyImage(
		m_VansVKCommandBuffer,
		source.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		target.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &copyRegion
	);

	// Restore the source image layout after copying.
	source.SetImageMemoryBarrier(*this, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		{
			source.m_VansVKImage,
			VK_ACCESS_NONE,
			VK_ACCESS_NONE,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_QUEUE_FAMILY_IGNORED,
			VK_QUEUE_FAMILY_IGNORED,
			source.m_ImageAspect
		});
}

void VansGraphics::VansVKCommandBuffer::CopyImageRegions(VansVKImage& source, VkImageLayout sourceLayout,
	VansVKImage& target, VkImageLayout targetLayout,
	const std::vector<VkImageCopy>& copyRegions)
{
	if (copyRegions.empty())
		return;

	VansGraphics::vkCmdCopyImage(
		m_VansVKCommandBuffer,
		source.GetImage(), sourceLayout,
		target.GetImage(), targetLayout,
		static_cast<uint32_t>(copyRegions.size()), copyRegions.data());
}

void VansGraphics::VansVKCommandBuffer::BlitImageRegions(VansVKImage& source, VkImageLayout sourceLayout,
	VansVKImage& target, VkImageLayout targetLayout,
	const std::vector<VkImageBlit>& blitRegions,
	VkFilter filter)
{
	if (blitRegions.empty())
		return;

	BlitImageRegions(source.GetImage(), sourceLayout, target.GetImage(), targetLayout, blitRegions, filter);
}

void VansGraphics::VansVKCommandBuffer::BlitImageRegions(VkImage source, VkImageLayout sourceLayout,
	VkImage target, VkImageLayout targetLayout,
	const std::vector<VkImageBlit>& blitRegions,
	VkFilter filter)
{
	if (blitRegions.empty())
		return;

	VansGraphics::vkCmdBlitImage(
		m_VansVKCommandBuffer,
		source, sourceLayout,
		target, targetLayout,
		static_cast<uint32_t>(blitRegions.size()), blitRegions.data(), filter);
}

void VansGraphics::VansVKCommandBuffer::BindDescriptorSets(
	VkPipelineBindPoint pipeline_type, 
	VansGraphicsShader& shader,
	int index_for_first_set,
	const std::vector<VkDescriptorSet>& descriptor_sets, 
	const std::vector<uint32_t>& dynamic_offsets)
{
	// Bind the descriptor sets associated with this pipeline.
	// Dynamic offsets are forwarded to vkCmdBindDescriptorSets.
	VansVKGraphicsPipeline* pipeline = shader.GetGraphicsPipeline();
	if (pipeline == nullptr)
	{
		VANS_LOG_ERROR("descriptor bind skipped because graphics pipeline is null");
		return;
	}
	if (pipeline->m_VansPipelineLayout == VK_NULL_HANDLE)
	{
		VANS_LOG_ERROR("descriptor bind skipped because graphics pipeline layout is null");
		return;
	}
	if (!ValidateDescriptorSets(descriptor_sets, "descriptor bind"))
	{
		return;
	}
	VansGraphics::vkCmdBindDescriptorSets(
		m_VansVKCommandBuffer,
		pipeline_type,
		pipeline->m_VansPipelineLayout,
		index_for_first_set,
		static_cast<uint32_t>(descriptor_sets.size()),
		descriptor_sets.data(),
		static_cast<uint32_t>(dynamic_offsets.size()),
		dynamic_offsets.data());
}

void VansGraphics::VansVKCommandBuffer::BindDescriptorSets(
	VkPipelineBindPoint pipeline_type,
	VansVKGraphicsPipeline& pipeline,
	int index_for_first_set,
	const std::vector<VkDescriptorSet>& descriptor_sets,
	const std::vector<uint32_t>& dynamic_offsets)
{
	if (pipeline.m_VansPipelineLayout == VK_NULL_HANDLE)
	{
		VANS_LOG_ERROR("descriptor bind skipped because graphics pipeline layout is null");
		return;
	}
	if (!ValidateDescriptorSets(descriptor_sets, "descriptor bind"))
	{
		return;
	}
	VansGraphics::vkCmdBindDescriptorSets(
		m_VansVKCommandBuffer,
		pipeline_type,
		pipeline.m_VansPipelineLayout,
		index_for_first_set,
		static_cast<uint32_t>(descriptor_sets.size()),
		descriptor_sets.data(),
		static_cast<uint32_t>(dynamic_offsets.size()),
		dynamic_offsets.data());
}

void VansGraphics::VansVKCommandBuffer::BindGraphicsPipeline(VansVKGraphicsPipeline& graphicsPipeline)
{
	if (m_BoundGraphicsPipeline == graphicsPipeline.m_GraphicsPipeline)
	{
		return;
	}

	graphicsPipeline.BindGraphicsPipeline(m_VansVKCommandBuffer);
	m_BoundGraphicsPipeline = graphicsPipeline.m_GraphicsPipeline;
}

void VansGraphics::VansVKCommandBuffer::BindComputePipeline(VansVKComputePipeline& computePipeline)
{
	if (m_BoundComputePipeline == computePipeline.m_ComputePipeline)
	{
		return;
	}

	computePipeline.BindComputePipeline(m_VansVKCommandBuffer);
	m_BoundComputePipeline = computePipeline.m_ComputePipeline;
}

void VansGraphics::VansVKCommandBuffer::BindRayTracingPipeline(VansVKRayTracingPipeline& rayTracingPipeline)
{
	if (m_BoundRayTracingPipeline == rayTracingPipeline.m_RayTracingPipeline)
	{
		return;
	}

	VansGraphics::vkCmdBindPipeline(m_VansVKCommandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rayTracingPipeline.m_RayTracingPipeline);
	m_BoundRayTracingPipeline = rayTracingPipeline.m_RayTracingPipeline;
}

void VansGraphics::VansVKCommandBuffer::BindRayTracingDescriptorSets(
	VansVKRayTracingPipeline& rayTracingPipeline,
	uint32_t firstSet,
	const std::vector<VkDescriptorSet>& descriptorSets,
	const std::vector<uint32_t>& dynamicOffsets)
{
	if (descriptorSets.empty())
	{
		return;
	}
	if (rayTracingPipeline.m_RayTracingLayout == VK_NULL_HANDLE)
	{
		VANS_LOG_ERROR("ray tracing descriptor bind skipped because pipeline layout is null");
		return;
	}
	for (VkDescriptorSet descriptorSet : descriptorSets)
	{
		if (descriptorSet == VK_NULL_HANDLE)
		{
			VANS_LOG_ERROR("ray tracing descriptor bind skipped because descriptor set list contains VK_NULL_HANDLE");
			return;
		}
	}

	VansGraphics::vkCmdBindDescriptorSets(
		m_VansVKCommandBuffer,
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		rayTracingPipeline.m_RayTracingLayout,
		firstSet,
		static_cast<uint32_t>(descriptorSets.size()),
		descriptorSets.data(),
		static_cast<uint32_t>(dynamicOffsets.size()),
		dynamicOffsets.data());
}

void VansGraphics::VansVKCommandBuffer::UpdateRayTracingPushConstants(
	VansVKRayTracingPipeline& rayTracingPipeline,
	VkShaderStageFlags flags,
	uint32_t offset,
	uint32_t size,
	const void* data)
{
	if (size == 0 || data == nullptr)
	{
		return;
	}

	VansGraphics::vkCmdPushConstants(
		m_VansVKCommandBuffer,
		rayTracingPipeline.m_RayTracingLayout,
		flags,
		offset,
		size,
		data);
}

void VansGraphics::VansVKCommandBuffer::TraceRays(
	VansVKRayTracingPipeline& rayTracingPipeline,
	uint32_t width,
	uint32_t height,
	uint32_t depth)
{
	VansGraphics::vkCmdTraceRaysKHR(
		m_VansVKCommandBuffer,
		&rayTracingPipeline.m_RaygenShaderBindingTable,
		&rayTracingPipeline.m_MissShaderBindingTable,
		&rayTracingPipeline.m_HitShaderBindingTable,
		&rayTracingPipeline.m_CallableShaderBindingTable,
		width,
		height,
		depth);
}

bool VansGraphics::VansVKCommandBuffer::BeginCommandBufferRecord(VkCommandBufferUsageFlags commandBufferUsage)
{
	//only meaningful for secondary command buffers
	VkCommandBufferInheritanceInfo* secondary_command_buffer_info = nullptr;
	VkCommandBufferBeginInfo command_buffer_begin_info = 
	{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		nullptr,
		commandBufferUsage,
		secondary_command_buffer_info
	};
	//when beginCommandbuffer, it implicity reset
	VkResult result = VansGraphics::vkBeginCommandBuffer(m_VansVKCommandBuffer, &command_buffer_begin_info);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Could not begin command buffer.");
		return false;
	}
	m_BoundGraphicsPipeline = VK_NULL_HANDLE;
	m_BoundComputePipeline = VK_NULL_HANDLE;
	m_BoundRayTracingPipeline = VK_NULL_HANDLE;
	return true;
}

bool VansGraphics::VansVKCommandBuffer::BeginSecondaryCommandBufferRecord(
	VkCommandBufferUsageFlags commandBufferUsage,
	const CommandBufferInheritanceInfo& inheritanceInfo)
{
	VkCommandBufferInheritanceInfo vkInheritanceInfo = {};
	vkInheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
	vkInheritanceInfo.renderPass = inheritanceInfo.renderPass;
	vkInheritanceInfo.subpass = inheritanceInfo.subpass;
	vkInheritanceInfo.framebuffer = inheritanceInfo.framebuffer;
	vkInheritanceInfo.occlusionQueryEnable = inheritanceInfo.occlusionQueryEnable;
	vkInheritanceInfo.queryFlags = inheritanceInfo.queryFlags;
	vkInheritanceInfo.pipelineStatistics = inheritanceInfo.pipelineStatistics;

	VkCommandBufferBeginInfo commandBufferBeginInfo =
	{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		nullptr,
		commandBufferUsage | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
		&vkInheritanceInfo
	};
	VkResult result = VansGraphics::vkBeginCommandBuffer(m_VansVKCommandBuffer, &commandBufferBeginInfo);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Could not begin secondary command buffer.");
		return false;
	}
	m_BoundGraphicsPipeline = VK_NULL_HANDLE;
	m_BoundComputePipeline = VK_NULL_HANDLE;
	m_BoundRayTracingPipeline = VK_NULL_HANDLE;
	return true;
}

bool VansGraphics::VansVKCommandBuffer::EndCommandBufferRecord()
{
	VkResult result = VansGraphics::vkEndCommandBuffer(m_VansVKCommandBuffer);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Error occurred during command buffer recording.");
		return false;
	}
	return true;
}

bool VansGraphics::VansVKCommandBuffer::ResetCommandBuffer(bool release_buffer_memory)
{
	//judge whther we shold return the memory to a pool, or if the command buffer should keep it and reuse it during the next command recording
	VkResult result = VansGraphics::vkResetCommandBuffer(m_VansVKCommandBuffer, release_buffer_memory ?
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT : 0);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Error occurred during command buffer reset.");
		return false;
	}
	m_BoundGraphicsPipeline = VK_NULL_HANDLE;
	m_BoundComputePipeline = VK_NULL_HANDLE;
	m_BoundRayTracingPipeline = VK_NULL_HANDLE;
	return true;
}

bool VansGraphics::VansVKCommandBuffer::ResetEvent(VkEvent eventHandle)
{
	VkResult result = VansGraphics::vkResetEvent(m_VansVKDevice, eventHandle);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Error occurred during event reset.");
		return false;
	}

	return true;
}

void VansGraphics::VansVKCommandBuffer::SetEvent(VkEvent eventHandle, VkPipelineStageFlags stageMask)
{
	VansGraphics::vkCmdSetEvent(m_VansVKCommandBuffer, eventHandle, stageMask);
}

void VansGraphics::VansVKCommandBuffer::WaitEvents(
	const std::vector<VkEvent>& events,
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask,
	const std::vector<VkMemoryBarrier>& memoryBarriers,
	const std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers,
	const std::vector<VkImageMemoryBarrier>& imageMemoryBarriers)
{
	VansGraphics::vkCmdWaitEvents(
		m_VansVKCommandBuffer,
		static_cast<uint32_t>(events.size()),
		events.empty() ? nullptr : events.data(),
		srcStageMask,
		dstStageMask,
		static_cast<uint32_t>(memoryBarriers.size()),
		memoryBarriers.empty() ? nullptr : memoryBarriers.data(),
		static_cast<uint32_t>(bufferMemoryBarriers.size()),
		bufferMemoryBarriers.empty() ? nullptr : bufferMemoryBarriers.data(),
		static_cast<uint32_t>(imageMemoryBarriers.size()),
		imageMemoryBarriers.empty() ? nullptr : imageMemoryBarriers.data());
}



void VansGraphics::VansVKCommandBuffer::BindVertexBuffers(uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* buffers, const VkDeviceSize* offsets)
{
	VansGraphics::vkCmdBindVertexBuffers(m_VansVKCommandBuffer, firstBinding, bindingCount, buffers, offsets);
}

void VansGraphics::VansVKCommandBuffer::BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{
	VansGraphics::vkCmdBindIndexBuffer(m_VansVKCommandBuffer, buffer, offset, indexType);
}

void VansGraphics::VansVKCommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
	VansGraphics::vkCmdDrawIndexed(m_VansVKCommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VansGraphics::VansVKCommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
	VansGraphics::vkCmdDraw(m_VansVKCommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VansGraphics::VansVKCommandBuffer::PipelineBarrier(
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask,
	const std::vector<VkMemoryBarrier>& memoryBarriers,
	const std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers,
	const std::vector<VkImageMemoryBarrier>& imageMemoryBarriers)
{
	VansGraphics::vkCmdPipelineBarrier(
		m_VansVKCommandBuffer,
		srcStageMask,
		dstStageMask,
		0,
		static_cast<uint32_t>(memoryBarriers.size()),
		memoryBarriers.empty() ? nullptr : memoryBarriers.data(),
		static_cast<uint32_t>(bufferMemoryBarriers.size()),
		bufferMemoryBarriers.empty() ? nullptr : bufferMemoryBarriers.data(),
		static_cast<uint32_t>(imageMemoryBarriers.size()),
		imageMemoryBarriers.empty() ? nullptr : imageMemoryBarriers.data());
}

bool VansGraphics::VansVKCommandBuffer::WaitForFence(VkDevice& device, const VkFence& fence)
{
	if (fence != VK_NULL_HANDLE)
	{
		VkResult result = VansGraphics::vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("vkWaitForFences failed. VkResult=" << static_cast<int>(result));
			return false;
		}

		result = VansGraphics::vkResetFences(device, 1, &fence);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("vkResetFences failed. VkResult=" << static_cast<int>(result));
			return false;
		}
		return true;
	}
	return true;
}

bool VansGraphics::VansVKCommandBuffer::SubmitCommands(VkQueue& queue, VkDevice& device, const std::vector<VkCommandBuffer>& command_buffers, const std::vector<VansGraphics::WaitSemaphoreInfo>& wait_semaphore_infos, const std::vector<VkSemaphore>& signal_semaphores, const VkFence& fence, bool wait_fence)
{
	//semaphores should be waited
	std::vector<VkSemaphore> wait_semaphore_handles;
	std::vector<VkPipelineStageFlags> wait_semaphore_stages;
	for (auto& wait_semaphore_info : wait_semaphore_infos) 
	{
		wait_semaphore_handles.emplace_back(wait_semaphore_info.semaphore);
		wait_semaphore_stages.emplace_back(wait_semaphore_info.waiting_stage);
	}

	VkSubmitInfo submit_info = 
	{
		 VK_STRUCTURE_TYPE_SUBMIT_INFO,
		 nullptr,
		 static_cast<uint32_t>(wait_semaphore_handles.size()),
		 wait_semaphore_handles.size() > 0 ? &wait_semaphore_handles[0] : nullptr,
		 wait_semaphore_stages.size() > 0 ? &wait_semaphore_stages[0] : nullptr,
		 static_cast<uint32_t>(command_buffers.size()),
		 command_buffers.data(),
		 //semaphores should be signaled once the command buffer has finished execution
		 static_cast<uint32_t>(signal_semaphores.size()),
		 signal_semaphores.size() > 0 ? &signal_semaphores[0] : nullptr
	};

	//send this fence to queue, it will be signaled when the command buffer has finished execution
	VkResult result = VansGraphics::vkQueueSubmit(queue, 1, &submit_info, fence);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Error occurred during command buffer submission. VkResult=" << static_cast<int>(result));
		return false;
	}

	if (wait_fence)
	{
		WaitForFence(device, fence);
	}

	return true;
}
