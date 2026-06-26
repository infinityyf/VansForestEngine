#ifndef BRDF_SKIN_INCLUDED
#define BRDF_SKIN_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Skin / Subsurface Scattering BRDF (Material ID = MATERIAL_ID_SKIN)
//   - Pre-integrated subsurface scattering (diffusion profile LUT)
//   - Kelemen/Szirmay-Kalos specular (runtime Beckmann NDF, no LUT)
//   - Shadow-map thickness estimation for transmission / back-lighting
// =============================================================================


// ---------------------------------------------------------------------------
// Sample the pre-integrated SSS LUT.
// U = NdotL remapped from [-1,1] to [0,1],  V = curvature [0,1].
// Returns the pre-integrated scattering color.
// ---------------------------------------------------------------------------
vec3 SampleSSSLUT(float NdotL, float curvature)
{
    float u = NdotL * 0.5 + 0.5;   // Map [-1,1] -> [0,1]
    float v = clamp(curvature, 0.0, 1.0);
    return texture(SkinPreIntegratedLUT, vec2(u, v)).rgb;
}

// ---------------------------------------------------------------------------
// Subsurface scattering tint based on surface curvature.
// Thin areas (high curvature: ears, nostrils, fingers) are tinted by
// sssColor (typically reddish from blood vessels underneath).
// Thick/flat areas stay neutral.
//
// The tint is intentionally NOT NdotL-dependent — the pre-integrated
// SSS LUT already encodes the smooth color shift across the terminator.
// Adding another NdotL curve on top creates visible jump discontinuities.
// ---------------------------------------------------------------------------
vec3 ComputeSkinSSSColor(float curvature, vec3 sssColor, float sssScale)
{
    float sssAmount = curvature * sssScale;
    return mix(vec3(1.0), sssColor, clamp(sssAmount, 0.0, 1.0));
}

// ---------------------------------------------------------------------------
// Compute skin-specific Fresnel reflectance (F0).
// Skin has ~2.8% reflectance at normal incidence; the oil / sweat layer
// can raise this slightly.  A small albedo tint is mixed in for realism.
// ---------------------------------------------------------------------------
vec3 ComputeSkinF0(vec3 albedo)
{
    vec3 F0 = vec3(0.028);              // Base skin specular reflectance
    return mix(F0, albedo, 0.2);        // Slight tint from skin color
}

// ---------------------------------------------------------------------------
// Beckmann Normal Distribution Function – computed at runtime.
// Replaces the pre-computed 2D LUT from Kelemen & Szirmay-Kalos.
// NdotH : dot(N, H) clamped,  m : roughness parameter.
// Reference: Kelemen & Szirmay-Kalos, "A Microfacet Based Coupled
//            Specular-Matte BRDF Model with Importance Sampling", EG 2001.
// ---------------------------------------------------------------------------
float PHBeckmann(float NdotH, float m)
{
    // Guard: m (roughness) must be > 0 to avoid division by zero
    m = max(m, 0.001);
    float NdotH_safe = clamp(NdotH, 0.001, 1.0);  // avoid tan(PI/2) = Inf
    float alpha = acos(NdotH_safe);
    float ta    = tan(alpha);
    float m2    = m * m;
    float NdotH4 = NdotH_safe * NdotH_safe * NdotH_safe * NdotH_safe;
    // 修正: 标准 Beckmann NDF 分母包含 PI（Walter 2007）
    // D(m) = exp(-tan²θ/m²) / (PI * m² * cos⁴θ)
    float val   = (1.0 / (PI * m2 * NdotH4)) * exp(-(ta * ta) / m2);
    return val;
}

// ---------------------------------------------------------------------------
// Fresnel reflectance (Schlick approximation) – scalar version for skin.
// F0 ~ 0.028 for skin (IOR ~ 1.4).
// ---------------------------------------------------------------------------
float KS_Fresnel(vec3 H, vec3 V, float F0)
{
    float base        = 1.0 - max(dot(V, H), 0.0);
    float exponential = pow(base, 5.0);
    return exponential + F0 * (1.0 - exponential);
}

