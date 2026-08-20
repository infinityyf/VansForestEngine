#ifndef GI_PROBE_COMMON_GLSL_INCLUDED
#define GI_PROBE_COMMON_GLSL_INCLUDED

#include "../Common/Common.glsl"

#ifndef GI_SCREEN_FAST_RECONSTRUCTION
#define GI_SCREEN_FAST_RECONSTRUCTION 0
#endif

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
    vec2 tile = GI_OctahedralEncode(normalize(direction)) * float(GI_VISIBILITY_INTERIOR_RES - 1) +
        float(GI_ATLAS_BORDER) + 0.5;
    return GI_AtlasUVFromTileTexel(probeLinearIndex, tile, probesPerRow,
        GI_VISIBILITY_OCTA_RES, 1.0 / vec2(atlasSize));
}

vec2 GI_IrradianceAtlasUV(ivec3 probeIndex, vec3 direction, ivec3 probeCounts, ivec2 atlasSize)
{
    int probesPerRow = max(atlasSize.x / GI_IRRADIANCE_OCTA_RES, 1);
    uint probeLinearIndex = GI_ProbeAtlasLinearIndex(probeIndex, probeCounts);
    vec2 tile = GI_OctahedralEncode(normalize(direction)) * float(GI_IRRADIANCE_INTERIOR_RES - 1) +
        float(GI_ATLAS_BORDER) + 0.5;
    return GI_AtlasUVFromTileTexel(probeLinearIndex, tile, probesPerRow,
        GI_IRRADIANCE_OCTA_RES, 1.0 / vec2(atlasSize));
}

float GI_EvaluateProbeVisibilityMoments(
    vec2 moments,
    float receiverDistance,
    float normalBias,
    float cellSize)
{
    float meanDistance = moments.x;
    if (meanDistance <= 1e-4)
        return 1.0;

    float distanceToReceiver = max(receiverDistance - normalBias, 0.0);
    float softBias = max(cellSize * 0.12, normalBias * 2.0);
    if (distanceToReceiver <= meanDistance + softBias)
        return 1.0;

    float variance = max(moments.y - meanDistance * meanDistance,
        cellSize * cellSize * 0.02);
    float distanceDelta = distanceToReceiver - meanDistance;
    float chebyshev = variance / (variance + distanceDelta * distanceDelta);
    return clamp(chebyshev * chebyshev, 0.0, 1.0);
}

float GI_EvaluateProbeVisibilityTile(
    sampler2D visibilityAtlas,
    ivec2 probeTileIndex,
    vec3 normalizedProbeToReceiver,
    float receiverDistance,
    float normalBias,
    float cellSize,
    vec2 inverseAtlasSize)
{
    vec2 tile = GI_OctahedralEncode(normalizedProbeToReceiver) *
        float(GI_VISIBILITY_INTERIOR_RES - 1) +
        float(GI_ATLAS_BORDER) + 0.5;
    vec2 moments = textureLod(
        visibilityAtlas,
        GI_AtlasUVFromTileIndex(probeTileIndex, tile,
            GI_VISIBILITY_OCTA_RES, inverseAtlasSize),
        0.0).rg;
    return GI_EvaluateProbeVisibilityMoments(
        moments, receiverDistance, normalBias, cellSize);
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
    return GI_EvaluateProbeVisibilityMoments(
        moments, receiverDistance, normalBias, cellSize);
}

