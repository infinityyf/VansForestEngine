#ifndef CLOTH_SURFACE_INCLUDED
#define CLOTH_SURFACE_INCLUDED
#include "../BRDF/ClothBSDF.glsl"
#include "ClothCoverage.glsl"
#ifndef CLOTH_TEXTURE_SET
#define CLOTH_TEXTURE_SET 4
#endif

// Set 4 — cloth-owned textures
layout( set = CLOTH_TEXTURE_SET, binding = 0 ) uniform sampler2D clothAlbedo;
layout( set = CLOTH_TEXTURE_SET, binding = 1 ) uniform sampler2D clothNormal;
layout( set = CLOTH_TEXTURE_SET, binding = 2 ) uniform sampler2D clothRoughness;
layout( set = CLOTH_TEXTURE_SET, binding = 3 ) uniform sampler2D clothAO;
layout( set = CLOTH_TEXTURE_SET, binding = 4 ) uniform sampler2D clothSheenMask;
layout( set = CLOTH_TEXTURE_SET, binding = 5 ) uniform sampler2D clothFiberDirection;
layout( set = CLOTH_TEXTURE_SET, binding = 6 ) uniform sampler2D clothOpacity;

struct ClothSampledSurface
{
    ClothShadingData shading;
    vec3 tangent;
    float ao;
};

ClothSampledSurface SampleClothSurface(vec4 albedoTint, float roughnessScale,
    float aoScale, inout ClothMaterialPayload cloth, vec2 uv, float mipBias,
    vec3 normalWS, vec3 tangentWS, vec3 bitangentWS, bool frontFacing,
    vec3 viewDirection)
{
    float coverage = cloth.flags.x != 0u ? texture(clothOpacity, uv, mipBias).r : 1.0;
    if (!ClothSurfaceVisible(cloth, frontFacing, coverage)) discard;
    vec3  albedo         = max(albedoTint.rgb, vec3(0.0)) * texture(clothAlbedo, uv, mipBias).rgb;
    float roughness = clamp(roughnessScale * texture(clothRoughness, uv, mipBias).r, 0.045, 1.0);
    float ao             = clamp(aoScale * texture(clothAO, uv, mipBias).r, 0.0, 1.0);
    float sheenWeight = clamp(cloth.sheenColorWeight.a * texture(clothSheenMask, uv, mipBias).r, 0.0, 1.0);

    // Two-sided, orthonormal TBN. Keep the original tangent direction so silk
    // anisotropy remains stable when the geometric normal is flipped.
    vec3 geometricNormal = normalize(normalWS);
    if (!frontFacing)
        geometricNormal = -geometricNormal;
    vec3 tangent = tangentWS - geometricNormal * dot(geometricNormal, tangentWS);
    vec3 fallbackB;
    if (dot(tangent, tangent) <= 1e-8) BuildStableClothFrame(geometricNormal, tangent, fallbackB);
    else tangent = normalize(tangent);
    float handedness = dot(cross(normalize(normalWS), tangent), bitangentWS) < 0.0
        ? -1.0 : 1.0;
    vec3 bitangent = normalize(cross(geometricNormal, tangent)) * handedness;

    vec3 normal_sample = texture(clothNormal, uv, mipBias).rgb;
    normal_sample = normal_sample * 2.0 - 1.0;
    mat3 TBN           = mat3(tangent, bitangent, geometricNormal);
    vec3 normal        = normalize(TBN * normal_sample);
    float rotation = cloth.surface.z;
    if (cloth.flags.z != 0u)
    {
        // Cook 后的方向图表示无向轴 cos(2θ),sin(2θ)。
        vec2 axis = texture(clothFiberDirection, uv, mipBias).rg * 2.0 - 1.0;
        // At zero resultant direction, RG8 quantization alone can leave both
        // channels at +/-1/255. Do not turn that residual into a 22.5-degree axis.
        if (dot(axis, axis) > 1e-4) rotation += 0.5 * atan(axis.y, axis.x);
    }
    vec3 fiber = cos(rotation) * tangent + sin(rotation) * bitangent;
    cloth.sheenColorWeight.a = sheenWeight;
    fiber -= normal * dot(normal, fiber);
    if (dot(fiber, fiber) <= 1e-8) BuildStableClothFrame(normal, fiber, fallbackB);
    else fiber = normalize(fiber);
    return ClothSampledSurface(ClothShadingData(albedo, normal, viewDirection, roughness), fiber, ao);
}
#endif
