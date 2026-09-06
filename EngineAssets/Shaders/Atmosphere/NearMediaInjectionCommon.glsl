#include "../Common/Common.glsl"
#include "../Common/CameraData.glsl"
#include "../Common/FogVolumeCommon.glsl"
#include "AtmosphereViewCommon.glsl"
#include "NearMediaDepthCommon.glsl"
#include "NearMediaTemporalCommon.glsl"

layout(local_size_x = 4, local_size_y = 4, local_size_z = 4) in;
layout(set = 1, binding = 2) uniform LocalMediaParams
{
    vec4 depthRangeAndGrid;
    vec4 sliceHistoryAndVolumeCount;
    vec4 localFogTileGridAndLimits;
    vec4 lightTransmittance;
} uLocal;
layout(set = 1, binding = 19, rgba16f) uniform writeonly image3D nearMediaScatteringExtinction;
layout(set = 1, binding = 20, rgba16f) uniform writeonly image3D nearMediaLightingPhaseCloud;
layout(set = 1, binding = 21, rgba16f) uniform writeonly image3D nearMediaEmissiveWeight;

struct LocalFogVolume
{
    mat4 worldToLocal;
    vec4 extinctionAnisotropyCloudPadding;
    vec4 dimensionsAndEdgeFade;
    vec4 scatteringAlbedo;
    vec4 emissivePerMeter;
    vec4 lightingAndDistanceFade;
    uvec4 fieldHandlesAndFlags;
    vec4 shapeTilingOffset;
    vec4 shapeRemapInfluenceLod;
    vec4 detailTilingOffset;
    vec4 detailRemapInfluenceLod;
    vec4 flowTilingOffset;
    vec4 flowSpeedDistancePhaseLod;
    vec4 flowFallbackDirectionPadding;
};

layout(set = 1, binding = 7, std430) readonly buffer LocalFogVolumes
{
    LocalFogVolume volumes[];
} uLocalFogVolumes;

layout(set = 1, binding = 8, std430) readonly buffer LocalFogTileHeaders
{
    uvec2 headers[];
} uLocalFogTileHeaders;

layout(set = 1, binding = 9, std430) readonly buffer LocalFogTileIndices
{
    uint indices[];
} uLocalFogTileIndices;

#include "LocalFogFieldSampling.glsl"
#include "LocalFogFlowAdvection.glsl"
#include "LocalFogMediumCommon.glsl"

bool IntersectLocalFogSliceSegment(
    LocalFogVolume volume,
    vec3 rayDirection,
    float sliceStartDistance,
    float sliceEndDistance,
    out float overlapStartDistance,
    out float overlapEndDistance)
{
    vec3 localOrigin =
        (volume.worldToLocal * vec4(cameraPosition.xyz, 1.0)).xyz;
    vec3 localDirection =
        (volume.worldToLocal * vec4(rayDirection, 0.0)).xyz;
    float entryDistance = sliceStartDistance;
    float exitDistance = sliceEndDistance;

    for (int axis = 0; axis < 3; ++axis)
    {
        float origin = localOrigin[axis];
        float direction = localDirection[axis];
        if (abs(direction) <= 1.0e-7)
        {
            if (origin < -0.5 || origin > 0.5)
                return false;
            continue;
        }

        float inverseDirection = 1.0 / direction;
        float plane0 = (-0.5 - origin) * inverseDirection;
        float plane1 = (0.5 - origin) * inverseDirection;
        entryDistance = max(entryDistance, min(plane0, plane1));
        exitDistance = min(exitDistance, max(plane0, plane1));
        if (exitDistance <= entryDistance)
            return false;
    }

    overlapStartDistance = entryDistance;
    overlapEndDistance = exitDistance;
    return overlapEndDistance > overlapStartDistance;
}

// 所有近场介质提供者只构造光学属性；这里不允许读取或计算任何光源。
struct NearMediaMediumSample
{
    vec3 scatteringAlbedo;
    float extinctionPerMeter;
    vec3 emissivePerMeter;
    float anisotropy;
    float directLightingScale;
    float skyLightingScale;
    float receiveCloudShadows;
};

struct NearMediaMaterialAccumulation
{
    // RGB: sigmaS，A: sigmaT。
    vec4 scatteringExtinction;
    // 以散射亮度加权的 direct、sky、phase g、receiveCloudShadows 分子。
    vec4 lightingPhaseCloud;
    // RGB: 单位长度自发光，A: 上述加权量的分母。
    vec4 emissiveWeight;
};

