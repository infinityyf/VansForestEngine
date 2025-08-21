#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "../../RenderCore/VulkanCore/VansVKBuffer.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VulkanCore/VansShader.h"
#include "../../RenderCore/BRDFData/VansLight.h"
#include "../../RenderCore/VansCommonUtils.h"
namespace VansGraphics 
{
	class VansLightManager;
}
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

		void UpdateGIProbe(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansLightManager* lightManager);
		
		RayTracingPushConstant m_RayTracingConstant;

	private:

		void CreateRayTraceDescriptorSets(VansVKDevice* device);

		void CreateGIPointLightDescriptorSets(VansVKDevice* device);

		//绑定数据
		void BindRayTracingData(VansVKDevice* device, VkAccelerationStructureKHR& tlas);

		void BindGIPointLightData();

		bool m_RayTracingDescriptorSetIsDirty;

		bool m_GIPointLightDescriptorSetIsDirty;

	private:

		VansTexture m_RayTracingResult;

		
		VansRayTracingShader m_VansRayTracingShader;

		VkDescriptorSetLayout m_RayTracingSetLayout;
		std::vector<VkDescriptorSet> m_RayTracingDescriptorSets;


		//GI采样点着色
		VkDescriptorSetLayout m_GISamplePositionLightSetLayout;
		std::vector<VkDescriptorSet> m_GISamplePositionLightDescriptorSets;

		//平面平铺的几个位置为中心，每个点向外发射32条光线，用斐波那契螺旋分布
		//击中点的信息保存到0，1，保存到buffer中
		//然后通过球谐积分，得到积分的结果存到result里
		int m_RayTracingPositionCount;

		int m_RayCountPerSample;

		float m_RayTracingPositionStride;

		VansVKBuffer m_RayTracingHitResult;


		VansComputeShader* m_RayTracingPointLighting;

		VansComputeShader* m_GISHUpdateShader;

		VansTexture m_SHRResult;

		VansTexture m_SHGResult;

		VansTexture m_SHBResult;

		//记录命中点的光照信息
		VansVKBuffer m_HitPointDirectLightBuffer;

		VansVKBuffer m_HitPointIndirectLightBuffer;

	};
}