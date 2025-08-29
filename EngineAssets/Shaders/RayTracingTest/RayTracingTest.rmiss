#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "../Common/Common.glsl"
layout(location = 0) rayPayloadInEXT RayTracePayload prd;

void main()
{
    prd.positionHit = vec4(gl_WorldRayDirectionEXT,0);
    prd.normalHit = vec4(0,0,0,0);
}