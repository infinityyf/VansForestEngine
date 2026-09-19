#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <chrono>
#include <vector>
#include <cstddef>
#include <memory>
#include <string>
#include "../../RenderCore/VulkanCore/VansVKBuffer.h"
#include "../../RenderCore/VulkanCore/VansShader.h"
#include "../../RenderCore/BRDFData/VansLight.h"
#include "../../RenderCore/GICore/VansGISettings.h"
#include "../../RenderCore/GICore/VansGIProbeWorkScheduler.h"
#include "../../RenderCore/GICore/VansGIProbeLayout.h"
#include "../../RenderCore/GICore/VansGIWorld.h"
#include "../../RenderCore/GICore/VansGIScrollingGrid.h"
#include "../../ScriptCore/VansCommonUtils.h"
namespace VansGraphics
{
	inline constexpr uint32_t GIRTPreviewModeCount = 12u;

	class VansLightManager;
	class VansScene;
	class VansMaterialManager;
	struct VansRenderLightFrameData;
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
		glm::vec4 updateParams;     // x = update frame, yz = reserved, w = rays per complete probe
	};
	static_assert(sizeof(GIRTPreviewPushConstant) == 64, "GI RT preview push constant layout must match GLSL");

	class VansRayTracing
	{
		//由于和正常shader流程差异较大，这里重新做一份shader的解析，编译和管线创建

	private:



	public:

		void PrepareGIProbeUpdate(
			const VansRenderLightFrameData& lightFrame,
			VansMaterialManager* materialManager, VkFence completionFence);
        // 调用点必须已通过现有提交 fence 确认 GPU 完成；这些函数不会新增等待。
        void CompleteGIProbeUpdate(VkFence completionFence);
        void DiscardGIProbeUpdate();

		void DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene);
		
		bool CreateRayTracingResource(VansVKDevice* device, VansVKCommandBuffer* commandBuffer,
			VansScene* scene, const VansGISettings& settings);
		const std::string& GetResourceError() const { return m_ResourceError; }
		const VansGISettings& GetAppliedSettings() const { return m_State->settings; }

		// 场景切换时清理与当前场景绑定的 RT 资源（descriptor set、buffer 等）
		// BLAS 由 mesh 管理，不在此处释放。
		void CleanupSceneResources(VkDevice device);

		void UpdateGIProbe(
			VansVKDevice* device,
			VansVKCommandBuffer* commandBuffer,
			VansMaterialManager* materialManager);

		void UpdateGISettings(const VansGISettings& settings);

		bool IsReady() const { return m_State->m_RTResourcesReady; }
		void InvalidateWorldSources() { if(m_State->world)m_State->world->InvalidateSources(); }
        bool QueueWorldSourceChanges(GIVoxelSourceChanges changes);
        bool NeedsWorldSourceRebuild() const { return m_State->world && m_State->world->SourcesDirty(); }
        GIWorldSourceUpdateStats GetWorldSourceUpdateStats() const { return m_State->world?m_State->world->SourceUpdateStats():GIWorldSourceUpdateStats{}; }
        void PrepareWorldUpdates();
        bool HasPendingWorldUpdates() const { return m_State->world && m_State->world->HasPendingUpdates(); }
        void InvalidateWorldGeometry(const GIWorldDirtyRegions& changed);
        bool ApplyWorldHeightPatch(uint32_t x,uint32_t z,uint32_t w,uint32_t h,
            const std::vector<uint8_t>& pixels);
        bool ApplyWorldColorPatch(uint32_t map,uint32_t x,uint32_t y,uint32_t w,uint32_t h,
            const std::vector<uint8_t>& pixels);
        void SetWorldViewCenter(glm::vec3 center);
        uint64_t GetWorldGeometryRevision() const { return m_State->world ? m_State->world->Revision() : 0; }
		uint64_t GetWorldAllocatedBytes() const { return m_State->world ? m_State->world->AllocatedBytes() : 0; }

		void RequestGIRTPreviews(uint32_t zSlice, uint32_t rayIndex, float exposure, float positionScale);
		VansTexture* GetGIRTPreviewTexture(uint32_t mode) const
		{
			return m_State->m_GIRTPreviewTextures[std::min(mode, GIRTPreviewModeCount - 1u)];
		}
		uint32_t GetGIRegionCount() const { return static_cast<uint32_t>(m_State->m_GIRegions.size()); }
		VansTexture* GetGIRegionIrradianceAtlas(uint32_t regionIndex) const;
		VansTexture* GetGIRegionVisibilityAtlas(uint32_t regionIndex) const;
		const VansVKBuffer* GetGIRegionProbeStateBuffer(uint32_t regionIndex) const;
        const VansVKBuffer* GetGIRegionPreviousProbeStateBuffer(uint32_t regionIndex) const;
        void CopyPublishedProbeStateHistory(VansVKCommandBuffer& command);
        bool DispatchReceiverBias(VansVKDevice* device, VansVKCommandBuffer& command,
            VansScene* scene, VkDescriptorSetLayout layout, VkDescriptorSet descriptor, uint32_t width, uint32_t height);
        bool DispatchReceiverVisibility(VansVKDevice* device, VansVKCommandBuffer& command,
            VansScene* scene, VkDescriptorSetLayout layout, VkDescriptorSet descriptor);

		
		const VansVKBuffer& GetGIProbeLayoutBuffer() const { return m_State->m_ProbeLayoutBuffer; }
		uint32_t GetGIRegionPhysicalProbeCount(uint32_t index) const
		{ return index < m_State->m_GIRegions.size() ? m_State->m_GIRegions[index].physicalProbeCount : 0u; }
		bool UsesSparseGI() const { return m_State->m_AutomaticGIWork; }
        const GIResolvedRegion* GetGIRegionResolved(uint32_t index) const
        { return index < m_State->m_GIRegions.size() ? &m_State->m_GIRegions[index].resolved : nullptr; }
		std::shared_ptr<const GIProbeLayoutSnapshot> GetGIProbeLayoutSnapshot() const
		{ return std::atomic_load(&m_PublishedGIProbeLayout); }
	private:

        void RecordWorldProbeInvalidation(VansVKCommandBuffer& command);
        void QueueLayoutParameters(const std::vector<GIResolvedRegion>& resolved);
		std::shared_ptr<const GIProbeLayoutSnapshot> m_PublishedGIProbeLayout;
		struct GIRegionRuntime
		{
			GIResolvedRegion resolved;
            VansGIScrollingGrid scrollingGrid;
			glm::uvec3 storageDimensions{1u};
			uint32_t physicalProbeCount = 0u;
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
			VansVKBuffer previousProbeStateBuffer;
			VansVKBuffer workBuffer;
            VansVKBuffer feedbackBuffer;
            VansVKBuffer feedbackReadback;
            // 显式诊断时才分配；在已有完成 fence 后读取真实 GPU 状态。
            VansVKBuffer stateAuditReadback;
            bool stateAuditPending = false;
            uint32_t stateAuditFrame = 0;
			GIProbeRegionWork work;
            std::vector<uint32_t> pendingStateClears;

			bool rayTracingDescriptorSetIsDirty = true;
			bool giPointLightDescriptorSetIsDirty = true;
			bool giVisibilityDescriptorSetIsDirty = true;
			bool giProbeStateDescriptorSetIsDirty = true;
			uint32_t giUpdateFrameIndex = 0;
		};

		struct RuntimeState
		{
			std::unique_ptr<VansGIWorld> world;
			bool hasHardwareGeometry = false;
			std::vector<GIRegionRuntime> m_GIRegions;
			VansGIProbeWorkScheduler m_WorkScheduler;
            std::chrono::steady_clock::time_point m_LastGIUpdateTime{};
			VansGIProbeLayout m_ProbeLayout;
			VansVKBuffer m_ProbeLayoutBuffer;
			std::vector<glm::uvec4> m_PendingLayoutRegionData;
            std::vector<glm::uvec4> m_PendingScrollData;
            uint32_t m_ScrollDataOffset = 0;
			bool m_AutomaticGIWork = false;
            bool m_GIStateAuditEnabled = false;
			VkFence m_GIFeedbackFence = VK_NULL_HANDLE;
			uint32_t m_MaxComputeGroupsX = 65535u;


			std::array<VansTexture*, GIRTPreviewModeCount> m_GIRTPreviewTextures{};

			VansRayTracingShader* m_VansRayTracingShader = nullptr;
			VansRayTracingShader* m_ReceiverVisibilityShader = nullptr;
            VansRayTracingShader* m_ReceiverBiasShader = nullptr;
            VansVKBuffer m_ReceiverGeometryData;

			VkDescriptorSetLayout m_RayTracingSetLayout = VK_NULL_HANDLE;
			std::vector<VkDescriptorSet> m_RayTracingDescriptorSets;


			//GI采样点着色
			VkDescriptorSetLayout m_GISamplePositionLightSetLayout = VK_NULL_HANDLE;
			std::vector<VkDescriptorSet> m_GISamplePositionLightDescriptorSets;

			VkDescriptorSetLayout m_GIVisibilityUpdateSetLayout = VK_NULL_HANDLE;
			std::vector<VkDescriptorSet> m_GIVisibilityUpdateDescriptorSets;

			VkDescriptorSetLayout m_GIProbeStateSetLayout = VK_NULL_HANDLE;
			std::vector<VkDescriptorSet> m_GIProbeStateDescriptorSets;

			VkDescriptorSetLayout m_GIRTPreviewSetLayout = VK_NULL_HANDLE;
			std::vector<VkDescriptorSet> m_GIRTPreviewDescriptorSets;

			VansVKBuffer m_BLASInstanceBuffer;
			VansVKBuffer m_TLASInstanceMaterialBuffer;
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

			bool m_HasLastGIMainLight = false;
			float m_LastSkyRadianceScale = 1.0f;

			// True after CreateRayTracingResource succeeds (scene has RT geometry).
			bool m_RTResourcesReady = false;
			VansGISettings settings;
		};
		std::unique_ptr<RuntimeState> m_State = std::make_unique<RuntimeState>();
		std::string m_ResourceError;
		void InitializeSceneResources(VansVKDevice* device, VansVKCommandBuffer* commandBuffer,
			VansScene* scene, const VansGISettings& settings);
		void ReleaseSceneResources(VkDevice device, bool releaseSharedPipeline);

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

		bool UpdateLightingResponseState(const VansRenderLightFrameData& lightFrame);

		void DestroyRegionRuntime(VkDevice device, GIRegionRuntime& region);
		GIRegionRuntime* GetPreviewRegion();
		const GIRegionRuntime* GetPreviewRegion() const;


	};
}
