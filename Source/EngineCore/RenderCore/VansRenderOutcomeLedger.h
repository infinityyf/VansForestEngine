#pragma once

#include "VansRenderFrame.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace VansGraphics
{
	enum class VansRenderFrameStatus
	{
		PresentQueued,
		SubmittedWithoutPresent,
		SkippedMinimized,
		SkippedSurfaceUnavailable,
		RecoverableFailure,
		FatalProtocolViolation,
		FatalDeviceLost,
		Stopped
	};

	struct VansRenderFrameOutcome final
	{
		VansRenderWorkSerial workSerial;
		VansRenderFrameId frameId;
		VansRenderFrameStatus status = VansRenderFrameStatus::RecoverableFailure;
		std::optional<VansGpuSubmitSerial> gpuSubmitSerial;
		VansSurfaceEpoch surfaceEpoch;
		std::string error;

		bool ReleasesLeadCredit() const;
	};

	enum class VansRenderOutcomeWaitStatus
	{
		OutcomeAvailable,
		OutcomeEvicted,
		Stopped,
		Fatal
	};

	struct VansRenderOutcomeWaitResult final
	{
		VansRenderOutcomeWaitStatus status = VansRenderOutcomeWaitStatus::Stopped;
		std::optional<VansRenderFrameOutcome> outcome;
		std::string error;
	};

	// Thread-safe source of truth for ordered CPU render work and terminal frame
	// outcomes. Counters are only derived acceleration data; outcomes remain the
	// authoritative record and are retained in a bounded diagnostics ring.
	class VansRenderOutcomeLedger final
	{
	public:
		explicit VansRenderOutcomeLedger(
			std::size_t maxPendingFrames = 2,
			std::size_t outcomeCapacity = 16);

		VansRenderOutcomeLedger(const VansRenderOutcomeLedger&) = delete;
		VansRenderOutcomeLedger& operator=(const VansRenderOutcomeLedger&) = delete;

		bool TryAcceptFrame(VansRenderWorkSerial workSerial, VansRenderFrameId frameId);
		bool PublishOutcome(VansRenderFrameOutcome outcome);
		std::optional<VansRenderFrameOutcome> FindOutcome(VansRenderFrameId frameId) const;
		VansRenderOutcomeWaitResult WaitForOutcome(VansRenderFrameId frameId);

		bool LeadCreditReleasedFor(VansRenderFrameId frameId) const;
		std::size_t PendingFrameCount() const;
		std::size_t RetainedOutcomeCount() const;

		void SignalStopped();
		void SignalFatal(std::string error);

	private:
		struct PendingFrame final
		{
			VansRenderWorkSerial workSerial;
			VansRenderFrameId frameId;
		};

		std::optional<VansRenderFrameOutcome> FindOutcomeLocked(VansRenderFrameId frameId) const;

		const std::size_t m_MaxPendingFrames;
		const std::size_t m_OutcomeCapacity;
		mutable std::mutex m_Mutex;
		std::condition_variable m_Condition;
		std::deque<PendingFrame> m_PendingFrames;
		std::deque<VansRenderFrameOutcome> m_Outcomes;
		std::optional<VansRenderWorkSerial> m_LastAcceptedWorkSerial;
		std::optional<VansRenderFrameId> m_LastAcceptedFrameId;
		std::optional<VansRenderFrameId> m_LastLeadCreditReleasedFrame;
		std::optional<VansRenderFrameId> m_LastEvictedFrame;
		bool m_Stopped = false;
		bool m_Fatal = false;
		std::string m_FatalError;
	};
}
