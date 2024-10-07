#include "../Common/Common.glsl"

layout(set=3, binding=5) uniform MaterialData
{
    vec4 albedo;
    float roughness;
    float metallic;
    float ao;
};

layout( set = 4, binding = 10) uniform samplerCube PreConvDiffuseEnvironment;


struct BRDFData
{
    vec3 albedo;
    vec3 normal;
    float roughness;
    float metallic;
    float ao;
    vec3 fresnel0;
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

float GeometrySchlickGGX(float NdotV, float k)
{
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness)
{
    float NdotV = max(dot(normal, viewDir), 0.0);
    float NdotL = max(dot(normal, lightDir), 0.0);
    float ggx2 = DistributionTrowbridgeReitzGGX(normal, viewDir, roughness);
    float ggx1 = DistributionTrowbridgeReitzGGX(normal, lightDir, roughness);
    float G1 = GeometrySchlickGGX(NdotV, 0.5);
    float G2 = GeometrySchlickGGX(NdotL, 0.5);

    return G1 * G2;
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

void AmbientBRDF(BRDFData brdf, vec3 viewDirection, inout vec3 diffuse)
{
    diffuse = texture(PreConvDiffuseEnvironment, brdf.normal).rgb;
}

void DirectBRDF(BRDFData brdf, vec3 lightDirection, vec3 viewDirection, inout vec3 diffuse, inout vec3 specular)
{
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

    diffuse = vec3(NdotL/ PI);
    diffuse *= kD;

    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(NdotL, 0.001) * max(NdotV, 0.001);
    specular = numerator / denominator;
}