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

// Skin-specific textures (dedicated per-node descriptor set)
layout( set = 4, binding = 0 ) uniform sampler2D skinAlbedoTexture;
layout( set = 4, binding = 1 ) uniform sampler2D skinNormalTexture;

layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

//输出到MRT
layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outGBuffer0;
layout (location = 2) out vec4 outGBuffer1;
layout (location = 3) out vec4 outGBuffer2;

void main() 
{ 
    // Skin uses dedicated textures, no material index needed
    float roughness = 0.5;
    float metallic  = 0.0;
    float ao        = 1.0;

    // Sample dedicated skin textures
    vec3 albedo        = texture(skinAlbedoTexture, frag_uv).rgb;
    vec3 normal_sample = texture(skinNormalTexture, frag_uv).rgb;

    normal_sample.rg = normal_sample.rg * 2.0 - 1.0;
    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(TBN * normal_sample);

    // Compute curvature from geometric normal (without normal-map detail)
    // for pre-integrated subsurface scattering lookup
    vec3 geoNormal = normalize(normal_ws);
    float curvature = length(fwidth(geoNormal)) / max(length(fwidth(position_world)), 0.001);
    curvature = clamp(curvature * 0.5, 0.0, 1.0);

    vec3 fresnel0 = vec3(0.04);
    
    // Store curvature in normal.w for the deferred skin BRDF
    outNormal = vec4(normal, curvature);
    outGBuffer0 = vec4(albedo, roughness);
    // Mark material ID as MATERIAL_ID_SKIN (3) in GBuffer1.z
    outGBuffer1 = vec4(metallic, ao, float(MATERIAL_ID_SKIN), 1.0);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;
    outGBuffer2 = vec4(position_world, -linearDepth);
}
