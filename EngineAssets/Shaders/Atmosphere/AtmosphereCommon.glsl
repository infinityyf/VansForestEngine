#ifndef VANS_ATMOSPHERE_COMMON_GLSL
#define VANS_ATMOSPHERE_COMMON_GLSL

#include "ParticipatingMediaCommon.glsl"

const float ATM_PI = 3.14159265358979323846;
const float ATM_INV_FOUR_PI = 0.07957747154594767;

struct AtmosphereCelestialBody
{
    vec4 directionAndValidity;
    vec4 topOfAtmosphereIrradiance;
    vec4 diskParameters;
};

layout(set = 0, binding = 20, std140) uniform AtmosphereStaticParameters
{
    vec4 planetRadiiMeters;
    vec4 rayleighScatteringAndScaleHeight;
    vec4 mieScatteringAndScaleHeight;
    vec4 mieAbsorptionAndAnisotropy;
    vec4 ozoneAbsorptionAndCenterAltitude;
    vec4 ozoneHalfWidthAndGroundAlbedo;
    vec4 aerialPerspectiveAndVolumetricLighting;
    vec4 heightFogDensityParameters;
	vec4 heightFogDistanceParameters;
    vec4 heightFogAlbedoAndAnisotropy;
    vec4 heightFogEmissiveAndSkyScale;
    vec4 heightFogLightingParameters;
    uvec4 atmosphereFeatureFlags;
    uvec4 atmosphereLutSampleCounts;
    uvec4 atmosphereLutDimensions;
} uAtmosphere;

layout(set = 0, binding = 21, std140) uniform AtmosphereFrameParameters
{
    vec4 planetCenterRelativeToCameraMeters;
    vec4 cameraWorldMetersAndMaxDistance;
    vec4 aerialPerspectiveParameters;
    uvec4 atmosphereViewParameters;
	// x: 当前帧是否存在水面；水面深度只参与最终大气合成。
	uvec4 atmosphereSurfaceCompositionFlags;
    vec4 preparedMainLightDirectionAndValidity;
    vec4 preparedMainLightColorAndIntensity;
    AtmosphereCelestialBody atmosphereCelestialBodies[2];
} uAtmosphereFrame;

layout(set = 0, binding = 22) uniform sampler2D atmosphereTransmittanceLut;
layout(set = 0, binding = 23) uniform sampler2D atmosphereMultiScatteringLut;
layout(set = 0, binding = 24) uniform sampler2D atmosphereSkyViewLut;
layout(set = 0, binding = 25) uniform sampler3D atmosphereAerialScattering;
layout(set = 0, binding = 26) uniform sampler3D atmosphereAerialOpticalDepth;
layout(set = 0, binding = 27) uniform sampler2DArray atmosphereCloudShadow;
layout(set = 0, binding = 28) uniform sampler2D atmosphereCloudResult;
layout(set = 0, binding = 29) uniform sampler3D atmosphereLocalMediaScattering;
layout(set = 0, binding = 30) uniform sampler3D atmosphereLocalMediaOpticalDepth;
layout(set = 0, binding = 32) uniform LocalMediaGlobalParams
{
    vec4 nearMediaDepthRangeAndGrid;
    vec4 nearMediaSliceHistoryAndVolumeCount;
    vec4 nearMediaLocalFogTileGridAndLimits;
} uLocalMedia;
layout(set = 0, binding = 33, std140) uniform VolumetricCloudRuntimeParameters
{
    vec4 cloudRuntimeParameters[16];
} uVolumetricCloud;
layout(set = 0, binding = 34) uniform sampler2D atmosphereCloudDepth;
layout(set = 0, binding = 35) uniform sampler2D atmosphereCloudOpticalDepth;

struct AtmosphereMedium
{
    vec3 rayleighScattering;
    vec3 mieScattering;
    vec3 extinction;
    vec3 heightFogScattering;
    vec3 heightFogEmissive;
};

float AtmosphereBottomRadius() { return uAtmosphere.planetRadiiMeters.x; }
float AtmosphereTopRadius() { return uAtmosphere.planetRadiiMeters.y; }

float AtmosphereAltitude(vec3 positionRelativeToCamera)
{
    return length(positionRelativeToCamera -
        uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz) -
        AtmosphereBottomRadius();
}

AtmosphereMedium EmptyAtmosphereMedium()
{
    AtmosphereMedium medium;
    medium.rayleighScattering = vec3(0.0);
    medium.mieScattering = vec3(0.0);
    medium.extinction = vec3(0.0);
    medium.heightFogScattering = vec3(0.0);
    medium.heightFogEmissive = vec3(0.0);
    return medium;
}

