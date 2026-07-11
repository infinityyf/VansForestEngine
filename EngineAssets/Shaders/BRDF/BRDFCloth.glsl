#ifndef BRDF_CLOTH_INCLUDED
#define BRDF_CLOTH_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// =============================================================================
// Cloth BRDF (Material ID = MATERIAL_ID_CLOTH)
//
// Direct light:
//   - Charlie NDF for fabric sheen
//   - Neubelt visibility term for cloth
//   - albedo-derived sheen tint with independent material strength
//
// GBuffer convention for cloth in Deferred.frag:
//   brdf.roughness = effective sheen roughness
//   brdf.metallic  = sheen strength, not metallic
//   brdf.fresnel0.r = thin-cloth translucency
// =============================================================================

float D_Charlie(float roughness, float NoH)
{
    float alpha = max(roughness * roughness, 1e-4);
    float invAlpha = 1.0 / alpha;
    float sin2h = max(1.0 - NoH * NoH, 0.0078125);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

float V_Cloth(float NoV, float NoL)
{
    float denom = 4.0 * (NoL + NoV - NoL * NoV);
    return 1.0 / max(denom, 1e-5);
}

float Max3(vec3 v)
{
    return max(max(v.x, v.y), v.z);
}

vec3 ClothSheenTint(vec3 albedo)
{
    vec3 safeAlbedo = max(albedo, vec3(0.0));
    float luma = max(Luminance(safeAlbedo), 1e-4);
    vec3 tint = safeAlbedo / luma;
    return clamp(mix(vec3(1.0), tint, 0.5), vec3(0.0), vec3(4.0));
}

vec3 ClothSheenColor(BRDFData brdf)
{
    float strength = clamp(brdf.metallic, 0.0, 1.0);
    return ClothSheenTint(brdf.albedo) * (0.24 * strength);
}

float ClothTranslucency(BRDFData brdf)
{
    return clamp(brdf.fresnel0.r, 0.0, 1.0);
}

vec3 SampleClothDFG(float NoV, float sheenRoughness)
{
    return texture(ClothBRDFLUT, vec2(clamp(NoV, 0.0, 1.0), clamp(sheenRoughness, 0.0, 1.0))).rgb;
}

void DirectBRDF_Cloth(BRDFData brdf, vec3 lightDirection,
                      inout vec3 diffuse, inout vec3 specular)
{
    diffuse = vec3(0.0);
    specular = vec3(0.0);

    vec3 N = normalize(brdf.normal);
    vec3 V = normalize(brdf.viewDirection);
    vec3 L = normalize(lightDirection);

    if (dot(N, V) < 0.0)
        N = -N;

    float NoV = max(dot(N, V), 1e-4);
    float signedNoL = dot(N, L);
    float frontNoL = max(signedNoL, 0.0);
    float backNoL = max(-signedNoL, 0.0);
    float twoSidedNoL = max(frontNoL, backNoL);
    if (twoSidedNoL <= 0.0)
        return;

    float roughness = clamp(brdf.roughness, 0.045, 1.0);
    float translucency = ClothTranslucency(brdf);
    vec3 sheenColor = ClothSheenColor(brdf);

    vec3 F = vec3(0.0);
    vec3 sheen = vec3(0.0);
    if (frontNoL > 0.0)
    {
        vec3 H = normalize(L + V);
        float NoH = clamp(dot(N, H), 0.0, 1.0);
        float VoH = clamp(dot(V, H), 0.0, 1.0);

        float D = D_Charlie(roughness, NoH);
        float Vis = V_Cloth(NoV, frontNoL);

        float grazing = pow(1.0 - VoH, 5.0);
        F = sheenColor * mix(0.25, 1.0, grazing);
        sheen = D * Vis * F * frontNoL;
    }

    float diffuseEnergy = clamp(1.0 - Max3(F) * 0.75, 0.0, 1.0);
    float transmittedNoL = backNoL * mix(0.15, 0.75, translucency);
    float diffuseNoL = frontNoL + transmittedNoL;

    diffuse = (brdf.albedo * diffuseEnergy / PI) * diffuseNoL;
    specular = sheen;
}

void AmbientBRDF_Cloth(BRDFData brdf, vec3 viewDirection,
                       inout vec3 diffuse, inout vec3 specular)
{
    vec3 N = normalize(brdf.normal);
    vec3 V = normalize(viewDirection);
    if (dot(N, V) < 0.0)
        N = -N;

    float NoV = clamp(dot(N, V), 0.0, 1.0);
    float roughness = clamp(brdf.roughness, 0.045, 1.0);

    vec3 lut = SampleClothDFG(NoV, roughness);
    vec3 sheenColor = ClothSheenColor(brdf);
    vec3 sheenEnergy = mix(lut.rrr, lut.ggg, clamp(sheenColor, vec3(0.0), vec3(1.0)));
    sheenEnergy *= mix(vec3(1.0), vec3(lut.b), 0.5);
    sheenEnergy = clamp(sheenEnergy, vec3(0.0), vec3(0.95));

    vec3 R = reflect(-V, N);
    float lod = GetMipLevelFromRoughness(roughness);
    ReflectionProbeSample probeSample = SampleReflectionProbes(brdf.positionWS, N, R, roughness);
    vec3 skySpec = textureLod(PreConvSpecularEnvironment, R, lod).rgb * reflectionProbeLightingParams.z;
    vec3 iblSpec = mix(skySpec, probeSample.specular, probeSample.coverage);

    float ssrFade = 1.0 - smoothstep(reflectionProbeLightingParams.x, reflectionProbeLightingParams.y, roughness);
    float ssrMask = clamp(brdf.indirectSpecular.a * ssrFade, 0.0, 1.0);
    vec3 specLighting = mix(iblSpec, brdf.indirectSpecular.rgb, ssrMask);

    float diffuseEnergy = clamp(1.0 - Max3(sheenEnergy), 0.0, 1.0);
    diffuse = brdf.albedo * brdf.indirectDiffuse * diffuseEnergy * brdf.ao;
    specular = specLighting * sheenEnergy * brdf.ao;
}

void CalculateDirectLight_Cloth(BRDFData brdf,
                                sampler2DArray cascadeShadowMap, float viewDepth,
                                sampler2D punctualShadowMap,
                                float screenSpaceShadow,
                                inout LightResult lightResult)
{
    lightResult.directDiffuse = vec3(0.0);
    lightResult.directSpecular = vec3(0.0);

    {
        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, uDirectionLight.direction.rgb, dR, sR);

        float shadow = min(SampleCascadeShadow(brdf.positionWS, brdf.normal, cascadeShadowMap, viewDepth), screenSpaceShadow);
        lightResult.directDiffuse += dR * uDirectionLight.color.rgb * uDirectionLight.intensity * shadow;
        lightResult.directSpecular += sR * uDirectionLight.color.rgb * uDirectionLight.intensity * shadow;
    }

#ifdef TILE_LIGHT
    TileLightHeader tileLightHdr = GetFragTileLightHeader();

    for (uint ptk = 0u; ptk < tileLightHdr.pointCount; ++ptk)
    {
        uint i = tileLightIndices[tileLightHdr.pointOffset + ptk];
        PointLightData pl = GetPointLight(int(i));
        vec3 lightDir = pl.position.xyz - brdf.positionWS;
        float dist = length(lightDir);
        if (dist > pl.radius) continue;
        lightDir /= max(dist, 1e-5);

        float attenuation = 1.0 - (dist / pl.radius);
        attenuation *= attenuation;
        attenuation = min(attenuation, SamplePointShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(pl.shadowIndex)));

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(pl.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDir, vec3(0.0, -1.0, 0.0));
#endif

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, lightDir, dR, sR);
        lightResult.directDiffuse += dR * pl.color.rgb * pl.intensity * attenuation;
        lightResult.directSpecular += sR * pl.color.rgb * pl.intensity * attenuation;
    }

    for (uint spk = 0u; spk < tileLightHdr.spotCount; ++spk)
    {
        uint i = tileLightIndices[tileLightHdr.spotOffset + spk];
        SpotLightData sl = GetSpotLight(int(i));
        vec3 lightDir = sl.position.xyz - brdf.positionWS;
        float dist = length(lightDir);
        if (dist > sl.radius) continue;
        lightDir /= max(dist, 1e-5);

        float attenuation = 1.0 - (dist / sl.radius);
        attenuation *= attenuation;
        attenuation = min(attenuation, SampleSpotShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(sl.shadowIndex)));

        float coneAngle = dot(normalize(sl.direction.xyz), lightDir);
        if (coneAngle < cos(sl.outerConeAngle)) continue;

        float innerCone = cos(sl.innerConeAngle);
        float outerCone = cos(sl.outerConeAngle);
        float coneAtten = clamp((coneAngle - outerCone) / max(innerCone - outerCone, 1e-4), 0.0, 1.0);

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(sl.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDir, sl.direction.xyz) * sl.iesIntensityScale;
#endif

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, lightDir, dR, sR);
        lightResult.directDiffuse += dR * sl.color.rgb * sl.intensity * attenuation * coneAtten;
        lightResult.directSpecular += sR * sl.color.rgb * sl.intensity * attenuation * coneAtten;
    }
