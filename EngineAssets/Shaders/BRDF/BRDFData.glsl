#ifndef COMMON_BRDF_INCLUDED
#define COMMON_BRDF_INCLUDED

#include "../Common/Common.glsl"

//brdf data set
#if !defined(PBRDataSetBind)
    #define PBRDataSetBind 0
#endif
#if !defined(PBRDataBinding)
    #define PBRDataBinding 2
#endif

// 1. 定义材质结构体 (注意对齐)
struct MaterialPayload
{
    vec4 albedo;
    float roughness;
    float metallic;
    float ao;
    float padding; // 显式填充：vec4(16) + 3*float(12) = 28。数组元素需要按最大成员(16)对齐，因此补齐到32。
};

// layout(set=PBRDataSetBind, binding=0) uniform MaterialData
// {
//     vec4 albedo;
//     float roughness;
//     float metallic;
//     float ao;
// };

layout(set=PBRDataSetBind, binding=PBRDataBinding, std430) readonly buffer MaterialData
{
    MaterialPayload materials[];
} materialDataBuffer;


#if !defined(PBRLutSetBind)
    #define PBRLutSetBind 0
#endif
//intergration lut
layout(set=PBRLutSetBind, binding=3) uniform sampler2D BRDFLUT;
//pbr texture set
layout(set=PBRLutSetBind, binding=4) uniform samplerCube PreConvDiffuseEnvironment;
layout(set=PBRLutSetBind, binding=5) uniform samplerCube PreConvSpecularEnvironment;
layout(set=PBRLutSetBind, binding=6) buffer shCoefficientsBuffer 
{
    float shCoefficients[27];
};
// Pre-integrated skin scattering LUT
// U = NdotL * 0.5 + 0.5,  V = curvature [0..1]
layout(set = PBRLutSetBind, binding = 7) uniform sampler2D SkinPreIntegratedLUT;

// Cloth pre-integrated DFG LUT  (EngineAssets/Textures/ClothBRDFLUT.png)
// U = NoV [0..1],  V = perceptualRoughness [0..1]
// R = split-sum term A  (F-independent, mix start)
// G = split-sum term B  (F-dependent,   mix end)
// B = sheen tint (pre-baked directional sheen colour scale)
// Usage: E = mix(dfg.rrr, dfg.ggg, sheenColor)
layout(set = PBRLutSetBind, binding = 8) uniform sampler2D ClothBRDFLUT;

struct BRDFData
{
    vec3 albedo;
    vec3 normal;
    float roughness;
    float metallic;
    float ao;
    vec3 fresnel0;
    vec3 viewDirection;
    vec3 positionWS;
    vec3 indirectDiffuse;
    vec4 indirectSpecular; // rgba: color, a: mask
};

float DistributionTrowbridgeReitzGGX(vec3 normal, vec3 halfvector, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(normal, halfvector), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness)
{
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotL = max(dot(normal, lightDir), 0.0);
    // float ggx2 = DistributionTrowbridgeReitzGGX(normal, viewDir, roughness);
    // float ggx1 = DistributionTrowbridgeReitzGGX(normal, lightDir, roughness);
    float G1 = GeometrySchlickGGX(NdotV, roughness);
    float G2 = GeometrySchlickGGX(NdotL, roughness);

    return G1 * G2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}   

float GetMipLevelFromRoughness(float roughness)
{
    return roughness * 9.0;
}


float D_GGX(float NoH, float Roughness)
{
	Roughness = pow(Roughness,4);
	float D = (NoH * Roughness - NoH) * NoH + 1;
	return Roughness / (PI * D*D);
}

float Vis_SmithGGXCorrelated(float NoL, float NoV, float Roughness)
{
    // sanitize inputs to avoid NaNs and negative sqrt arguments
    NoL = clamp(NoL, 0.0, 1.0);
    NoV = clamp(NoV, 0.0, 1.0);
    float r = max(Roughness, 0.0);

    float a = r * r;
    // compute safe radicands
    float radV = (-NoL * a + NoL) * NoV + a;
    float radL = (-NoV * a + NoV) * NoL + a;
    radV = max(radV, 0.0);
    radL = max(radL, 0.0);

    float LambdaV = NoV * sqrt(radV);
    float LambdaL = NoL * sqrt(radL);

    float denom = LambdaL + LambdaV;
    const float EPS = 1e-6;
    if (denom <= EPS) return 0.0;

    return (0.5 / denom) / PI;
}

