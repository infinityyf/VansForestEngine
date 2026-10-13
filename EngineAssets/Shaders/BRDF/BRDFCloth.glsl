#ifndef BRDF_CLOTH_INCLUDED
#define BRDF_CLOTH_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"
#include "ClothData.glsl"

// 单一布料模型：基础介质反射、微纤维 Sheen、薄层漫透射。

#include "ClothBSDF.glsl"
#include "ClothAreaLighting.glsl"

void EvaluateRectLight_Cloth(RectLightData light, BRDFData brdf, ClothMaterialPayload cloth,
    ClothAreaLightingData area, out vec3 diffuse, out vec3 specular)
{
    diffuse = vec3(0.0); specular = vec3(0.0);
    vec3 delta = light.position_halfW.xyz - brdf.positionWS;
    float distanceSquared = dot(delta,delta), range = light.right_range.w;
    if (distanceSquared >= range*range) return;
    float facing = dot(light.normal_halfH.xyz,-delta);
    if ((!bool(light.color_twoSided.w > 0.5) && facing <= 0.0) ||
        abs(facing) <= 1e-8*max(sqrt(distanceSquared),1e-4)) return;
    vec3 d,g,s;
    EvaluateClothRectangle(area,cloth,brdf.albedo,area.worldToFrame*delta,
        area.worldToFrame*(light.right_range.xyz*light.position_halfW.w),
        area.worldToFrame*(light.up_intensity.xyz*light.normal_halfH.w),d,g,s);
    float window = clamp(1.0-pow(distanceSquared/max(range*range,1e-6),2.0),0.0,1.0);
    window = window*window*pow(window*window,max(light.attenuationExp-2.0,0.0));
    // 多边形立体角已经包含面积和距离；这里只叠加现有有限范围窗口。
    vec3 radiance = light.color_twoSided.rgb * light.up_intensity.w * window;
    vec3 diffuseRadiance = radiance, ggxRadiance = radiance, sheenRadiance = radiance;
#ifdef RECT_LIGHT_EMISSIVE_ENABLED
    int slot = int(light.textureSlot);
    if (slot >= 0)
    {
        float solidAngle;
        vec2 diffuseUV = ComputeRectLightIrradianceUV(light,brdf.positionWS,solidAngle);
        float maxMip = float(textureQueryLevels(rectLightEmissive)-1);
        float baseLOD = max(0.0,0.5*log2(max(PI/max(solidAngle,1e-6),1.0)));
        float diffuseLOD = clamp(baseLOD+light.texLodBias,0.0,maxMip);
        bool validUV;
        vec3 normal = vec3(area.worldToFrame[0].z,area.worldToFrame[1].z,area.worldToFrame[2].z);
        vec2 specularUV = ComputeRectLightSpecularUV(light,normal,normalize(brdf.viewDirection),brdf.positionWS,validUV);
        if (!validUV || brdf.roughness >= 0.5) specularUV = diffuseUV;
        diffuseRadiance *= textureLod(rectLightEmissive,vec3(diffuseUV,float(slot)),diffuseLOD).rgb;
        ggxRadiance *= textureLod(rectLightEmissive,vec3(specularUV,float(slot)),
            clamp(baseLOD+brdf.roughness*maxMip*0.5+light.texLodBias,0.0,maxMip)).rgb;
        sheenRadiance *= textureLod(rectLightEmissive,vec3(diffuseUV,float(slot)),
            clamp(baseLOD+cloth.transmittanceRoughness.a*maxMip*0.5+light.texLodBias,0.0,maxMip)).rgb;
    }
#endif
    diffuse = d * diffuseRadiance;
    specular = g * ggxRadiance + s * sheenRadiance;
}

void AddClothRectLight(uint index, BRDFData brdf, ClothMaterialPayload cloth, ClothAreaLightingData area,
    sampler2DShadow shadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT], bool fallbackSelected, inout LightResult result)
{
    RectLightData light = GetRectLight(int(index));
    light.color_twoSided.rgb *= SampleSurfaceLightCookie(129+int(index),brdf.positionWS);
    vec3 d,s;
    EvaluateRectLight_Cloth(light,brdf,cloth,area,d,s);
    vec3 delta = light.position_halfW.xyz-brdf.positionWS;
    float distance = max(length(delta),1e-5);
    vec3 L = delta/distance, shadowN = ClothLightFacingNormal(brdf.normal,L);
    float shadow = SampleRectShadowMapBRDF(brdf.positionWS,shadowN,L,shadowMap,int(index));
    if (fallbackSelected) shadow = BlendPunctualShadowFallback(shadow,
        SamplePunctualScreenSpaceShadow(brdf.positionWS,shadowN,L,distance),2u,int(index));
    result.directDiffuse += d*shadow;
    result.directSpecular += s*shadow;
}

