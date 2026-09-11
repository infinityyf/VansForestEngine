#ifndef GI_RECEIVER_VISIBILITY_REUSE_GLSL
#define GI_RECEIVER_VISIBILITY_REUSE_GLSL
#include "GIReceiverVisibilityProbeReuse.glsl"
void GI_ConsiderReceiverHistory(GIReceiverVisibilityRecord old, vec4 position, vec3 normal,
    float material, float footprint, uint count, uint maximumAge,
    inout GIReceiverVisibilityRecord record, inout float bestDistance)
{
    if (old.metadata.x != record.metadata.x || old.metadata.z == 0u ||
        frame.z - old.metadata.y > maximumAge ||
        !GI_ReceiverSurfaceMatches(old.surface, position, normal, material, footprint)) return;
    uint known = GI_CompleteReceiverHistory(old, record, count);
    uint visible = old.metadata.w & known;
    GI_ReuseProbeIntersection(old, record, count, known, visible); // 优化 1 独立入口
    uint coverage = uint(bitCount(known));
    uint bestCoverage = uint(bitCount(record.metadata.z));
    float distance = length(old.surface.xyz - position.xyz);
    if (coverage == 0u || coverage < bestCoverage ||
        (coverage == bestCoverage && distance >= bestDistance)) return;
    bestDistance = distance;
    record.surface = old.surface;
    record.anchor = old.anchor;
    record.metadata.yzw = uvec3(old.metadata.y, known, visible);
}
void GI_SearchReceiverHistoryTap(ivec2 tap, vec4 position, vec3 normal, float material,
    float footprint, uint count, uint maximumAge,
    inout GIReceiverVisibilityRecord record, inout float bestDistance)
{
    if (any(lessThan(tap, ivec2(0))) || any(greaterThanEqual(tap, ivec2(frame.xy)))) return;
    GI_ConsiderReceiverHistory(history[tap.y * int(frame.x) + tap.x], position, normal,
        material, footprint, count, maximumAge, record, bestDistance);
}
#include "GIReceiverVisibilityExtendedSearch.glsl"
#endif
