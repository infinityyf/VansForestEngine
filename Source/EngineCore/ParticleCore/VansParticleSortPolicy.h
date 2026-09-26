#pragma once

namespace VansGraphics
{
// 模拟顺序只由 ParticleCore 在发布不可变帧数据前执行。
enum class VansParticleSimulationOrder
{
    Stable,
    OldestFirst,
    NewestFirst
};

// 深度排序依赖当前 RenderView，只能由 RenderCore 消费已发布的帧数据执行。
enum class VansParticleRenderSortMode
{
    None,
    ByDistance
};
}