vec3 GI_SampleProbeIrradianceAtlasMeanNoState(
    ivec3 probeCounts,
    sampler2D irradianceAtlas,
    vec3 worldPos,
    vec3 volumeMin,
    vec3 volumeSize,
    float volumeFadeDistance)
{
    if (!GI_IsInsideVolume(worldPos, volumeMin, volumeSize))
        return vec3(0.0);

    vec3 texelWorldSize = volumeSize / vec3(probeCounts);
    vec3 samplePos = clamp(worldPos,
        volumeMin + texelWorldSize * 0.5,
        volumeMin + volumeSize - texelWorldSize * 0.5);
    vec3 texPos = (samplePos - volumeMin) / volumeSize * vec3(probeCounts) - 0.5;
    ivec3 base = ivec3(floor(texPos));
    vec3 frac = clamp(texPos - floor(texPos), vec3(0.0), vec3(1.0));

    const vec3 debugDirections[6] = vec3[6](
        vec3( 1.0,  0.0,  0.0),
        vec3(-1.0,  0.0,  0.0),
        vec3( 0.0,  1.0,  0.0),
        vec3( 0.0, -1.0,  0.0),
        vec3( 0.0,  0.0,  1.0),
        vec3( 0.0,  0.0, -1.0));

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    vec3 untrustedSum = vec3(0.0);
    float untrustedWeightSum = 0.0;
    ivec2 atlasSize = textureSize(irradianceAtlas, 0);
    for (int z = 0; z <= 1; ++z)
    for (int y = 0; y <= 1; ++y)
    for (int x = 0; x <= 1; ++x)
    {
        ivec3 tap = clamp(base + ivec3(x, y, z), ivec3(0), probeCounts - 1);
        vec3 blend = mix(1.0 - frac, frac, vec3(x, y, z));
        float triWeight = blend.x * blend.y * blend.z;
        if (triWeight <= 0.0)
            continue;

        vec3 probeMean = vec3(0.0);
        float confidence = 0.0;
        for (int directionIndex = 0; directionIndex < 6; ++directionIndex)
        {
            vec4 atlasSample = textureLod(irradianceAtlas,
                GI_IrradianceAtlasUV(tap, debugDirections[directionIndex], probeCounts, atlasSize),
                0.0);
            probeMean += atlasSample.rgb;
            confidence += clamp(atlasSample.a, 0.0, 1.0);
        }
        probeMean /= 6.0;
        confidence /= 6.0;
        sum += probeMean * triWeight * confidence;
        weightSum += triWeight * confidence;

        float untrustedWeight = triWeight * step(0.002, Luminance(probeMean) * INV_PI);
        untrustedSum += probeMean * untrustedWeight;
        untrustedWeightSum += untrustedWeight;
    }

    vec3 primary = weightSum > 1e-4 ? sum / weightSum : vec3(0.0);
    vec3 untrustedPrimary = untrustedWeightSum > 1e-4
        ? untrustedSum / untrustedWeightSum
        : vec3(0.0);
    if (weightSum >= 0.35 && Luminance(primary) * INV_PI >= 0.04)
        return max(primary *
            GI_VolumeFade(worldPos, volumeMin, volumeSize, volumeFadeDistance) * INV_PI,
            vec3(0.0));

    ivec3 centerProbe = clamp(ivec3(floor(texPos + vec3(0.5))), ivec3(0), probeCounts - 1);
    vec3 fillSum = vec3(0.0);
    float fillWeightSum = 0.0;
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        ivec3 tap = clamp(centerProbe + ivec3(x, y, z), ivec3(0), probeCounts - 1);
        vec3 probeCenter = vec3(tap);
        vec3 delta = texPos - probeCenter;
        float spatialWeight = max(1.5 - length(delta), 0.0);
        if (spatialWeight <= 0.0)
            continue;

        vec3 probeMean = vec3(0.0);
        float confidence = 0.0;
        for (int directionIndex = 0; directionIndex < 6; ++directionIndex)
        {
            vec4 atlasSample = textureLod(irradianceAtlas,
                GI_IrradianceAtlasUV(tap, debugDirections[directionIndex], probeCounts, atlasSize),
                0.0);
            probeMean += atlasSample.rgb;
            confidence += clamp(atlasSample.a, 0.0, 1.0);
        }
        probeMean /= 6.0;
        confidence /= 6.0;

        float weight = spatialWeight * confidence;
        fillSum += probeMean * weight;
        fillWeightSum += weight;
    }

    vec3 filled = fillWeightSum > 1e-4 ? fillSum / fillWeightSum : primary;
    if (fillWeightSum <= 1e-4 && untrustedWeightSum > 1e-4)
        filled = untrustedPrimary;
    if (fillWeightSum <= 1e-4 || Luminance(filled) * INV_PI < 0.04)
    {
        const ivec3 axialOffsets[6] = ivec3[6](
            ivec3( 2,  0,  0),
            ivec3(-2,  0,  0),
            ivec3( 0,  2,  0),
            ivec3( 0, -2,  0),
            ivec3( 0,  0,  2),
            ivec3( 0,  0, -2));
        vec3 axialSum = vec3(0.0);
        float axialWeightSum = 0.0;
        for (int axialIndex = 0; axialIndex < 6; ++axialIndex)
        {
            ivec3 tap = clamp(centerProbe + axialOffsets[axialIndex], ivec3(0), probeCounts - 1);
            vec3 probeMean = vec3(0.0);
            float confidence = 0.0;
            for (int directionIndex = 0; directionIndex < 6; ++directionIndex)
            {
                vec4 atlasSample = textureLod(irradianceAtlas,
                    GI_IrradianceAtlasUV(tap, debugDirections[directionIndex], probeCounts, atlasSize),
                    0.0);
                probeMean += atlasSample.rgb;
                confidence += clamp(atlasSample.a, 0.0, 1.0);
            }
            probeMean /= 6.0;
            confidence /= 6.0;
            float weight = confidence * step(0.04, Luminance(probeMean) * INV_PI);
            axialSum += probeMean * weight;
            axialWeightSum += weight;
        }
        if (axialWeightSum > 1e-4)
            filled = max(filled, axialSum / axialWeightSum);
        else if (untrustedWeightSum > 1e-4)
            filled = max(filled, untrustedPrimary);
    }
    float primaryWeight = smoothstep(0.08, 0.35, weightSum);
    return max(mix(filled, primary, primaryWeight) *
        GI_VolumeFade(worldPos, volumeMin, volumeSize, volumeFadeDistance) * INV_PI,
        vec3(0.0));
}

vec3 GI_SampleProbeIrradianceAtlasDirectionalNoState(
    ivec3 probeCounts,
    sampler2D irradianceAtlas,
    vec3 worldPos,
    vec3 normal,
    vec3 volumeMin,
    vec3 volumeSize,
    float volumeFadeDistance)
{
    if (!GI_IsInsideVolume(worldPos, volumeMin, volumeSize))
        return vec3(0.0);

    vec3 texelWorldSize = volumeSize / vec3(probeCounts);
    vec3 samplePos = clamp(worldPos,
        volumeMin + texelWorldSize * 0.5,
        volumeMin + volumeSize - texelWorldSize * 0.5);
    vec3 texPos = (samplePos - volumeMin) / volumeSize * vec3(probeCounts) - 0.5;
    ivec3 base = ivec3(floor(texPos));
    vec3 frac = clamp(texPos - floor(texPos), vec3(0.0), vec3(1.0));
    vec3 N = dot(normal, normal) > 1e-6 ? normalize(normal) : vec3(0.0, 1.0, 0.0);

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    ivec2 atlasSize = textureSize(irradianceAtlas, 0);
    for (int z = 0; z <= 1; ++z)
    for (int y = 0; y <= 1; ++y)
    for (int x = 0; x <= 1; ++x)
    {
        ivec3 tap = clamp(base + ivec3(x, y, z), ivec3(0), probeCounts - 1);
        vec3 blend = mix(1.0 - frac, frac, vec3(x, y, z));
        float triWeight = blend.x * blend.y * blend.z;
        if (triWeight <= 0.0)
            continue;

        vec4 atlasSample = textureLod(irradianceAtlas,
            GI_IrradianceAtlasUV(tap, N, probeCounts, atlasSize), 0.0);
        float confidence = clamp(atlasSample.a, 0.0, 1.0);
        sum += atlasSample.rgb * triWeight * confidence;
        weightSum += triWeight * confidence;
    }

    if (weightSum <= 1e-4)
    {
        return GI_SampleProbeIrradianceAtlasMeanNoState(
            probeCounts, irradianceAtlas, worldPos, volumeMin, volumeSize,
            volumeFadeDistance);
    }

    return max(sum / weightSum *
        GI_VolumeFade(worldPos, volumeMin, volumeSize, volumeFadeDistance) * INV_PI,
        vec3(0.0));
}

