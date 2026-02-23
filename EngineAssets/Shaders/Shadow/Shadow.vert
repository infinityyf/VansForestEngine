#version 450
#extension GL_GOOGLE_include_directive : require

#define ModelCBBind 0
#define LightCBBind 1

#include "../Common/ModelData.glsl"
#include "../Lights/LightsData.glsl"

layout( location = 0 ) in f16vec4 position;
layout( location = 1 ) in f16vec3 normal;

layout( location = 0 ) out float shadowDepth;

layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
} materialConst;

void main() 
{
    int objectIndex = materialConst.objectIndex;
    mat4 ModelMatrix = ModelBuffer.transforms[objectIndex].ModelMatrix;
    
    vec4 clipCoord = uDirectionLight.shadowMatrix * ModelMatrix * position;
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z;
}