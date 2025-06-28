#version 450
#extension GL_GOOGLE_include_directive : require

#define ModelCBBind 0
#define LightCBBind 1

#include "../Common/ModelData.glsl"
#include "../Lights/LightsData.glsl"

layout(push_constant) uniform LightShadowIndex 
{
    int lightIndex;
    int shadowIndex;
};

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec3 normal;

layout( location = 0 ) out float shadowDepth;


void main() 
{
    int pointLightCount = int(uPointLightCount);
    int spotLightCount = int(uSpotLightCount);
    if(lightIndex < pointLightCount)
    {
        mat4x4 shadowMatrix = uPointLights[lightIndex].shadowMatrix[shadowIndex];
        vec4 clipCoord = shadowMatrix * ModelMatrix * position;
        gl_Position = clipCoord;
        shadowDepth = clipCoord.z / clipCoord.w;
    }
    
    if(lightIndex >= pointLightCount)
    {
        int spotLightIndex = lightIndex - pointLightCount;
        mat4x4 shadowMatrix = uSpotLights[spotLightIndex].shadowMatrix;
        vec4 clipCoord = shadowMatrix * ModelMatrix * position;
        gl_Position = clipCoord;
        shadowDepth = clipCoord.z / clipCoord.w;
    }
}