#ifndef GI_PROBE_COMMON_GLSL_INCLUDED
#define GI_PROBE_COMMON_GLSL_INCLUDED

#include "../Common/Common.glsl"

#define GI_VISIBILITY_OCTA_RES 8

// GI probe SH 约定：
// 1. 3D 纹理存储入射 radiance 的 L0+L1 SH 系数，不是卷积后的 irradiance。
// 2. 接收端查询 diffuse ambient 时，对 radiance SH 做 Lambert cosine kernel 卷积。
// 3. 返回值是 diffuse lighting 项，也就是 irradiance / PI，后续材质再乘 albedo。
vec3 GI_EvaluateDiffuseIrradianceL1(vec4 rC, vec4 gC, vec4 bC, vec3 normal)
{
    normal = normalize(normal);

    float y0 = SHBasis(0, normal);
    float y1 = SHBasis(1, normal);
    float y2 = SHBasis(2, normal);
    float y3 = SHBasis(3, normal);

    vec3 l0 = vec3(rC.x, gC.x, bC.x) * y0 * PI;
    vec3 l1 =
        vec3(rC.y, gC.y, bC.y) * y1 * (2.0 * PI / 3.0) +
        vec3(rC.z, gC.z, bC.z) * y2 * (2.0 * PI / 3.0) +
        vec3(rC.w, gC.w, bC.w) * y3 * (2.0 * PI / 3.0);

    return max(l0 + l1, vec3(0.0));
}

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

vec2 GI_OctahedralEncode(vec3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6);
    vec2 p = n.xz;
    if (n.y < 0.0)
    {
        p = (1.0 - abs(p.yx)) * sign(p);
    }
    return p * 0.5 + 0.5;
}

vec3 GI_OctahedralDecode(vec2 e)
{
    vec2 f = e * 2.0 - 1.0;
    vec3 n = vec3(f.x, 1.0 - abs(f.x) - abs(f.y), f.y);
    if (n.y < 0.0)
    {
        n.xz = (1.0 - abs(n.zx)) * sign(n.xz);
    }
    return normalize(n);
}

vec2 GI_VisibilityAtlasUV(ivec3 probeIndex, vec3 direction, ivec3 probeCounts, ivec2 atlasSize)
{
    int probesPerRow = max(atlasSize.x / GI_VISIBILITY_OCTA_RES, 1);
    int probeLinearIndex =
        probeIndex.z * (probeCounts.x * probeCounts.y) +
        probeIndex.y * probeCounts.x +
        probeIndex.x;
    ivec2 tileIndex = ivec2(
        probeLinearIndex % probesPerRow,
        probeLinearIndex / probesPerRow);

    vec2 oct = GI_OctahedralEncode(normalize(direction));
    vec2 tile = oct * float(GI_VISIBILITY_OCTA_RES - 1) + 0.5;
    float atlasX = float(tileIndex.x * GI_VISIBILITY_OCTA_RES) + tile.x;
    float atlasY = float(tileIndex.y * GI_VISIBILITY_OCTA_RES) + tile.y;
    return vec2(atlasX, atlasY) / vec2(atlasSize);
}

float GI_EvaluateProbeVisibility(
    sampler2D visibilityAtlas,
    ivec3 probeIndex,
    vec3 probeToReceiver,
    float receiverDistance,
    ivec3 probeCounts,
    float normalBias,
    float cellSize)
{
    ivec2 atlasSize = textureSize(visibilityAtlas, 0);
    int totalProbeCount = probeCounts.x * probeCounts.y * probeCounts.z;
    int probesPerRow = max(atlasSize.x / GI_VISIBILITY_OCTA_RES, 1);
    int probeRows = (totalProbeCount + probesPerRow - 1) / probesPerRow;
    if (atlasSize.x < GI_VISIBILITY_OCTA_RES || atlasSize.y < probeRows * GI_VISIBILITY_OCTA_RES)
        return 1.0;

    vec2 moments = textureLod(
        visibilityAtlas,
        GI_VisibilityAtlasUV(probeIndex, probeToReceiver, probeCounts, atlasSize),
        0.0).rg;
    float meanDistance = moments.x;
    if (meanDistance <= 1e-4)
        return 1.0;

    float distanceToReceiver = max(receiverDistance - normalBias, 0.0);
    float softBias = max(cellSize * 0.12, normalBias * 2.0);
    if (distanceToReceiver <= meanDistance + softBias)
        return 1.0;

    float variance = max(moments.y - meanDistance * meanDistance, cellSize * cellSize * 0.02);
    float distanceDelta = distanceToReceiver - meanDistance;

    // DDGI 常用 Chebyshev 上界由距离一阶/二阶矩估计可见性。
    // 这里的矩来自预缓存射线，不重新做实时求交；权重越低，说明 receiver 位于 probe 该方向首个遮挡面之后。
    float chebyshev = variance / (variance + distanceDelta * distanceDelta);
    float visibility = clamp(chebyshev * chebyshev, 0.0, 1.0);
    return max(visibility, 0.04);
}

