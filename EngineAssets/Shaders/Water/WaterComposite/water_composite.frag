#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../BRDF/ReflectionProbeData.glsl"
#include "../../Common/CameraData.glsl"
#include "../water_screen_common.glsl"
#include "../PBRWater/pbr_water_bsdf.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 3) uniform sampler2D BRDFLUT;
layout(set = 0, binding = 5) uniform samplerCube PreConvSpecularEnvironment;

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

layout(set = 1, binding = 2) uniform PBRWaterParams
{
    vec4 absorptionCoeff;
    vec4 scatteringCoeff;
    vec4 cameraPosition;
    vec4 mainLightDir;
    vec4 mainLightColor;
    vec4 surfaceParams;
    vec4 refractionParams;
    vec4 volumeParams;
    vec4 thinSSSParams;
    vec4 backlitParams;
    vec4 filterParams;
    ivec4 effectFlags;
    mat4 invViewProjMatrix;
    mat4 viewMatrix;
    mat4 projMatrix;
} p;

vec3 SampleRefractedScene(vec2 baseUV, vec2 uvOffset, float dispersion)
{
    if (p.effectFlags.y == 0)
        uvOffset = vec2(0.0);
    vec2 rUV = clamp(baseUV + uvOffset * (1.0 + dispersion * 0.012), vec2(0.001), vec2(0.999));
    vec2 gUV = clamp(baseUV + uvOffset, vec2(0.001), vec2(0.999));
    vec2 bUV = clamp(baseUV + uvOffset * (1.0 - dispersion * 0.012), vec2(0.001), vec2(0.999));
    return vec3(
        textureLod(sceneColor, rUV, 0.0).r,
        textureLod(sceneColor, gUV, 0.0).g,
        textureLod(sceneColor, bUV, 0.0).b);
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
    float roughness = clamp(normalRoughness.a, 0.002, 0.3);
    vec3 F0 = vec3(p.surfaceParams.z);
    float NdotV = max(dot(N, V), 0.0);
    vec3 Fview = PBRW_FresnelSchlick(F0, NdotV);
    vec3 transmissionWeight = vec3(1.0) - Fview;

    vec4 ssr = texelFetch(waterReflection, pixel, 0);
    vec3 R = reflect(-V, N);
    ReflectionProbeSample probe = SampleReflectionProbes(W, N, R, roughness);
    vec3 sky = SampleSkySpecularCube(PreConvSpecularEnvironment, R, roughness * 9.0);
    vec3 reflectedRadiance = mix(sky, probe.specular, probe.coverage);
    float roughnessFade = 1.0 - smoothstep(reflectionProbeLightingParams.x,
                                           reflectionProbeLightingParams.y,
                                           roughness);
    float ssrConfidence = p.effectFlags.x != 0 ? clamp(ssr.a * roughnessFade, 0.0, 1.0) : 0.0;
    reflectedRadiance = mix(reflectedRadiance, ssr.rgb, ssrConfidence);
    vec2 envBRDF = texture(BRDFLUT, vec2(NdotV, 1.0 - roughness)).rg;
    vec3 reflected = reflectedRadiance * (Fview * envBRDF.x + envBRDF.y);

    vec3 directSpec = PBRW_EvaluateDirectSpecular(
        N, V, L, F0, roughness, p.surfaceParams.w) * max(p.mainLightColor.rgb, vec3(0.0));

    vec4 refractionData = texelFetch(waterRefractionData, pixel, 0);
    vec3 refractedScene = SampleRefractedScene(
        screenUV, refractionData.xy, p.refractionParams.z);
    vec3 volumeDiffuse = textureLod(waterVolumeColor, screenUV, 0.0).rgb;
    vec3 volumeT = textureLod(waterVolumeTransmittance, screenUV, 0.0).rgb;
    vec3 caustics = p.effectFlags.z != 0 ? texelFetch(waterCaustics, pixel, 0).rgb : vec3(0.0);

    vec3 atmosphere = max(p.mainLightColor.rgb, vec3(0.0)) * 0.015;
    vec3 transmitted = refractedScene * volumeT * (vec3(1.0) + caustics);
    vec3 color = volumeDiffuse * transmissionWeight
        + reflected
        + directSpec
        + transmitted
        + atmosphere * (vec3(1.0) - volumeT);

    if (reflectionProbeDebugView != 0u)
    {
        if (reflectionProbeDebugView == 3u) color = vec3(ssr.a);
        else if (reflectionProbeDebugView == 7u) color = ssr.rgb;
        else color = reflectedRadiance;
    }

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}