AtmosphereMedium EvaluatePhysicalAtmosphereMedium(float altitudeMeters)
{
    AtmosphereMedium medium = EmptyAtmosphereMedium();
    if (uAtmosphere.atmosphereFeatureFlags.x == 0u ||
        altitudeMeters < 0.0 ||
        altitudeMeters > uAtmosphere.planetRadiiMeters.z)
        return medium;

    float rayleighDensity = exp(-altitudeMeters /
        max(uAtmosphere.rayleighScatteringAndScaleHeight.w, 1.0));
    float mieDensity = exp(-altitudeMeters /
        max(uAtmosphere.mieScatteringAndScaleHeight.w, 1.0));
    float ozoneDensity = max(1.0 - abs(altitudeMeters -
        uAtmosphere.ozoneAbsorptionAndCenterAltitude.w) /
        max(uAtmosphere.ozoneHalfWidthAndGroundAlbedo.x, 1.0), 0.0);
    medium.rayleighScattering =
        uAtmosphere.rayleighScatteringAndScaleHeight.xyz * rayleighDensity;
    medium.mieScattering =
        uAtmosphere.mieScatteringAndScaleHeight.xyz * mieDensity;
    vec3 mieAbsorption =
        uAtmosphere.mieAbsorptionAndAnisotropy.xyz * mieDensity;
    vec3 ozoneAbsorption =
        uAtmosphere.ozoneAbsorptionAndCenterAltitude.xyz * ozoneDensity;
    medium.extinction = medium.rayleighScattering +
        medium.mieScattering + mieAbsorption + ozoneAbsorption;
    return medium;
}

float AtmosphereHeightFogDensityAtWorldHeight(float worldHeightMeters)
{
    float heightAboveGround = max(worldHeightMeters -
        uAtmosphere.heightFogDensityParameters.x, 0.0);
    return exp(-heightAboveGround /
        max(uAtmosphere.heightFogDensityParameters.z, 1.0e-3));
}

float AtmosphereSmoothStep(float edge0, float edge1, float value)
{
    if (edge1 <= edge0)
        return value >= edge1 ? 1.0 : 0.0;
    float t = clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

float AtmosphereHeightFogDistanceWeight(float distanceMeters)
{
    float startDistance = uAtmosphere.heightFogDistanceParameters.x;
    float nearFade = uAtmosphere.heightFogDistanceParameters.y;
    float maximumDistance = uAtmosphere.heightFogDistanceParameters.z;
    float farFade = uAtmosphere.heightFogDistanceParameters.w;
    if (distanceMeters < startDistance || distanceMeters > maximumDistance)
        return 0.0;
    float nearWeight = nearFade > 0.0
        ? AtmosphereSmoothStep(startDistance, startDistance + nearFade,
            distanceMeters)
        : 1.0;
    float farWeight = farFade > 0.0
        ? 1.0 - AtmosphereSmoothStep(maximumDistance - farFade,
            maximumDistance, distanceMeters)
        : 1.0;
    return nearWeight * farWeight;
}

AtmosphereMedium EvaluateHeightFogSegmentMedium(
    vec3 viewDirection,
    float segmentStartDistanceMeters,
    float segmentEndDistanceMeters)
{
    AtmosphereMedium medium = EmptyAtmosphereMedium();
    float segmentLength = max(
        segmentEndDistanceMeters - segmentStartDistanceMeters, 0.0);
    if (uAtmosphere.atmosphereFeatureFlags.y == 0u ||
        segmentLength <= 0.0)
        return medium;

    vec3 direction = normalize(viewDirection);
    float cameraWorldHeight =
        uAtmosphereFrame.cameraWorldMetersAndMaxDistance.y;
    const int sampleCount = 4;
    float integratedDensity = 0.0;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        float u = (float(sampleIndex) + 0.5) / float(sampleCount);
        float distanceMeters = mix(
            segmentStartDistanceMeters, segmentEndDistanceMeters, u);
        float worldHeight =
            cameraWorldHeight + direction.y * distanceMeters;
        integratedDensity +=
            AtmosphereHeightFogDensityAtWorldHeight(worldHeight) *
            AtmosphereHeightFogDistanceWeight(distanceMeters);
    }
    integratedDensity *= segmentLength / float(sampleCount);
    float opticalDepth =
        uAtmosphere.heightFogDensityParameters.y * integratedDensity;
    float averageExtinction = opticalDepth / segmentLength;
    medium.extinction = vec3(averageExtinction);
    medium.heightFogScattering = averageExtinction *
        uAtmosphere.heightFogAlbedoAndAnisotropy.xyz;
    medium.heightFogEmissive =
        uAtmosphere.heightFogEmissiveAndSkyScale.xyz *
        (integratedDensity / segmentLength);
    return medium;
}

