#ifndef GI_RECEIVER_VISIBILITY_PERSISTENT_REUSE_GLSL
#define GI_RECEIVER_VISIBILITY_PERSISTENT_REUSE_GLSL
#include "GIReceiverVisibilityWorld.glsl"
layout(set=1,binding=13,std430) readonly buffer WorldCache
{
    uvec4 worldHeader;
    GIReceiverVisibilityRecord worldRecords[];
};
// 优化 5 读取入口：仅历史未补齐时查固定容量缓存；最多三个尺度各八个相邻桶。
void GI_ReusePersistentReceiverHistory(vec4 position, vec3 normal, float material,
    float footprint, uint count, uint maximumAge,
    inout GIReceiverVisibilityRecord record, inout float bestDistance)
{
    if (frame.z == 0u || worldHeader.x == 0u || record.metadata.z == (1u << count) - 1u) return;
    int centerLevel = GI_ReceiverWorldLevel(footprint);
    for (int offset = 0; offset < 3; ++offset)
    {
        int level = centerLevel + (offset == 1 ? -1 : (offset == 2 ? 1 : 0));
        if (level < -12 || level > 8) continue;
        float cellSize = exp2(float(level));
        ivec3 base = GI_ReceiverWorldCell(position.xyz - vec3(cellSize * 0.5), cellSize);
        for (int z = 0; z < 2; ++z) for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
        {
            uint bucket = GI_ReceiverWorldBucket(base + ivec3(x,y,z), level, record.metadata.x);
            GI_ConsiderReceiverHistory(worldRecords[bucket], position, normal, material,
                footprint, count, min(maximumAge, GI_RECEIVER_WORLD_MAX_AGE), record, bestDistance);
        }
        if (record.metadata.z == (1u << count) - 1u) return;
    }
}
#endif
