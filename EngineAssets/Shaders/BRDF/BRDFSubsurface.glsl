#ifndef BRDF_SUBSURFACE_INCLUDED
#define BRDF_SUBSURFACE_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

struct SubsurfaceParams
{
    float thickness;
    float subsurfacePower;
    float subsurfaceAmount;
    float backlightShadowRelax;
    vec3  subsurfaceColor;
};

void DirectBRDF_Subsurface(BRDFData brdf, vec3 lightDirection, SubsurfaceParams sss,
                           inout vec3 diffuse, inout vec3 specular,
                           inout vec3 subsurfaceTransmission)
{
    vec3 V = brdf.viewDirection;
    vec3 N = brdf.normal;
    vec3 L = lightDirection;
    vec3 H = normalize(V + L);

    float NoL = dot(N, L);
    float NoH = clamp(dot(N, H), 0.0, 1.0);
    float LoH = clamp(dot(L, H), 0.0, 1.0);
    float NoV = max(dot(N, V), 0.001);
    float NoLClamped = max(NoL, 0.0);

    float thickness = clamp(sss.thickness, 0.0, 1.0);
    float thinness = 1.0 - thickness;
    float amount = clamp(sss.subsurfaceAmount, 0.0, 1.0);

    vec3 Fr = vec3(0.0);
    if (NoL > 0.0)
    {
        vec3 F0 = mix(brdf.fresnel0, brdf.albedo, brdf.metallic);
        float D = DistributionTrowbridgeReitzGGX(N, H, brdf.roughness);
        float G = GeometrySmith(N, V, L, brdf.roughness);
        vec3 F = FresnelSchlick(LoH, F0);
        float denom = 4.0 * max(NoL, 0.001) * NoV;
        Fr = (D * G * F) / denom;
    }

    vec3 F0Diff = mix(brdf.fresnel0, brdf.albedo, brdf.metallic);
    vec3 kS = FresnelSchlick(NoH, F0Diff);
    vec3 kD = (vec3(1.0) - kS) * (1.0 - brdf.metallic);

    diffuse = brdf.albedo * kD *
              (1.0 - amount * thinness) *
              NoLClamped / PI;
    specular = Fr * NoLClamped;

    float scatterFacing = exp2(sss.subsurfacePower * (dot(V, -L) - 1.0));
    float backWrap = clamp(NoL * thickness + thinness, 0.0, 1.0);
    float transmissionShape = mix(backWrap * 0.5, 1.0, scatterFacing);

    vec3 transmissionTint = brdf.albedo * max(sss.subsurfaceColor, vec3(0.0));
    subsurfaceTransmission = transmissionTint * transmissionShape *
                             thinness * amount / PI;
}

void AmbientBRDF_Subsurface(BRDFData brdf, SubsurfaceParams sss, vec3 viewDirection,
                            inout vec3 diffuse, inout vec3 specular)
{
    AmbientBRDF(brdf, viewDirection, diffuse, specular);

    float thinness = 1.0 - clamp(sss.thickness, 0.0, 1.0);
    diffuse += brdf.indirectDiffuse * brdf.albedo * max(sss.subsurfaceColor, vec3(0.0)) *
               thinness * clamp(sss.subsurfaceAmount, 0.0, 1.0) * brdf.ao * 0.25;
}

float SubsurfaceTransmissionShadow(float shadowValue, float NoL, SubsurfaceParams sss)
{
    float thinness = 1.0 - clamp(sss.thickness, 0.0, 1.0);
    float backFacing = 1.0 - step(0.0, NoL);
    return mix(shadowValue, 1.0, backFacing * thinness * sss.backlightShadowRelax);
}

void CalculateDirectLight_Subsurface(BRDFData brdfData, SubsurfaceParams sss,
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
        vec3 tR = vec3(0.0);
        vec3 L = uDirectionLight.direction.rgb;
        DirectBRDF_Subsurface(brdfData, L, sss, dR, sR, tR);

        vec3 lightEnergy = uDirectionLight.color.rgb * uDirectionLight.intensity;
        dR *= lightEnergy;
        sR *= lightEnergy;
        tR *= lightEnergy;

        float shadowValue = min(SampleCascadeShadow(brdfData.positionWS, brdfData.normal, cascadeShadowMap, viewDepth), screenSpaceShadow);
        dR *= shadowValue;
        sR *= shadowValue;
        tR *= SubsurfaceTransmissionShadow(shadowValue, dot(brdfData.normal, L), sss);

        lightResult.directDiffuse += dR + tR;
        lightResult.directSpecular += sR;
    }

    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3 L = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(L);
        if (distance > pointLight.radius) continue;

        L /= distance;
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;
        float shadowValue = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, L, punctualShadowMap, int(pointLight.shadowIndex));

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        vec3 tR = vec3(0.0);
        DirectBRDF_Subsurface(brdfData, L, sss, dR, sR, tR);

        vec3 lightEnergy = pointLight.color.rgb * pointLight.intensity * attenuation;
        dR *= lightEnergy * shadowValue;
        sR *= lightEnergy * shadowValue;
        tR *= lightEnergy * SubsurfaceTransmissionShadow(shadowValue, dot(brdfData.normal, L), sss);

        lightResult.directDiffuse += dR + tR;
        lightResult.directSpecular += sR;
    }

    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3 L = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(L);
        if (distance > spotLight.radius) continue;

        L /= distance;
        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(L));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;
        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);
        float shadowValue = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, L, punctualShadowMap, int(spotLight.shadowIndex));

        vec3 dR = vec3(0.0);
        vec3 sR = vec3(0.0);
        vec3 tR = vec3(0.0);
        DirectBRDF_Subsurface(brdfData, L, sss, dR, sR, tR);

        vec3 lightEnergy = spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        dR *= lightEnergy * shadowValue;
        sR *= lightEnergy * shadowValue;
        tR *= lightEnergy * SubsurfaceTransmissionShadow(shadowValue, dot(brdfData.normal, L), sss);

        lightResult.directDiffuse += dR + tR;
        lightResult.directSpecular += sR;
    }
}

#endif