vec3 GI_SampleProbeDiffuseLightingL1Internal(
    sampler3D shR,
    sampler3D shG,
    sampler3D shB,
    sampler2D visibilityAtlas,
    bool useVisibility,
    vec3 receiverPos,
    vec3 samplePos,
    vec3 normal,
    vec3 volumeMin,
    vec3 volumeSize,
    float normalBias,
    float volumeFadeDistance)
{
    vec3 N = normalize(normal);
    if (!GI_IsInsideVolume(receiverPos, volumeMin, volumeSize))
        return vec3(0.0);

    ivec3 texSize = textureSize(shR, 0);
    vec3 texelWorldSize = volumeSize / vec3(texSize);
    float cellSize = min(min(texelWorldSize.x, texelWorldSize.y), texelWorldSize.z);
    vec3 volumeMax = volumeMin + volumeSize;
    vec3 safeMin = volumeMin + texelWorldSize * 0.5;
    vec3 safeMax = volumeMax - texelWorldSize * 0.5;

    vec3 clampedSamplePos = clamp(samplePos, safeMin, safeMax);
    vec3 texPos = (clampedSamplePos - volumeMin) / volumeSize * vec3(texSize) - 0.5;
    ivec3 base = ivec3(floor(texPos));
    vec3 frac = clamp(texPos - floor(texPos), vec3(0.0), vec3(1.0));

    vec3 colorSum = vec3(0.0);
    float weightSum = 0.0;

    for (int dz = 0; dz <= 1; ++dz)
    for (int dy = 0; dy <= 1; ++dy)
    for (int dx = 0; dx <= 1; ++dx)
    {
        ivec3 offset = ivec3(dx, dy, dz);
        ivec3 tap = clamp(base + offset, ivec3(0), texSize - 1);

        vec3 blend = mix(1.0 - frac, frac, vec3(offset));
        float triW = blend.x * blend.y * blend.z;
        if (triW <= 0.0)
            continue;

        vec3 probeWorld = (vec3(tap) + 0.5) / vec3(texSize) * volumeSize + volumeMin;
        vec3 probeToReceiver = samplePos - probeWorld;
        float receiverDistance = length(probeToReceiver);
        vec3 probeToReceiverDir = (receiverDistance > 1e-5) ? (probeToReceiver / receiverDistance) : N;
        vec3 receiverToProbeDir = -probeToReceiverDir;

        // 法线权重用于降低表面背侧 probe 的影响；visibility 负责真正的几何遮挡判断。
        float sameSide = smoothstep(-0.15, 0.85, dot(receiverToProbeDir, N));
        float normalW = mix(0.12, 1.0, sameSide);
        float visibilityW = useVisibility
            ? GI_EvaluateProbeVisibility(
                visibilityAtlas,
                tap,
                probeToReceiverDir,
                receiverDistance,
                texSize,
                normalBias,
                cellSize)
            : 1.0;
        float w = triW * normalW * visibilityW;

        vec4 rC = texelFetch(shR, tap, 0);
        vec4 gC = texelFetch(shG, tap, 0);
        vec4 bC = texelFetch(shB, tap, 0);
        colorSum += GI_EvaluateDiffuseIrradianceL1(rC, gC, bC, N) * w;
        weightSum += w;
    }

    vec3 irradiance = (weightSum > 1e-5) ? (colorSum / weightSum) : vec3(0.0);
    irradiance *= GI_VolumeFade(receiverPos, volumeMin, volumeSize, volumeFadeDistance);
    return max(irradiance * INV_PI, vec3(0.0));
}

