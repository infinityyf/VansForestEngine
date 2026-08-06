#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"
#include "../Common/VertexDeformation.glsl"

layout(location = 0) in f16vec4 position;

layout(location = 0) out vec4 vCurrentClipPos;
layout(location = 1) out vec4 vPreviousClipPos;

layout(push_constant) uniform MotionVectorDrawPushConsts
{
    int materialIndex;
    int objectIndex;
    int passUser0;
    uint vertexFeatureMask;
} materialConst;

void main()
{
    mat4 currentModel = ModelBuffer.transforms[materialConst.objectIndex].ModelMatrix;
    mat4 previousModel = ModelBuffer.transforms[materialConst.objectIndex].PrevModelMatrix;

    vec4 localPosition = vec4(position);
    VansApplyVertexPositionDeformation(localPosition, materialConst.vertexFeatureMask);

    vec4 worldPos = currentModel * localPosition;
    vec4 prevWorldPos = previousModel * localPosition;

    vCurrentClipPos = UnjitteredVPMatrix * worldPos;
    vPreviousClipPos = LastUnjitteredVPMatrix * prevWorldPos;
    gl_Position = VPMatrix * worldPos;
}
