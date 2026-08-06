#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/ModelData.glsl"
#include "../../Lights/LightsData.glsl"
#include "../../Common/VertexDeformation.glsl"

layout(push_constant) uniform LightShadowIndex
{
    int shadowViewIndex;
    int unusedShadowFaceIndex;
    int materialIndex;
    int objectIndex;
    uint vertexFeatureMask;
};

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec2 fragUV;

void main()
{
    mat4 modelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;
    mat4 shadowMatrix = uPunctualShadowViews[uint(shadowViewIndex)].worldToShadow;

    vec4 localPosition = position;
    VansApplyVertexPositionDeformation(localPosition, vertexFeatureMask);
    vec4 clipCoord = shadowMatrix * modelMatrix * localPosition;
    clipCoord.z = clipCoord.z * 0.5 + clipCoord.w * 0.5;
    gl_Position = clipCoord;

    fragUV = uv;
}
