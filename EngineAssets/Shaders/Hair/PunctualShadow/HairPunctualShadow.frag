#version 450
#extension GL_GOOGLE_include_directive : require
#include "../HairMaterial.glsl"
layout(location=0) in vec2 fragUV;
void main() { if(!HairCastsShadow(fragUV)) discard; }
