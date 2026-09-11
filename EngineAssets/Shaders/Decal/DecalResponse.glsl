#ifndef VANS_DECAL_RESPONSE_INCLUDED
#define VANS_DECAL_RESPONSE_INCLUDED
#include "../Common/Common.glsl"

int DecodeDecalReceiverMaterialID(float encodedID)
{
    if (isnan(encodedID) || isinf(encodedID)) return MATERIAL_ID_SKIP;
    // Skin 将 thinness 打包到小数部分；与 Deferred 共用解码规则。
    return int(round(encodedID));
}

// RGB 分别表示 Color / Normal / Roughness；新增材质默认不接收。
vec3 DecalReceiverResponse(int materialID)
{
    switch (materialID)
    {
    case MATERIAL_ID_PBR:
    case MATERIAL_ID_TERRAIN:
    case MATERIAL_ID_SKIN:
    case MATERIAL_ID_CLOTH:
    case MATERIAL_ID_SUBSURFACE:
        return vec3(1.0);
    case MATERIAL_ID_GRASS:
    case MATERIAL_ID_TREE:
        return vec3(1.0, 0.0, 0.0);
    default:
        return vec3(0.0);
    }
}

vec3 DecalSafeNormal(vec3 value, vec3 fallback)
{
    if (any(isnan(value)) || any(isinf(value))) return fallback;
    float lengthSquared = dot(value, value);
    return lengthSquared > 1e-8 && !isinf(lengthSquared) && !isnan(lengthSquared)
        ? value * inversesqrt(lengthSquared) : fallback;
}

vec3 DecalAttributeCoverage(vec3 textureAlpha, vec3 mask, float opacity, vec3 weights, vec3 response)
{
    return clamp(textureAlpha * mask * opacity * weights * response, 0.0, 1.0);
}

void ApplyDecalModifiers(int materialID, vec4 colorModifier, vec4 normalModifier,
    vec4 roughnessModifier, inout vec3 albedo, inout vec3 normal, inout float roughness)
{
    vec3 response = DecalReceiverResponse(materialID);
    albedo = albedo * (1.0 - colorModifier.a * response.x) + colorModifier.rgb * response.x;
    normal = DecalSafeNormal(normal * (1.0 - normalModifier.a * response.y)
        + normalModifier.rgb * response.y, normal);
    roughness = roughness * (1.0 - roughnessModifier.a * response.z) + roughnessModifier.r * response.z;
}
#endif
