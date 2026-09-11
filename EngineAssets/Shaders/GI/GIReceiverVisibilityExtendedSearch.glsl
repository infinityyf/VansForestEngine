#ifndef GI_RECEIVER_VISIBILITY_EXTENDED_SEARCH_GLSL
#define GI_RECEIVER_VISIBILITY_EXTENDED_SEARCH_GLSL
// 优化 3：基础 2x2 未覆盖所有候选时才检查 3x3 的额外位置；世界空间容差不放宽。
void GI_SearchExtendedReceiverHistory(vec2 cachePixel, ivec2 base, vec4 position,
    vec3 normal, float material, float footprint, uint count, uint maximumAge,
    inout GIReceiverVisibilityRecord record, inout float bestDistance)
{
    if (record.metadata.z == (1u << count) - 1u) return;
    ivec2 origin = ivec2(round(cachePixel)) - 1;
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x)
    {
        ivec2 tap = origin + ivec2(x, y);
        if (all(greaterThanEqual(tap, base)) && all(lessThanEqual(tap, base + 1))) continue;
        GI_SearchReceiverHistoryTap(tap, position, normal, material, footprint,
            count, maximumAge, record, bestDistance);
    }
}
#endif
