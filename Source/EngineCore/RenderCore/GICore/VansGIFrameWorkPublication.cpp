#include "VansGIFrameWorkPublication.h"

VansGraphics::VansGIFrameWork VansGraphics::VansGIFrameWorkPublication::Read() const
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Published;
}

void VansGraphics::VansGIFrameWorkPublication::PublishProbes(VansGIProbeFrameWork completed)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Published.probes = completed;
}

void VansGraphics::VansGIFrameWorkPublication::PublishConsumers(VansGIConsumerFrameWork completed)
{
    if (!completed.available) return;
    std::lock_guard<std::mutex> lock(m_Mutex);
    // 空闲维护可能按 slot 顺序回收，不能让较旧的完成帧覆盖较新的观测。
    if (!m_Published.consumers.available ||
        completed.renderFrameNumber >= m_Published.consumers.renderFrameNumber)
        m_Published.consumers = completed;
}
