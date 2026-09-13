#ifndef GI_RECEIVER_ZERO_SUPPORT_FALLBACK_GLSL
#define GI_RECEIVER_ZERO_SUPPORT_FALLBACK_GLSL
// 只在额外接收点 RT 清空支持时重试原 DDGI；候选、偏移、发布状态和距离矩不变。
void GI_ApplyReceiverZeroSupportFallback(uint region, sampler2D irradiance, sampler2D visibility,
    GIProbeCandidates candidates, vec3 normal, ivec2 tileGrid, inout vec3 sum, inout float support)
{
    if(support>0.0 || !receiverRecordValid || receiverRecord.metadata.x!=region ||
        (receiverRecord.metadata.z & ~receiverRecord.metadata.w)==0u) return;
    receiverRecordValid=false;
    for(uint i=0u;i<8u;++i)
    {
        if(candidates.probes[i]==0xffffffffu) continue;
        GI_AccumulateProbeIrradiance(region,candidates.probes[i],irradiance,visibility,
            candidates.positions[i],candidates.samplePosition,normal,candidates.weights[i],tileGrid,sum,support);
    }
    receiverRecordValid=true; // 只影响本次照明重采样，不改遮挡缓存及后续查询。
}
#endif