#ifdef GI_LOAD_PROBE_STATE
vec3 GI_SampleProbeIrradianceAtlasRaw(
    uint regionIndex,
    ivec3 probeCounts,
    sampler2D irradianceAtlas,
    vec3 worldPos,
    vec3 normal,
    vec3 volumeMin,
    vec3 volumeSize,
    float normalBias,
    float volumeFadeDistance)
{
    if (!GI_IsInsideVolume(worldPos, volumeMin, volumeSize))
        return vec3(0.0);

    vec3 texelWorldSize = volumeSize / vec3(probeCounts);
    float cellSize = min(min(texelWorldSize.x, texelWorldSize.y), texelWorldSize.z);
    vec3 N = normalize(normal);
    vec3 samplePos = clamp(worldPos + N * max(normalBias, cellSize * 0.35),
        volumeMin + texelWorldSize * 0.5,
        volumeMin + volumeSize - texelWorldSize * 0.5);
    vec3 texPos = (samplePos - volumeMin) / volumeSize * vec3(probeCounts) - 0.5;
    ivec3 base = ivec3(floor(texPos));
    vec3 frac = clamp(texPos - floor(texPos), vec3(0.0), vec3(1.0));

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    ivec2 atlasSize = textureSize(irradianceAtlas, 0);
    for (int z = 0; z <= 1; ++z)
    for (int y = 0; y <= 1; ++y)
    for (int x = 0; x <= 1; ++x)
    {
        ivec3 tap = clamp(base + ivec3(x, y, z), ivec3(0), probeCounts - 1);
        vec3 blend = mix(1.0 - frac, frac, vec3(x, y, z));
        float triWeight = blend.x * blend.y * blend.z;
        if (triWeight <= 0.0)
            continue;

        vec4 atlasSample = textureLod(irradianceAtlas,
            GI_IrradianceAtlasUV(tap, N, probeCounts, atlasSize), 0.0);
        float confidenceWeight = clamp(atlasSample.a, 0.0, 1.0);
        sum += atlasSample.rgb * triWeight * confidenceWeight;
        weightSum += triWeight * confidenceWeight;
    }

    if (weightSum <= 1e-4)
        return vec3(0.0);
    return max(sum / weightSum *
        GI_VolumeFade(worldPos, volumeMin, volumeSize, volumeFadeDistance) * INV_PI,
        vec3(0.0));
}

vec3 GI_SampleProbeIrradianceAtlasVisible(
    uint regionIndex,
    ivec3 probeCounts,
    sampler2D irradianceAtlas,
    sampler2D visibilityAtlas,
    vec3 worldPos,
    vec3 normal,
    vec3 volumeMin,
    vec3 volumeSize,
    float normalBias,
    float volumeFadeDistance)
{
    if (!GI_IsInsideVolume(worldPos, volumeMin, volumeSize))
        return vec3(0.0);

    vec3 texelWorldSize = volumeSize / vec3(probeCounts);
    float cellSize = min(min(texelWorldSize.x, texelWorldSize.y), texelWorldSize.z);
    vec3 N = normalize(normal);
    vec3 samplePos = clamp(worldPos + N * max(normalBias, cellSize * 0.35),
        volumeMin + texelWorldSize * 0.5,
        volumeMin + volumeSize - texelWorldSize * 0.5);
    vec3 texPos = (samplePos - volumeMin) / volumeSize * vec3(probeCounts) - 0.5;
    ivec3 base = ivec3(floor(texPos));
    vec3 frac = clamp(texPos - floor(texPos), vec3(0.0), vec3(1.0));

    vec3 sum = vec3(0.0);
    float weightSum = 0.0;
    ivec2 atlasSize = textureSize(irradianceAtlas, 0);
    for (int z = 0; z <= 1; ++z)
    for (int y = 0; y <= 1; ++y)
    for (int x = 0; x <= 1; ++x)
    {
        ivec3 tap = clamp(base + ivec3(x, y, z), ivec3(0), probeCounts - 1);
        vec3 blend = mix(1.0 - frac, frac, vec3(x, y, z));
        float triWeight = blend.x * blend.y * blend.z;
        if (triWeight <= 0.0)
            continue;

        uint probeLinearIndex = GI_ProbeAtlasLinearIndex(tap, probeCounts);
        GIProbeState probeState = GI_LOAD_PROBE_STATE(regionIndex, probeLinearIndex);
        if (probeState.metadata.x != 1u)
            continue;

        vec3 probeWorld = (vec3(tap) + 0.5) / vec3(probeCounts) * volumeSize +
            volumeMin + probeState.relocationAndConfidence.xyz;
        vec3 probeToReceiver = samplePos - probeWorld;
        float receiverDistance = length(probeToReceiver);
        vec3 direction = receiverDistance > 1e-5 ? probeToReceiver / receiverDistance : N;

        // Receiver support must be continuous.  A hard hemisphere cut rejects
        // the entire 8-probe neighborhood on thin walls and normal-mapped
        // surfaces, so the surviving single tap gets normalized into a grid
        // spot or the pixel drops to black.  DDGI reconstruction uses a
        // wrapped normal term: probes on the correct side still dominate,
        // perpendicular probes keep small support, and probes fully behind the
        // surface remain rejected.
        float normalWeight = clamp(dot(-direction, N) * 0.5 + 0.5, 0.0, 1.0);
        normalWeight *= normalWeight;
        if (normalWeight <= 0.0)
            continue;

        float visibilityWeight = GI_EvaluateProbeVisibility(visibilityAtlas, tap, direction,
            receiverDistance, probeCounts, normalBias, cellSize);
        if (visibilityWeight <= 0.0)
            continue;

        vec4 atlasSample = textureLod(irradianceAtlas,
            GI_IrradianceAtlasUV(tap, N, probeCounts, atlasSize), 0.0);
        float confidenceWeight = clamp(atlasSample.a, 0.0, 1.0) *
            clamp(probeState.relocationAndConfidence.w, 0.0, 1.0);
        float weight = triWeight * normalWeight * visibilityWeight * confidenceWeight;
        sum += atlasSample.rgb * weight;
        weightSum += weight;
    }

    if (weightSum <= 1e-4)
        return vec3(0.0);
    // Visibility can legitimately reject most of the 8-cell neighborhood near
    // walls and thin geometry.  Normalizing a single surviving tap to full
    // energy turns one bad or freshly-updated probe into a visible grid spot.
    // Treat very low neighborhood support as unreliable instead of amplifying
    // it; once several confident/visible taps contribute, interpolation remains
    // energy-normalized.
    float neighborhoodSupport = smoothstep(0.05, 0.25, weightSum);
    return max(sum / weightSum *
        neighborhoodSupport *
        GI_VolumeFade(worldPos, volumeMin, volumeSize, volumeFadeDistance) * INV_PI,
        vec3(0.0));
}

