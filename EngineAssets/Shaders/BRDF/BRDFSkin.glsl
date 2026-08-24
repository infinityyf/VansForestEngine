#ifndef BRDF_SKIN_INCLUDED
#define BRDF_SKIN_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"
#include "SkinData.glsl"

// =============================================================================
// Skin BRDF (Material ID = MATERIAL_ID_SKIN)
//   - Pre-integrated skin diffusion profile LUT
//   - Kelemen/Szirmay-Kalos specular (runtime Beckmann NDF, no LUT)
//   - Shadow-map thickness estimation for transmission / back-lighting
// =============================================================================


// ---------------------------------------------------------------------------
// Sample the pre-integrated skin diffusion LUT.
// U = NdotL remapped from [-1,1] to [0,1],  V = curvature [0,1].
// Returns the pre-integrated skin scatter color.
// ---------------------------------------------------------------------------
vec3 SampleSkinDiffusionLUT(float NdotL, float curvature)
{
    float u = NdotL * 0.5 + 0.5;   // Map [-1,1] -> [0,1]
    float v = clamp(curvature, 0.0, 1.0);
    return texture(SkinPreIntegratedLUT, vec2(u, v)).rgb;
}

vec3 SampleSkinDiffusionLUTLayer(float NdotL, float curvature, float layer)
{
    float u = NdotL * 0.5 + 0.5;
    float v = clamp(curvature, 0.0, 1.0);
    float safeLayer = clamp(floor(layer + 0.5), 0.0, 15.0);
    return texture(SkinProfilePreIntegratedLUTArray, vec3(u, v, safeLayer)).rgb;
}

struct SkinMaterialParams
{
    vec3 scatterColor;
    float scatterAmount;
    float specularScale;
    float scatterMask;
    float cavity;
    float primaryRoughnessScale;
    float secondaryRoughnessScale;
    float transmissionScale;
    float ior;
    float specularLobeMix;
    float diffusionRadiusScale;
    float thinnessScale;
    float transmissionDepthScale;
    float ambientScatterScale;
    vec3 scatterRadiusScale;
    float boundaryColorBleed;
    float profileLutLayer;
    float authoredThinness;
    float authoredThinnessWeight;
    float debugView;
};

SkinMaterialParams DecodeSkinMaterialParams(int materialIndex)
{
    SkinMaterialParams skin;
    skin.scatterColor = vec3(1.0, 0.34, 0.22);
    skin.scatterAmount = 0.65;
    skin.specularScale = 1.0;
    skin.scatterMask = 1.0;
    skin.cavity = 1.0;
    skin.primaryRoughnessScale = 0.75;
    skin.secondaryRoughnessScale = 1.75;
    skin.transmissionScale = 1.0;
    skin.ior = 1.4;
    skin.specularLobeMix = 0.72;
    skin.diffusionRadiusScale = 1.0;
    skin.thinnessScale = 1.0;
    skin.transmissionDepthScale = 1.0;
    skin.ambientScatterScale = 0.35;
    skin.scatterRadiusScale = vec3(1.0);
    skin.boundaryColorBleed = 1.0;
    skin.profileLutLayer = -1.0;
    skin.authoredThinness = 0.0;
    skin.authoredThinnessWeight = 0.0;
    skin.debugView = 0.0;

    SkinMaterialPayload payload = GetSkinMaterialPayload(materialIndex);
    skin.scatterColor = max(payload.scatterColorAmount.rgb, vec3(0.0));
    skin.scatterAmount = clamp(payload.scatterColorAmount.a, 0.0, 1.0);
    skin.specularScale = clamp(payload.roughnessNormalSpecular.z, 0.0, 4.0);
    skin.transmissionScale = clamp(payload.roughnessNormalSpecular.w, 0.0, 4.0);
    skin.primaryRoughnessScale = clamp(payload.lobeIOR.x, 0.1, 4.0);
    skin.secondaryRoughnessScale = clamp(payload.lobeIOR.y, 0.1, 4.0);
    skin.ior = clamp(payload.lobeIOR.z, 1.0, 2.5);
    skin.specularLobeMix = clamp(payload.lobeIOR.w, 0.0, 1.0);
    skin.diffusionRadiusScale = clamp(payload.profileControls.x, 0.05, 4.0);
    skin.thinnessScale = clamp(payload.profileControls.y, 0.10, 4.0);
    skin.transmissionDepthScale = clamp(payload.profileControls.z, 0.05, 4.0);
    skin.ambientScatterScale = clamp(payload.profileControls.w, 0.0, 1.0);
    skin.scatterRadiusScale = clamp(payload.profileShape.rgb, vec3(0.05), vec3(4.0));
    skin.boundaryColorBleed = clamp(payload.profileShape.w, 0.0, 2.0);
    skin.profileLutLayer = clamp(payload.profileLUT.x, -1.0, 15.0);
    skin.authoredThinnessWeight = clamp(payload.profileLUT.y, 0.0, 1.0);
    skin.debugView = clamp(payload.debugControls.x, 0.0, 16.0);
    return skin;
}

