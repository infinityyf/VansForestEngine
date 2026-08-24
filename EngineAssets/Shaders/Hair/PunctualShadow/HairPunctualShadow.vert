#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/ModelData.glsl"
#include "../../Common/VansDrawSubmission.glsl"
#include "../../Lights/LightsData.glsl"
#include "../../Common/VertexDeformation.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec2 fragUV;

void main()
{
    VansDrawData drawData = VansGetDrawData();
    mat4 modelMatrix = ModelBuffer.transforms[drawData.transformIndex].ModelMatrix;
    mat4 shadowMatrix = uPunctualShadowViews[uint(drawData.passUser0)].worldToShadow;

    vec4 localPosition = position;
    VansApplyVertexPositionDeformation(localPosition, drawData.vertexFeatureMask);
    vec4 clipCoord = shadowMatrix * modelMatrix * localPosition;
    clipCoord.z = clipCoord.z * 0.5 + clipCoord.w * 0.5;
    gl_Position = clipCoord;

    fragUV = uv;
}
