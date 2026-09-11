#version 460
#extension GL_EXT_ray_tracing : require
layout(location=0) rayPayloadInEXT uint visible;
void main() { visible=1u; }
