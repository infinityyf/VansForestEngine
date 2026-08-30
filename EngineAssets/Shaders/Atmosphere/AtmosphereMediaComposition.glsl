#ifndef VANS_ATMOSPHERE_MEDIA_COMPOSITION_GLSL
#define VANS_ATMOSPHERE_MEDIA_COMPOSITION_GLSL

#include "AtmosphereCommon.glsl"

vec3 SampleFarAerialScatteringAt(vec2 uv, float distanceMeters)
{
    if (distanceMeters <= 0.0)
        return vec3(0.0);
    float slice = AtmosphereAerialSliceCoordinate(distanceMeters);
    float sliceCount =
        max(uAtmosphereFrame.aerialPerspectiveParameters.z, 1.0);
    float centeredSlice =
        (slice * (sliceCount - 1.0) + 0.5) / sliceCount;
    return texture(
        atmosphereAerialScattering, vec3(uv, centeredSlice)).rgb;
}

vec3 SampleFarAerialOpticalDepthAt(vec2 uv, float distanceMeters)
{
    if (distanceMeters <= 0.0)
        return vec3(0.0);
    float slice = AtmosphereAerialSliceCoordinate(distanceMeters);
    float sliceCount =
        max(uAtmosphereFrame.aerialPerspectiveParameters.z, 1.0);
    float centeredSlice =
        (slice * (sliceCount - 1.0) + 0.5) / sliceCount;
    return texture(
        atmosphereAerialOpticalDepth, vec3(uv, centeredSlice)).rgb;
}

float NearMediaSliceCoordinate(float distanceMeters)
{
    float nearDistance = uLocalMedia.nearMediaDepthRangeAndGrid.x;
    float farDistance = uLocalMedia.nearMediaDepthRangeAndGrid.y;
    float normalized = clamp((distanceMeters - nearDistance) /
        max(farDistance - nearDistance, 1.0e-5), 0.0, 1.0);
    return pow(normalized, 1.0 /
        max(uLocalMedia.nearMediaSliceHistoryAndVolumeCount.x, 1.0e-4));
}

vec3 SampleNearMediaScatteringAt(vec2 uv, float distanceMeters)
{
    float nearDistance = uLocalMedia.nearMediaDepthRangeAndGrid.x;
    if (distanceMeters <= nearDistance)
        return vec3(0.0);
    float sliceCount =
        max(textureSize(atmosphereLocalMediaScattering, 0).z, 1);
    float slice = NearMediaSliceCoordinate(distanceMeters);
    float firstIntervalWeight = min(slice * sliceCount, 1.0);
    float textureZ = clamp(slice - 0.5 / sliceCount,
        0.5 / sliceCount, 1.0 - 0.5 / sliceCount);
    return texture(atmosphereLocalMediaScattering,
        vec3(uv, textureZ)).rgb * firstIntervalWeight;
}

vec3 SampleNearMediaOpticalDepthAt(vec2 uv, float distanceMeters)
{
    float nearDistance = uLocalMedia.nearMediaDepthRangeAndGrid.x;
    if (distanceMeters <= nearDistance)
        return vec3(0.0);
    float sliceCount =
        max(textureSize(atmosphereLocalMediaOpticalDepth, 0).z, 1);
    float slice = NearMediaSliceCoordinate(distanceMeters);
    float firstIntervalWeight = min(slice * sliceCount, 1.0);
    float textureZ = clamp(slice - 0.5 / sliceCount,
        0.5 / sliceCount, 1.0 - 0.5 / sliceCount);
    return texture(atmosphereLocalMediaOpticalDepth,
        vec3(uv, textureZ)).rgb * firstIntervalWeight;
}

void ComposeInterval(
    inout vec3 scattering,
    inout vec3 opticalDepth,
    vec3 nextScattering,
    vec3 nextOpticalDepth)
{
    ComposeParticipatingMediaInterval(
        scattering, opticalDepth, nextScattering, nextOpticalDepth);
}

void ExtractCumulativeInterval(
    vec3 startScattering,
    vec3 startOpticalDepth,
    vec3 endScattering,
    vec3 endOpticalDepth,
    out vec3 intervalScattering,
    out vec3 intervalOpticalDepth)
{
    vec3 startTransmittance =
        exp(-min(startOpticalDepth, vec3(80.0)));
    intervalScattering =
        max(endScattering - startScattering, vec3(0.0)) /
        max(startTransmittance, vec3(1.0e-6));
    intervalOpticalDepth =
        max(endOpticalDepth - startOpticalDepth, vec3(0.0));
}

