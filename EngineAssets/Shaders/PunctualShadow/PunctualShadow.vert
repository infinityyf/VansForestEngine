#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../Common/ModelData.glsl"
#include "../Lights/LightsData.glsl"

layout(push_constant) uniform LightShadowIndex 
{
    int lightIndex;
    int shadowIndex;
    int materialIndex;
    int objectIndex;
    int animationEnabled;
};

layout( location = 0 ) in f16vec4 position;
layout( location = 1 ) in f16vec3 normal;

// ── 骨骼蒙皮数据 (Set 3) ─────────────────────────────────────────────────
layout(std430, set = 3, binding = 0) readonly buffer BoneIDBuffer
{
    ivec4 boneIDs[];
} BoneIDData;

layout(std430, set = 3, binding = 1) readonly buffer BoneMatrixBuffer
{
    mat4 boneMatrices[];
} BoneBuffer;

layout(std430, set = 3, binding = 2) readonly buffer BoneWeightBuffer
{
    vec4 weights[];
} WeightBuffer;

// 骨骼蒙皮辅助函数：仅变换位置（shadow pass 不需要法线）
void applySkinning(inout vec4 pos)
{
    ivec4 ids = BoneIDData.boneIDs[gl_VertexIndex];
    vec4  wts = WeightBuffer.weights[gl_VertexIndex];

    mat4 skinMatrix = mat4(0.0);
    float totalWeight = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        if (ids[i] >= 0)
        {
            skinMatrix  += wts[i] * BoneBuffer.boneMatrices[ids[i]];
            totalWeight += wts[i];
        }
    }

    if (totalWeight < 0.0001)
        return;

    pos = skinMatrix * pos;
}

void main() 
{
    mat4 ModelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;

    vec4 pos = position;
    if (animationEnabled != 0)
    {
        applySkinning(pos);
    }

    int pointLightCount = int(uPointLightCount);
    int spotLightCount  = int(uSpotLightCount);
    int rectLightStart  = pointLightCount + spotLightCount;

    if (lightIndex < pointLightCount)
    {
        // 点光源：lightIndex = 点光源下标，shadowIndex = 立方体面下标
        mat4x4 shadowMatrix = uPointLights[lightIndex].shadowMatrix[shadowIndex];
        vec4 clipCoord = shadowMatrix * ModelMatrix * pos;
        gl_Position = clipCoord;
    }
    else if (lightIndex < rectLightStart)
    {
        // 聚光灯
        int spotLightIndex = lightIndex - pointLightCount;
        mat4x4 shadowMatrix = uSpotLights[spotLightIndex].shadowMatrix;
        vec4 clipCoord = shadowMatrix * ModelMatrix * pos;
        gl_Position = clipCoord;
    }
    else
    {
        // 面光源 (Rect light)
        int rectLightIndex = lightIndex - rectLightStart;
        mat4x4 shadowMatrix = uRectLights[rectLightIndex].shadowMatrix;
        vec4 clipCoord = shadowMatrix * ModelMatrix * pos;
        gl_Position = clipCoord;
    }
}