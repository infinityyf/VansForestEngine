#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"

// ── Vertex attributes ──────────────────────────────────────────────────────
layout( location = 0 ) in vec3 inPosition;
layout( location = 1 ) in vec3 inNormal;
layout( location = 2 ) in vec2 inUV;

// ── Varyings to fragment shader ────────────────────────────────────────────
layout( location = 0 ) out vec2 frag_uv;
layout( location = 1 ) out vec3 normal_ws;
layout( location = 2 ) out vec3 tangent_ws;
layout( location = 3 ) out vec3 bitangent_ws;
layout( location = 4 ) out vec3 position_world;

// ── Push constants ─────────────────────────────────────────────────────────
layout( push_constant ) uniform GrassDrawPC
{
    int   materialIndex;
    int   objectIndex;
    int   animationEnabled;
    uint  boneCount;
    uint  subBladeCount;
    float grassHeight;
    // P6a: terrain params for VS heightmap sampling
    float terrainSize;
    float terrainMaxHeight;
    float terrainHeightOffset;
    int   terrainEnabled;
    // P1: 子叶片距离 LOD 参数
    float lodMidDist;       // 中距离阈值，超过后子叶片数减半
    float lodFarDist;       // 远距离阈值，超过后子叶片降至最少
} pc;

// ─────────────────────────────────────────────────────────────────────────────
// Set 3: Vegetation Draw SSBOs
// ─────────────────────────────────────────────────────────────────────────────

// Binding 0 — Bone matrices computed by bone-sim (one mat4 per bone per instance)
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
struct GrassInstance
{
    vec3  position;
    float scale;
    float cosR;         // cos(rotation) — precomputed on CPU
    float sinR;         // sin(rotation) — precomputed on CPU
    int   padding0;
    int   padding1;
};

layout(std430, set = 3, binding = 5) readonly buffer InstanceDataBuffer
{
    GrassInstance instances[];
};

// Binding 6 — Terrain heightmap (for sub-blade ground placement in VS)
layout(set = 3, binding = 6) uniform sampler2D terrainHeightmap;

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

// ─────────────────────────────────────────────────────────────────────────────
void main()
{
    // gl_InstanceIndex = a * subBladeCount + s  where a ∈ [0..assignedCount)
    uint subBladeIdx  = gl_InstanceIndex % pc.subBladeCount;
    uint localInstIdx = gl_InstanceIndex / pc.subBladeCount;

    // Remap to global instance index
    uint globalInstIdx = instanceRemap[localInstIdx];

    // Instance data
    GrassInstance inst = instances[globalInstIdx];

    // P0: GPU 剔除检查 — 不可见实例的顶点直接退化为零面积三角形
    if (visibilityFlags[globalInstIdx] == 0u)
    {
        gl_Position = vec4(0.0);
        return;
    }

    // P1: 基于距离的子叶片 LOD — 远距离减少子叶片数，大幅降低 VS/FS 开销
    // 用实例 ID 的 golden-ratio hash 做 ±3m 抱动，避免固定距离处密度突变
    {
        float distXZ = distance(inst.position.xz, cameraPosition.xz);
        float dither = fract(float(globalInstIdx) * 0.6180339887) * 6.0 - 3.0;
        float ditheredDist = distXZ + dither;

        uint maxSub = pc.subBladeCount;
        if (ditheredDist > pc.lodFarDist)
            maxSub = 2u;
        else if (ditheredDist > pc.lodMidDist)
            maxSub = max(3u, pc.subBladeCount / 2u);

        if (subBladeIdx >= maxSub)
        {
            gl_Position = vec4(0.0);
            return;
        }
    }

    // LOD factor (1 = full physics / frozen rest-pose, fades toward 0 at boundary).
    // We do NOT cull based on lod — even lod≈0 means rest-pose, not invisible.
    // The smoothstep produces lod≈0 right at lodFadeDist; culling there would create
    // a camera-tracking invisible ring.  Only skip truly degenerate vertices (lod<0).
    float lod = lodFactors[globalInstIdx];
    if (lod < 0.0)
    {
        gl_Position = vec4(0.0);
        return;
    }

    // P6a: 用共享散布偏移 + 地形采样替代原来的 subBladeRoots SSBO (省 ~320 MB)
    // 子叶片世界位置 = 实例根位置 + 散布偏移 + 地形高度
    vec4 scatterOff = scatterOffsets[subBladeIdx];
    vec3 subRootXZ = inst.position + scatterOff.xyz;
    float subY = inst.position.y; // 默认与主根高度相同
    if (pc.terrainEnabled != 0)
    {
        vec2 subUV = subRootXZ.xz / pc.terrainSize + 0.5;
        subY = texture(terrainHeightmap, subUV).r * pc.terrainMaxHeight + pc.terrainHeightOffset;
    }
    vec3 subRoot = vec3(subRootXZ.x, subY, subRootXZ.z);

    // ── Fetch local-space vertex data from bound mesh ────────────────
    vec3 localPos = inPosition;
    vec3 localNrm = inNormal;
    vec2 uv       = inUV;

    // ── Scale by instance scale ─────────────────────────────────────
    localPos *= inst.scale;

    // ── Rotation around Y by instance rotation ──────────────────────
    // P4: 直接读取预计算的 sin/cos，每顶点省去两次三角函数
    float cosR = inst.cosR;
    float sinR = inst.sinR;
    vec3 rotatedPos = vec3(
        localPos.x * cosR - localPos.z * sinR,
        localPos.y,
        localPos.x * sinR + localPos.z * cosR);
    vec3 rotatedNrm = vec3(
        localNrm.x * cosR - localNrm.z * sinR,
        localNrm.y,
        localNrm.x * sinR + localNrm.z * cosR);

    // ── Dual-bone skinning ──────────────────────────────────────────
    uint globalBoneBase = globalInstIdx * pc.boneCount;
    vec4 bw = boneWeights[gl_VertexIndex]; // per-template-vertex weights

    vec3 skinnedPos = skinPosition(rotatedPos, globalBoneBase, bw);
    vec3 skinnedNrm = skinNormal(rotatedNrm, globalBoneBase, bw);

    // ── Offset by sub-blade root (world-space XZ scatter + terrain Y) ─
    vec3 worldPos = skinnedPos + subRoot;

    // LOD only controls whether Verlet physics ran (compute side).
    // Blade scale never changes — no size-based LOD collapse here,
    // which would create a visible discontinuity ring following the camera.

    // ── Build tangent frame ─────────────────────────────────────────
    vec3 N = normalize(skinnedNrm);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 T = normalize(cross(up, N));
    if (length(cross(up, N)) < 0.001)
        T = vec3(1.0, 0.0, 0.0);
    vec3 B = cross(N, T);

    // ── Output ──────────────────────────────────────────────────────
    gl_Position    = VPMatrix * vec4(worldPos, 1.0);
    frag_uv        = uv;
    normal_ws      = N;
    tangent_ws     = T;
    bitangent_ws   = B;
    position_world = worldPos;
}
