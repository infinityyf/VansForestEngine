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

layout( set = 0, binding = 50 ) uniform sampler2D globalPBRTextures[];
layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

// 输出到 MRT
layout( location = 0 ) out vec4 outNormal;
layout( location = 1 ) out vec4 outGBuffer0;
layout( location = 2 ) out vec4 outGBuffer1;
layout( location = 3 ) out vec4 outGBuffer2;

void main()
{
    int materialIndex = nonuniformEXT(materialConst.materialIndex);

    // 从全局 PBR SSBO 读取自发光参数
    // 约定：MaterialPayload.albedo    = 自发光颜色 (RGB)
    //        MaterialPayload.roughness = 自发光强度 (scalar)，写入 GBuffer0.w (roughness 插槽)
    MaterialPayload materialData = materialDataBuffer.materials[materialIndex];
    vec3 emissiveColorParam = materialData.albedo.rgb;
    float emissiveIntensity  = materialData.roughness;   // 强度复用 roughness 通道

    // 采样自发光纹理（Bindless slot 0，与 PBR albedo 规则一致）
    // 未配置纹理时 defaultAlbedo 为纯白，乘法中性
    vec3 emissiveColor = emissiveColorParam
                       * texture( globalPBRTextures[materialIndex * 5 + 0], frag_uv ).rgb;

    // ── 法线：与 PBR 路径保持一致，通过法线贴图做 TBN 变换（slot 1）──────────
    vec3 normal_sample = texture( globalPBRTextures[materialIndex * 5 + 1], frag_uv ).rgb;
    normal_sample = normal_sample * 2.0 - 1.0;
    mat3 TBN   = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(TBN * normal_sample);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    // 法线格式与其他材质保持一致
    outNormal   = vec4(normal, 1.0);
    // GBuffer0.rgb = 自发光颜色，GBuffer0.w = 强度（占用 roughness 插槽）
    // Deferred.frag 读取 color.rgb * roughness 即可得到最终发光输出
    outGBuffer0 = vec4(emissiveColor, emissiveIntensity);
    outGBuffer1 = vec4(0.0, 0.0, float(MATERIAL_ID_EMISSIVE), 0.0);
    outGBuffer2 = vec4(position_world, -linearDepth);
}