float SkinEffectiveScatterAmount(SkinMaterialParams skin)
{
    return clamp(skin.scatterAmount * skin.scatterMask, 0.0, 1.0);
}

uint SkinDebugViewMode(SkinMaterialParams skin)
{
    return uint(round(clamp(skin.debugView, 0.0, 16.0)));
}

float SkinProfileCurvature(float curvature, SkinMaterialParams skin)
{
    return clamp(curvature * skin.diffusionRadiusScale, 0.0, 1.0);
}

vec3 SkinProfileCurvatureRGB(float curvature, SkinMaterialParams skin)
{
    return clamp(curvature * skin.diffusionRadiusScale * skin.scatterRadiusScale, vec3(0.0), vec3(1.0));
}

vec3 SampleSkinDiffusionLUT_Profile(float NdotL, float curvature, SkinMaterialParams skin)
{
    vec3 profileCurvature = SkinProfileCurvatureRGB(curvature, skin);
    if (skin.profileLutLayer >= 0.0)
    {
        return vec3(
            SampleSkinDiffusionLUTLayer(NdotL, profileCurvature.r, skin.profileLutLayer).r,
            SampleSkinDiffusionLUTLayer(NdotL, profileCurvature.g, skin.profileLutLayer).g,
            SampleSkinDiffusionLUTLayer(NdotL, profileCurvature.b, skin.profileLutLayer).b);
    }
    return vec3(
        SampleSkinDiffusionLUT(NdotL, profileCurvature.r).r,
        SampleSkinDiffusionLUT(NdotL, profileCurvature.g).g,
        SampleSkinDiffusionLUT(NdotL, profileCurvature.b).b);
}

// ---------------------------------------------------------------------------
// Skin diffusion tint based on surface curvature.
// Thin areas (high curvature) are tinted by the authored scatter color.
// Thick or flat areas stay neutral.
//
// The tint is intentionally NOT NdotL-dependent — the pre-integrated
// skin diffusion LUT already encodes the smooth color shift across the terminator.
// Adding another NdotL curve on top creates visible jump discontinuities.
// ---------------------------------------------------------------------------
vec3 ComputeSkinScatterTint(float curvature, vec3 scatterColor, float scatterScale)
{
    float scatterAmount = curvature * scatterScale;
    return mix(vec3(1.0), scatterColor, clamp(scatterAmount, 0.0, 1.0));
}

vec3 ComputeSkinScatterTint_Profile(float curvature, vec3 scatterColor, float scatterScale, SkinMaterialParams skin)
{
    return ComputeSkinScatterTint(
        SkinProfileCurvature(curvature, skin),
        scatterColor,
        scatterScale * skin.boundaryColorBleed);
}

// ---------------------------------------------------------------------------
// Compute skin-specific Fresnel reflectance (F0).
// Skin has ~2.8% reflectance at normal incidence; the oil / sweat layer
// can raise this slightly.  A small albedo tint is mixed in for realism.
// ---------------------------------------------------------------------------
float SkinF0FromIOR(float ior)
{
    float safeIOR = max(ior, 1.0001);
    float ratio = (safeIOR - 1.0) / (safeIOR + 1.0);
    return ratio * ratio;
}

vec3 ComputeSkinF0(vec3 albedo, float ior)
{
    vec3 baseF0 = vec3(SkinF0FromIOR(ior));    // IOR ~= 1.4, skin/oil dielectric layer
    vec3 tint = albedo / max(max(albedo.r, max(albedo.g, albedo.b)), 1e-3);
    return baseF0 * mix(vec3(1.0), clamp(tint, vec3(0.0), vec3(1.0)), 0.10);
}

vec3 ComputeSkinF0(vec3 albedo)
{
    return ComputeSkinF0(albedo, 1.4);
}

