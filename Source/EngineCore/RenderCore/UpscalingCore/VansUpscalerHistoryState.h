#pragma once

#include "VansUpscalerTypes.h"

#include <cstdint>

namespace VansGraphics
{
	class VansUpscalerHistoryState
	{
	public:
		void RequestReset(VansUpscalerResetReason reason);
		void ObserveFrame(
			std::uint64_t frameIndex,
			std::uint64_t cameraIdentity,
			VansExtent2D renderExtent,
			VansExtent2D outputExtent,
			VansUpscalerBackend backend,
			VansUpscaleQualityMode quality);
		bool IsResetPending() const
		{
			return m_PendingReasons != VansUpscalerResetReason::None;
		}
		VansUpscalerResetReason GetPendingReasons() const { return m_PendingReasons; }
		VansUpscalerResetReason GetLastConsumedReasons() const { return m_LastConsumedReasons; }
		void OnTemporalDispatchSucceeded();
		void ClearForOffBackend();

	private:
		VansUpscalerResetReason m_PendingReasons = VansUpscalerResetReason::FirstFrame;
		VansUpscalerResetReason m_LastConsumedReasons = VansUpscalerResetReason::None;
		std::uint64_t m_LastObservedFrameIndex = 0;
		std::uint64_t m_LastCameraIdentity = 0;
		VansExtent2D m_LastRenderExtent;
		VansExtent2D m_LastOutputExtent;
		VansUpscalerBackend m_LastBackend = VansUpscalerBackend::Off;
		VansUpscaleQualityMode m_LastQuality = VansUpscaleQualityMode::NativeAA;
		bool m_HasObservedFrame = false;
	};
}
