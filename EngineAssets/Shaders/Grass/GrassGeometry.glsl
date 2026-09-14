#ifndef VANS_GRASS_GEOMETRY_GLSL
#define VANS_GRASS_GEOMETRY_GLSL
// 主视图和阴影使用同一实例变换、散布与骨骼形变。
layout(std430, set = 3, binding = 0) readonly buffer BoneMatrixBuffer
{
    mat4 boneMatrices[];
};

// Binding 1 — Static per-vertex bone weights: vec4(boneIdx0, boneIdx1, w0, w1)
layout(std430, set = 3, binding = 1) readonly buffer BoneWeightBuffer
{
    vec4 boneWeights[];
};

// Binding 2 — Instance remap: maps [0..assignedCount) → global instance index
layout(std430, set = 3, binding = 2) readonly buffer InstanceRemapBuffer
{
    uint instanceRemap[];
};

// Binding 3 — P6a: Shared scatter offset UBO (sub-blade XZ offsets, all instances share)
layout(std140, set = 3, binding = 3) uniform ScatterOffsetUBO
{
    vec4 scatterOffsets[32]; // 最多 32 个子叶片
};

// Binding 4 — LOD factors (one float per main instance, written by bone-sim)
layout(std430, set = 3, binding = 4) readonly buffer LodFactorsBuffer
{
    float lodFactors[];
};

// Binding 5 — Per-instance data (position, scale, precomputed rotation sin/cos)
// P4 优化: 预计算旋转的 sin/cos，避免每顶点调用三角函数
#include "GrassInstance.glsl"

layout(std430, set = 3, binding = 5) readonly buffer InstanceDataBuffer
{
    GrassInstance instances[];
};

// Binding 6 — Terrain heightmap (for sub-blade ground placement in VS)

// Binding 7 — P0: Per-instance visibility flags from GPU cull
layout(std430, set = 3, binding = 7) readonly buffer VisibilityBuffer
{
    uint visibilityFlags[];
};

// ── Dual-bone skinning ─────────────────────────────────────────────────────
vec3 skinPosition(vec3 localPos, uint globalBoneBase, vec4 bw)
{
    uint b0 = uint(bw.x);
    uint b1 = uint(bw.y);
    float w0 = bw.z;
    float w1 = bw.w;

    mat4 m0 = boneMatrices[globalBoneBase + b0];
    mat4 m1 = boneMatrices[globalBoneBase + b1];

    vec3 p0 = (m0 * vec4(localPos, 1.0)).xyz;
    vec3 p1 = (m1 * vec4(localPos, 1.0)).xyz;
    return p0 * w0 + p1 * w1;
}

vec3 skinNormal(vec3 localNrm, uint globalBoneBase, vec4 bw)
{
    uint b0 = uint(bw.x);
    uint b1 = uint(bw.y);
    float w0 = bw.z;
    float w1 = bw.w;

    mat3 m0 = mat3(boneMatrices[globalBoneBase + b0]);
    mat3 m1 = mat3(boneMatrices[globalBoneBase + b1]);

    vec3 n0 = m0 * localNrm;
    vec3 n1 = m1 * localNrm;
    return normalize(n0 * w0 + n1 * w1);
}


vec3 grassWorldPosition(vec3 localPosition, uint instanceIndex, uint subBladeIndex, uint boneCount, vec4 weights)
{
    GrassInstance instance = instances[instanceIndex];
    mat3 basis = mat3(instance.modelMatrix);
    return grassRoot(instance) + basis * scatterOffsets[subBladeIndex].xyz +
        skinPosition(basis * localPosition, instanceIndex * boneCount, weights);
}

#endif
