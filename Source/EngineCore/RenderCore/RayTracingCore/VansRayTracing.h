#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include <cstddef>
#include "../../RenderCore/VulkanCore/VansVKBuffer.h"
#include "../../RenderCore/VulkanCore/VansShader.h"
#include "../../RenderCore/BRDFData/VansLight.h"
#include "../../RenderCore/GICore/VansGISettings.h"
#include "../../ScriptCore/VansCommonUtils.h"
namespace VansGraphics
{
	inline constexpr uint32_t GIRTPreviewModeCount = 12u;

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
		glm::vec4 gridParams;
		glm::vec4 dispatchParams;
		glm::vec4 frameParams;
		glm::vec4 regionParams;
		glm::vec4 lightingParams;
		glm::vec4 temporalParams;
	};
	static_assert(sizeof(RayTracingPushConstant) == 96, "GI push constant layout must match GLSL");
	static_assert(alignof(RayTracingPushConstant) == 16, "GI push constant alignment must match GLSL vec4");
	static_assert(offsetof(RayTracingPushConstant, gridParams) == 0, "GI grid parameters must occupy GLSL slot 0");
	static_assert(offsetof(RayTracingPushConstant, dispatchParams) == 16, "GI dispatch parameters must occupy GLSL slot 1");
	static_assert(offsetof(RayTracingPushConstant, lightingParams) == 64, "GI lighting parameters must occupy GLSL slot 4");
	static_assert(offsetof(RayTracingPushConstant, temporalParams) == 80, "GI temporal parameters must occupy GLSL slot 5");

	struct alignas(16) GIRTPreviewPushConstant
	{
		glm::vec4 gridParams;       // xyz = grid dimensions, w = rays per probe
		glm::vec4 selectionParams;  // x = mode, y = z slice, z = ray index, w = exposure
		glm::vec4 displayParams;    // x = signed world-position scale
		glm::vec4 updateParams;     // x = update frame, y = spatial divisor, z = direction slices, w = rays in active slice
	};
	static_assert(sizeof(GIRTPreviewPushConstant) == 64, "GI RT preview push constant layout must match GLSL");

	class VansRayTracing
	{
		//由于和正常shader流程差异较大，这里重新做一份shader的解析，编译和管线创建

	private:

		std::vector<VkPipelineShaderStageCreateInfo> m_RayTracingShaderStages;

	public:

		void PrepareGIProbeUpdate(VansLightManager* lightManager, VansMaterialManager* materialManager);

		void DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene);
		
		void CreateRayTracingResource(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene);

		// 场景切换时清理与当前场景绑定的 RT 资源（descriptor set、buffer 等）
		// BLAS 由 mesh 管理，不在此处释放。
		void CleanupSceneResources(VkDevice device, VansMaterialManager* materialManager = nullptr);

		void UpdateGIProbe(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansLightManager* lightManager, VansMaterialManager* materialManager);

		void UpdateGISettings(const VansGISettings& settings);

		bool IsReady() const { return m_RTResourcesReady; }

		void RequestGIRTPreviews(uint32_t zSlice, uint32_t rayIndex, float exposure, float positionScale);
		VansTexture* GetGIRTPreviewTexture(uint32_t mode) const
		{
			return m_GIRTPreviewTextures[std::min(mode, GIRTPreviewModeCount - 1u)];
		}
		uint32_t GetGIRegionCount() const { return static_cast<uint32_t>(m_GIRegions.size()); }
		VansTexture* GetGIRegionIrradianceAtlas(uint32_t regionIndex) const;
		VansTexture* GetGIRegionVisibilityAtlas(uint32_t regionIndex) const;
		const VansVKBuffer* GetGIRegionProbeStateBuffer(uint32_t regionIndex) const;
		
	private:
		struct GIRegionRuntime
		{
			GIResolvedRegion resolved;
			RayTracingPushConstant constants{};

			VansTexture* rayTracingResult = nullptr;
			VansTexture* irradianceAtlas = nullptr;
			VansTexture* visibilityAtlas = nullptr;

			VansVKBuffer hitPositionResult;
			VansVKBuffer hitNormalResult;
			VansVKBuffer hitAlbedoRoughnessResult;
			VansVKBuffer hitEmissionResult;
			VansVKBuffer hitRadianceBuffer;
			VansVKBuffer probeStateBuffer;

			bool rayTracingDescriptorSetIsDirty = true;
			bool giPointLightDescriptorSetIsDirty = true;
			bool giVisibilityDescriptorSetIsDirty = true;
			bool giProbeStateDescriptorSetIsDirty = true;
			uint32_t giUpdateFrameIndex = 0;
			uint32_t lightingResetFramesRemaining = 0;
		};

		void CreateRayTraceDescriptorSets(VansVKDevice* device, int blasMeshCount);

		void CreateGIPointLightDescriptorSets(VansVKDevice* device);

		void CreateGIVisibilityUpdateDescriptorSets(VansVKDevice* device);
		void CreateGIProbeStateDescriptorSets(VansVKDevice* device);

		void CreateGIRTPreviewDescriptorSets(VansVKDevice* device);


		//绑定数据
		void BindRayTracingData(VansVKDevice* device, VansScene* scene, uint32_t regionIndex);

		void BindGIPointLightData(uint32_t regionIndex);

		void BindGIVisibilityData(VansMaterialManager* materialManager, uint32_t regionIndex);
		void BindGIProbeStateData(uint32_t regionIndex);

		void BindGIRTPreviewData(VansMaterialManager* materialManager);

		void DispatchGIRTPreview(VansVKCommandBuffer* commandBuffer, VansMaterialManager* materialManager);

		bool UpdateLightingResponseState(VansLightManager* lightManager);

		void DestroyRegionRuntime(VkDevice device, GIRegionRuntime& region);
		GIRegionRuntime* GetPreviewRegion();
		const GIRegionRuntime* GetPreviewRegion() const;

		std::vector<GIRegionRuntime> m_GIRegions;

	private:

		std::array<VansTexture*, GIRTPreviewModeCount> m_GIRTPreviewTextures{};

		
		VansRayTracingShader* m_VansRayTracingShader = nullptr;

		VkDescriptorSetLayout m_RayTracingSetLayout;
		std::vector<VkDescriptorSet> m_RayTracingDescriptorSets;


		//GI采样点着色
		VkDescriptorSetLayout m_GISamplePositionLightSetLayout;
		std::vector<VkDescriptorSet> m_GISamplePositionLightDescriptorSets;

		VkDescriptorSetLayout m_GIVisibilityUpdateSetLayout;
		std::vector<VkDescriptorSet> m_GIVisibilityUpdateDescriptorSets;

		VkDescriptorSetLayout m_GIProbeStateSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GIProbeStateDescriptorSets;

		VkDescriptorSetLayout m_GIRTPreviewSetLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_GIRTPreviewDescriptorSets;

		VansVKBuffer m_BLASInstanceBuffer;
		VansVKBuffer m_TLASInstanceTextureIndexBuffer;
		VansVKBuffer m_TLASInstanceGIEmissionBuffer;

		VansComputeShader* m_RayTracingPointLighting = nullptr;

		VansComputeShader* m_GIVisibilityUpdateShader = nullptr;
		VansComputeShader* m_GIProbeStateShader = nullptr;

		VansComputeShader* m_GIRTPreviewShader = nullptr;
		std::array<GIRTPreviewPushConstant, GIRTPreviewModeCount> m_GIRTPreviewConstants{};
		uint32_t m_GIRTPreviewRequestFrames = 0;
		bool m_GIRTPreviewDescriptorSetIsDirty = true;
		uint32_t m_GIRTPreviewBoundZSlice = 0xffffffffu;
		VkDeviceSize m_GIRTPreviewStorageBufferAlignment = 1;

		//GI 可见度计算

		glm::vec4 m_LastGIMainLightDirectionIntensity = glm::vec4(0.0f);
		glm::vec4 m_LastGIMainLightColor = glm::vec4(0.0f);
		uint64_t m_LastGILightSignature = 0;
		float m_BaseGIEnvironmentIntensity = 1.0f;
		bool m_HasLastGIMainLight = false;
		float m_CurrentGIEnvironmentIntensity = 1.0f;

		// True after CreateRayTracingResource succeeds (scene has RT geometry).
		bool m_RTResourcesReady = false;
	};
}