// ---------------------------------------------------------------------------
// Kelemen & Szirmay-Kalos skin specular BRDF.
// Uses runtime-computed Beckmann NDF instead of a precomputed texture LUT.
// N     : bumped surface normal
// L     : direction to light
// V     : direction to eye
// m     : roughness (Beckmann)
// rho_s : specular brightness / intensity
// ---------------------------------------------------------------------------
float KS_Skin_Specular(vec3 N, vec3 L, vec3 V, float m, float rho_s)
{
    float result = 0.0;
    float NdotL  = dot(N, L);
    if (NdotL > 0.0)
    {
        vec3  h      = L + V;               // unnormalized half-vector
        float hLenSq = dot(h, h);
        // Guard: when L and V are nearly opposite, h ≈ 0 → skip to avoid Inf
        if (hLenSq < 1e-6) return 0.0;
        vec3  H     = h * inversesqrt(hLenSq);
        float NdotH = dot(N, H);
        // Runtime path: use PHBeckmann directly.
        // The original LUT stored 0.5*pow(PH,0.1) and read back via
        // pow(2.0*texValue,10.0) — the encode/decode cancel to just PH.
        float PH    = PHBeckmann(NdotH, m);
        float F     = KS_Fresnel(H, V, 0.028);
        // 修正: KSK 论文标准化分母为 2*|L+V|，而非 |L+V|²
        // fr_specular = D(θh) * F(cosθd) / (2 * |L+V|)
        float frSpec = max(PH * F / (2.0 * sqrt(hLenSq)), 0.0);
        // Clamp to prevent specular fireflies in extreme configurations
        result = min(NdotL * rho_s * frSpec, 16.0);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Subsurface transmission through thin skin (ears, nostrils, etc.).
// Uses an exponential absorption profile: red scatters furthest, blue least.
// ---------------------------------------------------------------------------
vec3 ComputeSkinTransmission(float thickness, vec3 albedo, vec3 lightColor, float lightIntensity)
{
    // Per-channel absorption coefficients (blood / tissue)
    vec3 sigma = vec3(1.0, 3.0, 5.0);
    float scale = 30.0; // overall falloff speed

    vec3 transmission = exp(-sigma * thickness * scale);
    return transmission * albedo * lightColor * lightIntensity;
}

// ---------------------------------------------------------------------------
// Skin direct BRDF – pre-integrated diffuse + KS specular.
// Specular uses Kelemen/Szirmay-Kalos model with runtime Beckmann NDF.
// ---------------------------------------------------------------------------
void DirectBRDF_Skin(BRDFData brdf, vec3 lightDirection, float curvature,
                     inout vec3 diffuse, inout vec3 specular)
{
    vec3  viewDirection = brdf.viewDirection;
    float NdotL = dot(brdf.normal, lightDirection);       // may be negative

    // --- Pre-integrated skin diffuse (wraps NdotL via LUT) ---
    vec3 skinScatter = SampleSSSLUT(NdotL, curvature);

    // Curvature-only SSS tint: thin areas (ears, nose) get reddish blood-vessel color
    vec3 sssTint = ComputeSkinSSSColor(curvature, vec3(1.0, 0.2, 0.1), 1.0);
    diffuse = skinScatter * brdf.albedo * sssTint / PI;

    // --- Kelemen/Szirmay-Kalos skin specular (runtime Beckmann NDF) ---
    // Reference: Kelemen & Szirmay-Kalos, "A Microfacet Based Coupled
    //            Specular-Matte BRDF Model with Importance Sampling", EG 2001.
    // rho_s = 1.0 (specular brightness); light intensity applied externally.
    float ksSpec = KS_Skin_Specular(brdf.normal, lightDirection, viewDirection,
                                     brdf.roughness, 1.0);
    specular = vec3(ksSpec);
}

// ---------------------------------------------------------------------------
// [Original] Skin direct BRDF – dual-lobe Cook-Torrance specular variant.
// Kept for reference / A-B comparison.  Switch callers to this if preferred.
// ---------------------------------------------------------------------------
void DirectBRDF_Skin_DualLobe(BRDFData brdf, vec3 lightDirection, float curvature,
                              inout vec3 diffuse, inout vec3 specular)
{
    vec3  viewDirection = brdf.viewDirection;
    vec3  halfVector    = normalize(lightDirection + viewDirection);
    float NdotL = dot(brdf.normal, lightDirection);       // may be negative
    float NdotV = max(dot(brdf.normal, viewDirection), 0.0);
    float NdotH = max(dot(brdf.normal, halfVector),    0.0);

    // --- Pre-integrated skin diffuse (wraps NdotL via LUT) ---
    vec3 skinScatter = SampleSSSLUT(NdotL, curvature);

    // Curvature-only SSS tint: thin areas (ears, nose) get reddish blood-vessel color
    vec3 sssTint = ComputeSkinSSSColor(curvature, vec3(1.0, 0.2, 0.1), 1.0);
    diffuse = skinScatter * brdf.albedo * sssTint / PI;

    // --- Dual-lobe specular ---
    /*
    Eugene d'Eon & David Luebke — "Advanced Techniques for Realistic Real-Time Skin Rendering", GPU Gems 3, Chapter 14, NVIDIA 2007.
    */
    // Skin-specific F0 (~2.8% base reflectance, slight albedo tint)
    vec3 F0 = ComputeSkinF0(brdf.albedo);
    vec3 F  = FresnelSchlick(NdotH, F0);

    // Primary lobe – original roughness
    float D1 = DistributionTrowbridgeReitzGGX(brdf.normal, halfVector, brdf.roughness);
    float G1 = GeometrySmith(brdf.normal, viewDirection, lightDirection, brdf.roughness);

    // Secondary lobe – wider / softer sheen
    float roughness2 = clamp(brdf.roughness * 2.0, 0.0, 1.0);
    float D2 = DistributionTrowbridgeReitzGGX(brdf.normal, halfVector, roughness2);
    float G2 = GeometrySmith(brdf.normal, viewDirection, lightDirection, roughness2);

    // Cook-Torrance: contribution = (D*G*F / (4*NdotV)) * NdotL
    float NdotL_clamped = max(NdotL, 0.0);
    // 修正: 分母加入 NdotL，与 DirectBRDF 保持一致，避免双重 NdotL 衰减
    // Cook-Torrance: (D*G*F) / (4*NdotL*NdotV) * NdotL = (D*G*F) / (4*NdotV)
    float denom         = 4.0 * max(NdotV, 0.001) * max(NdotL_clamped, 0.001);
    vec3  spec1         = (D1 * G1 * F) / denom * NdotL_clamped;
    vec3  spec2         = (D2 * G2 * F) / denom * NdotL_clamped * 0.3;

    specular = spec1 + spec2;
}

// ---------------------------------------------------------------------------
// Ambient for skin – standard PBR fallback (SSS mainly affects direct light).
// ---------------------------------------------------------------------------
void AmbientBRDF_Skin(BRDFData brdf, vec3 viewDirection, inout vec3 diffuse, inout vec3 specular)
{
    AmbientBRDF(brdf, viewDirection, diffuse, specular);
}

// ---------------------------------------------------------------------------
// Main skin lighting (directional + punctual).
// Directional light uses pre-integrated SSS diffuse + thickness transmission.
// Point / spot lights fall back to standard DirectBRDF for now.
// ---------------------------------------------------------------------------
// Cascade shadow map version of skin lighting
void CalculateDirectLight_Skin(BRDFData brdfData, float curvature,
                               sampler2DArray cascadeShadowMap, float viewDepth, sampler2D punctualShadowMap,
                               float screenSpaceShadow,
                               inout LightResult lightResult)
{
    lightResult.directDiffuse  = vec3(0);
    lightResult.directSpecular = vec3(0);

    // ===================== Directional light (skin-specific) =====================
    vec3 diffuseResult  = vec3(0);
    vec3 specularResult = vec3(0);
    float NdotL_dir = dot(brdfData.normal, uDirectionLight.direction.rgb);
    DirectBRDF_Skin(brdfData, uDirectionLight.direction.rgb, curvature,
                    diffuseResult, specularResult);
    diffuseResult  *= uDirectionLight.color.rgb * uDirectionLight.intensity;
    specularResult *= uDirectionLight.color.rgb * uDirectionLight.intensity;

    float shadowValue = min(SampleCascadeShadow(brdfData.positionWS, brdfData.normal, cascadeShadowMap, viewDepth), screenSpaceShadow);

    diffuseResult  *= shadowValue;
    specularResult *= shadowValue;

    lightResult.directDiffuse  += diffuseResult;
    lightResult.directSpecular += specularResult;

    // ===================== Point lights (standard fallback) =====================
    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3  lightDirection = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;

        float pShadow = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(pointLight.shadowIndex));
        attenuation = min(attenuation, pShadow);

        vec3 dR = vec3(0);
        vec3 sR = vec3(0);
        DirectBRDF_Skin(brdfData, lightDirection, curvature, dR, sR);
        dR *= pointLight.color.rgb * pointLight.intensity * attenuation;
        sR *= pointLight.color.rgb * pointLight.intensity * attenuation;

        lightResult.directDiffuse  += dR;
        lightResult.directSpecular += sR;
    }

    // ===================== Spot lights (standard fallback) =====================
    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3  lightDirection = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        float sShadow = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(spotLight.shadowIndex));
        attenuation = min(attenuation, sShadow);

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        vec3 dR = vec3(0);
        vec3 sR = vec3(0);
        DirectBRDF_Skin(brdfData, lightDirection, curvature, dR, sR);
        dR *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        sR *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

        lightResult.directDiffuse  += dR;
        lightResult.directSpecular += sR;
    }
}

#endif // BRDF_SKIN_INCLUDED
