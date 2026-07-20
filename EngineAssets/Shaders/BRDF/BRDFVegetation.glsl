#ifndef BRDF_VEGETATION_INCLUDED
#define BRDF_VEGETATION_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Vegetation BRDF
//   - 正面：能量受控的 wrap diffuse，减少叶片/草片法线抖动导致的硬黑。
//   - 背面：薄片透射，光从叶背穿过时贡献带 albedo 色相的 diffuse。
//   - 高光：只在视线侧法线计算 GGX，使用较低的叶面蜡质 F0，避免塑料感。
//
// 这类模型接近 Unreal Two Sided Foliage / Frostbite vegetation 的工程近似：
// 它不是体散射，而是为 alpha-cutout 薄片提供稳定的双面接收光照。
// =============================================================================

struct VegetationParams
{
    float translucency;     // [0,1] 透光强度，草/叶片高，树干为 0
    float scatterWidth;     // wrap diffuse 宽度，越大暗面越柔
    float sssDistortion;    // 预留：用于后续扭曲透射法线
    float sssAmbient;       // 背光透射的最低环境量
    float sssPower;         // 视线-光线 forward scatter 锥形指数
};

// Wrap diffuse：把 Lambert 的 N·L 向暗面扩展。
// 使用 (N·L + w) / (1 + w)^2，可避免 wrap 变宽后总能量无限增加。
float WrapDiffuse(vec3 N, vec3 L, float wrap)
{
    float NoL = dot(N, L);
    float denom = (1.0 + wrap) * (1.0 + wrap);
    return max(0.0, (NoL + wrap) / denom);
}

// 薄片透射近似：
//   backNoL 表示光从几何背面打入叶片，forwardScatter 表示视线接近透射方向。
//   两者组合可得到常见的逆光叶缘/草尖发亮，同时不会无条件抬亮所有暗面。
vec3 ThinSurfaceTransmission(vec3 V, vec3 L, vec3 N, VegetationParams veg, vec3 albedo)
{
    float trans = clamp(veg.translucency, 0.0, 1.0);
    if (trans <= 0.0)
        return vec3(0.0);

    float backNoL = max(dot(-N, L), 0.0);
    float forwardScatter = pow(max(dot(V, -L), 0.0), max(veg.sssPower, 1.0));
    float broadBackLight = pow(backNoL, 0.45);
    float scatter = broadBackLight * mix(0.65, 1.0, forwardScatter);
    scatter += clamp(veg.sssAmbient, 0.0, 1.0) * 0.15;

    return albedo * (scatter * trans / PI);
}

void DirectBRDF_Vegetation(BRDFData brdf, vec3 lightDirection, VegetationParams veg,
                           inout vec3 diffuse, inout vec3 specular,
                           inout vec3 subsurfaceTransmission)
{
    vec3 V = normalize(brdf.viewDirection);
    vec3 N = normalize(brdf.normal);
    vec3 L = normalize(lightDirection);
    vec3 Nf = (dot(N, V) < 0.0) ? -N : N;
    vec3 H = normalize(V + L);

    float VoH = clamp(dot(V, H), 0.0, 1.0);
    float NoV = max(dot(Nf, V), 0.001);
    float NoL = max(dot(Nf, L), 0.0);

    float metallic = clamp(brdf.metallic, 0.0, 1.0);
    vec3 dielectricF0 = mix(vec3(0.035), vec3(0.08), clamp(veg.translucency, 0.0, 1.0));
    vec3 F0 = mix(dielectricF0, brdf.albedo, metallic);
    vec3 F = FresnelSchlick(VoH, F0);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    float wrap = clamp(veg.scatterWidth, 0.0, 0.9);
    float wrappedNoL = WrapDiffuse(Nf, L, wrap);
    float diffuseNoL = mix(NoL, wrappedNoL, 0.75);
    diffuse = brdf.albedo * kD * (diffuseNoL / PI);

    // 透射项与正面漫反射分开累加：只有光从叶片背面入射时才显著贡献。
    subsurfaceTransmission = ThinSurfaceTransmission(V, L, Nf, veg, brdf.albedo);

    // 叶面蜡质高光：仍按 microfacet GGX，只使用视线侧法线，避免背面翻光。
    if (NoL > 0.0 && VoH > 0.0)
    {
        float specRough = clamp(brdf.roughness, 0.18, 0.75);
        float D = DistributionTrowbridgeReitzGGX(Nf, H, specRough);
        float G = GeometrySmith(Nf, V, L, specRough);

        float denom = max(4.0 * NoL * NoV, 0.001);
        specular = (D * G * F / denom) * NoL;
    }
}

void CalculateDirectLight_Vegetation(BRDFData brdfData, VegetationParams veg,
                                     sampler2DArray cascadeShadowMap, float viewDepth,
                                     sampler2D punctualShadowMap,
                                     float screenSpaceShadow,
                                     inout LightResult lightResult)
{
    lightResult.directDiffuse  = vec3(0);
    lightResult.directSpecular = vec3(0);

    {
        vec3 dR = vec3(0), sR = vec3(0), tR = vec3(0);
        DirectBRDF_Vegetation(brdfData, uDirectionLight.direction.rgb, veg, dR, sR, tR);

        vec3 lightEnergy = uDirectionLight.color.rgb * uDirectionLight.intensity;
        float shadow = min(SampleCascadeShadow(brdfData.positionWS, brdfData.normal, cascadeShadowMap, viewDepth), screenSpaceShadow);

        lightResult.directDiffuse  += (dR + tR) * lightEnergy * shadow;
        lightResult.directSpecular += sR * lightEnergy * shadow;
    }

    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3 lightDirection = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;

        float pShadow = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));

        vec3 dR = vec3(0), sR = vec3(0), tR = vec3(0);
        DirectBRDF_Vegetation(brdfData, lightDirection, veg, dR, sR, tR);

        vec3 lightEnergy = pointLight.color.rgb * pointLight.intensity * attenuation;
        lightResult.directDiffuse  += (dR + tR) * lightEnergy * pShadow;
        lightResult.directSpecular += sR * lightEnergy * pShadow;
    }

    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3 lightDirection = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        float sShadow = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        vec3 dR = vec3(0), sR = vec3(0), tR = vec3(0);
        DirectBRDF_Vegetation(brdfData, lightDirection, veg, dR, sR, tR);

        vec3 lightEnergy = spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        lightResult.directDiffuse  += (dR + tR) * lightEnergy * sShadow;
        lightResult.directSpecular += sR * lightEnergy * sShadow;
    }
}

void AmbientBRDF_Vegetation(BRDFData brdf, vec3 viewDirection,
                            inout vec3 diffuse, inout vec3 specular)
{
    // SSGI/probe 已在接收端为薄片植被做双面环境光采样；这里仅调整蜡质 F0。
    vec3 savedFresnel = brdf.fresnel0;
    brdf.fresnel0 = vec3(0.06);
    AmbientBRDF(brdf, viewDirection, diffuse, specular);
    brdf.fresnel0 = savedFresnel;
}

#endif // BRDF_VEGETATION_INCLUDED