float AccumulateNearMediaMaterial(
    inout NearMediaMaterialAccumulation accumulation,
    NearMediaMediumSample medium,
    float weight)
{
    float sigmaT = max(medium.extinctionPerMeter, 0.0) * weight;
    if (sigmaT <= 1.0e-7)
        return 0.0;

    vec3 sigmaS = clamp(medium.scatteringAlbedo,
        vec3(0.0), vec3(1.0)) * sigmaT;
    float scatteringWeight = dot(sigmaS,
        vec3(0.2126, 0.7152, 0.0722));
    vec3 emissive = max(medium.emissivePerMeter, vec3(0.0)) * weight;

    accumulation.scatteringExtinction += vec4(sigmaS, sigmaT);
    accumulation.lightingPhaseCloud += scatteringWeight * vec4(
        max(medium.directLightingScale, 0.0),
        max(medium.skyLightingScale, 0.0),
        clamp(medium.anisotropy, -0.98, 0.98),
        medium.receiveCloudShadows > 0.5 ? 1.0 : 0.0);
    accumulation.emissiveWeight += vec4(emissive, scatteringWeight);
    return sigmaT + dot(emissive, vec3(0.2126, 0.7152, 0.0722));
}

#if VANS_NEAR_MEDIA_WITH_VOLUMETRIC_PARTICLES
#include "VolumetricParticleMediumProvider.glsl"
#endif

