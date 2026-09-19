#ifndef GI_PROBE_COMMON_GLSL_INCLUDED
#define GI_PROBE_COMMON_GLSL_INCLUDED

#include "../Common/Common.glsl"

#define GI_VISIBILITY_INTERIOR_RES 14
#define GI_VISIBILITY_OCTA_RES 16
#define GI_IRRADIANCE_INTERIOR_RES 6
#define GI_IRRADIANCE_OCTA_RES 8
#define GI_ATLAS_BORDER 1

float GI_VolumeFade(vec3 worldPos, vec3 volumeMin, vec3 volumeSize, float fadeDistance)
{
    if (fadeDistance <= 1e-4)
        return 1.0;

    vec3 volumeMax = volumeMin + volumeSize;
    vec3 distToEdge = min(worldPos - volumeMin, volumeMax - worldPos);
    float edgeDist = min(min(distToEdge.x, distToEdge.y), distToEdge.z);
    return smoothstep(0.0, fadeDistance, edgeDist);
}

bool GI_IsInsideVolume(vec3 worldPos, vec3 volumeMin, vec3 volumeSize)
{
    vec3 volumeMax = volumeMin + volumeSize;
    return all(greaterThanEqual(worldPos, volumeMin)) &&
           all(lessThanEqual(worldPos, volumeMax));
}

uint GI_ProbeAtlasLinearIndex(ivec3 probeIndex, ivec3 probeCounts)
{
    return uint(probeIndex.z * (probeCounts.x * probeCounts.y) +
        probeIndex.y * probeCounts.x + probeIndex.x);
}

ivec2 GI_AtlasTileIndex(uint probeLinearIndex, int probesPerRow)
{
    uint safeProbesPerRow = uint(max(probesPerRow, 1));
    return ivec2(
        int(probeLinearIndex % safeProbesPerRow),
        int(probeLinearIndex / safeProbesPerRow));
}

vec2 GI_AtlasUVFromTileTexel(
    uint probeLinearIndex,
    vec2 tileTexel,
    int probesPerRow,
    int tileResolution,
    vec2 inverseAtlasSize)
{
    ivec2 tileIndex = GI_AtlasTileIndex(probeLinearIndex, probesPerRow);
    return (vec2(tileIndex * tileResolution) + tileTexel) * inverseAtlasSize;
}

vec2 GI_AtlasUVFromTileIndex(
    ivec2 tileIndex,
    vec2 tileTexel,
    int tileResolution,
    vec2 inverseAtlasSize)
{
    return (vec2(tileIndex * tileResolution) + tileTexel) * inverseAtlasSize;
}

vec2 GI_SignNotZero(vec2 v)
{
    return vec2(v.x >= 0.0 ? 1.0 : -1.0,
        v.y >= 0.0 ? 1.0 : -1.0);
}

vec2 GI_OctahedralEncode(vec3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6);
    vec2 p = n.xz;
    if (n.y < 0.0)
        p = (1.0 - abs(p.yx)) * GI_SignNotZero(p);
    return p * 0.5 + 0.5;
}

vec3 GI_OctahedralDecode(vec2 e)
{
    vec2 f = e * 2.0 - 1.0;
    vec3 n = vec3(f.x, 1.0 - abs(f.x) - abs(f.y), f.y);
    if (n.y < 0.0)
        n.xz = (1.0 - abs(n.zx)) * GI_SignNotZero(n.xz);
    return normalize(n);
}

vec2 GI_VisibilityAtlasUV(ivec3 probeIndex, vec3 direction, ivec3 probeCounts, ivec2 atlasSize)
{
    int probesPerRow = max(atlasSize.x / GI_VISIBILITY_OCTA_RES, 1);
    uint probeLinearIndex = GI_ProbeAtlasLinearIndex(probeIndex, probeCounts);
    vec2 tile = GI_OctahedralEncode(normalize(direction)) * float(GI_VISIBILITY_INTERIOR_RES) +
        float(GI_ATLAS_BORDER);
    return GI_AtlasUVFromTileTexel(probeLinearIndex, tile, probesPerRow,
        GI_VISIBILITY_OCTA_RES, 1.0 / vec2(atlasSize));
}

vec2 GI_IrradianceAtlasUV(ivec3 probeIndex, vec3 direction, ivec3 probeCounts, ivec2 atlasSize)
{
    int probesPerRow = max(atlasSize.x / GI_IRRADIANCE_OCTA_RES, 1);
    uint probeLinearIndex = GI_ProbeAtlasLinearIndex(probeIndex, probeCounts);
    vec2 tile = GI_OctahedralEncode(normalize(direction)) * float(GI_IRRADIANCE_INTERIOR_RES) +
        float(GI_ATLAS_BORDER);
    return GI_AtlasUVFromTileTexel(probeLinearIndex, tile, probesPerRow,
        GI_IRRADIANCE_OCTA_RES, 1.0 / vec2(atlasSize));
}

// 接收点已在寻址前沿法线偏移一次；距离矩与距离比较必须使用同一原点。
float GI_EvaluateProbeVisibilityMoments(vec2 moments, float receiverDistance)
{
    float meanSquared = moments.x * moments.x;
    // RG32F 两个近等量相减时保留舍入误差尺度，不能按 probe 间距伪造几何方差。
    float variance = max(moments.y - meanSquared, max(moments.y, meanSquared) * 1e-6);
    float delta = max(receiverDistance - moments.x, 0.0);
    if (delta <= 0.0) return 1.0;
    float probability = variance / (variance + delta * delta);
    return probability * probability;
}

