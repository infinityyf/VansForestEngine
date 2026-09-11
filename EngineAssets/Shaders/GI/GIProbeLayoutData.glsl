#ifndef GI_PROBE_LAYOUT_DATA_GLSL
#define GI_PROBE_LAYOUT_DATA_GLSL
layout(set = GI_LAYOUT_SET, binding = GI_LAYOUT_BINDING, std430) readonly buffer GIProbeLayoutBuffer
{
    uvec4 words[];
} giLayout;
const uint GI_INVALID_ADDRESS = 0xffffffffu;
const uint GI_PARENT_CELL_BIT = 0x80000000u;
bool GI_LayoutIsSparse() { return giLayout.words[0].x != 0u; }
uint GI_LayoutRegionCount() { return giLayout.words[0].y; }
uvec4 GI_LayoutRegionWord(uint region, uint slot)
{ return giLayout.words[giLayout.words[0].z + region * 5u + slot]; }
vec4 GI_LayoutRegionMin(uint region) { return uintBitsToFloat(GI_LayoutRegionWord(region, 0u)); }
vec4 GI_LayoutRegionSize(uint region) { return uintBitsToFloat(GI_LayoutRegionWord(region, 1u)); }
vec4 GI_LayoutRegionTrace(uint region) { return uintBitsToFloat(GI_LayoutRegionWord(region, 4u)); }
uint GI_LayoutProbeCount(uint region) { return GI_LayoutRegionWord(region, 3u).w; }
vec4 GI_LayoutPosition(uint region, uint localProbe)
{
    uint globalProbe = GI_LayoutRegionWord(region, 3u).z + localProbe;
    return uintBitsToFloat(giLayout.words[giLayout.words[1].z + globalProbe * 2u]);
}
float GI_LayoutVisibilityRange(uint region, uint localProbe)
{
    uint globalProbe = GI_LayoutRegionWord(region, 3u).z + localProbe;
    return uintBitsToFloat(giLayout.words[giLayout.words[1].z + globalProbe * 2u + 1u].y);
}
uint GI_LayoutLocateNode(uint region, vec3 worldPos)
{
    if (region >= GI_LayoutRegionCount() || any(isnan(worldPos)) || any(isinf(worldPos))) return GI_INVALID_ADDRESS;
    vec4 minimum = GI_LayoutRegionMin(region);
    vec3 size = GI_LayoutRegionSize(region).xyz;
    uvec4 roots = GI_LayoutRegionWord(region, 2u);
    if (any(equal(roots.xyz, uvec3(0))) || any(lessThan(worldPos, minimum.xyz)) ||
        any(greaterThan(worldPos, minimum.xyz + size))) return GI_INVALID_ADDRESS;
    vec3 coordinate = (worldPos - minimum.xyz) / minimum.w;
    uvec3 root = min(uvec3(coordinate), roots.xyz - 1u);
    // 根索引的除法可能在边界舍入到邻格；fma 与布局的世界格点保持一次舍入。
    vec3 rootMinimum = fma(vec3(root), vec3(minimum.w), minimum.xyz);
    root -= uvec3(lessThan(worldPos, rootMinimum)) * uvec3(greaterThan(root, uvec3(0)));
    vec3 nextMinimum = fma(vec3(root + 1u), vec3(minimum.w), minimum.xyz);
    root += uvec3(greaterThanEqual(worldPos, nextMinimum)) * uvec3(lessThan(root + 1u, roots.xyz));
    uint address = roots.w + (root.z * roots.y + root.y) * roots.x + root.x;
    uint node = giLayout.words[giLayout.words[0].w + address / 4u][address % 4u];
    uint maxDepth = min(GI_LayoutRegionWord(region, 3u).x, 24u);
    for (uint depth = 0u; node != GI_INVALID_ADDRESS && depth <= maxDepth; ++depth)
    {
        uvec4 entry = giLayout.words[giLayout.words[1].x + node * 2u];
        if ((entry.x & 0x80000000u) != 0u)
            return node;
        uvec3 upper = uvec3(greaterThanEqual(worldPos, uintBitsToFloat(entry.yzw)));
        node = entry.x + upper.x + 2u * upper.y + 4u * upper.z;
    }
    return GI_INVALID_ADDRESS;
}
uint GI_LayoutNodeLeaf(uint node)
{
    if (node == GI_INVALID_ADDRESS) return GI_INVALID_ADDRESS;
    uint entry = giLayout.words[giLayout.words[1].x + node * 2u].x;
    return entry == GI_INVALID_ADDRESS || (entry & GI_PARENT_CELL_BIT) == 0u ? GI_INVALID_ADDRESS : entry & 0x7fffffffu;
}
uvec2 GI_LayoutNodeLighting(uint node)
{ return giLayout.words[giLayout.words[1].x + node * 2u + 1u].xy; }
uint GI_LayoutLocateLeaf(uint region, vec3 worldPos)
{ return GI_LayoutNodeLeaf(GI_LayoutLocateNode(region, worldPos)); }
uint GI_LayoutCellAddress(uint cell)
{
    uint base = (cell & GI_PARENT_CELL_BIT) != 0u ? giLayout.words[3].y : giLayout.words[1].y;
    return base + (cell & 0x7fffffffu) * 4u;
}
vec4 GI_LayoutCellMinimum(uint cell)
{ return uintBitsToFloat(giLayout.words[GI_LayoutCellAddress(cell)]); }
uint GI_LayoutCellProbe(uint region, uint leaf, uint corner)
{
    uint globalProbe = giLayout.words[GI_LayoutCellAddress(leaf) + 1u + corner / 4u][corner % 4u];
    return globalProbe == GI_INVALID_ADDRESS ? GI_INVALID_ADDRESS : globalProbe - GI_LayoutRegionWord(region, 3u).z;
}
// 先把八个几何角点的三线性权重映射到真实探针，随后各探针仍使用自己的可见性与状态。
void GI_LayoutCellWeights(uint leaf, vec3 fraction, out vec4 lower, out vec4 upper)
{
    lower = vec4(0.0); upper = vec4(0.0);
    for (uint corner = 0u; corner < 8u; ++corner)
    {
        vec3 side = vec3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        vec3 blend = mix(1.0 - fraction, fraction, side);
        if (corner < 4u) lower[corner] = blend.x * blend.y * blend.z;
        else upper[corner - 4u] = blend.x * blend.y * blend.z;
    }
    uint stencil = giLayout.words[GI_LayoutCellAddress(leaf) + 3u].w;
    if (stencil == GI_INVALID_ADDRESS) return;
    vec4 cornerLower = lower, cornerUpper = upper;
    uint address = giLayout.words[1].w + stencil * 16u;
    for (uint slot = 0u; slot < 8u; ++slot)
    {
        float weight = dot(uintBitsToFloat(giLayout.words[address + slot * 2u]), cornerLower)
            + dot(uintBitsToFloat(giLayout.words[address + slot * 2u + 1u]), cornerUpper);
        if (slot < 4u) lower[slot] = weight;
        else upper[slot - 4u] = weight;
    }
}
// 接收点只应用配置的法线偏移，不随叶层级或 probe 间距扩大。
// 先确定真实查询位置，再只寻址一次；不能把偏移截断在未偏移位置所在的叶盒内。
uint GI_LayoutLocateSampleNode(uint region, vec3 worldPos, vec3 normal, float normalBias, out vec3 samplePos)
{
    samplePos = vec3(0.0);
    if (region >= GI_LayoutRegionCount() || any(isnan(worldPos)) || any(isinf(worldPos)) ||
        any(isnan(normal)) || any(isinf(normal)) || isnan(normalBias) || isinf(normalBias)) return GI_INVALID_ADDRESS;
    vec4 minimum = GI_LayoutRegionMin(region);
    vec3 maximum = minimum.xyz + GI_LayoutRegionSize(region).xyz;
    if (any(lessThan(worldPos, minimum.xyz)) || any(greaterThan(worldPos, maximum))) return GI_INVALID_ADDRESS;
    samplePos = clamp(worldPos + normal * max(normalBias, 0.0), minimum.xyz, maximum);
    return GI_LayoutLocateNode(region, samplePos);
}
uint GI_LayoutLocateSample(uint region, vec3 worldPos, vec3 normal, float normalBias, out vec3 samplePos)
{ return GI_LayoutNodeLeaf(GI_LayoutLocateSampleNode(region, worldPos, normal, normalBias, samplePos)); }
// 作者区域数量有固定上限；空间细分数量不进入此循环。
uint GI_LayoutSelectRegion(vec3 worldPos)
{
    if (any(isnan(worldPos)) || any(isinf(worldPos))) return GI_INVALID_ADDRESS;
    uint best = GI_INVALID_ADDRESS;
    float bestPriority = -3.402823e38;
    float bestWeight = -1.0;
    for (uint region = 0u; region < GI_LayoutRegionCount(); ++region)
    {
        vec3 minimum = GI_LayoutRegionMin(region).xyz;
        vec3 size = GI_LayoutRegionSize(region).xyz;
        vec3 edge = min(worldPos - minimum, minimum + size - worldPos);
        if (any(lessThan(edge, vec3(0.0)))) continue;
        vec4 trace = GI_LayoutRegionTrace(region);
        float weight = trace.y > 0.0 ? clamp(min(edge.x, min(edge.y, edge.z)) / trace.y, 0.0, 1.0) : 1.0;
        if (trace.z > bestPriority || (trace.z == bestPriority && weight > bestWeight))
        { best = region; bestPriority = trace.z; bestWeight = weight; }
    }
    return best;
}
#endif
