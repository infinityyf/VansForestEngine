#ifndef BRDF_VEGETATION_INCLUDED
#define BRDF_VEGETATION_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Vegetation BRDF (Material ID = MATERIAL_ID_GRASS)
//   - Wrap diffuse for soft light penetration through thin leaves/blades
//   - Subsurface scatter approximation for translucency
//   - Standard GGX specular
//
// Suitable for grass, foliage, thin plant surfaces.
// =============================================================================

// ---------------------------------------------------------------------------
// Vegetation-specific parameters, unpacked from the G-Buffer in deferred pass.
// ---------------------------------------------------------------------------
struct VegetationParams
{
    float translucency;     // [0,1] — how much light passes through
    float scatterWidth;     // wrap-diffuse width (typically 0.5)
    float sssDistortion;    // normal distortion for back-lit scatter
    float sssAmbient;       // ambient scatter term
    float sssPower;         // exponent for forward scatter lobe
};

// ---------------------------------------------------------------------------
// Wrap diffuse — extends Lambertian to wrap around the surface.
//   WrapDiffuse(N, L, w) = max(0, (N·L + w) / (1 + w)^2 )
// ---------------------------------------------------------------------------
float WrapDiffuse(vec3 N, vec3 L, float wrap)
{
    float NoL = dot(N, L);
    float denom = (1.0 + wrap) * (1.0 + wrap);
    return max(0.0, (NoL + wrap) / denom);
}

// ---------------------------------------------------------------------------
// Subsurface scatter approximation for thin translucent surfaces.
//   Simulates back-lit translucency by distorting the surface normal
//   towards the light and computing a forward-scatter lobe.
// ---------------------------------------------------------------------------
vec3 SubsurfaceScatter(vec3 V, vec3 L, vec3 N, float translucency,
                       float distortion, float ambient, float power,
                       vec3 albedo)
{
    // Distort normal towards the light for back-lit contribution
    vec3 scatterDir = normalize(L + N * distortion);
    float VdotS = max(0.0, dot(V, -scatterDir));
    float scatter = pow(VdotS, power) * translucency;
    float backContrib = scatter + ambient * translucency;
    return albedo * backContrib;
}

// ---------------------------------------------------------------------------
// Direct BRDF for vegetation materials.
//
// Algorithm:
//   1. Wrap diffuse for soft Lambert (instead of hard N·L clamp)
//   2. Standard Cook-Torrance GGX specular (when N·L > 0)
//   3. Subsurface scatter for translucency
//   4. Total = (Fd_wrap + Fr) * occlusion + subsurface
// ---------------------------------------------------------------------------
void DirectBRDF_Vegetation(BRDFData brdf, vec3 lightDirection, VegetationParams veg,
                           inout vec3 diffuse, inout vec3 specular,
                           inout vec3 subsurfaceTransmission)
{
    vec3  V = brdf.viewDirection;
    vec3  N = brdf.normal;
    vec3  L = lightDirection;
    vec3  H = normalize(V + L);

    float NoL = dot(N, L);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float LoH = clamp(dot(L, H), 0.0, 1.0);
    float NoV = max(dot(N, V), 0.001);

    // ── Wrap diffuse ────────────────────────────────────────────────────────
    float wrapNoL = WrapDiffuse(N, L, veg.scatterWidth);
    vec3  kD = (1.0 - brdf.metallic) * brdf.albedo / PI;
    diffuse = kD * wrapNoL;

    // ── Specular GGX (only when lit) ────────────────────────────────────────
    if (NoL > 0.0)
    {
        vec3  F0 = mix(brdf.fresnel0, brdf.albedo, brdf.metallic);
        float D  = DistributionTrowbridgeReitzGGX(N, H, brdf.roughness);
        float G  = GeometrySmith(N, V, L, brdf.roughness);
        vec3  F  = FresnelSchlick(NoH, F0);

        float denom = 4.0 * max(NoL, 0.001) * NoV;
        specular = (D * G * F) / denom;
    }

    // ── Subsurface scatter (translucency) ───────────────────────────────────
    subsurfaceTransmission = SubsurfaceScatter(V, L, N,
        veg.translucency, veg.sssDistortion, veg.sssAmbient, veg.sssPower,
        brdf.albedo);
}

#endif // BRDF_VEGETATION_INCLUDED
