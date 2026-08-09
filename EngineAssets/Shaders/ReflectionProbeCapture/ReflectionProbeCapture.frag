#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : enable

#include "../GI/GIProbeStateData.glsl"

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 worldTangent;
layout(location = 4) in vec3 worldBitangent;
layout(location = 0) out vec4 outColor;

struct MaterialPayload
{
    vec4 albedo;
    float roughness;
    float metallic;
    float ao;
    float padding;
};

struct DirectionLightData
{
    vec4 direction;
    vec4 color;
    float intensity;
    mat4 shadowMatrix[4];
    vec4 cascadeSplits;
    vec4 cascadeTexelSize;
    vec4 cascadeDepthScale;
    vec4 cascadeNormalBias;
    vec4 cascadeFilterRadius; // maximum PCSS footprint in texels
};

struct PointLightData
{
    vec4 position;
    vec4 color;
    float intensity;
    float radius;
    uint shadowMetaIndex;
    float iesProfileIndex;
};

struct SpotLightData
{
    vec4 position;
    vec4 direction;
    vec4 color;
    float intensity;
    float radius;
    float innerConeAngle;
    float outerConeAngle;
    uint shadowMetaIndex;
    float iesProfileIndex;
    float iesIntensityScale;
    float pad0;
};

layout(set = 0, binding = 1, std430) readonly buffer LightsData
{
    uint pointLightCount;
    uint spotLightCount;
    uint shadowAtlasSize;
    uint shadowAtlasCount;
    vec4 softShadowParams;
    DirectionLightData directionLight;
    PointLightData pointLights[64];
    SpotLightData spotLights[64];
};

layout(set = 0, binding = 2, std430) readonly buffer MaterialData
{
    MaterialPayload materials[];
} materialDataBuffer;

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

layout(set = 1, binding = 1) uniform sampler2DArray cascadeShadowMap;
layout(set = 1, binding = 3) uniform samplerCube skyDiffuseEnvironment;
layout(set = 1, binding = 4) uniform sampler2D giIrradianceAtlas;
layout(set = 1, binding = 5) uniform sampler2D giVisibilityAtlas;
layout(set = 1, binding = 6, std430) readonly buffer ProbeStateBuffer
{
    GIProbeState states[];
} giProbeStates;

#define GI_LOAD_PROBE_STATE(regionIndex, probeLinearIndex) giProbeStates.states[probeLinearIndex]
#include "../GI/GIProbeCommon.glsl"

layout(set = 1, binding = 0) uniform CaptureCamera
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 position;
    vec4 giVolumeMin;
    vec4 giVolumeSizeAndBias;
	vec4 giGridDimensions;
} captureCamera;

layout(push_constant) uniform CaptureDraw
{
    mat4 model;
    vec4 albedo;
    vec4 emissive;
    vec4 params;
} drawData;

vec3 SampleIndirectDiffuseRadiance(vec3 worldPosition, vec3 normal)
{
    vec3 samplePosition = worldPosition + normalize(normal) * captureCamera.giVolumeSizeAndBias.w;
    bool insideGIVolume = GI_IsInsideVolume(
        samplePosition,
        captureCamera.giVolumeMin.xyz,
        captureCamera.giVolumeSizeAndBias.xyz);

	vec3 probeLighting = GI_SampleProbeIrradianceAtlasVisible(
		0u, ivec3(captureCamera.giGridDimensions.xyz), giIrradianceAtlas, giVisibilityAtlas,
		worldPosition, normal, captureCamera.giVolumeMin.xyz,
		captureCamera.giVolumeSizeAndBias.xyz, captureCamera.giVolumeSizeAndBias.w, 0.0);

    if (insideGIVolume)
        return probeLighting;

    // sky diffuse cubemap 存的是 irradiance；这里返回 lighting 项，
    // 和 GI probe helper 一样在调用处再乘 baseColor/kD。
    return max(texture(skyDiffuseEnvironment, normal).rgb / PI, vec3(0.0));
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NoH = max(dot(N, H), 0.0);
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-6);
}

float GeometrySchlickGGX(float NoX, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r * 0.125;
    return NoX / max(NoX * (1.0 - k) + k, 1e-6);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
        pow(1.0 - cosTheta, 5.0);
}