AtmosphereMedium CombineAtmosphereMedium(
    AtmosphereMedium firstMedium,
    AtmosphereMedium secondMedium)
{
    firstMedium.rayleighScattering += secondMedium.rayleighScattering;
    firstMedium.mieScattering += secondMedium.mieScattering;
    firstMedium.extinction += secondMedium.extinction;
    firstMedium.heightFogScattering += secondMedium.heightFogScattering;
    firstMedium.heightFogEmissive += secondMedium.heightFogEmissive;
    return firstMedium;
}

AtmosphereMedium EvaluateAtmosphereMedium(float altitudeMeters)
{
    return EvaluatePhysicalAtmosphereMedium(altitudeMeters);
}

AtmosphereMedium EvaluatePhysicalAerialPerspectiveSegmentMedium(
    vec3 positionRelativeToCamera,
    vec3 viewDirection,
    float segmentStartDistanceMeters,
    float segmentEndDistanceMeters)
{
    return EvaluatePhysicalAtmosphereMedium(
        AtmosphereAltitude(positionRelativeToCamera));
}

AtmosphereMedium EvaluateNearMediaSegmentMedium(
    vec3 positionRelativeToCamera,
    vec3 viewDirection,
    float segmentStartDistanceMeters,
    float segmentEndDistanceMeters)
{
    return CombineAtmosphereMedium(
        EvaluatePhysicalAerialPerspectiveSegmentMedium(
            positionRelativeToCamera, viewDirection,
            segmentStartDistanceMeters, segmentEndDistanceMeters),
        EvaluateHeightFogSegmentMedium(
            viewDirection,
            segmentStartDistanceMeters, segmentEndDistanceMeters));
}
vec3 AtmosphereIntegratedSegmentSource(
    vec3 sourcePerMeter,
    vec3 extinctionPerMeter,
    float segmentLengthMeters)
{
    ParticipatingMediaSample medium = EmptyParticipatingMediaSample();
    AddParticipatingMediaSample(medium, sourcePerMeter, extinctionPerMeter);
    return ParticipatingMediaSegmentScattering(medium, segmentLengthMeters);
}

bool AtmosphereRaySphere(
    vec3 origin,
    vec3 direction,
    vec3 center,
    float radius,
    out float entryDistance,
    out float exitDistance)
{
    vec3 relativeOrigin = origin - center;
    float projected = dot(relativeOrigin, direction);
    float discriminant = projected * projected -
        (dot(relativeOrigin, relativeOrigin) - radius * radius);
    if (discriminant < 0.0)
        return false;
    float root = sqrt(max(discriminant, 0.0));
    entryDistance = max(-projected - root, 0.0);
    exitDistance = -projected + root;
    return exitDistance >= 0.0;
}

float AtmosphereDistanceToTop(vec3 origin, vec3 direction)
{
    float entryDistance;
    float exitDistance;
    if (!AtmosphereRaySphere(origin, direction,
        uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz,
        AtmosphereTopRadius(), entryDistance, exitDistance))
        return 0.0;
    return exitDistance;
}

bool AtmosphereRayHitsGround(vec3 origin, vec3 direction, out float distanceMeters)
{
    float entryDistance;
    float exitDistance;
    bool intersects = AtmosphereRaySphere(origin, direction,
        uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz,
        AtmosphereBottomRadius(), entryDistance, exitDistance);
    distanceMeters = entryDistance;
    return intersects && entryDistance > 0.0;
}

vec2 AtmosphereTransmittanceUvForShell(
    float radius,
    float zenithCosine,
    float bottomRadius,
    float topRadius)
{
    float bottom = bottomRadius;
    float top = topRadius;
    float h = sqrt(max(top * top - bottom * bottom, 0.0));
    float rho = sqrt(max(radius * radius - bottom * bottom, 0.0));
    float discriminant = radius * radius * (zenithCosine * zenithCosine - 1.0) + top * top;
    float distance = max(-radius * zenithCosine + sqrt(max(discriminant, 0.0)), 0.0);
    float minimumDistance = top - radius;
    float maximumDistance = rho + h;
    float xMu = (distance - minimumDistance) /
        max(maximumDistance - minimumDistance, 1.0e-4);
    return clamp(vec2(xMu, rho / max(h, 1.0e-4)), vec2(0.0), vec2(1.0));
}

vec2 AtmosphereTransmittanceUv(float radius, float zenithCosine)
{
    return AtmosphereTransmittanceUvForShell(
        radius, zenithCosine, AtmosphereBottomRadius(), AtmosphereTopRadius());
}

