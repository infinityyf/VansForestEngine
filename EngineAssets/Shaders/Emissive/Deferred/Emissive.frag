#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

layout(early_fragment_tests) in;
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

    // Legacy emissive materials encode -(intensity + 1) in padding and keep
    // their slot-0-only behavior. Non-negative padding identifies a mixed
    // PBR + emission-mask material imported from a shared lamp atlas.
    bool mixedPbrEmission = materialData.padding >= 0.0;
    vec3 baseColor = materialData.albedo.rgb *
        texture(globalPBRTextures[materialIndex * 5 + 0], frag_uv, MaterialMipBias).rgb;
    vec3 normalSample = texture(
        globalPBRTextures[materialIndex * 5 + 1], frag_uv, MaterialMipBias).rgb;
    normalSample = normalSample * 2.0 - 1.0;
    mat3 tbn = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(tbn * normalSample);
    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    outNormal = vec4(normal, 1.0);
    if (!mixedPbrEmission)
    {
        float emissiveIntensity = max(-materialData.padding - 1.0, 0.0);
        outGBuffer0 = vec4(baseColor, emissiveIntensity);
        outGBuffer1 = vec4(0.0, 0.0, float(MATERIAL_ID_EMISSIVE), 0.0);
    }
    else
    {
        float metallic = materialData.metallic * texture(
            globalPBRTextures[materialIndex * 5 + 2], frag_uv, MaterialMipBias).r;
        float roughness = materialData.roughness * texture(
            globalPBRTextures[materialIndex * 5 + 3], frag_uv, MaterialMipBias).r;
        vec3 emission = texture(
            globalPBRTextures[materialIndex * 5 + 4], frag_uv, MaterialMipBias).rgb;

        // The emission map is a mask over a shared atlas. Lit texels bypass
        // lighting; black texels retain the full metallic PBR response.
        if (max(max(emission.r, emission.g), emission.b) > (2.0 / 255.0))
        {
            outGBuffer0 = vec4(emission, materialData.padding);
            outGBuffer1 = vec4(0.0, 0.0, float(MATERIAL_ID_EMISSIVE), 0.0);
        }
        else
        {
            outGBuffer0 = vec4(baseColor, clamp(roughness, 0.045, 1.0));
            outGBuffer1 = vec4(clamp(metallic, 0.0, 1.0),
                              clamp(materialData.ao, 0.0, 1.0),
                              float(MATERIAL_ID_PBR), 1.0);
        }
    }
    outGBuffer2 = vec4(position_world, -linearDepth);
    outMotionVector = VansMotionVectorFromClip(motion_current_clip, motion_previous_clip);
}
