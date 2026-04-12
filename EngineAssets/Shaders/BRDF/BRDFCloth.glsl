#ifndef BRDF_CLOTH_INCLUDED
#define BRDF_CLOTH_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Cloth BRDF  (Material ID = MATERIAL_ID_CLOTH = 5)
//
// Sheen NDF : Charlie (fabric / cotton / denim)  -- used for direct lighting
// ClothBRDFLUT channels:
//   R = split-sum term A  (F-independent lobe,   used as mix start)
//   G = split-sum term B  (F-dependent lobe,     used as mix end)
//   B = sheen tint (pre-baked directional sheen colour scale)
// E = mix(dfg.rrr, dfg.ggg, sheenColor)
//
// References:
//   Estevez & Kulla 2017 -- "Production Friendly Microfacet Sheen BRDF"
//   Ashikhmin & Premoze 2007 -- "Distribution-based BRDFs"
//   Neubelt & Pettineo 2013 -- "Crafting a Next-gen Material Pipeline..."
// =============================================================================

// ---------------------------------------------------------------------------
// Charlie NDF (fabric / cotton)  -  Estevez & Kulla 2017
// D_Charlie = (2 + 1/a) * sin^(1/a)(th) / (2*PI)
// ---------------------------------------------------------------------------
float D_Charlie(float roughness, float NoH)
{
    float invAlpha = 1.0 / max(roughness * roughness, 1e-4);
    float cos2h    = NoH * NoH;
    float sin2h    = max(1.0 - cos2h, 0.0078125);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

// ---------------------------------------------------------------------------
// Ashikhmin NDF (silk / satin)  -  Ashikhmin & Premoze 2007
// ---------------------------------------------------------------------------
float D_Ashikhmin(float roughness, float NoH)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float d  = (1.0 - NoH * NoH) * (1.0 + a2 * NoH * NoH);
    return max(0.0, (1.0 + 4.0 * exp(-2.0 / max(d, 1e-6))) / (PI * (1.0 + 4.0 * a2)));
}

// ---------------------------------------------------------------------------
// Neubelt visibility  -  Neubelt & Pettineo 2013
// V = 1 / (4 * (NoL + NoV - NoL*NoV))
// ---------------------------------------------------------------------------
float V_Cloth(float NoV, float NoL)
{
    float denom = 4.0 * (NoL + NoV - NoL * NoV);
    return 1.0 / max(denom, 1e-5);
}

// ---------------------------------------------------------------------------
// Sample the pre-baked cloth DFG LUT.
// U = NoV,  V = perceptual roughness
// R = split-sum DFG term A  (F-dependent part)
// G = split-sum DFG term B  (F-independent part)
// B = sheen tint            (pre-baked directional sheen colour scale)
// ---------------------------------------------------------------------------
vec3 SampleClothDFG(float NoV, float sheenRoughness)
{
    return texture(ClothBRDFLUT, vec2(clamp(NoV, 0.0, 1.0), clamp(sheenRoughness, 0.0, 1.0))).rgb;
}

// ---------------------------------------------------------------------------
// Direct cloth BRDF (per-light)
//   Uses Charlie NDF for the sheen specular lobe.
// ---------------------------------------------------------------------------
void DirectBRDF_Cloth(BRDFData brdf, vec3 lightDirection,
                      inout vec3 diffuse, inout vec3 specular)
{
    vec3  viewDir = brdf.viewDirection;
    vec3  H   = normalize(lightDirection + viewDir);
    float NoH = max(dot(brdf.normal, H),             0.0);
    float NoL = max(dot(brdf.normal, lightDirection), 0.0);
    float NoV = max(dot(brdf.normal, viewDir),        0.0);

    float D = D_Charlie(brdf.roughness, NoH);
    float V = V_Cloth(NoV, NoL);

    // Constant sheen colour: greyscale from roughness (no per-pixel tint yet)
    vec3 F = vec3(brdf.roughness);

    specular = D * V * F * NoL;
    diffuse  = (brdf.albedo / PI) * NoL;
}

