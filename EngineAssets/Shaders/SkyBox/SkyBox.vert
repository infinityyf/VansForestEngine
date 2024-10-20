#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/CameraData.glsl"


layout( location = 0 ) in vec4 position;
layout( location = 0 ) out vec3 direction;

void main() 
{
    vec3 positionWS = (position).xyz;
    gl_Position = (ProjectionMatrix * vec4( positionWS, 0.0 )).xyzz;
    direction = position.xyz;
}