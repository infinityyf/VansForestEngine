#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require
layout(early_fragment_tests) in;

#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"
#include "../../BRDF/BRDFData.glsl"

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec3 normal_ws;
layout(location = 2) in vec3 tangent_ws;
layout(location = 3) in vec3 bitangent_ws;
layout(location = 4) in vec3 position_world;

layout(set = 0, binding = 50) uniform sampler2D globalPBRTextures[];

layout(push_constant) uniform MaterialPushConsts
{
    int materialIndex;
    int objectIndex;
    int animationEnabled;
} materialConst;

layout(location = 0) out vec4 outNormal;    // .xyz = world normal, .w = effective thickness
layout(location = 1) out vec4 outGBuffer0;  // .rgb = albedo, .w = roughness
layout(location = 2) out vec4 outGBuffer1;  // .x = subsurface amount, .y = ao, .z = material id, .w = material index
layout(location = 3) out vec4 outGBuffer2;  // .xyz = world pos, .w = -linearDepth

float EstimateEffectiveThickness(vec3 normalWS, vec3 positionWS,
                                 float baseThickness, float curvatureInfluence)
{
    vec3 dNdx = dFdx(normalWS);
    vec3 dNdy = dFdy(normalWS);
    vec3 dPdx = dFdx(positionWS);
    vec3 dPdy = dFdy(positionWS);

    float kx = length(dNdx) / max(length(dPdx), 1e-4);
    float ky = length(dNdy) / max(length(dPdy), 1e-4);
    float curvature = (kx + ky) * 0.5;

    float thinFeature = curvature / (curvature + 20.0);
    return clamp(baseThickness - thinFeature * curvatureInfluence, 0.0, 1.0);
}

void main()
{
    int mi = nonuniformEXT(materialConst.materialIndex);
    MaterialPayload mat = materialDataBuffer.materials[mi];

    float baseThickness = clamp(mat.metallic, 0.0, 1.0);
    float subsurfaceAmount = clamp(mat.ao, 0.0, 1.0);
    float curvatureInfluence = clamp(mat.padding, 0.0, 1.0);

    vec3 albedo = texture(globalPBRTextures[nonuniformEXT(mi * 5 + 0)], frag_uv).rgb;
    float roughness = max(texture(globalPBRTextures[nonuniformEXT(mi * 5 + 3)], frag_uv).r, 0.045);
    float ao = 1.0;

    vec3 normalSample = textureLod(globalPBRTextures[nonuniformEXT(mi * 5 + 1)], frag_uv, 0.0).rgb;
    normalSample = normalSample * 2.0 - 1.0;
    normalSample.rg *= 0.5;

    mat3 TBN = mat3(normalize(tangent_ws), normalize(bitangent_ws), normalize(normal_ws));
    vec3 normal = normalize(TBN * normalSample);
    float effectiveThickness = EstimateEffectiveThickness(
        normal, position_world, baseThickness, curvatureInfluence);

    float linearDepth = (ViewMatrix * vec4(position_world, 1.0)).z;

    outNormal = vec4(normal, effectiveThickness);
    outGBuffer0 = vec4(albedo, roughness);
    outGBuffer1 = vec4(subsurfaceAmount, ao, float(MATERIAL_ID_SUBSURFACE), float(mi));
    outGBuffer2 = vec4(position_world, -linearDepth);
}
