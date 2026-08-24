#pragma once

#include "../VansMainCameraVisibility.h"
#include "VansVKBuffer.h"

#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace VansGraphics
{
	class VansVKDevice;
	struct VansRenderViewSnapshot;

	// Vulkan backend owner for main-camera HiZ history, frame-slot GPU resources,
	// visibility results and diagnostics. Scene contributes only immutable inputs.
	class VansMainCameraVisibilityState final
	{
	public:
		static constexpr uint32_t kFrameSlotCount = 2;

		void PrepareFrame(
			const VansRenderViewSnapshot& view,
			const VansRenderSceneFrameSnapshot& sceneSnapshot,
			uint32_t frameSlotIndex,
			uint64_t frameSerial);
		bool UploadActiveCandidates(VansVKDevice& device);
		void MarkDispatched();

		bool HasActiveCandidates() const;
		uint32_t GetActiveCandidateCount() const;
		uint32_t GetActiveFrameSlotIndex() const { return m_ActiveFrameSlotIndex; }
		VansVKBuffer& GetActiveCullObjectBuffer();
		VansVKBuffer& GetActiveVisibilityBuffer();
		const VansMainCameraHiZCullSettings& GetActiveSettings() const { return m_Settings; }

		bool ShouldDraw(VansRenderProxyHandle proxy);
		VansMainCameraVisibilityDebugSnapshot GetDebugSnapshot() const;
		void Reset();
		void ReleaseGpuResources(VkDevice device);

	private:
		struct FrameSlot final
		{
			std::vector<VansMainCameraCullCandidate> candidates;
			std::vector<VansMainCameraCullObjectGPU> gpuObjects;
			std::vector<uint64_t> dispatchedNodeIds;
			VansVKBuffer cullObjectBuffer;
			VansVKBuffer visibilityBuffer;
			uint32_t bufferCapacity = 0;
			uint64_t dispatchedFrameSerial = 0;
			bool pendingVisibilityReadback = false;
		};

		FrameSlot& ActiveSlot();
		const FrameSlot& ActiveSlot() const;
		bool EnsureActiveGpuResources(VansVKDevice& device, uint32_t candidateCount);
		void ReleaseSlotGpuResources(FrameSlot& slot, VkDevice device);
		void ConsumeActiveReadback();
		void UpdateHistory(const VansRenderViewSnapshot& view);
		bool ShouldCullClassRunHiZ(VansMainCameraCullClass cullClass) const;
		void AppendCandidate(
			FrameSlot& slot,
			const VansRenderMainCameraCullInput& input,
			const glm::mat4& viewProjection);
		void PublishDebugSnapshot();

		std::array<FrameSlot, kFrameSlotCount> m_FrameSlots;
		VansMainCameraHiZCullSettings m_Settings;
		VansMainCameraHiZHistoryState m_History;
		VansMainCameraVisibilityStats m_Stats;
		std::vector<VansMainCameraHiZCulledNodeDebug> m_CulledDebugNodes;
		std::unordered_map<uint64_t, uint32_t> m_CurrentCullIndexByNodeId;
		std::unordered_map<uint64_t, bool> m_CurrentFrustumVisibilityByNodeId;
		std::unordered_map<uint64_t, bool> m_DrawVisibilityByNodeId;
		std::unordered_map<uint64_t, VansRenderBounds> m_PreviousCullBounds;
		std::unordered_map<uint64_t, uint32_t> m_ForceVisibleFramesByNodeId;
		std::atomic<uint64_t> m_DrawCallStatsPacked{ 0 };
		std::atomic<uint64_t> m_DrawCallStatsFrameSerial{ 0 };
		uint64_t m_LastConsumedFrameSerial = 0;
		uint64_t m_CurrentFrameSerial = 0;
		uint32_t m_FrameIndex = 0;
		uint32_t m_ActiveFrameSlotIndex = 0;

		mutable std::mutex m_DebugMutex;
		VansMainCameraVisibilityDebugSnapshot m_PublishedDebugSnapshot;
		uint64_t m_PublishedDebugFrameSerial = 0;
	};
}
