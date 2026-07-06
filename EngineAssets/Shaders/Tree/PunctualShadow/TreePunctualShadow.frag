#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/Common.glsl"

layout(location = 0) out vec4 outPut;

void main()
{
    outPut = vec4(gl_FragCoord.z);
}