float SkinThinnessFromCurvature(float curvature)
{
    return smoothstep(0.05, 0.85, clamp(curvature, 0.0, 1.0));
}

float SkinThinnessFromCurvature(float curvature, SkinMaterialParams skin)
{
    float curvatureThinness = SkinThinnessFromCurvature(clamp(curvature * skin.thinnessScale, 0.0, 1.0));
    return mix(curvatureThinness, clamp(skin.authoredThinness, 0.0, 1.0), skin.authoredThinnessWeight);
}

float SkinOpticalThickness(float curvature)
{
    float thinness = SkinThinnessFromCurvature(curvature);
    return mix(0.24, 0.025, thinness);
}

float SkinOpticalThickness(float curvature, SkinMaterialParams skin)
{
    float thinness = SkinThinnessFromCurvature(curvature, skin);
    return mix(0.24, 0.025, thinness) * skin.transmissionDepthScale;
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
// Transmission through thin skin regions.
// Uses an exponential absorption profile: red reaches further, blue least.
// ---------------------------------------------------------------------------
vec3 ComputeSkinTransmission(float thickness, vec3 albedo)
{
    vec3 sigma = vec3(1.0, 3.0, 5.0);
    float scale = 12.0;

    return exp(-sigma * thickness * scale) * albedo;
}

vec3 ComputeSkinTransmission(float thickness, vec3 albedo, SkinMaterialParams skin)
{
    vec3 sigma = vec3(1.0, 3.0, 5.0) / max(skin.scatterRadiusScale, vec3(0.05));
    float scale = 12.0;
    return exp(-sigma * thickness * scale) * albedo;
}

vec3 ComputeSkinBackTransmission(BRDFData brdf, vec3 lightDirection, float curvature,
                                 SkinMaterialParams skin)
{
    float NoL = dot(brdf.normal, lightDirection);
    float backFacing = smoothstep(0.0, 0.85, -NoL);
    float scatterAmount = SkinEffectiveScatterAmount(skin);
    if (backFacing <= 0.0 || scatterAmount <= 0.0)
        return vec3(0.0);

    float thinness = SkinThinnessFromCurvature(curvature, skin);
    float thickness = SkinOpticalThickness(curvature, skin);
    float forwardScatter = pow(max(dot(brdf.viewDirection, -lightDirection), 0.0), 2.0);
    vec3 transmission = ComputeSkinTransmission(thickness, brdf.albedo, skin) * skin.scatterColor;

    return transmission * thinness * backFacing *
           mix(0.35, 1.0, forwardScatter) *
           scatterAmount * skin.transmissionScale / PI;
}

float SkinTransmissionShadow(float shadowValue, float NoL, float curvature, float scatterAmount)
{
    float thinness = SkinThinnessFromCurvature(curvature);
    float backFacing = smoothstep(0.0, 0.85, -NoL);
    return mix(shadowValue, max(shadowValue, 0.35),
               thinness * backFacing * clamp(scatterAmount, 0.0, 1.0));
}

float SkinTransmissionShadow(float shadowValue, float NoL, float curvature, SkinMaterialParams skin)
{
    float thinness = SkinThinnessFromCurvature(curvature, skin);
    float backFacing = smoothstep(0.0, 0.85, -NoL);
    return mix(shadowValue, max(shadowValue, 0.35),
               thinness * backFacing * SkinEffectiveScatterAmount(skin));
}

vec3 SkinGGXLobe(BRDFData brdf, vec3 lightDirection, float roughness)
{
    vec3 viewDirection = brdf.viewDirection;
    float NdotL = max(dot(brdf.normal, lightDirection), 0.0);
    float NdotV = max(dot(brdf.normal, viewDirection), 0.0);
    if (NdotL <= 0.0 || NdotV <= 0.0)
        return vec3(0.0);

    vec3 halfVectorRaw = lightDirection + viewDirection;
    float halfLenSq = dot(halfVectorRaw, halfVectorRaw);
    if (halfLenSq < 1e-6)
        return vec3(0.0);
    vec3 halfVector = halfVectorRaw * inversesqrt(halfLenSq);
    float VdotH = max(dot(viewDirection, halfVector), 0.0);

    roughness = clamp(roughness, 0.045, 1.0);
    vec3 F = FresnelSchlick(VdotH, brdf.fresnel0);
    float D = DistributionTrowbridgeReitzGGX(brdf.normal, halfVector, roughness);
    float G = GeometrySmith(brdf.normal, viewDirection, lightDirection, roughness);

    return (D * G * F / (4.0 * max(NdotL, 0.001) * max(NdotV, 0.001))) * NdotL;
}

vec3 DualLobeSkinSpecular(BRDFData brdf, vec3 lightDirection, SkinMaterialParams skin)
{
    float baseRoughness = clamp(brdf.roughness, 0.045, 1.0);
    vec3 primary = SkinGGXLobe(
        brdf, lightDirection,
        clamp(baseRoughness * skin.primaryRoughnessScale, 0.045, 1.0));
    vec3 broad = SkinGGXLobe(
        brdf, lightDirection,
        clamp(baseRoughness * skin.secondaryRoughnessScale, 0.08, 1.0));
    return (primary * skin.specularLobeMix + broad * (1.0 - skin.specularLobeMix)) *
           skin.specularScale * clamp(skin.cavity, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Skin direct BRDF – pre-integrated diffuse + KS specular.
// Specular uses Kelemen/Szirmay-Kalos model with runtime Beckmann NDF.
// ---------------------------------------------------------------------------
void DirectBRDF_Skin(BRDFData brdf, vec3 lightDirection, float curvature,
                     SkinMaterialParams skin,
                     inout vec3 diffuse, inout vec3 specular,
                     inout vec3 transmission)
{
    vec3  viewDirection = brdf.viewDirection;
    float NdotL = dot(brdf.normal, lightDirection);       // may be negative
    vec3 halfVector = lightDirection + viewDirection;
    float halfLenSq = dot(halfVector, halfVector);
    float VdotH = halfLenSq > 1e-6
        ? max(dot(viewDirection, halfVector * inversesqrt(halfLenSq)), 0.0)
        : 0.0;

    // --- Pre-integrated skin diffuse (wraps NdotL via LUT) ---
    float scatterAmount = SkinEffectiveScatterAmount(skin);
    vec3 lambert = vec3(max(NdotL, 0.0));
    vec3 skinScatter = SampleSkinDiffusionLUT_Profile(NdotL, curvature, skin);
    skinScatter = mix(lambert, skinScatter, scatterAmount);

    // Curvature-only skin tint for thin areas.
    vec3 scatterTint = ComputeSkinScatterTint_Profile(curvature, skin.scatterColor, scatterAmount, skin);

    vec3 F = FresnelSchlick(VdotH, brdf.fresnel0);
    vec3 kD = (vec3(1.0) - F) * (1.0 - brdf.metallic);
    diffuse = skinScatter * brdf.albedo * scatterTint * kD / PI;

    specular = DualLobeSkinSpecular(brdf, lightDirection, skin);
    transmission = ComputeSkinBackTransmission(brdf, lightDirection, curvature, skin);
}

void DirectBRDF_Skin(BRDFData brdf, vec3 lightDirection, float curvature,
                     SkinMaterialParams skin,
                     inout vec3 diffuse, inout vec3 specular)
{
    vec3 transmission = vec3(0.0);
    DirectBRDF_Skin(brdf, lightDirection, curvature, skin, diffuse, specular, transmission);
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
    vec3 skinScatter = SampleSkinDiffusionLUT(NdotL, curvature);

    // Curvature-only skin tint for thin areas.
    vec3 scatterTint = ComputeSkinScatterTint(curvature, vec3(1.0, 0.2, 0.1), 1.0);
    diffuse = skinScatter * brdf.albedo * scatterTint / PI;

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
// Ambient for skin - standard PBR fallback; diffusion mainly affects direct light.
// ---------------------------------------------------------------------------
void AmbientBRDF_Skin(BRDFData brdf, SkinMaterialParams skin,
                      vec3 viewDirection, inout vec3 diffuse, inout vec3 specular)
{
    AmbientBRDF(brdf, viewDirection, diffuse, specular);

    float NoV = max(dot(brdf.normal, viewDirection), 0.0);
    float grazingScatter = pow(1.0 - NoV, 2.0);
    float scatterAmount = SkinEffectiveScatterAmount(skin);
    vec3 ambientTint = ComputeSkinScatterTint_Profile(
        grazingScatter, skin.scatterColor, 0.45 * scatterAmount, skin);
    diffuse *= mix(vec3(1.0), ambientTint, skin.ambientScatterScale * scatterAmount);
}

void EvaluateRectLight_Skin(RectLightData rl, BRDFData brdf, float curvature,
                            SkinMaterialParams skin,
                            out vec3 diffuse, out vec3 specular)
{
    float scatterAmount = SkinEffectiveScatterAmount(skin);
    vec3 scatterTint = ComputeSkinScatterTint_Profile(curvature, skin.scatterColor, scatterAmount, skin);
    vec3 rectDiffuseColor = brdf.albedo * scatterTint;
    vec3 unusedSpecular = vec3(0.0);
    EvaluateRectLightLTC(
        rl,
        brdf.normal, brdf.viewDirection, brdf.positionWS,
        brdf.roughness, rectDiffuseColor, brdf.fresnel0,
        diffuse, unusedSpecular);

    float baseRoughness = clamp(brdf.roughness, 0.045, 1.0);
    vec3 unusedDiffuse0 = vec3(0.0);
    vec3 unusedDiffuse1 = vec3(0.0);
    vec3 primarySpecular = vec3(0.0);
    vec3 broadSpecular = vec3(0.0);
    EvaluateRectLightLTC(
        rl,
        brdf.normal, brdf.viewDirection, brdf.positionWS,
        clamp(baseRoughness * skin.primaryRoughnessScale, 0.045, 1.0), vec3(0.0), brdf.fresnel0,
        unusedDiffuse0, primarySpecular);
    EvaluateRectLightLTC(
        rl,
        brdf.normal, brdf.viewDirection, brdf.positionWS,
        clamp(baseRoughness * skin.secondaryRoughnessScale, 0.08, 1.0), vec3(0.0), brdf.fresnel0,
        unusedDiffuse1, broadSpecular);

    specular = (primarySpecular * skin.specularLobeMix + broadSpecular * (1.0 - skin.specularLobeMix)) *
               skin.specularScale * clamp(skin.cavity, 0.0, 1.0);
}

bool TrySkinDebugView(BRDFData brdf, SkinMaterialParams skin, float curvature,
                      LightResult lightResult, out vec3 debugColor)
{
    uint mode = SkinDebugViewMode(skin);
    if (mode == 0u)
        return false;

    float profiledCurvature = SkinProfileCurvature(curvature, skin);
    if (mode == 1u)
        debugColor = vec3(SkinEffectiveScatterAmount(skin));
    else if (mode == 2u)
        debugColor = vec3(clamp(skin.cavity, 0.0, 1.0));
    else if (mode == 3u)
        debugColor = vec3(curvature, profiledCurvature, SkinThinnessFromCurvature(curvature, skin));
    else if (mode == 4u)
        debugColor = vec3(clamp(brdf.roughness, 0.0, 1.0));
    else if (mode == 5u)
        debugColor = clamp(skin.scatterColor * SkinEffectiveScatterAmount(skin), vec3(0.0), vec3(1.0));
    else if (mode == 6u)
        debugColor = clamp(vec3(
            skin.diffusionRadiusScale / 4.0,
            skin.thinnessScale / 4.0,
            skin.transmissionDepthScale / 4.0), vec3(0.0), vec3(1.0));
    else if (mode == 7u)
        debugColor = max(lightResult.directDiffuse, vec3(0.0));
    else if (mode == 8u)
        debugColor = max(lightResult.directSpecular, vec3(0.0));
    else if (mode == 9u)
        debugColor = max(lightResult.ambientDiffuse + lightResult.ambientSpecular, vec3(0.0));
    else if (mode == 10u)
        debugColor = clamp(skin.scatterRadiusScale / 4.0, vec3(0.0), vec3(1.0));
    else if (mode == 11u)
        debugColor = vec3(max(skin.profileLutLayer, 0.0) / 15.0);
    else
        debugColor = vec3(float(mode) / 16.0, 0.0, 1.0 - float(mode) / 16.0);

    return true;
}

// ---------------------------------------------------------------------------
// Main skin lighting (directional + punctual + rect area lights).
// Directional / punctual lights use pre-integrated skin diffuse and thin-skin
// transmission. Rect lights use the shared LTC path for lighting parity while
// keeping skin specular unfiltered and dual-lobed.
// ---------------------------------------------------------------------------
// Cascade shadow map version of skin lighting
void CalculateDirectLight_Skin(BRDFData brdfData, float curvature,
                               SkinMaterialParams skin,
							   sampler2DShadow punctualShadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT],
							   float directionalShadow,
                               inout LightResult lightResult)
{
    lightResult.directDiffuse  = vec3(0);
    lightResult.directSpecular = vec3(0);

    // ===================== Directional light (skin-specific) =====================
    vec3 diffuseResult  = vec3(0);
    vec3 specularResult = vec3(0);
    vec3 transmissionResult = vec3(0);
    float NdotL_dir = dot(brdfData.normal, uDirectionLight.direction.rgb);
    DirectBRDF_Skin(brdfData, uDirectionLight.direction.rgb, curvature, skin,
                    diffuseResult, specularResult, transmissionResult);
    diffuseResult  *= uDirectionLight.color.rgb * uDirectionLight.intensity;
    specularResult *= uDirectionLight.color.rgb * uDirectionLight.intensity;
    transmissionResult *= uDirectionLight.color.rgb * uDirectionLight.intensity;

	float shadowValue = directionalShadow;
    float transmissionShadow = SkinTransmissionShadow(shadowValue, NdotL_dir, curvature, skin);

    diffuseResult  *= shadowValue;
    specularResult *= shadowValue;
    transmissionResult *= transmissionShadow;

    lightResult.directDiffuse  += diffuseResult + transmissionResult;
    lightResult.directSpecular += specularResult;

    // ===================== Local lights =====================
#ifdef TILE_LIGHT
    TileLightHeader tileLightHdr = GetFragTileLightHeader();

    for (uint ptk = 0u; ptk < tileLightHdr.pointCount; ++ptk)
    {
        uint i = tileLightIndices[tileLightHdr.pointOffset + ptk];
        PointLightData pointLight = GetPointLight(int(i));
        vec3 lightDirection = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / max(pointLight.radius, 1e-5));
        attenuation *= attenuation;

        float pShadow = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
        if (IsPointShadowFallbackSelected(tileLightHdr, i))
        {
            pShadow = BlendPunctualShadowFallback(
                pShadow,
                SamplePunctualScreenSpaceShadow(brdfData.positionWS, brdfData.normal, lightDirection, distance),
                0u, int(i));
        }

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(pointLight.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDirection, vec3(0.0, -1.0, 0.0));
#endif

        float litAttenuation = attenuation * pShadow;
        float transmissionAttenuation = attenuation *
            SkinTransmissionShadow(pShadow, dot(brdfData.normal, lightDirection), curvature, skin);

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        vec3 tR = vec3(0.0);
        DirectBRDF_Skin(brdfData, lightDirection, curvature, skin, dR, sR, tR);
        vec3 pointEnergy = pointLight.color.rgb * pointLight.intensity;
        lightResult.directDiffuse  += (dR * litAttenuation + tR * transmissionAttenuation) * pointEnergy;
        lightResult.directSpecular += sR * litAttenuation * pointEnergy;
    }

    for (uint spk = 0u; spk < tileLightHdr.spotCount; ++spk)
    {
        uint i = tileLightIndices[tileLightHdr.spotOffset + spk];
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3 lightDirection = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / max(spotLight.radius, 1e-5));
        attenuation *= attenuation;

        float coneAngle = dot(normalize(spotLight.direction.xyz), lightDirection);
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / max(innerConeAngle - outerConeAngle, 1e-4), 0.0, 1.0);

        float sShadow = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
        if (IsSpotShadowFallbackSelected(tileLightHdr, i))
        {
            sShadow = BlendPunctualShadowFallback(
                sShadow,
                SamplePunctualScreenSpaceShadow(brdfData.positionWS, brdfData.normal, lightDirection, distance),
                1u, int(i));
        }

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(spotLight.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDirection, spotLight.direction.xyz) *
                           spotLight.iesIntensityScale;
#endif

        float litAttenuation = attenuation * sShadow * coneAttenuation;
        float transmissionAttenuation = attenuation *
            SkinTransmissionShadow(sShadow, dot(brdfData.normal, lightDirection), curvature, skin) *
            coneAttenuation;

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        vec3 tR = vec3(0.0);
        DirectBRDF_Skin(brdfData, lightDirection, curvature, skin, dR, sR, tR);
        vec3 spotEnergy = spotLight.color.rgb * spotLight.intensity;
        lightResult.directDiffuse  += (dR * litAttenuation + tR * transmissionAttenuation) * spotEnergy;
        lightResult.directSpecular += sR * litAttenuation * spotEnergy;
    }

    for (uint rck = 0u; rck < tileLightHdr.rectCount; ++rck)
    {
        uint i = tileLightIndices[tileLightHdr.rectOffset + rck];
        RectLightData rectLight = GetRectLight(int(i));
        vec3 rectD = vec3(0.0);
        vec3 rectS = vec3(0.0);
        EvaluateRectLight_Skin(rectLight, brdfData, curvature, skin, rectD, rectS);

        uint shadowMetaIndex = rectLight.shadowMetaIndex;
        if (shadowMetaIndex != INVALID_SHADOW_INDEX)
        {
            vec3 lightDirection = normalize(rectLight.position_halfW.xyz - brdfData.positionWS);
            float rectShadow = SampleRectShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
            if (IsRectShadowFallbackSelected(tileLightHdr, i))
            {
                float distance = length(rectLight.position_halfW.xyz - brdfData.positionWS);
                rectShadow = BlendPunctualShadowFallback(
                    rectShadow,
                    SamplePunctualScreenSpaceShadow(brdfData.positionWS, brdfData.normal, lightDirection, distance),
                    2u, int(i));
            }
            rectD *= rectShadow;
            rectS *= rectShadow;
        }

        lightResult.directDiffuse  += rectD;
        lightResult.directSpecular += rectS;
    }
#else
    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3  lightDirection = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / max(pointLight.radius, 1e-5));
        attenuation *= attenuation;

        float pShadow = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(pointLight.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDirection, vec3(0.0, -1.0, 0.0));
