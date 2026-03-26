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

// Set 4 — per-node subsurface textures (albedo + normal + thickness + roughness)
layout( set = 4, binding = 0 ) uniform sampler2D subsurfaceAlbedo;
layout( set = 4, binding = 1 ) uniform sampler2D subsurfaceNormal;
layout( set = 4, binding = 2 ) uniform sampler2D subsurfaceThickness;
layout( set = 4, binding = 3 ) uniform sampler2D subsurfaceRoughness;

layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

// G-Buffer MRT outputs
layout (location = 0) out vec4 outNormal;    // .xyz = world normal,  .w = thickness [0,1]
layout (location = 1) out vec4 outGBuffer0;  // .rgb = albedo,        .w = roughness
layout (location = 2) out vec4 outGBuffer1;  // .x = subsurfacePower (packed), .y = ao, .z = MATERIAL_ID_SUBSURFACE, .w = 1.0
layout (location = 3) out vec4 outGBuffer2;  // .xyz = world pos,     .w = -linearDepth

void main() 
{
    // Sample subsurface textures
    vec3  albedo    = texture(subsurfaceAlbedo, frag_uv).rgb;
    float thickness = 0.2;//texture(subsurfaceThickness, frag_uv).r; // 0 = thin (max transmission), 1 = thick (no transmission)
    float roughness = 0;//texture(subsurfaceRoughness, frag_uv).r;
    float ao        = 1.0;

    // Normal mapping
    vec3 normal_sample = textureLod(subsurfaceNormal, frag_uv, 0.0).rgb;
    normal_sample.rg   = normal_sample.rg * 2.0 - 1.0;
    normal_sample.rg  *= 0.5;  // moderate normal strength
    mat3 TBN           = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal        = normalize(TBN * normal_sample);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    // Pack subsurfacePower into GBuffer1.x:
    // subsurfacePower typically ranges [1, 50], normalize to [0,1] via power / 50.0
    // Default subsurfacePower = 12.234 → packed ~ 0.245
    float subsurfacePowerPacked = 12.234 / 50.0;

    // Store thickness in outNormal.w for the deferred subsurface BRDF
    outNormal   = vec4(normal, thickness);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(subsurfacePowerPacked, ao, float(MATERIAL_ID_SUBSURFACE), 1.0);
    outGBuffer2 = vec4(position_world, -linearDepth);
}
