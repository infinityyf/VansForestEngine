#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#define LightCBBind 0
#include "../Common/ModelData.glsl"
#include "../Lights/LightsData.glsl"

layout( location = 0 ) in f16vec4 position;
layout( location = 1 ) in f16vec3 normal;

layout( location = 0 ) out float shadowDepth;

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

layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int cascadeIndex;
    int animationEnabled;
} materialConst;

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
    int objectIndex = materialConst.objectIndex;
    mat4 ModelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;

    vec4 pos = position;
    if (materialConst.animationEnabled != 0)
    {
        applySkinning(pos);
    }

    vec4 clipCoord = uDirectionLight.shadowMatrix[materialConst.cascadeIndex] * ModelMatrix * pos;
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z;
}