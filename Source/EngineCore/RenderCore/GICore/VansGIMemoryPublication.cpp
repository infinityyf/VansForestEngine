#include "VansGIMemoryPublication.h"
#include <algorithm>
#include <cassert>

VansGraphics::VansGIMemorySnapshot VansGraphics::VansGIMemoryPublication::Read() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Snapshot;
}

void VansGraphics::VansGIMemoryPublication::PublishProbes(VansGIProbeMemory current, uint64_t retiredBytes)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Snapshot.probes = current;
    m_Snapshot.retiredAllocationBytes += retiredBytes;
    UpdatePeak();
}

void VansGraphics::VansGIMemoryPublication::PublishConsumer(
    VansGIMemoryUsage VansGIMemorySnapshot::* component, VansGIMemoryUsage current)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Snapshot.*component = current;
    UpdatePeak();
}

void VansGraphics::VansGIMemoryPublication::Retire(uint64_t bytes)
{
    if (!bytes) return;
    std::lock_guard<std::mutex> lock(m_Mutex);
    assert(bytes <= m_Snapshot.retiredAllocationBytes);
    m_Snapshot.retiredAllocationBytes -= bytes;
}

void VansGraphics::VansGIMemoryPublication::UpdatePeak()
{
    m_Snapshot.publishedRetainedPeakBytes = std::max(m_Snapshot.publishedRetainedPeakBytes,
        m_Snapshot.ActiveAllocationBytes() + m_Snapshot.retiredAllocationBytes);
}
