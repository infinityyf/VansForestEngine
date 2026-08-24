#ifndef BRDF_CLOTH_INCLUDED
#define BRDF_CLOTH_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"
#include "ClothData.glsl"

// =============================================================================
// Cloth BRDF families (Material ID = MATERIAL_ID_CLOTH):
//   Fuzz: fitted Charlie sheen layered over diffuse.
//   Silk: anisotropic dielectric GGX with an optional Charlie fiber layer.
//   Thin: Fuzz plus energy-bounded direct back transmission.
// =============================================================================

float D_Charlie(float roughness, float NoH)
{
    float alpha = max(roughness * roughness, 1e-4);
    float invAlpha = 1.0 / alpha;
    float sin2h = max(1.0 - NoH * NoH, 1e-6);
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

float CharlieLambdaFit(float x, float alphaG)
{
    float oneMinusAlphaSq = (1.0 - alphaG) * (1.0 - alphaG);
    float a = mix(21.5473, 25.3245, oneMinusAlphaSq);
    float b = mix(3.82987, 3.32435, oneMinusAlphaSq);
    float c = mix(0.19823, 0.16801, oneMinusAlphaSq);
    float d = mix(-1.97760, -1.27393, oneMinusAlphaSq);
    float e = mix(-4.32054, -4.85967, oneMinusAlphaSq);
    return a / (1.0 + b * pow(max(x, 1e-4), c)) + d * x + e;
}

float CharlieLambda(float cosTheta, float alphaG)
{
    float x = clamp(abs(cosTheta), 1e-4, 1.0);
    return x < 0.5
        ? exp(CharlieLambdaFit(x, alphaG))
        : exp(2.0 * CharlieLambdaFit(0.5, alphaG) - CharlieLambdaFit(1.0 - x, alphaG));
}

float V_Charlie(float roughness, float NoV, float NoL)
{
    float alphaG = max(roughness * roughness, 1e-4);
    float denominator = (1.0 + CharlieLambda(NoV, alphaG) + CharlieLambda(NoL, alphaG))
        * (4.0 * max(NoV, 1e-4) * max(NoL, 1e-4));
    return 1.0 / max(denominator, 1e-5);
}

float Max3Cloth(vec3 v)
{
    return max(max(v.x, v.y), v.z);
}

vec3 ClothAlbedoTint(vec3 albedo)
{
    vec3 safeAlbedo = max(albedo, vec3(0.0));
    float luma = max(Luminance(safeAlbedo), 1e-4);
    return clamp(safeAlbedo / luma, vec3(0.0), vec3(1.0));
}

vec3 ClothSheenColor(BRDFData brdf, ClothMaterialPayload cloth)
{
    vec3 color = max(cloth.sheenColorWeight.rgb, vec3(0.0));
    if ((ClothFlags(cloth) & CLOTH_FLAG_ALBEDO_SHEEN_TINT) != 0u)
        color *= ClothAlbedoTint(brdf.albedo) * 0.24;
    return clamp(color * clamp(cloth.sheenColorWeight.a, 0.0, 1.0), vec3(0.0), vec3(1.0));
}

float ClothDirectionalAlbedo(float NoX, float sheenRoughness)
{
    return clamp(texture(ClothBRDFLUT,
        vec2(clamp(NoX, 0.0, 1.0), clamp(sheenRoughness, 0.0, 1.0))).b, 0.0, 1.0);
}

float ClothEnergyNormalization(float NoX, float sheenRoughness)
{
    return clamp(texture(ClothBRDFLUT,
        vec2(clamp(NoX, 0.0, 1.0), clamp(sheenRoughness, 0.0, 1.0))).r, 0.0, 1.0);
}

float ClothBaseLayerScale(vec3 sheenColor, float roughness, float NoV, float NoL)
{
    float sheenMax = Max3Cloth(sheenColor);
    float viewScale = 1.0 - sheenMax * ClothDirectionalAlbedo(NoV, roughness);
    float lightScale = 1.0 - sheenMax * ClothDirectionalAlbedo(NoL, roughness);
    return clamp(min(viewScale, lightScale), 0.0, 1.0);
}

float D_AnisotropicGGX(vec3 N, vec3 T, vec3 B, vec3 H, float alphaT, float alphaB)
{
    float ToH = dot(T, H);
    float BoH = dot(B, H);
    float NoH = max(dot(N, H), 0.0);
    float d = ToH * ToH / max(alphaT * alphaT, 1e-6)
            + BoH * BoH / max(alphaB * alphaB, 1e-6)
            + NoH * NoH;
    return 1.0 / max(PI * alphaT * alphaB * d * d, 1e-6);
}

float V_AnisotropicGGXCorrelated(vec3 N, vec3 T, vec3 B, vec3 V, vec3 L,
                                 float alphaT, float alphaB)
{
    float NoV = max(dot(N, V), 1e-4);
    float NoL = max(dot(N, L), 1e-4);
    float lambdaV = NoL * length(vec3(alphaT * dot(T, V), alphaB * dot(B, V), NoV));
    float lambdaL = NoV * length(vec3(alphaT * dot(T, L), alphaB * dot(B, L), NoL));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

void ClothAnisotropicRoughness(float roughness, float anisotropy,
                               out float alphaT, out float alphaB)
{
    float alpha = max(roughness * roughness, 0.002);
    float aspect = sqrt(max(1.0 - 0.9 * abs(anisotropy), 0.1));
    float along = max(alpha / aspect, 0.002);
    float across = max(alpha * aspect, 0.002);
    alphaT = anisotropy >= 0.0 ? along : across;
    alphaB = anisotropy >= 0.0 ? across : along;
}

void DirectBRDF_Cloth(BRDFData brdf, ClothMaterialPayload cloth,
                      vec3 clothTangent, vec3 lightDirection,
                      inout vec3 diffuse, inout vec3 specular)
{
    diffuse = vec3(0.0);
    specular = vec3(0.0);

    vec3 N = normalize(brdf.normal);
    vec3 V = normalize(brdf.viewDirection);
    vec3 L = normalize(lightDirection);
    if (dot(N, V) < 0.0)
        N = -N;

    vec3 T = clothTangent - N * dot(N, clothTangent);
    if (dot(T, T) <= 1e-8)
    {
        vec3 fallbackB;
        BuildStableClothFrame(N, T, fallbackB);
    }
    else
        T = normalize(T);
    vec3 B = normalize(cross(N, T));

    float NoV = max(dot(N, V), 1e-4);
    float signedNoL = dot(N, L);
    float frontNoL = max(signedNoL, 0.0);
    float backNoL = max(-signedNoL, 0.0);
    float roughness = clamp(brdf.roughness, 0.045, 1.0);
    vec3 sheenColor = ClothSheenColor(brdf, cloth);
    int model = ClothModel(cloth);

    if (frontNoL > 0.0)
    {
        vec3 halfSum = L + V;
        if (dot(halfSum, halfSum) <= 1e-8)
            return;
        vec3 H = normalize(halfSum);
        float NoH = clamp(dot(N, H), 0.0, 1.0);
        float VoH = clamp(dot(V, H), 0.0, 1.0);

        float sheenD = D_Charlie(roughness, NoH);
        float sheenV = V_Charlie(roughness, NoV, frontNoL);
        float sheenNormalization = min(ClothEnergyNormalization(NoV, roughness),
                                       ClothEnergyNormalization(frontNoL, roughness));
        vec3 sheen = sheenColor * sheenD * sheenV * sheenNormalization;
        float baseScale = ClothBaseLayerScale(sheenColor, roughness, NoV, frontNoL);

        if (model == CLOTH_MODEL_SILK)
        {
            float alphaT;
            float alphaB;
            ClothAnisotropicRoughness(roughness, clamp(cloth.controls.y, -0.95, 0.95), alphaT, alphaB);
            float D = D_AnisotropicGGX(N, T, B, H, alphaT, alphaB);
            float Vis = V_AnisotropicGGXCorrelated(N, T, B, V, L, alphaT, alphaB);
            vec3 F = FresnelSchlick(VoH, vec3(0.04));
            vec3 Fv = FresnelSchlick(NoV, vec3(0.04));
            vec3 Fl = FresnelSchlick(frontNoL, vec3(0.04));
            vec3 baseDiffuse = (vec3(1.0) - Fv) * (vec3(1.0) - Fl) * brdf.albedo / PI;
            diffuse = baseDiffuse * baseScale * frontNoL;
            specular = (D * Vis * F * baseScale + sheen) * frontNoL;
        }
        else
        {
            diffuse = brdf.albedo * (baseScale / PI) * frontNoL;
            specular = sheen * frontNoL;
        }
    }

    if (model != CLOTH_MODEL_SILK && backNoL > 0.0)
    {
        float transmissionStrength = clamp(cloth.transmissionColorStrength.a, 0.0, 1.0);
        float thickness = clamp(cloth.controls.z, 0.0, 1.0);
        float thicknessAttenuation = mix(1.0, 0.15, thickness);
        vec3 transmissionColor = clamp(cloth.transmissionColorStrength.rgb, vec3(0.0), vec3(1.0));
        float transmissionWeight = transmissionStrength * thicknessAttenuation;
        diffuse += brdf.albedo * transmissionColor * (transmissionWeight / PI) * backNoL;
    }
}

void AmbientBRDF_Cloth(BRDFData brdf, ClothMaterialPayload cloth,
                       vec3 clothTangent, vec3 viewDirection,
                       inout vec3 diffuse, inout vec3 specular)
{
    vec3 N = normalize(brdf.normal);
    vec3 V = normalize(viewDirection);
    if (dot(N, V) < 0.0)
        N = -N;

    float NoV = clamp(dot(N, V), 0.0, 1.0);
    float roughness = clamp(brdf.roughness, 0.045, 1.0);

    vec3 sheenColor = ClothSheenColor(brdf, cloth);
    vec3 sheenEnergy = sheenColor * ClothDirectionalAlbedo(NoV, roughness);

    vec3 R = reflect(-V, N);
    float ssrFade = 1.0 - smoothstep(reflectionProbeLightingParams.x, reflectionProbeLightingParams.y, roughness);
    float ssrMask = clamp(brdf.indirectSpecular.a * ssrFade, 0.0, 1.0);
	vec3 specLighting = brdf.indirectSpecular.rgb;
	if (ssrMask < 1.0)
	{
		ReflectionProbeSample probeSample = SampleReflectionProbes(brdf.positionWS, N, R, roughness);
		vec3 iblSpec = probeSample.specular;
		if (probeSample.coverage < 1.0)
		{
			float lod = GetMipLevelFromRoughness(roughness);
			vec3 skySpec = SampleSkySpecularCube(PreConvSpecularEnvironment, R, lod);
			iblSpec = mix(skySpec, probeSample.specular, probeSample.coverage);
		}
		specLighting = mix(iblSpec, brdf.indirectSpecular.rgb, ssrMask);
	}

    float baseScale = clamp(1.0 - Max3Cloth(sheenEnergy), 0.0, 1.0);
    if (ClothModel(cloth) == CLOTH_MODEL_SILK)
    {
        vec3 F = fresnelSchlickRoughness(NoV, vec3(0.04), roughness);
        vec2 environmentBRDF = texture(BRDFLUT, vec2(NoV, 1.0 - roughness)).rg;
        diffuse = brdf.albedo * brdf.indirectDiffuse * (vec3(1.0) - F) * (vec3(1.0) - F)
                  * baseScale * brdf.ao;
        specular = specLighting * ((F * environmentBRDF.x + environmentBRDF.y) * baseScale
                   + sheenEnergy) * brdf.ao;
    }
    else
    {
        diffuse = brdf.albedo * brdf.indirectDiffuse * baseScale * brdf.ao;
        specular = specLighting * sheenEnergy * brdf.ao;
    }
}

void CalculateDirectLight_Cloth(BRDFData brdf, ClothMaterialPayload cloth,
                                vec3 clothTangent,
                                sampler2DShadow punctualShadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT],
								float directionalShadow,
                                inout LightResult lightResult)
{
    lightResult.directDiffuse = vec3(0.0);
    lightResult.directSpecular = vec3(0.0);

    {
        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, cloth, clothTangent, uDirectionLight.direction.rgb, dR, sR);

		float shadow = directionalShadow;
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
        float shadowValue = SamplePointShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(i));
        if (IsPointShadowFallbackSelected(tileLightHdr, i))
        {
            shadowValue = BlendPunctualShadowFallback(
                shadowValue,
                SamplePunctualScreenSpaceShadow(brdf.positionWS, brdf.normal, lightDir, dist),
                0u, int(i));
        }
        attenuation *= shadowValue;

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(pl.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDir, vec3(0.0, -1.0, 0.0));
#endif

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, cloth, clothTangent, lightDir, dR, sR);
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

        float coneAngle = dot(normalize(sl.direction.xyz), lightDir);
        if (coneAngle < cos(sl.outerConeAngle)) continue;

        float innerCone = cos(sl.innerConeAngle);
        float outerCone = cos(sl.outerConeAngle);
        float coneAtten = clamp((coneAngle - outerCone) / max(innerCone - outerCone, 1e-4), 0.0, 1.0);

        float shadowValue = SampleSpotShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(i));
        if (IsSpotShadowFallbackSelected(tileLightHdr, i))
        {
            shadowValue = BlendPunctualShadowFallback(
                shadowValue,
                SamplePunctualScreenSpaceShadow(brdf.positionWS, brdf.normal, lightDir, dist),
                1u, int(i));
        }
        attenuation *= shadowValue;

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(sl.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDir, sl.direction.xyz) * sl.iesIntensityScale;
#endif

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, cloth, clothTangent, lightDir, dR, sR);
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
        float shadowValue = SamplePointShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(i));
        attenuation *= shadowValue;

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(pl.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDir, vec3(0.0, -1.0, 0.0));
#endif

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, cloth, clothTangent, lightDir, dR, sR);
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

        float coneAngle = dot(normalize(sl.direction.xyz), lightDir);
        if (coneAngle < cos(sl.outerConeAngle)) continue;

        float innerCone = cos(sl.innerConeAngle);
        float outerCone = cos(sl.outerConeAngle);
        float coneAtten = clamp((coneAngle - outerCone) / max(innerCone - outerCone, 1e-4), 0.0, 1.0);

        float shadowValue = SampleSpotShadowMapBRDF(brdf.positionWS, brdf.normal, lightDir, punctualShadowMap, int(i));
        attenuation *= shadowValue;

#ifdef IES_PROFILE_ENABLED
        int iesIdx = int(sl.iesProfileIndex);
        if (iesIdx >= 0)
            attenuation *= SampleIESProfile(iesIdx, lightDir, sl.direction.xyz) * sl.iesIntensityScale;
#endif

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        DirectBRDF_Cloth(brdf, cloth, clothTangent, lightDir, dR, sR);
        lightResult.directDiffuse += dR * sl.color.rgb * sl.intensity * attenuation * coneAtten;
        lightResult.directSpecular += sR * sl.color.rgb * sl.intensity * attenuation * coneAtten;
    }
#endif
}

#endif // BRDF_CLOTH_INCLUDED
