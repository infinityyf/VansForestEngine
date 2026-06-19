#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 worldTangent;
layout(location = 4) in vec3 worldBitangent;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;
const int CASCADE_COUNT = 4;

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
};

struct PointLightData
{
    vec4 position;
    vec4 color;
    float intensity;
    float radius;
    float shadowIndex;
    float iesProfileIndex;
    mat4 shadowMatrix[6];
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
    mat4 shadowMatrix;
    float shadowIndex;
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
layout(set = 1, binding = 4) uniform sampler3D giSHR;
layout(set = 1, binding = 5) uniform sampler3D giSHG;
layout(set = 1, binding = 6) uniform sampler3D giSHB;

layout(set = 1, binding = 0) uniform CaptureCamera
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 position;
    vec4 giVolumeMin;
    vec4 giVolumeSizeAndBias;
} captureCamera;

layout(push_constant) uniform CaptureDraw
{
    mat4 model;
    vec4 albedo;
    vec4 emissive;
    vec4 params;
} drawData;

bool IsInsideGIVolume(vec3 worldPosition)
{
    vec3 volumeMax = captureCamera.giVolumeMin.xyz + captureCamera.giVolumeSizeAndBias.xyz;
    return all(greaterThanEqual(worldPosition, captureCamera.giVolumeMin.xyz)) &&
        all(lessThanEqual(worldPosition, volumeMax));
}

vec3 SampleIndirectDiffuseRadiance(vec3 worldPosition, vec3 normal)
{
    if (IsInsideGIVolume(worldPosition))
    {
        vec3 biasedPosition = worldPosition + normal * captureCamera.giVolumeSizeAndBias.w;
        vec3 uvw = clamp((biasedPosition - captureCamera.giVolumeMin.xyz) /
            captureCamera.giVolumeSizeAndBias.xyz, vec3(0.0), vec3(1.0));
        // The GI textures store radiance SH coefficients per color channel.
        // SH0 reconstructs constant incident radiance as C0 * Y00.
        vec3 sh0 = vec3(texture(giSHR, uvw).x, texture(giSHG, uvw).x,
            texture(giSHB, uvw).x) * 0.282095;
        return max(sh0, vec3(0.0));
    }

    // The sky diffuse cubemap stores irradiance. Lambertian outgoing radiance
    // is irradiance * albedo / PI, so convert it before applying baseColor.
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

float DirectionalShadow(vec3 position, vec3 normal, vec3 lightDirection)
{
    float bias = max(0.0005 * (1.0 - dot(normal, lightDirection)), 0.0001);
    vec2 texelSize = 1.0 / vec2(textureSize(cascadeShadowMap, 0).xy);

    // Capture has no meaningful main-camera linear depth. Select the finest
    // cascade whose world-space projection contains the shaded point.
    for (int cascade = 0; cascade < CASCADE_COUNT; ++cascade)
    {
        vec4 clip = directionLight.shadowMatrix[cascade] * vec4(position, 1.0);
        if (abs(clip.w) < 1e-5) continue;
        vec3 projected = clip.xyz / clip.w;
        vec2 shadowUV = projected.xy * 0.5 + 0.5;
        shadowUV.y = 1.0 - shadowUV.y;
        float receiverDepth = projected.z * 0.5 + 0.5;
        if (any(lessThan(shadowUV, vec2(0.0))) || any(greaterThan(shadowUV, vec2(1.0))) ||
            receiverDepth < 0.0 || receiverDepth > 1.0) continue;

        float visibility = 0.0;
        for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
        {
            float depth = texture(cascadeShadowMap,
                vec3(clamp(shadowUV + vec2(x, y) * texelSize, vec2(0.0), vec2(1.0)),
                    float(cascade))).r;
            visibility += receiverDepth - bias <= depth ? 1.0 : 0.0;
        }
        return visibility / 9.0;
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