void DirectBRDF_Cloth(BRDFData brdf, ClothMaterialPayload cloth,
    vec3 tangent, vec3 lightDirection, inout vec3 diffuse, inout vec3 specular)
{
    ClothShadingData surface = ClothShadingData(brdf.albedo, brdf.normal,
        brdf.viewDirection, brdf.roughness);
    DirectBRDF_Cloth(surface, cloth, tangent, lightDirection, diffuse, specular);
}

void AmbientBRDF_Cloth(BRDFData brdf, ClothMaterialPayload cloth,
                       vec3 clothTangent, vec3 viewDirection, vec3 backIrradiance,
                       inout vec3 diffuse, inout vec3 specular)
{
    vec3 N = normalize(brdf.normal);
    vec3 V = normalize(viewDirection);
    if (dot(N, V) < 0.0)
        N = -N;

    float NoV = clamp(dot(N, V), 0.0, 1.0);
    float roughness = clamp(brdf.roughness, 0.045, 1.0);

    vec3 sheenColor = ClothSheenColor(cloth);
    vec3 sheenEnergy = sheenColor * ClothDirectionalAlbedo(NoV, cloth.transmittanceRoughness.a);

    vec3 R = reflect(-V, N);
    float ssrFade = 1.0 - smoothstep(reflectionProbeLightingParams.x, reflectionProbeLightingParams.y, roughness);
    // The existing SSR sampler/resolve is isotropic GGX. It cannot supply the
    // Charlie lobe or an anisotropic GGX lobe without changing that estimator.
    float ssrMask = cloth.surface.y == 0.0 && cloth.surface.x > 0.0
        ? clamp(brdf.indirectSpecular.a * ssrFade, 0.0, 1.0) : 0.0;
    vec3 environmentLighting = vec3(0.0);
	if (ssrMask < 1.0 || Max3Cloth(sheenEnergy) > 0.0)
	{
		ReflectionProbeSample probeSample = SampleReflectionProbes(brdf.positionWS, N, R, roughness);
		vec3 iblSpec = probeSample.specular;
		if (probeSample.coverage < 1.0)
		{
			float lod = GetMipLevelFromRoughness(roughness);
			vec3 skySpec = SampleSkySpecularCube(PreConvSpecularEnvironment, R, lod);
			#ifdef AMBIENT_SKY_CACHE_ENABLED
				AmbientSkyTransmittanceSample skyVisibility = SampleAmbientSkyTransmittance(brdf.positionWS, R, roughness);
				skySpec *= skyVisibility.visibility;
			#endif
			iblSpec = mix(skySpec, probeSample.specular, probeSample.coverage);
		}
		environmentLighting = iblSpec;
	}

    vec3 baseLighting = mix(environmentLighting, brdf.indirectSpecular.rgb, ssrMask);

    float baseScale = clamp(1.0 - Max3Cloth(sheenEnergy), 0.0, 1.0);
    if (cloth.surface.x > 0.0)
    {
        vec3 F = fresnelSchlickRoughness(NoV, vec3(0.04), roughness);
        vec2 environmentBRDF = texture(BRDFLUT, vec2(NoV, 1.0 - roughness)).rg;
        specular = (baseLighting * (F * environmentBRDF.x + environmentBRDF.y) * cloth.surface.x * baseScale
                   + environmentLighting * sheenEnergy) * brdf.ao;
    }
    else
    {
        specular = environmentLighting * sheenEnergy * brdf.ao;
    }
    vec3 tau = cloth.transmittanceRoughness.rgb;
    diffuse = (brdf.albedo * brdf.indirectDiffuse * ClothDiffuseReflectanceScale(NoV, cloth) *
        (vec3(1.0) - tau) + tau * backIrradiance) * brdf.ao;
    specular *= vec3(1.0) - cloth.transmittanceRoughness.rgb;
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
		vec3 directionalIrradiance =
			uDirectionLight.color.rgb * uDirectionLight.intensity * SampleSurfaceLightCookie(0, brdf.positionWS);
		lightResult.directDiffuse += dR * directionalIrradiance * shadow;
		lightResult.directSpecular += sR * directionalIrradiance * shadow;
    }

