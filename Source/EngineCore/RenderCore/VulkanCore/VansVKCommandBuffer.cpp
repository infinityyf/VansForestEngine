#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKCommandBuffer.h"
#include "VansVKBuffer.h"
#include "VansVKImage.h"
#include "VansMesh.h"
#include "VansShader.h"
#include "VansRenderPass.h"
#include "VansPipeline.h"
#include "VansVKDevice.h"
#include <iostream>
#include <cassert>

bool VansVulkan::VansVKCommandBuffer::CreateVulkanCommandBuffer(VansVKDevice& device ,uint32_t queue_family, CommandBufferCreateParams& buffer_create_info)
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
		std::cout << "Could not create command pool." << std::endl;
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
		std::cout << "Could not allocate command buffers." << std::endl;
		return false;
	}

	return true;
}


void VansVulkan::VansVKCommandBuffer::DestroyVulkanCommandBuffer(VkDevice& logical_device)
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

void VansVulkan::VansVKCommandBuffer::ClearColor(VansVKImage& image, const VkClearColorValue& value)
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

void VansVulkan::VansVKCommandBuffer::ClearMRTColor(const std::vector<VansVKImage>& images, const std::vector<VkClearColorValue>& values)
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

void VansVulkan::VansVKCommandBuffer::ClearDepthStencil(VansVKImage& image, const VkClearDepthStencilValue& value)
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

void VansVulkan::VansVKCommandBuffer::ClearAttachment(std::vector<VkClearAttachment>& attachments, std::vector<VkClearRect>& rests)
{
	std::vector<VkClearRect> rects;
	vkCmdClearAttachments(
		m_VansVKCommandBuffer,
		static_cast<uint32_t>(attachments.size()), 
		attachments.data(),
		static_cast<uint32_t>(rects.size()), 
		rects.data());
}


void VansVulkan::VansVKCommandBuffer::UpdatePushConstants(VansVKGraphicsPipeline& pipeline, VkShaderStageFlags flags, uint32_t offset, uint32_t size, void* data)
{
	vkCmdPushConstants(
		m_VansVKCommandBuffer,
		pipeline.m_VansPipelineLayout,
		flags,
		offset,
		size,
		data);
}

void VansVulkan::VansVKCommandBuffer::SetViewport(uint32_t first_viewport, const std::vector<VkViewport>& viewports)
{
	//first_viewport记录开启的索引offset
	vkCmdSetViewport(
		m_VansVKCommandBuffer,
		first_viewport,
		static_cast<uint32_t>(viewports.size()), 
		viewports.data());
}

void VansVulkan::VansVKCommandBuffer::SetScissor(uint32_t first_scissor, const std::vector<VkRect2D>& scissors)
{
	vkCmdSetScissor(
		m_VansVKCommandBuffer,
		first_scissor,
		static_cast<uint32_t>(scissors.size()), 
		scissors.data());
}

void VansVulkan::VansVKCommandBuffer::SetLineWidth(float line_width)
{
	vkCmdSetLineWidth(m_VansVKCommandBuffer, line_width);
}

void VansVulkan::VansVKCommandBuffer::SetDepthBias(float constant_factor, float clamp, float slope_factor)
{
	//clamp:specify the maximal or minimal value of the depth bias
	//slope_factor is a scalar factor applied to a fragment’s slope in depth bias calculations.
	vkCmdSetDepthBias(m_VansVKCommandBuffer, constant_factor, clamp, slope_factor);
}

void VansVulkan::VansVKCommandBuffer::SetBlendConstants(float blend_constants[4])
{
	vkCmdSetBlendConstants(m_VansVKCommandBuffer, blend_constants);
}

void VansVulkan::VansVKCommandBuffer::DrawMesh(VansMesh& mesh, VansGraphicsShader& shader, uint32_t instance_count)
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

void VansVulkan::VansVKCommandBuffer::ExecuteSecondaryCommandBuffer(std::vector<VkCommandBuffer>& secondary_command_buffers)
{
	vkCmdExecuteCommands(
		m_VansVKCommandBuffer,
		static_cast<uint32_t>(secondary_command_buffers.size()),
		secondary_command_buffers.data());
}

void VansVulkan::VansVKCommandBuffer::BindMesh(VansMesh& mesh, uint32_t fist_bind, GlobalStateData& global_state_data)
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
	global_state_data.vertexInputBindingDescription = &mesh.m_VertexInputBindingDescription;

}

