#pragma once

#include <cstddef>

namespace Vans
{
struct VansGAFDiagnosticsSettings
{
	static constexpr std::size_t DefaultMaximumActionTraceEntries = 4096;
	static constexpr std::size_t DefaultMaximumRecentEventsPerAction = 64;
	static constexpr std::size_t DefaultMaximumCompletedActionSnapshots = 256;

	bool enabled = true;
	std::size_t maximumActionTraceEntries = DefaultMaximumActionTraceEntries;
	std::size_t maximumRecentEventsPerAction = DefaultMaximumRecentEventsPerAction;
	std::size_t maximumCompletedActionSnapshots = DefaultMaximumCompletedActionSnapshots;
};
}
