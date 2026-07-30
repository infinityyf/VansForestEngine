#pragma once

#include "VansVKCommandBuffer.h"
#include <vector>

namespace VansGraphics
{
	class VansVKDevice;

	class VansVKSecondaryCommandContext
	{
	public:
		bool Create(VansVKDevice& device, uint32_t queueFamily, uint32_t commandBufferCount);
		void Destroy(VkDevice& logicalDevice);

		bool IsReady() const { return m_Ready; }
		uint32_t GetCommandBufferCount() const { return static_cast<uint32_t>(m_CommandBuffers.size()); }
		VansVKCommandBuffer* Get(uint32_t index);
		bool ResetAll(bool releaseBufferMemory);

	private:
		std::vector<VansVKCommandBuffer> m_CommandBuffers;
		bool m_Ready = false;
	};
}