void AtmosphereTransmittanceRMuForShell(
    vec2 uv,
    float bottomRadius,
    float topRadius,
    out float radius,
    out float zenithCosine)
{
    float bottom = bottomRadius;
    float top = topRadius;
    float h = sqrt(max(top * top - bottom * bottom, 0.0));
    float rho = h * clamp(uv.y, 0.0, 1.0);
    radius = sqrt(rho * rho + bottom * bottom);
    float minimumDistance = top - radius;
    float maximumDistance = rho + h;
    float distance = minimumDistance + clamp(uv.x, 0.0, 1.0) *
        (maximumDistance - minimumDistance);
    zenithCosine = distance <= 1.0e-4 ? 1.0 :
        (h * h - rho * rho - distance * distance) / (2.0 * radius * distance);
    zenithCosine = clamp(zenithCosine, -1.0, 1.0);
}

void AtmosphereTransmittanceRMu(vec2 uv, out float radius, out float zenithCosine)
{
    AtmosphereTransmittanceRMuForShell(
        uv, AtmosphereBottomRadius(), AtmosphereTopRadius(),
        radius, zenithCosine);
}

vec3 SampleAtmosphereTransmittance(vec3 positionRelativeToCamera, vec3 direction)
{
    vec3 radial = positionRelativeToCamera -
        uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz;
    float radius = length(radial);
    float zenithCosine = dot(radial / max(radius, 1.0), direction);
    float groundDistance;
    if (AtmosphereRayHitsGround(positionRelativeToCamera, direction, groundDistance))
        return vec3(0.0);
    return texture(atmosphereTransmittanceLut,
        AtmosphereTransmittanceUv(radius, zenithCosine)).rgb;
}

float AtmosphereRayleighPhase(float cosineTheta)
{
    return 3.0 / (16.0 * ATM_PI) * (1.0 + cosineTheta * cosineTheta);
}

float AtmosphereHenyeyGreenstein(float cosineTheta, float anisotropy)
{
    float g2 = anisotropy * anisotropy;
    return ATM_INV_FOUR_PI * (1.0 - g2) /
        max(pow(1.0 + g2 - 2.0 * anisotropy * cosineTheta, 1.5), 1.0e-5);
}

float AtmosphereHeightFogPhase(float cosineTheta)
{
    return AtmosphereHenyeyGreenstein(
        cosineTheta,
        uAtmosphere.heightFogAlbedoAndAnisotropy.w);
}
vec3 SampleAtmosphereMultipleScattering(
    vec3 positionRelativeToCamera, vec3 lightDirection)
{
    vec3 radial = positionRelativeToCamera -
        uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz;
    float radius = length(radial);
    float altitude = clamp(radius - AtmosphereBottomRadius(), 0.0,
        uAtmosphere.planetRadiiMeters.z);
    float sunCosine = dot(radial / max(radius, 1.0), lightDirection);
    return texture(atmosphereMultiScatteringLut,
        vec2(sunCosine * 0.5 + 0.5,
             altitude / max(uAtmosphere.planetRadiiMeters.z, 1.0))).rgb;
}

vec3 SampleAtmosphereCloudShadow(vec3 positionRelativeToCamera);
vec3 AtmospherePrimaryDirection();

bool AtmospherePreparedLightCelestialIrradiance(
    vec3 preparedDirection,
    out vec3 topOfAtmosphereIrradiance)
{
    topOfAtmosphereIrradiance = vec3(0.0);
    for (uint bodyIndex = 0u; bodyIndex < 2u; ++bodyIndex)
    {
        AtmosphereCelestialBody body =
            uAtmosphereFrame.atmosphereCelestialBodies[bodyIndex];
        if (body.directionAndValidity.w <= 0.5)
            continue;
        if (dot(normalize(body.directionAndValidity.xyz), preparedDirection) > 0.999)
        {
            topOfAtmosphereIrradiance = body.topOfAtmosphereIrradiance.rgb;
            return true;
        }
    }
    return false;
}

// 物理大气与近地介质共享同一光源可见度；云影强度在线性辐照度域混合，
// 不再对已经积分的 optical depth 再次做指数缩放。
float AtmosphereCloudShadowStrength()
{
    return clamp(uVolumetricCloud.cloudRuntimeParameters[1].z, 0.0, 1.0);
}

float AtmosphereCloudAmbientOcclusionStrength()
{
    return clamp(uVolumetricCloud.cloudRuntimeParameters[15].z, 0.0, 1.0);
}

vec3 AtmosphereCloudVisibility(
    vec3 positionRelativeToCamera,
    float strength)
{
    return mix(vec3(1.0),
        SampleAtmosphereCloudShadow(positionRelativeToCamera),
        clamp(strength, 0.0, 1.0));
}

