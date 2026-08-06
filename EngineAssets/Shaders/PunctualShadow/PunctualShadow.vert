#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../Common/ModelData.glsl"
#include "../Common/VertexDeformation.glsl"
#include "../Lights/LightsData.glsl"

layout(push_constant) uniform LightShadowIndex
{
    int shadowViewIndex;
    int unusedShadowFaceIndex;
    int materialIndex;
    int objectIndex;
    uint vertexFeatureMask;
};

layout(location = 0) in f16vec4 position;
layout(location = 1) in f16vec3 normal;

void main()
{
    mat4 modelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;

    vec4 localPosition = vec4(position);
    VansApplyVertexPositionDeformation(localPosition, vertexFeatureMask);

    vec4 clipCoord = uPunctualShadowViews[uint(shadowViewIndex)].worldToShadow * modelMatrix * localPosition;
    clipCoord.z = clipCoord.z * 0.5 + clipCoord.w * 0.5;
    gl_Position = clipCoord;
}
