#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "../Common/Common.glsl"
layout(location=0) rayPayloadInEXT RayTracePayload prd;
void main() { prd.hitDistance=-1.0; }
