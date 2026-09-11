#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../../Common/VansDrawSubmission.glsl"
#include "../../BRDF/BRDFData.glsl"

layout(location = 0) in float shadowDepth;
layout(location = 1) in vec2 fragUV;

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

layout(location = 0) out vec4 outPut;

void main()
{
    VansDrawData drawData = VansGetDrawData();
    int materialIndex = nonuniformEXT(drawData.materialIndex);
    float alphaCutoff = materialDataBuffer.materials[materialIndex].padding;
    float baseColorAlpha = texture(globalPBRTextures[materialIndex * 5 + 0], fragUV).a;
    if (baseColorAlpha < alphaCutoff)
        discard;

    outPut = vec4(shadowDepth);
}
