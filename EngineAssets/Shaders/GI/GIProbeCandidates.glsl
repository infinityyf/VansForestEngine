#ifndef GI_PROBE_CANDIDATES_GLSL
#define GI_PROBE_CANDIDATES_GLSL
// 光照查询与接收点可见性预计算共用候选集，避免两条路径在稀疏层级边界选到不同 probe。
struct GIProbeCandidates
{
    vec3 samplePosition;
    uint probes[8];
    vec3 positions[8];
    float weights[8];
};
GIProbeCandidates GI_EmptyProbeCandidates(vec3 samplePosition)
{
    GIProbeCandidates result;
    result.samplePosition = samplePosition;
    for (uint i = 0u; i < 8u; ++i)
    { result.probes[i] = 0xffffffffu; result.positions[i] = vec3(0.0); result.weights[i] = 0.0; }
    return result;
}
#ifdef GI_PROBE_LAYOUT_DATA_GLSL
GIProbeCandidates GI_GatherCellCandidates(uint region, uint cellAddress, vec3 samplePos)
{
    GIProbeCandidates result = GI_EmptyProbeCandidates(samplePos);
    vec4 cell = GI_LayoutCellMinimum(cellAddress);
    vec3 fraction = clamp((samplePos - cell.xyz) / cell.w, vec3(0.0), vec3(1.0));
    vec4 lower, upper;
    GI_LayoutCellWeights(cellAddress, fraction, lower, upper);
    for (uint i = 0u; i < 8u; ++i)
    {
        uint probe = GI_LayoutCellProbe(region, cellAddress, i);
        if (probe == GI_INVALID_ADDRESS) continue;
        result.probes[i] = probe;
        result.positions[i] = GI_LayoutPosition(region, probe).xyz;
        result.weights[i] = i < 4u ? lower[i] : upper[i - 4u];
    }
    return result;
}
bool GI_HasCandidateAddresses(GIProbeCandidates candidates)
{
    for (uint i = 0u; i < 8u; ++i)
        if (candidates.probes[i] != GI_INVALID_ADDRESS && candidates.weights[i] > 0.0) return true;
    return false;
}
#endif
GIProbeCandidates GI_GatherProbeCandidates(uint region, ivec3 counts,
    vec3 worldPos, vec3 N, vec3 volumeMin, vec3 volumeSize, float normalBias)
{
#ifdef GI_RECEIVER_NORMAL_BIAS
    normalBias=GI_RECEIVER_NORMAL_BIAS(region,normalBias);
#endif
    GIProbeCandidates result = GI_EmptyProbeCandidates(worldPos);
#ifdef GI_PROBE_LAYOUT_DATA_GLSL
    if (GI_LayoutRegionIsSparse(region))
    {
        vec3 samplePos;
        uint node = GI_LayoutLocateSampleNode(region, worldPos, N, normalBias, samplePos);
        result.samplePosition = samplePos;
        if (node == GI_INVALID_ADDRESS) return result;
        uint leaf = GI_LayoutNodeLeaf(node);
        if (leaf != GI_INVALID_ADDRESS)
        {
            result = GI_GatherCellCandidates(region, leaf, samplePos);
            // 已有叶的发布、朝向、遮挡或零照度仍交给原采样规则；不以最终权重触发回退。
            if (GI_HasCandidateAddresses(result)) return result;
        }
        // 仅空间 miss 读取父层；每次返回同一层最多八个真实来源，不累加父子光照。
        uint maximumDepth = min(GI_LayoutRegionWord(region, 3u).x, 24u);
        for (uint depth = 0u; node != GI_INVALID_ADDRESS && depth <= maximumDepth; ++depth)
        {
            uvec2 lighting = GI_LayoutNodeLighting(node);
            if (lighting.x != GI_INVALID_ADDRESS)
            {
                GIProbeCandidates parent = GI_GatherCellCandidates(region, GI_PARENT_CELL_BIT | lighting.x, samplePos);
                for (uint i = 0u; i < 8u; ++i)
                {
                    if (parent.probes[i] == GI_INVALID_ADDRESS || parent.weights[i] <= 0.0) continue;
                    GIProbeState state = GI_LOAD_PROBE_STATE(region, parent.probes[i]);
                    if (state.metadata.x == 1u) return parent;
                }
            }
            node = lighting.y;
        }
        return GI_EmptyProbeCandidates(samplePos);
    }
    GI_LayoutQueryBounds(region, counts, volumeMin, volumeSize);
#endif
    vec3 spacing = volumeSize / vec3(counts);
    result.samplePosition = clamp(worldPos + N * max(normalBias, 0.0),
        volumeMin + spacing * 0.5, volumeMin + volumeSize - spacing * 0.5);
    vec3 coordinate = (result.samplePosition - volumeMin) / spacing - 0.5;
    ivec3 base = ivec3(floor(coordinate));
    vec3 fraction = clamp(coordinate - floor(coordinate), vec3(0.0), vec3(1.0));
    for (uint i = 0u; i < 8u; ++i)
    {
        ivec3 corner = ivec3(i & 1u, (i >> 1u) & 1u, i >> 2u);
        ivec3 tap = clamp(base + corner, ivec3(0), counts - 1);
        vec3 blend = mix(1.0 - fraction, fraction, vec3(corner));
        result.probes[i] = GI_ProbeAtlasLinearIndex(tap, counts);
#ifdef GI_PROBE_LAYOUT_DATA_GLSL
        result.probes[i] = GI_LayoutRegularAddress(region, tap, counts);
#endif
        result.positions[i] = volumeMin + (vec3(tap) + 0.5) * spacing;
        result.weights[i] = blend.x * blend.y * blend.z;
    }
    return result;
}
#endif