vec4 GI_SampleProbeIrradianceAtlasScreenTap(
    sampler2D irradianceAtlas,
    ivec3 probeIndex,
    vec3 normal,
    ivec3 probeCounts,
    ivec2 atlasSize)
{
    vec4 directional = textureLod(irradianceAtlas,
        GI_IrradianceAtlasUV(probeIndex, normal, probeCounts, atlasSize), 0.0);

#if GI_SCREEN_FAST_RECONSTRUCTION
    float axisSignX = normal.x >= 0.0 ? 1.0 : -1.0;
    float axisSignY = normal.y >= 0.0 ? 1.0 : -1.0;
    float axisSignZ = normal.z >= 0.0 ? 1.0 : -1.0;
    vec4 axialX = textureLod(irradianceAtlas,
        GI_IrradianceAtlasUV(probeIndex, vec3(axisSignX, 0.0, 0.0), probeCounts, atlasSize),
        0.0);
    vec4 axialY = textureLod(irradianceAtlas,
        GI_IrradianceAtlasUV(probeIndex, vec3(0.0, axisSignY, 0.0), probeCounts, atlasSize),
        0.0);
    vec4 axialZ = textureLod(irradianceAtlas,
        GI_IrradianceAtlasUV(probeIndex, vec3(0.0, 0.0, axisSignZ), probeCounts, atlasSize),
        0.0);

    vec3 axialRgb = (axialX.rgb + axialY.rgb + axialZ.rgb) * (1.0 / 3.0);
    float axialConfidence = (axialX.a + axialY.a + axialZ.a) * (1.0 / 3.0);

    const float directionalBlend = 0.35;
#else
    const vec3 axialDirections[6] = vec3[6](
        vec3( 1.0,  0.0,  0.0),
        vec3(-1.0,  0.0,  0.0),
        vec3( 0.0,  1.0,  0.0),
        vec3( 0.0, -1.0,  0.0),
        vec3( 0.0,  0.0,  1.0),
        vec3( 0.0,  0.0, -1.0));

    vec3 axialRgb = vec3(0.0);
    float axialConfidence = 0.0;
    for (int directionIndex = 0; directionIndex < 6; ++directionIndex)
    {
        vec4 axial = textureLod(irradianceAtlas,
            GI_IrradianceAtlasUV(probeIndex, axialDirections[directionIndex], probeCounts, atlasSize),
            0.0);
        axialRgb += axial.rgb;
        axialConfidence += axial.a;
    }
    axialRgb /= 6.0;
    axialConfidence /= 6.0;

    // Screen reconstruction should be lower-frequency than the probe atlas
    // itself.  A high directional texel weight makes sparse per-probe octa
    // differences visible as grid-aligned bands on large flat surfaces.
    const float directionalBlend = 0.22;
#endif
    return vec4(
        mix(axialRgb, directional.rgb, directionalBlend),
        mix(axialConfidence, directional.a, directionalBlend));
}

