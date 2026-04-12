#ifndef BRDF_HAIR_INCLUDED
#define BRDF_HAIR_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// ============================================================================
// Hair BRDF — Marschner R / TT / TRT (UE-style)
// Material ID = MATERIAL_ID_HAIR
//
// Three-lobe physically-based hair scattering model:
//   R   — specular reflection off the cuticle surface
//   TT  — transmission through the fiber (refraction → exit)
//   TRT — internal reflection (refraction → internal bounce → exit)
//
// Based on:
//   [Marschner et al. 2003] Light Scattering from Human Hair Fibers
//   [d'Eon et al. 2011]     An Energy-Conserving Hair Reflectance Model
//   [Karis 2016]            Physically Based Hair Shading in Unreal
//
// GBuffer convention (written by Hair.frag):
//   outNormal.xyz   = shaded normal WS (tilted by flow map when present)
//   outNormal.w     = strand shift [0..1] → remapped to [-1,1]
//   outGBuffer0.rgb = albedo (hair base color, drives absorption)
//   outGBuffer0.w   = roughness (longitudinal width β)
//   outGBuffer1.x   = tangent oct X [0..1] (octahedral-encoded fiber direction,
//                      bent by flow map when present)
//   outGBuffer1.y   = ao
//   outGBuffer1.z   = MATERIAL_ID_HAIR
//   outGBuffer1.w   = tangent oct Y [0..1] (octahedral-encoded fiber direction)
//
//   specularStrength = 1.0  (constant, not stored in GBuffer)
//   scatter          = 0.35 (constant, not stored in GBuffer)
//   flowBend         = 0.0  (flow already baked into tangent; adjust to amplify shift)
// ============================================================================

// Hair fiber index of refraction (keratin)
#define HAIR_IOR 1.55

// Octahedral decode: vec2 in [-1,1] → unit vec3
vec3 OctDecodeHair(vec2 f) {
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}

struct HairBRDFParams
{
    vec3  tangentWS;
    float roughness;          // longitudinal roughness β
    float specularStrength;
    float scatter;            // TT / backlit transmission intensity
    float shift;              // cuticle tilt α in [-1, 1]
    float flowBend;           // flow-induced tangent bend strength [0, 1] (0 = no flow)
};

// ---------------------------------------------------------------------------
// Longitudinal scattering M — Gaussian on sinθ
// β = roughness width for the lobe, θ = sinThetaH - shift
// ---------------------------------------------------------------------------
float Hair_G(float beta, float theta)
{
    float b = max(beta, 0.001);
    return exp(-0.5 * theta * theta / (b * b)) / (sqrt(TWO_PI) * b);
}