void ExtractFarAerialInterval(
    vec2 uv,
    float startDistance,
    float endDistance,
    out vec3 intervalScattering,
    out vec3 intervalOpticalDepth)
{
    ExtractCumulativeInterval(
        SampleFarAerialScatteringAt(uv, startDistance),
        SampleFarAerialOpticalDepthAt(uv, startDistance),
        SampleFarAerialScatteringAt(uv, endDistance),
        SampleFarAerialOpticalDepthAt(uv, endDistance),
        intervalScattering,
        intervalOpticalDepth);
}

void ExtractNearMediaInterval(
    vec2 uv,
    float startDistance,
    float endDistance,
    out vec3 intervalScattering,
    out vec3 intervalOpticalDepth)
{
    ExtractCumulativeInterval(
        SampleNearMediaScatteringAt(uv, startDistance),
        SampleNearMediaOpticalDepthAt(uv, startDistance),
        SampleNearMediaScatteringAt(uv, endDistance),
        SampleNearMediaOpticalDepthAt(uv, endDistance),
        intervalScattering,
        intervalOpticalDepth);
}

void AppendFarAerialInterval(
    vec2 uv,
    float startDistance,
    float endDistance,
    inout vec3 scattering,
    inout vec3 opticalDepth)
{
    if (endDistance <= startDistance)
        return;
    vec3 intervalScattering;
    vec3 intervalOpticalDepth;
    ExtractFarAerialInterval(uv, startDistance, endDistance,
        intervalScattering, intervalOpticalDepth);
    ComposeInterval(scattering, opticalDepth,
        intervalScattering, intervalOpticalDepth);
}

// NearMedia 已联合积分 Physical Atmosphere、Height Fog 与全部 Local Fog。
// 它在自身距离范围内替换 FarPhysicalAP 的对应区间，避免物理大气重复累计。
void ExtractBaseMediaInterval(
    vec2 uv,
    vec3 rayDirection,
    float startDistance,
    float endDistance,
    out vec3 intervalScattering,
    out vec3 intervalOpticalDepth)
{
    intervalScattering = vec3(0.0);
    intervalOpticalDepth = vec3(0.0);
    if (endDistance <= startDistance)
        return;

    float nearRangeStart =
        uLocalMedia.nearMediaDepthRangeAndGrid.x;
    float nearRangeEnd =
        uLocalMedia.nearMediaDepthRangeAndGrid.y;
    float overlapStart = max(startDistance, nearRangeStart);
    float overlapEnd = min(endDistance, nearRangeEnd);

    AppendFarAerialInterval(uv, startDistance,
        min(endDistance, overlapStart),
        intervalScattering, intervalOpticalDepth);

    if (overlapEnd > overlapStart)
    {
        vec3 nearScattering;
        vec3 nearOpticalDepth;
        ExtractNearMediaInterval(uv, overlapStart, overlapEnd,
            nearScattering, nearOpticalDepth);
        ComposeInterval(intervalScattering, intervalOpticalDepth,
            nearScattering, nearOpticalDepth);
    }

    AppendFarAerialInterval(uv, max(startDistance, overlapEnd),
        endDistance, intervalScattering, intervalOpticalDepth);
}

void ReconstructViewCloudInterval(
    vec2 uv,
    float terminalDistance,
    out vec3 cloudScattering,
    out vec3 cloudOpticalDepth,
    out vec2 cloudDistances,
    out float coverageWeight)
{
    ivec2 extent = textureSize(atmosphereCloudResult, 0);
    vec2 samplePosition = uv * vec2(extent) - 0.5;
    ivec2 baseTexel = ivec2(floor(samplePosition));
    vec2 fraction = fract(samplePosition);
    cloudScattering = vec3(0.0);
    cloudOpticalDepth = vec3(0.0);
    cloudDistances = vec2(0.0);
    coverageWeight = 0.0;
    vec3 cloudTransmittance = vec3(0.0);
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 2; ++x)
        {
            ivec2 texel = clamp(baseTexel + ivec2(x, y),
                ivec2(0), extent - 1);
            float bilinearWeight =
                (x == 0 ? 1.0 - fraction.x : fraction.x) *
                (y == 0 ? 1.0 - fraction.y : fraction.y);
            vec2 distances = texelFetch(
                atmosphereCloudDepth, texel, 0).rg;
            vec4 opticalDepth = texelFetch(
                atmosphereCloudOpticalDepth, texel, 0);
            float tolerance = max(2.0, terminalDistance * 1.0e-4);
            bool valid = distances.y > distances.x &&
                opticalDepth.a > 1.0e-6 &&
                distances.x < terminalDistance &&
                distances.y <= terminalDistance + tolerance;
            if (!valid)
                continue;
            coverageWeight += bilinearWeight;
            cloudScattering += bilinearWeight *
                texelFetch(atmosphereCloudResult, texel, 0).rgb;
            cloudTransmittance += bilinearWeight *
                exp(-min(opticalDepth.rgb, vec3(80.0)));
            cloudDistances += bilinearWeight * distances;
        }
    }
    if (coverageWeight <= 1.0e-6)
    {
        coverageWeight = 0.0;
        return;
    }
    float inverseWeight = 1.0 / coverageWeight;
    cloudScattering *= inverseWeight;
    cloudOpticalDepth = -log(max(
        cloudTransmittance * inverseWeight, vec3(1.0e-6)));
    cloudDistances *= inverseWeight;
    coverageWeight = clamp(coverageWeight, 0.0, 1.0);
}

