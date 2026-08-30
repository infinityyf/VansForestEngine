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
    vec3  subsurfaceColor;  // UE-style subsurface color, includes transmission mask strength
    float opacity;
    float wrap;
    float scatterRoughness;
    float transmissionScale;
    float specularScale;
};

float D_GGX_Scalar(float a2, float x)
{
    float d = (x * a2 - x) * x + 1.0;
    return a2 / max(PI * d * d, 1e-4);
}

vec3 TwoSidedFoliageTransmission(BRDFData brdf, vec3 lightDirection,
                                 VegetationParams veg, float lightFalloff,
                                 float transmissionShadow)
{
    if (veg.transmissionScale <= 0.0 || max(max(veg.subsurfaceColor.r, veg.subsurfaceColor.g), veg.subsurfaceColor.b) <= 0.0)
        return vec3(0.0);

    vec3 V = normalize(brdf.viewDirection);
    vec3 N = normalize(brdf.normal);
    vec3 L = normalize(lightDirection);

    float wrap = clamp(veg.wrap, 0.0, 0.9);
    float wrapNoL = clamp((-dot(N, L) + wrap) / ((1.0 + wrap) * (1.0 + wrap)), 0.0, 1.0);

    float minusVoL = clamp(-dot(V, L), 0.0, 1.0);
    float roughness = max(veg.scatterRoughness, 0.05);
    float scatter = D_GGX_Scalar(roughness * roughness, minusVoL);

    return veg.subsurfaceColor
         * wrapNoL
         * scatter
         * max(lightFalloff, 0.0)
         * clamp(transmissionShadow, 0.0, 1.0)
         * max(veg.transmissionScale, 0.0);
}

void DirectBRDF_Vegetation(BRDFData brdf, vec3 lightDirection, VegetationParams veg,
                           float lightFalloff, float shadow,
                           inout vec3 diffuse, inout vec3 specular,
                           inout vec3 subsurfaceTransmission)
{
    BRDFData local = brdf;
    vec3 V = normalize(local.viewDirection);
    vec3 N = normalize(local.normal);
    local.normal = (dot(N, V) < 0.0) ? -N : N;
    local.metallic = 0.0;
    local.fresnel0 = vec3(0.04);

    DirectBRDF(local, normalize(lightDirection), diffuse, specular);
    specular *= max(veg.specularScale, 0.0);

    subsurfaceTransmission = TwoSidedFoliageTransmission(
        brdf,
        normalize(lightDirection),
        veg,
        lightFalloff,
        shadow);
}

void CalculateDirectLight_Vegetation(BRDFData brdfData, VegetationParams veg,
                                     sampler2DShadow punctualShadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT],
									 float directionalShadow,
                                     inout LightResult lightResult)
{
    lightResult.directDiffuse  = vec3(0);
    lightResult.directSpecular = vec3(0);

    {
        vec3 dR = vec3(0), sR = vec3(0), tR = vec3(0);
		float shadow = directionalShadow;
        DirectBRDF_Vegetation(brdfData, uDirectionLight.direction.rgb, veg, 1.0, shadow, dR, sR, tR);

        vec3 lightEnergy =
            uDirectionLight.color.rgb * uDirectionLight.intensity;
        lightResult.directDiffuse  += (dR * shadow + tR) * lightEnergy;
        lightResult.directSpecular += sR * shadow * lightEnergy;
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
        if (IsPointShadowFallbackSelected(GetFragTileLightHeader(), i))
        {
            pShadow = BlendPunctualShadowFallback(
                pShadow,
                SamplePunctualScreenSpaceShadow(brdfData.positionWS, brdfData.normal, lightDirection, distance),
                0u, int(i));
        }

        vec3 dR = vec3(0), sR = vec3(0), tR = vec3(0);
        DirectBRDF_Vegetation(brdfData, lightDirection, veg, attenuation, pShadow, dR, sR, tR);

        vec3 lightEnergy = pointLight.color.rgb * pointLight.intensity;
        lightResult.directDiffuse  += (dR * pShadow * attenuation + tR) * lightEnergy;
        lightResult.directSpecular += sR * pShadow * attenuation * lightEnergy;
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

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        float sShadow = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
        if (IsSpotShadowFallbackSelected(GetFragTileLightHeader(), i))
        {
            sShadow = BlendPunctualShadowFallback(
                sShadow,
                SamplePunctualScreenSpaceShadow(brdfData.positionWS, brdfData.normal, lightDirection, distance),
                1u, int(i));
        }

        vec3 dR = vec3(0), sR = vec3(0), tR = vec3(0);
        float falloff = attenuation * coneAttenuation;
        DirectBRDF_Vegetation(brdfData, lightDirection, veg, falloff, sShadow, dR, sR, tR);

        vec3 lightEnergy = spotLight.color.rgb * spotLight.intensity;
        lightResult.directDiffuse  += (dR * sShadow * falloff + tR) * lightEnergy;
        lightResult.directSpecular += sR * sShadow * falloff * lightEnergy;
    }
}

void AmbientBRDF_Vegetation(BRDFData brdf, VegetationParams veg, vec3 viewDirection,
                            inout vec3 diffuse, inout vec3 specular)
{
    BRDFData local = brdf;
    vec3 N = normalize(local.normal);
    vec3 V = normalize(viewDirection);
    local.normal = (dot(N, V) < 0.0) ? -N : N;
    local.fresnel0 = vec3(0.04);
    local.metallic = 0.0;
    AmbientBRDF(local, viewDirection, diffuse, specular);
    specular *= max(veg.specularScale, 0.0);
}

#endif // BRDF_VEGETATION_INCLUDED
