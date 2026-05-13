#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"

// ---------------------------------------------------------------------------
// MotionVector vertex shader
//
// Descriptor sets (same layout as shadow pass):
//   Set 0 — Global  (CameraData UBO: VPMatrix, LastVPMatrix, …)
//   Set 1 — EmptyPass (unused placeholder)
//   Set 2 — Object  (Transform SSBO: ModelMatrix per object)
//   Set 3 — Animation (Bone IDs / Bone Matrices / Bone Weights)
//
// 计算当前帧和上一帧的裁剪空间坐标，供片元着色器输出屏幕空间运动向量。
//
// 蒙皮支持：对动画节点应用骨骼蒙皮后再变换至世界空间。
// 当前帧和上一帧均使用当前帧骨骼矩阵（捕获相机 + 对象 Transform 运动），
// 骨骼动画帧间差异将在引入上一帧骨骼矩阵 SSBO 后再行支持。
// ---------------------------------------------------------------------------

layout( location = 0 ) in f16vec4 position;

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
    int cascadeIndex;       // 未使用，与 Shadow pass push constant 布局对齐
    int animationEnabled;
} materialConst;

// Clip-space positions passed to the fragment shader.
// Full vec4 so the fragment shader can do the perspective divide.
layout( location = 0 ) out vec4 vCurrentClipPos;
layout( location = 1 ) out vec4 vPreviousClipPos;

// 骨骼蒙皮辅助函数：仅变换位置
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

    mat4 currentModel = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 prevModel    = ModelBuffer.transforms[objectIndex].PrevModelMatrix;

    vec4 localPos = position;
    if (materialConst.animationEnabled != 0)
    {
        // 对本地坐标应用蒙皮，再分别乘以当前帧和上一帧的 Model 矩阵
        applySkinning(localPos);
    }

    // 当前帧世界坐标
    vec4 worldPos = currentModel * localPos;

    // 当前帧裁剪坐标（不含抖动，保证静止时 motionVec 精确为零）
    vec4 currentClip = UnjitteredVPMatrix * worldPos;

    // 上一帧世界坐标（使用上一帧 Model 矩阵捕获对象 Transform 运动；
    // 骨骼动画帧间运动在获得上一帧骨骼矩阵后再支持）
    vec4 prevWorldPos = prevModel * localPos;

    // 上一帧裁剪坐标
    vec4 previousClip = LastUnjitteredVPMatrix * prevWorldPos;

    // gl_Position 仍用 jittered VPMatrix 保持 TAA 抖动对 GBuffer 的作用
    gl_Position      = VPMatrix * worldPos;
    vCurrentClipPos  = currentClip;
    vPreviousClipPos = previousClip;
}
