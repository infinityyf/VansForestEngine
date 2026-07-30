#include "VansVKSecondaryCommandContext.h"
#include "VansVKDevice.h"
#include "../../Util/VansLog.h"

namespace VansGraphics
{
	bool VansVKSecondaryCommandContext::Create(VansVKDevice& device, uint32_t queueFamily, uint32_t commandBufferCount)
	{
		Destroy(device.GetLogicDevice());
		if (commandBufferCount == 0)
			return false;

		m_CommandBuffers.resize(commandBufferCount);
		CommandBufferCreateParams params = {};
		params.pool_params = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		params.commandbuffer_level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
		params.commandbuffer_count = 1;

		for (VansVKCommandBuffer& commandBuffer : m_CommandBuffers)
		{
			if (!commandBuffer.CreateVulkanCommandBuffer(device, queueFamily, params))
			{
				VANS_LOG_ERROR("[VansVKSecondaryCommandContext] Failed to create secondary command buffer.");
				Destroy(device.GetLogicDevice());
				return false;
			}
		}

		m_Ready = true;
		return true;
	}

	void VansVKSecondaryCommandContext::Destroy(VkDevice& logicalDevice)
	{
		for (VansVKCommandBuffer& commandBuffer : m_CommandBuffers)
		{
			commandBuffer.DestroyVulkanCommandBuffer(logicalDevice);
		}
		m_CommandBuffers.clear();
		m_Ready = false;
	}

	VansVKCommandBuffer* VansVKSecondaryCommandContext::Get(uint32_t index)
	{
		if (index >= m_CommandBuffers.size())
			return nullptr;
		return &m_CommandBuffers[index];
	}

	bool VansVKSecondaryCommandContext::ResetAll(bool releaseBufferMemory)
	{
		bool result = true;
		for (VansVKCommandBuffer& commandBuffer : m_CommandBuffers)
		{
			result = commandBuffer.ResetCommandBuffer(releaseBufferMemory) && result;
		}
		return result;
	}
}
