#pragma once
#include "VansGIFrameWork.h"
#include <mutex>

namespace VansGraphics
{
    // 只发布调用方已经通过既有完成条件确认的值，不拥有 fence 或第二条时间轴。
    class VansGIFrameWorkPublication
    {
    public:
        VansGIFrameWork Read() const;
        void PublishProbes(VansGIProbeFrameWork completed);
        void PublishConsumers(VansGIConsumerFrameWork completed);
    private:
        mutable std::mutex m_Mutex;
        VansGIFrameWork m_Published;
    };
}
