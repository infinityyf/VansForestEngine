#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
#include "../Common/Common.glsl"
#include "../GI/GIReceiverTransportMesh.glsl"
#define GI_INSTANCE_MATERIAL_SET 2
#include "../GI/GIInstanceMaterial.glsl"
layout(set=2,binding=10,std430) readonly buffer InstanceEmission { vec4 emissionScale[]; } instanceGIEmissionData;
layout(set=2,binding=50) uniform sampler2D PBRTextures[];
layout(location=0) rayPayloadInEXT RayTracePayload prd;
hitAttributeEXT vec2 attribs;
#define ALBEDO_INDEX 0
#define METALLIC_INDEX 2
#define ROUGHNESS_INDEX 3
#define AO_INDEX 4
void main()
{
    prd.hitDistance=gl_HitTEXT;
    if(prd.geometryOnly!=0u) return; // 净空检查不解码顶点材质，也不改变主命中的照明数据。
    uint instanceID=gl_InstanceID;
    uint modelIndex=receiverInstances.instances[instanceID];
    uvec3 triangle=ReceiverTriangle(modelIndex);
    vec3 bary=vec3(1.0-attribs.x-attribs.y,attribs);
    vec3 normal=ReceiverInterpolatedAttribute(modelIndex,triangle,bary,2u);
    if(dot(normal,normal)<1e-8)
        normal=cross(ReceiverAttribute(modelIndex,triangle.y,0u)-ReceiverAttribute(modelIndex,triangle.x,0u),
            ReceiverAttribute(modelIndex,triangle.z,0u)-ReceiverAttribute(modelIndex,triangle.x,0u));
    vec3 worldNormal=transpose(mat3(gl_WorldToObjectEXT))*normal;
    worldNormal=dot(worldNormal,worldNormal)>1e-8?normalize(worldNormal):-gl_WorldRayDirectionEXT;
    bool frontFace=gl_HitKindEXT!=gl_HitKindBackFacingTriangleEXT;
    if(!frontFace) worldNormal=-worldNormal;
    prd.normalHit=vec4(worldNormal,frontFace?1.0:-1.0);
    vec2 uv=ReceiverInterpolatedAttribute(modelIndex,triangle,bary,1u).xy;
    // 材质语义与世界 GI 的 RayTracingTest.rchit 相同；仅顶点寻址独立。
    uint packedTextureIndex = instanceMaterialData.materials[instanceID].packedTextureIndex;
    uint textureIndex = packedTextureIndex & GI_TEXTURE_INDEX_MASK;
    bool pureEmissive = (packedTextureIndex & GI_PURE_EMISSIVE_FLAG) != 0u;
    bool pbrEmissive = (packedTextureIndex & GI_PBR_EMISSIVE_FLAG) != 0u;
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