// 已解析的主天体可见度是光照源项的一部分，而不是视线消光的一部分。
// 这使普通 Aerial pass、清空 LUT 与云层联合区间可以各自提供正确的
// “采样点到太阳”透射率，同时共享完全相同的 Rayleigh/Mie 计算。
vec3 EvaluateAtmosphereScatteringSourceWithResolvedPrimaryVisibility(
    vec3 positionRelativeToCamera,
    vec3 viewDirection,
    AtmosphereMedium medium,
    vec3 primaryDirectVisibility,
    vec3 primaryAmbientVisibility)
{
    vec3 source = medium.heightFogEmissive;
    vec3 physicalScattering =
        medium.rayleighScattering + medium.mieScattering;
    bool hasHeightFog =
        dot(medium.heightFogScattering, medium.heightFogScattering) > 0.0;
    bool heightFogHasCelestialLight = false;
    for (uint bodyIndex = 0u; bodyIndex < 2u; ++bodyIndex)
    {
        AtmosphereCelestialBody body =
            uAtmosphereFrame.atmosphereCelestialBodies[bodyIndex];
        if (body.directionAndValidity.w <= 0.5)
            continue;

        vec3 lightDirection = normalize(body.directionAndValidity.xyz);
        float cosineTheta = dot(viewDirection, lightDirection);
        vec3 lightTransmittance = SampleAtmosphereTransmittance(
            positionRelativeToCamera, lightDirection);
        vec3 bodyCloudVisibility =
            bodyIndex == 0u ? primaryDirectVisibility : vec3(1.0);
        vec3 bodyAmbientVisibility =
            bodyIndex == 0u ? primaryAmbientVisibility : vec3(1.0);

        if (dot(physicalScattering, physicalScattering) > 0.0)
        {
            float directScatteringScale = bodyIndex == 0u
                ? max(uAtmosphere.aerialPerspectiveAndVolumetricLighting.y, 0.0)
                : 1.0;
            vec3 phaseScattering =
                medium.rayleighScattering *
                    AtmosphereRayleighPhase(cosineTheta) +
                medium.mieScattering *
                    AtmosphereHenyeyGreenstein(
                        cosineTheta,
                        uAtmosphere.mieAbsorptionAndAnisotropy.w);
            source += body.topOfAtmosphereIrradiance.rgb *
                (lightTransmittance * bodyCloudVisibility *
                    phaseScattering * directScatteringScale +
                 SampleAtmosphereMultipleScattering(
                    positionRelativeToCamera, lightDirection) *
                    bodyAmbientVisibility * physicalScattering);
        }

        if (hasHeightFog)
        {
            heightFogHasCelestialLight = true;
            vec3 fogCloudVisibility =
                bodyIndex == 0u &&
                uAtmosphere.heightFogLightingParameters.y > 0.5
                    ? primaryDirectVisibility : vec3(1.0);
            source += medium.heightFogScattering *
                body.topOfAtmosphereIrradiance.rgb *
                lightTransmittance * fogCloudVisibility *
                AtmosphereHeightFogPhase(cosineTheta) *
                max(uAtmosphere.heightFogLightingParameters.x, 0.0);
            source += medium.heightFogScattering *
                body.topOfAtmosphereIrradiance.rgb *
                SampleAtmosphereMultipleScattering(
                    positionRelativeToCamera, lightDirection) *
                bodyAmbientVisibility *
                max(uAtmosphere.heightFogEmissiveAndSkyScale.w, 0.0);
        }
    }

    // 非天体主光仍能驱动近场雾，但不会冒充 TOA 光源参与物理天空。
    if (hasHeightFog && !heightFogHasCelestialLight &&
        uAtmosphereFrame.preparedMainLightDirectionAndValidity.w > 0.5)
    {
        vec3 lightDirection = normalize(
            uAtmosphereFrame.preparedMainLightDirectionAndValidity.xyz);
        vec3 preparedIrradiance =
            uAtmosphereFrame.preparedMainLightColorAndIntensity.rgb *
            uAtmosphereFrame.preparedMainLightColorAndIntensity.w;
        vec3 fogCloudVisibility =
            uAtmosphere.heightFogLightingParameters.y > 0.5 &&
            dot(lightDirection, AtmospherePrimaryDirection()) > 0.999
                ? primaryDirectVisibility : vec3(1.0);
        source += medium.heightFogScattering * preparedIrradiance *
            fogCloudVisibility *
            AtmosphereHeightFogPhase(dot(viewDirection, lightDirection)) *
            max(uAtmosphere.heightFogLightingParameters.x, 0.0);
    }
    return source;
}

// 将原始云透射率转换为当前场景的直射/低频多次散射可见度。
// 云层联合步进会传入该采样点到太阳的局部透射率；普通 Aerial pass
// 则传入地面 clipmap 的完整云柱透射率。
vec3 EvaluateAtmosphereScatteringSourceWithPrimaryCloudTransmittance(
    vec3 positionRelativeToCamera,
    vec3 viewDirection,
    AtmosphereMedium medium,
    vec3 rawPrimaryCloudTransmittance)
{
    vec3 clampedTransmittance = clamp(
        rawPrimaryCloudTransmittance, vec3(0.0), vec3(1.0));
    return EvaluateAtmosphereScatteringSourceWithResolvedPrimaryVisibility(
        positionRelativeToCamera,
        viewDirection,
        medium,
        mix(vec3(1.0), clampedTransmittance,
            AtmosphereCloudShadowStrength()),
        mix(vec3(1.0), clampedTransmittance,
            AtmosphereCloudAmbientOcclusionStrength()));
}