void VansVulkan::VansVKCommandBuffer::EnsureGraphicsShader(VansGraphicsShader& shader, GlobalStateData& global_state_data, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	VansVKGraphicsPipeline* pipeline = shader.GetGraphicsPipeline(m_VansVKDevice, global_state_data, descriptorset_layouts);
	if (pipeline == nullptr)
	{
		std::cout << "pipe get failed" << std::endl;
		return;
	}
	//BindGraphicsPipeline(*pipeline);

	//根据shader声明和材质绑定的数据进行绑定
	//BindDescriptorSets();
}

void VansVulkan::VansVKCommandBuffer::EnsureComputeShader(VansComputeShader& shader, const std::vector<VkDescriptorSetLayout>& descriptorset_layouts)
{
	VansVKComputePipeline* pipeline = shader.GetComputePipeline(m_VansVKDevice, descriptorset_layouts);
	if (pipeline == nullptr)
	{
		std::cout << "compute pipe get failed" << std::endl;
		return;
	}
}

void VansVulkan::VansVKCommandBuffer::DispatchCompute(VansComputeShader& shader, uint32_t x_size, uint32_t y_size, uint32_t z_size, const std::vector<VkDescriptorSet>& descriptor_sets)
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

	pipeline->DispatchCompute(m_VansVKCommandBuffer, x_size, y_size, z_size);
}

void VansVulkan::VansVKCommandBuffer::BindDescriptorSets(
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

void VansVulkan::VansVKCommandBuffer::BindGraphicsPipeline(VansVKGraphicsPipeline& graphicsPipeline)
{
	graphicsPipeline.BindGraphicsPipeline(m_VansVKCommandBuffer);
}

bool VansVulkan::VansVKCommandBuffer::BeginCommandBufferRecord(VkCommandBufferUsageFlagBits commandBufferUsage)
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
		std::cout << "Could not begin command buffer." << std::endl;
		return false;
	}
	return true;
}

bool VansVulkan::VansVKCommandBuffer::EndCommandBufferRecord()
{
	VkResult result = vkEndCommandBuffer(m_VansVKCommandBuffer);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Error occurred during command buffer recording." << std::endl;
		return false;
	}
	return true;
}

bool VansVulkan::VansVKCommandBuffer::ResetCommandBuffer(bool release_buffer_memory)
{
	//judge whther we shold return the memory to a pool, or if the command buffer should keep it and reuse it during the next command recording
	VkResult result = vkResetCommandBuffer(m_VansVKCommandBuffer, release_buffer_memory ?
		VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT : 0);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Error occurred during command buffer reset." << std::endl;
		return false;
	}
	return true;
}
VkFence VansVulkan::VansVKCommandBuffer::m_CommandBufferFinishSubmitFence = VK_NULL_HANDLE;

bool VansVulkan::VansVKCommandBuffer::SubmitCommands(VkQueue& queue, VkDevice& device, const std::vector<VkCommandBuffer>& command_buffers, const std::vector<VansVulkan::WaitSemaphoreInfo>& wait_semaphore_infos, const std::vector<VkSemaphore>& signal_semaphores)
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
	VkResult result = vkQueueSubmit(queue, 1, &submit_info, m_CommandBufferFinishSubmitFence);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Error occurred during command buffer submission." << std::endl;
		return false;
	}

	if (m_CommandBufferFinishSubmitFence != VK_NULL_HANDLE)
	{
		vkWaitForFences(device, 1, &m_CommandBufferFinishSubmitFence, VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &m_CommandBufferFinishSubmitFence);
	}

	return true;
}

void VansVulkan::VansMultiThreadCommandBufferMangaer::InitCommandRecordThreads(std::vector<VansVulkan::VansVKCommandBuffer>& vans_command_buffers)
{
	m_CommandBufferRecordingThreadParameters.reserve(vans_command_buffers.size());
	for (size_t i = 0; i < vans_command_buffers.size(); ++i)
	{
		m_CommandBufferRecordingThreadParameters[i] =
		{
			vans_command_buffers[i].GetVKCommandBuffer(),
			NULL,
		};
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

void VansVulkan::VansMultiThreadCommandBufferMangaer::SubmitMultiCommands(VkQueue& queue, VkDevice& device, const std::vector<WaitSemaphoreInfo>& wait_semaphore_infos, const std::vector<VkSemaphore>& signal_semaphores, VkFence& fence)
{
	std::vector<VkCommandBuffer> command_buffers(m_CommandBufferRecordingThreadParameters.size());
	for (size_t i = 0; i < m_CommandBufferRecordingThreadParameters.size(); ++i)
	{
		m_CommandBufferRecordingThreads[i].join();
		command_buffers[i] = m_CommandBufferRecordingThreadParameters[i].CommandBuffer;
	}
	//submit只能从一个线程，所以需要所有record都join后才能submit
	VansVKCommandBuffer::SubmitCommands(queue, device, command_buffers, wait_semaphore_infos, signal_semaphores);

}
