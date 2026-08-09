#ifndef GI_PROBE_UPDATE_COMMON_GLSL_INCLUDED
#define GI_PROBE_UPDATE_COMMON_GLSL_INCLUDED

#include "../Common/Common.glsl"

// DDGI 的采样节奏与 C++ BuildGIProbeUpdateBatch 必须保持一致：
// D^3 个空间 phase，S 个方向 slice。默认 D=2、S=16、R=256，完整周期为 128 帧。
uvec3 GI_UpdateSpatialDivisors(uvec3 probeCounts, uint spatialDivisor)
{
    return min(uvec3(max(spatialDivisor, 1u)), max(probeCounts, uvec3(1u)));
}

uint GI_UpdateSpatialPhaseCount(uvec3 probeCounts, uint spatialDivisor)
{
    uvec3 divisors = GI_UpdateSpatialDivisors(probeCounts, spatialDivisor);
    return divisors.x * divisors.y * divisors.z;
}

uvec3 GI_UpdateSpatialOffset(uint frameIndex, uvec3 probeCounts, uint spatialDivisor)
{
    uvec3 divisors = GI_UpdateSpatialDivisors(probeCounts, spatialDivisor);
    uint phaseCount = divisors.x * divisors.y * divisors.z;
    uint spatialPhase = frameIndex % phaseCount;
    return uvec3(
        spatialPhase % divisors.x,
        (spatialPhase / divisors.x) % divisors.y,
        spatialPhase / (divisors.x * divisors.y));
}

uint GI_UpdateDirectionSlice(uint frameIndex, uvec3 probeCounts, uint spatialDivisor, uint directionSlices)
{
    return (frameIndex / GI_UpdateSpatialPhaseCount(probeCounts, spatialDivisor)) % directionSlices;
}

uint GI_UpdateCycleIndex(uint frameIndex, uvec3 probeCounts, uint spatialDivisor, uint directionSlices)
{
    return frameIndex / (GI_UpdateSpatialPhaseCount(probeCounts, spatialDivisor) * directionSlices);
}

uvec3 GI_UpdateDispatchDimensions(uvec3 probeCounts, uint spatialDivisor, uint frameIndex)
{
    uvec3 divisors = GI_UpdateSpatialDivisors(probeCounts, spatialDivisor);
    uvec3 offset = GI_UpdateSpatialOffset(frameIndex, probeCounts, spatialDivisor);
    // Do not use a phase-0 ceil here: on odd dimensions later phases contain
    // fewer probes. This count must match C++ BuildGIProbeUpdateBatch or the
    // packed transient-ray buffer gets holes/over-counted entries.
    return (probeCounts - uvec3(1u) - offset) / divisors + uvec3(1u);
}

uint GI_Hash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

uint GI_HashCombine(uint left, uint right)
{
    return GI_Hash(left ^ (right + 0x9e3779b9u + (left << 6u) + (left >> 2u)));
}

float GI_Hash01(uint value)
{
    return float(GI_Hash(value)) * (1.0 / 4294967296.0);
}

vec4 GI_UniformRotationQuaternion(uint seed)
{
    float u0 = GI_Hash01(seed);
    float u1 = GI_Hash01(seed ^ 0x68bc21ebu);
    float u2 = GI_Hash01(seed ^ 0x02e5be93u);
    float sqrtOneMinusU0 = sqrt(max(1.0 - u0, 0.0));
    float sqrtU0 = sqrt(max(u0, 0.0));
    float angle0 = TWO_PI * u1;
    float angle1 = TWO_PI * u2;
    return vec4(
        sqrtOneMinusU0 * sin(angle0),
        sqrtOneMinusU0 * cos(angle0),
        sqrtU0 * sin(angle1),
        sqrtU0 * cos(angle1));
}

vec3 GI_RotateByQuaternion(vec3 direction, vec4 rotation)
{
    vec3 q = rotation.xyz;
    vec3 t = 2.0 * cross(q, direction);
    return normalize(direction + rotation.w * t + cross(q, t));
}

uint GI_ProbeLinearIndex(ivec3 probeIndex, ivec3 probeCounts)
{
    return uint(probeIndex.z * probeCounts.x * probeCounts.y +
        probeIndex.y * probeCounts.x + probeIndex.x);
}

bool GI_IsFixedClassificationRay(uint localRayIndex, uint raysPerSlice)
{
    // 在默认 256/16 配置下，每个 slice 前两条为固定方向，16 个 slice 合计 32 条。
    return raysPerSlice >= 2u && localRayIndex < 2u;
}

vec3 GI_ProbeRayDirection(
    ivec3 probeIndex,
    ivec3 probeCounts,
    uint raysPerProbe,
    uint localRayIndex,
    uint raysPerSlice,
    uint directionSlice,
    uint directionSlices,
    uint cycleIndex,
    out bool fixedClassificationRay)
{
    const uint fixedRayCount = min(raysPerProbe, min(32u, max(directionSlices * 2u, 2u)));
    const uint dynamicRayCount = max(raysPerProbe - fixedRayCount, 1u);
    fixedClassificationRay = GI_IsFixedClassificationRay(localRayIndex, raysPerSlice);

    uint probeSeed = GI_HashCombine(
        GI_ProbeLinearIndex(probeIndex, probeCounts),
        uint(probeCounts.x * 73856093 ^ probeCounts.y * 19349663 ^ probeCounts.z * 83492791));
    vec4 stableRotation = GI_UniformRotationQuaternion(probeSeed);

    if (fixedClassificationRay)
    {
        uint fixedIndex = localRayIndex * directionSlices + directionSlice;
        vec3 fixedDirection = SampleSphere(int(fixedIndex % fixedRayCount), int(fixedRayCount));
        return GI_RotateByQuaternion(fixedDirection, stableRotation);
    }

    uint dynamicLocalIndex = localRayIndex - min(2u, raysPerSlice);
    uint dynamicIndex = dynamicLocalIndex * directionSlices + directionSlice;
    vec3 dynamicDirection = SampleSphere(int(dynamicIndex % dynamicRayCount), int(dynamicRayCount));
    vec4 cycleRotation = GI_UniformRotationQuaternion(GI_HashCombine(probeSeed, cycleIndex));
    return GI_RotateByQuaternion(
        GI_RotateByQuaternion(dynamicDirection, stableRotation),
        cycleRotation);
}

#endif
