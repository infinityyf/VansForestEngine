#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
layout(early_fragment_tests) in;

#include "../../Common/CameraData.glsl"
#include "../../Common/VansDrawSubmission.glsl"
#include "../../Common/MotionVector.glsl"
#include "../../Common/Common.glsl"
#include "../../BRDF/BRDFData.glsl"

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec3 normal_ws;
layout(location = 2) in vec3 tangent_ws;
layout(location = 3) in vec3 bitangent_ws;
layout(location = 4) in vec3 position_world;
layout(location = 5) in vec4 motion_current_clip;
layout(location = 6) in vec4 motion_previous_clip;

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

layout(location = 0) out vec4 outNormal;    // .xyz = world normal, .w = physical thickness (mm)
layout(location = 1) out vec4 outGBuffer0;  // .rgb = albedo, .w = roughness
layout(location = 2) out vec4 outGBuffer1;  // .x = subsurface amount, .y = ao, .z = material id, .w = material index
layout(location = 3) out vec4 outGBuffer2;  // .xyz = world pos, .w = -linearDepth
layout(location = 4) out vec2 outMotionVector;

void main()
{
    VansDrawData drawData = VansGetDrawData();
    int mi = nonuniformEXT(drawData.materialIndex);
    MaterialPayload mat = materialDataBuffer.materials[mi];

    float thicknessScaleMM = max(mat.metallic, 0.0);
    float subsurfaceAmount = clamp(mat.ao, 0.0, 1.0);

    vec3 albedo = texture(globalPBRTextures[nonuniformEXT(mi * 5 + 0)], frag_uv, MaterialMipBias).rgb;
    float thicknessMask = max(texture(globalPBRTextures[nonuniformEXT(mi * 5 + 2)], frag_uv, MaterialMipBias).r, 0.0);
    float roughness = max(texture(globalPBRTextures[nonuniformEXT(mi * 5 + 3)], frag_uv, MaterialMipBias).r, 0.045);
    float ao = 1.0;

    vec3 normalSample = texture(globalPBRTextures[nonuniformEXT(mi * 5 + 1)], frag_uv, MaterialMipBias).rgb;
    normalSample = normalSample * 2.0 - 1.0;

    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(TBN * normalSample);
    float thicknessMM = thicknessScaleMM * thicknessMask;

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    outNormal = vec4(normal, thicknessMM);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(subsurfaceAmount, ao, float(MATERIAL_ID_SUBSURFACE), float(mi));
    outGBuffer2 = vec4(position_world, -linearDepth);
    outMotionVector = VansMotionVectorFromClip(motion_current_clip, motion_previous_clip);
}
