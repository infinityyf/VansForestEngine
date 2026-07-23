#ifndef CLOTH_DATA_INCLUDED
#define CLOTH_DATA_INCLUDED

#include "../Common/Common.glsl"

const int CLOTH_MODEL_FUZZ = 0;
const int CLOTH_MODEL_SILK = 1;
const int CLOTH_MODEL_THIN = 2;
const uint CLOTH_FLAG_ALBEDO_SHEEN_TINT = 1u;

struct ClothMaterialPayload
{
    vec4 sheenColorWeight;
    vec4 transmissionColorStrength;
    vec4 controls;
};

layout(set = 0, binding = 16, std430) readonly buffer ClothMaterialData
{
    ClothMaterialPayload materials[];
} clothMaterialDataBuffer;

ClothMaterialPayload DefaultClothMaterialPayload()
{
    ClothMaterialPayload payload;
    payload.sheenColorWeight = vec4(1.0, 1.0, 1.0, 0.5);
    payload.transmissionColorStrength = vec4(1.0, 1.0, 1.0, 0.0);
    payload.controls = vec4(float(CLOTH_MODEL_FUZZ), 0.0, 1.0,
                            float(CLOTH_FLAG_ALBEDO_SHEEN_TINT));
    return payload;
}

ClothMaterialPayload GetClothMaterialPayload(int materialIndex)
{
    if (materialIndex < 0 || materialIndex >= clothMaterialDataBuffer.materials.length())
        return DefaultClothMaterialPayload();
    return clothMaterialDataBuffer.materials[materialIndex];
}

int ClothModel(ClothMaterialPayload payload)
{
    return clamp(int(round(payload.controls.x)), CLOTH_MODEL_FUZZ, CLOTH_MODEL_THIN);
}

uint ClothFlags(ClothMaterialPayload payload)
{
    return uint(max(round(payload.controls.w), 0.0));
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

void DecodeClothTangentFrame(vec3 normal, float encodedAngle,
                             out vec3 tangent, out vec3 bitangent)
{
    vec3 N = normalize(normal);
    vec3 frameT;
    vec3 frameB;
    BuildStableClothFrame(N, frameT, frameB);
    float angle = clamp(encodedAngle, -1.0, 1.0) * PI;
    tangent = normalize(frameT * cos(angle) + frameB * sin(angle));
    bitangent = normalize(cross(N, tangent));
}

#endif