void main()
{
    ivec3 voxel = ivec3(gl_GlobalInvocationID.xyz);
    ivec3 gridSize = imageSize(nearMediaScatteringExtinction);
    if (any(greaterThanEqual(voxel, gridSize)))
        return;

    float nearViewDepth = uLocal.depthRangeAndGrid.x;
    float farViewDepth = uLocal.depthRangeAndGrid.y;
    float slicePower = uLocal.sliceHistoryAndVolumeCount.x;
    vec2 uv = (vec2(voxel.xy) + 0.5) / vec2(gridSize.xy);
    vec3 rayDirection = AtmosphereWorldDirectionFromUv(uv);
    float sliceStartViewDepth = NearMediaSliceViewDepth(float(voxel.z),
        nearViewDepth, farViewDepth, float(gridSize.z), slicePower);
    float sliceEndViewDepth = NearMediaSliceViewDepth(float(voxel.z) + 1.0,
        nearViewDepth, farViewDepth, float(gridSize.z), slicePower);
    float sliceStartDistance = NearMediaRayDistanceFromViewDepth(
        rayDirection, sliceStartViewDepth);
    float sliceEndDistance = NearMediaRayDistanceFromViewDepth(
        rayDirection, sliceEndViewDepth);
    float sliceThickness = max(sliceEndDistance - sliceStartDistance, 1.0e-6);

#if VANS_NEAR_MEDIA_WITH_VOLUMETRIC_PARTICLES
    vec2 froxelHalfUv = 0.5 / vec2(gridSize.xy);
    vec3 cornerRay0 = AtmosphereWorldDirectionFromUv(clamp(
        uv + vec2(-froxelHalfUv.x, -froxelHalfUv.y),
        vec2(0.0), vec2(1.0)));
    vec3 cornerRay1 = AtmosphereWorldDirectionFromUv(clamp(
        uv + vec2( froxelHalfUv.x, -froxelHalfUv.y),
        vec2(0.0), vec2(1.0)));
    vec3 cornerRay2 = AtmosphereWorldDirectionFromUv(clamp(
        uv + vec2(-froxelHalfUv.x,  froxelHalfUv.y),
        vec2(0.0), vec2(1.0)));
    vec3 cornerRay3 = AtmosphereWorldDirectionFromUv(clamp(
        uv + vec2( froxelHalfUv.x,  froxelHalfUv.y),
        vec2(0.0), vec2(1.0)));
    float froxelFootprintRadius = max(max(
        length((cornerRay0 - rayDirection) * sliceEndDistance),
        length((cornerRay1 - rayDirection) * sliceEndDistance)), max(
        length((cornerRay2 - rayDirection) * sliceEndDistance),
        length((cornerRay3 - rayDirection) * sliceEndDistance)));
#endif

    float subFroxelJitter = uLocal.localFogTileGridAndLimits.w > 0.5
        ? NearMediaSubFroxelJitter(
            uvec2(voxel.xy), uint(max(FrameIndex, 0.0)))
        : 0.5;
    NearMediaMaterialAccumulation current;
    current.scatteringExtinction = vec4(0.0);
    current.lightingPhaseCloud = vec4(0.0);
    current.emissiveWeight = vec4(0.0);
    float particleActivity = 0.0;

    uint volumeCount = min(uint(uLocal.sliceHistoryAndVolumeCount.w + 0.5), 64u);
    uint localFogTileIndex = uint(voxel.y) * uint(gridSize.x) + uint(voxel.x);
    uvec2 localFogHeader = uLocalFogTileHeaders.headers[localFogTileIndex];
    bool localFogTileOverflow = localFogHeader.y == 0xffffffffu;
    uint candidateCount = localFogTileOverflow
        ? volumeCount : min(localFogHeader.y, volumeCount);
    for (uint candidateIndex = 0u; candidateIndex < candidateCount; ++candidateIndex)
    {
        uint volumeIndex = localFogTileOverflow
            ? candidateIndex
            : uLocalFogTileIndices.indices[localFogHeader.x + candidateIndex];
        if (volumeIndex >= volumeCount)
            continue;
        LocalFogVolume volume = uLocalFogVolumes.volumes[volumeIndex];

        float overlapStartDistance;
        float overlapEndDistance;
        if (!IntersectLocalFogSliceSegment(volume, rayDirection,
            sliceStartDistance, sliceEndDistance,
            overlapStartDistance, overlapEndDistance))
        {
            continue;
        }
        float overlapDistance = overlapEndDistance - overlapStartDistance;
        float overlapFraction = clamp(overlapDistance / sliceThickness, 0.0, 1.0);
        float sampleDistance = mix(overlapStartDistance,
            overlapEndDistance, subFroxelJitter);
        vec3 sampleWorldPosition = cameraPosition.xyz +
            rayDirection * sampleDistance;
        vec3 localPosition =
            (volume.worldToLocal * vec4(sampleWorldPosition, 1.0)).xyz;
        vec3 distanceToFaceMeters =
            (vec3(0.5) - abs(localPosition)) *
            max(volume.dimensionsAndEdgeFade.xyz, vec3(1.0e-4));
        float minimumDistanceToFace = min(min(
            distanceToFaceMeters.x, distanceToFaceMeters.y),
            distanceToFaceMeters.z);
        if (minimumDistanceToFace < 0.0)
            continue;
        float edgeFadeMeters = max(volume.dimensionsAndEdgeFade.w, 0.0);
        float boxWeight = edgeFadeMeters > 0.0
            ? smoothstep(0.0, edgeFadeMeters, minimumDistanceToFace)
            : 1.0;
        float distanceWeight = 1.0 - smoothstep(
            volume.lightingAndDistanceFade.z,
            max(volume.lightingAndDistanceFade.w,
                volume.lightingAndDistanceFade.z + 1.0e-3),
            sampleDistance);
        float baseWeight = boxWeight * distanceWeight * overlapFraction;
        if (baseWeight <= 1.0e-7)
            continue;

        float densityFactor = 1.0;
        uint densityFieldFlags = volume.fieldHandlesAndFlags.w;
        if ((densityFieldFlags &
            (LOCAL_FOG_SHAPE_ENABLED | LOCAL_FOG_DETAIL_ENABLED)) != 0u)
        {
            vec2 froxelUvStep = 1.0 / vec2(gridSize.xy);
            vec2 neighborUvX = uv + vec2(
                voxel.x + 1 < gridSize.x ? froxelUvStep.x : -froxelUvStep.x,
                0.0);
            vec2 neighborUvY = uv + vec2(
                0.0,
                voxel.y + 1 < gridSize.y ? froxelUvStep.y : -froxelUvStep.y);
            vec3 worldFootprintX = cameraPosition.xyz +
                AtmosphereWorldDirectionFromUv(neighborUvX) * sampleDistance -
                sampleWorldPosition;
            vec3 worldFootprintY = cameraPosition.xyz +
                AtmosphereWorldDirectionFromUv(neighborUvY) * sampleDistance -
                sampleWorldPosition;
            vec3 worldFootprintZ = rayDirection * overlapDistance;
            vec2 dimensionsMetersXZ = max(
                volume.dimensionsAndEdgeFade.xz, vec2(1.0e-4));
            vec2 localXZFootprintMeters =
                abs((volume.worldToLocal * vec4(worldFootprintX, 0.0)).xz) *
                    dimensionsMetersXZ +
                abs((volume.worldToLocal * vec4(worldFootprintY, 0.0)).xz) *
                    dimensionsMetersXZ +
                abs((volume.worldToLocal * vec4(worldFootprintZ, 0.0)).xz) *
                    dimensionsMetersXZ;
            densityFactor = EvaluateLocalFogDensityFactor(
                volume, localPosition, localXZFootprintMeters);
        }

        NearMediaMediumSample medium;
        medium.scatteringAlbedo = volume.scatteringAlbedo.rgb;
        medium.extinctionPerMeter =
            volume.extinctionAnisotropyCloudPadding.x;
        medium.emissivePerMeter = volume.emissivePerMeter.rgb;
        medium.anisotropy = volume.extinctionAnisotropyCloudPadding.y;
        medium.directLightingScale = volume.lightingAndDistanceFade.x;
        medium.skyLightingScale = volume.lightingAndDistanceFade.y;
        medium.receiveCloudShadows =
            volume.extinctionAnisotropyCloudPadding.z;
        AccumulateNearMediaMaterial(current, medium,
            baseWeight * densityFactor);
    }

#if VANS_NEAR_MEDIA_WITH_VOLUMETRIC_PARTICLES
    particleActivity = InjectVolumetricParticleMedium(
        current, voxel, gridSize, rayDirection,
        sliceStartDistance, sliceEndDistance, sliceThickness,
        froxelFootprintRadius);
    // 该体积只记录粒子介质变化，用于粒子专用的 history 降权。
    imageStore(currentParticleActivity, voxel,
        vec4(particleActivity, 0.0, 0.0, 0.0));
#endif

    imageStore(nearMediaScatteringExtinction, voxel,
        current.scatteringExtinction);
    imageStore(nearMediaLightingPhaseCloud, voxel,
        current.lightingPhaseCloud);
    imageStore(nearMediaEmissiveWeight, voxel,
        current.emissiveWeight);
}
