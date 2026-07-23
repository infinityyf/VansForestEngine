#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "../../Common/CameraData.glsl"
#define TILE_LIGHT
#include "../../Common/TileLightData.glsl"
#include "../../Common/CustomMaterialData.glsl"
#include "../../Lights/LightsData.glsl"
#include "../../Lighting/RectLightLTC.glsl"

layout(set = 1, binding = 0) uniform sampler2D opaqueSceneColor;
layout(set = 1, binding = 2) uniform sampler2D opaqueDepth;
layout(set = 1, binding = 3) uniform sampler2DArray cascadeShadowMap;
layout(set = 1, binding = 4) uniform sampler2DShadow punctualShadowMap;
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

const int GLASS_REFRACTION_THIN = 0;
const int GLASS_REFRACTION_SCREEN_TRACE = 1;
const int GLASS_REFRACTION_ENVIRONMENT = 2;

float DielectricF0FromIor(float ior)
{
    float f = (ior - 1.0) / max(ior + 1.0, 1e-4);
    return f * f;
}

vec3 SampleTextureOrWhite(int textureIndex, vec2 uv)
{
    if (textureIndex < 0) return vec3(1.0);
    return texture(globalPBRTextures[nonuniformEXT(textureIndex)], uv, MaterialMipBias).rgb;
}

float SampleTextureOrOne(int textureIndex, vec2 uv)
{
    if (textureIndex < 0) return 1.0;
    return texture(globalPBRTextures[nonuniformEXT(textureIndex)], uv, MaterialMipBias).r;
}

vec3 BuildNormal(vec3 N, vec3 T, vec3 B, int normalIndex, float normalScale)
{
    vec3 n = normalize(N);
    vec3 t = normalize(T - n * dot(n, T));
    float handedness = dot(cross(n, t), B) < 0.0 ? -1.0 : 1.0;
    vec3 b = normalize(cross(n, t)) * handedness;
    if (normalIndex < 0) return n;

    vec3 sampleN = texture(globalPBRTextures[nonuniformEXT(normalIndex)], fragUV, MaterialMipBias).xyz * 2.0 - 1.0;
    sampleN.xy *= normalScale;
    return normalize(mat3(t, b, n) * sampleN);
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
    vec2 envBRDF = texture(BRDFLUT, vec2(NoV, 1.0 - roughness)).rg;
    return prefiltered * (F * envBRDF.x + envBRDF.y);
}

vec3 EvaluateDirectionalReflectance(vec3 N, vec3 V, vec3 F0, float roughness)
{
    float NoV = clamp(dot(N, V), 0.0, 1.0);
    vec3 F = fresnelSchlickRoughness(NoV, F0, roughness);
    vec2 envBRDF = texture(BRDFLUT, vec2(NoV, 1.0 - roughness)).rg;
    return clamp(F * envBRDF.x + envBRDF.y, vec3(0.0), vec3(1.0));
}

vec3 SampleTransmissionFallback(vec3 P, vec3 N, vec3 V, float ior, float roughness, out vec3 rayDirection)
{
    rayDirection = refract(-V, N, 1.0 / max(ior, 1.0001));
    if (dot(rayDirection, rayDirection) < 1e-5)
        rayDirection = reflect(-V, N);
    rayDirection = normalize(rayDirection);

    float lod = GetMipLevelFromRoughness(roughness);
    ReflectionProbeSample probe = SampleReflectionProbes(P, N, rayDirection, roughness);
    vec3 sky = textureLod(PreConvSpecularEnvironment, rayDirection, lod).rgb * reflectionProbeLightingParams.z;
    return mix(sky, probe.specular, probe.coverage);
}

float ReconstructLinearDepth(vec2 uv, float deviceDepth)
{
    if (deviceDepth >= 0.99999) return FarPlane;
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    vec4 viewPosition = InverseProjectionMatrix * vec4(ndc, deviceDepth, 1.0);
    viewPosition /= max(abs(viewPosition.w), 1e-6);
    return max(-viewPosition.z, 0.0);
}

