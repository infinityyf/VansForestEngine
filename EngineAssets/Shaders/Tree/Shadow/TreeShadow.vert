#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Lights/LightsData.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 normal;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out float shadowDepth;
layout(location = 1) out vec2 frag_uv;

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

layout(push_constant) uniform TreeShadowPC
{
    int materialIndex;
    int objectIndex;
    uint visibleOffset;
    int cascadeIndex;
} pc;

void main()
{
    uint visibleIndex = pc.visibleOffset + gl_InstanceIndex;
    uint treeIndex = visibleIndices[visibleIndex];
    mat4 model = instances[treeIndex].modelMatrix;

    vec4 clipCoord = uDirectionLight.shadowMatrix[pc.cascadeIndex] * model * position;
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z;
    frag_uv = uv;
}
