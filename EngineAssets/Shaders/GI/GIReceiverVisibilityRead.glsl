#ifndef GI_RECEIVER_VISIBILITY_READ_GLSL
#define GI_RECEIVER_VISIBILITY_READ_GLSL
#include "GIReceiverVisibilityData.glsl"
layout(set = 1, binding = GI_RECEIVER_BINDING, std430) readonly buffer ReceiverVisibility
{
    uvec4 receiverHeader; // enabled, cacheWidth, cacheHeight, reserved
    GIReceiverVisibilityRecord receiverRecords[];
};
GIReceiverVisibilityRecord receiverRecord;
bool receiverRecordValid = false;
// 只从同一局部表面选一个最近的记录。位掩码不作双线性滤波。
void GI_SetReceiverVisibility(ivec2 pixel, vec4 position, vec3 normal, float material, float footprint)
{
#ifdef GI_RECEIVER_BIAS_READ_GLSL
    GI_SetReceiverBias(pixel,position,normal,material,footprint);
#endif
    receiverRecordValid = false;
    if (receiverHeader.x == 0u) return;
    ivec2 size = ivec2(receiverHeader.yz);
    ivec2 base = ivec2(floor((vec2(pixel) - 2.0) * 0.25));
    float bestDistance = 3.402823e38;
    for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x)
    {
        ivec2 tap = base + ivec2(x,y);
        if (any(lessThan(tap,ivec2(0))) || any(greaterThanEqual(tap,size))) continue;
        GIReceiverVisibilityRecord candidate = receiverRecords[tap.y * size.x + tap.x];
        if (candidate.metadata.z == 0u ||
            !GI_ReceiverSurfaceMatches(candidate.surface, position, normal, material, footprint * 2.0)) continue;
        vec3 delta = candidate.surface.xyz - position.xyz;
        float distanceSquared = dot(delta,delta);
        if (distanceSquared >= bestDistance) continue;
        bestDistance = distanceSquared;
        receiverRecord = candidate;
        receiverRecordValid = true;
    }
}
float GI_ReceiverProbeVisibility(uint region, uint probe, float momentsVisibility)
{
    if (!receiverRecordValid || receiverRecord.metadata.x != region) return momentsVisibility;
    for (uint i = 0u; i < 16u; ++i)
        if ((receiverRecord.metadata.z & (1u << i)) != 0u && GI_ReceiverProbe(receiverRecord,i) == probe)
            return (receiverRecord.metadata.w & (1u << i)) != 0u ? 1.0 : 0.0;
    return momentsVisibility;
}
#define GI_RECEIVER_VISIBILITY
#endif