float RaySceneDepthDelta(vec3 startSS, vec3 endSS, float t, out vec2 uv)
{
    uv = mix(startSS.xy, endSS.xy, t);
    float invDepth = mix(1.0 / max(startSS.z, 1e-5), 1.0 / max(endSS.z, 1e-5), t);
    float rayDepth = 1.0 / max(invDepth, 1e-5);
    float sceneDepth = ReconstructLinearDepth(uv, textureLod(opaqueDepth, uv, 0.0).r);
    return rayDepth - sceneDepth;
}

bool TraceOpaqueScene(
    vec3 startWS, vec3 directionWS, float maxDistance, float thicknessTolerance,
    out vec2 hitUV, out float hitConfidence)
{
    hitConfidence = 0.0;
    vec3 startSS;
    vec3 endSS;
    if (!HiZ_ProjectToScreenChecked(ViewMatrix, ProjectionMatrix, startWS, startSS) ||
        !HiZ_ProjectToScreenChecked(ViewMatrix, ProjectionMatrix, startWS + directionWS * maxDistance, endSS))
        return false;

    vec2 rayUV = endSS.xy - startSS.xy;
    float pixelSpan = length(rayUV * ScreenParams.xy);
    int stepCount = int(clamp(ceil(pixelSpan / 6.0), 8.0, 48.0));
    float previousDelta = -1.0;
    float previousT = 0.0;

    for (int stepIndex = 1; stepIndex <= stepCount; ++stepIndex)
    {
        float t = float(stepIndex) / float(stepCount);
        vec2 uv = startSS.xy + rayUV * t;
        if (any(lessThanEqual(uv, vec2(0.001))) || any(greaterThanEqual(uv, vec2(0.999))))
            return false;

        vec2 sampledUV;
        float delta = RaySceneDepthDelta(startSS, endSS, t, sampledUV);
        if (delta >= 0.0 && previousDelta < 0.0)
        {
            float lowT = previousT;
            float highT = t;
            for (int refineIndex = 0; refineIndex < 5; ++refineIndex)
            {
                float midT = (lowT + highT) * 0.5;
                vec2 refineUV;
                float refineDelta = RaySceneDepthDelta(startSS, endSS, midT, refineUV);
                if (refineDelta >= 0.0) highT = midT;
                else lowT = midT;
            }

            float hitDelta = RaySceneDepthDelta(startSS, endSS, highT, hitUV);
            float sceneDepth = ReconstructLinearDepth(hitUV, textureLod(opaqueDepth, hitUV, 0.0).r);
            float tolerance = max(thicknessTolerance, sceneDepth * 0.002);
            float edge = min(min(hitUV.x, hitUV.y), min(1.0 - hitUV.x, 1.0 - hitUV.y));
            hitConfidence = smoothstep(0.0, 0.04, edge) *
                (1.0 - smoothstep(tolerance, tolerance * 4.0, abs(hitDelta)));
            return true;
        }
        previousDelta = delta;
        previousT = t;
    }
    return false;
}

float SceneColorLod(float roughness)
{
    return roughness * roughness * float(max(textureQueryLevels(opaqueSceneColor) - 1, 0));
}

vec3 ApplyScreenSpaceReflection(vec3 environmentReflection, vec3 N, vec3 V, float roughness)
{
    if (roughness >= 0.55) return environmentReflection;

    vec3 reflectionDirection = normalize(reflect(-V, N));
    vec2 hitUV;
    float hitConfidence;
    float startBias = max(0.01, length(positionWS - cameraPosition.xyz) * 0.0005);
    float maxDistance = min(FarPlane * 0.8, 80.0);
    if (!TraceOpaqueScene(positionWS + reflectionDirection * startBias, reflectionDirection,
                          maxDistance, 0.12 + roughness * 0.4, hitUV, hitConfidence))
        return environmentReflection;

    vec3 screenReflection = textureLod(opaqueSceneColor, hitUV, SceneColorLod(roughness)).rgb;
    float edge = min(min(hitUV.x, hitUV.y), min(1.0 - hitUV.x, 1.0 - hitUV.y));
    float confidence = hitConfidence * smoothstep(0.0, 0.08, edge) *
        (1.0 - smoothstep(0.35, 0.55, roughness));
    return mix(environmentReflection, screenReflection, confidence);
}

