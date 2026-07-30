#ifndef PBR_WATER_BSDF_GLSL
#define PBR_WATER_BSDF_GLSL

#include "pbr_water_common.glsl"

vec3 PBRW_EvaluateDirectSpecular(vec3 N, vec3 V, vec3 L, vec3 F0, float roughness, float intensity)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotV <= 0.0 || NdotL <= 0.0)
        return vec3(0.0);
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float LdotH = max(dot(L, H), 0.0);
    vec3 F = PBRW_FresnelSchlick(F0, LdotH);
    return F * PBRW_D_GGX(NdotH, roughness) *
        PBRW_V_SmithJointGGX(NdotV, NdotL, roughness) * NdotL * intensity;
}

vec3 PBRW_Scatter(vec3 Li, vec3 sigmaA, vec3 sigmaS, float distanceMeters, float phase)
{
    vec3 sigmaT = max(sigmaA + sigmaS, vec3(1e-5));
    vec3 T = PBRW_BeerLambert(sigmaT, distanceMeters);
    vec3 albedo = sigmaS / sigmaT;
    return Li * (vec3(1.0) - T) * albedo * phase;
}

vec3 PBRW_EvaluateThinSSS(vec3 Li, vec3 sigmaA, vec3 sigmaS, float thickness01,
                         float pathScale, float nonlinearStrength, float boost, float phase)
{
    float opticalDepth = PBRW_Luminance(sigmaA + sigmaS) * thickness01 * pathScale;
    float linearPath = thickness01 * pathScale;
    float nonlinearPath = thickness01 * thickness01 * pathScale *
        (1.0 + PBRW_Luminance(sigmaS));
    float distanceMeters = mix(linearPath, nonlinearPath,
        PBRW_Saturate(opticalDepth * nonlinearStrength));
    return PBRW_Scatter(Li, sigmaA, sigmaS, distanceMeters, phase) * boost;
}

vec3 PBRW_EvaluateBacklit(vec3 Li, vec3 sigmaT, float thickness01, float pathScale,
                         float VdotMinusL, float phaseG)
{
    float path = thickness01 * pathScale;
    vec3 T = PBRW_BeerLambert(sigmaT, path);
    float phase = PBRW_HenyeyGreenstein(VdotMinusL, phaseG);
    return Li * T * phase;
}

#endif
