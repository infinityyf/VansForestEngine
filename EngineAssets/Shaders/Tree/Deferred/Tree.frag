#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../../Common/CameraData.glsl"
#include "../../BRDF/BRDFData.glsl"
#include "../../BRDF/TreeLeafData.glsl"

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
    bool isLeaf = pc.alphaTestEnabled != 0u;
    TreeLeafMaterialPayload leafPayload = GetTreeLeafMaterialPayload(materialIndex);

    vec4 albedoSample = texture(globalPBRTextures[materialIndex * 5 + 0], frag_uv, MaterialMipBias);
    float alphaClip = clamp(leafPayload.scattering.w, 0.0, 1.0);
    if (isLeaf && albedoSample.a < alphaClip)
        discard;

    vec3 albedo = albedoParam * albedoSample.rgb;
    vec3 normalSample = texture(globalPBRTextures[materialIndex * 5 + 1], frag_uv, MaterialMipBias).rgb * 2.0 - 1.0;
    float metallic = metallicParam * texture(globalPBRTextures[materialIndex * 5 + 2], frag_uv, MaterialMipBias).r;

    vec4 roughnessSample = texture(globalPBRTextures[materialIndex * 5 + 3], frag_uv, MaterialMipBias);
    vec4 aoSample = texture(globalPBRTextures[materialIndex * 5 + 4], frag_uv, MaterialMipBias);
    bool packedMaskMap = roughnessSample.r < 0.02 && aoSample.r < 0.02 &&
        max(roughnessSample.g, aoSample.g) > 0.02;
    float roughnessTex = packedMaskMap ? (1.0 - roughnessSample.a) : roughnessSample.r;
    float aoTex = packedMaskMap ? aoSample.g : aoSample.r;
    float roughness = roughnessParam * roughnessTex;
    float ao = aoParam * aoTex;

    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 n = normalize(TBN * normalSample);
    if (!gl_FrontFacing)
        n = -n;

    float translucencyMask = aoSample.g > 0.001 ? aoSample.g : 1.0;
    float leafTranslucency = isLeaf ? clamp(translucencyMask * leafPayload.subsurfaceColorAndStrength.a, 0.0, 1.0) : 0.0;

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;
    outNormal = vec4(n, leafTranslucency);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(isLeaf ? 0.0 : metallic, ao, float(MATERIAL_ID_TREE), float(materialIndex));
    outGBuffer2 = vec4(position_world, -linearDepth);
}