// ---------------------------------------------------------------------------
// Dielectric Fresnel — Schlick approximation for η = 1.55
// F0 = ((1−η)/(1+η))² ≈ 0.0465
// ---------------------------------------------------------------------------
float Hair_F(float cosTheta)
{
    const float F0 = 0.0465;
    return F0 + (1.0 - F0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

// ---------------------------------------------------------------------------
// Main per-light BRDF: Marschner R + TT + TRT
//
//   S(ωi,ωr) = Σ_p  M_p(θ_h) · N_p(φ) · A_p  /  cos²θ_d
//
// Where:
//   M_p  = longitudinal Gaussian (different width & shift per lobe)
//   N_p  = azimuthal scattering distribution
//   A_p  = Fresnel × absorption attenuation
//   θ_h  = longitudinal half angle
//   θ_d  = longitudinal difference angle
//   φ    = azimuthal difference angle
// ---------------------------------------------------------------------------
void DirectBRDF_Hair(
    BRDFData brdfData,
    HairBRDFParams hair,
    vec3 lightDirection,
    out vec3 diffuseResult,
    out vec3 specularResult)
{
    vec3 N = normalize(brdfData.normal);
    vec3 V = normalize(brdfData.viewDirection);
    vec3 L = normalize(lightDirection);
    vec3 T = normalize(hair.tangentWS);

    // ── Flow-aware shift: tilt the tangent toward / away from the normal ─
    // The shift value bends the tangent along the normal direction.
    // Positive shift pushes the tangent toward the normal (cuticle tip → root),
    // negative shift pushes it away.  The flow bend strength scales how much
    // the shift is amplified when a flow map is active.
    float shiftStrength = hair.shift * (1.0 + hair.flowBend * 0.5);
    vec3 T_shifted = normalize(T + N * shiftStrength * 0.15);

    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0)
    {
        diffuseResult  = vec3(0.0);
        specularResult = vec3(0.0);
        return;
    }

    // ── Longitudinal angles relative to fiber tangent ────────────────────
    // Use the flow+shift-bent tangent for specular lobe placement.
    float sinThetaL = clamp(dot(T_shifted, L), -1.0, 1.0);
    float sinThetaV = clamp(dot(T_shifted, V), -1.0, 1.0);
    float cosThetaL = sqrt(max(1.0 - sinThetaL * sinThetaL, 0.0));
    float cosThetaV = sqrt(max(1.0 - sinThetaV * sinThetaV, 0.0));

    // Half/difference longitudinal angles
    float sinThetaH = (sinThetaL + sinThetaV) * 0.5;
    float cosThetaD = cos(0.5 * abs(
        asin(clamp(sinThetaV, -1.0, 1.0)) -
        asin(clamp(sinThetaL, -1.0, 1.0))));
    cosThetaD = max(cosThetaD, 0.001);

    // ── Azimuthal angle φ (angle between L and V in the normal plane) ───
    vec3 Lp = L - sinThetaL * T_shifted;   // project onto normal plane
    vec3 Vp = V - sinThetaV * T_shifted;
    float cosPhi = clamp(dot(Lp, Vp) / max(length(Lp) * length(Vp), 1e-5), -1.0, 1.0);
    float cosHalfPhi = sqrt(max(0.5 + 0.5 * cosPhi, 0.0));   // cos(φ/2)

    float VdotL = dot(V, L);

    // ── Cuticle shift α per lobe (Marschner convention) ─────────────────
    //   R   shifts by +α   (toward root)
    //   TT  shifts by −α/2 (toward tip)
    //   TRT shifts by −3α/2
    float alpha = hair.shift;
    float sinAlpha   = sin(alpha * 0.05);   // small angle (~3° at α=1)
    float sinAlphaH  = sin(alpha * 0.025);
    float sinAlpha3H = sin(alpha * 0.075);

    // ── Longitudinal roughness β per lobe ───────────────────────────────
    float betaR   = clamp(hair.roughness,       0.04, 1.0);
    float betaTT  = clamp(hair.roughness * 0.5, 0.02, 0.5);
    float betaTRT = clamp(hair.roughness * 2.0, 0.08, 2.0);

    // ── M: Longitudinal scattering (Gaussian) ──────────────────────────
    float M_R   = Hair_G(betaR,   sinThetaH + sinAlpha);     // R
    float M_TT  = Hair_G(betaTT,  sinThetaH - sinAlphaH);    // TT
    float M_TRT = Hair_G(betaTRT, sinThetaH - sinAlpha3H);   // TRT

    // ── N: Azimuthal scattering ─────────────────────────────────────────
    //   R:   cos(φ/2) / 4                (concentrated around mirror)
    //   TT:  exp(-3.65·cosφ − 3.98)      (focused behind the fiber)
    //   TRT: exp(-3.65·cosφ − 3.98) / 2π (broader internal-bounce lobe)
    float N_R   = 0.25 * cosHalfPhi;
    float N_TT  = exp(-3.65 * cosPhi - 3.98);
    float N_TRT = exp(-3.65 * cosPhi - 3.98) / TWO_PI;

    // ── A: Attenuation (Fresnel × Beer-Lambert absorption) ─────────────
    //
    // Absorption from hair base color:
    //   Transmittance T = pow(baseColor, path / cosThetaD)
    //   where path depends on how far light travels through the fiber.
    //
    // R:   A = F(cosThetaD)
    // TT:  A = (1−F)² · T(baseColor, 0.5·√(1−h²·a²) / cosThetaD)
    // TRT: A = F·(1−F)² · T(baseColor, 0.8 / cosThetaD)

    float cosTheta_F = cosThetaD * sqrt(max(0.5 + 0.5 * VdotL, 0.0));
    float f = Hair_F(cosTheta_F);

    // R — pure Fresnel reflection
    vec3 A_R = vec3(f) * 2.0 * hair.specularStrength;

    // TT — transmission with absorption
    float a_inv = 1.0 / HAIR_IOR;
    float h_TT  = cosHalfPhi * (1.0 + a_inv * (0.6 - 0.8 * cosPhi));
    float f_TT  = Hair_F(cosThetaD * sqrt(max(1.0 - h_TT * h_TT, 0.0)));
    float Fp_TT = (1.0 - f_TT) * (1.0 - f_TT);
    vec3  Tp_TT = pow(max(brdfData.albedo, vec3(0.001)),
                      vec3(0.5 * sqrt(max(1.0 - pow(h_TT * a_inv, 2.0), 0.0)) / cosThetaD));
    vec3  A_TT  = Fp_TT * Tp_TT;

    // TRT — internal reflection with double absorption path
    float f_TRT  = Hair_F(cosThetaD * 0.5);
    float Fp_TRT = (1.0 - f_TRT) * (1.0 - f_TRT) * f_TRT;
    vec3  Tp_TRT = pow(max(brdfData.albedo, vec3(0.001)),
                       vec3(0.8 / cosThetaD));
    vec3  A_TRT  = Fp_TRT * Tp_TRT;

    // ── Combine all three lobes ─────────────────────────────────────────
    //   S = Σ (M_p · N_p · A_p) / cos²θ_d
    float invCosThetaD2 = 1.0 / (cosThetaD * cosThetaD);

    vec3 S_R   = A_R   * (M_R   * N_R);
    vec3 S_TT  = A_TT  * (M_TT  * N_TT)  * hair.scatter;
    vec3 S_TRT = A_TRT * (M_TRT * N_TRT);

    specularResult = (S_R + S_TT + S_TRT) * invCosThetaD2 * NoL;

    // ── Diffuse: soft wrapped Lambert for fill on hair cards ────────────
    float wrap = 0.35;
    float wrappedNoL = max((dot(N, L) + wrap) / (1.0 + wrap), 0.0);
    diffuseResult = brdfData.albedo * wrappedNoL / PI;

    // ── TT back-scatter (fake transmission through the volume) ──────────
    float backScatter = pow(max(-VdotL, 0.0), mix(2.0, 10.0, 1.0 - hair.scatter));
    diffuseResult += brdfData.albedo * backScatter * hair.scatter * 0.15;
}

// ---------------------------------------------------------------------------
// Ambient / environment BRDF for hair  (UE Karis 2016 approach)
//
// Diffuse:
//   SSGI + cubemap diffuse fallback — ensures hair always receives some
//   environment light even when SSGI coverage is weak (thin cards in shadow).
//   Fake-scatter wrap term provides fill analogous to the direct path.
//
// Specular (two lobes sampled from the environment):
//   R   — sharp cuticle reflection (narrow, Fresnel-only)
//   TRT — broad internal reflection (wider, tinted by hair color absorption)
// ---------------------------------------------------------------------------
void AmbientBRDF_Hair(
    BRDFData brdfData,
    HairBRDFParams hair,
    vec3 viewDirection,
    vec4 giVisSH,
    out vec3 ambientDiffuse,
    out vec3 ambientSpecular)
{
    vec3 N = normalize(brdfData.normal);
    vec3 V = normalize(viewDirection);
    vec3 T = normalize(hair.tangentWS);

    // ── Flow-aware shift for ambient specular lobes ─────────────────────
    float shiftStrength = hair.shift * (1.0 + hair.flowBend * 0.5);
    vec3 T_shifted = normalize(T + N * shiftStrength * 0.15);

    float NoV = max(dot(N, V), 0.0);

    // ── Diffuse: SSGI + cubemap fallback + fake scatter ─────────────────
    // SSGI (brdfData.indirectDiffuse) can be very weak for thin hair cards
    // in shadow because screen-space GI doesn't capture light wrapping
    // around semi-transparent fibers.  Blend in the pre-convolved diffuse
    // cubemap as a floor so hair always receives some environment light.
    vec3 cubemapDiffuse = texture(PreConvDiffuseEnvironment, N).rgb;
    // Attenuate cubemap diffuse by directional GI probe visibility (indoor occlusion)
    float giVisDiffuse = EvalGIVisibility(giVisSH, N);
    cubemapDiffuse *= giVisDiffuse;
    float ssgiLum = dot(brdfData.indirectDiffuse, vec3(0.2126, 0.7152, 0.0722));
    // Smoothly blend: when SSGI is strong, trust it; when weak, add cubemap
    float cubemapWeight = clamp(1.0 - ssgiLum * 4.0, 0.0, 1.0);
    vec3 effectiveDiffuse = brdfData.indirectDiffuse + cubemapDiffuse * cubemapWeight * 0.5;

    vec3 fakeScatter = effectiveDiffuse * brdfData.albedo
                     * hair.scatter * 0.15;
    ambientDiffuse = effectiveDiffuse * brdfData.albedo * brdfData.ao
                   + fakeScatter;

    // ── Anisotropic reflection direction (per-lobe) ─────────────────────
    vec3 R = reflect(-V, N);
    float RdotT = dot(R, T_shifted);

    const float anisoR   = 0.8;
    const float anisoTRT = 0.4;

    vec3 R_dirR   = normalize(R - T_shifted * RdotT * anisoR);
    vec3 R_dirTRT = normalize(R - T_shifted * RdotT * anisoTRT);

    // ── Per-lobe roughness (same as direct path) ────────────────────────
    float roughR   = clamp(hair.roughness,       0.04, 1.0);
    float roughTRT = clamp(hair.roughness * 2.0, 0.08, 2.0);

    // ── Environment lookup per lobe ─────────────────────────────────────
    float lodR   = GetMipLevelFromRoughness(roughR);
    float lodTRT = GetMipLevelFromRoughness(min(roughTRT, 1.0));

    vec3 envR   = textureLod(PreConvSpecularEnvironment, R_dirR,   lodR).rgb;
    vec3 envTRT = textureLod(PreConvSpecularEnvironment, R_dirTRT, lodTRT).rgb;

    // Attenuate cubemap specular lobes by directional GI probe visibility
    float giVisR   = EvalGIVisibility(giVisSH, R_dirR);
    float giVisTRT = EvalGIVisibility(giVisSH, R_dirTRT);
    envR   *= giVisR;
    envTRT *= giVisTRT;

    // Blend with SSR when available — fade out SSR on rough hair to
    // avoid noisy screen-space artefacts on the wider TRT lobe.
    float ssrFadeR   = 1.0 - smoothstep(SSR_ROUGHNESS_FADE_START, SSR_ROUGHNESS_FADE_END, roughR);
    float ssrFadeTRT = 1.0 - smoothstep(SSR_ROUGHNESS_FADE_START, SSR_ROUGHNESS_FADE_END, min(roughTRT, 1.0));
    float ssrMaskR   = brdfData.indirectSpecular.a * ssrFadeR;
    float ssrMaskTRT = brdfData.indirectSpecular.a * ssrFadeTRT;
    envR   = mix(envR,   brdfData.indirectSpecular.rgb, ssrMaskR);
    envTRT = mix(envTRT, brdfData.indirectSpecular.rgb, ssrMaskTRT);

    // ── Split-sum integration via BRDF LUT ──────────────────────────────
    vec2 brdfR   = texture(BRDFLUT, vec2(NoV, roughR)).rg;
    vec2 brdfTRT = texture(BRDFLUT, vec2(NoV, min(roughTRT, 1.0))).rg;

    // ── Fresnel & attenuation per lobe ──────────────────────────────────
    float f = Hair_F(NoV);

    // R: pure Fresnel reflection — F0 only, no color absorption
    vec3 F0_R = vec3(0.0465);   // dielectric F0 for keratin (η≈1.55)
    vec3 specR = envR * (F0_R * brdfR.x + brdfR.y)
               * hair.specularStrength * 2.0;

    // TRT: internal reflection tinted by hair color absorption
    // Approximate path absorption: sqrt(albedo) represents double-pass T
    vec3 envTint = sqrt(max(brdfData.albedo, vec3(0.001)));
    float fTRT = (1.0 - f) * (1.0 - f) * f;   // Fresnel attenuation for TRT
    vec3 specTRT = envTRT * (envTint * brdfTRT.x + brdfTRT.y) * fTRT;

    ambientSpecular = (specR + specTRT) * brdfData.ao;
}

// ---------------------------------------------------------------------------
// Full direct-light accumulator (directional + point + spot)
// ---------------------------------------------------------------------------
void CalculateDirectLight_Hair(
    BRDFData brdfData,
    HairBRDFParams hair,
    sampler2DArray cascadeShadowMap,
    float viewDepth,
    sampler2D punctualShadowMap,
    inout LightResult lightResult)
{
    lightResult.directDiffuse = vec3(0.0);
    lightResult.directSpecular = vec3(0.0);

    // Directional light
    {
        vec3 diffuseResult = vec3(0.0);
        vec3 specularResult = vec3(0.0);

        DirectBRDF_Hair(brdfData, hair, uDirectionLight.direction.rgb, diffuseResult, specularResult);

        float shadowValue = SampleCascadeShadow(brdfData.positionWS, cascadeShadowMap, viewDepth);
        diffuseResult *= uDirectionLight.color.rgb * uDirectionLight.intensity * shadowValue;
        specularResult *= uDirectionLight.color.rgb * uDirectionLight.intensity * shadowValue;

        lightResult.directDiffuse += diffuseResult;
        lightResult.directSpecular += specularResult;
    }

    // Point lights
    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3 lightDirection = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;

        float shadowValue = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(pointLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        vec3 diffuseResult = vec3(0.0);
        vec3 specularResult = vec3(0.0);
        DirectBRDF_Hair(brdfData, hair, lightDirection, diffuseResult, specularResult);

        diffuseResult *= pointLight.color.rgb * pointLight.intensity * attenuation;
        specularResult *= pointLight.color.rgb * pointLight.intensity * attenuation;

        lightResult.directDiffuse += diffuseResult;
        lightResult.directSpecular += specularResult;
    }

    // Spot lights
    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3 lightDirection = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        float shadowValue = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(spotLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        vec3 diffuseResult = vec3(0.0);
        vec3 specularResult = vec3(0.0);
        DirectBRDF_Hair(brdfData, hair, lightDirection, diffuseResult, specularResult);

        diffuseResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        specularResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

        lightResult.directDiffuse += diffuseResult;
        lightResult.directSpecular += specularResult;
    }
}

#endif
