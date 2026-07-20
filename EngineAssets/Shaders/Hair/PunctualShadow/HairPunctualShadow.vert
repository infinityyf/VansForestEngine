#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/ModelData.glsl"
#include "../../Lights/LightsData.glsl"

layout(push_constant) uniform LightShadowIndex
{
    int lightIndex;
    int shadowIndex;
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

    mat4 shadowMatrix;
    if (lightIndex < pointLightCount)
        shadowMatrix = uPointLights[lightIndex].shadowMatrix[shadowIndex];
    else if (lightIndex < rectLightStart)
        shadowMatrix = uSpotLights[lightIndex - pointLightCount].shadowMatrix;
    else
        shadowMatrix = uRectLights[lightIndex - rectLightStart].shadowMatrix;

    vec4 clipCoord = shadowMatrix * modelMatrix * position;
    clipCoord.z = clipCoord.z * 0.5 + clipCoord.w * 0.5;
    gl_Position = clipCoord;

    fragUV = uv;
}
