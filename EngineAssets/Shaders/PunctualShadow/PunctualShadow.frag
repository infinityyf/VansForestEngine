#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/Common.glsl"
layout( location = 0 ) out vec4 outPut;
void main() 
{ 
    // Use the actual rasterized depth for punctual shadows.
    // Interpolating clip.z / clip.w from the vertex shader breaks perspective shadow maps.
    outPut = vec4(gl_FragCoord.z);
    // float esmDepth = exp(ESM_C * shadowDepth);
    // outPut = vec4(esmDepth, 0, 0, 1);
}