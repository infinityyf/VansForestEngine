#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in float shadowDepth;
layout(location = 1) in vec2 frag_uv;

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

struct TreeShadowMaterialPayload
{
    vec4 albedo;
    float roughness;
    float metallic;
    float ao;
    float padding;
};

layout(set = 0, binding = 2, std430) readonly buffer TreeShadowMaterialData
{
    TreeShadowMaterialPayload materials[];
} materialDataBuffer;

layout(push_constant) uniform TreeShadowPC
{
    int materialIndex;
    int objectIndex;
    uint visibleOffset;
    int cascadeIndex;
    uint alphaTestEnabled;
} pc;

layout(location = 0) out vec4 outPut;

void main()
{
    int materialIndex = nonuniformEXT(pc.materialIndex);
    float alphaClip = materialDataBuffer.materials[materialIndex].padding > 0.0
        ? clamp(materialDataBuffer.materials[materialIndex].padding, 0.0, 1.0)
        : 0.5;
    if (pc.alphaTestEnabled != 0u && texture(globalPBRTextures[materialIndex * 5 + 0], frag_uv).a < alphaClip)
        discard;

    outPut = vec4(shadowDepth);
}
