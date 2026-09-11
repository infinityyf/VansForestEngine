#ifndef GI_RECEIVER_VISIBILITY_WORLD_GLSL
#define GI_RECEIVER_VISIBILITY_WORLD_GLSL
const uint GI_RECEIVER_WORLD_CAPACITY = 65536u;
const uint GI_RECEIVER_WORLD_MAX_AGE = 240u;
// 桶键仅用于寻址；命中后仍验证实际表面、区域、时间和 Probe，哈希冲突不能产生有效遮挡。
int GI_ReceiverWorldLevel(float footprint)
{
    // 接近屏幕锚点间距，避免一个粗桶把同一表面的许多有效锚点挤掉。
    return int(clamp(ceil(log2(max(footprint * 2.0, 0.000244140625))), -12.0, 8.0));
}
uint GI_ReceiverWorldBucket(ivec3 cell, int level, uint region)
{
    uvec3 p = uvec3(cell);
    uint hash = p.x * 73856093u ^ p.y * 19349663u ^ p.z * 83492791u ^
        uint(level + 12) * 2654435761u ^ region * 2246822519u;
    hash ^= hash >> 16u; hash *= 2246822519u; hash ^= hash >> 13u;
    return hash & (GI_RECEIVER_WORLD_CAPACITY - 1u);
}
ivec3 GI_ReceiverWorldCell(vec3 position, float cellSize)
{
    return ivec3(clamp(floor(position / cellSize), vec3(-2147483000.0), vec3(2147483000.0)));
}
uint GI_ReceiverWorldBucket(GIReceiverVisibilityRecord record)
{
    int level = GI_ReceiverWorldLevel(record.anchor.x);
    return GI_ReceiverWorldBucket(GI_ReceiverWorldCell(record.surface.xyz, exp2(float(level))),
        level, record.metadata.x);
}
#endif
