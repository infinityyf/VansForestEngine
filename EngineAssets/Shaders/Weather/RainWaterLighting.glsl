#ifndef RAIN_WATER_LIGHTING_GLSL
#define RAIN_WATER_LIGHTING_GLSL

#define TILE_LIGHT
#define VANS_SURFACE_COOKIES
#include "../Common/TileLightData.glsl"
#include "../Lights/LightsData.glsl"

layout(set = 1, binding = 4) uniform sampler2DArray cascadeShadowMap;
layout(set = 1, binding = 5) uniform sampler2DShadow punctualShadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT];

const float RainWaterIor = 1.33;
const float RainWaterF0 = 0.0200593122;
const float RainOpticalRoughness = 0.12;

float RainWaterFresnel(vec3 normalWS, vec3 viewWS)
{
    float noV = clamp(dot(normalWS, viewWS), 0.0, 1.0);
    return RainWaterF0 + (1.0 - RainWaterF0) * pow(1.0 - noV, 5.0);
}

vec3 RainSampleReflectionRadiance(vec3 positionWS, vec3 normalWS, vec3 viewWS)
{
    vec3 reflectionDirection = normalize(reflect(-viewWS, normalWS));
    ReflectionProbeSample reflectionProbe = SampleReflectionProbes(
        positionWS, normalWS, reflectionDirection, RainOpticalRoughness);
    vec3 reflectionSky = SampleSkySpecularCube(
        PreConvSpecularEnvironment, reflectionDirection,
        GetMipLevelFromRoughness(RainOpticalRoughness));
    return mix(reflectionSky, reflectionProbe.specular,
        reflectionProbe.coverage);
}

vec3 RainSampleReflection(vec3 positionWS, vec3 normalWS, vec3 viewWS)
{
    return RainSampleReflectionRadiance(positionWS, normalWS, viewWS) *
        RainWaterFresnel(normalWS, viewWS);
}

vec3 RainSampleEnvironment(vec3 positionWS, vec3 normalWS, vec3 viewWS)
{
    float fresnel = RainWaterFresnel(normalWS, viewWS);
    float lod = GetMipLevelFromRoughness(RainOpticalRoughness);

    vec3 reflectionDirection = normalize(reflect(-viewWS, normalWS));
    vec3 reflection = RainSampleReflectionRadiance(
        positionWS, normalWS, viewWS);

    vec3 refractionDirection = refract(-viewWS, normalWS, 1.0 / RainWaterIor);
    if (dot(refractionDirection, refractionDirection) < 1.0e-5)
        refractionDirection = reflectionDirection;
    refractionDirection = normalize(refractionDirection);
    ReflectionProbeSample refractionProbe = SampleReflectionProbes(
        positionWS, -normalWS, refractionDirection, RainOpticalRoughness);
    vec3 refractionSky = SampleSkySpecularCube(
        PreConvSpecularEnvironment, refractionDirection, lod);
    vec3 refraction = mix(refractionSky, refractionProbe.specular,
        refractionProbe.coverage);

    return mix(refraction, reflection, fresnel);
}

vec3 RainDirectSpecular(
    vec3 positionWS, vec3 normalWS, vec3 viewWS,
    vec3 lightDirectionWS, vec3 radiance, float visibility)
{
    BRDFData brdf;
    brdf.albedo = vec3(0.0);
    brdf.normal = normalWS;
    brdf.roughness = RainOpticalRoughness;
    brdf.metallic = 0.0;
    brdf.ao = 1.0;
    brdf.fresnel0 = vec3(RainWaterF0);
    brdf.viewDirection = viewWS;
    brdf.positionWS = positionWS;
    brdf.indirectDiffuse = vec3(0.0);
    brdf.indirectSpecular = vec4(0.0);
    vec3 unusedDiffuse = vec3(0.0);
    vec3 specular = vec3(0.0);
    DirectBRDF(brdf, lightDirectionWS, unusedDiffuse, specular);
    return specular * radiance * visibility;
}

vec3 RainEvaluateDirectLighting(vec3 positionWS, vec3 normalWS, vec3 viewWS)
{
    vec3 direct = vec3(0.0);
    float viewDepth = abs((ViewMatrix * vec4(positionWS, 1.0)).z);
    vec3 directionalL = normalize(uDirectionLight.direction.xyz);
    float directionalVisibility = SampleCascadeShadow(
        positionWS, normalWS, cascadeShadowMap, viewDepth);
    direct += RainDirectSpecular(
        positionWS, normalWS, viewWS, directionalL,
        uDirectionLight.color.rgb * uDirectionLight.intensity *
            SampleSurfaceLightCookie(0, positionWS),
        directionalVisibility);

    TileLightHeader tileHeader = GetFragTileLightHeader();
    for (uint tileIndex = 0u; tileIndex < tileHeader.pointCount; ++tileIndex)
    {
        uint lightIndex = tileLightIndices[tileHeader.pointOffset + tileIndex];
        PointLightData light = GetPointLight(int(lightIndex));
        vec3 toLight = light.position.xyz - positionWS;
        float distanceToLight = length(toLight);
        if (distanceToLight <= 1.0e-5 || distanceToLight > light.radius)
            continue;
        vec3 L = toLight / distanceToLight;
        float attenuation = 1.0 - distanceToLight / max(light.radius, 1.0e-4);
        attenuation *= attenuation;
        float visibility = light.shadowMetaIndex != INVALID_SHADOW_INDEX
            ? SamplePointShadowMapBRDF(
                positionWS, normalWS, L, punctualShadowMap, int(lightIndex))
            : 1.0;
        vec3 radiance = light.color.rgb * light.intensity * attenuation *
            SampleSurfaceLightCookie(1 + int(lightIndex), positionWS);
        direct += RainDirectSpecular(
            positionWS, normalWS, viewWS, L, radiance, visibility);
    }

    for (uint tileIndex = 0u; tileIndex < tileHeader.spotCount; ++tileIndex)
    {
        uint lightIndex = tileLightIndices[tileHeader.spotOffset + tileIndex];
        SpotLightData light = GetSpotLight(int(lightIndex));
        vec3 toLight = light.position.xyz - positionWS;
        float distanceToLight = length(toLight);
        if (distanceToLight <= 1.0e-5 || distanceToLight > light.radius)
            continue;
        vec3 L = toLight / distanceToLight;
        float coneAngle = dot(normalize(light.direction.xyz), L);
        float outerCone = cos(light.outerConeAngle);
        if (coneAngle < outerCone)
            continue;
        float innerCone = cos(light.innerConeAngle);
        float coneAttenuation = clamp(
            (coneAngle - outerCone) / max(innerCone - outerCone, 1.0e-4), 0.0, 1.0);
        float attenuation = 1.0 - distanceToLight / max(light.radius, 1.0e-4);
        attenuation *= attenuation;
        float visibility = light.shadowMetaIndex != INVALID_SHADOW_INDEX
            ? SampleSpotShadowMapBRDF(
                positionWS, normalWS, L, punctualShadowMap, int(lightIndex))
            : 1.0;
        vec3 radiance = light.color.rgb * light.intensity * attenuation *
            coneAttenuation * SampleSurfaceLightCookie(65 + int(lightIndex), positionWS);
        direct += RainDirectSpecular(
            positionWS, normalWS, viewWS, L, radiance, visibility);
    }
    return direct;
}

#endif
