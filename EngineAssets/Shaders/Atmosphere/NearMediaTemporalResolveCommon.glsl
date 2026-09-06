#include "../Common/CameraData.glsl"
#include "AtmosphereViewCommon.glsl"
#include "NearMediaDepthCommon.glsl"
#include "NearMediaTemporalCommon.glsl"

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(set = 1, binding = 0, rgba16f) uniform readonly image3D localMediaCurrent;
layout(set = 1, binding = 1) uniform sampler3D localMediaHistory;
layout(set = 1, binding = 2) uniform LocalMediaParams
{
    vec4 depthRangeAndGrid;
    vec4 sliceHistoryAndVolumeCount;
    vec4 localFogTileGridAndLimits;
    vec4 lightTransmittance;
} uLocal;
layout(set = 1, binding = 12, rgba16f) uniform writeonly image3D localMediaResolved;

#if VANS_NEAR_MEDIA_WITH_PARTICLE_REACTIVE_HISTORY
layout(set = 1, binding = 17, r16f) uniform readonly image3D particleActivityCurrent;
layout(set = 1, binding = 18) uniform sampler3D particleActivityHistory;
#endif

vec4 LoadCurrentClamped(ivec3 coordinate, ivec3 gridSize)
{
    return imageLoad(localMediaCurrent,
        clamp(coordinate, ivec3(0), gridSize - ivec3(1)));
}

void AccumulateNeighborhood(vec4 sampleValue, inout vec4 minimumValue,
    inout vec4 maximumValue, inout vec4 sum, inout vec4 sumSquared)
{
    minimumValue = min(minimumValue, sampleValue);
    maximumValue = max(maximumValue, sampleValue);
    sum += sampleValue;
    sumSquared += sampleValue * sampleValue;
}

void main()
{
    ivec3 voxel = ivec3(gl_GlobalInvocationID.xyz);
    ivec3 gridSize = imageSize(localMediaResolved);
    if (any(greaterThanEqual(voxel, gridSize)))
        return;

    vec4 current = max(imageLoad(localMediaCurrent, voxel), vec4(0.0));
    if (uLocal.sliceHistoryAndVolumeCount.y <= 0.5)
    {
        imageStore(localMediaResolved, voxel, current);
        return;
    }

    float nearViewDepth = uLocal.depthRangeAndGrid.x;
    float farViewDepth = uLocal.depthRangeAndGrid.y;
    float slicePower = uLocal.sliceHistoryAndVolumeCount.x;
    vec2 uv = (vec2(voxel.xy) + 0.5) / vec2(gridSize.xy);
    vec3 rayDirection = AtmosphereWorldDirectionFromUv(uv);
    float centerViewDepth = NearMediaSliceViewDepth(float(voxel.z) + 0.5,
        nearViewDepth, farViewDepth, float(gridSize.z), slicePower);
    float centerDistance = NearMediaRayDistanceFromViewDepth(
        rayDirection, centerViewDepth);
    vec3 centerWorldPosition = cameraPosition.xyz + rayDirection * centerDistance;
    vec4 previousUVW = NearMediaHistoryUVW(centerWorldPosition,
        nearViewDepth, farViewDepth, slicePower);
    vec3 halfTexel = 0.5 / vec3(gridSize);
    if (previousUVW.w <= 0.5 ||
        any(lessThan(previousUVW.xyz, halfTexel)) ||
        any(greaterThan(previousUVW.xyz, vec3(1.0) - halfTexel)))
    {
        imageStore(localMediaResolved, voxel, current);
        return;
    }

    vec4 minimumValue = current;
    vec4 maximumValue = current;
    vec4 sum = current;
    vec4 sumSquared = current * current;
    AccumulateNeighborhood(LoadCurrentClamped(voxel + ivec3(1, 0, 0), gridSize),
        minimumValue, maximumValue, sum, sumSquared);
    AccumulateNeighborhood(LoadCurrentClamped(voxel - ivec3(1, 0, 0), gridSize),
        minimumValue, maximumValue, sum, sumSquared);
    AccumulateNeighborhood(LoadCurrentClamped(voxel + ivec3(0, 1, 0), gridSize),
        minimumValue, maximumValue, sum, sumSquared);
    AccumulateNeighborhood(LoadCurrentClamped(voxel - ivec3(0, 1, 0), gridSize),
        minimumValue, maximumValue, sum, sumSquared);
    AccumulateNeighborhood(LoadCurrentClamped(voxel + ivec3(0, 0, 1), gridSize),
        minimumValue, maximumValue, sum, sumSquared);
    AccumulateNeighborhood(LoadCurrentClamped(voxel - ivec3(0, 0, 1), gridSize),
        minimumValue, maximumValue, sum, sumSquared);

    const float neighborhoodSampleCount = 7.0;
    const float varianceGamma = 1.5;
    vec4 mean = sum / neighborhoodSampleCount;
    vec4 variance = max(sumSquared / neighborhoodSampleCount - mean * mean,
        vec4(0.0));
    vec4 standardDeviation = sqrt(variance);
    vec4 lowerBound = max(minimumValue,
        mean - varianceGamma * standardDeviation);
    vec4 upperBound = min(maximumValue,
        mean + varianceGamma * standardDeviation);

    vec4 history = max(textureLod(localMediaHistory, previousUVW.xyz, 0.0),
        vec4(0.0));
    vec4 clippedHistory = clamp(history, lowerBound, upperBound);
    vec4 normalizedClipDistance = abs(history - clippedHistory) /
        max(upperBound - lowerBound, vec4(1.0e-4));
    float clipAmount = clamp(max(max(normalizedClipDistance.x,
        normalizedClipDistance.y), max(normalizedClipDistance.z,
        normalizedClipDistance.w)), 0.0, 1.0);
    float configuredHistoryWeight = clamp(
        uLocal.sliceHistoryAndVolumeCount.z, 0.0, 0.999);
    float responsiveHistoryWeight = min(configuredHistoryWeight, 0.2);
    float historyWeight = mix(configuredHistoryWeight,
        responsiveHistoryWeight, clipAmount);

#if VANS_NEAR_MEDIA_WITH_PARTICLE_REACTIVE_HISTORY
    // 粒子活动只调节统一介质历史权重，不携带或计算任何光照结果。
    float currentActivity = max(imageLoad(particleActivityCurrent, voxel).r, 0.0);
    float previousActivity = max(textureLod(
        particleActivityHistory, previousUVW.xyz, 0.0).r, 0.0);
    float activityDelta = abs(currentActivity - previousActivity);
    float relativeDelta = activityDelta /
        max(max(currentActivity, previousActivity), 0.01);
    float particleReactive = smoothstep(0.08, 0.4, relativeDelta) *
        smoothstep(0.001, 0.02, activityDelta);
    historyWeight *= 1.0 - particleReactive;
#endif

    imageStore(localMediaResolved, voxel,
        mix(current, clippedHistory, historyWeight));
}
