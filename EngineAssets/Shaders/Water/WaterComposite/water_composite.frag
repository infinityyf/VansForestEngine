#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../BRDF/ReflectionProbeData.glsl"
#include "../../Common/CameraData.glsl"
#include "../water_screen_common.glsl"
#include "../PBRWater/pbr_water_bsdf.glsl"
#include "../../Lights/LightsData.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D waterGBufNormal;
layout(set = 1, binding = 1) uniform sampler2D waterGBufPosDepth;
layout(set = 1, binding = 3) uniform sampler2D sceneGBuf2;
layout(set = 1, binding = 4) uniform sampler2D waterReflection;
layout(set = 1, binding = 5) uniform sampler2D waterRefractionData;
layout(set = 1, binding = 6) uniform sampler2D waterCaustics;
layout(set = 1, binding = 7) uniform sampler2D waterGBufScatter;
layout(set = 1, binding = 8) uniform sampler2D waterGBufAbsorption;
layout(set = 1, binding = 9) uniform sampler2D waterVolumeColor;
layout(set = 1, binding = 10) uniform sampler2D waterVolumeTransmittance;
layout(set = 1, binding = 11) uniform sampler2D waterVolumeDepth;
layout(set = 1, binding = 12) uniform sampler2D sceneColor;
layout(set = 1, binding = 13) uniform sampler2DArrayShadow cascadeShadowMap;

#define PBR_WATER_PARAMS_SET 1
#define PBR_WATER_PARAMS_BINDING 2
#define PBR_WATER_PARAMS_INSTANCE p
#include "../PBRWater/pbr_water_params.glsl"

vec3 SampleRefractedScene(vec2 baseUV, vec2 uvOffset, float dispersion, float colorLod)
{
    if (p.effectFlags.y == 0)
        uvOffset = vec2(0.0);
    vec2 rUV = clamp(baseUV + uvOffset * (1.0 + dispersion * 0.012), vec2(0.001), vec2(0.999));
    vec2 gUV = clamp(baseUV + uvOffset, vec2(0.001), vec2(0.999));
    vec2 bUV = clamp(baseUV + uvOffset * (1.0 - dispersion * 0.012), vec2(0.001), vec2(0.999));
    return vec3(
        textureLod(sceneColor, rUV, colorLod).r,
        textureLod(sceneColor, gUV, colorLod).g,
        textureLod(sceneColor, bUV, colorLod).b);
}

void main()
{
    vec2 screenUV = clamp(vec2(inUV.x, 1.0 - inUV.y), 0.001, 0.999);
    ivec2 size = textureSize(waterGBufPosDepth, 0);
    ivec2 pixel = WaterUVToPixel(screenUV, size);

    vec4 normalRoughness = texelFetch(waterGBufNormal, pixel, 0);
    vec4 surface = texelFetch(waterGBufPosDepth, pixel, 0);
    if (!WaterSurfaceValid(normalRoughness, surface))
        discard;

    vec4 scene = texelFetch(sceneGBuf2, pixel, 0);
    if (SceneOccludesWater(scene.w, surface.w))
        discard;

    vec3 W = surface.xyz;
    vec3 N = normalize(normalRoughness.xyz);
    vec3 V = normalize(p.cameraPosition.xyz - W);
    vec3 L = normalize(p.mainLightDir.xyz);
    float roughness = clamp(normalRoughness.a, 0.002, 1.0);
    vec3 F0 = vec3(p.surfaceParams.z);
    float NdotV = max(dot(N, V), 0.0);
    vec3 Fview = PBRW_FresnelSchlick(F0, NdotV);
    vec3 transmissionWeight = vec3(1.0) - Fview;

    vec4 ssr = texelFetch(waterReflection, pixel, 0);
    vec3 R = reflect(-V, N);
    ReflectionProbeSample probe = SampleReflectionProbes(W, N, R, roughness);
    vec3 sky = SampleSkySpecularCube(
        PreConvSpecularEnvironment, R, GetMipLevelFromRoughness(roughness));
    vec3 reflectedRadiance = mix(sky, probe.specular, probe.coverage);
    float ssrConfidence = p.effectFlags.x != 0 ? clamp(ssr.a, 0.0, 1.0) : 0.0;
    reflectedRadiance = mix(reflectedRadiance, ssr.rgb, ssrConfidence);
    vec2 envBRDF = texture(BRDFLUT, vec2(NdotV, 1.0 - roughness)).rg;
    vec3 reflected = reflectedRadiance * (Fview * envBRDF.x + envBRDF.y);

    float surfaceVisibility = p.shadowParams.x > 0.5
        ? SampleCascadeShadowFast(
            cascadeShadowMap, W, N, surface.w,
            p.shadowParams.z, p.shadowParams.w,
            clamp(int(p.shadowParams.y + 0.5), 0, 1))
        : 1.0;
    vec3 directIrradiance = max(p.mainLightColor.rgb, vec3(0.0));
    vec3 directSpec = PBRW_EvaluateDirectSpecular(
        N, V, L, F0, roughness, p.surfaceParams.w) *
        directIrradiance * surfaceVisibility;

    vec4 refractionData = texelFetch(waterRefractionData, pixel, 0);
    float refractionPathLength = clamp(
        max(refractionData.z - surface.w, 0.0), 0.0, p.volumeParams.x);
    vec3 sigmaS = max(texelFetch(waterGBufScatter, pixel, 0).rgb, vec3(0.0));
    ivec2 sceneColorSize = textureSize(sceneColor, 0);
    float sceneColorMaxMip = min(
        p.colorMipParams1.y, float(textureQueryLevels(sceneColor) - 1));
    float refractionColorLod = PBRW_ComputeColorMipLod(
        refractionPathLength, sigmaS, roughness, surface.w,
        float(sceneColorSize.y), p.projMatrix[1][1],
        p.colorMipParams0.x, p.colorMipParams0.y,
        p.colorMipParams0.w, sceneColorMaxMip);
    vec3 refractedScene = SampleRefractedScene(
        screenUV, refractionData.xy, p.refractionParams.z, refractionColorLod);
    vec3 volumeDiffuse = textureLod(waterVolumeColor, screenUV, 0.0).rgb;
    vec3 volumeT = textureLod(waterVolumeTransmittance, screenUV, 0.0).rgb;
    vec3 causticRadiance = p.effectFlags.z != 0
        ? texelFetch(waterCaustics, pixel, 0).rgb : vec3(0.0);

    // Preserve the established water-volume fill light.  The visible physical
    // sky is not the material environment source; water IBL remains the
    // authored SkyBox cache and this term remains tied to the prepared key.
    vec3 atmosphere = max(p.mainLightColor.rgb, vec3(0.0)) * 0.015;
    // The caustics pass outputs receiver-reflected radiance. Apply the return
    // water path once; do not multiply already-lit scene radiance by a gain.
    vec3 transmitted = (refractedScene + causticRadiance) * volumeT;
    vec3 transmissionRadiance = volumeDiffuse + transmitted +
        atmosphere * (vec3(1.0) - volumeT);
    vec3 color = transmissionRadiance * transmissionWeight
        + reflected
        + directSpec;

    if (reflectionProbeDebugView != 0u)
    {
        if (reflectionProbeDebugView == 3u) color = vec3(ssr.a);
        else if (reflectionProbeDebugView == 7u) color = ssr.rgb;
        else color = reflectedRadiance;
    }

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