// 物理大气与近场 Height Fog 共用同一个光源可见度查询。
// 云影只调制直射辐照度；多次散射/天空填充使用独立的低频 AO 强度。
vec3 EvaluateAtmosphereScatteringSource(
    vec3 positionRelativeToCamera,
    vec3 viewDirection,
    AtmosphereMedium medium)
{
    return EvaluateAtmosphereScatteringSourceWithPrimaryCloudTransmittance(
        positionRelativeToCamera,
        viewDirection,
        medium,
        SampleAtmosphereCloudShadow(positionRelativeToCamera));
}

// 探针捕获应反映清空中的大气，而不把主视图附近的动态云影烘进缓存。
vec3 EvaluateAtmosphereClearSkyScatteringSource(
    vec3 positionRelativeToCamera,
    vec3 viewDirection,
    AtmosphereMedium medium)
{
    return EvaluateAtmosphereScatteringSourceWithResolvedPrimaryVisibility(
        positionRelativeToCamera, viewDirection, medium,
        vec3(1.0), vec3(1.0));
}

vec3 AtmospherePrimaryDirection()
{
    vec4 directionAndValidity =
        uAtmosphereFrame.atmosphereCelestialBodies[0].directionAndValidity;
    float directionLengthSquared = dot(
        directionAndValidity.xyz, directionAndValidity.xyz);
    return directionAndValidity.w > 0.5 && directionLengthSquared > 1.0e-8
        ? directionAndValidity.xyz * inversesqrt(directionLengthSquared)
        : vec3(0.0, 1.0, 0.0);
}

vec3 AtmospherePrimaryIrradiance()
{
    return uAtmosphereFrame.atmosphereCelestialBodies[0].directionAndValidity.w > 0.5
        ? uAtmosphereFrame.atmosphereCelestialBodies[0].topOfAtmosphereIrradiance.rgb
        : vec3(0.0);
}

// CloudShadow.comp stores Beer transmittance for rays that start on the ground
// and travel toward the primary celestial light.  A receiver below the cloud
// layer must therefore be reprojected to the ground along -lightDirection;
// radial projection would slide the shadow with receiver height and reverse
// the expected sun-shaft direction.  Receivers in/above the cloud layer are
// handled by cloud self-shadowing and interval composition, because this 2D
// map does not contain the depth needed for a partial cloud-column query.
vec3 SampleAtmosphereCloudShadow(vec3 positionRelativeToCamera)
{
    float cloudDensity = uVolumetricCloud.cloudRuntimeParameters[0].w;
    float cloudMinimumAltitude = uVolumetricCloud.cloudRuntimeParameters[0].y;
    vec4 clipmap = uVolumetricCloud.cloudRuntimeParameters[14];
    if (cloudDensity <= 1.0e-7 || clipmap.w < 0.5 ||
        uAtmosphereFrame.atmosphereCelestialBodies[0].directionAndValidity.w <= 0.5)
        return vec3(1.0);

    vec3 centerRelative = uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz;
    vec3 receiverRadial = positionRelativeToCamera - centerRelative;
    float receiverAltitude = length(receiverRadial) - AtmosphereBottomRadius();
    if (receiverAltitude >= cloudMinimumAltitude)
        return vec3(1.0);

    vec3 lightDirection = AtmospherePrimaryDirection();
    float groundEntry;
    float groundExit;
    if (!AtmosphereRaySphere(positionRelativeToCamera, -lightDirection,
        centerRelative, AtmosphereBottomRadius(), groundEntry, groundExit))
        return vec3(1.0);
    vec3 groundPoint = positionRelativeToCamera - lightDirection * groundEntry -
        centerRelative;

    vec3 cameraRadial = -centerRelative;
    vec3 up = normalize(cameraRadial);
    vec3 tangentX = normalize(cross(abs(up.y) < 0.99 ? vec3(0.0, 1.0, 0.0) :
        vec3(1.0, 0.0, 0.0), up));
    vec3 tangentY = cross(up, tangentX);
    vec3 cameraGroundPoint = up * AtmosphereBottomRadius();
    vec2 tangentOffset = vec2(dot(groundPoint - cameraGroundPoint, tangentX),
        dot(groundPoint - cameraGroundPoint, tangentY));
    int clipmapCount = clamp(int(clipmap.w + 0.5), 1, 2);
    vec3 levelVisibility[2];
    float levelMaximumOffset[2];
    bool levelValid[2];
    for (int level = 0; level < 2; ++level)
    {
        levelVisibility[level] = vec3(1.0);
        levelMaximumOffset[level] = 1.0e30;
        levelValid[level] = false;
        if (level >= clipmapCount)
            continue;
        float fraction = clipmapCount <= 1 ? 0.0 : float(level) / float(clipmapCount - 1);
        float coverageMeters = mix(clipmap.x, clipmap.y, fraction);
        if (coverageMeters <= 0.0)
            continue;
        float resolution = max(uVolumetricCloud.cloudRuntimeParameters[14].z, 1.0);
        float texelWorldMeters = 2.0 * coverageMeters / resolution;
        vec2 cameraTangentWorld = vec2(
            dot(cameraRadial, tangentX), dot(cameraRadial, tangentY));
        vec2 snappedCenter = round(cameraTangentWorld / texelWorldMeters) *
            texelWorldMeters;
        vec2 snappedOffset = snappedCenter - cameraTangentWorld;
        vec2 levelOffset = tangentOffset - snappedOffset;
        float maximumOffset = max(abs(levelOffset.x), abs(levelOffset.y));
        levelMaximumOffset[level] = maximumOffset;
        if (maximumOffset > coverageMeters)
            continue;
        vec2 uv = levelOffset / (2.0 * coverageMeters) + 0.5;
        levelVisibility[level] = texture(atmosphereCloudShadow,
            vec3(clamp(uv, vec2(0.0), vec2(1.0)), float(level))).rgb;
        levelValid[level] = true;
    }

    if (!levelValid[0])
        return levelValid[1] ? levelVisibility[1] : vec3(1.0);
    if (!levelValid[1])
        return levelVisibility[0];
    float crossFadeFraction = clamp(
        uVolumetricCloud.cloudRuntimeParameters[15].y, 0.0, 0.5);
    float nearCoverage = clipmap.x;
    float blendStart = nearCoverage * (1.0 - crossFadeFraction);
    float blend = crossFadeFraction > 0.0
        ? smoothstep(blendStart, nearCoverage, levelMaximumOffset[0])
        : 0.0;
    return mix(levelVisibility[0], levelVisibility[1], blend);
}

