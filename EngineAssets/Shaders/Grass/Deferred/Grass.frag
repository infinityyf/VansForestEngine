#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"

// Varyings from vertex shader
layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_ws;
layout( location = 2 ) in vec3 tangent_ws;
layout( location = 3 ) in vec3 bitangent_ws;
layout( location = 4 ) in vec3 position_world;
layout( location = 5 ) in float blade_height01;

// Set 4: per-material grass textures
layout( set = 4, binding = 0 ) uniform sampler2D grassAlbedo;
layout( set = 4, binding = 1 ) uniform sampler2D grassNormal;
layout( set = 4, binding = 2 ) uniform sampler2D grassRoughness;
layout( set = 4, binding = 3 ) uniform sampler2D grassTranslucency;
layout( set = 4, binding = 4 ) uniform sampler2D grassAO;

// Push constants
layout( push_constant ) uniform GrassDrawPC
{
    int materialIndex;
    int objectIndex;
    uint vertexFeatureMask;
    uint boneCount;
    uint subBladeCount;
    float grassHeight;
    float terrainSize;
    float terrainMaxHeight;
    float terrainHeightOffset;
    int terrainEnabled;
    float lodMidDist;
    float lodFarDist;
    float aoStrength;
    float rootAOIntensity;
    float rootAOHeight;
} pc;

// G-Buffer MRT outputs
layout (location = 0) out vec4 outNormal;    // .xyz = world normal,  .w = translucency [0,1]
layout (location = 1) out vec4 outGBuffer0;  // .rgb = albedo,        .w = roughness
layout (location = 2) out vec4 outGBuffer1;  // .x = reserved,        .y = ao, .z = MATERIAL_ID_GRASS, .w = 1.0
layout (location = 3) out vec4 outGBuffer2;  // .xyz = world pos,     .w = -linearDepth

void main() 
{
    // Sample textures
    vec4  albedoSample = texture(grassAlbedo, frag_uv, MaterialMipBias);

    // Alpha test: discard transparent pixels (grass cards).
    if (albedoSample.a < 0.5)
        discard;

    // Preserve the authored albedo. External grass atlases already contain their
    // intended colour, so a fixed green multiplier would break the PBR input.
    vec3  albedo       = albedoSample.rgb;
    float roughness    = clamp(texture(grassRoughness, frag_uv, MaterialMipBias).r, 0.35, 0.98);
    float translucency = clamp(texture(grassTranslucency, frag_uv, MaterialMipBias).r, 0.0, 1.0);
    float sampledAO    = clamp(texture(grassAO, frag_uv, MaterialMipBias).r, 0.0, 1.0);

    // 纹理 AO 描述叶片自身细节；高度 AO 补足草簇根部在 Alpha Cutout、
    // 低几何密度及半分辨率 SSAO 下容易丢失的环境遮蔽。使用 min 合并不同
    // 尺度的可见度，避免把同一暗部重复相乘而压成纯黑。
    float materialAO = mix(1.0, sampledAO, clamp(pc.aoStrength, 0.0, 1.0));
    float rootRange = max(pc.rootAOHeight, 0.001);
    float rootMask = 1.0 - smoothstep(0.0, rootRange, clamp(blade_height01, 0.0, 1.0));
    float rootContact = clamp(pc.rootAOIntensity, 0.0, 0.85) * rootMask;
    float rootAO = 1.0 - rootContact;
    float ao = min(materialAO, rootAO);

    // 屏幕空间接触阴影无法稳定覆盖密集的亚像素草簇。直接在 Grass
    // GBuffer 写入前压低根部固有色，可同时改善直射漫反射与现有薄片透射，
    // 且不改变延迟材质通道语义，也不新增阴影投射或阴影透射流程。
    float rootColorVisibility = 1.0 - rootContact * 0.75;
    albedo *= rootColorVisibility;

    // Use interpolated vertex normal directly (skinned and bent by bone animation).
    // The blade is a two-sided mesh: for back-facing fragments, negate the
    // geometric normal so the back side is lit by light coming from behind.
    // This is a winding-based flip (gl_FrontFacing), NOT a view-direction flip,
    // so specular highlights and directional lighting remain correct.
    vec3 geomNormal = normalize(normal_ws);
    vec3 tangent = normalize(tangent_ws);
    vec3 bitangent = normalize(bitangent_ws);
    vec3 tangentNormal = texture(grassNormal, frag_uv, MaterialMipBias).xyz * 2.0 - 1.0;
    tangentNormal.xy *= 0.55;
    vec3 normal = normalize(mat3(tangent, bitangent, geomNormal) * normalize(tangentNormal));
    if (!gl_FrontFacing)
        normal = -normal;

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    // Write G-Buffer
    outNormal   = vec4(normal, translucency);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(0.0, ao, float(MATERIAL_ID_GRASS), 1.0);
    outGBuffer2 = vec4(position_world, -linearDepth);
}
