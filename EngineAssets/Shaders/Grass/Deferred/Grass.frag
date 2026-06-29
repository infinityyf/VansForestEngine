#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"

// ── Varyings from vertex shader ────────────────────────────────────────────
layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_ws;
layout( location = 2 ) in vec3 tangent_ws;
layout( location = 3 ) in vec3 bitangent_ws;
layout( location = 4 ) in vec3 position_world;

// ── Set 4: Per-material grass textures ─────────────────────────────────────
layout( set = 4, binding = 0 ) uniform sampler2D grassAlbedo;
layout( set = 4, binding = 1 ) uniform sampler2D grassNormal;
layout( set = 4, binding = 2 ) uniform sampler2D grassRoughness;
layout( set = 4, binding = 3 ) uniform sampler2D grassTranslucency;
layout( set = 4, binding = 4 ) uniform sampler2D grassAO;

// ── Push constants ─────────────────────────────────────────────────────────
layout( push_constant ) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

// ── G-Buffer MRT outputs ───────────────────────────────────────────────────
layout (location = 0) out vec4 outNormal;    // .xyz = world normal,  .w = translucency [0,1]
layout (location = 1) out vec4 outGBuffer0;  // .rgb = albedo,        .w = roughness
layout (location = 2) out vec4 outGBuffer1;  // .x = reserved,        .y = ao, .z = MATERIAL_ID_GRASS, .w = 1.0
layout (location = 3) out vec4 outGBuffer2;  // .xyz = world pos,     .w = -linearDepth

void main() 
{
    // Sample textures
    vec4  albedoSample = texture(grassAlbedo, frag_uv);

    // Alpha test — discard transparent pixels (grass cards)
    if (albedoSample.a < 0.5)
        discard;

    vec3  albedo       = albedoSample.rgb * vec3(0.3, 0.75, 0.15); // tint grass green
    float roughness    = 0.5;  // fixed roughness for grass — clear specular highlight
    float translucency = texture(grassTranslucency, frag_uv).r;
    float ao           = texture(grassAO, frag_uv).r;

    // Use interpolated vertex normal directly (skinned and bent by bone animation).
    // The blade is a two-sided mesh: for back-facing fragments, negate the
    // geometric normal so the back side is lit by light coming from behind.
    // This is a winding-based flip (gl_FrontFacing), NOT a view-direction flip,
    // so specular highlights and directional lighting remain correct.
    vec3 normal = normalize(normal_ws);
    if (!gl_FrontFacing)
        normal = -normal;

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    // Write G-Buffer
    outNormal   = vec4(normal, translucency);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(0.0, ao, float(MATERIAL_ID_GRASS), 1.0);
    outGBuffer2 = vec4(position_world, -linearDepth);
}
