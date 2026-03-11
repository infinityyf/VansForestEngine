#ifndef BRDF_SKIN_INCLUDED
#define BRDF_SKIN_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Skin / Subsurface Scattering BRDF (Material ID = MATERIAL_ID_SKIN)
//   - Pre-integrated subsurface scattering (diffusion profile LUT)
//   - Dual-lobe specular (primary sharp + secondary wide)
//   - Shadow-map thickness estimation for transmission / back-lighting
// =============================================================================

// Pre-integrated skin scattering LUT
// U = NdotL * 0.5 + 0.5,  V = curvature [0..1]
#if !defined(PBRLutSetBind)
    #define PBRLutSetBind 0
#endif
layout(set = PBRLutSetBind, binding = 7) uniform sampler2D SkinPreIntegratedLUT;

// ---------------------------------------------------------------------------
// Estimate light-space thickness through the object using the shadow map.
// Returns thickness in normalised light-space depth units.
// ---------------------------------------------------------------------------
float ComputeSkinThickness(vec3 positionWS, sampler2D shadowMap)
{
    vec4 clipCoord = uDirectionLight.shadowMatrix * vec4(positionWS, 1.0);
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    vec2 shadowUV = clipCoord.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    float shadowDepth   = texture(shadowMap, shadowUV).r;
    float receiverDepth = clipCoord.z;

    // Positive when the fragment is behind the nearest lit surface
    float thickness = max(receiverDepth - shadowDepth, 0.0);
    return thickness;
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
// Skin direct BRDF – pre-integrated diffuse + dual-lobe specular.
// ---------------------------------------------------------------------------
void DirectBRDF_Skin(BRDFData brdf, vec3 lightDirection, float curvature,
                     inout vec3 diffuse, inout vec3 specular)
{
    vec3  viewDirection = brdf.viewDirection;
    vec3  halfVector    = normalize(lightDirection + viewDirection);
    float NdotL = dot(brdf.normal, lightDirection);       // may be negative
    float NdotV = max(dot(brdf.normal, viewDirection), 0.0);
    float NdotH = max(dot(brdf.normal, halfVector),    0.0);

    // --- Pre-integrated skin diffuse (wraps NdotL via LUT) ---
    vec2 lutUV       = vec2(NdotL * 0.5 + 0.5, curvature);
    vec3 skinScatter = texture(SkinPreIntegratedLUT, lutUV).rgb;
    diffuse = skinScatter * brdf.albedo / PI;

    // --- Dual-lobe specular ---
    vec3 F0 = brdf.fresnel0;
    vec3 F  = FresnelSchlick(NdotH, F0);

    // Primary lobe – original roughness
    float D1 = DistributionTrowbridgeReitzGGX(brdf.normal, halfVector, brdf.roughness);
    float G1 = GeometrySmith(brdf.normal, viewDirection, lightDirection, brdf.roughness);

    // Secondary lobe – wider / softer sheen
    float roughness2 = clamp(brdf.roughness * 2.0, 0.0, 1.0);
    float D2 = DistributionTrowbridgeReitzGGX(brdf.normal, halfVector, roughness2);
    float G2 = GeometrySmith(brdf.normal, viewDirection, lightDirection, roughness2);

    float NdotL_pos = max(NdotL, 0.001);
    float denom     = 4.0 * NdotL_pos * max(NdotV, 0.001);
    vec3  spec1     = (D1 * G1 * F) / denom;
    vec3  spec2     = (D2 * G2 * F) / denom * 0.3;

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
void CalculateDirectLight_Skin(BRDFData brdfData, float curvature,
                               sampler2D shadowMap, sampler2D punctualShadowMap,
                               inout LightResult lightResult)
{
    lightResult.directDiffuse  = vec3(0);
    lightResult.directSpecular = vec3(0);

    // ===================== Directional light (skin-specific) =====================
    vec3 diffuseResult  = vec3(0);
    vec3 specularResult = vec3(0);
    DirectBRDF_Skin(brdfData, uDirectionLight.direction.rgb, curvature,
                    diffuseResult, specularResult);
    diffuseResult  *= uDirectionLight.color.rgb * uDirectionLight.intensity;
    specularResult *= uDirectionLight.color.rgb * uDirectionLight.intensity;

    float shadowValue = SampleDirectionShadowMap_PCF_Noise(brdfData.positionWS, shadowMap);

    // Shadow attenuates specular fully.
    // Diffuse keeps a small residual – pre-integrated scattering means light
    // bleeds past the shadow terminator on skin.
    diffuseResult  *= mix(0.3, 1.0, shadowValue);
    specularResult *= shadowValue;

    // --- Thickness-based transmission (back-lighting through thin skin) ---
    float thickness   = ComputeSkinThickness(brdfData.positionWS, shadowMap);
    float NdotL_back  = max(-dot(brdfData.normal, uDirectionLight.direction.rgb), 0.0);
    vec3  transmission = ComputeSkinTransmission(thickness, brdfData.albedo,
                                                 uDirectionLight.color.rgb,
                                                 uDirectionLight.intensity);
    diffuseResult += transmission * NdotL_back;

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

        float pShadow = SamplePointShadowMap(brdfData.positionWS, punctualShadowMap, int(pointLight.shadowIndex));
        attenuation = min(attenuation, pShadow);

        vec3 dR = vec3(0);
        vec3 sR = vec3(0);
        DirectBRDF(brdfData, lightDirection, dR, sR);
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

        float sShadow = SampleSpotShadowMap(brdfData.positionWS, punctualShadowMap, int(spotLight.shadowIndex));
        attenuation = min(attenuation, sShadow);

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        vec3 dR = vec3(0);
        vec3 sR = vec3(0);
        DirectBRDF(brdfData, lightDirection, dR, sR);
        dR *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        sR *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

        lightResult.directDiffuse  += dR;
        lightResult.directSpecular += sR;
    }
}

#endif // BRDF_SKIN_INCLUDED
