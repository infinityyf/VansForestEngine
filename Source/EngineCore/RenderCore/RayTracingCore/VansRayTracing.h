#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <chrono>
#include <vector>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <string>
#include "../../RenderCore/VulkanCore/VansVKBuffer.h"
#include "../../RenderCore/VulkanCore/VansShader.h"
#include "../../RenderCore/GICore/VansGISettings.h"
#include "../GICore/VansGIProbeDispatchParameters.h"
#include "../GICore/VansGIMemoryPublication.h"
#include "../GICore/VansGIFrameWorkPublication.h"
#include "../../RenderCore/GICore/VansGIInstanceMaterial.h"
#include "../../RenderCore/GICore/VansGIProbeWorkScheduler.h"
#include "../../RenderCore/GICore/VansGILightingChangeTracker.h"
#include "../../RenderCore/GICore/VansGIProbeLayout.h"
#include "../../RenderCore/GICore/VansGIWorld.h"
#include "../../RenderCore/GICore/VansGIScrollingGrid.h"
#include "../../ScriptCore/VansCommonUtils.h"
namespace VansGraphics
{
	inline constexpr uint32_t GIRTPreviewModeCount = 12u;

	class VansScene;
	class VansMaterialManager;
	class VansSSGIHistory;
	struct VansRenderLightFrameData;
	struct VansRenderMaterialFrameData;
}
namespace VansGraphics
{
	class VansVKCommandBuffer;
	class VansVKDevice;
	class VansMesh;
	class VansRayTracingShader;
	class VansTexture;
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
			VansSSGIHistory& ssgiHistory, VkFence completionFence,
            const VansGILightingContentChanges& contentChanges);
        VkFence GetPendingGIProbeFeedbackFence() const { return m_State->m_GIFeedbackFence; }
        // 调用点必须已通过现有提交 fence 确认 GPU 完成；这些函数不会新增等待。
        void CompleteGIProbeUpdate(VkFence completionFence);
        void DiscardGIProbeUpdate();
        VansRayTracing(std::shared_ptr<VansGIMemoryPublication> memory,
            std::shared_ptr<VansGIFrameWorkPublication> work)
            : m_MemoryPublication(std::move(memory)), m_WorkPublication(std::move(work)) {}

		void DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene);
		bool PrepareClothMaterialUpdates(const VansRenderMaterialFrameData& frame);
		void InvalidateClothTextureDescriptors();
		
		bool CreateRayTracingResource(VansVKDevice* device, VansVKCommandBuffer* commandBuffer,
			VansScene* scene, const VansGISettings& settings, const std::shared_ptr<const VansGIVoxelMaterialInputs>& materials);
		const std::string& GetResourceError() const { return m_ResourceError; }
		const VansGISettings& GetAppliedSettings() const { return m_State->settings; }

		// 场景切换时清理与当前场景绑定的 RT 资源（descriptor set、buffer 等）
		// BLAS 由 mesh 管理，不在此处释放。
		void CleanupSceneResources(VkDevice device);

		void UpdateGIProbe(
			VansVKDevice* device,
			VansVKCommandBuffer* commandBuffer,
			VansMaterialManager* materialManager);

		void UpdateGISettings(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, const VansGISettings& settings);

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
		VansGIWorldQueryPublication GetWorldQueryPublication() const
        { return m_State->world ? m_State->world->QueryPublication() : VansGIWorldQueryPublication{}; }
		VansGIWorld* GetGIWorld() const { return m_State->world.get(); }

		void RequestGIRTPreviews(uint32_t zSlice, uint32_t rayIndex, float exposure, float positionScale);
		VansTexture* GetGIRTPreviewTexture(uint32_t mode) const
		{
			return m_State->m_GIRTPreviewTextures[std::min(mode, GIRTPreviewModeCount - 1u)];
		}
		uint32_t GetGIRegionCount() const { return static_cast<uint32_t>(m_State->m_GIRegions.size()); }
		VansTexture* GetGIRegionIrradianceAtlas(uint32_t regionIndex) const;
		VansTexture* GetGIRegionVisibilityAtlas(uint32_t regionIndex) const;
		const VansVKBuffer* GetGIRegionProbeStateBuffer(uint32_t regionIndex) const;
        const VansVKBuffer* GetGIRegionIrradianceHistoryBuffer(uint32_t regionIndex) const;
        const VansVKBuffer* GetGIRegionPreviousProbeStateBuffer(uint32_t regionIndex) const;
        uint64_t CopyPublishedProbeStateHistory(VansVKCommandBuffer& command);
        bool DispatchReceiverBias(VansVKDevice* device, VansVKCommandBuffer& command,
            VansScene* scene, VkDescriptorSetLayout layout, VkDescriptorSet descriptor, uint32_t width, uint32_t height, VansGIReceiverFrameWork& work);
        bool DispatchReceiverVisibility(VansVKDevice* device, VansVKCommandBuffer& command,
            VansScene* scene, VkDescriptorSetLayout layout, VkDescriptorSet descriptor, VansGIReceiverFrameWork& work);

		
		const VansVKBuffer& GetGIProbeLayoutBuffer() const { return m_State->m_ProbeLayoutBuffer; }
		uint32_t GetGIRegionPhysicalProbeCount(uint32_t index) const
		{ return index < m_State->m_GIRegions.size() ? m_State->m_GIRegions[index].physicalProbeCount : 0u; }
		bool UsesSparseGI() const { return m_State->m_HasAdaptiveRegions; }
        const GIResolvedRegion* GetGIRegionResolved(uint32_t index) const
        { return index < m_State->m_GIRegions.size() ? &m_State->m_GIRegions[index].resolved : nullptr; }
		std::shared_ptr<const GIProbeLayoutSnapshot> GetGIProbeLayoutSnapshot() const
		{ return std::atomic_load(&m_PublishedGIProbeLayout); }
	private:
		struct GIRTPreviewRequest
		{
			uint32_t zSlice, rayIndex;
			float exposure, positionScale;
		};
		std::mutex m_PreviewRequestMutex;
		std::optional<GIRTPreviewRequest> m_PendingPreviewRequest;
		void ApplyGIRTPreviewRequest(const GIRTPreviewRequest& request);

        void RecordWorldProbeInvalidation(VansVKCommandBuffer& command);
        void QueueLayoutParameters(const std::vector<GIResolvedRegion>& resolved);
		std::shared_ptr<const GIProbeLayoutSnapshot> m_PublishedGIProbeLayout;
        std::shared_ptr<VansGIFrameWorkPublication> m_WorkPublication;
        // 候选及退役对象共享设备发布器；回调不捕获已可能销毁的设备/RT 对象。
        std::shared_ptr<VansGIMemoryPublication> m_MemoryPublication;
        VansGIProbeMemory CaptureMemorySnapshot() const;
		struct GIRegionRuntime
		{
			GIResolvedRegion resolved;
            VansGIScrollingGrid scrollingGrid;
			glm::uvec3 storageDimensions{1u};
			uint32_t physicalProbeCount = 0u;
			VansGIProbeDispatchParameters constants{};

			VansTexture* rayTracingResult = nullptr;
			VansTexture* irradianceAtlas = nullptr;
			VansTexture* visibilityAtlas = nullptr;

			VansVKBuffer hitPositionResult;
			VansVKBuffer hitNormalResult;
			VansVKBuffer hitAlbedoRoughnessResult;
			VansVKBuffer hitEmissionResult;
			VansVKBuffer hitRadianceBuffer;
            VansVKBuffer irradianceHistoryBuffer;
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
			bool m_HasAdaptiveRegions = false;
            bool m_GIStateAuditEnabled = false;
			VkFence m_GIFeedbackFence = VK_NULL_HANDLE;
            VansGIProbeFrameWork m_RecordedGIFrameWork;
            bool m_GIFrameWorkComplete = false;
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
			struct ClothInstanceUpdate
			{
				uint32_t instanceIndex;
				GIInstanceMaterialGPU material;
			};
			std::vector<ClothInstanceUpdate> m_ClothInstanceUpdates;
			bool m_ClothLightingChanged = false;
            uint32_t m_PendingQueryRegionResets = 0;
			VansVKBuffer m_TLASInstanceGIEmissionBuffer;

			VansComputeShader* m_RayTracingPointLighting = nullptr;

			VansComputeShader* m_GIVisibilityUpdateShader = nullptr;
			VansComputeShader* m_GIProbeStateShader = nullptr;

			VansComputeShader* m_GIRTPreviewShader = nullptr;
			std::array<GIRTPreviewPushConstant, GIRTPreviewModeCount> m_GIRTPreviewConstants{};
			uint32_t m_GIRTPreviewRequestFrames = 0;
			bool m_GIRTPreviewDescriptorSetIsDirty = true;

			//GI 可见度计算

			VansGILightingChangeTracker m_LightingChanges;

			// True after CreateRayTracingResource succeeds (scene has RT geometry).
			bool m_RTResourcesReady = false;
			VansGISettings settings;
		};
		std::unique_ptr<RuntimeState> m_State = std::make_unique<RuntimeState>();
		std::string m_ResourceError;
		void InitializeSceneResources(VansVKDevice* device, VansVKCommandBuffer* commandBuffer,
			VansScene* scene, const VansGISettings& settings, const std::shared_ptr<const VansGIVoxelMaterialInputs>& materials);
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

		void InvalidateLocalLighting(const VansGILightingBounds& bounds, float changeAmount, bool worldOnly);

		void DestroyRegionRuntime(VkDevice device, GIRegionRuntime& region);
		const GIRegionRuntime* GetPreviewRegion(const VansGISettings& settings) const;


	};
}
