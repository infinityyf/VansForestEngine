#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/ModelData.glsl"

// ── No vertex attributes — all data fetched from SSBOs ─────────────────────

// ── Varyings to fragment shader ────────────────────────────────────────────
layout( location = 0 ) out vec2 frag_uv;
layout( location = 1 ) out vec3 normal_ws;
layout( location = 2 ) out vec3 tangent_ws;
layout( location = 3 ) out vec3 bitangent_ws;
layout( location = 4 ) out vec3 position_world;

// ── Push constants ─────────────────────────────────────────────────────────
layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

// ── Set 3: Vegetation draw SSBOs (skinned positions + normals + instances + template) ─
layout(std430, set = 3, binding = 0) readonly buffer SkinnedPositionBuffer
{
    vec4 skinnedPositions[];
};

layout(std430, set = 3, binding = 1) readonly buffer SkinnedNormalBuffer
{
    vec4 skinnedNormals[];
};

struct GrassInstance
{
    vec3  position;
    float scale;
    float rotation;
    int   padding0;
    int   padding1;
    int   padding2;
};

layout(std430, set = 3, binding = 2) readonly buffer InstanceBuffer
{
    GrassInstance instances[];
};

// Template mesh vertex (matches CPU-side GrassVertex struct)
struct GrassTemplateVertex
{
    vec4 position;  // xyz = local position, w = 0
    vec4 normal;    // xyz = local normal,   w = 0
    vec2 uv;
    vec2 padding;
};

layout(std430, set = 3, binding = 3) readonly buffer TemplateMeshBuffer
{
    GrassTemplateVertex templateVertices[];
};

void main() 
{
    // Derive vertex count from the template mesh SSBO length (no hardcoding needed)
    uint vertexCount = uint(templateVertices.length());

    // Compute which instance and which template vertex this invocation is
    uint globalVertexIdx = gl_InstanceIndex * vertexCount + gl_VertexIndex;

    // Read skinned world-space position and normal
    vec3 worldPos = skinnedPositions[globalVertexIdx].xyz;
    vec3 worldNrm = normalize(skinnedNormals[globalVertexIdx].xyz);

    // Build tangent frame from normal (grass blades face viewer roughly)
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 T = normalize(cross(up, worldNrm));
    if (length(cross(up, worldNrm)) < 0.001)
        T = vec3(1.0, 0.0, 0.0);
    vec3 B = cross(worldNrm, T);

    // Read UV from template mesh SSBO (per-template-vertex, not per-instance)
    vec2 templateUV = templateVertices[gl_VertexIndex].uv;

    gl_Position    = VPMatrix * vec4(worldPos, 1.0);
    frag_uv        = templateUV;
    normal_ws      = worldNrm;
    tangent_ws     = T;
    bitangent_ws   = B;
    position_world = worldPos;
}
