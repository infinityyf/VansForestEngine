#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"
#include "../Common/VansDrawSubmission.glsl"
layout(location = 0) in vec4 position;
void main()
{
    VansDrawData drawData = VansGetDrawData();
    gl_Position = VPMatrix * ModelBuffer.transforms[drawData.transformIndex].ModelMatrix * position;
}