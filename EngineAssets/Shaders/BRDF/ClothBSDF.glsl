#ifndef CLOTH_BSDF_INCLUDED
#define CLOTH_BSDF_INCLUDED

#include "ClothData.glsl"

// Single optical closure shared by deferred lighting and reflection captures.
// No lights, pass descriptors or SSR policy belong in this evaluator.
struct ClothShadingData
{
    vec3 albedo;
    vec3 normal;
    vec3 viewDirection;
    float roughness;
};

// Tools/Rendering/GenerateClothEnergyLUT.py owns the integration convention.
layout(set = 0, binding = 8) uniform sampler2D ClothBRDFLUT;

vec3 ClothFresnelSchlick(float cosine, vec3 F0)
{
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosine, 0.0, 1.0), 5.0);
}

float D_Charlie(float roughness, float NoH)
{
    float alpha = max(roughness * roughness, 1e-4);
    float invAlpha = 1.0 / alpha;
    float sin2h = max(1.0 - NoH * NoH, 1e-6);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

float CharlieLambdaFit(float x, float alphaG)
{
    float oneMinusAlphaSq = (1.0 - alphaG) * (1.0 - alphaG);
    float a = mix(21.5473, 25.3245, oneMinusAlphaSq);
    float b = mix(3.82987, 3.32435, oneMinusAlphaSq);
    float c = mix(0.19823, 0.16801, oneMinusAlphaSq);
    float d = mix(-1.97760, -1.27393, oneMinusAlphaSq);
    float e = mix(-4.32054, -4.85967, oneMinusAlphaSq);
    return a / (1.0 + b * pow(max(x, 1e-4), c)) + d * x + e;
}

float CharlieLambda(float cosTheta, float alphaG)
{
    float x = clamp(abs(cosTheta), 1e-4, 1.0);
    return x < 0.5
        ? exp(CharlieLambdaFit(x, alphaG))
        : exp(2.0 * CharlieLambdaFit(0.5, alphaG) - CharlieLambdaFit(1.0 - x, alphaG));
}

float V_Charlie(float roughness, float NoV, float NoL)
{
    float alphaG = max(roughness * roughness, 1e-4);
    float denominator = (1.0 + CharlieLambda(NoV, alphaG) + CharlieLambda(NoL, alphaG))
        * (4.0 * max(NoV, 1e-4) * max(NoL, 1e-4));
    return 1.0 / max(denominator, 1e-5);
}

float Max3Cloth(vec3 v)
{
    return max(max(v.x, v.y), v.z);
}

vec3 ClothSheenColor(ClothMaterialPayload cloth)
{
    return cloth.sheenColorWeight.rgb * cloth.sheenColorWeight.a;
}

vec4 ClothEnergyTerms(float NoX, float sheenRoughness)
{
    // Tools/Rendering/GenerateClothEnergyLUT.py: endpoint-centered sampling,
    // with extra resolution at grazing angles. R=normalization, B=integral.
    vec2 coordinate = vec2(pow(clamp((NoX - 1e-4) / 0.9999, 0.0, 1.0), 1.0 / 3.0),
        clamp((sheenRoughness - 0.045) / 0.955, 0.0, 1.0));
    vec2 size = vec2(textureSize(ClothBRDFLUT, 0));
    return texture(ClothBRDFLUT, (coordinate * (size - 1.0) + 0.5) / size);
}

vec2 ClothSheenIntegral(float NoX, float sheenRoughness)
{
    return ClothEnergyTerms(NoX, sheenRoughness).rb;
}

float ClothDiffuseReflectanceScale(float NoV, ClothMaterialPayload cloth)
{
    // 与直接 BSDF 相同的 (1-Fv)(1-Fl)(1-w*max(Ev,El)) 余弦半球积分。
    // 避免旧路径把入射 Fresnel 也近似成 Fv，导致掠射漫反射过暗。
    const float fresnelMean = 0.04 + 0.96 / 21.0;
    vec4 energy = ClothEnergyTerms(NoV, cloth.transmittanceRoughness.a);
    float weight = Max3Cloth(ClothSheenColor(cloth));
    float specularWeight = cloth.surface.x;
    float viewTransmission = 1.0 - ClothFresnelSchlick(NoV, vec3(0.04)).x * specularWeight;
    return max(viewTransmission * (1.0 - specularWeight * fresnelMean - weight * energy.g +
        specularWeight * weight * energy.a * fresnelMean), 0.0);
}

float ClothDirectionalAlbedo(float NoX, float sheenRoughness)
{
    return ClothSheenIntegral(NoX, sheenRoughness).y;
}

float D_AnisotropicGGX(vec3 N, vec3 T, vec3 B, vec3 H, float alphaT, float alphaB)
{
    float ToH = dot(T, H);
    float BoH = dot(B, H);
    float NoH = max(dot(N, H), 0.0);
    float d = ToH * ToH / max(alphaT * alphaT, 1e-6)
            + BoH * BoH / max(alphaB * alphaB, 1e-6)
            + NoH * NoH;
    return 1.0 / max(PI * alphaT * alphaB * d * d, 1e-6);
}

float V_AnisotropicGGXCorrelated(vec3 N, vec3 T, vec3 B, vec3 V, vec3 L,
                                 float alphaT, float alphaB)
{
    float NoV = max(dot(N, V), 1e-4);
    float NoL = max(dot(N, L), 1e-4);
    float lambdaV = NoL * length(vec3(alphaT * dot(T, V), alphaB * dot(B, V), NoV));
    float lambdaL = NoV * length(vec3(alphaT * dot(T, L), alphaB * dot(B, L), NoL));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

void ClothAnisotropicRoughness(float roughness, float anisotropy,
                               out float alphaT, out float alphaB)
{
    float alpha = max(roughness * roughness, 0.002);
    float aspect = sqrt(max(1.0 - 0.9 * anisotropy, 0.1));
    float along = max(alpha / aspect, 0.002);
    float across = max(alpha * aspect, 0.002);
    alphaT = along;
    alphaB = across;
}

// 薄层背光沿光线一侧进行接收者偏置，不能把相机一侧法线交给阴影查询。
vec3 ClothLightFacingNormal(vec3 normal, vec3 lightDirection)
{
    return dot(normal,lightDirection) < 0.0 ? -normal : normal;
}

void BuildClothOpticalFrame(vec3 normal, vec3 viewDirection, vec3 clothTangent,
    out vec3 N, out vec3 T, out vec3 B)
{
    N = normalize(normal);
    if (dot(N, viewDirection) < 0.0) N = -N;
    T = clothTangent - N * dot(N, clothTangent);
    if (dot(T, T) <= 1e-8)
    {
        vec3 fallbackB;
        BuildStableClothFrame(N, T, fallbackB);
    }
    else
        T = normalize(T);
    B = normalize(cross(N, T));
}

void DirectBRDF_Cloth(ClothShadingData brdf, ClothMaterialPayload cloth,
                      vec3 clothTangent, vec3 lightDirection,
                      inout vec3 diffuse, inout vec3 specular)
{
    diffuse = vec3(0.0);
    specular = vec3(0.0);
    vec3 N, T, B;
    BuildClothOpticalFrame(brdf.normal, brdf.viewDirection, clothTangent, N, T, B);
    vec3 V = normalize(brdf.viewDirection);
    vec3 L = normalize(lightDirection);

    float NoV = max(dot(N, V), 1e-4);
    float signedNoL = dot(N, L);
    float frontNoL = max(signedNoL, 0.0);
    float backNoL = max(-signedNoL, 0.0);
    float roughness = clamp(brdf.roughness, 0.045, 1.0);
    vec3 sheenColor = ClothSheenColor(cloth);
    float sheenRoughness = cloth.transmittanceRoughness.a;

    if (frontNoL > 0.0)
    {
        vec3 halfSum = L + V;
        if (dot(halfSum, halfSum) <= 1e-8)
            return;
        vec3 H = normalize(halfSum);
        float NoH = clamp(dot(N, H), 0.0, 1.0);
        float VoH = clamp(dot(V, H), 0.0, 1.0);

        float sheenD = D_Charlie(sheenRoughness, NoH);
        float sheenV = V_Charlie(sheenRoughness, NoV, frontNoL);
        vec2 sheenViewIntegral = ClothSheenIntegral(NoV, sheenRoughness);
        vec2 sheenLightIntegral = ClothSheenIntegral(frontNoL, sheenRoughness);
        float sheenNormalization = min(sheenViewIntegral.x, sheenLightIntegral.x);
        vec3 sheen = sheenColor * sheenD * sheenV * sheenNormalization;
        float baseScale = clamp(1.0 - Max3Cloth(sheenColor) *
            max(sheenViewIntegral.y, sheenLightIntegral.y), 0.0, 1.0);

        if (cloth.surface.x > 0.0)
        {
            float alphaT;
            float alphaB;
            ClothAnisotropicRoughness(roughness, cloth.surface.y, alphaT, alphaB);
            float D = D_AnisotropicGGX(N, T, B, H, alphaT, alphaB);
            float Vis = V_AnisotropicGGXCorrelated(N, T, B, V, L, alphaT, alphaB);
            vec3 F = ClothFresnelSchlick(VoH, vec3(0.04)) * cloth.surface.x;
            vec3 Fv = ClothFresnelSchlick(NoV, vec3(0.04)) * cloth.surface.x;
            vec3 Fl = ClothFresnelSchlick(frontNoL, vec3(0.04)) * cloth.surface.x;
            vec3 baseDiffuse = (vec3(1.0) - Fv) * (vec3(1.0) - Fl) * brdf.albedo / PI;
            diffuse = baseDiffuse * baseScale * frontNoL;
            specular = (D * Vis * F * baseScale + sheen) * frontNoL;
        }
        else
        {
            diffuse = brdf.albedo * (baseScale / PI) * frontNoL;
            specular = sheen * frontNoL;
        }
    }

    // 反射与背面透射共用逐通道预算，不叠加额外能量。
    vec3 tau = cloth.transmittanceRoughness.rgb;
    diffuse = diffuse * (vec3(1.0) - tau) + tau * (backNoL / PI);
    specular *= vec3(1.0) - tau;
}

#endif
