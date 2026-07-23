#version 450
#extension GL_GOOGLE_include_directive : require
layout(early_fragment_tests) in;
#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"
#include "../../BRDF/BRDFData.glsl"
#include "../../BRDF/ClothData.glsl"

layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_ws;
layout( location = 2 ) in vec3 tangent_ws;
layout( location = 3 ) in vec3 bitangent_ws;
layout( location = 4 ) in vec3 position_world;

// Set 4 — per-node cloth textures (albedo + normal + roughness + ao)
layout( set = 4, binding = 0 ) uniform sampler2D clothAlbedo;
layout( set = 4, binding = 1 ) uniform sampler2D clothNormal;
layout( set = 4, binding = 2 ) uniform sampler2D clothRoughness;
layout( set = 4, binding = 3 ) uniform sampler2D clothAO;

layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

// G-Buffer MRT outputs
layout (location = 0) out vec4 outNormal;   // .xyz = world normal, .w = tangent angle / PI
layout (location = 1) out vec4 outGBuffer0; // .rgb = albedo, .w = effective roughness
layout (location = 2) out vec4 outGBuffer1; // .x = fallback sheen weight, .y = ao, .z = material ID, .w = materialIndex
layout (location = 3) out vec4 outGBuffer2; // .xyz = world pos,     .w = linear depth

void main()
{
    int mi = max(materialConst.materialIndex, 0);
    MaterialPayload mat = materialDataBuffer.materials[mi];

    vec3  albedo         = max(mat.albedo.rgb, vec3(0.0)) * texture(clothAlbedo, frag_uv, MaterialMipBias).rgb;
    float sheenRoughness = clamp(mat.roughness * texture(clothRoughness, frag_uv, MaterialMipBias).r, 0.045, 1.0);
    float ao             = clamp(mat.ao * texture(clothAO, frag_uv, MaterialMipBias).r, 0.0, 1.0);
    float sheenStrength  = clamp(mat.padding, 0.0, 1.0);

    // Two-sided, orthonormal TBN. Keep the original tangent direction so silk
    // anisotropy remains stable when the geometric normal is flipped.
    vec3 geometricNormal = normalize(normal_ws);
    if (!gl_FrontFacing)
        geometricNormal = -geometricNormal;
    vec3 tangent = normalize(tangent_ws - geometricNormal * dot(geometricNormal, tangent_ws));
    float handedness = dot(cross(normalize(normal_ws), normalize(tangent_ws)), normalize(bitangent_ws)) < 0.0
        ? -1.0 : 1.0;
    vec3 bitangent = normalize(cross(geometricNormal, tangent)) * handedness;

    vec3 normal_sample = texture(clothNormal, frag_uv, MaterialMipBias).rgb;
    normal_sample = normal_sample * 2.0 - 1.0;
    mat3 TBN           = mat3(tangent, bitangent, geometricNormal);
    vec3 normal        = normalize(TBN * normal_sample);
    float tangentAngle = EncodeClothTangentAngle(normal, tangent);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    outNormal   = vec4(normal, tangentAngle);
    outGBuffer0 = vec4(albedo, sheenRoughness);
    outGBuffer1 = vec4(sheenStrength, ao, float(MATERIAL_ID_CLOTH), float(mi));
    outGBuffer2 = vec4(position_world, -linearDepth);
}
