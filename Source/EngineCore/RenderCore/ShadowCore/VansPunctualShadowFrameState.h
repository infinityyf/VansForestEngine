#pragma once

#include "VansPunctualShadowManager.h"
#include "../VansRenderSceneSnapshot.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace VansGraphics
{
	// Render-backend-owned punctual-shadow runtime. Main publishes only immutable
	// inputs; this object owns residency, atlas allocation, caster history, jobs,
	// submission acknowledgement and the thread-safe diagnostics snapshot.
	class VansPunctualShadowFrameState final
	{
	public:
		VansPunctualShadowFrameState();

		VansPunctualShadowFrameState(const VansPunctualShadowFrameState&) = delete;
		VansPunctualShadowFrameState& operator=(const VansPunctualShadowFrameState&) = delete;

		bool PrepareFrame(VansRenderSceneFrameSnapshot& snapshot, std::uint64_t frameIndex);
		void NotifyRenderJobsSubmitted();

		const std::vector<VansPunctualShadowGPU>& GetGPUShadowData() const
		{
			return m_Manager.GetGPUShadowData();
		}
		const std::vector<VansPunctualShadowViewGPU>& GetGPUShadowViews() const
		{
			return m_Manager.GetGPUShadowViews();
		}

		void RequestDebugPreview();
		bool ConsumeDebugPreviewRefreshRequest();
		VansPunctualShadowDebugSnapshot CaptureDebugSnapshot() const;
		std::uint32_t GetTotalAtlasPages() const;

	private:
		bool ValidateFrameInput(const VansRenderSceneFrameSnapshot& snapshot) const;
		void UpdateCasterState(const std::vector<VansRenderPunctualShadowCasterInput>& casters);
		void BuildCasterLists(std::vector<VansPunctualShadowRenderJob>& jobs) const;
		void ResolveLightMetaIndices(
			const std::vector<VansPunctualShadowLightInput>& inputs,
			VansRenderLightFrameData& lightFrame) const;
		void PublishDebugSnapshot();

		VansPunctualShadowManager m_Manager;
		std::unordered_map<
			VansRenderProxyHandle,
			VansRenderPunctualShadowCasterInput,
			VansRenderProxyHandleHash> m_Casters;
		std::uint64_t m_SceneEpoch = 0;
		bool m_HasSceneEpoch = false;

		std::atomic_bool m_DebugPreviewRequested{ false };
		std::uint32_t m_DebugPreviewHeartbeat = 0;
		bool m_DebugPreviewForceRefresh = true;
		mutable std::mutex m_DebugSnapshotMutex;
		VansPunctualShadowDebugSnapshot m_PublishedDebugSnapshot;
		std::uint32_t m_PublishedTotalAtlasPages = 0;
	};
}
