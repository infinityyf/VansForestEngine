#pragma once

#include "VansReflectionProbe.h"
#include "../VulkanCore/VansVKBuffer.h"
#include "../VulkanCore/VansVKImage.h"
#include <nlohmann/json.hpp>
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
		VansReflectionProbeSystem() = default;
		~VansReflectionProbeSystem();

		void LoadFromSceneJson(const nlohmann::json& sceneRoot, const std::string& scenePath);
		void SaveToSceneJson(nlohmann::json& sceneRoot) const;
		bool SaveConfiguration() const;
		void Clear(VkDevice device);

		void GenerateAutoProbes(const VansScene& scene, bool replaceExisting);
		void ClearAutoProbes();
		void ConvertToManual(size_t index);
		std::vector<std::string> ValidatePlacement() const;

		void CreateGPUResources(VansVKDevice& device, VansVKCommandBuffer& commandBuffer);
		void UpdateGlobalDescriptors(VkDescriptorSet globalSet);
		void UploadMetadata();
		void UpdateRealtimeProbes(uint32_t frameIndex);
		void ProcessBakeQueue(VansScene& scene, VansVKDevice& device, VansVKCommandBuffer& commandBuffer,
			uint32_t frameIndex, bool updateRealtime = true);
		void BakeQueuedProbesNow(VansScene& scene, VansVKDevice& device, VansVKCommandBuffer& commandBuffer);
		void DeferInitialBakeForGI(uint32_t spatialUpdateDivisor, uint32_t directionUpdateSlices);
		uint32_t GetBakeFaceBudget() const;

		void RequestBake(size_t index);
		void RequestBakeAll();
		bool ConsumeBakeRequest(size_t& outIndex);
		void MarkBakeComplete(size_t index, bool success, const std::string& message);
		void MarkDirty(size_t index);

		std::vector<VansReflectionProbeDesc>& GetProbes() { return m_Probes; }
		const std::vector<VansReflectionProbeDesc>& GetProbes() const { return m_Probes; }
		const std::vector<VansReflectionProbeGPU>& GetGPUProbes() const { return m_GPUProbes; }
		std::vector<ReflectionProbeBakeResult>& GetBakeResults() { return m_BakeResults; }
		const std::vector<ReflectionProbeBakeResult>& GetBakeResults() const { return m_BakeResults; }
		ReflectionProbePlacementSettings& GetPlacementSettings() { return m_PlacementSettings; }
		const ReflectionProbePlacementSettings& GetPlacementSettings() const { return m_PlacementSettings; }
		ReflectionProbeLightingSettings& GetLightingSettings() { return m_LightingSettings; }
		const ReflectionProbeLightingSettings& GetLightingSettings() const { return m_LightingSettings; }
		ReflectionProbeEditorState& GetEditorState() { return m_EditorState; }
		const ReflectionProbeEditorState& GetEditorState() const { return m_EditorState; }
		VansTexture* GetSpecularArray() const { return m_SpecularArray; }
		uint32_t GetArrayResolution() const { return m_ArrayResolution; }
		uint32_t GetMipCount() const { return m_MipCount; }
		const std::string& GetScenePath() const { return m_ScenePath; }
		const ReflectionProbePlacementGrid& GetPlacementGrid() const { return m_PlacementGrid; }
		const std::vector<GeometryRegion>& GetRegions() const { return m_Regions; }
		const std::vector<ProbeGeometryError>& GetGeometryErrors() const { return m_GeometryErrors; }

	private:
		void EnsureDefaults();
		void BuildPlacementGrid(const VansScene& scene);
		void BuildRegions();
		void BuildAutoProbesFromRegions();
		void EvaluateCoverage();
		void BuildGPUData();
		bool LoadCachedProbe(VansVKCommandBuffer& commandBuffer, size_t probeIndex);
		bool EnsureCaptureResources(VansScene& scene, VansVKDevice& device);
		void DestroyCaptureResources();
		bool CaptureFaceGPU(VansScene& scene, VansVKDevice& device, VansVKCommandBuffer& commandBuffer,
			const VansReflectionProbeDesc& probe, size_t probeIndex, uint32_t face);
		bool SaveProbeCacheGPU(VansVKDevice& device, VansVKCommandBuffer& commandBuffer,
			const VansReflectionProbeDesc& probe, size_t probeIndex);
		void CreatePrefilterResources(VansVKDevice& device);
		void PrefilterAll(VansVKDevice& device, VansVKCommandBuffer& commandBuffer);
		void PrefilterProbe(VansVKDevice& device, VansVKCommandBuffer& commandBuffer, uint32_t probeIndex);
		static VansReflectionProbeDesc ParseProbe(const nlohmann::json& value);
		static nlohmann::json SerializeProbe(const VansReflectionProbeDesc& probe);

		std::vector<VansReflectionProbeDesc> m_Probes;
		std::vector<VansReflectionProbeGPU> m_GPUProbes;
		std::vector<ReflectionProbeBakeResult> m_BakeResults;
		std::vector<size_t> m_BakeQueue;
		ReflectionProbePlacementSettings m_PlacementSettings;
		ReflectionProbeLightingSettings m_LightingSettings;
		ReflectionProbeEditorState m_EditorState;
		ReflectionProbePlacementGrid m_PlacementGrid;
		std::vector<GeometryRegion> m_Regions;
		std::vector<ProbeGeometryError> m_GeometryErrors;
		ReflectionProbeBufferHeader m_Header;
		VansVKBuffer m_MetadataBuffer;
		VansTexture* m_SpecularArray = nullptr;
		VkDevice m_Device = VK_NULL_HANDLE;
		VansComputeShader* m_PrefilterShader = nullptr;
		VkDescriptorSetLayout m_PrefilterLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> m_PrefilterSets;
		std::vector<VkImageView> m_PrefilterMipViews;
		VansGraphicsShader* m_CaptureGeometryShader = nullptr;
		VansGraphicsShader* m_CaptureSkyShader = nullptr;
		VansVKImage m_CaptureDepthImage;
		VansVKBuffer m_CaptureCameraBuffer;
		VkDescriptorSetLayout m_CaptureDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSet m_CaptureDescriptorSet = VK_NULL_HANDLE;
		VkRenderPass m_CaptureRenderPass = VK_NULL_HANDLE;
		std::vector<VkImageView> m_CaptureFaceViews;
		std::vector<VkFramebuffer> m_CaptureFramebuffers;
		bool m_CaptureDepthCreated = false;
		bool m_CaptureCameraBufferCreated = false;
		std::string m_ScenePath;
		uint32_t m_ArrayResolution = 128;
		uint32_t m_MipCount = 8;
		uint64_t m_BakeRevision = 0;
		size_t m_ActiveBakeIndex = size_t(-1);
		uint32_t m_ActiveBakeFace = 0;
		uint32_t m_GIWarmupFramesRemaining = 0;
		uint32_t m_LastGIWarmupFrame = 0xffffffffu;
	};
}