vec3 SampleScreenTransmission(
    vec3 P, vec3 N, vec3 V, float ior, float roughness, float thickness,
    float distortionStrength, int refractionMode, out vec3 rayDirection)
{
    vec3 environment = SampleTransmissionFallback(P, N, V, ior, roughness, rayDirection);
    if (refractionMode == GLASS_REFRACTION_ENVIRONMENT)
        return environment;

    float travelScale = max(distortionStrength, 0.0);
    float surfaceBias = max(0.005, length(P - cameraPosition.xyz) * 0.00025);
    vec3 screenRayDirection = normalize(mix(-V, rayDirection, travelScale));
    vec3 projectedExit;
    float projectedDistance = max(surfaceBias, thickness * max(travelScale, 0.001));
    bool hasScreenSample = HiZ_ProjectToScreenChecked(
        ViewMatrix, ProjectionMatrix, P + screenRayDirection * projectedDistance, projectedExit);
    vec2 sampleUV = projectedExit.xy;
    hasScreenSample = hasScreenSample && all(greaterThan(sampleUV, vec2(0.001))) &&
        all(lessThan(sampleUV, vec2(0.999)));
    vec3 screenColor = hasScreenSample
        ? textureLod(opaqueSceneColor, sampleUV, SceneColorLod(roughness)).rgb
        : environment;

    if (refractionMode == GLASS_REFRACTION_SCREEN_TRACE)
    {
        float maxDistance = min(FarPlane * 0.8, max(20.0, thickness * 128.0));
        vec2 tracedUV;
        float traceConfidence;
        if (TraceOpaqueScene(P + screenRayDirection * surfaceBias, screenRayDirection,
            maxDistance, 0.1 + thickness * 0.25 + roughness * 0.35,
            tracedUV, traceConfidence))
        {
            vec3 tracedColor = textureLod(opaqueSceneColor, tracedUV, SceneColorLod(roughness)).rgb;
            screenColor = mix(screenColor, tracedColor, traceConfidence);
            sampleUV = mix(sampleUV, tracedUV, traceConfidence);
            hasScreenSample = true;
        }
    }

    if (!hasScreenSample) return environment;

    float edge = min(min(sampleUV.x, sampleUV.y), min(1.0 - sampleUV.x, 1.0 - sampleUV.y));
    float screenConfidence = smoothstep(0.0, 0.06, edge) * (1.0 - smoothstep(0.7, 1.0, roughness));
    return mix(environment, screenColor, screenConfidence);
}

void EvaluateMedium(
    vec3 attenuationColor, float attenuationDistance, vec3 scatteringColor,
    float scatteringStrength, float opticalPath, vec3 incidentDirection,
    out vec3 transmittance, out vec3 inScattering)
{
    vec3 sigmaA = vec3(0.0);
    if (attenuationDistance > 0.0)
        sigmaA = -log(clamp(attenuationColor, vec3(1e-4), vec3(1.0))) / attenuationDistance;

    vec3 sigmaS = max(scatteringColor, vec3(0.0)) * max(scatteringStrength, 0.0);
    vec3 sigmaT = sigmaA + sigmaS;
    transmittance = exp(-sigmaT * max(opticalPath, 0.0));

    vec3 singleScatteringAlbedo = sigmaS / max(sigmaT, vec3(1e-5));
    vec3 incident = texture(PreConvDiffuseEnvironment, -incidentDirection).rgb * reflectionProbeLightingParams.z;
    inScattering = incident * (vec3(1.0) - transmittance) * singleScatteringAlbedo;
}

void AccumulatePunctualBRDF(
    BRDFData brdf, vec3 L, vec3 radiance, float visibility, float transmission,
    inout vec3 diffuse, inout vec3 specular)
{
    vec3 localDiffuse = vec3(0.0);
    vec3 localSpecular = vec3(0.0);
    DirectBRDF(brdf, L, localDiffuse, localSpecular);
    diffuse += localDiffuse * radiance * visibility * (1.0 - transmission);
    specular += localSpecular * radiance * visibility;
}

