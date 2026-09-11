#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../../Common/VansDrawSubmission.glsl"
#include "../../BRDF/BRDFData.glsl"

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

void main()
{
    VansDrawData drawData = VansGetDrawData();
    int materialIndex = nonuniformEXT(drawData.materialIndex);
    float alphaCutoff = materialDataBuffer.materials[materialIndex].padding;
    float baseColorAlpha = texture(globalPBRTextures[materialIndex * 5 + 0], fragUV).a;
    if (baseColorAlpha < alphaCutoff)
        discard;
}