float FetchCaptureCascadeDepth(ivec2 texel, int cascade)
{
    ivec2 size = textureSize(cascadeShadowMap, 0).xy;
    texel = clamp(texel, ivec2(0), size - ivec2(1));
    return texelFetch(cascadeShadowMap, ivec3(texel, cascade), 0).r;
}

float CompareCaptureCascadeBilinear(vec2 uv, int cascade, float receiverDepth)
{
    ivec2 size = textureSize(cascadeShadowMap, 0).xy;
    vec2 texelPosition = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(texelPosition));
    vec2 f = fract(texelPosition);
    float c00 = FetchCaptureCascadeDepth(base, cascade) < receiverDepth ? 0.0 : 1.0;
    float c10 = FetchCaptureCascadeDepth(base + ivec2(1, 0), cascade) < receiverDepth ? 0.0 : 1.0;
    float c01 = FetchCaptureCascadeDepth(base + ivec2(0, 1), cascade) < receiverDepth ? 0.0 : 1.0;
    float c11 = FetchCaptureCascadeDepth(base + ivec2(1, 1), cascade) < receiverDepth ? 0.0 : 1.0;
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

vec2 CaptureCascadeDiskSample(int sampleIndex, int sampleCount, bool clumped, int cascade)
{
    float u = (float(sampleIndex) + 0.5) / float(sampleCount);
    float radius = clumped ? u : sqrt(u);
    vec2 d = fibonacciSpiralDirection[sampleIndex] * radius;
    float angle = float(cascade) * 1.61803398875;
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * d.x - s * d.y, s * d.x + c * d.y);
}

float DirectionalShadow(vec3 position, vec3 normal, vec3 lightDirection)
{
    ivec2 size = textureSize(cascadeShadowMap, 0).xy;

    // Capture has no meaningful main-camera linear depth. Select the finest
    // cascade whose world-space projection contains the shaded point.
    for (int cascade = 0; cascade < CASCADE_COUNT; ++cascade)
    {
        vec4 clip = directionLight.shadowMatrix[cascade] * vec4(position, 1.0);
        if (abs(clip.w) < 1e-5) continue;
        vec3 projected = clip.xyz / clip.w;
        vec2 shadowUV = projected.xy * 0.5 + 0.5;
        shadowUV.y = 1.0 - shadowUV.y;
        float rawReceiverDepth = projected.z * 0.5 + 0.5;
        if (any(lessThan(shadowUV, vec2(0.0))) || any(greaterThan(shadowUV, vec2(1.0))) ||
            rawReceiverDepth < 0.0 || rawReceiverDepth > 1.0) continue;

        float ndl = clamp(dot(normalize(normal), normalize(lightDirection)), 0.0, 1.0);
        float slope = min(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.05), 4.0);
        float normalBiasWorld = directionLight.cascadeNormalBias[cascade] * (1.0 + slope * 0.75);
        float minimumBiasWorld = directionLight.cascadeTexelSize[cascade] * 0.25;
        float receiverDepth = rawReceiverDepth
            - max(minimumBiasWorld, normalBiasWorld) * directionLight.cascadeDepthScale[cascade];
        float maxRadiusTexels = max(directionLight.cascadeFilterRadius[cascade], 1.0);
        float depthRange = 1.0 / max(directionLight.cascadeDepthScale[cascade], 1e-6);
        float worldUnitsPerTexel = max(directionLight.cascadeTexelSize[cascade], 1e-6);
        float angularRadius = 0.00464258 * clamp(softShadowParams.y / 0.3, 0.0, 4.0);
        float searchRadiusTexels = clamp(
            max(receiverDepth, 0.0) * depthRange * tan(angularRadius) / worldUnitsPerTexel,
            2.0,
            maxRadiusTexels);
        vec2 edgeTexels = min(shadowUV, vec2(1.0) - shadowUV) * vec2(size);
        if (min(edgeTexels.x, edgeTexels.y) <= searchRadiusTexels + 1.5)
            continue;

        const int blockerSampleCount = 12;
        float blockerDepthSum = 0.0;
        float blockerCount = 0.0;
        vec2 texelSize = 1.0 / vec2(size);
        for (int i = 0; i < blockerSampleCount; ++i)
        {
            vec2 d = CaptureCascadeDiskSample(i, blockerSampleCount, true, cascade);
            ivec2 texel = ivec2(floor((shadowUV + d * texelSize * searchRadiusTexels) * vec2(size)));
            float depth = FetchCaptureCascadeDepth(texel, cascade);
            if (depth < receiverDepth)
            {
                blockerDepthSum += depth;
                blockerCount += 1.0;
            }
        }

        if (blockerCount <= 0.0)
            return 1.0;

        float averageBlockerDepth = blockerDepthSum / blockerCount;
        float separationWorld = max(rawReceiverDepth - averageBlockerDepth, 0.0) * depthRange;
        float filterRadiusTexels = clamp(
            separationWorld * tan(angularRadius) / worldUnitsPerTexel,
            0.75,
            maxRadiusTexels);

        const int filterSampleCount = 16;
        float visibility = 0.0;
        for (int i = 0; i < filterSampleCount; ++i)
        {
            vec2 d = CaptureCascadeDiskSample(i, filterSampleCount, false, cascade);
            visibility += CompareCaptureCascadeBilinear(
                shadowUV + d * texelSize * filterRadiusTexels,
                cascade,
                receiverDepth);
        }
        return visibility / float(filterSampleCount);
    }

    // Outside the main-light shadow coverage: keep the direct light visible.
    return 1.0;
}

