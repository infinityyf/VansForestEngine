#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <cstddef>
#include "../../RenderCore/VulkanCore/VansVKBuffer.h"
#include "../../RenderCore/VulkanCore/VansShader.h"
#include "../../RenderCore/BRDFData/VansLight.h"
#include "../../ScriptCore/VansCommonUtils.h"
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
	struct VansGISettings;

	struct alignas(16) RayTracingPushConstant
	{
		glm::vec4 gridParams;
		glm::vec4 cameraDir;
		glm::vec4 cameraUp;
		glm::vec4 cameraRight;
		glm::vec4 dispatchParams;
		glm::vec4 frameParams;
		glm::vec4 regionParams;
		glm::vec4 lightingParams;
	};
	static_assert(sizeof(RayTracingPushConstant) == 128, "GI push constant layout must match GLSL");
	static_assert(alignof(RayTracingPushConstant) == 16, "GI push constant alignment must match GLSL vec4");
	static_assert(offsetof(RayTracingPushConstant, gridParams) == 0, "GI grid parameters must occupy GLSL slot 0");
	static_assert(offsetof(RayTracingPushConstant, dispatchParams) == 64, "GI dispatch parameters must occupy GLSL slot 4");
	static_assert(offsetof(RayTracingPushConstant, lightingParams) == 112, "GI lighting parameters must occupy GLSL slot 7");

	struct alignas(16) GIRTPreviewPushConstant
	{
		glm::vec4 gridParams;       // xyz = grid dimensions, w = rays per probe
		glm::vec4 selectionParams;  // x = mode, y = z slice, z = ray index, w = exposure
		glm::vec4 displayParams;    // x = signed world-position scale
	};
	static_assert(sizeof(GIRTPreviewPushConstant) == 48, "GI RT preview push constant layout must match GLSL");

	class VansRayTracing
	{
		//由于和正常shader流程差异较大，这里重新做一份shader的解析，编译和管线创建

	private:

		std::vector<VkPipelineShaderStageCreateInfo> m_RayTracingShaderStages;

	public:

		void DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene);
		
		void CreateRayTracingResource(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene);

		// 场景切换时清理与当前场景绑定的 RT 资源（descriptor set、buffer 等）
		// BLAS 由 mesh 管理，不在此处释放。
		void CleanupSceneResources(VkDevice device);

		void UpdateGIProbe(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansLightManager* lightManager, VansMaterialManager* materialManager);

		void UpdateGISettings(const VansGISettings& settings);

		bool IsReady() const { return m_RTResourcesReady; }

		void RequestGIRTPreview(uint32_t mode, uint32_t zSlice, uint32_t rayIndex, float exposure, float positionScale);
		VansTexture* GetGIRTPreviewTexture() const { return m_GIRTPreviewTexture; }
		
		RayTracingPushConstant m_RayTracingConstant;

	private:

		void CreateRayTraceDescriptorSets(VansVKDevice* device, int blasMeshCount);

		void CreateGIPointLightDescriptorSets(VansVKDevice* device);

		void CreateGISHUpdateDescriptorSets(VansVKDevice* device);

		void CreateGIVisibilityUpdateDescriptorSets(VansVKDevice* device);

		void CreateGIRTPreviewDescriptorSets(VansVKDevice* device);


		//绑定数据
		void BindRayTracingData(VansVKDevice* device, VansScene* scene);

		void BindGIPointLightData();

		void BindGISHData(VansMaterialManager* materialManager);

		void BindGIVisibilityData(VansMaterialManager* materialManager);

		void BindGIRTPreviewData(VansMaterialManager* materialManager);

		void DispatchGIRTPreview(VansVKCommandBuffer* commandBuffer, VansMaterialManager* materialManager);

		void CopyCurrentSHToFeedback(VansVKCommandBuffer* commandBuffer, VansMaterialManager* materialManager);

		bool UpdateLightingResponseState(VansLightManager* lightManager);


		bool m_RayTracingDescriptorSetIsDirty = true;

		bool m_GIPointLightDescriptorSetIsDirty = true;

		bool m_GISHUpdateDesctiproeSetIsDirty = true;

		bool m_GIVisibilityDescriptorSetIsDirty = true;


	private:

		VansTexture* m_RayTracingResult = nullptr;

		VansTexture* m_SHFeedbackR = nullptr;
		VansTexture* m_SHFeedbackG = nullptr;
		VansTexture* m_SHFeedbackB = nullptr;
		VansTexture* m_GIRTPreviewTexture = nullptr;

		
		VansRayTracingShader m_VansRayTracingShader;

		VkDescriptorSetLayout m_RayTracingSetLayout;
		std::vector<VkDescriptorSet> m_RayTracingDescriptorSets;


		//GI采样点着色
		VkDescriptorSetLayout m_GISamplePositionLightSetLayout;
		std::vector<VkDescriptorSet> m_GISamplePositionLightDescriptorSets;

		//GI积分着色
		VkDescriptorSetLayout m_GISHUpdateSetLayout;
		std::vector<VkDescriptorSet> m_GISHUpdateDescriptorSets;

		VkDescriptorSetLayout m_GIVisibilityUpdateSetLayout;
		std::vector<VkDescriptorSet> m_GIVisibilityUpdateDescriptorSets;

		VkDescriptorSetLayout m_GIRTPreviewSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GIRTPreviewDescriptorSets;

		//平面平铺的几个位置为中心，每个点向外发射32条光线，用斐波那契螺旋分布
		//击中点的信息保存到0，1，保存到buffer中
		//然后通过球谐积分，得到积分的结果存到result里
		glm::uvec3 m_RayTracingGridDimensions = glm::uvec3(1u);

		int m_RayCountPerSample;

		glm::vec3 m_RayTracingProbeSpacing = glm::vec3(0.5f);

		VansVKBuffer m_RayTracingHitPositionResult;
		VansVKBuffer m_RayTracingHitNormalResult;
		VansVKBuffer m_RayTracingHitAlbedoRoughnessResult;

		VansVKBuffer m_BLASInstanceBuffer;
		VansVKBuffer m_TLASInstanceTextureIndexBuffer;

		VansComputeShader* m_RayTracingPointLighting = nullptr;

		VansComputeShader* m_GISHUpdateShader = nullptr;

		VansComputeShader* m_GIVisibilityUpdateShader = nullptr;

		VansComputeShader* m_GIRTPreviewShader = nullptr;
		GIRTPreviewPushConstant m_GIRTPreviewConstant{};
		uint32_t m_GIRTPreviewRequestFrames = 0;
		bool m_GIRTPreviewDescriptorSetIsDirty = true;
		uint32_t m_GIRTPreviewBoundZSlice = 0xffffffffu;
		VkDeviceSize m_GIRTPreviewStorageBufferAlignment = 1;

		//GI 可见度计算

		//记录命中点的光照信息
		VansVKBuffer m_HitPointLightBuffer;

		bool m_HitPositionCalculateDone = false;

		bool m_GIVisibilityCalculateDone = false;

		int m_GIUpdateFrameIndex;

		glm::vec4 m_LastGIMainLightDirectionIntensity = glm::vec4(0.0f);
		glm::vec4 m_LastGIMainLightColor = glm::vec4(0.0f);
		bool m_HasLastGIMainLight = false;
		uint32_t m_GILightingResponseFramesRemaining = 0;

		// True after CreateRayTracingResource succeeds (scene has RT geometry).
		bool m_RTResourcesReady = false;
	};
}