void EvaluateDirectLighting(BRDFData brdf, float transmission, out vec3 diffuse, out vec3 specular)
{
    diffuse = vec3(0.0);
    specular = vec3(0.0);
    float viewDepth = abs((ViewMatrix * vec4(brdf.positionWS, 1.0)).z);
    float directionShadow = SampleCascadeShadow(
        brdf.positionWS, brdf.normal, cascadeShadowMap, viewDepth);
    AccumulatePunctualBRDF(
        brdf, normalize(uDirectionLight.direction.xyz),
        uDirectionLight.color.rgb * uDirectionLight.intensity,
        directionShadow, transmission, diffuse, specular);

    TileLightHeader tileHeader = GetFragTileLightHeader();
    for (uint tileIndex = 0u; tileIndex < tileHeader.pointCount; ++tileIndex)
    {
        uint lightIndex = tileLightIndices[tileHeader.pointOffset + tileIndex];
        PointLightData light = GetPointLight(int(lightIndex));
        vec3 L = light.position.xyz - brdf.positionWS;
        float distanceToLight = length(L);
        if (distanceToLight <= 1e-5 || distanceToLight > light.radius) continue;
        L /= distanceToLight;
        float attenuation = 1.0 - distanceToLight / max(light.radius, 1e-4);
        attenuation *= attenuation;
        uint shadowIndex = light.shadowMetaIndex;
        float visibility = shadowIndex != INVALID_SHADOW_INDEX
            ? SamplePointShadowMapBRDF(brdf.positionWS, brdf.normal, L, punctualShadowMap, int(lightIndex))
            : 1.0;
        AccumulatePunctualBRDF(brdf, L, light.color.rgb * light.intensity * attenuation,
            visibility, transmission, diffuse, specular);
    }

    for (uint tileIndex = 0u; tileIndex < tileHeader.spotCount; ++tileIndex)
    {
        uint lightIndex = tileLightIndices[tileHeader.spotOffset + tileIndex];
        SpotLightData light = GetSpotLight(int(lightIndex));
        vec3 L = light.position.xyz - brdf.positionWS;
        float distanceToLight = length(L);
        if (distanceToLight <= 1e-5 || distanceToLight > light.radius) continue;
        L /= distanceToLight;
        float coneAngle = dot(normalize(light.direction.xyz), L);
        float outerCone = cos(light.outerConeAngle);
        if (coneAngle < outerCone) continue;
        float innerCone = cos(light.innerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerCone) / max(innerCone - outerCone, 1e-4), 0.0, 1.0);
        float attenuation = 1.0 - distanceToLight / max(light.radius, 1e-4);
        attenuation *= attenuation;
        uint shadowIndex = light.shadowMetaIndex;
        float visibility = shadowIndex != INVALID_SHADOW_INDEX
            ? SampleSpotShadowMapBRDF(brdf.positionWS, brdf.normal, L, punctualShadowMap, int(lightIndex))
            : 1.0;
        AccumulatePunctualBRDF(brdf, L,
            light.color.rgb * light.intensity * attenuation * coneAttenuation,
            visibility, transmission, diffuse, specular);
    }

    for (uint tileIndex = 0u; tileIndex < tileHeader.rectCount; ++tileIndex)
    {
        uint lightIndex = tileLightIndices[tileHeader.rectOffset + tileIndex];
        RectLightData light = GetRectLight(int(lightIndex));
        vec3 rectDiffuse = vec3(0.0);
        vec3 rectSpecular = vec3(0.0);
        EvaluateRectLightLTC(light, brdf.normal, brdf.viewDirection, brdf.positionWS,
            brdf.roughness, brdf.albedo * (1.0 - transmission), brdf.fresnel0,
            rectDiffuse, rectSpecular);
        uint shadowIndex = light.shadowMetaIndex;
        float visibility = 1.0;
        if (shadowIndex != INVALID_SHADOW_INDEX)
        {
            vec3 L = normalize(light.position_halfW.xyz - brdf.positionWS);
            visibility = SampleRectShadowMapBRDF(
                brdf.positionWS, brdf.normal, L, punctualShadowMap, int(lightIndex));
        }
        diffuse += rectDiffuse * visibility;
        specular += rectSpecular * visibility;
    }
}

