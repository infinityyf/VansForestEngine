#pragma once
#include "VansGIWorldData.h"
#include <cstdint>

namespace VansGraphics
{
// 每次查询数据发布只保留两类保守范围，大小不随砖或实例数量增长。
struct VansGIWorldQueryChanges
{
    std::optional<GIWorldBounds> geometry;
    std::optional<GIWorldBounds> residency;
    static void Include(std::optional<GIWorldBounds>& target, const GIWorldBounds& bounds)
    {
        if (!target) target = bounds;
        else
        {
            target->minimum = glm::min(target->minimum, bounds.minimum);
            target->maximum = glm::max(target->maximum, bounds.maximum);
        }
    }
    bool Empty() const { return !geometry && !residency; }
};

// generation 区分资源 owner；sequence 只标记真正录制的查询数据发布。
// 消费者漏过多个发布时必须完整失效，不能把最后一个范围误当成全部变化。
struct VansGIWorldQueryPublication
{
    uint64_t generation = 0, sequence = 0;
    VansGIWorldQueryChanges changes;
};

struct alignas(16) VansGIReceiverInvalidation
{
    glm::uvec4 regions{0}; // x：完整区域，y：按世界查询范围过滤的区域。
    glm::vec4 geometryMin{0}, geometryMax{0};
    glm::vec4 residencyMin{0}, residencyMax{0}; // minimum.w 表示范围是否存在。
    bool Any() const { return regions.x != 0 || regions.y != 0; }
};
static_assert(sizeof(VansGIReceiverInvalidation) == 80);
}
