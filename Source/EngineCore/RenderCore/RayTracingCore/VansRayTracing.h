#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "../../RenderCore/VulkanCore/VansVKBuffer.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VulkanCore/VansShader.h"
#include "../../RenderCore/VansCommonUtils.h"
namespace VansVulkan
{
	class VansVKCommandBuffer;
	class VansVKDevice;
	class VansMesh;
	class VansRayTracingShader;

	struct alignas(16) RayTracingPushConstant
	{
		glm::vec4 cameraPos;
		glm::vec4 cameraDir;
		glm::vec4 cameraUp;
		glm::vec4 cameraRight;
		glm::vec4 dispatchParams;
	};


	class VansRayTracing
	{
		//由于和正常shader流程差异较大，这里重新做一份shader的解析，编译和管线创建

	private:

		std::vector<VkPipelineShaderStageCreateInfo> m_RayTracingShaderStages;

	public:

		/*void BuildBottomLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansMesh* mesh);

		void BuildTopLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer);*/

		void DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VkAccelerationStructureKHR& tlas);
		
		void CreateRayTracingResource(VansVKDevice* device, VansVKCommandBuffer* commandBuffer);

		void CreateDescriptorSets(VansVKDevice* device);

		RayTracingPushConstant m_RayTracingConstant;

	private:
		//绑定数据
		void BindRayTracingData(VansVKDevice* device, VkAccelerationStructureKHR& tlas);

		bool m_DescriptorSetIsDirty;

	private:
		//// 加速结构
		//VkAccelerationStructureKHR m_BottomLevelAS;

		//VansVKBuffer m_BottomLevelASBuffer;

		//VkAccelerationStructureKHR m_TopLevelAS;

		//VansVKBuffer m_TopLevelASBuffer;

		//VansVKBuffer m_InstanceBuffer;

		VansTexture m_RayTracingResult;

		
		VansRayTracingShader m_VansRayTracingShader;

		VkDescriptorSetLayout m_RayTracingSetLayout;
		std::vector<VkDescriptorSet> m_RayTracingDescriptorSets;

		//平面平铺的几个位置为中心，每个点向外发射32条光线，用斐波那契螺旋分布
		//击中点的信息保存到0，1，保存到buffer中
		//然后通过球谐积分，得到积分的结果存到result里
		int m_RayTracingPositionCount;

		int m_RayCountPerSample;

		float m_RayTracingPositionStride;

		VansVKBuffer m_RayTracingHitResult;

	};
}