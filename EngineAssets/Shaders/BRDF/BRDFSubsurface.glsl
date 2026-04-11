#ifndef BRDF_SUBSURFACE_INCLUDED
#define BRDF_SUBSURFACE_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Subsurface Scattering BRDF (Material ID = MATERIAL_ID_SUBSURFACE)
//   - Approximated BTDF for forward/back scatter through thin surfaces
//   - Specular GGX reflection (standard Cook-Torrance when surface faces light)
//   - Lambertian diffuse + subsurface transmission term
//
// Reference: Google Filament engine — subsurface shading model.
// Follows the algorithm described in SUBSURFACE_RENDER_PIPELINE.md.
// =============================================================================

// ---------------------------------------------------------------------------
// Subsurface-specific parameters, unpacked from the G-Buffer in the deferred pass.
// ---------------------------------------------------------------------------
struct SubsurfaceParams
{
    float thickness;        // [0,1] — 0 = thin (max transmission), 1 = thick (no transmission)
    float subsurfacePower;  // sharpness exponent for forward scatter
    vec3  subsurfaceColor;  // tint of transmitted/scattered light
};

// ---------------------------------------------------------------------------
// Direct BRDF for subsurface materials.
//
// Algorithm (per SUBSURFACE_RENDER_PIPELINE.md §2):
//   1. Standard Cook-Torrance specular (only when N·L > 0)
//   2. Standard Lambertian diffuse
//   3. Combine reflected light: (Fd + Fr) * max(NoL,0) * occlusion
//   4. Forward scatter: exp2(power * (V·(-L) - 1))
//   5. Back scatter: saturate(N·L * t + (1-t)) * 0.5
//   6. Subsurface = mix(backScatter, 1.0, forwardScatter) * (1 - t)
//   7. Add subsurfaceColor * subsurface * (1/PI)
// ---------------------------------------------------------------------------
void DirectBRDF_Subsurface(BRDFData brdf, vec3 lightDirection, SubsurfaceParams sss,
                           inout vec3 diffuse, inout vec3 specular,
                           inout vec3 subsurfaceTransmission)
{
    vec3  V = brdf.viewDirection;
    vec3  N = brdf.normal;
    vec3  L = lightDirection;
    vec3  H = normalize(V + L);

    float NoL = dot(N, L);       // intentionally NOT clamped — subsurface needs negative
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float LoH = clamp(dot(L, H), 0.0, 1.0);
    float NoV = max(dot(N, V), 0.001);

    float t = sss.thickness;

    // ────────────────────────────────────────────────────────────────────────
    // §2.2  Specular BRDF (reflection) — only when surface faces the light
    // ────────────────────────────────────────────────────────────────────────
    vec3 Fr = vec3(0.0);
    if (NoL > 0.0)
    {
        vec3 F0 = mix(brdf.fresnel0, brdf.albedo, brdf.metallic);
        float D = DistributionTrowbridgeReitzGGX(N, H, brdf.roughness);
        float G = GeometrySmith(N, V, L, brdf.roughness);
        vec3  F = FresnelSchlick(NoH, F0);

        vec3  kS = F;
        float denom = 4.0 * max(NoL, 0.001) * NoV;
        Fr = (D * G * F) / denom;
    }

    // ────────────────────────────────────────────────────────────────────────
    // §2.3  Diffuse BRDF (reflection) — Lambertian
    // ────────────────────────────────────────────────────────────────────────
    vec3 F0_diff = mix(brdf.fresnel0, brdf.albedo, brdf.metallic);
    vec3 kS_diff = FresnelSchlick(NoH, F0_diff);
    vec3 kD      = (vec3(1.0) - kS_diff) * (1.0 - brdf.metallic);
    vec3 Fd      = brdf.albedo * kD / PI;

    // ────────────────────────────────────────────────────────────────────────
    // §2.4  Reflected light combination
    // ────────────────────────────────────────────────────────────────────────
    float NoL_clamped = max(NoL, 0.0);
    diffuse  = Fd * NoL_clamped;
    specular = Fr * NoL_clamped;

    // ────────────────────────────────────────────────────────────────────────
    // §2.5  Subsurface Scattering Term (BTDF approximation)
    // ────────────────────────────────────────────────────────────────────────

    // Step 1 — Forward scatter angle: how closely V aligns with light through object
    float scatterVoH = clamp(dot(V, -L), 0.0, 1.0);

    // Step 2 — Forward scattering (spherical gaussian approximation of pow)
    // forwardScatter = 2^(power * (scatterVoH - 1))
    float forwardScatter = exp2(scatterVoH * sss.subsurfacePower - sss.subsurfacePower);

    // Step 3 — Back scatter (diffuse wrap around)
    // thin (t=0): backScatter = 0.5  (maximum wrap)
    // thick (t=1): backScatter = saturate(NoL) * 0.5  (standard half-Lambertian)
    float backScatter = clamp(NoL * t + (1.0 - t), 0.0, 1.0) * 0.5;

    // Step 4 — Combine: forward scatter dominates when viewing through the object,
    //          back scatter fills in diffuse transmission on dark side,
    //          scaled by (1 - thickness) — thicker objects transmit less.
    float subsurface = mix(backScatter, 1.0, forwardScatter) * (1.0 - t);

    // Step 5 — Output transmitted light separately (tinted by subsurfaceColor)
    //          so the lighting loop can apply shadow independently.
    subsurfaceTransmission = sss.subsurfaceColor * subsurface / PI;
}

