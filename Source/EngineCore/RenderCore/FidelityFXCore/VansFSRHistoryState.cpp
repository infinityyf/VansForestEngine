#include "VansFSRHistoryState.h"

namespace VansGraphics
{
	VansFSRHistoryState::VansFSRHistoryState() = default;

	void VansFSRHistoryState::RequestReset(VansFSRResetReason reason)
	{
		m_PendingReasons |= reason;
	}

	void VansFSRHistoryState::ObserveFrame(
		std::uint64_t frameIndex,
		const void* cameraIdentity,
		std::uint32_t renderWidth,
		std::uint32_t renderHeight,
		std::uint32_t displayWidth,
		std::uint32_t displayHeight,
		VansFSRMode mode)
	{
		if (m_HasObservedFrame)
		{
			if (frameIndex != m_LastObservedFrameIndex + 1u)
				RequestReset(VansFSRResetReason::FrameDiscontinuity);
			if (cameraIdentity != m_LastCameraIdentity)
				RequestReset(VansFSRResetReason::CameraCut);
			if (renderWidth != m_LastRenderWidth || renderHeight != m_LastRenderHeight)
				RequestReset(VansFSRResetReason::RenderSizeChange);
			if (displayWidth != m_LastDisplayWidth || displayHeight != m_LastDisplayHeight)
				RequestReset(VansFSRResetReason::DisplaySizeChange);
			if (mode != m_LastMode)
				RequestReset(VansFSRResetReason::ModeChange);
		}

		m_LastObservedFrameIndex = frameIndex;
		m_LastCameraIdentity = cameraIdentity;
		m_LastRenderWidth = renderWidth;
		m_LastRenderHeight = renderHeight;
		m_LastDisplayWidth = displayWidth;
		m_LastDisplayHeight = displayHeight;
		m_LastMode = mode;
		m_HasObservedFrame = true;
	}

	void VansFSRHistoryState::OnDispatchSucceeded()
	{
		m_LastConsumedReasons = m_PendingReasons;
		m_PendingReasons = VansFSRResetReason::None;
	}
}
