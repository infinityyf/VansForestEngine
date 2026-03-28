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
	VkResult result = vkCreateCommandPool(m_VansVKDevice, &command_pool_create_info, nullptr, &m_VansVKCommandPool);
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
	result = vkAllocateCommandBuffers(m_VansVKDevice, &command_buffer_allocate_info, &m_VansVKCommandBuffer);
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
		vkDestroyCommandPool(logical_device, m_VansVKCommandPool, nullptr);
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
	vkCmdClearColorImage(
		m_VansVKCommandBuffer, 
		image.m_VansVKImage, 
		image.m_ImageLayout,
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
		vkCmdClearColorImage(
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
	vkCmdClearDepthStencilImage(
		m_VansVKCommandBuffer,
		image.m_VansVKImage,
		image.m_ImageLayout,
		&value,
		1,
		&image_subresource_range);
}

void VansGraphics::VansVKCommandBuffer::ClearAttachment(std::vector<VkClearAttachment>& attachments, std::vector<VkClearRect>& rests)
{
	std::vector<VkClearRect> rects;
	vkCmdClearAttachments(
		m_VansVKCommandBuffer,
		static_cast<uint32_t>(attachments.size()), 
		attachments.data(),
		static_cast<uint32_t>(rects.size()), 
		rects.data());
}


void VansGraphics::VansVKCommandBuffer::UpdatePushConstants(VansVKGraphicsPipeline& pipeline, VkShaderStageFlags flags, uint32_t offset, uint32_t size, void* data)
{
	vkCmdPushConstants(
		m_VansVKCommandBuffer,
		pipeline.m_VansPipelineLayout,
		flags,
		offset,
		size,
		data);
}

void VansGraphics::VansVKCommandBuffer::SetViewport(uint32_t first_viewport, const std::vector<VkViewport>& viewports)
{
	//first_viewport记录开启的索引offset
	vkCmdSetViewport(
		m_VansVKCommandBuffer,
		first_viewport,
		static_cast<uint32_t>(viewports.size()), 
		viewports.data());
}

void VansGraphics::VansVKCommandBuffer::SetScissor(uint32_t first_scissor, const std::vector<VkRect2D>& scissors)
{
	vkCmdSetScissor(
		m_VansVKCommandBuffer,
		first_scissor,
		static_cast<uint32_t>(scissors.size()), 
		scissors.data());
}

void VansGraphics::VansVKCommandBuffer::SetLineWidth(float line_width)
{
	vkCmdSetLineWidth(m_VansVKCommandBuffer, line_width);
}

void VansGraphics::VansVKCommandBuffer::SetDepthBias(float constant_factor, float clamp, float slope_factor)
{
	//clamp:specify the maximal or minimal value of the depth bias
	//slope_factor is a scalar factor applied to a fragment’s slope in depth bias calculations.
	vkCmdSetDepthBias(m_VansVKCommandBuffer, constant_factor, clamp, slope_factor);
}

void VansGraphics::VansVKCommandBuffer::SetBlendConstants(float blend_constants[4])
{
	vkCmdSetBlendConstants(m_VansVKCommandBuffer, blend_constants);
}

void VansGraphics::VansVKCommandBuffer::DrawMesh(VansMesh& mesh, VansGraphicsShader& shader, uint32_t instance_count)
{
	BindGraphicsPipeline(*shader.GetGraphicsPipeline());
	vkCmdDrawIndexed(
		m_VansVKCommandBuffer,
		mesh.GetIndexCount(),
		instance_count, 
		0,
		0, 
		0);
}

void VansGraphics::VansVKCommandBuffer::DrawIndexedIndirect(VkBuffer buffer, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
	vkCmdDrawIndexedIndirect(m_VansVKCommandBuffer, buffer, offset, drawCount, stride);
}

void VansGraphics::VansVKCommandBuffer::ExecuteSecondaryCommandBuffer(std::vector<VkCommandBuffer>& secondary_command_buffers)
{
	vkCmdExecuteCommands(
		m_VansVKCommandBuffer,
		static_cast<uint32_t>(secondary_command_buffers.size()),
		secondary_command_buffers.data());
}

void VansGraphics::VansVKCommandBuffer::BindMesh(VansMesh& mesh, uint32_t fist_bind, GlobalStateData& global_state_data)
{
	VertexBufferParameters vparam = mesh.GetVertexBufferParameter();
	vkCmdBindVertexBuffers(
		m_VansVKCommandBuffer,
		fist_bind,
		1,
		&vparam.Buffer,
		&vparam.MemoryOffset);

	IndexBufferParameters iparam = mesh.GetIndexBufferParameter();
	vkCmdBindIndexBuffer(
		m_VansVKCommandBuffer,
		iparam.Buffer,
		iparam.MemoryOffset,
		iparam.IndexType);

	//记录mesh 的bind data
	global_state_data.vertexInputAttributeDescriptions = &mesh.m_VertexInputAttributeDescriptions;
	global_state_data.vertexInputBindingDescriptions = &mesh.m_VertexInputBindingDescriptions;

}

void VansGraphics::VansVKCommandBuffer::BuildAccelerationStructures(VkAccelerationStructureBuildGeometryInfoKHR* buildInfo, const VkAccelerationStructureBuildRangeInfoKHR* rangeInfo)
{
	vkCmdBuildAccelerationStructuresKHR(m_VansVKCommandBuffer, 1, buildInfo, &rangeInfo);
}

void VansGraphics::VansVKCommandBuffer::EnsureGraphicsShader(VansGraphicsShader& shader, GlobalStateData& global_state_data, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	VansVKGraphicsPipeline* pipeline = shader.GetGraphicsPipeline(m_VansVKDevice, global_state_data, descriptorset_layouts);
	if (pipeline == nullptr)
	{
		VANS_LOG_ERROR("pipe get failed");
		return;
	}
	//BindGraphicsPipeline(*pipeline);

	//根据shader声明和材质绑定的数据进行绑定
	//BindDescriptorSets();
}

void VansGraphics::VansVKCommandBuffer::EnsureComputeShader(VansComputeShader& shader, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	//检查compute shader是否修改

	VansVKComputePipeline* pipeline = shader.GetComputePipeline(m_VansVKDevice, descriptorset_layouts);
	if (pipeline == nullptr)
	{
		VANS_LOG_ERROR("compute pipe get failed");
		return;
	}
}

void VansGraphics::VansVKCommandBuffer::DispatchCompute(VansComputeShader& shader, uint32_t x_size, uint32_t y_size, uint32_t z_size, const std::vector<VkDescriptorSet>& descriptor_sets)
{
	VansVKComputePipeline* pipeline = shader.GetComputePipeline();
	//绑定管线
	pipeline->BindComputePipeline(m_VansVKCommandBuffer);

	//绑定描述符
	vkCmdBindDescriptorSets(
		m_VansVKCommandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline->m_VansPipelineLayout,
		0,
		static_cast<uint32_t>(descriptor_sets.size()),
		descriptor_sets.data(),
		0,
		nullptr);

	int pushConstantSize = shader.GetPushConstantSize();
	void* pushConstantData = shader.GetPushConstantData();
	if (pushConstantSize > 0 && pushConstantData != nullptr)
	{
		vkCmdPushConstants(m_VansVKCommandBuffer, pipeline->m_VansPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
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

	vkCmdCopyImage(
		m_VansVKCommandBuffer,
		source.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		target.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &copyRegion
	);

	//结束后转换回来
	source.SetImageMemoryBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
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

void VansGraphics::VansVKCommandBuffer::BindDescriptorSets(
	VkPipelineBindPoint pipeline_type, 
	VansGraphicsShader& shader,
	int index_for_first_set,
	const std::vector<VkDescriptorSet>& descriptor_sets, 
	const std::vector<uint32_t>& dynamic_offsets)
{
	//将关联好的descriptor set 绑定到 pipeline
	//通过bindSetCMD实现
	vkCmdBindDescriptorSets(
		m_VansVKCommandBuffer,
		pipeline_type,
		shader.GetGraphicsPipeline()->m_VansPipelineLayout,
		index_for_first_set,
		static_cast<uint32_t>(descriptor_sets.size()),
		descriptor_sets.data(),
		static_cast<uint32_t>(dynamic_offsets.size()),
		dynamic_offsets.data());
}

void VansGraphics::VansVKCommandBuffer::BindGraphicsPipeline(VansVKGraphicsPipeline& graphicsPipeline)
{
	if (VansVKGraphicsPipeline::CurrentValidGraphicsPipeline == graphicsPipeline.m_GraphicsPipeline)
	{
		return;
	}
	else
	{
		VansVKGraphicsPipeline::CurrentValidGraphicsPipeline = graphicsPipeline.m_GraphicsPipeline;
	}

	graphicsPipeline.BindGraphicsPipeline(m_VansVKCommandBuffer);
}

bool VansGraphics::VansVKCommandBuffer::BeginCommandBufferRecord(VkCommandBufferUsageFlagBits commandBufferUsage)
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
	VkResult result = vkBeginCommandBuffer(m_VansVKCommandBuffer, &command_buffer_begin_info);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Could not begin command buffer.");
		return false;
	}
	return true;
}

bool VansGraphics::VansVKCommandBuffer::EndCommandBufferRecord()
{
	VkResult result = vkEndCommandBuffer(m_VansVKCommandBuffer);
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
	VkResult result = vkResetCommandBuffer(m_VansVKCommandBuffer, release_buffer_memory ?
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT : 0);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Error occurred during command buffer reset.");
		return false;
	}
	return true;
}

bool VansGraphics::VansVKCommandBuffer::ResetEvent(VkEvent eventHandle)
{
	VkResult result = vkResetEvent(m_VansVKDevice, eventHandle);
	if (VK_SUCCESS != result)
	{
		VANS_LOG_ERROR("Error occurred during event reset.");
		return false;
	}

	return true;
}

void VansGraphics::VansVKCommandBuffer::SetEvent(VkEvent eventHandle, VkPipelineStageFlags stageMask)
{
	vkCmdSetEvent(m_VansVKCommandBuffer, eventHandle, stageMask);
}

void VansGraphics::VansVKCommandBuffer::WaitEvents(
	const std::vector<VkEvent>& events,
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask,
	const std::vector<VkMemoryBarrier>& memoryBarriers,
	const std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers,
	const std::vector<VkImageMemoryBarrier>& imageMemoryBarriers)
{
	vkCmdWaitEvents(
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

// ── Standalone draw / bind helpers ──────────────────────────────────────

void VansGraphics::VansVKCommandBuffer::BindIndexBuffer(VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType)
{
	vkCmdBindIndexBuffer(m_VansVKCommandBuffer, buffer, offset, indexType);
}

void VansGraphics::VansVKCommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
	vkCmdDrawIndexed(m_VansVKCommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VansGraphics::VansVKCommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
	vkCmdDraw(m_VansVKCommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VansGraphics::VansVKCommandBuffer::PipelineBarrier(
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask,
	const std::vector<VkMemoryBarrier>& memoryBarriers,
	const std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers,
	const std::vector<VkImageMemoryBarrier>& imageMemoryBarriers)
{
	vkCmdPipelineBarrier(
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
		bool result = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &fence);
		return result;
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
	VkResult result = vkQueueSubmit(queue, 1, &submit_info, fence);
	if (VK_SUCCESS != result) 
	{
		VANS_LOG_ERROR("Error occurred during command buffer submission.");
		return false;
	}

	if (wait_fence)
	{
		WaitForFence(device, fence);
	}

	return true;
}

void VansGraphics::VansMultiThreadCommandBufferMangaer::InitCommandRecordThreads(std::vector<VansGraphics::VansVKCommandBuffer>& vans_command_buffers)
{
	m_CommandBufferRecordingThreadParameters.clear();
	m_CommandBufferRecordingThreadParameters.reserve(vans_command_buffers.size());
	for (size_t i = 0; i < vans_command_buffers.size(); ++i)
	{
		m_CommandBufferRecordingThreadParameters.push_back(
		{
			vans_command_buffers[i].GetVKCommandBuffer(),
			NULL,
		});
	}
	assert(m_CommandBufferRecordingThreadParameters.size() > 0);
	m_CommandBufferRecordingThreads.resize(m_CommandBufferRecordingThreadParameters.size());
	for (size_t i = 0; i < m_CommandBufferRecordingThreadParameters.size(); ++i)
	{
		m_CommandBufferRecordingThreads[i] = std::thread::thread(
			m_CommandBufferRecordingThreadParameters[i].RecordingFunction,
			m_CommandBufferRecordingThreadParameters[i].CommandBuffer);
	}
}

void VansGraphics::VansMultiThreadCommandBufferMangaer::SubmitMultiCommands(VkQueue& queue, VkDevice& device, const std::vector<WaitSemaphoreInfo>& wait_semaphore_infos, const std::vector<VkSemaphore>& signal_semaphores, VkFence& fence)
{
	std::vector<VkCommandBuffer> command_buffers(m_CommandBufferRecordingThreadParameters.size());
	for (size_t i = 0; i < m_CommandBufferRecordingThreadParameters.size(); ++i)
	{
		m_CommandBufferRecordingThreads[i].join();
		command_buffers[i] = m_CommandBufferRecordingThreadParameters[i].CommandBuffer;
	}
	//submit只能从一个线程，所以需要所有record都join后才能submit
	//VansVKCommandBuffer::SubmitCommands(queue, device, command_buffers, wait_semaphore_infos, signal_semaphores, command_buffer.m_CommandBufferFinishSubmitFence);
}
