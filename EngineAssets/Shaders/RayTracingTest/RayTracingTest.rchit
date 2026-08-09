#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

#include "../Common/Common.glsl"

struct Vertex
{
    f16vec3 position;
    f16vec2 uv;
    f16vec3 normal;
    f16vec3 tangent;
    f16vec3 bitangent;
};

layout(location = 0) rayPayloadInEXT RayTracePayload prd;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 3, std430, scalar) buffer VertexBuffers
{
    Vertex vertices[];
} vertexBuffers[];

layout(set = 0, binding = 4, std430) buffer IndexBuffers
{
    uint indices[];
} indexBuffers[];

layout(set = 0, binding = 5, std430) buffer InstanceDataBuffer
{
    uint instances[];
} instanceData;

layout(set = 0, binding = 7, std430) buffer InstanceToTextureIndexBuffer
{
    uint indexs[];
} textureIndexData;

layout(set = 0, binding = 10, std430) readonly buffer InstanceGIEmissionBuffer
{
    vec4 emissionScale[];
} instanceGIEmissionData;

layout(set = 0, binding = 50) uniform sampler2D PBRTextures[];

#define ALBEDO_INDEX 0
#define NORMAL_INDEX 1
#define METALLIC_INDEX 2
#define ROUGHNESS_INDEX 3
#define AO_INDEX 4
#define GI_TEXTURE_INDEX_MASK 0x3FFFFFFFu
#define GI_PURE_EMISSIVE_FLAG 0x40000000u
#define GI_PBR_EMISSIVE_FLAG 0x80000000u

void main()
{
    uint instanceID = gl_InstanceID;
    uint primitiveID = gl_PrimitiveID;

    uint modelIndex = instanceData.instances[instanceID];

    uint indexBase = primitiveID * 3;
    uint i0 = indexBuffers[modelIndex].indices[indexBase + 0];
    uint i1 = indexBuffers[modelIndex].indices[indexBase + 1];
    uint i2 = indexBuffers[modelIndex].indices[indexBase + 2];

    Vertex v0 = vertexBuffers[modelIndex].vertices[i0];
    Vertex v1 = vertexBuffers[modelIndex].vertices[i1];
    Vertex v2 = vertexBuffers[modelIndex].vertices[i2];

    vec3 position = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    vec3 normal =
        vec3(v0.normal) * barycentrics.x +
        vec3(v1.normal) * barycentrics.y +
        vec3(v2.normal) * barycentrics.z;
    mat3 normalMat = transpose(mat3(gl_WorldToObjectEXT));
    vec3 worldNormal = normalize(normalMat * normal);
    bool frontFace = gl_HitKindEXT != gl_HitKindBackFacingTriangleEXT;
    if (!frontFace)
        worldNormal = -worldNormal;

    prd.positionHit = vec4(position, 1.0);
    // xyz is the shading normal used by hit shading; w preserves the
    // geometric facing classification for relocation/state updates.
    prd.normalHit = vec4(worldNormal, frontFace ? 1.0 : -1.0);

    // texture/material payloads are emitted one per TLAS instance.  modelIndex
    // identifies a shared BLAS and is therefore wrong whenever a mesh has more
    // than one material instance.
    uint packedTextureIndex = textureIndexData.indexs[instanceID];
    uint textureIndex = packedTextureIndex & GI_TEXTURE_INDEX_MASK;
    bool pureEmissive = (packedTextureIndex & GI_PURE_EMISSIVE_FLAG) != 0u;
    bool pbrEmissive = (packedTextureIndex & GI_PBR_EMISSIVE_FLAG) != 0u;
    vec2 uv =
        vec2(v0.uv) * barycentrics.x +
        vec2(v1.uv) * barycentrics.y +
        vec2(v2.uv) * barycentrics.z;

    // 非 PBR 材质在 CPU 收集阶段写入 0xFFFFFFFF。这里给中性材质兜底，
    // 避免越界访问 bindless texture array 导致 GPU device lost。
    prd.emissiveRadiance = vec4(0.0);
    if (packedTextureIndex == 0xFFFFFFFFu || (textureIndex + AO_INDEX) >= 2048u)
    {
        prd.albedoRoughness = vec4(0.5, 0.5, 0.5, 1.0);
        return;
    }

    vec4 albedo = texture(PBRTextures[nonuniformEXT(textureIndex + ALBEDO_INDEX)], uv);
    float metallic = clamp(texture(PBRTextures[nonuniformEXT(textureIndex + METALLIC_INDEX)], uv).r, 0.0, 1.0);
    float roughness = texture(PBRTextures[nonuniformEXT(textureIndex + ROUGHNESS_INDEX)], uv).r;
    prd.albedoRoughness = vec4(pureEmissive ? vec3(0.0) : albedo.rgb * (1.0 - metallic), roughness);
    if (pureEmissive)
    {
        prd.emissiveRadiance = vec4(
            texture(PBRTextures[nonuniformEXT(textureIndex)], uv).rgb * instanceGIEmissionData.emissionScale[instanceID].rgb, 1.0);
    }
    else if (pbrEmissive)
    {
        prd.emissiveRadiance = vec4(
            texture(PBRTextures[nonuniformEXT(textureIndex + AO_INDEX)], uv).rgb * instanceGIEmissionData.emissionScale[instanceID].rgb, 1.0);
    }
}