#if GI_SCREEN_FAST_RECONSTRUCTION
void GI_AccumulateScreenNeighborhoodTap(
    uint regionIndex,
    sampler2D irradianceAtlas,
    ivec3 probeCounts,
    ivec3 centerProbe,
    ivec3 offset,
    vec3 texPos,
    vec3 samplePos,
    vec3 N,
    vec3 volumeMin,
    vec3 volumeSize,
    ivec2 atlasSize,
    inout vec3 sum,
    inout float weightSum,
    inout float luminanceSum,
    inout float luminanceSqSum)
{
    ivec3 tap = clamp(centerProbe + offset, ivec3(0), probeCounts - 1);
    uint probeLinearIndex = GI_ProbeAtlasLinearIndex(tap, probeCounts);
    GIProbeState probeState = GI_LOAD_PROBE_STATE(regionIndex, probeLinearIndex);
    if (probeState.metadata.x != 1u)
        return;

    vec4 atlasSample = GI_SampleProbeIrradianceAtlasScreenTap(
        irradianceAtlas, tap, N, probeCounts, atlasSize);
    float confidenceWeight = clamp(atlasSample.a, 0.0, 1.0) *
        clamp(probeState.relocationAndConfidence.w, 0.0, 1.0);
    if (confidenceWeight <= 1e-4)
        return;

    vec3 delta = texPos - vec3(tap);
    float spatialWeight = max(2.0 - length(delta), 0.0);
    spatialWeight = spatialWeight * spatialWeight * spatialWeight;
    if (spatialWeight <= 0.0)
        return;

    vec3 probeWorld = (vec3(tap) + 0.5) / vec3(probeCounts) * volumeSize +
        volumeMin + probeState.relocationAndConfidence.xyz;
    vec3 probeToReceiver = samplePos - probeWorld;
    float receiverDistance = length(probeToReceiver);
    vec3 direction = receiverDistance > 1e-5 ? probeToReceiver / receiverDistance : N;
    float normalWeight = clamp(dot(-direction, N) * 0.5 + 0.5, 0.0, 1.0);
    float normalSupport = normalWeight * normalWeight;
    float weight = spatialWeight * confidenceWeight * mix(0.25, 1.0, normalSupport);
    sum += atlasSample.rgb * weight;
    float sampleLuminance = Luminance(atlasSample.rgb);
    luminanceSum += sampleLuminance * weight;
    luminanceSqSum += sampleLuminance * sampleLuminance * weight;
    weightSum += weight;
}
#endif

vec3 GI_SampleProbeIrradianceAtlasScreenNeighborhood(
    uint regionIndex,
    sampler2D irradianceAtlas,
    ivec3 probeCounts,
    vec3 texPos,
    vec3 samplePos,
    vec3 normal,
    vec3 volumeMin,
    vec3 volumeSize,
    ivec2 atlasSize,
    out float weightSum,
    out float luminanceVariance)
{
    vec3 N = dot(normal, normal) > 1e-6 ? normalize(normal) : vec3(0.0, 1.0, 0.0);
    ivec3 centerProbe = clamp(ivec3(floor(texPos + vec3(0.5))), ivec3(0), probeCounts - 1);
    vec3 sum = vec3(0.0);
    weightSum = 0.0;
    float luminanceSum = 0.0;
    float luminanceSqSum = 0.0;
#if GI_SCREEN_FAST_RECONSTRUCTION
    GI_AccumulateScreenNeighborhoodTap(regionIndex, irradianceAtlas, probeCounts, centerProbe,
        ivec3( 0,  0,  0), texPos, samplePos, N, volumeMin, volumeSize, atlasSize,
        sum, weightSum, luminanceSum, luminanceSqSum);
    GI_AccumulateScreenNeighborhoodTap(regionIndex, irradianceAtlas, probeCounts, centerProbe,
        ivec3( 1,  0,  0), texPos, samplePos, N, volumeMin, volumeSize, atlasSize,
        sum, weightSum, luminanceSum, luminanceSqSum);
    GI_AccumulateScreenNeighborhoodTap(regionIndex, irradianceAtlas, probeCounts, centerProbe,
        ivec3(-1,  0,  0), texPos, samplePos, N, volumeMin, volumeSize, atlasSize,
        sum, weightSum, luminanceSum, luminanceSqSum);
    GI_AccumulateScreenNeighborhoodTap(regionIndex, irradianceAtlas, probeCounts, centerProbe,
        ivec3( 0,  1,  0), texPos, samplePos, N, volumeMin, volumeSize, atlasSize,
        sum, weightSum, luminanceSum, luminanceSqSum);
    GI_AccumulateScreenNeighborhoodTap(regionIndex, irradianceAtlas, probeCounts, centerProbe,
        ivec3( 0, -1,  0), texPos, samplePos, N, volumeMin, volumeSize, atlasSize,
        sum, weightSum, luminanceSum, luminanceSqSum);
    GI_AccumulateScreenNeighborhoodTap(regionIndex, irradianceAtlas, probeCounts, centerProbe,
        ivec3( 0,  0,  1), texPos, samplePos, N, volumeMin, volumeSize, atlasSize,
        sum, weightSum, luminanceSum, luminanceSqSum);
    GI_AccumulateScreenNeighborhoodTap(regionIndex, irradianceAtlas, probeCounts, centerProbe,
        ivec3( 0,  0, -1), texPos, samplePos, N, volumeMin, volumeSize, atlasSize,
        sum, weightSum, luminanceSum, luminanceSqSum);
#else
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        ivec3 tap = clamp(centerProbe + ivec3(x, y, z), ivec3(0), probeCounts - 1);
        uint probeLinearIndex = GI_ProbeAtlasLinearIndex(tap, probeCounts);
        GIProbeState probeState = GI_LOAD_PROBE_STATE(regionIndex, probeLinearIndex);
        if (probeState.metadata.x != 1u)
            continue;

        vec4 atlasSample = GI_SampleProbeIrradianceAtlasScreenTap(
            irradianceAtlas, tap, N, probeCounts, atlasSize);
        float confidenceWeight = clamp(atlasSample.a, 0.0, 1.0) *
            clamp(probeState.relocationAndConfidence.w, 0.0, 1.0);
        if (confidenceWeight <= 1e-4)
            continue;

        vec3 delta = texPos - vec3(tap);
        float spatialWeight = max(2.0 - length(delta), 0.0);
        spatialWeight = spatialWeight * spatialWeight * spatialWeight;
        if (spatialWeight <= 0.0)
            continue;

        vec3 probeWorld = (vec3(tap) + 0.5) / vec3(probeCounts) * volumeSize +
            volumeMin + probeState.relocationAndConfidence.xyz;
        vec3 probeToReceiver = samplePos - probeWorld;
        float receiverDistance = length(probeToReceiver);
        vec3 direction = receiverDistance > 1e-5 ? probeToReceiver / receiverDistance : N;
        float normalWeight = clamp(dot(-direction, N) * 0.5 + 0.5, 0.0, 1.0);
        float normalSupport = normalWeight * normalWeight;
        float weight = spatialWeight * confidenceWeight * mix(0.25, 1.0, normalSupport);
        sum += atlasSample.rgb * weight;
        float sampleLuminance = Luminance(atlasSample.rgb);
        luminanceSum += sampleLuminance * weight;
        luminanceSqSum += sampleLuminance * sampleLuminance * weight;
        weightSum += weight;
    }
