#version 450
#extension GL_GOOGLE_include_directive : require
layout(early_fragment_tests) in;
#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"

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
layout (location = 0) out vec4 outNormal;   // .xyz = world normal,  .w = sheenRoughness
layout (location = 1) out vec4 outGBuffer0; // .rgb = albedo,        .w = sheenRoughness (mirror)
layout (location = 2) out vec4 outGBuffer1; // .x = 0 (no metallic), .y = ao, .z = MATERIAL_ID_CLOTH, .w = silk/fabric blend
layout (location = 3) out vec4 outGBuffer2; // .xyz = world pos,     .w = linear depth

void main()
{
    // 0.0 = full fabric (Charlie), 1.0 = full silk (Ashikhmin)
    const float silkMode = 0.0;

    vec3  albedo         = texture(clothAlbedo,    frag_uv).rgb;
    float sheenRoughness = texture(clothRoughness, frag_uv).r;
    float ao             = texture(clothAO,        frag_uv).r;

    // Normal mapping
    vec3 normal_sample = textureLod(clothNormal, frag_uv, 0.0).rgb;
    normal_sample = normal_sample * 2.0 - 1.0;
    mat3 TBN           = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal        = normalize(TBN * normal_sample);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    outNormal   = vec4(normal, sheenRoughness);
    outGBuffer0 = vec4(albedo, sheenRoughness);
    outGBuffer1 = vec4(0.0, ao, float(MATERIAL_ID_CLOTH), silkMode);
    outGBuffer2 = vec4(position_world, -linearDepth);
}
