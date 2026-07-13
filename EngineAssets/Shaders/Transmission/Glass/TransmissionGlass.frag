#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "../../Common/CameraData.glsl"
#include "../../Common/CustomMaterialData.glsl"
#include "../../Lights/LightsData.glsl"

layout(set = 1, binding = 0) uniform sampler2D opaqueSceneColor;
layout(set = 1, binding = 1) uniform sampler2D ssrReflection;
layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec3 positionWS;
layout(location = 2) in vec3 normalWS;
layout(location = 3) in vec3 tangentWS;
layout(location = 4) in vec3 bitangentWS;
layout(location = 5) in vec4 clipPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform DrawPushConsts
{
    int materialIndex;
    int transformIndex;
    int animationEnabled;
    int passUser0;
} pc;

float DielectricF0FromIor(float ior)
{
    float f = (ior - 1.0) / max(ior + 1.0, 1e-4);
    return f * f;
}

vec3 FresnelSchlickLocal(float cosTheta, vec3 F0)
{
    float f = pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
    return F0 + (vec3(1.0) - F0) * f;
}

float D_GGX_Local(float NoH, float alpha)
{
    float a2 = alpha * alpha;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float V_SmithGGXCorrelatedLocal(float NoV, float NoL, float alpha)
{
    float a2 = alpha * alpha;
    float gv = NoL * sqrt(max(NoV * (NoV - NoV * a2) + a2, 1e-6));
    float gl = NoV * sqrt(max(NoL * (NoL - NoL * a2) + a2, 1e-6));
    return 0.5 / max(gv + gl, 1e-6);
}

vec3 EvaluateDirectSpecular(vec3 N, vec3 V, vec3 L, vec3 lightRadiance, vec3 F0, float roughness)
{
    vec3 H = normalize(V + L);
    float NoV = clamp(dot(N, V), 0.0, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    if (NoV <= 0.0 || NoL <= 0.0) return vec3(0.0);

    float alpha = max(roughness * roughness, 0.002);
    float D = D_GGX_Local(NoH, alpha);
    float Vis = V_SmithGGXCorrelatedLocal(NoV, NoL, alpha);
    vec3 F = FresnelSchlickLocal(VoH, F0);
    return lightRadiance * (D * Vis) * F * NoL;
}

vec3 EvaluateDirectDiffuse(vec3 N, vec3 V, vec3 L, vec3 lightRadiance, vec3 baseColor, vec3 F0, float transmission)
{
    vec3 H = normalize(V + L);
    float NoV = clamp(dot(N, V), 0.0, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    if (NoV <= 0.0 || NoL <= 0.0) return vec3(0.0);

    vec3 F = FresnelSchlickLocal(VoH, F0);
    vec3 diffuseWeight = (vec3(1.0) - F) * (1.0 - transmission);
    return lightRadiance * baseColor * diffuseWeight * (NoL / PI);
}

vec3 EvaluateEnvironmentDiffuse(vec3 N, vec3 V, vec3 baseColor, vec3 F0, float roughness, float transmission)
{
    float NoV = clamp(dot(N, V), 0.0, 1.0);
    vec3 F = fresnelSchlickRoughness(NoV, F0, roughness);
    vec3 diffuseWeight = (vec3(1.0) - F) * (1.0 - transmission);
    vec3 irradiance = texture(PreConvDiffuseEnvironment, N).rgb;
    return irradiance * baseColor * diffuseWeight;
}

vec3 EvaluateTransmittance(vec3 attenuationColor, float attenuationDistance, float thickness)
{
    if (attenuationDistance <= 0.0) return vec3(1.0);
    vec3 sigma = -log(max(attenuationColor, vec3(1e-4))) / attenuationDistance;
    return exp(-sigma * max(thickness, 0.0));
}

vec3 SampleTextureOrWhite(int textureIndex, vec2 uv)
{
    if (textureIndex < 0) return vec3(1.0);
    return texture(globalPBRTextures[nonuniformEXT(textureIndex)], uv).rgb;
}

float SampleTextureOrOne(int textureIndex, vec2 uv)
{
    if (textureIndex < 0) return 1.0;
    return texture(globalPBRTextures[nonuniformEXT(textureIndex)], uv).r;
}

vec3 BuildNormal(vec3 N, vec3 T, vec3 B, int normalIndex, float normalScale)
{
    vec3 n = normalize(N);
    vec3 t = normalize(T - n * dot(n, T));
    vec3 b = normalize(B);
    mat3 tbn = mat3(t, b, n);
    if (normalIndex < 0) return n;
    vec3 sampleN = texture(globalPBRTextures[nonuniformEXT(normalIndex)], fragUV).xyz * 2.0 - 1.0;
    sampleN.xy *= normalScale;
    return normalize(tbn * sampleN);
}

vec3 EvaluateEnvironmentReflection(vec3 P, vec3 N, vec3 V, vec3 F0, float roughness)
{
    float NoV = clamp(dot(N, V), 0.0, 1.0);
    vec3 F = fresnelSchlickRoughness(NoV, F0, roughness);
    vec3 R = reflect(-V, N);
    float lod = GetMipLevelFromRoughness(roughness);

    ReflectionProbeSample probe = SampleReflectionProbes(P, N, R, roughness);
    vec3 sky = textureLod(PreConvSpecularEnvironment, R, lod).rgb * reflectionProbeLightingParams.z;
    vec3 prefiltered = mix(sky, probe.specular, probe.coverage);

    vec2 lutUV = vec2(NoV, 1.0 - roughness);
    vec2 envBRDF = texture(BRDFLUT, lutUV).rg;
    return prefiltered * (F * envBRDF.x + envBRDF.y);
}

vec3 ApplyScreenSpaceReflection(vec3 environmentReflection, float roughness)
{
    vec2 uv = gl_FragCoord.xy * ScreenParams.zw;
    vec4 ssr = texture(ssrReflection, uv);
    float ssrFade = 1.0 - smoothstep(reflectionProbeLightingParams.x, reflectionProbeLightingParams.y, roughness);
    float ssrWeight = clamp(ssr.a * ssrFade, 0.0, 1.0);
    return mix(environmentReflection, ssr.rgb, ssrWeight);
}

vec3 SampleTransmissionFallback(vec3 N, vec3 V, float ior, float roughness)
{
    float eta = 1.0 / max(ior, 1.0001);
    vec3 T = refract(-V, N, eta);
    if (dot(T, T) < 1e-5)
    {
        T = reflect(-V, N);
    }
    float lod = GetMipLevelFromRoughness(roughness);
    return textureLod(PreConvSpecularEnvironment, normalize(T), lod).rgb * reflectionProbeLightingParams.z;
}

vec3 SampleScreenTransmission(vec3 N, vec3 V, float ior, float roughness, float thickness, float strength)
{
    float eta = 1.0 / max(ior, 1.0001);
    vec3 T = refract(-V, N, eta);
    if (dot(T, T) < 1e-5)
    {
        return SampleTransmissionFallback(N, V, ior, roughness);
    }

    vec2 uv = gl_FragCoord.xy * ScreenParams.zw;
    vec2 normalOffset = N.xy * 0.5;
    vec2 rayOffset = T.xy / max(abs(T.z) + 1.0, 1e-3);
    float distortionScale = clamp(thickness * strength, 0.0, 8.0) * 0.015;
    vec2 refractedUV = clamp(uv + (normalOffset + rayOffset) * distortionScale, vec2(0.001), vec2(0.999));

    vec3 screenColor = texture(opaqueSceneColor, refractedUV).rgb;
    vec3 envColor = SampleTransmissionFallback(N, V, ior, roughness);
    float envFallback = smoothstep(0.55, 1.0, roughness);
    return mix(screenColor, envColor, envFallback);
}

void main()
{
    CustomMaterialPayload payload = customMaterialDataBuffer.materials[pc.materialIndex];

    vec3 baseColorFactor = payload.values[0].rgb;
    float alphaCoverage = clamp(payload.values[0].a, 0.0, 1.0);
    float roughnessFactor = payload.values[1].x;
    float transmission = clamp(payload.values[1].y, 0.0, 1.0);
    float ior = max(payload.values[1].z, 1.0001);
    float thicknessFactor = max(payload.values[1].w, 0.0);
    vec3 attenuationColor = payload.values[2].rgb;
    float attenuationDistance = payload.values[2].a;
    float normalScale = payload.values[3].x;
    float refractionStrength = payload.values[3].y;
    float reflectionStrength = payload.values[3].z;

    int baseColorIndex = payload.textureIndices.x;
    int normalIndex = payload.textureIndices.y;
    int roughnessIndex = payload.textureIndices.z;
    int thicknessIndex = payload.textureIndices.w;

    vec3 N = BuildNormal(normalize(normalWS), tangentWS, bitangentWS, normalIndex, normalScale);
    vec3 V = normalize(cameraPosition.xyz - positionWS);
    if (dot(N, V) < 0.0) N = -N;

    vec3 baseColor = baseColorFactor * SampleTextureOrWhite(baseColorIndex, fragUV);
    float roughness = clamp(roughnessFactor * SampleTextureOrOne(roughnessIndex, fragUV), 0.045, 1.0);
    float thickness = thicknessFactor * SampleTextureOrOne(thicknessIndex, fragUV);

    vec3 F0 = vec3(DielectricF0FromIor(ior));
    float NoV = clamp(dot(N, V), 0.0, 1.0);
    vec3 F = fresnelSchlickRoughness(NoV, F0, roughness);

    vec3 ambientDiffuse = EvaluateEnvironmentDiffuse(N, V, baseColor, F0, roughness, transmission);
    vec3 reflection = ApplyScreenSpaceReflection(
        EvaluateEnvironmentReflection(positionWS, N, V, F0, roughness), roughness) * reflectionStrength;
    vec3 refracted = SampleScreenTransmission(N, V, ior, roughness, thickness, refractionStrength);
    vec3 transmittance = EvaluateTransmittance(attenuationColor, attenuationDistance, thickness);
    vec3 transmissionColor = refracted * baseColor * transmission * transmittance * refractionStrength;

    vec3 color = ambientDiffuse + reflection + transmissionColor * (vec3(1.0) - F);

    vec3 L = normalize(uDirectionLight.direction.xyz);
    vec3 lightRadiance = uDirectionLight.color.rgb * uDirectionLight.intensity;
    color += EvaluateDirectDiffuse(N, V, L, lightRadiance, baseColor, F0, transmission);
    color += EvaluateDirectSpecular(N, V, L, lightRadiance, F0, roughness) * reflectionStrength;

    outColor = vec4(color, alphaCoverage);
}