vec3 AtmosphereSkyBasisX(vec3 up, vec3 sunDirection)
{
    vec3 projectedSun = sunDirection - up * dot(up, sunDirection);
    if (dot(projectedSun, projectedSun) < 1.0e-8)
        projectedSun = abs(up.y) < 0.99 ? cross(up, vec3(0.0, 1.0, 0.0))
                                        : cross(up, vec3(1.0, 0.0, 0.0));
    return normalize(projectedSun);
}

vec3 AtmosphereSkyDirectionFromUv(vec2 uv)
{
    vec3 cameraRadial = -uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz;
    vec3 up = normalize(cameraRadial);
    vec3 basisX = AtmosphereSkyBasisX(up, AtmospherePrimaryDirection());
    vec3 basisY = normalize(cross(up, basisX));
    float zenithCosine = clamp(uv.y * 2.0 - 1.0, -1.0, 1.0);
    float azimuth = (uv.x * 2.0 - 1.0) * ATM_PI;
    float horizontal = sqrt(max(1.0 - zenithCosine * zenithCosine, 0.0));
    return normalize(up * zenithCosine + horizontal *
        (basisX * cos(azimuth) + basisY * sin(azimuth)));
}

vec2 AtmosphereSkyUvFromDirection(vec3 direction)
{
    vec3 cameraRadial = -uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz;
    vec3 up = normalize(cameraRadial);
    vec3 basisX = AtmosphereSkyBasisX(up, AtmospherePrimaryDirection());
    vec3 basisY = normalize(cross(up, basisX));
    float zenithCosine = clamp(dot(direction, up), -1.0, 1.0);
    float azimuth = atan(dot(direction, basisY), dot(direction, basisX));
    return vec2(azimuth / (2.0 * ATM_PI) + 0.5, zenithCosine * 0.5 + 0.5);
}

vec3 SampleAtmosphereSkyRadiance(vec3 direction)
{
    vec3 normalizedDirection = normalize(direction);
    return texture(atmosphereSkyViewLut,
        AtmosphereSkyUvFromDirection(normalizedDirection)).rgb;
}

