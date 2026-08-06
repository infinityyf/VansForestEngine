#version 450
#extension GL_GOOGLE_include_directive : require

#define LightCBBind 0
#include "../../Common/ModelData.glsl"
#include "../../Lights/LightsData.glsl"
#include "../../Common/VertexDeformation.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out float lightDepth;

layout(push_constant) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int cascadeIndex;
    uint vertexFeatureMask;
} materialConst;

void main()
{
    mat4 modelMatrix = ModelBuffer.transforms[materialConst.objectIndex].ModelMatrix;
    vec4 localPosition = position;
    VansApplyVertexPositionDeformation(localPosition, materialConst.vertexFeatureMask);
    vec4 clipCoord = uDirectionLight.shadowMatrix[0] * modelMatrix * localPosition;
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    lightDepth = clamp(clipCoord.z, 0.0, 1.0);
    fragUV = uv;
}
