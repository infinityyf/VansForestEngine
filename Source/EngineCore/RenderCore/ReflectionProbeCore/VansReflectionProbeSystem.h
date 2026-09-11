#pragma once

#include "VansReflectionProbe.h"
#include "VansReflectionProbePageLayout.h"
#include "VansReflectionProbeSpatialIndex.h"
#include "VansReflectionProbePlacement.h"
#include "VansReflectionProbeLayout.h"
#include "../VulkanCore/VansRenderPass.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKImage.h"
#include "../../SceneCore/VansSceneReflectionProbeConfig.h"
#include <memory>

namespace VansGraphics
{
	class VansScene;
	class VansTexture;
	class VansVKDevice;
	class VansVKCommandBuffer;
	class VansComputeShader;
	class VansGraphicsShader;

	class VansReflectionProbeSystem
	{
	public:
		struct PrefilterPushConstants
		{
			float roughness;
			uint32_t outputSize;
			uint32_t cubeCount;
			uint32_t sampleCount;
			uint32_t baseCube;
		};

		VansReflectionProbeSystem();
		~VansReflectionProbeSystem();

		void LoadFromSceneConfig(const Vans::VansSceneReflectionProbeConfig& config, const std::string& scenePath);
		void Clear(VkDevice device);

		bool GenerateAutoProbes(const VansScene& scene, VansVKDevice& device);
		bool ApplySettings(const VansScene& scene, VansVKDevice& device,
			const ReflectionProbePlacementSettings& placement, const ReflectionProbeLightingSettings& lighting,
			const ReflectionProbeEditorState& editor, const std::vector<VansReflectionProbeDesc>& edits,
			bool forceRegenerate);
		bool ClearAutoProbes(const VansScene& scene, VansVKDevice& device);
		void ConvertToManual(size_t index);
		std::vector<std::string> ValidatePlacement() const;

		bool CreateGPUResources(VansVKDevice& device, VansVKCommandBuffer& commandBuffer);
		void UpdateGlobalDescriptors(VkDescriptorSet globalSet);
		void UploadMetadata();
		void UpdateRealtimeProbes(uint32_t frameIndex);
		bool HasCaptureWork() const { return !m_State->m_SpecularPages.empty() && !m_State->m_BakeQueue.empty(); }
		uint32_t GetPendingCaptureCount() const { return static_cast<uint32_t>(m_State->m_BakeQueue.size()); }
		// GI 资源退役前由渲染事务调用；不更改 Reflection 排布、开关或缓存内容。
		void ReleaseGILightingBindings() { DestroyCaptureResources(); }
		void ProcessBakeQueue(VansScene& scene, VansVKDevice& device, VansVKCommandBuffer& commandBuffer);
		void BakeQueuedProbesNow(VansScene& scene, VansVKDevice& device, VansVKCommandBuffer& commandBuffer);
		uint32_t GetBakeFaceBudget() const;

		void RequestBake(size_t index);
		void RequestBakeAll();
        void SetSkyLightingSource(const std::string& sourceKey);
		void MarkBakeComplete(size_t index, bool success, const std::string& message);
		void MarkDirty(size_t index);

		const std::vector<VansReflectionProbeDesc>& GetProbes() const { return m_State->config.m_Probes; }
		const std::vector<VansReflectionProbeDesc>& GetAuthoredProbes() const { return m_State->config.m_Layout.Authored(); }
		const std::vector<VansReflectionProbeDesc>& GetPlacementOverrides() const { return m_State->config.m_Layout.Overrides(); }
		bool IsGeneratedProbe(size_t index) const { return index < m_State->config.m_ProbeOrigins.size() && m_State->config.m_ProbeOrigins[index].source == VansReflectionProbeSource::Generated; }
		const VansReflectionProbePlacementResult& GetPlacementResult() const { return m_State->config.m_PlacementResult; }
		const std::string& GetPlacementError() const { return m_State->m_PlacementError; }
		const std::vector<VansReflectionProbeGPU>& GetGPUProbes() const { return m_State->m_GPUProbes; }
		std::vector<ReflectionProbeBakeResult>& GetBakeResults() { return m_State->m_BakeResults; }
		const std::vector<ReflectionProbeBakeResult>& GetBakeResults() const { return m_State->m_BakeResults; }
		const ReflectionProbePlacementSettings& GetPlacementSettings() const { return m_State->config.m_PlacementSettings; }
		ReflectionProbeLightingSettings& GetLightingSettings() { return m_State->config.m_LightingSettings; }
		const ReflectionProbeLightingSettings& GetLightingSettings() const { return m_State->config.m_LightingSettings; }
		ReflectionProbeEditorState& GetEditorState() { return m_State->config.m_EditorState; }
		const ReflectionProbeEditorState& GetEditorState() const { return m_State->config.m_EditorState; }
		VansTexture* GetProbeTexture(size_t probeIndex) const;
		uint32_t GetTexturePageCount() const { return static_cast<uint32_t>(m_State->m_SpecularPages.size()); }
		VkImageView GetPreviewFaceView(size_t probeIndex, uint32_t face, uint32_t mipLevel);
		uint32_t GetProbeResolution(size_t probeIndex) const;
		uint32_t GetProbeMipCount(size_t probeIndex) const;
		uint64_t GetResidentTextureBytes() const { return m_State->m_PageLayout.ResidentBytes(); }
		const std::string& GetScenePath() const { return m_State->config.m_ScenePath; }
		const ReflectionProbePlacementGrid& GetPlacementGrid() const { return m_State->config.m_PlacementGrid; }
		const std::vector<GeometryRegion>& GetRegions() const { return m_State->config.m_Regions; }
		const std::vector<ProbeGeometryError>& GetGeometryErrors() const { return m_State->config.m_GeometryErrors; }

