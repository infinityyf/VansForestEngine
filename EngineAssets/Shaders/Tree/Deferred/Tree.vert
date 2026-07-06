#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec3 normal_ws;
layout(location = 2) out vec3 tangent_ws;
layout(location = 3) out vec3 bitangent_ws;
layout(location = 4) out vec3 position_world;

struct TreeInstanceGPU
{
    mat4 modelMatrix;
    vec4 boundsSphere;
    uint speciesIndex;
    uint regionIndex;
    uint randomSeed;
    uint flags;
};

layout(std430, set = 3, binding = 0) readonly buffer TreeInstances
{
    TreeInstanceGPU instances[];
};

layout(std430, set = 3, binding = 1) readonly buffer TreeVisibleIndices
{
    uint visibleIndices[];
};

layout(push_constant) uniform TreeDrawPC
{
    int materialIndex;
    int objectIndex;
    uint visibleOffset;
    uint padding;
} pc;

void main()
{
    uint visibleIndex = pc.visibleOffset + gl_InstanceIndex;
    uint treeIndex = visibleIndices[visibleIndex];
    mat4 model = instances[treeIndex].modelMatrix;

    vec4 worldPos = model * position;
    mat3 normalMatrix = transpose(inverse(mat3(model)));

    vec3 n = normalMatrix * normal;
    vec3 t = normalMatrix * tangent;
    vec3 b = normalMatrix * bitangent;
    normal_ws = dot(n, n) > 1e-8 ? normalize(n) : vec3(0.0, 1.0, 0.0);
    tangent_ws = dot(t, t) > 1e-8 ? normalize(t) : vec3(1.0, 0.0, 0.0);
    bitangent_ws = dot(b, b) > 1e-8 ? normalize(b) : normalize(cross(normal_ws, tangent_ws));

    frag_uv = uv;
    position_world = worldPos.xyz;
    gl_Position = VPMatrix * worldPos;
}
