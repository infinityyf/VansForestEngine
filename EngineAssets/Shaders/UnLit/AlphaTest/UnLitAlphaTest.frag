#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../../Common/CameraData.glsl"
#include "../../Common/VansDrawSubmission.glsl"
#include "../../Common/MotionVector.glsl"
#include "../../BRDF/BRDFData.glsl"

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec3 normal_ws;
layout(location = 2) in vec3 tangent_ws;
layout(location = 3) in vec3 bitangent_ws;
layout(location = 4) in vec3 position_world;
layout(location = 5) in vec4 motion_current_clip;
layout(location = 6) in vec4 motion_previous_clip;
layout(location = 14) flat in float decalReceiverGroup;

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outGBuffer0;
layout(location = 2) out vec4 outGBuffer1;
layout(location = 3) out vec4 outGBuffer2;
layout(location = 4) out vec2 outMotionVector;

void main()
{
    VansDrawData drawData = VansGetDrawData();
    int materialIndex = nonuniformEXT(drawData.materialIndex);
    MaterialPayload materialData = materialDataBuffer.materials[materialIndex];

    // Alpha test 必须发生在 GBuffer/深度写入前，因此本变体不启用 early_fragment_tests。
    vec4 baseColorSample = texture(
        globalPBRTextures[materialIndex * 5 + 0], frag_uv, MaterialMipBias);
    if (baseColorSample.a < materialData.padding)
        discard;

    vec3 albedo = materialData.albedo.rgb * baseColorSample.rgb;
    vec3 normalSample = texture(
        globalPBRTextures[materialIndex * 5 + 1], frag_uv, MaterialMipBias).rgb;
    float metallic = materialData.metallic * texture(
        globalPBRTextures[materialIndex * 5 + 2], frag_uv, MaterialMipBias).r;
    float roughness = materialData.roughness * texture(
        globalPBRTextures[materialIndex * 5 + 3], frag_uv, MaterialMipBias).r;
    float ao = materialData.ao * texture(
        globalPBRTextures[materialIndex * 5 + 4], frag_uv, MaterialMipBias).r;

    normalSample = normalSample * 2.0 - 1.0;
    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(TBN * normalSample);

    outNormal = vec4(normal, 1.0);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(metallic, ao, float(MATERIAL_ID_PBR),
        -decalReceiverGroup);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;
    outGBuffer2 = vec4(position_world, -linearDepth);
    outMotionVector = VansMotionVectorFromClip(motion_current_clip, motion_previous_clip);
}