// ---------------------------------------------------------------------------
// Ambient BRDF for subsurface materials.
//
// Standard PBR ambient + subsurface IBL transmission (§3.2):
//   viewIndependent = diffuseIrradiance(N)
//   viewDependent   = prefilteredRadiance(-V, roughness + 1 + thickness)
//   attenuation     = (1 - thickness) / (2*PI)
//   Fd += subsurfaceColor * (viewIndependent + viewDependent) * attenuation
// ---------------------------------------------------------------------------
void AmbientBRDF_Subsurface(BRDFData brdf, SubsurfaceParams sss, vec3 viewDirection,
                            vec4 giVisSH, inout vec3 diffuse, inout vec3 specular)
{
    // Standard PBR ambient (reuse existing AmbientBRDF)
    AmbientBRDF(brdf, viewDirection, giVisSH, diffuse, specular);

    // §3.2 — Subsurface IBL transmission
    // View-independent: diffuse irradiance (already computed in brdf.indirectDiffuse)
    vec3 viewIndependent = brdf.indirectDiffuse;

    // View-dependent: sample specular environment in reverse view direction at high mip
    vec3 reverseViewDir = -viewDirection;
    float subsurfaceLod = GetMipLevelFromRoughness(brdf.roughness) + 1.0 + sss.thickness;
    vec3 viewDependent = textureLod(PreConvSpecularEnvironment, reverseViewDir, subsurfaceLod).rgb;
    // Attenuate cubemap by directional GI probe visibility (indoor occlusion)
    float giVis = EvalGIVisibility(giVisSH, reverseViewDir);
    viewDependent *= giVis;

    // Attenuation: thin objects transmit more, thick objects transmit nothing
    float attenuation = (1.0 - sss.thickness) / TWO_PI;

    // Add subsurface transmission to diffuse (purely additive)
    diffuse += sss.subsurfaceColor * (viewIndependent + viewDependent) * attenuation;
}

// ---------------------------------------------------------------------------
// Main subsurface lighting loop (directional + punctual).
// Follows the same structure as CalculateDirectLight_Skin but uses the
// subsurface scattering BTDF approximation from SUBSURFACE_RENDER_PIPELINE.md.
// ---------------------------------------------------------------------------
void CalculateDirectLight_Subsurface(BRDFData brdfData, SubsurfaceParams sss,
                                     sampler2DArray cascadeShadowMap, float viewDepth,
                                     sampler2D punctualShadowMap,
                                     inout LightResult lightResult)
{
    lightResult.directDiffuse  = vec3(0);
    lightResult.directSpecular = vec3(0);

    // ===================== Directional light =====================
    {
        vec3 reflectedDiffuse = vec3(0);
        vec3 specularResult   = vec3(0);
        vec3 transmission     = vec3(0);
        DirectBRDF_Subsurface(brdfData, uDirectionLight.direction.rgb, sss,
                              reflectedDiffuse, specularResult, transmission);

        // §2.6 — Apply light color, intensity, attenuation
        vec3 lightEnergy = uDirectionLight.color.rgb * uDirectionLight.intensity;
        reflectedDiffuse *= lightEnergy;
        specularResult   *= lightEnergy;
        transmission     *= lightEnergy;

        float shadowValue = SampleCascadeShadow(brdfData.positionWS, cascadeShadowMap, viewDepth);

        // Apply shadow uniformly — if the light is occluded, no contribution at all.
        // The subsurface BTDF already handles wrap-around lighting for back-facing
        // geometry via the forward/back scatter terms in DirectBRDF_Subsurface.
        reflectedDiffuse *= shadowValue;
        specularResult   *= shadowValue;
        transmission     *= shadowValue;

        lightResult.directDiffuse  += reflectedDiffuse + transmission;
        lightResult.directSpecular += specularResult;
    }

    // ===================== Point lights =====================
    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3  lightDirection = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;

        float pShadow = SamplePointShadowMap(brdfData.positionWS, punctualShadowMap, int(pointLight.shadowIndex));

        vec3 dR = vec3(0);
        vec3 sR = vec3(0);
        vec3 tR = vec3(0);
        DirectBRDF_Subsurface(brdfData, lightDirection, sss, dR, sR, tR);

        vec3 lightEnergy = pointLight.color.rgb * pointLight.intensity * attenuation;
        dR *= lightEnergy * pShadow;
        sR *= lightEnergy * pShadow;
        tR *= lightEnergy * pShadow;

        lightResult.directDiffuse  += dR + tR;
        lightResult.directSpecular += sR;
    }

    // ===================== Spot lights =====================
    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3  lightDirection = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        float sShadow = SampleSpotShadowMap(brdfData.positionWS, punctualShadowMap, int(spotLight.shadowIndex));

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        vec3 dR = vec3(0);
        vec3 sR = vec3(0);
        vec3 tR = vec3(0);
        DirectBRDF_Subsurface(brdfData, lightDirection, sss, dR, sR, tR);

        vec3 lightEnergy = spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        dR *= lightEnergy * sShadow;
        sR *= lightEnergy * sShadow;
        tR *= lightEnergy * sShadow;

        lightResult.directDiffuse  += dR + tR;
        lightResult.directSpecular += sR;
    }
}

#endif // BRDF_SUBSURFACE_INCLUDED
