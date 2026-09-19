#version 450
#extension GL_GOOGLE_include_directive : require

#define LightCBBind 0
#include "../Common/ModelData.glsl"
#include "../Common/VansDrawSubmission.glsl"
#include "../Common/VertexDeformation.glsl"
#include "../Lights/LightsData.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out float shadowDepth;

void main()
{
    VansDrawData drawData = VansGetDrawData();
    mat4 modelMatrix = ModelBuffer.transforms[drawData.transformIndex].ModelMatrix;

    vec4 localPosition = vec4(position);
    VansApplyVertexPositionDeformation(localPosition, drawData.vertexFeatureMask);

    vec4 clipCoord = uDirectionLight.shadowMatrix[drawData.passUser0] * modelMatrix * localPosition;
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z;
}
