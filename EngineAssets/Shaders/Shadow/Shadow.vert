#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/ModelData.glsl"
#include "../Lights/LightsData.glsl"

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec3 normal;

layout( location = 0 ) out float shadowDepth;


void main() 
{
    vec4 clipCoord = GetDirectionLight(0).shadowMatrix * ModelMatrix * position;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z / clipCoord.w;
}