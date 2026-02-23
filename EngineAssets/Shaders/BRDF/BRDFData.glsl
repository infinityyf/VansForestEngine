#ifndef COMMON_BRDF_INCLUDED
#define COMMON_BRDF_INCLUDED

#include "../Common/Common.glsl"

//brdf data set
#if !defined(PBRDataSetBind)
    #define PBRDataSetBind 4
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

layout(set=PBRDataSetBind, binding=0, std430) readonly buffer MaterialData
{
    MaterialPayload materials[];
} materialDataBuffer;


#if !defined(PBRLutSetBind)
    #define PBRLutSetBind 5
#endif
//intergration lut
layout(set=PBRLutSetBind, binding=0) uniform sampler2D BRDFLUT;
//pbr texture set
layout(set=PBRLutSetBind, binding=1) uniform samplerCube PreConvDiffuseEnvironment;
layout(set=PBRLutSetBind, binding=2) uniform samplerCube PreConvSpecularEnvironment;
layout(set=PBRLutSetBind, binding=3) buffer shCoefficientsBuffer 
{
    float shCoefficients[27];
};

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
    float a = roughness * roughness;
    float k = ((a + 1.0) * (a + 1.0)) / 8.0;

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

void AmbientBRDF(BRDFData brdf, vec3 viewDirection, inout vec3 diffuse, inout vec3 specular)
{
    float NdotV = max(dot(brdf.normal, viewDirection), 0.0);
    vec3 F = fresnelSchlickRoughness(NdotV, brdf.fresnel0, brdf.roughness);
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
    prefilteredColor = mix(prefilteredColor, brdf.indirectSpecular.rgb, brdf.indirectSpecular.a);
    specular = prefilteredColor * (F * environmentBRDF.x + environmentBRDF.y);
}

void DirectBRDF(BRDFData brdf, vec3 lightDirection, inout vec3 diffuse, inout vec3 specular)
{
    vec3 viewDirection = brdf.viewDirection;
    vec3 halfVector = normalize(lightDirection + viewDirection);
    float NdotH = max(dot(brdf.normal, halfVector), 0.0);
    float NdotL = max(dot(brdf.normal, lightDirection), 0.0);
    float NdotV = max(dot(brdf.normal, viewDirection), 0.0);
    float NdotH2 = NdotH * NdotH;

    vec3 F0 = brdf.fresnel0;
    F0 = mix(F0, brdf.albedo, brdf.metallic);

    vec3 F = FresnelSchlick(NdotH, F0);
    float D = DistributionTrowbridgeReitzGGX(brdf.normal, halfVector, brdf.roughness);
    float G = GeometrySmith(brdf.normal, viewDirection, lightDirection, brdf.roughness);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - brdf.metallic;

    diffuse = vec3(NdotL/ PI) * brdf.albedo * kD;

    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(NdotL, 0.001) * max(NdotV, 0.001);
    specular = numerator / denominator;
}
#endif // COMMON_BRDF_INCLUDED