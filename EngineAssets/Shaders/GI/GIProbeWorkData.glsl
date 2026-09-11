#ifndef GI_PROBE_WORK_DATA_GLSL_INCLUDED
#define GI_PROBE_WORK_DATA_GLSL_INCLUDED

// 每项都是完整 probe 更新，统一射线步长，共享有上限的射线缓存。
layout(set = GI_WORK_SET, binding = GI_WORK_BINDING, std430) readonly buffer GIProbeWorkBuffer
{
    uvec4 header; // 更新数、每 probe 射线数、区域 ID、自动布局标志
    uvec4 entries[]; // physical ID, elapsed seconds (float bits), rotation sequence, flags
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
        if (giProbeWork.entries[middle].x < physicalIndex) first = middle + 1u;
        else end = middle;
    }
    return first < limit && giProbeWork.entries[first].x == physicalIndex ? first : 0xffffffffu;
}
#endif
