#ifndef GI_RECEIVER_VISIBILITY_PROBE_REUSE_GLSL
#define GI_RECEIVER_VISIBILITY_PROBE_REUSE_GLSL
// 历史只从一个经过验证的真实锚点继承，不能混合不同起点的位掩码。
bool GI_ReceiverProbeUnmoved(uint region, uint probe)
{
    GIProbeState a = currentStates[nonuniformEXT(region)].states[probe];
    GIProbeState b = previousStates[nonuniformEXT(region)].states[probe];
    return a.metadata.x == 1u && b.metadata.x == 1u &&
        all(equal(a.traceOffsetAndBackface.xyz, b.traceOffsetAndBackface.xyz));
}
uint GI_CompleteReceiverHistory(GIReceiverVisibilityRecord old, GIReceiverVisibilityRecord wanted, uint count)
{
    uint mask = (1u << count) - 1u;
    if (old.metadata.z != mask) return 0u;
    for (uint i = 0u; i < count; ++i)
        if (GI_ReceiverProbe(old, i) != GI_ReceiverProbe(wanted, i) ||
            !GI_ReceiverProbeUnmoved(wanted.metadata.x, GI_ReceiverProbe(wanted, i))) return 0u;
    return mask;
}
// 优化 1：按身份取交集。注释调用后仍保留完整候选集的基础复用路径。
void GI_ReuseProbeIntersection(GIReceiverVisibilityRecord old, GIReceiverVisibilityRecord wanted,
    uint count, inout uint known, inout uint visible)
{
    if (known == (1u << count) - 1u) return;
    known = 0u; visible = 0u;
    for (uint i = 0u; i < count; ++i)
    {
        uint probe = GI_ReceiverProbe(wanted, i);
        if (!GI_ReceiverProbeUnmoved(wanted.metadata.x, probe)) continue;
        for (uint j = 0u; j < 16u; ++j)
        {
            if ((old.metadata.z & (1u << j)) == 0u || GI_ReceiverProbe(old, j) != probe) continue;
            known |= 1u << i;
            if ((old.metadata.w & (1u << j)) != 0u) visible |= 1u << i;
            break;
        }
    }
}
#endif