#endif

        float litAttenuation = attenuation * pShadow;
        float transmissionAttenuation = attenuation *
            SkinTransmissionShadow(pShadow, dot(brdfData.normal, lightDirection), curvature, skin);

        vec3 dR = vec3(0);
        vec3 sR = vec3(0);
        vec3 tR = vec3(0);
        DirectBRDF_Skin(brdfData, lightDirection, curvature, skin, dR, sR, tR);
        vec3 pointEnergy = pointLight.color.rgb * pointLight.intensity;
        dR *= pointEnergy * litAttenuation;
        sR *= pointEnergy * litAttenuation;
        tR *= pointEnergy * transmissionAttenuation;

        lightResult.directDiffuse  += dR + tR;
        lightResult.directSpecular += sR;
    }

    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3  lightDirection = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / max(spotLight.radius, 1e-5));
        attenuation *= attenuation;

        float coneAngle = dot(normalize(spotLight.direction.xyz), lightDirection);
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / max(innerConeAngle - outerConeAngle, 1e-4), 0.0, 1.0);
        float sShadow = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(spotLight.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDirection, spotLight.direction.xyz) *
                           spotLight.iesIntensityScale;
#endif

        float litAttenuation = attenuation * sShadow * coneAttenuation;
        float transmissionAttenuation = attenuation *
            SkinTransmissionShadow(sShadow, dot(brdfData.normal, lightDirection), curvature, skin) *
            coneAttenuation;

        vec3 dR = vec3(0);
        vec3 sR = vec3(0);
        vec3 tR = vec3(0);
        DirectBRDF_Skin(brdfData, lightDirection, curvature, skin, dR, sR, tR);
        vec3 spotEnergy = spotLight.color.rgb * spotLight.intensity;
        dR *= spotEnergy * litAttenuation;
        sR *= spotEnergy * litAttenuation;
        tR *= spotEnergy * transmissionAttenuation;

        lightResult.directDiffuse  += dR + tR;
        lightResult.directSpecular += sR;
    }

    uint rectCount = GetRectLightCount();
    for (uint i = 0u; i < rectCount && i < uint(MAX_RECT_LIGHTS); ++i)
    {
        RectLightData rectLight = GetRectLight(int(i));
        vec3 rectD = vec3(0.0);
        vec3 rectS = vec3(0.0);
        EvaluateRectLight_Skin(rectLight, brdfData, curvature, skin, rectD, rectS);

        uint shadowMetaIndex = rectLight.shadowMetaIndex;
        if (shadowMetaIndex != INVALID_SHADOW_INDEX)
        {
            vec3 lightDirection = normalize(rectLight.position_halfW.xyz - brdfData.positionWS);
            float rectShadow = SampleRectShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
            rectD *= rectShadow;
            rectS *= rectShadow;
        }

        lightResult.directDiffuse  += rectD;
        lightResult.directSpecular += rectS;
    }
#endif
}

#endif // BRDF_SKIN_INCLUDED