float SSR_BRDF(vec3 V, vec3 L, vec3 N, float Roughness)
{
	vec3 H = normalize(L + V);

	float NoH = max(dot(N, H), 0);
	float NoL = max(dot(N, L), 0);
	float NoV = max(dot(N, V), 0);

	float D = D_GGX(NoH, Roughness);
	float G = Vis_SmithGGXCorrelated(NoL, NoV, Roughness);

	return max(0, D * G);
}

void AmbientBRDF(BRDFData brdf, vec3 viewDirection, vec4 giVisSH, inout vec3 diffuse, inout vec3 specular)
{
    float NdotV = max(dot(brdf.normal, viewDirection), 0.0);
    vec3 F = fresnelSchlickRoughness(NdotV, brdf.fresnel0, brdf.roughness);

    //vec3 F = fresnelSchlickRoughness(NdotV, F0, brdf.roughness);
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - brdf.metallic;
    
    diffuse = brdf.indirectDiffuse * brdf.ao * kD * brdf.albedo;

    vec3 reflection = reflect(-viewDirection, brdf.normal); 
    vec2 intergrationUV = vec2(NdotV, brdf.roughness);
    intergrationUV.y = 1 - intergrationUV.y;
    vec2 environmentBRDF = texture(BRDFLUT, intergrationUV).rg;

    //reflection specular lod level
    float lod = GetMipLevelFromRoughness(brdf.roughness);
    vec3 prefilteredColor = textureLod(PreConvSpecularEnvironment,reflection,lod).rgb;
    // Attenuate cubemap by directional GI probe visibility (indoor occlusion)
    float giVis = EvalGIVisibility(giVisSH, reflection);
    prefilteredColor *= giVis;
    // Roughness fade: SSR quality degrades on rough surfaces — smoothly
    // fall back to the pre-filtered cubemap which is always correct.
    float ssrFade = 1;//1.0 - smoothstep(SSR_ROUGHNESS_FADE_START, SSR_ROUGHNESS_FADE_END, brdf.roughness);
    float ssrWeight = brdf.indirectSpecular.a * ssrFade;
    prefilteredColor = mix(prefilteredColor, brdf.indirectSpecular.rgb, ssrWeight);
    // Split-sum: LUT already integrates Fresnel, so use F0 (not F) here
    specular = prefilteredColor * (F * environmentBRDF.x + environmentBRDF.y) * brdf.ao;
}

void DirectBRDF(BRDFData brdf, vec3 lightDirection, inout vec3 diffuse, inout vec3 specular)
{
    vec3 viewDirection = brdf.viewDirection;
    vec3 halfVector = normalize(lightDirection + viewDirection);
    float NdotH = max(dot(brdf.normal, halfVector), 0.0);
    float NdotL = max(dot(brdf.normal, lightDirection), 0.0);
    float NdotV = max(dot(brdf.normal, viewDirection), 0.0);
    float VdotH = max(dot(viewDirection, halfVector), 0.0);
    float roughness = clamp(brdf.roughness, 0.045, 1.0);

    if (NdotL <= 0.0 || NdotV <= 0.0)
    {
        diffuse = vec3(0.0);
        specular = vec3(0.0);
        return;
    }

    vec3 F0 = brdf.fresnel0;
    F0 = mix(F0, brdf.albedo, brdf.metallic);

    vec3 F = FresnelSchlick(VdotH, F0);
    float D = DistributionTrowbridgeReitzGGX(brdf.normal, halfVector, roughness);
    float G = GeometrySmith(brdf.normal, viewDirection, lightDirection, roughness);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - brdf.metallic;

    diffuse = vec3(NdotL/ PI) * brdf.albedo * kD;

    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(NdotL, 0.001) * max(NdotV, 0.001);
    specular = (numerator / denominator) * NdotL;
}
#endif // COMMON_BRDF_INCLUDED