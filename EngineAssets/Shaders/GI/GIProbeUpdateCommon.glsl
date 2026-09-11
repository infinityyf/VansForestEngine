#ifndef GI_PROBE_UPDATE_COMMON_GLSL_INCLUDED
#define GI_PROBE_UPDATE_COMMON_GLSL_INCLUDED

#include "../Common/Common.glsl"

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


uint GI_ProbeLinearIndex(ivec3 index, ivec3 counts)
{ return uint((index.z * counts.y + index.y) * counts.x + index.x); }
uint GI_FixedRayCount(uint raysPerProbe) { return min(32u, raysPerProbe / 2u); }
struct GIProbeRayDirectionContext
{
    uint fixedRayCount;
    uint dynamicRayCount;
    vec4 stableRotation;
    vec4 cycleRotation;
};
GIProbeRayDirectionContext GI_BuildProbeRayDirectionContext(
    ivec3 probeIndex, ivec3 probeCounts, uint raysPerProbe, uint cycleIndex)
{
    GIProbeRayDirectionContext context;
    context.fixedRayCount = GI_FixedRayCount(raysPerProbe);
    context.dynamicRayCount = raysPerProbe - context.fixedRayCount;
    uint seed = GI_HashCombine(GI_ProbeLinearIndex(probeIndex, probeCounts),
        uint(probeCounts.x * 73856093 ^ probeCounts.y * 19349663 ^ probeCounts.z * 83492791));
    context.stableRotation = GI_UniformRotationQuaternion(seed);
    context.cycleRotation = GI_UniformRotationQuaternion(GI_HashCombine(seed, cycleIndex));
    return context;
}
// 默认 32 条稳定几何方向和 224 条旋转光照方向，各自覆盖完整球面。
vec3 GI_ProbeRayDirectionPrepared(GIProbeRayDirectionContext context, uint localRay, out bool fixedRay)
{
    fixedRay = localRay < context.fixedRayCount;
    uint index = fixedRay ? localRay : localRay - context.fixedRayCount;
    uint count = fixedRay ? context.fixedRayCount : context.dynamicRayCount;
    vec3 direction = GI_RotateByQuaternion(SampleSphere(int(index), int(count)), context.stableRotation);
    return fixedRay ? direction : GI_RotateByQuaternion(direction, context.cycleRotation);
}
// 每次完整球面估计贡献一个样本，稀疏调度的等待间隔不消耗历史。
float GI_HistoryBlend(float hysteresis)
{ return 1.0 - clamp(hysteresis, 0.0, 0.999); }
#endif
