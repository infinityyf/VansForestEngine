#version 460
#extension GL_EXT_ray_tracing : require
layout(location=0) rayPayloadInEXT float hitDistance;
void main() { hitDistance=-1.0; }
