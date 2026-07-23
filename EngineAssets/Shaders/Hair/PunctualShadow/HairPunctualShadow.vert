#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/ModelData.glsl"
#include "../../Lights/LightsData.glsl"
#include "../../Common/AnimationSkinning.glsl"

layout(push_constant) uniform LightShadowIndex
{
    int lightIndex;
    int shadowFaceIndex;
    int materialIndex;
    int objectIndex;
    int animationEnabled;
};

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec2 fragUV;

void main()
{
    mat4 modelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;
    int pointLightCount = int(uPointLightCount);
    int spotLightCount = int(uSpotLightCount);
    int rectLightStart = pointLightCount + spotLightCount;

    uint metaIndex;
    if (lightIndex < pointLightCount)
        metaIndex = uPointLights[lightIndex].shadowMetaIndex;
    else if (lightIndex < rectLightStart)
        metaIndex = uSpotLights[lightIndex - pointLightCount].shadowMetaIndex;
    else
        metaIndex = uRectLights[lightIndex - rectLightStart].shadowMetaIndex;

    PunctualShadowData shadow = uPunctualShadows[metaIndex];
    mat4 shadowMatrix = uPunctualShadowViews[shadow.firstView + uint(shadowFaceIndex)].worldToShadow;

    vec4 skinnedPosition = position;
    if (animationEnabled != 0)
        VansApplyAnimationSkinningPosition(skinnedPosition);
    vec4 clipCoord = shadowMatrix * modelMatrix * skinnedPosition;
    clipCoord.z = clipCoord.z * 0.5 + clipCoord.w * 0.5;
    gl_Position = clipCoord;

    fragUV = uv;
}