// 查询到任意终点的完整视线介质。云区间替换同距离的基础介质；
// 前段、云段和后段始终按 S/T 顺序组合。
void ExtractViewMediaInterval(
    vec2 uv,
    vec3 rayDirection,
    float terminalDistance,
    out vec3 scattering,
    out vec3 opticalDepth)
{
    vec3 clearScattering;
    vec3 clearOpticalDepth;
    ExtractBaseMediaInterval(uv, rayDirection, 0.0, terminalDistance,
        clearScattering, clearOpticalDepth);

    vec3 cloudScattering;
    vec3 cloudOpticalDepth;
    vec2 cloudDistances;
    float cloudCoverage;
    ReconstructViewCloudInterval(uv, terminalDistance,
        cloudScattering, cloudOpticalDepth, cloudDistances, cloudCoverage);
    float cloudEntry = clamp(cloudDistances.x, 0.0, terminalDistance);
    float cloudExit = clamp(cloudDistances.y, 0.0, terminalDistance);
    if (cloudCoverage <= 1.0e-6 || cloudExit <= cloudEntry)
    {
        scattering = clearScattering;
        opticalDepth = clearOpticalDepth;
        return;
    }

    vec3 cloudyScattering = vec3(0.0);
    vec3 cloudyOpticalDepth = vec3(0.0);
    vec3 intervalScattering;
    vec3 intervalOpticalDepth;
    ExtractBaseMediaInterval(uv, rayDirection, 0.0, cloudEntry,
        intervalScattering, intervalOpticalDepth);
    ComposeInterval(cloudyScattering, cloudyOpticalDepth,
        intervalScattering, intervalOpticalDepth);
    ComposeInterval(cloudyScattering, cloudyOpticalDepth,
        cloudScattering, cloudOpticalDepth);
    ExtractBaseMediaInterval(uv, rayDirection, cloudExit, terminalDistance,
        intervalScattering, intervalOpticalDepth);
    ComposeInterval(cloudyScattering, cloudyOpticalDepth,
        intervalScattering, intervalOpticalDepth);

    scattering = mix(clearScattering, cloudyScattering, cloudCoverage);
    opticalDepth = -log(max(mix(
        exp(-min(clearOpticalDepth, vec3(80.0))),
        exp(-min(cloudyOpticalDepth, vec3(80.0))),
        cloudCoverage), vec3(1.0e-6)));
}

vec3 CompositeSurfaceMedia(
    vec2 uv,
    vec3 rayDirection,
    float surfaceDistance,
    vec3 surfaceRadiance)
{
    vec3 scattering;
    vec3 opticalDepth;
    ExtractViewMediaInterval(uv, rayDirection, surfaceDistance,
        scattering, opticalDepth);
    return scattering +
        exp(-min(opticalDepth, vec3(80.0))) * surfaceRadiance;
}

vec3 CompositeAtmosphereSurfaceRadiance(
    vec2 screenUv,
    vec3 worldPosition,
    vec3 surfaceRadiance)
{
    vec3 cameraToSurface = worldPosition - cameraPosition.xyz;
    float distanceMeters = length(cameraToSurface);
    if (distanceMeters <= 1.0e-5)
        return surfaceRadiance;
    return CompositeSurfaceMedia(screenUv,
        cameraToSurface / distanceMeters,
        distanceMeters,
        surfaceRadiance);
}

#endif
