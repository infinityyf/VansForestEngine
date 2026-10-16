#ifndef VANS_GI_DIRECT_LIGHTING_GLSL
#define VANS_GI_DIRECT_LIGHTING_GLSL
#include "../Lights/LightCookie.glsl"
#include "../Lights/RectLightEmission.glsl"

float FetchGICascadeDepth(sampler2D shadowMap, ivec2 texel)
{
    ivec2 size = textureSize(shadowMap, 0);
    return texelFetch(shadowMap, clamp(texel, ivec2(0), size - ivec2(1)), 0).r;
}

float CompareGICascadeDepthBilinear(sampler2D shadowMap, vec2 texelPosition, float receiverDepth)
{
    ivec2 base = ivec2(floor(texelPosition - vec2(0.5)));
    vec2 f = fract(texelPosition - vec2(0.5));
    float c00 = FetchGICascadeDepth(shadowMap, base) < receiverDepth ? 0.0 : 1.0;
    float c10 = FetchGICascadeDepth(shadowMap, base + ivec2(1, 0)) < receiverDepth ? 0.0 : 1.0;
    float c01 = FetchGICascadeDepth(shadowMap, base + ivec2(0, 1)) < receiverDepth ? 0.0 : 1.0;
    float c11 = FetchGICascadeDepth(shadowMap, base + ivec2(1, 1)) < receiverDepth ? 0.0 : 1.0;
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

// The GI pass binds one cascade layer as sampler2D.  Use a compact, stable PCF
// here instead of filtering R32 depth and comparing the interpolated value.
float SampleGICascadeShadow(vec3 positionWS, vec3 normalWS, sampler2D shadowMap)
{
    const int cascadeIdx = RAYTRACING_CASCADE_INDEX;
    CascadeProjection p = ProjectCascade(positionWS, cascadeIdx);
    if (p.valid <= 0.0)
        return 1.0;

    ivec2 size = textureSize(shadowMap, 0);
    float receiverDepth = p.depth - ComputeCascadeReceiverBias(normalWS, cascadeIdx);
    float radiusTexels = min(max(uDirectionLight.cascadeFilterRadius[cascadeIdx] * 0.1, 1.0), 2.0);
    float confidence = CascadeFootprintConfidence(p.uv, size, radiusTexels);
    if (confidence <= 0.0)
        return 1.0;

    const int sampleCount = 8;
    float visibility = 0.0;
    for (int i = 0; i < sampleCount; ++i)
    {
        vec2 disk = RotateCascadeDiskSample(CascadeDiskSample(i, sampleCount, true), cascadeIdx);
        vec2 texelPosition = p.uv * vec2(size) + disk * radiusTexels;
        visibility += CompareGICascadeDepthBilinear(shadowMap, texelPosition, receiverDepth);
    }
    return mix(1.0, visibility / float(sampleCount), confidence);
}

uint CalculateDirectDiffuse(vec3 positionWS, vec3 normalWS, float footprintMeters, sampler2DArray rectEmission, sampler2D shadowMap, sampler2DShadow punctualShadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT], vec4 surfaceAlbedoRoughness, inout vec3 diffuseResult)
{
    diffuseResult = vec3(0.0);
    vec3 albedo = clamp(surfaceAlbedoRoughness.rgb, vec3(0.0), vec3(1.0));
    vec3 samplePos = positionWS;

    // GI stores outgoing diffuse radiance, including the Lambert albedo / PI term.
    float dirNoL = max(dot(normalWS, uDirectionLight.direction.xyz), 0.0);
    float dirShadow = SampleGICascadeShadow(samplePos, normalWS, shadowMap);
    vec3 directionalIrradiance =
        uDirectionLight.color.rgb * uDirectionLight.intensity * SampleLightCookie(0, samplePos, footprintMeters);
#ifdef GI_WORLD_ENABLED
    // 确认没有太阳贡献时无需世界查询。未知遮挡不能作为有效黑色写入主历史。
    if(dirNoL>0.0 && dirShadow>0.0 && any(greaterThan(directionalIrradiance*albedo,vec3(0))))
    {
        vec3 queryOrigin=samplePos+normalWS*max(gwGrid.x*.75,0.01);
        float queryDistance=GIWorldEnvironmentDistance(queryOrigin);
        GIWorldHit worldSun=GIWorldTrace(queryOrigin,normalize(uDirectionLight.direction.xyz),0.001,queryDistance,true);
        if(!worldSun.valid)return worldSun.failure;
        dirShadow=min(dirShadow,worldSun.t<queryDistance?0.0:worldSun.transmission);
    }
#endif
    diffuseResult += dirNoL * directionalIrradiance * dirShadow * albedo * INV_PI;

    for (uint lightIndex = 0u; lightIndex < uPointLightCount; ++lightIndex)
    {
        PointLightData pointLight = GetPointLight(int(lightIndex));
        vec3 lightDirection = pointLight.position.xyz - samplePos;
        float distance = length(lightDirection);
        if (pointLight.radius <= 1e-5 || distance >= pointLight.radius)
            continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;
        attenuation *= SamplePointShadowMapBRDF(
            samplePos, normalWS, lightDirection, punctualShadowMap, int(lightIndex));

        attenuation *= SampleLightCookie(1 + int(lightIndex), samplePos, footprintMeters);
        float noL = max(dot(normalWS, lightDirection), 0.0);
        diffuseResult += noL * pointLight.color.rgb * pointLight.intensity * attenuation * albedo * INV_PI;
    }

    for (uint lightIndex = 0u; lightIndex < uSpotLightCount; ++lightIndex)
    {
        SpotLightData spotLight = GetSpotLight(int(lightIndex));
        vec3 lightDirection = spotLight.position.xyz - samplePos;
        float distance = length(lightDirection);
        if (spotLight.radius <= 1e-5 || distance >= spotLight.radius)
            continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        float coneAngle = dot(normalize(spotLight.direction.xyz), lightDirection);
        if (coneAngle < cos(spotLight.outerConeAngle))
            continue;

        attenuation *= SampleSpotShadowMapBRDF(
            samplePos, normalWS, lightDirection, punctualShadowMap, int(lightIndex));

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / max(innerConeAngle - outerConeAngle, 1e-5), 0.0, 1.0);

        attenuation *= SampleLightCookie(LIGHT_COOKIE_SPOT_OFFSET + int(lightIndex), samplePos, footprintMeters);
        float noL = max(dot(normalWS, lightDirection), 0.0);
        diffuseResult += noL * spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation * albedo * INV_PI;
    }

    // Probe-hit shading has no camera direction, so it uses a diffuse-only
    // solid-angle approximation for rectangular emitters instead of the
    // view-dependent LTC BRDF path.  This keeps area lights in the DDGI
    // transport graph and uses the same punctual shadow atlas as raster.
    for (uint lightIndex = 0u; lightIndex < GetRectLightCount(); ++lightIndex)
    {
        RectLightData rectLight = GetRectLight(int(lightIndex));
        vec3 toLight = rectLight.position_halfW.xyz - samplePos;
        float distanceSquared = dot(toLight, toLight);
        float range = rectLight.right_range.w;
        if (distanceSquared >= range * range || distanceSquared <= 1e-6)
            continue;

        float distance = sqrt(distanceSquared);
        vec3 lightDirection = toLight / distance;
        float lightFacing = dot(rectLight.normal_halfH.xyz, -lightDirection);
        if (rectLight.color_twoSided.w > 0.5)
            lightFacing = abs(lightFacing);
        if (lightFacing <= 0.0)
            continue;

        float area = 4.0 * rectLight.position_halfW.w * rectLight.normal_halfH.w;
        float rangeFade = 1.0 - distance / max(range, 1e-4);
        rangeFade *= rangeFade;
        float solidAngle = area * lightFacing / max(distanceSquared, 1e-4);
        float shadow = SampleRectShadowMapBRDF(samplePos, normalWS, lightDirection,
            punctualShadowMap, int(lightIndex));
        vec3 emissionTint = vec3(1.0);
        if (rectLight.textureSlot >= 0.0)
        {
            float emissionSolidAngle;
            vec2 uv = ComputeRectLightIrradianceUV(rectLight, samplePos, emissionSolidAngle);
            float lod = clamp(RectLightEmissionBaseLod(emissionSolidAngle) + rectLight.texLodBias,
                0.0, float(textureQueryLevels(rectEmission) - 1));
            emissionTint = textureLod(rectEmission, vec3(uv, rectLight.textureSlot), lod).rgb;
        }
        shadow *= SampleLightCookie(LIGHT_COOKIE_RECT_OFFSET + int(lightIndex), samplePos, footprintMeters);
        diffuseResult += max(dot(normalWS, lightDirection), 0.0) * emissionTint *
            rectLight.color_twoSided.rgb * rectLight.up_intensity.w * solidAngle *
            rangeFade * shadow * albedo * INV_PI;
    }
    return 0u;
}
#endif
