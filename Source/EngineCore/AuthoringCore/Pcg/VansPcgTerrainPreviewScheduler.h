#pragma once

#include <chrono>
#include <cstdint>

namespace Vans
{
// Terrain sculpting may emit one notification per brush dab. Keep those notifications
// lossless while limiting expensive spline-field and vegetation rebuild requests.
class VansPcgTerrainPreviewScheduler
{
public:
	using Clock = std::chrono::steady_clock;
	static constexpr auto MinimumRequestInterval = std::chrono::milliseconds(100);

	void NotifyHeightChanged(Clock::time_point now = Clock::now())
	{
		if (m_PendingHeightChanges == 0)
			m_FirstPendingChange = now;
		++m_PendingHeightChanges;
	}

	bool IsRequestDue(bool buildInFlight, Clock::time_point now = Clock::now()) const
	{
		if (buildInFlight || m_PendingHeightChanges == 0)
			return false;
		return !m_HasSubmittedRequest || now - m_LastSubmittedRequest >= MinimumRequestInterval;
	}

	std::uint32_t MarkRequestSubmitted(Clock::time_point now = Clock::now())
	{
		const std::uint32_t coalescedHeightChanges = m_PendingHeightChanges;
		m_PendingHeightChanges = 0;
		m_FirstPendingChange = {};
		m_LastSubmittedRequest = now;
		m_HasSubmittedRequest = true;
		++m_SubmittedRequests;
		return coalescedHeightChanges;
	}

	std::uint32_t PendingHeightChanges() const { return m_PendingHeightChanges; }
	std::uint64_t SubmittedRequests() const { return m_SubmittedRequests; }

private:
	Clock::time_point m_FirstPendingChange{};
	Clock::time_point m_LastSubmittedRequest{};
	std::uint32_t m_PendingHeightChanges = 0;
	std::uint64_t m_SubmittedRequests = 0;
	bool m_HasSubmittedRequest = false;
};
}
