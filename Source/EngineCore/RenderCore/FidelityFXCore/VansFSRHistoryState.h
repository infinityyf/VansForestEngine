#pragma once

#include "VansFSRTypes.h"

#include <cstdint>

namespace VansGraphics
{
	class VansFSRHistoryState
	{
	public:
		VansFSRHistoryState();

		void RequestReset(VansFSRResetReason reason);
		void ObserveFrame(
			std::uint64_t frameIndex,
			const void* cameraIdentity,
			std::uint32_t renderWidth,
			std::uint32_t renderHeight,
			std::uint32_t displayWidth,
			std::uint32_t displayHeight,
			VansFSRMode mode);
		bool IsResetPending() const { return m_PendingReasons != VansFSRResetReason::None; }
		VansFSRResetReason GetPendingReasons() const { return m_PendingReasons; }
		VansFSRResetReason GetLastConsumedReasons() const { return m_LastConsumedReasons; }

		// Reset 只能在成功提交到 SDK 后消费；失败帧继续保持请求。
		void OnDispatchSucceeded();

	private:
		VansFSRResetReason m_PendingReasons = VansFSRResetReason::FirstFrame;
		VansFSRResetReason m_LastConsumedReasons = VansFSRResetReason::None;
		std::uint64_t m_LastObservedFrameIndex = 0;
		const void* m_LastCameraIdentity = nullptr;
		std::uint32_t m_LastRenderWidth = 0;
		std::uint32_t m_LastRenderHeight = 0;
		std::uint32_t m_LastDisplayWidth = 0;
		std::uint32_t m_LastDisplayHeight = 0;
		VansFSRMode m_LastMode = VansFSRMode::MatchViewport;
		bool m_HasObservedFrame = false;
	};
}
