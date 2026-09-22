#version 450
#extension GL_GOOGLE_include_directive : require
#include "../Common/CameraData.glsl"
#include "../Common/ModelData.glsl"
#include "../Common/VansDrawSubmission.glsl"

layout(location=0) in vec3 position;
layout(location=1) in vec4 originDepth;
layout(location=2) in vec3 edge1;
layout(location=3) in vec3 edge2;
layout(location=4) in vec4 uv01;
layout(location=5) in vec2 uv2;
layout(location=0) flat out vec4 roadOriginDepth;
layout(location=1) flat out vec3 roadEdge1;
layout(location=2) flat out vec3 roadEdge2;
layout(location=3) flat out vec4 roadUV01;
layout(location=4) flat out vec2 roadUV2;

void main()
{
    VansDrawData drawData=VansGetDrawData();
    mat4 model=ModelBuffer.transforms[drawData.transformIndex].ModelMatrix;
    roadOriginDepth=originDepth;
    roadEdge1=edge1;roadEdge2=edge2;
    roadUV01=uv01;roadUV2=uv2;
    gl_Position=VPMatrix*model*vec4(position,1.0);
}
