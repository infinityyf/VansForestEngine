#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"

layout( location = 0 ) in vec4 position;
layout( location = 1 ) in vec2 uv;
layout( location = 2 ) in vec3 normal;

layout( location = 0 ) out vec2 frag_uv;
layout( location = 1 ) out vec3 normal_out;
layout( location = 2 ) out vec3 position_world;


void main() 
{
    gl_Position = ProjectionMatrix * ViewMatrix * ModelMatrix * position;
    normal_out = normal;
    frag_uv= uv;
    position_world = (ModelMatrix * position).xyz;
}