float GI_EvaluateProbeVisibilityTile(sampler2D visibilityAtlas, ivec2 tileIndex,
    vec3 direction, float receiverDistance, vec2 inverseAtlasSize)
{
    vec2 tile = GI_OctahedralEncode(direction) * float(GI_VISIBILITY_INTERIOR_RES) + float(GI_ATLAS_BORDER);
    vec2 moments = textureLod(visibilityAtlas,
        GI_AtlasUVFromTileIndex(tileIndex, tile, GI_VISIBILITY_OCTA_RES, inverseAtlasSize), 0.0).rg;
    return GI_EvaluateProbeVisibilityMoments(moments, receiverDistance);
}

#ifdef GI_LOAD_PROBE_STATE
// 两类布局共用完全相同的单 probe 贡献规则：空间权重 × 朝向 × DDGI 可见性。
bool GI_AccumulateProbeIrradiance(uint region, uint probe, sampler2D irradiance, sampler2D visibility,
    vec3 probePosition, vec3 samplePos, vec3 N, float spatialWeight, ivec2 tileGrid,
    inout vec3 sum, inout float weightSum)
{
    if (spatialWeight <= 0.0) return false;
    GIProbeState state = GI_LOAD_PROBE_STATE(region, probe);
    if (state.metadata.x != 1u) return false; // 首次完整发布之前，图集尚无可读数据。
    ivec2 tile = GI_AtlasTileIndex(probe, tileGrid.x);
    vec2 irradianceTexel = GI_OctahedralEncode(N) * float(GI_IRRADIANCE_INTERIOR_RES) + float(GI_ATLAS_BORDER);
    vec3 value = textureLod(irradiance, GI_AtlasUVFromTileIndex(tile, irradianceTexel,
        GI_IRRADIANCE_OCTA_RES, 1.0 / vec2(tileGrid * GI_IRRADIANCE_OCTA_RES)), 0.0).rgb;
    vec3 delta = samplePos - (probePosition + state.traceOffsetAndBackface.xyz);
    float distance = length(delta);
    vec3 direction = distance > 0.0 ? delta / distance : -N;
    float facing = clamp(dot(-direction, N) * 0.5 + 0.5, 0.0, 1.0);
    float visibilityWeight = GI_EvaluateProbeVisibilityTile(
        visibility, tile, direction, distance, 1.0 / vec2(tileGrid * GI_VISIBILITY_OCTA_RES));
#ifdef GI_RECEIVER_VISIBILITY
    visibilityWeight = GI_ReceiverProbeVisibility(region, probe, visibilityWeight);
#endif
    float weight = spatialWeight * facing * facing * visibilityWeight;
    sum += max(value, vec3(0.0)) * weight;
    weightSum += weight;
    return true;
}

#include "GIProbeCandidates.glsl"
#ifdef GI_RECEIVER_VISIBILITY
#include "GIReceiverZeroSupportFallback.glsl"
#endif

// 发布比例只由空间候选和发布身份决定，与 RGB 和遮挡支持分离。
struct GIProbeLighting { vec3 irradiance; float support; float published; };
// 空间权重、朝向、图集积分约定不变；可见性来源由调用者明确选择。
GIProbeLighting GI_SampleProbeIrradianceAtlas(uint region, ivec3 counts,
    sampler2D irradiance, sampler2D visibility, vec3 worldPos, vec3 N,
    vec3 volumeMin, vec3 volumeSize, float normalBias, float volumeWeight,
    int columns, int rows)
{
    GIProbeCandidates candidates = GI_GatherProbeCandidates(
        region, counts, worldPos, N, volumeMin, volumeSize, normalBias);
    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    float spatialSum = 0.0, publishedSum = 0.0;
    for (uint i = 0u; i < 8u; ++i)
    {
        if (candidates.probes[i] == 0xffffffffu) continue;
        spatialSum += candidates.weights[i];
        bool published = GI_AccumulateProbeIrradiance(region, candidates.probes[i], irradiance, visibility,
            candidates.positions[i], candidates.samplePosition, N, candidates.weights[i],
            ivec2(columns, rows), sum, weightSum);
        if (published) publishedSum += candidates.weights[i];
    }
#ifdef GI_RECEIVER_VISIBILITY
    // 独立回退入口；注释此调用可恢复严格的接收点 RT 剔除。
    GI_ApplyReceiverZeroSupportFallback(region, irradiance, visibility, candidates, N,
        ivec2(columns, rows), sum, weightSum);
#endif
    return GIProbeLighting(weightSum > 0.0 ? sum / weightSum * volumeWeight * INV_PI : vec3(0.0),
        weightSum, spatialSum > 0.0 ? clamp(publishedSum / spatialSum, 0.0, 1.0) : 0.0);
}

GIProbeLighting GI_SampleProbeIrradianceAtlasVisible(uint region, ivec3 counts,
    sampler2D irradiance, sampler2D visibility, vec3 worldPos, vec3 normal,
    vec3 volumeMin, vec3 volumeSize, float normalBias, float fadeDistance)
{
#ifdef GI_PROBE_LAYOUT_DATA_GLSL
    GI_LayoutQueryBounds(region, counts, volumeMin, volumeSize);
#endif
    if (!GI_IsInsideVolume(worldPos, volumeMin, volumeSize)) return GIProbeLighting(vec3(0), 0, 0);
    ivec2 grid = textureSize(irradiance, 0) / GI_IRRADIANCE_OCTA_RES;
    return GI_SampleProbeIrradianceAtlas(region, counts, irradiance, visibility,
        worldPos, normalize(normal), volumeMin, volumeSize, normalBias,
        GI_VolumeFade(worldPos, volumeMin, volumeSize, fadeDistance), grid.x, grid.y);
}
#endif
#endif
