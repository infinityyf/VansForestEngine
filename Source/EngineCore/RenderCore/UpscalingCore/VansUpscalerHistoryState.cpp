#include "VansUpscalerHistoryState.h"

namespace VansGraphics
{
	void VansUpscalerHistoryState::RequestReset(VansUpscalerResetReason reason)
	{
		m_PendingReasons |= reason;
	}

	void VansUpscalerHistoryState::ObserveFrame(
		std::uint64_t frameIndex,
		const void* cameraIdentity,
		VansExtent2D renderExtent,
		VansExtent2D outputExtent,
		VansUpscalerBackend backend,
		VansUpscaleQualityMode quality)
	{
		if (m_HasObservedFrame)
		{
			if (frameIndex != m_LastObservedFrameIndex + 1u)
				RequestReset(VansUpscalerResetReason::FrameDiscontinuity);
			if (cameraIdentity != m_LastCameraIdentity)
				RequestReset(VansUpscalerResetReason::CameraCut);
			if (renderExtent != m_LastRenderExtent)
				RequestReset(VansUpscalerResetReason::RenderSizeChange);
			if (outputExtent != m_LastOutputExtent)
				RequestReset(VansUpscalerResetReason::OutputSizeChange);
			if (backend != m_LastBackend)
				RequestReset(VansUpscalerResetReason::BackendChange);
			if (quality != m_LastQuality)
				RequestReset(VansUpscalerResetReason::QualityChange);
		}

		m_LastObservedFrameIndex = frameIndex;
		m_LastCameraIdentity = cameraIdentity;
		m_LastRenderExtent = renderExtent;
		m_LastOutputExtent = outputExtent;
		m_LastBackend = backend;
		m_LastQuality = quality;
		m_HasObservedFrame = true;
	}

	void VansUpscalerHistoryState::OnTemporalDispatchSucceeded()
	{
		m_LastConsumedReasons = m_PendingReasons;
		m_PendingReasons = VansUpscalerResetReason::None;
	}

	void VansUpscalerHistoryState::ClearForOffBackend()
	{
		m_LastConsumedReasons = VansUpscalerResetReason::None;
		m_PendingReasons |= VansUpscalerResetReason::FirstFrame;
	}
}
