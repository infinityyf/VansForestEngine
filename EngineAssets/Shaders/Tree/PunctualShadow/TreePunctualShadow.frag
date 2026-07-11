#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../../Common/Common.glsl"

layout(location = 0) in vec2 frag_uv;

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

layout(push_constant) uniform TreePunctualShadowPC
{
    int lightIndex;
    int shadowIndex;
    int materialIndex;
    int objectIndex;
    uint visibleOffset;
    uint alphaTestEnabled;
} pc;

layout(location = 0) out vec4 outPut;

void main()
{
    int materialIndex = nonuniformEXT(pc.materialIndex);
    if (pc.alphaTestEnabled != 0u && texture(globalPBRTextures[materialIndex * 5 + 0], frag_uv).a < 0.5)
        discard;

    outPut = vec4(gl_FragCoord.z);
}