#endif

    if (weightSum <= 1e-4)
    {
        luminanceVariance = 0.0;
        return vec3(0.0);
    }

    vec3 mean = sum / weightSum;
    float meanLuminance = luminanceSum / weightSum;
    luminanceVariance = max(luminanceSqSum / weightSum - meanLuminance * meanLuminance, 0.0);
    return mean;
}

vec3 GI_SampleProbeIrradianceAtlasScreenVisible(
    uint regionIndex,
    ivec3 probeCounts,
    sampler2D irradianceAtlas,
    sampler2D visibilityAtlas,
    vec3 worldPos,
    vec3 normal,
    vec3 volumeMin,
    vec3 volumeSize,
    float normalBias,
    float volumeFadeDistance)
{
    if (!GI_IsInsideVolume(worldPos, volumeMin, volumeSize))
        return vec3(0.0);

    vec3 N = dot(normal, normal) > 1e-6 ? normalize(normal) : vec3(0.0, 1.0, 0.0);
    vec3 texelWorldSize = volumeSize / vec3(probeCounts);
    float cellSize = min(min(texelWorldSize.x, texelWorldSize.y), texelWorldSize.z);
    vec3 samplePos = clamp(worldPos + N * max(normalBias, cellSize * 0.35),
        volumeMin + texelWorldSize * 0.5,
        volumeMin + volumeSize - texelWorldSize * 0.5);
    vec3 texPos = (samplePos - volumeMin) / volumeSize * vec3(probeCounts) - 0.5;
    ivec3 base = ivec3(floor(texPos));
    vec3 frac = clamp(texPos - floor(texPos), vec3(0.0), vec3(1.0));

    vec3 visibleSum = vec3(0.0);
    float visibleWeightSum = 0.0;
    vec3 relocatedSum = vec3(0.0);
    float relocatedWeightSum = 0.0;
    ivec2 atlasSize = textureSize(irradianceAtlas, 0);
    for (int z = 0; z <= 1; ++z)
    for (int y = 0; y <= 1; ++y)
    for (int x = 0; x <= 1; ++x)
    {
        ivec3 tap = clamp(base + ivec3(x, y, z), ivec3(0), probeCounts - 1);
        vec3 blend = mix(1.0 - frac, frac, vec3(x, y, z));
        float triWeight = blend.x * blend.y * blend.z;
        if (triWeight <= 0.0)
            continue;

        uint probeLinearIndex = GI_ProbeAtlasLinearIndex(tap, probeCounts);
        GIProbeState probeState = GI_LOAD_PROBE_STATE(regionIndex, probeLinearIndex);
        if (probeState.metadata.x != 1u)
            continue;

        vec4 atlasSample = GI_SampleProbeIrradianceAtlasScreenTap(
            irradianceAtlas, tap, N, probeCounts, atlasSize);
        float confidenceWeight = clamp(atlasSample.a, 0.0, 1.0) *
            clamp(probeState.relocationAndConfidence.w, 0.0, 1.0);
        if (confidenceWeight <= 1e-4)
            continue;

        vec3 probeWorld = (vec3(tap) + 0.5) / vec3(probeCounts) * volumeSize +
            volumeMin + probeState.relocationAndConfidence.xyz;
        vec3 probeToReceiver = samplePos - probeWorld;
        float receiverDistance = length(probeToReceiver);
        vec3 direction = receiverDistance > 1e-5 ? probeToReceiver / receiverDistance : N;

        float normalWeight = clamp(dot(-direction, N) * 0.5 + 0.5, 0.0, 1.0);
        normalWeight *= normalWeight;
        float relocatedWeight = triWeight * normalWeight * confidenceWeight;
        if (relocatedWeight <= 0.0)
            continue;

        relocatedSum += atlasSample.rgb * relocatedWeight;
        relocatedWeightSum += relocatedWeight;

        float visibilityWeight = GI_EvaluateProbeVisibility(visibilityAtlas, tap, direction,
            receiverDistance, probeCounts, normalBias, cellSize);
        float visibleWeight = relocatedWeight * visibilityWeight;
        visibleSum += atlasSample.rgb * visibleWeight;
        visibleWeightSum += visibleWeight;
    }

    float lowFrequencyWeight = 0.0;
    float lowFrequencyVariance = 0.0;
    vec3 lowFrequencyIrradiance = vec3(0.0);
#if GI_SCREEN_FAST_RECONSTRUCTION
    bool needLowFrequency = relocatedWeightSum <= 1e-4 ||
        relocatedWeightSum < 0.18 ||
        visibleWeightSum < 0.035;
    if (needLowFrequency)
#endif
    {
        lowFrequencyIrradiance = GI_SampleProbeIrradianceAtlasScreenNeighborhood(
            regionIndex, irradianceAtlas, probeCounts, texPos, samplePos, N,
            volumeMin, volumeSize, atlasSize, lowFrequencyWeight, lowFrequencyVariance);
    }

    if (relocatedWeightSum <= 1e-4)
    {
        if (lowFrequencyWeight > 1e-4)
        {
            return max(lowFrequencyIrradiance *
                GI_VolumeFade(worldPos, volumeMin, volumeSize, volumeFadeDistance) * INV_PI,
                vec3(0.0));
        }
        vec3 directionalFallback = GI_SampleProbeIrradianceAtlasDirectionalNoState(
            probeCounts, irradianceAtlas, worldPos, N, volumeMin, volumeSize,
            volumeFadeDistance);
        vec3 meanFallback = GI_SampleProbeIrradianceAtlasMeanNoState(
            probeCounts, irradianceAtlas, worldPos, volumeMin, volumeSize,
            volumeFadeDistance);
        return mix(meanFallback, directionalFallback, 0.7);
    }

    vec3 relocatedIrradiance = relocatedSum / relocatedWeightSum;
    vec3 visibleIrradiance = visibleWeightSum > 1e-4
        ? visibleSum / visibleWeightSum
        : relocatedIrradiance;
    float visibilitySupport = smoothstep(0.005, 0.08, visibleWeightSum);
    vec3 irradiance = mix(relocatedIrradiance, visibleIrradiance, visibilitySupport);
    if (lowFrequencyWeight > 1e-4)
    {
        float highFrequencySupport = smoothstep(0.12, 0.45, relocatedWeightSum) *
            smoothstep(0.02, 0.16, visibleWeightSum);
        float lowFrequencyMean = max(Luminance(lowFrequencyIrradiance), 0.03);
        float neighborhoodInstability = smoothstep(0.10, 0.35,
            sqrt(lowFrequencyVariance) / lowFrequencyMean);
        float highFrequencyBlend = mix(0.0, 0.24, highFrequencySupport) *
            mix(1.0, 0.45, neighborhoodInstability);
        float lowFrequencyLum = Luminance(lowFrequencyIrradiance);
        float highFrequencyLum = Luminance(irradiance);
        float brightOutlier = smoothstep(
            max(lowFrequencyLum * 1.8, lowFrequencyLum + 0.04),
            max(lowFrequencyLum * 3.2, lowFrequencyLum + 0.18),
            highFrequencyLum);
        highFrequencyBlend *= mix(1.0, 0.15, brightOutlier);
        vec3 highFrequencyCeiling = lowFrequencyIrradiance *
            mix(1.6, 2.8, highFrequencySupport) +
            vec3(mix(0.05, 0.18, highFrequencySupport));
        irradiance = min(irradiance, highFrequencyCeiling);
        irradiance = mix(lowFrequencyIrradiance, irradiance, highFrequencyBlend);
    }
    return max(irradiance *
        GI_VolumeFade(worldPos, volumeMin, volumeSize, volumeFadeDistance) * INV_PI,
        vec3(0.0));
}

