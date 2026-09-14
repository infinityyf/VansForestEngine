#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"
#include "../../Common/MotionVector.glsl"
#include "../TerrainCommon.glsl"
#include "../TerrainDeferredCommon.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in vec4 motionCurrentClip;
layout(location = 3) in vec4 motionPreviousClip;
layout(location = 4) in vec2 inNoiseGradient;

void main()
{
    TerrainWriteDeferred(inUV, inWorldPos, motionCurrentClip, motionPreviousClip, inNoiseGradient);
}
