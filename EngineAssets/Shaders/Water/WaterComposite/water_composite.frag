#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../BRDF/ReflectionProbeData.glsl"
#include "../../Common/CameraData.glsl"
#include "../water_screen_common.glsl"

const float WATER_PI = 3.14159265358979323846;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 3) uniform sampler2D BRDFLUT;
layout(set = 0, binding = 5) uniform samplerCube PreConvSpecularEnvironment;

layout(set = 1, binding = 0) uniform sampler2D waterGBufNormal;
layout(set = 1, binding = 1) uniform sampler2D waterGBufPosDepth;
layout(set = 1, binding = 3) uniform sampler2D sceneGBuf2;
layout(set = 1, binding = 4) uniform sampler2D waterReflection;
layout(set = 1, binding = 5) uniform sampler2D waterRefraction;
layout(set = 1, binding = 6) uniform sampler2D waterCaustics;
layout(set = 1, binding = 9) uniform sampler2D waterSSSScatter;

layout(set = 1, binding = 2) uniform WaterCompositeParams
{
    vec4  deepWaterColor;
    vec4  shallowWaterColor;
    float fresnelPower;
    float waterLevel;
    float specularIntensity;
    float refractionStrength;
    vec4  absorptionCoeff;
    vec4  scatteringCoeff;
    float sssAnisotropy;
    float waterRoughness;
    float waterIOR;
    float maxOpticalDepth;
    vec4  cameraPosition;
    mat4  invViewProjMatrix;
    vec4  mainLightDir;
    mat4  viewMatrix;
    mat4  projMatrix;
    ivec4 effectFlags; // x=SSR, y=refraction, z=caustics, w=SSS
} p;

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (vec3(1.0) - F0) * f;
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * f;
}

float DistributionGGX(float NdotH, float roughness)
{
    float alpha = max(roughness * roughness, 0.0025);
    float alpha2 = alpha * alpha;
    float d = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(WATER_PI * d * d, 1e-6);
}

float VisibilitySmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
    float alpha = max(roughness * roughness, 0.0025);
    float alpha2 = alpha * alpha;
    float lambdaV = NdotL * sqrt(max(NdotV * NdotV * (1.0 - alpha2) + alpha2, 0.0));
    float lambdaL = NdotV * sqrt(max(NdotL * NdotL * (1.0 - alpha2) + alpha2, 0.0));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

vec3 EvaluateDirectSpecular(vec3 N, vec3 V, vec3 L, vec3 F0)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotV <= 0.0 || NdotL <= 0.0)
        return vec3(0.0);

    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    float D = DistributionGGX(NdotH, p.waterRoughness);
    float Vis = VisibilitySmithGGXCorrelated(NdotV, NdotL, p.waterRoughness);
    vec3 F = FresnelSchlick(VdotH, F0);
    return D * Vis * F * NdotL * p.specularIntensity;
}

float SchlickPhase(float cosTheta, float g)
{
    float g2 = g * g;
    float d = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * WATER_PI * d * sqrt(d));
}

void main()
{
    vec2 screenUV = clamp(vec2(inUV.x, 1.0 - inUV.y), 0.001, 0.999);
    ivec2 size = textureSize(waterGBufPosDepth, 0);
    ivec2 pixel = WaterUVToPixel(screenUV, size);

    vec4 normalCoverage = texelFetch(waterGBufNormal, pixel, 0);
    vec4 surface = texelFetch(waterGBufPosDepth, pixel, 0);
    if (!WaterSurfaceValid(normalCoverage, surface))
        discard;

    vec4 scene = texelFetch(sceneGBuf2, pixel, 0);
    if (SceneOccludesWater(scene.w, surface.w))
        discard;

    vec3 W = surface.xyz;
    vec3 N = normalize(normalCoverage.xyz);
    vec3 V = normalize(p.cameraPosition.xyz - W);
    vec3 L = normalize(p.mainLightDir.xyz);
    float NdotV = max(dot(N, V), 0.0);

    float f0Scalar = (p.waterIOR - 1.0) / max(p.waterIOR + 1.0, 1e-4);
    vec3 F0 = vec3(f0Scalar * f0Scalar);
    vec3 F = FresnelSchlickRoughness(NdotV, F0, p.waterRoughness);
    vec3 transmissionWeight = vec3(1.0) - F;

    vec4 ssr = texelFetch(waterReflection, pixel, 0);
    vec3 R = reflect(-V, N);
    float lod = p.waterRoughness * 9.0;
    ReflectionProbeSample probe = SampleReflectionProbes(W, N, R, p.waterRoughness);
    vec3 sky = SampleSkySpecularCube(PreConvSpecularEnvironment, R, lod);
    vec3 reflectedRadiance = mix(sky, probe.specular, probe.coverage);
    float roughnessFade = 1.0 - smoothstep(reflectionProbeLightingParams.x,
                                           reflectionProbeLightingParams.y,
                                           p.waterRoughness);
    float ssrConfidence = p.effectFlags.x != 0
        ? clamp(ssr.a * roughnessFade, 0.0, 1.0)
        : 0.0;
    reflectedRadiance = mix(reflectedRadiance, ssr.rgb, ssrConfidence);

    vec2 lutUV = vec2(NdotV, 1.0 - p.waterRoughness);
    vec2 envBRDF = texture(BRDFLUT, lutUV).rg;
    vec3 reflected = reflectedRadiance * (F * envBRDF.x + envBRDF.y);

    vec3 refracted = p.effectFlags.y != 0
        ? texelFetch(waterRefraction, pixel, 0).rgb
        : p.shallowWaterColor.rgb;
    vec3 caustics = p.effectFlags.z != 0
        ? texelFetch(waterCaustics, pixel, 0).rgb
        : vec3(0.0);

    // The deferred scene already contains ordinary direct illumination.
    // Caustics is therefore a bounded, colored concentration gain rather than
    // additive white radiance; this prevents shallow bright terrain from
    // being illuminated twice.
    vec3 transmitted = transmissionWeight * refracted * (vec3(1.0) + caustics);
    vec3 direct = EvaluateDirectSpecular(N, V, L, F0);

    vec3 sss = vec3(0.0);
    if (p.effectFlags.w != 0)
    {
        vec3 scatter = texelFetch(waterSSSScatter, pixel, 0).rgb;
        float phase = SchlickPhase(dot(V, L), p.sssAnisotropy);
        sss = transmissionWeight * scatter * phase * max(dot(N, L), 0.0);
    }

    vec3 color = max(reflected + transmitted + direct + sss, vec3(0.0));

    if (reflectionProbeDebugView != 0u)
    {
        if (reflectionProbeDebugView == 1u)
        {
            float id = float(max(probe.topIndex, 0));
            color = (0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + id * 2.3999632)) *
                    clamp(probe.topWeight, 0.0, 1.0);
        }
        else if (reflectionProbeDebugView == 2u || reflectionProbeDebugView == 6u) color = probe.specular;
        else if (reflectionProbeDebugView == 3u) color = vec3(ssr.a);
        else if (reflectionProbeDebugView == 4u)
        {
            uint region = probe.topIndex >= 0 ? reflectionProbes[probe.topIndex].regionAndFlags.x : 0xffffffffu;
            color = fract(vec3(0.1031, 0.11369, 0.13787) * float(region + 1u));
        }
        else if (reflectionProbeDebugView == 5u) color = abs(probe.parallaxDelta);
        else if (reflectionProbeDebugView == 7u) color = ssr.rgb;
    }

    outColor = vec4(color, 1.0);
}
