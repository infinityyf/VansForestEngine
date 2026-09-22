#ifndef GI_REGION_BLEND_GLSL
#define GI_REGION_BLEND_GLSL
// 调用者提供未乘区域 fade 的 E/pi 查询与相同量纲的天空查询。
// 空间边界权重与 probe 发布/遮挡支持分离，合法黑色不会触发天空补光。
#ifndef GI_BLEND_REGION_COUNT
#define GI_BLEND_REGION_COUNT uint(regionInfo.x)
#define GI_BLEND_REGION_MIN(region) regions[region].volumeMin.xyz
#define GI_BLEND_REGION_SIZE(region) regions[region].volumeSizeAndBias.xyz
#define GI_BLEND_REGION_FADE(region) regions[region].traceParams.z
#define GI_BLEND_REGION_PRIORITY(region) regions[region].gridDimensionsAndPriority.w
#endif
#ifndef GI_BLEND_REGION_ALLOWED
#define GI_BLEND_REGION_ALLOWED(region) true
#endif
#ifndef GI_BLEND_NORMALIZE_FEEDBACK
#define GI_BLEND_NORMALIZE_FEEDBACK false
#endif
vec3 GI_BlendRegionLightingMode(vec3 worldPosition, vec3 normal, float normalBiasScale,
    bool allowSkyForUnpublished)
{
    uint order[8];
    float weights[8];
    uint count = 0u;
    for (uint region = 0u; region < min(GI_BLEND_REGION_COUNT, 8u); ++region)
    {
        if (!GI_BLEND_REGION_ALLOWED(region)) continue;
        vec3 minimum = GI_BLEND_REGION_MIN(region), size = GI_BLEND_REGION_SIZE(region);
#ifdef GI_PROBE_LAYOUT_DATA_GLSL
        GI_LayoutBlendBounds(region, minimum, size);
#endif
        if (!GI_IsInsideVolume(worldPosition, minimum, size)) continue;
        float weight = GI_VolumeFade(worldPosition, minimum, size, max(GI_BLEND_REGION_FADE(region), 0.0));
        if (weight <= 0.0) continue;
        uint slot = count;
        while (slot > 0u)
        {
            uint previous = order[slot - 1u];
            float priority = GI_BLEND_REGION_PRIORITY(region);
            float previousPriority = GI_BLEND_REGION_PRIORITY(previous);
            if (priority < previousPriority || (priority == previousPriority && weight <= weights[slot - 1u])) break;
            order[slot] = previous; weights[slot] = weights[slot - 1u]; --slot;
        }
        order[slot] = region; weights[slot] = weight; ++count;
    }
    vec3 lighting = vec3(0.0);
    float remaining = 1.0;
    float skyWeight = 1.0;
    float resolvedWeight = 0.0;
    for (uint slot = 0u; slot < count && remaining > 0.0; ++slot)
    {
        vec4 sampleValue = GI_SAMPLE_REGION(order[slot], worldPosition, normal, normalBiasScale);
        if (allowSkyForUnpublished && sampleValue.a <= 0.0)
        {
            // 叶片可能落在区域范围内但尚未有已发布 probe；保留天空残差，
            // 避免把“未发布”错误解释成封闭空间的纯黑。
            continue;
        }
        float weight = remaining * weights[slot] * clamp(sampleValue.a, 0.0, 1.0);
        lighting += weight * sampleValue.rgb;
        resolvedWeight += weight;
        remaining -= weight;
        // 缺少发布数据只允许查询有效粗级，不能因此把已覆盖的封闭区域照成天空。
        skyWeight *= 1.0 - weights[slot];
    }
    // 反弹只混合已发布场景光照；归一化避免每轮反馈重复吸收区域 fade。
    // 没有有效数据时不注入无遮挡天空，合法黑色仍有发布权重。
    if (GI_BLEND_NORMALIZE_FEEDBACK)
        return resolvedWeight > 0.0 ? lighting / resolvedWeight : vec3(0.0);
    if (remaining > 0.0) lighting += min(remaining, skyWeight) * GI_SAMPLE_SKY(normal);
    return lighting;
}

vec3 GI_BlendRegionLighting(vec3 worldPosition, vec3 normal, float normalBiasScale)
{
    return GI_BlendRegionLightingMode(worldPosition, normal, normalBiasScale, false);
}
#endif
