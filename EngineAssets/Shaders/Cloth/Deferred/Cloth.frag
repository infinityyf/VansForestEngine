#version 450
#extension GL_GOOGLE_include_directive : require
// Mask 在深度写入前裁剪；不强制 early_fragment_tests。
#include "../../Common/CameraData.glsl"
#include "../../Common/VansDrawSubmission.glsl"
#include "../../Common/MotionVector.glsl"
#include "../../Common/Common.glsl"
#include "../../BRDF/BRDFData.glsl"
#include "../../BRDF/ClothData.glsl"
#include "../ClothSurface.glsl"

layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_ws;
layout( location = 2 ) in vec3 tangent_ws;
layout( location = 3 ) in vec3 bitangent_ws;
layout( location = 4 ) in vec3 position_world;
layout( location = 5 ) in vec4 motion_current_clip;
layout( location = 6 ) in vec4 motion_previous_clip;

// G-Buffer MRT outputs
layout (location = 0) out vec4 outNormal;   // .xyz = world normal, .w = tangent angle / PI
layout (location = 1) out vec4 outGBuffer0; // .rgb = albedo, .w = effective roughness
layout (location = 2) out vec4 outGBuffer1; // .x = effective sheen weight, .y = ao, .z = material ID, .w = materialIndex
layout (location = 3) out vec4 outGBuffer2; // .xyz = world pos,     .w = linear depth
layout (location = 4) out vec2 outMotionVector;

void main()
{
    VansDrawData drawData = VansGetDrawData();
    int mi = max(drawData.materialIndex, 0);
    MaterialPayload mat = materialDataBuffer.materials[mi];
    ClothMaterialPayload cloth = GetClothMaterialPayload(mi);
    ClothSampledSurface surface = SampleClothSurface(mat.albedo, mat.roughness, mat.ao,
        cloth, frag_uv, MaterialMipBias, normal_ws, tangent_ws, bitangent_ws,
        gl_FrontFacing, vec3(0.0));
    float tangentAngle = EncodeClothTangentAngle(surface.shading.normal, surface.tangent);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    outNormal   = vec4(surface.shading.normal, tangentAngle);
    outGBuffer0 = vec4(surface.shading.albedo, surface.shading.roughness);
    outGBuffer1 = vec4(cloth.sheenColorWeight.a, surface.ao, float(MATERIAL_ID_CLOTH), float(mi));
    outGBuffer2 = vec4(position_world, -linearDepth);
    outMotionVector = VansMotionVectorFromClip(motion_current_clip, motion_previous_clip);
}