void main()
{
    int materialIndex = int(round(drawData.params.w));
    if (drawData.params.x > 0.5)
    {
        vec3 emissive = drawData.emissive.rgb;
        if (materialIndex >= 0)
            emissive *= texture(globalPBRTextures[nonuniformEXT(materialIndex * 5)], fragUV).rgb;
        outColor = vec4(max(emissive, vec3(0.0)), 1.0);
        return;
    }

    vec3 baseColor = max(drawData.albedo.rgb, vec3(0.0));
    float roughness = clamp(drawData.params.y, 0.045, 1.0);
    float metallic = clamp(drawData.params.z, 0.0, 1.0);
    vec3 N = normalize(worldNormal);

    if (materialIndex >= 0)
    {
        int mi = nonuniformEXT(materialIndex);
        MaterialPayload material = materialDataBuffer.materials[mi];
        baseColor = max(material.albedo.rgb *
            texture(globalPBRTextures[nonuniformEXT(mi * 5 + 0)], fragUV).rgb, vec3(0.0));
        metallic = clamp(material.metallic *
            texture(globalPBRTextures[nonuniformEXT(mi * 5 + 2)], fragUV).r, 0.0, 1.0);
        roughness = clamp(material.roughness *
            texture(globalPBRTextures[nonuniformEXT(mi * 5 + 3)], fragUV).r, 0.045, 1.0);

        vec3 tangentNormal = texture(globalPBRTextures[nonuniformEXT(mi * 5 + 1)], fragUV).xyz * 2.0 - 1.0;
        vec3 T = worldTangent - N * dot(worldTangent, N);
        vec3 B = worldBitangent - N * dot(worldBitangent, N);
        float tangentLength2 = dot(T, T);
        float bitangentLength2 = dot(B, B);
        if (tangentLength2 > 1e-6 && bitangentLength2 > 1e-6)
            N = normalize(mat3(T * inversesqrt(tangentLength2),
                B * inversesqrt(bitangentLength2), N) * tangentNormal);
    }

    vec3 V = normalize(captureCamera.position.xyz - worldPosition);
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    vec3 Fambient = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 indirectKd = (vec3(1.0) - Fambient) * (1.0 - metallic);
    vec3 indirect = indirectKd * baseColor * SampleIndirectDiffuseRadiance(worldPosition, N);
    vec3 direct = vec3(0.0);

    vec3 Ld = normalize(directionLight.direction.xyz);
    float NoL = max(dot(N, Ld), 0.0);
    float NoV = max(dot(N, V), 0.0);
    if (NoL > 0.0 && NoV > 0.0)
    {
        vec3 H = normalize(V + Ld);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySchlickGGX(NoV, roughness) * GeometrySchlickGGX(NoL, roughness);
        vec3 specular = D * G * F / max(4.0 * NoV * NoL, 1e-5);
        vec3 diffuse = (vec3(1.0) - F) * (1.0 - metallic) * baseColor / PI;
        vec3 radiance = directionLight.color.rgb * directionLight.intensity;
        direct = (diffuse + specular) * radiance * NoL *
            DirectionalShadow(worldPosition, N, Ld);
    }

    outColor = vec4(max(direct + indirect + drawData.emissive.rgb, vec3(0.0)), 1.0);
}