// ---------------------------------------------------------------------------
// Ambient cloth BRDF (IBL path)
//   .rg = split-sum DFG (F-dependent / F-independent parts)
//   .b  = sheen tint scale (pre-baked directional sheen colour)
// ---------------------------------------------------------------------------
void AmbientBRDF_Cloth(BRDFData brdf, vec3 viewDirection, vec4 giVisSH,
                       inout vec3 diffuse, inout vec3 specular)
{
    float NoV = max(dot(brdf.normal, viewDirection), 0.0);

    vec3  lut       = SampleClothDFG(NoV, brdf.roughness);
    // Greyscale sheen colour (roughness-based until per-pixel tint added)
    // Filament-style cloth split-sum:  mix(dfg.rrr, dfg.ggg, f0)
    //   = dfg.r * (1 - sheenColor) + dfg.g * sheenColor
    vec3  sheenColor = vec3(brdf.roughness);
    vec3  E          = mix(lut.rrr, lut.ggg, sheenColor);  // split-sum specular energy
    vec3  sheenTint  = vec3(lut.b);                        // pre-baked sheen tint from .b

    // Specular: blend IBL cubemap with SSR result, modulated by sheen tint
    vec3  R        = reflect(-viewDirection, brdf.normal);
    float lod      = GetMipLevelFromRoughness(brdf.roughness);
    vec3  iblSpec  = textureLod(PreConvSpecularEnvironment, R, lod).rgb;
    // Attenuate cubemap by directional GI probe visibility (indoor occlusion)
    float giVis    = EvalGIVisibility(giVisSH, R);
    iblSpec *= giVis;
    vec3  ssrColor = brdf.indirectSpecular.rgb;
    // Roughness fade: SSR quality degrades on rough surfaces
    float ssrFade  = 1.0 - smoothstep(SSR_ROUGHNESS_FADE_START, SSR_ROUGHNESS_FADE_END, brdf.roughness);
    float ssrMask  = brdf.indirectSpecular.a * ssrFade;
    specular       = E * sheenTint * mix(iblSpec, ssrColor, ssrMask) * brdf.ao;

    // Diffuse: SSGI attenuated by sheen energy
    diffuse = brdf.albedo * brdf.indirectDiffuse * (1.0 - E) * brdf.ao;
}

// ---------------------------------------------------------------------------
// Full per-light loop for cloth — mirrors CalculateDirectLight_Skin structure.
// Included after LightsData.glsl so light uniforms / helpers are visible.
// ---------------------------------------------------------------------------
void CalculateDirectLight_Cloth(BRDFData brdf,
                                sampler2DArray cascadeShadowMap, float viewDepth,
                                sampler2D punctualShadowMap,
                                inout LightResult lightResult)
{
    lightResult.directDiffuse  = vec3(0);
    lightResult.directSpecular = vec3(0);

    // ---- Directional light ----
    {
        vec3 dR = vec3(0), sR = vec3(0);
        DirectBRDF_Cloth(brdf, uDirectionLight.direction.rgb, dR, sR);
        dR *= uDirectionLight.color.rgb * uDirectionLight.intensity;
        sR *= uDirectionLight.color.rgb * uDirectionLight.intensity;

        float shadow = SampleCascadeShadow(brdf.positionWS, cascadeShadowMap, viewDepth);
        lightResult.directDiffuse  += dR * shadow;
        lightResult.directSpecular += sR * shadow;
    }

    // ---- Point lights ----
    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pl  = GetPointLight(int(i));
        vec3  lightDir     = pl.position.xyz - brdf.positionWS;
        float dist         = length(lightDir);
        if (dist > pl.radius) continue;
        lightDir /= dist;

        float atten  = 1.0 - (dist / pl.radius);
        atten       *= atten;
        atten        = min(atten, SamplePointShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(pl.shadowIndex)));

        vec3 dR = vec3(0), sR = vec3(0);
        DirectBRDF_Cloth(brdf, lightDir, dR, sR);
        lightResult.directDiffuse  += dR * pl.color.rgb * pl.intensity * atten;
        lightResult.directSpecular += sR * pl.color.rgb * pl.intensity * atten;
    }

    // ---- Spot lights ----
    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData sl = GetSpotLight(int(i));
        vec3  lightDir   = sl.position.xyz - brdf.positionWS;
        float dist       = length(lightDir);
        if (dist > sl.radius) continue;
        lightDir /= dist;

        float atten  = 1.0 - (dist / sl.radius);
        atten       *= atten;
        atten        = min(atten, SampleSpotShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(sl.shadowIndex)));

        float coneAngle = dot(normalize(sl.direction.xyz), lightDir);
        if (coneAngle < cos(sl.outerConeAngle)) continue;
        float coneAtten = clamp((coneAngle - cos(sl.outerConeAngle)) /
                                (cos(sl.innerConeAngle) - cos(sl.outerConeAngle)), 0.0, 1.0);
        atten *= coneAtten;

        vec3 dR = vec3(0), sR = vec3(0);
        DirectBRDF_Cloth(brdf, lightDir, dR, sR);
        lightResult.directDiffuse  += dR * sl.color.rgb * sl.intensity * atten;
        lightResult.directSpecular += sR * sl.color.rgb * sl.intensity * atten;
    }
}

#endif // BRDF_CLOTH_INCLUDED