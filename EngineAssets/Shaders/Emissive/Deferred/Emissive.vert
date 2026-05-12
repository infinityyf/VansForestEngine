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
layout(std430, set = 3, binding = 0) readonly buffer BoneIDBuffer
{
    ivec4 boneIDs[];
} BoneIDData;

// ── Bone Matrices SSBO (set 3, binding 1) ───────────────────────────────
layout(std430, set = 3, binding = 1) readonly buffer BoneMatrixBuffer
{
    mat4 boneMatrices[];
} BoneBuffer;

// ── Per-Vertex Bone Weight SSBO (set 3, binding 2) ─────────────────────
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

    if (totalWeight < 0.0001)
        return;

    pos = skinMatrix * pos;
    mat3 skinMat3 = mat3(skinMatrix);
    // 使用长度检测代替直接 normalize，防止零向量（UV退化切线等情况）产生 NaN
    vec3 sn = skinMat3 * norm;
    vec3 st = skinMat3 * tan;
    vec3 sb = skinMat3 * bitan;
    float snLen = dot(sn, sn);
    float stLen = dot(st, st);
    float sbLen = dot(sb, sb);
    norm  = snLen > 1e-8 ? sn * inversesqrt(snLen) : norm;
    tan   = stLen > 1e-8 ? st * inversesqrt(stLen) : tan;
    bitan = sbLen > 1e-8 ? sb * inversesqrt(sbLen) : bitan;
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

    if (materialConst.animationEnabled != 0)
    {
        applySkinning(pos, n, t, bt);
    }

    gl_Position  = VPMatrix * ModelMatrix * pos;
    mat3 normalMatrix = mat3(NormalMatrix);
    // 防御性 normalize：当输入为零向量时提供合理的回退值，避免 NaN 写入 GBuffer
    vec3 n_ws  = normalMatrix * n;
    vec3 t_ws  = normalMatrix * t;
    vec3 bt_ws = normalMatrix * bt;
    float nl = dot(n_ws,  n_ws);
    float tl = dot(t_ws,  t_ws);
    float bl = dot(bt_ws, bt_ws);
    normal_ws    = nl > 1e-8 ? n_ws  * inversesqrt(nl) : vec3(0.0, 0.0, 1.0);
    tangent_ws   = tl > 1e-8 ? t_ws  * inversesqrt(tl) : vec3(1.0, 0.0, 0.0);
    bitangent_ws = bl > 1e-8 ? bt_ws * inversesqrt(bl) : cross(normal_ws, tangent_ws);

    frag_uv        = uv;
    position_world = (ModelMatrix * pos).xyz;
}
