#ifndef CLOTH_DATA_INCLUDED
#define CLOTH_DATA_INCLUDED

#include "../Common/Common.glsl"

// 与 VansClothGPUParam 的 std430 布局一致；没有版本或旧分型回退。
struct ClothMaterialPayload
{
    vec4 sheenColorWeight;
    vec4 transmittanceRoughness;
    vec4 surface;
    uvec4 flags;
};

layout(set = 0, binding = 16, std430) readonly buffer ClothMaterialData
{
    ClothMaterialPayload materials[];
} clothMaterialDataBuffer;

ClothMaterialPayload GetClothMaterialPayload(int materialIndex)
{
    // 准入阶段负责索引校验，不把错误索引伪装成有效材质。
    return clothMaterialDataBuffer.materials[materialIndex];
}

void BuildStableClothFrame(vec3 normal, out vec3 frameT, out vec3 frameB)
{
    vec3 N = normalize(normal);
    vec3 axis = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    frameT = normalize(cross(axis, N));
    frameB = normalize(cross(N, frameT));
}

float EncodeClothTangentAngle(vec3 normal, vec3 tangent)
{
    vec3 N = normalize(normal);
    vec3 T = tangent - N * dot(N, tangent);
    float tangentLength2 = dot(T, T);
    if (tangentLength2 <= 1e-8)
        return 0.0;
    T *= inversesqrt(tangentLength2);

    vec3 frameT;
    vec3 frameB;
    BuildStableClothFrame(N, frameT, frameB);
    return clamp(atan(dot(T, frameB), dot(T, frameT)) / PI, -1.0, 1.0);
}

void DecodeClothTangentFrame(vec3 encodedNormal, float encodedAngle, vec3 shadingNormal,
                             out vec3 tangent, out vec3 bitangent)
{
    // The stored angle belongs to the GBuffer normal's frame. A decal can
    // change the shading normal without rotating the authored world fiber.
    // Decode that original axis first, then project onto the new tangent plane.
    vec3 sourceN = normalize(encodedNormal);
    vec3 N = normalize(shadingNormal);
    vec3 frameT;
    vec3 frameB;
    BuildStableClothFrame(sourceN, frameT, frameB);
    float angle = clamp(encodedAngle, -1.0, 1.0) * PI;
    tangent = frameT * cos(angle) + frameB * sin(angle);
    // The double cross stays perpendicular when a quantized source axis is
    // almost parallel to N; subtractive projection then normalization can
    // amplify the residual dot product in that near-degenerate case.
    tangent = cross(cross(N, tangent), N);
    if (dot(tangent, tangent) <= 1e-8)
        BuildStableClothFrame(N, tangent, bitangent);
    else
        tangent = normalize(tangent);
    bitangent = normalize(cross(N, tangent));
}

#endif
