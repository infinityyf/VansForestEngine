#include "VansRenderOutcomeLedger.h"

#include <algorithm>
#include <utility>

bool VansGraphics::VansRenderFrameOutcome::ReleasesLeadCredit() const
{
	switch (status)
	{
	case VansRenderFrameStatus::PresentQueued:
	case VansRenderFrameStatus::SubmittedWithoutPresent:
	case VansRenderFrameStatus::SkippedMinimized:
	case VansRenderFrameStatus::SkippedSurfaceUnavailable:
	case VansRenderFrameStatus::RecoverableFailure:
		return true;
	case VansRenderFrameStatus::FatalProtocolViolation:
	case VansRenderFrameStatus::FatalDeviceLost:
	case VansRenderFrameStatus::Stopped:
		return false;
	}
	return false;
}

VansGraphics::VansRenderOutcomeLedger::VansRenderOutcomeLedger(
	std::size_t maxPendingFrames,
	std::size_t outcomeCapacity)
	: m_MaxPendingFrames((std::max)(std::size_t(1), maxPendingFrames)),
	  m_OutcomeCapacity((std::max)(std::size_t(1), outcomeCapacity))
{
}

bool VansGraphics::VansRenderOutcomeLedger::TryAcceptFrame(
	VansRenderWorkSerial workSerial,
	VansRenderFrameId frameId)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	if (m_Stopped || m_Fatal || m_PendingFrames.size() >= m_MaxPendingFrames)
		return false;
	if (m_LastAcceptedWorkSerial.has_value() &&
		workSerial.Value() <= m_LastAcceptedWorkSerial->Value())
	{
		return false;
	}
	if (m_LastAcceptedFrameId.has_value())
	{
		if (frameId.Value() <= m_LastAcceptedFrameId->Value())
			return false;
		if (!m_LastLeadCreditReleasedFrame.has_value() ||
			m_LastLeadCreditReleasedFrame->Value() < m_LastAcceptedFrameId->Value())
		{
			return false;
		}
	}

	m_PendingFrames.push_back({ workSerial, frameId });
	m_LastAcceptedWorkSerial = workSerial;
	m_LastAcceptedFrameId = frameId;
	return true;
}

bool VansGraphics::VansRenderOutcomeLedger::PublishOutcome(
	VansRenderFrameOutcome outcome)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	if (m_PendingFrames.empty())
		return false;

	const PendingFrame& pending = m_PendingFrames.front();
	if (pending.workSerial != outcome.workSerial || pending.frameId != outcome.frameId)
		return false;

	m_PendingFrames.pop_front();
	if (outcome.ReleasesLeadCredit())
		m_LastLeadCreditReleasedFrame = outcome.frameId;
	if (outcome.status == VansRenderFrameStatus::FatalProtocolViolation ||
		outcome.status == VansRenderFrameStatus::FatalDeviceLost)
	{
		m_Fatal = true;
		m_FatalError = outcome.error;
	}
	else if (outcome.status == VansRenderFrameStatus::Stopped)
	{
		m_Stopped = true;
	}

	m_Outcomes.emplace_back(std::move(outcome));
	while (m_Outcomes.size() > m_OutcomeCapacity)
	{
		m_LastEvictedFrame = m_Outcomes.front().frameId;
		m_Outcomes.pop_front();
	}
	m_Condition.notify_all();
	return true;
}

std::optional<VansGraphics::VansRenderFrameOutcome>
VansGraphics::VansRenderOutcomeLedger::FindOutcomeLocked(
	VansRenderFrameId frameId) const
{
	for (const VansRenderFrameOutcome& outcome : m_Outcomes)
	{
		if (outcome.frameId == frameId)
			return outcome;
	}
	return std::nullopt;
}

std::optional<VansGraphics::VansRenderFrameOutcome>
VansGraphics::VansRenderOutcomeLedger::FindOutcome(VansRenderFrameId frameId) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return FindOutcomeLocked(frameId);
}

VansGraphics::VansRenderOutcomeWaitResult
VansGraphics::VansRenderOutcomeLedger::WaitForOutcome(VansRenderFrameId frameId)
{
	std::unique_lock<std::mutex> lock(m_Mutex);
	m_Condition.wait(lock, [&]
	{
		return FindOutcomeLocked(frameId).has_value() || m_Stopped || m_Fatal ||
			(m_LastEvictedFrame.has_value() &&
				m_LastEvictedFrame->Value() >= frameId.Value());
	});

	if (auto outcome = FindOutcomeLocked(frameId))
		return { VansRenderOutcomeWaitStatus::OutcomeAvailable, std::move(outcome), {} };
	if (m_Fatal)
		return { VansRenderOutcomeWaitStatus::Fatal, std::nullopt, m_FatalError };
	if (m_LastEvictedFrame.has_value() && m_LastEvictedFrame->Value() >= frameId.Value())
		return { VansRenderOutcomeWaitStatus::OutcomeEvicted, std::nullopt, {} };
	return { VansRenderOutcomeWaitStatus::Stopped, std::nullopt, {} };
}

bool VansGraphics::VansRenderOutcomeLedger::LeadCreditReleasedFor(
	VansRenderFrameId frameId) const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_LastLeadCreditReleasedFrame.has_value() &&
		m_LastLeadCreditReleasedFrame->Value() >= frameId.Value();
}

std::size_t VansGraphics::VansRenderOutcomeLedger::PendingFrameCount() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_PendingFrames.size();
}

std::size_t VansGraphics::VansRenderOutcomeLedger::RetainedOutcomeCount() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	return m_Outcomes.size();
}

void VansGraphics::VansRenderOutcomeLedger::SignalStopped()
{
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Stopped = true;
	}
	m_Condition.notify_all();
}

void VansGraphics::VansRenderOutcomeLedger::SignalFatal(std::string error)
{
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Fatal = true;
		m_FatalError = std::move(error);
	}
	m_Condition.notify_all();
}
