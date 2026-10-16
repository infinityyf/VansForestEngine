#include "VansGIFrameCost.h"
#include "../../Util/VansProfiler.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <tuple>
#include <vector>

namespace VansGraphics
{
    GIFrameCostSnapshot BuildGIFrameCostSnapshot(const Vans::ProfileFrame& frame, bool capturing)
    {
        GIFrameCostSnapshot result;
        result.capturing = capturing;
        result.frameIndex = frame.frameIndex;
        result.hasFrame = frame.eventCount != 0 || frame.gpuExpected;
        if (!result.hasFrame || frame.eventCount > Vans::ProfileFrame::MAX_EVENTS ||
            frame.trackCount > Vans::ProfileFrame::MAX_TRACKS || frame.overflow) return result;
        result.cpuValid = frame.droppedCpuEvents == 0;
        result.gpuValid = frame.gpuExpected && frame.gpuComplete && frame.droppedGpuEvents == 0;
        struct Interval { uint32_t track; size_t stage; double begin, end; };
        std::vector<Interval> intervals;
        intervals.reserve(frame.eventCount);
        for (uint32_t index = 0; index < frame.eventCount; ++index)
        {
            const auto& event = frame.events[index];
            const auto end = std::find(std::begin(event.name), std::end(event.name), '\0');
            const std::string_view name(event.name, size_t(end - event.name));
            const bool gpu = (event.flags & Vans::ProfileEventFlagGpu) != 0;
            const auto stage = std::find_if(GICostStages.begin(), GICostStages.end(),
                [&](const auto& item) { return name == item.scope; });
            if (stage == GICostStages.end()) continue;
            const bool valid = gpu == stage->gpu && event.trackId > 0 && event.trackId <= frame.trackCount &&
                frame.tracks[event.trackId - 1].trackId == event.trackId &&
                frame.tracks[event.trackId - 1].type == (gpu ? Vans::ProfileTrackType::GpuQueue : Vans::ProfileTrackType::CpuThread) &&
                std::isfinite(event.startUs) && std::isfinite(event.endUs) && event.endUs >= event.startUs &&
                std::isfinite(event.endUs - event.startUs) &&
                (event.flags & Vans::ProfileEventFlagOverflow) == 0;
            if (!valid)
            {
                if (stage->gpu) result.gpuValid = false; else result.cpuValid = false;
                continue;
            }
            intervals.push_back({event.trackId, size_t(stage - GICostStages.begin()), event.startUs, event.endUs});
        }
        intervals.erase(std::remove_if(intervals.begin(), intervals.end(), [&](const auto& interval) {
            return !(GICostStages[interval.stage].gpu ? result.gpuValid : result.cpuValid);
        }), intervals.end());
        result.cpuValid = std::any_of(intervals.begin(), intervals.end(),
            [](const auto& interval) { return !GICostStages[interval.stage].gpu; });
        result.gpuValid = std::any_of(intervals.begin(), intervals.end(),
            [](const auto& interval) { return GICostStages[interval.stage].gpu; });
        for (const auto& interval : intervals) ++result.stages[interval.stage].samples;
        // 不能相加父阶段与子阶段，也不能把异步队列重叠误当成关键路径缩短。
        std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) {
            return std::tie(a.track, a.begin, a.end) < std::tie(b.track, b.begin, b.end);
        });
        uint32_t track = 0; double end = 0.0;
        for (const auto& interval : intervals)
        {
            const double covered = track == interval.track ? std::max(interval.begin, end) : interval.begin;
            const double duration = std::max(0.0, interval.end - covered) / 1000.0;
            (GICostStages[interval.stage].gpu ? result.gpuScopedMilliseconds : result.cpuScopedMilliseconds) += duration;
            end = track == interval.track ? std::max(end, interval.end) : interval.end;
            track = interval.track;
        }
        std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) {
            return std::tie(a.stage, a.track, a.begin, a.end) < std::tie(b.stage, b.track, b.begin, b.end);
        });
        track = 0; size_t previousStage = GICostStages.size(); end = 0.0;
        for (const auto& interval : intervals)
        {
            const bool same = track == interval.track && previousStage == interval.stage;
            result.stages[interval.stage].milliseconds +=
                std::max(0.0, interval.end - (same ? std::max(interval.begin, end) : interval.begin)) / 1000.0;
            end = same ? std::max(end, interval.end) : interval.end;
            track = interval.track; previousStage = interval.stage;
        }
        return result;
    }
}
