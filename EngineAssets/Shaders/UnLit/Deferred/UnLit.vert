#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 uv;
layout( location = 2 ) in vec3 normal;
layout( location = 3 ) in vec3 tangent;
layout( location = 4 ) in vec3 bitangent;

layout( location = 0 ) out vec2 frag_uv;
layout( location = 1 ) out vec3 normal_ws;
layout( location = 2 ) out vec3 tangent_ws;
layout( location = 3 ) out vec3 bitangent_ws;
layout( location = 4 ) out vec3 position_world;

layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;   // 0 = static, 1 = skinned
} materialConst;

// ── Per-Vertex Bone ID SSBO (set 3, binding 0) ─────────────────────────
// Per-submesh bone IDs for animated nodes, shared dummy for static nodes.
layout(std430, set = 3, binding = 0) readonly buffer BoneIDBuffer
{
    ivec4 boneIDs[];
} BoneIDData;

// ── Bone Matrices SSBO (set 3, binding 1) ───────────────────────────────
// Real bone data for animated nodes, shared dummy (64 bytes) for static nodes.
layout(std430, set = 3, binding = 1) readonly buffer BoneMatrixBuffer
{
    mat4 boneMatrices[];
} BoneBuffer;

// ── Per-Vertex Bone Weight SSBO (set 3, binding 2) ─────────────────────
// Per-submesh bone weights for animated nodes, shared dummy for static nodes.
layout(std430, set = 3, binding = 2) readonly buffer BoneWeightBuffer
{
    vec4 weights[];
} WeightBuffer;

// ── Skinning helper ───────────────────────────────────────────────────
void applySkinning(inout vec4 pos, inout vec3 norm, inout vec3 tan, inout vec3 bitan)
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

    // Vertices with no bone influence (all IDs == -1) keep their bind-pose position.
    if (totalWeight < 0.0001)
        return;

    pos    = skinMatrix * pos;
    mat3 skinMat3 = mat3(skinMatrix);
    norm   = normalize(skinMat3 * norm);
    tan    = normalize(skinMat3 * tan);
    bitan  = normalize(skinMat3 * bitan);
}

void main() 
{
    int objectIndex = materialConst.objectIndex;
    mat4 ModelMatrix  = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 NormalMatrix = ModelBuffer.transforms[objectIndex].NormalMatrix;

    vec4 pos  = position;
    vec3 n    = normal;
    vec3 t    = tangent;
    vec3 bt   = bitangent;

    // Apply skeletal skinning if this is an animated node
    if (materialConst.animationEnabled != 0)
    {
        applySkinning(pos, n, t, bt);
    }

    gl_Position  = VPMatrix * ModelMatrix * pos;
    mat3 normalMatrix = mat3(NormalMatrix);
    normal_ws    = normalize(normalMatrix * n);
    tangent_ws   = normalize(normalMatrix * t);
    bitangent_ws = normalize(normalMatrix * bt);

    frag_uv        = uv;
    position_world = (ModelMatrix * pos).xyz;
}