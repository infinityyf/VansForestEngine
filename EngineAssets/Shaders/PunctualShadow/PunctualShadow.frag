#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/Common.glsl"
layout( location = 0 ) in float shadowDepth;
layout( location = 0 ) out vec4 outPut;
void main() 
{ 
    outPut = vec4(shadowDepth);
    // float esmDepth = exp(ESM_C * shadowDepth);
    // outPut = vec4(esmDepth, 0, 0, 1);
}