void main()
{
    CustomMaterialPayload payload = customMaterialDataBuffer.materials[pc.materialIndex];

    vec3 baseColorFactor = max(payload.values[0].rgb, vec3(0.0));
    float alphaCoverage = clamp(payload.values[0].a, 0.0, 1.0);
    float roughnessFactor = payload.values[1].x;
    float transmission = clamp(payload.values[1].y, 0.0, 1.0);
    float ior = max(payload.values[1].z, 1.0001);
    float thicknessFactor = max(payload.values[1].w, 0.0);
    vec3 attenuationColor = payload.values[2].rgb;
    float attenuationDistance = max(payload.values[2].a, 0.0);
    float normalScale = payload.values[3].x;
    float refractionStrength = clamp(payload.values[3].y, 0.0, 1.0);
    float reflectionStrength = clamp(payload.values[3].z, 0.0, 1.0);
    int refractionMode = clamp(int(round(payload.values[3].w)), GLASS_REFRACTION_THIN, GLASS_REFRACTION_ENVIRONMENT);
    vec3 scatteringColor = max(payload.values[4].rgb, vec3(0.0));
    float scatteringStrength = max(payload.values[4].a, 0.0);

    int baseColorIndex = payload.textureIndices.x;
    int normalIndex = payload.textureIndices.y;
    int roughnessIndex = payload.textureIndices.z;
    int thicknessIndex = payload.textureIndices.w;
    int reflectionIndex = int(round(payload.values[5].x));

    vec3 N = BuildNormal(normalWS, tangentWS, bitangentWS, normalIndex, normalScale);
    vec3 V = normalize(cameraPosition.xyz - positionWS);
    if (dot(N, V) < 0.0) N = -N;

    vec3 baseColor = baseColorFactor * SampleTextureOrWhite(baseColorIndex, fragUV);
    float roughness = clamp(roughnessFactor * SampleTextureOrOne(roughnessIndex, fragUV), 0.045, 1.0);
    float thickness = thicknessFactor * SampleTextureOrOne(thicknessIndex, fragUV);
    vec3 reflectionMask = clamp(SampleTextureOrWhite(reflectionIndex, fragUV), vec3(0.0), vec3(1.0));
    vec3 localReflectionStrength = reflectionStrength * reflectionMask;

    vec3 F0 = vec3(DielectricF0FromIor(ior));
    vec3 reflectedFraction = EvaluateDirectionalReflectance(N, V, F0, roughness) * localReflectionStrength;
    vec3 transmittedFraction = (vec3(1.0) - reflectedFraction) * transmission;
    vec3 diffuseFraction = (vec3(1.0) - reflectedFraction) * (1.0 - transmission);

    vec3 reflection = EvaluateEnvironmentReflection(positionWS, N, V, F0, roughness);
    reflection = ApplyScreenSpaceReflection(reflection, N, V, roughness);
    reflection *= localReflectionStrength;

    vec3 refractedDirection;
    vec3 refracted = SampleScreenTransmission(
        positionWS, N, V, ior, roughness, thickness,
        refractionStrength, refractionMode, refractedDirection);

    float NoT = max(abs(dot(N, refractedDirection)), 0.1);
    float opticalPath = thickness / NoT;
    vec3 mediumTransmittance;
    vec3 inScattering;
    EvaluateMedium(attenuationColor, attenuationDistance, scatteringColor,
        scatteringStrength, opticalPath, refractedDirection,
        mediumTransmittance, inScattering);

    vec3 environmentDiffuse = texture(PreConvDiffuseEnvironment, N).rgb * baseColor * diffuseFraction;
    vec3 transmissionColor = (refracted * baseColor * mediumTransmittance + inScattering) * transmittedFraction;

    BRDFData brdf;
    brdf.albedo = baseColor;
    brdf.normal = N;
    brdf.roughness = roughness;
    brdf.metallic = 0.0;
    brdf.ao = 1.0;
    brdf.fresnel0 = F0;
    brdf.viewDirection = V;
    brdf.positionWS = positionWS;
    brdf.indirectDiffuse = vec3(0.0);
    brdf.indirectSpecular = vec4(0.0);

    vec3 directDiffuse;
    vec3 directSpecular;
    EvaluateDirectLighting(brdf, transmission, directDiffuse, directSpecular);
    directSpecular *= localReflectionStrength;

    vec3 color = environmentDiffuse + reflection + transmissionColor + directDiffuse + directSpecular;
    outColor = vec4(color * alphaCoverage, alphaCoverage);
}
