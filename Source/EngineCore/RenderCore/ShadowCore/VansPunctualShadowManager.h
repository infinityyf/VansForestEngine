#pragma once

#include "VansPunctualShadowTypes.h"
#include "VansShadowAtlasAllocator.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	class VansPunctualShadowManager
	{
	public:
		VansPunctualShadowManager(uint32_t atlasSize = 4096, uint32_t basePageSize = 128, uint32_t gutter = 2);

		void Reset();
		void RemoveLight(uint32_t stableLightId);
		void PrepareFrame(
			const VansPunctualShadowCameraData& camera,
			const std::vector<VansPunctualShadowLightInput>& lights,
			uint64_t frameIndex);

		void InvalidateAllCasters(uint32_t dirtyReason = VansShadowDirty_CasterTransform);
		void InvalidateCastersInBounds(
			const VansShadowAABB& oldBounds,
			const VansShadowAABB& newBounds,
			uint32_t dirtyReason = VansShadowDirty_CasterTransform);

		uint32_t GetShadowMetaIndex(uint32_t stableLightId) const;
		bool HasRenderJobs() const { return !m_RenderJobs.empty(); }
		bool HasRenderJobs(uint32_t atlasIndex) const;

		const std::vector<VansPunctualShadowGPU>& GetGPUShadowData() const { return m_GPUShadowData; }
		const std::vector<VansPunctualShadowViewGPU>& GetGPUShadowViews() const { return m_GPUShadowViews; }
		const std::vector<VansPunctualShadowRenderJob>& GetRenderJobs() const { return m_RenderJobs; }
		std::vector<VansPunctualShadowRenderJob>& GetMutableRenderJobs() { return m_RenderJobs; }
		const VansPunctualShadowStatistics& GetStatistics() const { return m_Statistics; }

		const VansPunctualShadowBudget& GetBudget() const { return m_Budget; }
		void SetBudget(const VansPunctualShadowBudget& budget) { m_Budget = budget; }

		const VansShadowAtlasAllocator& GetAtlasAllocator(uint32_t atlasIndex) const;
		uint32_t GetTotalAtlasPages() const;
		VansPunctualShadowDebugSnapshot CaptureDebugSnapshot() const;
		void RequestDebugPreview();
		bool ConsumeDebugPreviewRefreshRequest();

	private:
		struct Runtime
		{
			VansPunctualShadowLightInput input;
			VansShadowRuntimeState state = VansShadowRuntimeState::Disabled;
			std::array<VansShadowAtlasBlock, 6> activeBlocks{};
			std::array<VansShadowAtlasBlock, 6> pendingBlocks{};
			uint16_t activeResolution = 0;
			uint16_t pendingResolution = 0;
			uint16_t targetResolution = 0;
			uint16_t allocationGeneration = 0;
			uint8_t requiredFaceMask = 0;
			uint8_t dirtyFaceMask = 0;
			uint8_t pendingFaceMask = 0;
			uint8_t validFaceMask = 0;
			uint32_t dirtyReasons = VansShadowDirty_None;
			float importance = 0.0f;
			float coverage = 0.0f;
			float cameraDistance = 0.0f;
			float distancePriority = 0.0f;
			float atlasWeight = 0.0f;
			uint32_t residencyFrames = 0;
			uint32_t staleFrames = 0;
			uint32_t upgradeFrames = 0;
			uint32_t downgradeFrames = 0;
			uint64_t lastRenderedFrame = 0;
			bool projectionValid = false;
			bool selected = false;
			bool seenThisFrame = false;
			bool hasPreviousInput = false;
		};

		struct Candidate
		{
			Runtime* runtime = nullptr;
			uint16_t resolution = 0;
			uint32_t pageCost = 0;
		};

		static uint8_t RequiredFaceMask(VansPunctualShadowLightType type);
		static uint32_t ViewCount(VansPunctualShadowLightType type);
		static uint32_t PageCost(uint32_t resolution, uint32_t viewCount, uint32_t basePageSize);
		static uint16_t DownshiftResolution(uint16_t resolution);
		static bool IntersectsSphere(const VansShadowAABB& bounds, const glm::vec3& center, float radius);

		float ComputeCoverage(const VansPunctualShadowLightInput& input, const VansPunctualShadowCameraData& camera) const;
		float ComputeDistancePriority(const VansPunctualShadowLightInput& input, const VansPunctualShadowCameraData& camera) const;
		float ComputeImportance(const VansPunctualShadowLightInput& input, const VansPunctualShadowCameraData& camera, bool resident, float coverage) const;
		uint16_t ComputeDesiredResolution(const VansPunctualShadowLightInput& input, const VansPunctualShadowCameraData& camera, float coverage) const;
		uint16_t ResolveHystereticResolution(Runtime& runtime, uint16_t desiredResolution) const;
		bool IsEligible(const VansPunctualShadowLightInput& input, const VansPunctualShadowCameraData& camera) const;
		bool ProjectionChanged(const Runtime& runtime, const VansPunctualShadowLightInput& input) const;

		bool EnsurePendingAllocation(Runtime& runtime, uint16_t resolution);
		bool AllocateGroup(uint16_t resolution, uint32_t viewCount, std::array<VansShadowAtlasBlock, 6>& outBlocks);
		bool ValidateBlock(const VansShadowAtlasBlock& block) const;
		void ReleaseBlocks(std::array<VansShadowAtlasBlock, 6>& blocks, uint32_t viewCount);
		void ReleaseRuntime(Runtime& runtime);
		void PromotePending(Runtime& runtime);
		void BuildRenderJobs(std::vector<Runtime*>& orderedRuntimes);
		void BuildGPUData(const std::vector<VansPunctualShadowLightInput>& lights);

		glm::mat4 BuildShadowMatrix(const Runtime& runtime, uint32_t faceIndex, const VansShadowAtlasBlock& block) const;
		VansPunctualShadowViewGPU BuildGPUView(const Runtime& runtime, uint32_t faceIndex, const VansShadowAtlasBlock& block) const;

		std::array<VansShadowAtlasAllocator, VANS_PUNCTUAL_SHADOW_ATLAS_COUNT> m_AtlasAllocators;
		VansPunctualShadowBudget m_Budget;
		std::unordered_map<uint32_t, Runtime> m_Runtimes;
		std::unordered_map<uint32_t, uint32_t> m_LightToMetaIndex;
		std::vector<VansPunctualShadowGPU> m_GPUShadowData;
		std::vector<VansPunctualShadowViewGPU> m_GPUShadowViews;
		std::vector<VansPunctualShadowRenderJob> m_RenderJobs;
		VansPunctualShadowStatistics m_Statistics;
		VansPunctualShadowCameraData m_Camera;
		uint64_t m_FrameIndex = 0;
		uint32_t m_NextAtomicGroupId = 1;
		uint32_t m_DebugPreviewHeartbeat = 1;
		bool m_DebugPreviewForceRefresh = true;
	};
}
