#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../../Common/CameraData.glsl"
#include "../../BRDF/BRDFData.glsl"

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec3 normal_ws;
layout(location = 2) in vec3 tangent_ws;
layout(location = 3) in vec3 bitangent_ws;
layout(location = 4) in vec3 position_world;

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

layout(push_constant) uniform TreeDrawPC
{
    int materialIndex;
    int objectIndex;
    uint visibleOffset;
    uint alphaTestEnabled;
} pc;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outGBuffer0;
layout(location = 2) out vec4 outGBuffer1;
layout(location = 3) out vec4 outGBuffer2;

void main()
{
    int materialIndex = nonuniformEXT(pc.materialIndex);
    MaterialPayload materialData = materialDataBuffer.materials[materialIndex];

    vec3 albedoParam = materialData.albedo.rgb;
    float roughnessParam = materialData.roughness;
    float metallicParam = materialData.metallic;
    float aoParam = materialData.ao;

    vec4 albedoSample = texture(globalPBRTextures[materialIndex * 5 + 0], frag_uv, MaterialMipBias);
    if (pc.alphaTestEnabled != 0u && albedoSample.a < 0.5)
        discard;

    vec3 albedo = albedoParam * albedoSample.rgb;
    vec3 normalSample = texture(globalPBRTextures[materialIndex * 5 + 1], frag_uv, MaterialMipBias).rgb * 2.0 - 1.0;
    float metallic = metallicParam * texture(globalPBRTextures[materialIndex * 5 + 2], frag_uv, MaterialMipBias).r;
    float roughness = roughnessParam * texture(globalPBRTextures[materialIndex * 5 + 3], frag_uv, MaterialMipBias).r;
    float ao = aoParam * texture(globalPBRTextures[materialIndex * 5 + 4], frag_uv, MaterialMipBias).r;

    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 n = normalize(TBN * normalSample);
    if (!gl_FrontFacing)
        n = -n;

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;
    float leafTranslucency = (pc.alphaTestEnabled != 0u) ? 0.55 : 0.0;
    outNormal = vec4(n, leafTranslucency);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(metallic, ao, float(MATERIAL_ID_TREE), 1.0);
    outGBuffer2 = vec4(position_world, -linearDepth);
}
