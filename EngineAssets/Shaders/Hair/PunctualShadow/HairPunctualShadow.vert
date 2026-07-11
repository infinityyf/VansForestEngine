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

    if (lightIndex < pointLightCount)
        gl_Position = uPointLights[lightIndex].shadowMatrix[shadowIndex] * modelMatrix * position;
    else if (lightIndex < rectLightStart)
        gl_Position = uSpotLights[lightIndex - pointLightCount].shadowMatrix * modelMatrix * position;
    else
        gl_Position = uRectLights[lightIndex - rectLightStart].shadowMatrix * modelMatrix * position;

    fragUV = uv;
}
