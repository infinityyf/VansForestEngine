#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include "../../RenderCore/VulkanCore/VansVKBuffer.h"
#include "../../RenderCore/VulkanCore/VansShader.h"
#include "../../RenderCore/BRDFData/VansLight.h"
#include "../../RenderCore/VansCommonUtils.h"
namespace VansGraphics 
{
	class VansLightManager;
	class VansScene;
	class VansMaterialManager;
}
namespace VansGraphics
{
	class VansVKCommandBuffer;
	class VansVKDevice;
	class VansMesh;
	class VansRayTracingShader;
	class VansTexture;

	struct alignas(16) RayTracingPushConstant
	{
		glm::vec4 cameraPos;
		glm::vec4 cameraDir;
		glm::vec4 cameraUp;
		glm::vec4 cameraRight;
		glm::vec4 dispatchParams;
		glm::vec4 frameParams;
	};

	class VansRayTracing
	{
		//由于和正常shader流程差异较大，这里重新做一份shader的解析，编译和管线创建

	private:

		std::vector<VkPipelineShaderStageCreateInfo> m_RayTracingShaderStages;

	public:

		/*void BuildBottomLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansMesh* mesh);

		void BuildTopLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer);*/

		void DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene);
		
		void CreateRayTracingResource(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene);

		void UpdateGIProbe(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansLightManager* lightManager, VansMaterialManager* materialManager);
		
		RayTracingPushConstant m_RayTracingConstant;

	private:

		void CreateRayTraceDescriptorSets(VansVKDevice* device, int blasMeshCount);

		void CreateGIPointLightDescriptorSets(VansVKDevice* device);

		void CreateGISHUpdateDescriptorSets(VansVKDevice* device);

		//绑定数据
		void BindRayTracingData(VansVKDevice* device, VansScene* scene);

		void BindGIPointLightData();

		void BindGISHData(VansMaterialManager* materialManager);

		bool m_RayTracingDescriptorSetIsDirty;

		bool m_GIPointLightDescriptorSetIsDirty;

		bool m_GISHUpdateDesctiproeSetIsDirty;

	private:

		VansTexture* m_RayTracingResult;

		
		VansRayTracingShader m_VansRayTracingShader;

		VkDescriptorSetLayout m_RayTracingSetLayout;
		std::vector<VkDescriptorSet> m_RayTracingDescriptorSets;


		//GI采样点着色
		VkDescriptorSetLayout m_GISamplePositionLightSetLayout;
		std::vector<VkDescriptorSet> m_GISamplePositionLightDescriptorSets;

		//GI积分着色
		VkDescriptorSetLayout m_GISHUpdateSetLayout;
		std::vector<VkDescriptorSet> m_GISHUpdateDescriptorSets;

		//平面平铺的几个位置为中心，每个点向外发射32条光线，用斐波那契螺旋分布
		//击中点的信息保存到0，1，保存到buffer中
		//然后通过球谐积分，得到积分的结果存到result里
		int m_RayTracingPositionCount;

		int m_RayCountPerSample;

		float m_RayTracingPositionStride;

		int m_ReSTIRSampleCount;

		VansVKBuffer m_RayTracingHitPositionResult;
		VansVKBuffer m_RayTracingHitNormalResult;
		VansVKBuffer m_RayTracingHitAlbedoRoughnessResult;

		VansVKBuffer m_BLASInstanceBuffer;
		VansVKBuffer m_TLASInstanceTextureIndexBuffer;

		//记录缓存的有效方向，和对应的权重
		//HEAD 1 float 当前有效采样数量
		// //ELEMENT
		//1 float 方向索引
		//2 float 方向权重
		//3 float 方向存在时间
		VansVKBuffer m_ReSTIRBuffer;

		VansComputeShader* m_RayTracingPointLighting;

		VansComputeShader* m_GISHUpdateShader;


		//记录命中点的光照信息
		VansVKBuffer m_HitPointDirectLightBuffer;
		VansVKBuffer m_HitPointIndirectLightBuffer;

		bool m_HitPositionCalculateDone;

		int m_GIUpdateFrameIndex;
	};
}