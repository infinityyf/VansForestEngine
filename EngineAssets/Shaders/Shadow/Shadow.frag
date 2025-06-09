#version 450
#extension GL_GOOGLE_include_directive : require

layout( location = 0 ) in float shadowDepth;
layout( location = 0 ) out vec4 outPut;
void main() 
{ 
    outPut = vec4(shadowDepth);
}