#else
    for (uint i = 0u; i < uPointLightCount; ++i)
    {
        PointLightData pl = GetPointLight(int(i));
        vec3 lightDir = pl.position.xyz - brdf.positionWS;
        float dist = length(lightDir);
        if (dist > pl.radius) continue;
        lightDir /= max(dist, 1e-5);

        float attenuation = 1.0 - (dist / pl.radius);
        attenuation *= attenuation;
        attenuation = min(attenuation, SamplePointShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(pl.shadowIndex)));

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(pl.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDir, vec3(0.0, -1.0, 0.0));
#endif

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, lightDir, dR, sR);
        lightResult.directDiffuse += dR * pl.color.rgb * pl.intensity * attenuation;
        lightResult.directSpecular += sR * pl.color.rgb * pl.intensity * attenuation;
    }

    for (uint i = 0u; i < uSpotLightCount; ++i)
    {
        SpotLightData sl = GetSpotLight(int(i));
        vec3 lightDir = sl.position.xyz - brdf.positionWS;
        float dist = length(lightDir);
        if (dist > sl.radius) continue;
        lightDir /= max(dist, 1e-5);

        float attenuation = 1.0 - (dist / sl.radius);
        attenuation *= attenuation;
        attenuation = min(attenuation, SampleSpotShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(sl.shadowIndex)));

        float coneAngle = dot(normalize(sl.direction.xyz), lightDir);
        if (coneAngle < cos(sl.outerConeAngle)) continue;

        float innerCone = cos(sl.innerConeAngle);
        float outerCone = cos(sl.outerConeAngle);
        float coneAtten = clamp((coneAngle - outerCone) / max(innerCone - outerCone, 1e-4), 0.0, 1.0);

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(sl.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDir, sl.direction.xyz) * sl.iesIntensityScale;
#endif

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, lightDir, dR, sR);
        lightResult.directDiffuse += dR * sl.color.rgb * sl.intensity * attenuation * coneAtten;
        lightResult.directSpecular += sR * sl.color.rgb * sl.intensity * attenuation * coneAtten;
    }
#endif
}

#endif // BRDF_CLOTH_INCLUDED
