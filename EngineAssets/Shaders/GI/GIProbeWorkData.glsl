#ifndef GI_PROBE_WORK_DATA_GLSL_INCLUDED
#define GI_PROBE_WORK_DATA_GLSL_INCLUDED
#include "GIProbeFeedbackStatus.glsl"

const uint GI_WORK_RESET_GEOMETRY = 1u;
struct GIProbeWorkEntry
{
    uvec4 scheduling;
    vec4 lighting; // 主/散射光源变化量、主/散射成功发布年龄（秒）。
};
// 只对确定的光源变化启用时间响应；静止光照仍使用原有样本权重。
float GI_LightingHistoryBlend(float changeAmount, float publicationAge)
{
    if (changeAmount <= 0.0) return 0.0;
    // 约 0.125 秒时间常数，而非等待八次低频 probe 更新；突变立即响应。
    return clamp(max(changeAmount, 1.0-exp2(-max(publicationAge,0.0)*11.5415603)),0.0,1.0);
}

// 每项都是完整 probe 更新，统一射线步长，共享有上限的射线缓存。
layout(set = GI_WORK_SET, binding = GI_WORK_BINDING, std430) readonly buffer GIProbeWorkBuffer
{
    uvec4 header; // 更新数、每 probe 射线数、区域 ID、自动布局标志
    GIProbeWorkEntry entries[]; // 32 B；反馈继续使用原有 16 B 身份。
} giProbeWork;

ivec3 GI_WorkProbeCoordinate(uint physicalIndex, ivec3 dimensions)
{
    uint width = uint(dimensions.x);
    uint height = uint(dimensions.y);
    return ivec3(physicalIndex % width, (physicalIndex / width) % height, physicalIndex / (width * height));
}

uint GI_WorkRayOffset(uint entry)
{
    return entry * giProbeWork.header.y;
}
// 仅调试查询使用；生产更新通过 dispatch ID 直接寻址。
uint GI_FindWorkEntry(uint physicalIndex)
{
    uint first = 0u, end = giProbeWork.header.x;
    uint limit = end;
    while (first < end)
    {
        uint middle = first + (end - first) / 2u;
        if (giProbeWork.entries[middle].scheduling.x < physicalIndex) first = middle + 1u;
        else end = middle;
    }
    return first < limit && giProbeWork.entries[first].scheduling.x == physicalIndex ? first : 0xffffffffu;
}
#endif
