#version 450
#extension GL_GOOGLE_include_directive : require

#include "../Common/MotionVector.glsl"

layout(location = 0) in vec4 vCurrentClipPos;
layout(location = 1) in vec4 vPreviousClipPos;

layout(location = 0) out vec2 outMotionVector;

void main()
{
    outMotionVector = VansMotionVectorFromClip(vCurrentClipPos, vPreviousClipPos);
}