vec3 EvaluateAtmosphereCelestialDiskAtPosition(
    uint bodyIndex,
    vec3 positionRelativeToCamera,
    vec3 viewDirection)
{
    if (uAtmosphere.atmosphereFeatureFlags.x == 0u)
        return vec3(0.0);

    AtmosphereCelestialBody body =
        uAtmosphereFrame.atmosphereCelestialBodies[bodyIndex];
    float angularRadius = body.diskParameters.x;
    if (body.directionAndValidity.w <= 0.5 || angularRadius <= 0.0)
        return vec3(0.0);
    vec3 lightDirection = normalize(body.directionAndValidity.xyz);
    float angle = acos(clamp(dot(normalize(viewDirection), lightDirection),
        -1.0, 1.0));
    float feather = max(body.diskParameters.y, 1.0e-6);
    float disk = 1.0 - smoothstep(
        angularRadius, angularRadius + feather, angle);
    return disk * body.topOfAtmosphereIrradiance.rgb *
        body.diskParameters.z *
        SampleAtmosphereTransmittance(
            positionRelativeToCamera, lightDirection);
}

vec3 EvaluateAtmosphereCelestialDisksAtPosition(
    vec3 positionRelativeToCamera,
    vec3 viewDirection)
{
    vec3 radiance = vec3(0.0);
    for (uint bodyIndex = 0u; bodyIndex < 2u; ++bodyIndex)
        radiance += EvaluateAtmosphereCelestialDiskAtPosition(
            bodyIndex, positionRelativeToCamera, viewDirection);
    return radiance;
}

// 为反射探针、环境立方体等非主视图消费者在指定世界位置直接积分天空。
// 该入口只依赖静态 Transmittance/MultiScattering LUT，不会错误复用主相机 SkyView。
vec3 IntegrateAtmosphereSkyRadianceAtPosition(
    vec3 positionRelativeToCamera,
    vec3 viewDirection,
    uint requestedSampleCount)
{
    vec3 direction = normalize(viewDirection);
    float atmosphereEntry;
    float atmosphereExit;
    bool intersectsAtmosphere = AtmosphereRaySphere(
        positionRelativeToCamera,
        direction,
        uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz,
        AtmosphereTopRadius(),
        atmosphereEntry,
        atmosphereExit);

    if (!intersectsAtmosphere)
        return vec3(0.0);

    float startDistance = atmosphereEntry;
    float endDistance = atmosphereExit;
    float groundDistance;
    bool hitsGround = AtmosphereRayHitsGround(
        positionRelativeToCamera, direction, groundDistance);
    if (hitsGround)
        endDistance = min(endDistance, groundDistance);

    uint sampleCount = max(requestedSampleCount, 1u);
    float stepMeters = max(endDistance - startDistance, 0.0) /
        float(sampleCount);
    vec3 opticalDepth = vec3(0.0);
    vec3 radiance = vec3(0.0);
    for (uint sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
    {
        float distance = startDistance +
            (float(sampleIndex) + 0.5) * stepMeters;
        vec3 samplePosition = positionRelativeToCamera + direction * distance;
        AtmosphereMedium medium =
            EvaluateAtmosphereMedium(AtmosphereAltitude(samplePosition));
        vec3 transmittance = exp(-min(opticalDepth, vec3(80.0)));
        vec3 source = EvaluateAtmosphereClearSkyScatteringSource(
            samplePosition, direction, medium);
        radiance += transmittance * AtmosphereIntegratedSegmentSource(
            source, medium.extinction, stepMeters);
        opticalDepth += medium.extinction * stepMeters;
    }

    vec3 pathTransmittance = exp(-min(opticalDepth, vec3(80.0)));
    if (hitsGround && groundDistance <= atmosphereExit &&
        uAtmosphere.atmosphereFeatureFlags.x != 0u)
    {
        vec3 groundPosition = positionRelativeToCamera +
            direction * groundDistance;
        vec3 groundNormal = normalize(groundPosition -
            uAtmosphereFrame.planetCenterRelativeToCameraMeters.xyz);
        vec3 groundLighting = vec3(0.0);
        for (uint bodyIndex = 0u; bodyIndex < 2u; ++bodyIndex)
        {
            AtmosphereCelestialBody body =
                uAtmosphereFrame.atmosphereCelestialBodies[bodyIndex];
            if (body.directionAndValidity.w <= 0.5)
                continue;
            vec3 lightDirection = normalize(body.directionAndValidity.xyz);
            groundLighting += body.topOfAtmosphereIrradiance.rgb *
                SampleAtmosphereTransmittance(
                    groundPosition, lightDirection) *
                max(dot(groundNormal, lightDirection), 0.0);
        }
        radiance += pathTransmittance *
            uAtmosphere.ozoneHalfWidthAndGroundAlbedo.yzw *
            groundLighting / ATM_PI;
    }
    return max(radiance, vec3(0.0));
}

float AtmosphereAerialSliceCoordinate(float distanceMeters)
{
    float maximumDistance = max(uAtmosphereFrame.cameraWorldMetersAndMaxDistance.w, 1.0);
    return sqrt(clamp(distanceMeters / maximumDistance, 0.0, 1.0));
}

#endif
