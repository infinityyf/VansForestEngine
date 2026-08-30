#version 450
#extension GL_GOOGLE_include_directive : require
layout(early_fragment_tests) in;
#include "../../Common/CameraData.glsl"
#include "../../Common/VansDrawSubmission.glsl"
#include "../../Common/MotionVector.glsl"
#include "../../Common/Common.glsl"
#include "../../BRDF/SkinData.glsl"

layout( location = 0 ) in vec2 frag_uv;
layout( location = 1 ) in vec3 normal_ws;
layout( location = 2 ) in vec3 tangent_ws;
layout( location = 3 ) in vec3 bitangent_ws;
layout( location = 4 ) in vec3 position_world;
layout( location = 5 ) in vec4 motion_current_clip;
layout( location = 6 ) in vec4 motion_previous_clip;

// Skin-specific textures (dedicated per-node descriptor set)
layout( set = 4, binding = 0 ) uniform sampler2D skinAlbedoTexture;
layout( set = 4, binding = 1 ) uniform sampler2D skinNormalTexture;
layout( set = 4, binding = 2 ) uniform sampler2D skinRoughnessTexture;
layout( set = 4, binding = 3 ) uniform sampler2D skinCavityTexture;
layout( set = 4, binding = 4 ) uniform sampler2D skinScatterMaskTexture;
layout( set = 4, binding = 5 ) uniform sampler2D skinThicknessTexture;

//输出到MRT
layout (location = 0) out vec4 outNormal;
layout (location = 1) out vec4 outGBuffer0;
layout (location = 2) out vec4 outGBuffer1;
layout (location = 3) out vec4 outGBuffer2;
layout (location = 4) out vec2 outMotionVector;

void main() 
{ 
    VansDrawData drawData = VansGetDrawData();
    float roughness = 0.62;
    float normalStrength = 0.35;
    SkinMaterialPayload skinMaterial = GetSkinMaterialPayload(drawData.materialIndex);
    roughness = clamp(skinMaterial.roughnessNormalSpecular.x, 0.045, 1.0);
    normalStrength = clamp(skinMaterial.roughnessNormalSpecular.y, 0.0, 2.0);

    // Sample dedicated skin textures
    vec3 albedo        = texture(skinAlbedoTexture, frag_uv).rgb;
    vec3 normal_sample = textureLod(skinNormalTexture, frag_uv, 0.0).rgb;
    float roughnessMap = texture(skinRoughnessTexture, frag_uv, MaterialMipBias).r;
    float cavity       = clamp(texture(skinCavityTexture, frag_uv, MaterialMipBias).r, 0.0, 1.0);
    float textureScatterMask = clamp(texture(skinScatterMaskTexture, frag_uv, MaterialMipBias).r, 0.0, 1.0);
    float thinnessMask = clamp(texture(skinThicknessTexture, frag_uv, MaterialMipBias).r, 0.0, 1.0);
    // Scatter coverage and optical thinness are independent authored signals.
    // Multiplying them made a dark thickness map disable the whole diffusion
    // profile instead of affecting transmission only.
    float scatterMask  = textureScatterMask;
    float thinnessWeight = clamp(skinMaterial.profileLUT.y, 0.0, 1.0);
    roughness = clamp(roughness * roughnessMap, 0.045, 1.0);

    normal_sample = normal_sample * 2.0 - 1.0;
    normal_sample.rg *= normalStrength;
    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(TBN * normal_sample);

    // 预积分 Skin 扩散只需要宏观几何曲率。法线贴图包含毛孔等高频细节，
    // 若参与曲率会把微表面误判为薄组织并放大红色散射/透射。
    vec3 geoNormal = normalize(normal_ws);
    vec3 dNdx = dFdx(geoNormal);
    vec3 dNdy = dFdy(geoNormal);
    vec3 dPdx = dFdx(position_world);
    vec3 dPdy = dFdy(position_world);

    // 修正: 分别计算 X/Y 方向曲率再取平均，避免因 max 取大值导致小方向曲率被严重低估
    // 正确: κ_x = |dN/dx| / |dP/dx|，κ_y = |dN/dy| / |dP/dy|，平均后 tone-map
    float kappaX = length(dNdx) / max(length(dPdx), 1e-5);
    float kappaY = length(dNdy) / max(length(dPdy), 1e-5);
    float kappa  = (kappaX + kappaY) * 0.5;

    // Soft tone-map κ into [0,1] via κ/(κ+K).
    // K ≈ 20 maps: flat(κ~5)→0.2, medium(κ~20)→0.5, sharp(κ~100)→0.83
    const float K = 20.0;
    float curvature = kappa / (kappa + K);
    vec3 fresnel0 = vec3(0.04);
    
    // Store curvature in normal.w for the deferred skin BRDF
    outNormal = vec4(normal, curvature);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(
        scatterMask,
        cavity,
        PackSkinMaterialIDWithThinness(float(MATERIAL_ID_SKIN), thinnessMask, thinnessWeight),
        float(drawData.materialIndex));

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;
    outGBuffer2 = vec4(position_world, -linearDepth);
    outMotionVector = VansMotionVectorFromClip(motion_current_clip, motion_previous_clip);
}
