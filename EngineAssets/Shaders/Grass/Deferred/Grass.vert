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

// Binding 3 — Sub-blade root positions (one vec4 per sub-blade, written by bone-sim)
layout(std430, set = 3, binding = 3) readonly buffer SubBladeRootsBuffer
{
    vec4 subBladeRoots[];
};

// Binding 4 — LOD factors (one float per main instance, written by bone-sim)
layout(std430, set = 3, binding = 4) readonly buffer LodFactorsBuffer
{
    float lodFactors[];
};

// Binding 5 — Per-instance data (position, scale, rotation)
struct GrassInstance
{
    vec3  position;
    float scale;
    float rotation;
    int   padding0;
    int   padding1;
    int   padding2;
};

layout(std430, set = 3, binding = 5) readonly buffer InstanceDataBuffer
{
    GrassInstance instances[];
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

    // Sub-blade root position (terrain-snapped Y from bone-sim)
    vec4 subRoot = subBladeRoots[globalInstIdx * pc.subBladeCount + subBladeIdx];

    // ── Fetch local-space vertex data from bound mesh ────────────────
    vec3 localPos = inPosition;
    vec3 localNrm = inNormal;
    vec2 uv       = inUV;

    // ── Scale by instance scale ─────────────────────────────────────
    localPos *= inst.scale;

    // ── Rotation around Y by instance rotation ──────────────────────
    float cosR = cos(inst.rotation);
    float sinR = sin(inst.rotation);
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
    vec3 worldPos = skinnedPos + subRoot.xyz;

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
