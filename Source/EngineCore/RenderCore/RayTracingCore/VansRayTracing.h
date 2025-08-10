#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "../../RenderCore/VulkanCore/VansVKBuffer.h"
namespace VansVulkan
{
	class VansVKCommandBuffer;
	class VansVKDevice;
	class VansMesh;

	class VansRayTracing
	{
	public:

		void BuildBottomLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansMesh* mesh);

		void BuildTopLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer);

	private:
		// 加速结构
		VkAccelerationStructureKHR m_BottomLevelAS;

		VansVKBuffer m_BottomLevelASBuffer;

		VkAccelerationStructureKHR m_TopLevelAS;

		VansVKBuffer m_TopLevelASBuffer;

		VansVKBuffer m_InstanceBuffer;
	};
}