	private:
		struct CaptureRequest
		{
			size_t probeIndex;
			bool persistCache;
		};

		bool PrepareSettings(const VansScene& scene, VansVKDevice& device,
			const ReflectionProbePlacementSettings& placement, const ReflectionProbeLightingSettings& lighting,
			const ReflectionProbeEditorState& editor, const std::vector<VansReflectionProbeDesc>& edits,
			bool forceRegenerate, bool& rebuildResources);
		void CopyConfigurationTo(VansReflectionProbeSystem& pending) const;
		bool InitializeGPUResources(VansVKDevice& device, VansVKCommandBuffer& commandBuffer);
		bool InitializeMetadataResources(VansVKDevice& device);
		void CommitResources(VansVKDevice& device, const std::shared_ptr<VansReflectionProbeSystem>& pending);
		void ReleaseSharedPipelines();
		void WriteGlobalDescriptors(VkDescriptorSet globalSet, const std::vector<std::unique_ptr<VansTexture>>& pages);
		void QueueCapture(size_t index, bool persistCache);
		void EnsureDefaults();
		void BuildGPUData();
		void UploadSpatialIndex();
		void RefreshLightingParams();
		bool LoadCachedProbes(VansVKDevice& device, VansVKCommandBuffer& commandBuffer);
		bool EnsureCaptureResources(VansScene& scene, VansVKDevice& device);
		bool PrepareCaptureTarget(uint32_t baseMip);
		void DestroyCaptureResources();
		bool CaptureFaceGPU(VansScene& scene, VansVKDevice& device, VansVKCommandBuffer& commandBuffer,
			const VansReflectionProbeDesc& probe, size_t probeIndex, uint32_t face);
		bool SaveProbeCacheGPU(VansVKDevice& device, VansVKCommandBuffer& commandBuffer,
			const VansReflectionProbeDesc& probe, size_t probeIndex);
		bool CreatePrefilterResources(VansVKDevice& device);
		bool PublishCapture(VansVKDevice& device, VansVKCommandBuffer& commandBuffer, uint32_t probeIndex);
		bool RecordPrefilterCapture(VansVKCommandBuffer& commandBuffer, uint32_t baseMip);
		// 作者配置与其派生 GPU 数据属于同一次发布；结构切换只交换完整状态。
		struct Configuration
		{
			std::vector<VansReflectionProbeDesc> m_Probes;
			VansReflectionProbeLayout m_Layout;
			std::vector<VansReflectionProbeOrigin> m_ProbeOrigins;
			VansReflectionProbePlacementResult m_PlacementResult;
			ReflectionProbePlacementSettings m_PlacementSettings;
			ReflectionProbeLightingSettings m_LightingSettings;
			ReflectionProbeEditorState m_EditorState;
			ReflectionProbePlacementGrid m_PlacementGrid;
			std::vector<GeometryRegion> m_Regions;
			std::vector<ProbeGeometryError> m_GeometryErrors;
			std::string m_ScenePath;
		};
		struct RuntimeState
		{
			Configuration config;
            std::string skyLightingSourceKey;
			std::string m_PlacementError;
			std::vector<VansReflectionProbeGPU> m_GPUProbes;
			std::vector<ReflectionProbeBakeResult> m_BakeResults;
			std::vector<CaptureRequest> m_BakeQueue;
			ReflectionProbeBufferHeader m_Header;
			VansVKBuffer m_MetadataBuffer;
			VansReflectionProbeSpatialIndex m_SpatialIndex;
			VansVKBuffer m_SpatialIndexBuffer;
			VkDescriptorSet m_GlobalDescriptorSet = VK_NULL_HANDLE;
			VansReflectionProbePageLayout m_PageLayout;
			std::vector<std::unique_ptr<VansTexture>> m_SpecularPages;
			std::unique_ptr<VansTexture> m_CaptureTexture;
			VkDevice m_Device = VK_NULL_HANDLE;
			std::vector<VkImageView> m_EditorPreviewFaceViews;
			VansComputeShader* m_PrefilterShader = nullptr;
			VkDescriptorSetLayout m_PrefilterLayout = VK_NULL_HANDLE;
			std::vector<VkDescriptorSet> m_PrefilterSets;
			std::vector<VkImageView> m_PrefilterMipViews;
			std::vector<VkImageView> m_PrefilterSourceViews;
			std::vector<uint32_t> m_PrefilterOffsets;
			VansGraphicsShader* m_CaptureGeometryShader = nullptr;
			VansGraphicsShader* m_CaptureSkyShader = nullptr;
			VansVKImage m_CaptureDepthImage;
			VansVKBuffer m_CaptureCameraBuffer;
			VkDescriptorSetLayout m_CaptureDescriptorLayout = VK_NULL_HANDLE;
			VkDescriptorSet m_CaptureDescriptorSet = VK_NULL_HANDLE;
			VansVKRenderPass m_CaptureRenderPass;
			bool m_CaptureRenderPassCreated = false;
			std::vector<VkImageView> m_CaptureFaceViews;
			std::vector<VansFrameBuffer> m_CaptureFramebuffers;
			bool m_CaptureDepthCreated = false;
			bool m_CaptureCameraBufferCreated = false;
			uint32_t m_CaptureResolution = 1;
			uint32_t m_CaptureMipCount = 1;
			uint64_t m_BakeRevision = 0;
			size_t m_ActiveBakeIndex = size_t(-1);
			uint32_t m_ActiveBakeFace = 0;
		};
		std::unique_ptr<RuntimeState> m_State;
	};
}
