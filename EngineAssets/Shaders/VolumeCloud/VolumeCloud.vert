#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../Common/CameraData.glsl"
layout( location = 0 ) in f16vec4 position;
layout( location = 0 ) out vec3 direction;

void main() 
{
    vec3 positionWS = (position).xyz;
    gl_Position = (ProjectionMatrix * ViewMatrix * vec4( positionWS, 0.0 )).xyzz;
    direction = position.xyz;
}