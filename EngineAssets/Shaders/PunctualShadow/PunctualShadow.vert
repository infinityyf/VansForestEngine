#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/ModelData.glsl"
#include "../Lights/LightsData.glsl"

layout(push_constant) uniform LightShadowIndex 
{
    int lightIndex;
    int shadowIndex;
    int materialIndex;
    int objectIndex;
};

layout( location = 0 ) in f16vec4 position;
layout( location = 1 ) in f16vec3 normal;


void main() 
{
    mat4 ModelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;

    int pointLightCount = int(uPointLightCount);
    int spotLightCount  = int(uSpotLightCount);
    int rectLightStart  = pointLightCount + spotLightCount;

    if (lightIndex < pointLightCount)
    {
        // Point light: lightIndex = point index, shadowIndex = cubemap face
        mat4x4 shadowMatrix = uPointLights[lightIndex].shadowMatrix[shadowIndex];
        vec4 clipCoord = shadowMatrix * ModelMatrix * position;
        gl_Position = clipCoord;
    }
    else if (lightIndex < rectLightStart)
    {
        // Spot light
        int spotLightIndex = lightIndex - pointLightCount;
        mat4x4 shadowMatrix = uSpotLights[spotLightIndex].shadowMatrix;
        vec4 clipCoord = shadowMatrix * ModelMatrix * position;
        gl_Position = clipCoord;
    }
    else
    {
        // Rect light (Phase 3): packed after spot lights in atlas order
        int rectLightIndex = lightIndex - rectLightStart;
        mat4x4 shadowMatrix = uRectLights[rectLightIndex].shadowMatrix;
        vec4 clipCoord = shadowMatrix * ModelMatrix * position;
        gl_Position = clipCoord;
    }
}