#include "VansAuthoringHistory.h"

#include <atomic>

namespace Vans
{
namespace
{
std::atomic<VansHistorySequence>& Revision()
{
	static std::atomic<VansHistorySequence> revision{ 0 };
	return revision;
}
}

VansHistorySequence VansAuthoringHistory::IssueEditSequence()
{
	return Revision().fetch_add(1, std::memory_order_relaxed) + 1;
}

VansHistorySequence VansAuthoringHistory::CurrentRevision()
{
	return Revision().load(std::memory_order_relaxed);
}
}