// Screen-probe cache build path. The supplied irradiance atlas already contains
// axial compensation produced by GIVisibilityUpdate, so every trilinear probe
// tap performs exactly one irradiance and one visibility fetch.
vec3 GI_SampleProbeIrradianceAtlasScreenVisiblePrefiltered(
    uint regionIndex,
    ivec3 probeCounts,
    sampler2D screenIrradianceAtlas,
    sampler2D visibilityAtlas,
    vec3 worldPos,
    vec3 normal,
    vec3 volumeMin,
    vec3 volumeSize,
    float normalBias,
    float volumeWeight,
    int cachedProbesPerRow,
    int cachedProbeRows,
    out float probeSupport)
{
    probeSupport = 0.0;
    // This entry point is private to SSGIProbeCache.  Region selection has
    // already proved that worldPos is inside the winning volume and computed
    // its fade weight, while ComputeCacheGeometricNormal supplies a unit
    // normal.  Reusing both values removes duplicate bounds/fade work and two
    // normalizations without changing the cache sampling formula.
    vec3 N = normal;
    vec3 texelWorldSize = volumeSize / vec3(probeCounts);
    float cellSize = min(min(texelWorldSize.x, texelWorldSize.y), texelWorldSize.z);
    vec3 samplePos = clamp(worldPos + N * max(normalBias, cellSize * 0.35),
        volumeMin + texelWorldSize * 0.5,
        volumeMin + volumeSize - texelWorldSize * 0.5);
    vec3 texPos = (samplePos - volumeMin) / volumeSize * vec3(probeCounts) - 0.5;
    vec3 flooredTexPos = floor(texPos);
    ivec3 base = ivec3(flooredTexPos);
    vec3 frac = clamp(texPos - flooredTexPos, vec3(0.0), vec3(1.0));
    ivec2 atlasSize;
    ivec2 visibilityAtlasSize;
    int irradianceProbesPerRow;
    int visibilityProbesPerRow;
    bool visibilityAtlasValid;
    if (cachedProbesPerRow > 0 && cachedProbeRows > 0)
    {
        // Normal path: the CPU publishes the immutable tile grid used by all
        // three DDGI atlases.  No per-cache-texel image query or integer
        // division is required.
        irradianceProbesPerRow = cachedProbesPerRow;
        visibilityProbesPerRow = cachedProbesPerRow;
        atlasSize = ivec2(cachedProbesPerRow, cachedProbeRows) *
            GI_IRRADIANCE_OCTA_RES;
        visibilityAtlasSize = ivec2(cachedProbesPerRow, cachedProbeRows) *
            GI_VISIBILITY_OCTA_RES;
        visibilityAtlasValid = true;
    }
    else
    {
        // Compatibility path for shader hot reload against an older CPU UBO.
        // This also prevents an include/binary mismatch from silently turning
        // the GI result black.
        atlasSize = textureSize(screenIrradianceAtlas, 0);
        visibilityAtlasSize = textureSize(visibilityAtlas, 0);
        irradianceProbesPerRow = max(
            atlasSize.x / GI_IRRADIANCE_OCTA_RES, 1);
        visibilityProbesPerRow = max(
            visibilityAtlasSize.x / GI_VISIBILITY_OCTA_RES, 1);
        int totalProbeCount = probeCounts.x * probeCounts.y * probeCounts.z;
        int visibilityProbeRows =
            (totalProbeCount + visibilityProbesPerRow - 1) /
            visibilityProbesPerRow;
        visibilityAtlasValid =
            visibilityAtlasSize.x >= GI_VISIBILITY_OCTA_RES &&
            visibilityAtlasSize.y >= visibilityProbeRows * GI_VISIBILITY_OCTA_RES;
    }
    vec2 inverseIrradianceAtlasSize = 1.0 / vec2(atlasSize);
    vec2 inverseVisibilityAtlasSize = 1.0 / vec2(visibilityAtlasSize);
    vec2 irradianceTileTexel = GI_OctahedralEncode(N) *
        float(GI_IRRADIANCE_INTERIOR_RES - 1) +
        float(GI_ATLAS_BORDER) + 0.5;

    vec3 visibleSum = vec3(0.0);
    float visibleWeightSum = 0.0;
    vec3 relocatedSum = vec3(0.0);
    float relocatedWeightSum = 0.0;
    for (int z = 0; z <= 1; ++z)
    for (int y = 0; y <= 1; ++y)
    for (int x = 0; x <= 1; ++x)
    {
        ivec3 tap = clamp(base + ivec3(x, y, z), ivec3(0), probeCounts - 1);
        vec3 blend = mix(1.0 - frac, frac, vec3(x, y, z));
        float triWeight = blend.x * blend.y * blend.z;
        if (triWeight <= 0.0)
            continue;

        uint probeLinearIndex = GI_ProbeAtlasLinearIndex(tap, probeCounts);
        GIProbeState probeState = GI_LOAD_PROBE_STATE(regionIndex, probeLinearIndex);
        if (probeState.metadata.x != 1u)
            continue;

        // Both DDGI atlases are created from the same probe tile grid.  Reuse
        // the quotient/remainder result so the steady-state screen-cache path
        // does not repeat integer atlas addressing for irradiance and
        // visibility.  Keep the uncommon mismatched-layout case compatible.
        ivec2 irradianceTileIndex = GI_AtlasTileIndex(
            probeLinearIndex, irradianceProbesPerRow);
        ivec2 visibilityTileIndex =
            visibilityProbesPerRow == irradianceProbesPerRow
            ? irradianceTileIndex
            : GI_AtlasTileIndex(probeLinearIndex, visibilityProbesPerRow);
        vec4 atlasSample = textureLod(screenIrradianceAtlas,
            GI_AtlasUVFromTileIndex(
                irradianceTileIndex, irradianceTileTexel,
                GI_IRRADIANCE_OCTA_RES, inverseIrradianceAtlasSize),
            0.0);
        float confidenceWeight = clamp(atlasSample.a, 0.0, 1.0) *
            clamp(probeState.relocationAndConfidence.w, 0.0, 1.0);
        if (confidenceWeight <= 1e-4)
            continue;

        vec3 probeWorld = (vec3(tap) + 0.5) * texelWorldSize +
            volumeMin + probeState.relocationAndConfidence.xyz;
        vec3 probeToReceiver = samplePos - probeWorld;
        float receiverDistance = length(probeToReceiver);
        vec3 direction = receiverDistance > 1e-5 ? probeToReceiver / receiverDistance : N;
        float normalWeight = clamp(dot(-direction, N) * 0.5 + 0.5, 0.0, 1.0);
        normalWeight *= normalWeight;
        float relocatedWeight = triWeight * normalWeight * confidenceWeight;
        if (relocatedWeight <= 0.0)
            continue;

        relocatedSum += atlasSample.rgb * relocatedWeight;
        relocatedWeightSum += relocatedWeight;
        float visibilityWeight = visibilityAtlasValid
            ? GI_EvaluateProbeVisibilityTile(
                visibilityAtlas, visibilityTileIndex, direction,
                receiverDistance, normalBias, cellSize,
                inverseVisibilityAtlasSize)
            : 1.0;
        float visibleWeight = relocatedWeight * visibilityWeight;
        visibleSum += atlasSample.rgb * visibleWeight;
        visibleWeightSum += visibleWeight;
    }

    if (relocatedWeightSum <= 1e-4)
    {
        // 屏幕查询缓存负责用 probeSupport 与天空光平滑混合。这里不能再进入
        // 无状态方向/均值扫描，否则探针预热时会重新触发数十次 atlas fetch，
        // 破坏该路径“每探针最多一次 irradiance + visibility”的采样上限。
        probeSupport = 0.0;
        return vec3(0.0);
    }

    vec3 relocatedIrradiance = relocatedSum / relocatedWeightSum;
    vec3 visibleIrradiance = visibleWeightSum > 1e-4
        ? visibleSum / visibleWeightSum
        : relocatedIrradiance;
    float visibilitySupport = smoothstep(0.005, 0.08, visibleWeightSum);
    probeSupport = smoothstep(0.01, 0.10, relocatedWeightSum);
    vec3 irradiance = mix(relocatedIrradiance, visibleIrradiance, visibilitySupport);
    return max(irradiance *
        volumeWeight * INV_PI,
        vec3(0.0));
}
#endif

#endif
