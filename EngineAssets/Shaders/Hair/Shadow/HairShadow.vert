#version 450
#extension GL_GOOGLE_include_directive : require

#define LightCBBind 0
#include "../../Common/ModelData.glsl"
#include "../../Lights/LightsData.glsl"
#include "../../Common/AnimationSkinning.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out float shadowDepth;

layout(push_constant) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int cascadeIndex;
    int animationEnabled;
} materialConst;

void main()
{
    mat4 modelMatrix = ModelBuffer.transforms[materialConst.objectIndex].ModelMatrix;
    vec4 skinnedPosition = position;
    if (materialConst.animationEnabled != 0)
        VansApplyAnimationSkinningPosition(skinnedPosition);
    vec4 clipCoord = uDirectionLight.shadowMatrix[materialConst.cascadeIndex] * modelMatrix * skinnedPosition;
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z;
    fragUV = uv;
}
