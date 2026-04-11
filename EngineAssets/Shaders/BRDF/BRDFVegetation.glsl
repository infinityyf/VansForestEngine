#ifndef BRDF_VEGETATION_INCLUDED
#define BRDF_VEGETATION_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Vegetation BRDF (Material ID = MATERIAL_ID_GRASS)
//   - Wrap diffuse for soft light penetration through thin leaves/blades
//   - Forward-scatter translucency restricted to narrow V≈L angles
//   - Enhanced GGX specular with higher Fresnel floor for wet/waxy grass
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
// Subsurface scatter approximation for thin translucent vegetation.
//   Only activates when the view direction is nearly aligned with the light
//   direction (forward-scatter through the blade).  Uses a high exponent
//   and a narrow angular window so it does NOT brighten the grass when
//   the surface is merely back-lit at wide angles.
// ---------------------------------------------------------------------------
vec3 SubsurfaceScatter(vec3 V, vec3 L, vec3 N, float translucency,
                       float distortion, float ambient, float power,
                       vec3 albedo)
{
    // Forward-scatter direction: view looking along light direction through
    // the thin surface.  Uses dot(V, -L) as the base angle.
    float VdotL = max(0.0, dot(V, -L));

    // Narrow the scatter lobe — only fire when V is closely aligned with L.
    // pow with high exponent (power, typically 12-16) plus a threshold
    // ensures only a tight cone around the light direction contributes.
    float scatter = pow(VdotL, power) * translucency;

    // Very small ambient term so there is minimal constant back-lit glow.
    float backContrib = scatter + ambient * translucency * 0.15;
    return albedo * backContrib;
}

// ---------------------------------------------------------------------------
// Direct BRDF for vegetation materials.
//
// Algorithm:
//   1. Wrap diffuse for soft Lambert (instead of hard N·L clamp)
//   2. Cook-Torrance GGX specular with elevated F0 for waxy/wet grass
//   3. Forward scatter (narrow-angle translucency)
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

    // ── Specular GGX — enhanced for grass ───────────────────────────────────
    // Grass has a waxy cuticle layer: high F0 for strong specular.
    // Clamp roughness low to keep the highlight tight and prominent.
    if (NoL > 0.0)
    {
        vec3  F0 = mix(vec3(0.20), brdf.albedo, brdf.metallic);
        float specRough = clamp(brdf.roughness, 0.15, 0.6);
        float D  = DistributionTrowbridgeReitzGGX(N, H, specRough);
        float G  = GeometrySmith(N, V, L, specRough);
        vec3  F  = FresnelSchlick(NoH, F0);

        float denom = 4.0 * max(NoL, 0.001) * NoV;
        specular = (D * G * F) / denom;
    }

    // ── Scatter disabled ────────────────────────────────────────────────────
    subsurfaceTransmission = vec3(0.0);
}

// ---------------------------------------------------------------------------
// Full vegetation direct light loop (directional + point + spot).
// ---------------------------------------------------------------------------
void CalculateDirectLight_Vegetation(BRDFData brdfData, VegetationParams veg,
                                     sampler2DArray cascadeShadowMap, float viewDepth,
                                     sampler2D punctualShadowMap,
                                     inout LightResult lightResult)
{
    lightResult.directDiffuse  = vec3(0);
    lightResult.directSpecular = vec3(0);

    // ── Directional light ───────────────────────────────────────────────────
    {
        vec3 dR = vec3(0), sR = vec3(0), tR = vec3(0);
        DirectBRDF_Vegetation(brdfData, uDirectionLight.direction.rgb, veg, dR, sR, tR);

        vec3 lightEnergy = uDirectionLight.color.rgb * uDirectionLight.intensity;
        float shadow = SampleCascadeShadow(brdfData.positionWS, cascadeShadowMap, viewDepth);

        lightResult.directDiffuse  += dR  * lightEnergy * shadow;
        lightResult.directSpecular += sR  * lightEnergy * shadow;
        lightResult.directDiffuse  += tR  * lightEnergy * shadow;
    }

    // ── Point lights ────────────────────────────────────────────────────────
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

        vec3 dR = vec3(0), sR = vec3(0), tR = vec3(0);
        DirectBRDF_Vegetation(brdfData, lightDirection, veg, dR, sR, tR);

        vec3 lightEnergy = pointLight.color.rgb * pointLight.intensity * attenuation;
        lightResult.directDiffuse  += (dR + tR) * lightEnergy * pShadow;
        lightResult.directSpecular += sR * lightEnergy * pShadow;
    }

    // ── Spot lights ─────────────────────────────────────────────────────────
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

        vec3 dR = vec3(0), sR = vec3(0), tR = vec3(0);
        DirectBRDF_Vegetation(brdfData, lightDirection, veg, dR, sR, tR);

        vec3 lightEnergy = spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        lightResult.directDiffuse  += (dR + tR) * lightEnergy * sShadow;
        lightResult.directSpecular += sR * lightEnergy * sShadow;
    }
}

// ---------------------------------------------------------------------------
// Ambient BRDF for vegetation (reuse standard PBR ambient with higher F0).
// ---------------------------------------------------------------------------
void AmbientBRDF_Vegetation(BRDFData brdf, vec3 viewDirection, vec4 giVisSH,
                            inout vec3 diffuse, inout vec3 specular)
{
    // Use the standard PBR ambient but with the elevated F0
    vec3 savedFresnel = brdf.fresnel0;
    brdf.fresnel0 = vec3(0.20);  // waxy cuticle — strong specular
    AmbientBRDF(brdf, viewDirection, giVisSH, diffuse, specular);
    brdf.fresnel0 = savedFresnel;
}


#endif // BRDF_VEGETATION_INCLUDED