vec3 GI_SampleProbeDiffuseLightingL1(
    sampler3D shR,
    sampler3D shG,
    sampler3D shB,
    vec3 worldPos,
    vec3 normal,
    vec3 volumeMin,
    vec3 volumeSize,
    float normalBias,
    float volumeFadeDistance)
{
    vec3 N = normalize(normal);
    if (!GI_IsInsideVolume(worldPos, volumeMin, volumeSize))
        return vec3(0.0);

    ivec3 texSize = textureSize(shR, 0);
    vec3 texelWorldSize = volumeSize / vec3(texSize);
    float cellSize = min(min(texelWorldSize.x, texelWorldSize.y), texelWorldSize.z);
    float receiverBias = max(normalBias, cellSize * 0.35);
    vec3 samplePos = worldPos + N * receiverBias;
    vec3 volumeMax = volumeMin + volumeSize;
    vec3 safeMin = volumeMin + texelWorldSize * 0.5;
    vec3 safeMax = volumeMax - texelWorldSize * 0.5;

    vec3 clampedSamplePos = clamp(samplePos, safeMin, safeMax);
    vec3 texPos = (clampedSamplePos - volumeMin) / volumeSize * vec3(texSize) - 0.5;
    ivec3 base = ivec3(floor(texPos));
    vec3 frac = clamp(texPos - floor(texPos), vec3(0.0), vec3(1.0));

    vec3 colorSum = vec3(0.0);
    float weightSum = 0.0;

    for (int dz = 0; dz <= 1; ++dz)
    for (int dy = 0; dy <= 1; ++dy)
    for (int dx = 0; dx <= 1; ++dx)
    {
        ivec3 offset = ivec3(dx, dy, dz);
        ivec3 tap = clamp(base + offset, ivec3(0), texSize - 1);

        vec3 blend = mix(1.0 - frac, frac, vec3(offset));
        float triW = blend.x * blend.y * blend.z;
        if (triW <= 0.0)
            continue;

        vec3 probeWorld = (vec3(tap) + 0.5) / vec3(texSize) * volumeSize + volumeMin;
        vec3 probeToReceiver = samplePos - probeWorld;
        float receiverDistance = length(probeToReceiver);
        vec3 receiverToProbeDir = (receiverDistance > 1e-5) ? (-probeToReceiver / receiverDistance) : N;

        float sameSide = smoothstep(-0.15, 0.85, dot(receiverToProbeDir, N));
        float normalW = mix(0.12, 1.0, sameSide);
        float w = triW * normalW;

        vec4 rC = texelFetch(shR, tap, 0);
        vec4 gC = texelFetch(shG, tap, 0);
        vec4 bC = texelFetch(shB, tap, 0);
        colorSum += GI_EvaluateDiffuseIrradianceL1(rC, gC, bC, N) * w;
        weightSum += w;
    }

    vec3 irradiance = (weightSum > 1e-5) ? (colorSum / weightSum) : vec3(0.0);
    irradiance *= GI_VolumeFade(worldPos, volumeMin, volumeSize, volumeFadeDistance);
    return max(irradiance * INV_PI, vec3(0.0));
}

vec3 GI_SampleProbeDiffuseLightingL1Visible(
    sampler3D shR,
    sampler3D shG,
    sampler3D shB,
    sampler2D visibilityAtlas,
    vec3 worldPos,
    vec3 normal,
    vec3 volumeMin,
    vec3 volumeSize,
    float normalBias,
    float volumeFadeDistance)
{
    vec3 N = normalize(normal);
    ivec3 texSize = textureSize(shR, 0);
    vec3 texelWorldSize = volumeSize / vec3(texSize);
    float cellSize = min(min(texelWorldSize.x, texelWorldSize.y), texelWorldSize.z);
    float receiverBias = max(normalBias, cellSize * 0.35);
    vec3 samplePos = worldPos + N * receiverBias;

    return GI_SampleProbeDiffuseLightingL1Internal(
        shR, shG, shB, visibilityAtlas,
        true,
        worldPos,
        samplePos,
        N,
        volumeMin,
        volumeSize,
        normalBias,
        volumeFadeDistance);
}

#endif