#ifdef TILE_LIGHT
    TileLightHeader tileLightHdr = GetFragTileLightHeader();

    for (uint ptk = 0u; ptk < tileLightHdr.pointCount; ++ptk)
    {
        uint i = tileLightIndices[tileLightHdr.pointOffset + ptk];
        PointLightData pl = GetPointLight(int(i));
        pl.color.rgb *= SampleSurfaceLightCookie(1 + int(i), brdf.positionWS);
        vec3 lightDir = pl.position.xyz - brdf.positionWS;
        float dist = length(lightDir);
        if (dist > pl.radius) continue;
        lightDir /= max(dist, 1e-5);

        float attenuation = 1.0 - (dist / pl.radius);
        attenuation *= attenuation;
        vec3 shadowNormal = ClothLightFacingNormal(brdf.normal,lightDir);
        float shadowValue = SamplePointShadowMapBRDF(brdf.positionWS, shadowNormal, lightDir, punctualShadowMap, int(i));
        if (IsPointShadowFallbackSelected(tileLightHdr, i))
        {
            shadowValue = BlendPunctualShadowFallback(
                shadowValue,
                SamplePunctualScreenSpaceShadow(brdf.positionWS, shadowNormal, lightDir, dist),
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
        sl.color.rgb *= SampleSurfaceLightCookie(65 + int(i), brdf.positionWS);
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

        vec3 shadowNormal = ClothLightFacingNormal(brdf.normal,lightDir);
        float shadowValue = SampleSpotShadowMapBRDF(brdf.positionWS, shadowNormal, lightDir, punctualShadowMap, int(i));
        if (IsSpotShadowFallbackSelected(tileLightHdr, i))
        {
            shadowValue = BlendPunctualShadowFallback(
                shadowValue,
                SamplePunctualScreenSpaceShadow(brdf.positionWS, shadowNormal, lightDir, dist),
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
        pl.color.rgb *= SampleSurfaceLightCookie(1 + int(i), brdf.positionWS);
        vec3 lightDir = pl.position.xyz - brdf.positionWS;
        float dist = length(lightDir);
        if (dist > pl.radius) continue;
        lightDir /= max(dist, 1e-5);

        float attenuation = 1.0 - (dist / pl.radius);
        attenuation *= attenuation;
        vec3 shadowNormal = ClothLightFacingNormal(brdf.normal,lightDir);
        float shadowValue = SamplePointShadowMapBRDF(brdf.positionWS, shadowNormal, lightDir, punctualShadowMap, int(i));
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
        sl.color.rgb *= SampleSurfaceLightCookie(65 + int(i), brdf.positionWS);
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

        vec3 shadowNormal = ClothLightFacingNormal(brdf.normal,lightDir);
        float shadowValue = SampleSpotShadowMapBRDF(brdf.positionWS, shadowNormal, lightDir, punctualShadowMap, int(i));
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

#ifdef TILE_LIGHT
    uint clothRectCount = tileLightHdr.rectCount;
#else
    uint clothRectCount = min(GetRectLightCount(),uint(MAX_RECT_LIGHTS));
#endif
    if (clothRectCount > 0u)
    {
        ClothShadingData surface = ClothShadingData(brdf.albedo,brdf.normal,brdf.viewDirection,brdf.roughness);
        ClothAreaLightingData area = PrepareClothAreaLighting(surface,cloth,clothTangent);
        for (uint k = 0u; k < clothRectCount; ++k)
        {
#ifdef TILE_LIGHT
            uint index = tileLightIndices[tileLightHdr.rectOffset+k];
            bool fallbackSelected = IsRectShadowFallbackSelected(tileLightHdr,index);
#else
            uint index = k;
            bool fallbackSelected = false;
#endif
            AddClothRectLight(index,brdf,cloth,area,punctualShadowMap,fallbackSelected,lightResult);
        }
    }
}

#endif // BRDF_CLOTH_INCLUDED
