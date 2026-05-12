#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(early_fragment_tests) in;
#include "../../Common/CameraData.glsl"
#include "../../BRDF/BRDFData.glsl"

layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_ws;
layout( location = 2 ) in vec3 tangent_ws;
layout( location = 3 ) in vec3 bitangent_ws;
layout( location = 4 ) in vec3 position_world;
// layout( set=2, binding=0 ) uniform sampler2D baseColor;
// layout( set=2, binding=1 ) uniform sampler2D normalMap;
// layout( set=2, binding=2 ) uniform sampler2D metalMap;
// layout( set=2, binding=3 ) uniform sampler2D roughnessMap;
// layout( set=2, binding=4 ) uniform sampler2D aoMap;
layout( set = 0, binding = 50 ) uniform sampler2D globalPBRTextures[];
layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
} materialConst;

//输出到MRT
layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outGBuffer0;
layout (location = 2) out vec4 outGBuffer1;
layout (location = 3) out vec4 outGBuffer2;

void main() 
{ 
    int materialIndex = nonuniformEXT(materialConst.materialIndex);
    //从globalbuffer里获取pbr参数
    MaterialPayload materialData = materialDataBuffer.materials[materialIndex];
    vec3 albedoParam = materialData.albedo.rgb;
    float roughnessParam = materialData.roughness;
    float metallicParam = materialData.metallic;
    float aoParam = materialData.ao;

    //采样其他 PBR 纹理 (通过 Bindless 索引)
    vec3 albedo     = albedoParam * texture( globalPBRTextures[materialIndex * 5 + 0], frag_uv ).rgb;
    vec3 normal_sample = texture(globalPBRTextures[materialIndex * 5 + 1], frag_uv).rgb;
    float metallic  = metallicParam * texture( globalPBRTextures[materialIndex * 5 + 2], frag_uv ).r;
    float roughness = roughnessParam * texture( globalPBRTextures[materialIndex * 5 + 3], frag_uv ).r;
    float ao        = aoParam * texture( globalPBRTextures[materialIndex * 5 + 4], frag_uv ).r;

    normal_sample = normal_sample * 2.0 - 1.0;
    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(TBN * normal_sample);

    

    vec3 fresnel0 = vec3(0.04);
    
    outNormal = vec4(normal, 1.0);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(metallic, ao, float(MATERIAL_ID_PBR), 1.0);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;
    outGBuffer2 = vec4(position_world, -linearDepth);
}