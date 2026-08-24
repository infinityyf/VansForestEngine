#ifndef BRDF_SUBSURFACE_INCLUDED
#define BRDF_SUBSURFACE_INCLUDED

#include "../Common/Common.glsl"
#include "BRDFData.glsl"

// Opaque diffuse BSSRDF parameters. Distances are in world metres. The
// material remains an opaque dielectric: this is deliberately independent of
// the glass/refraction path.
struct SubsurfaceParams
{
    vec3  scatteringDistance;
    float thickness;
    float subsurfaceAmount;
};

// Disney/Burley normalized-diffusion transmittance integrated over the plane:
// T(d) = A/4 * (exp(-s*d) + 3*exp(-s*d/3)).
vec3 BurleyTransmittance(vec3 scatteringDistance, vec3 diffuseReflectance,
                         float thickness)
{
    vec3 shape = 1.0 / max(scatteringDistance, vec3(1e-5));
    vec3 expOneThird = exp(-shape * max(thickness, 0.0) / 3.0);
    return clamp(0.25 * clamp(diffuseReflectance, 0.0, 1.0) *
                 expOneThird * (expOneThird * expOneThird + 3.0),
                 vec3(0.0), vec3(1.0));
}

// Exact inverse CDF of Burley's normalized diffusion profile.  The input is
// the regular CDF in [0, 1), and scatteringDistance is 1 / shape.  This is the
// analytic form described by Golubev; unlike the older fitted inverse it does
// not shrink the long tail of the kernel.
float SampleBurleyRadius(float u, float scatteringDistance)
{
    float ccdf = 1.0 - clamp(u, 0.0, 1.0 - 1e-5);
    float fourU = 4.0 * ccdf;
    float g = 1.0 + fourU *
              (2.0 * ccdf + sqrt(1.0 + fourU * ccdf));
    float n = pow(g, -1.0 / 3.0);
    float p = g * n * n;
    float c = 1.0 + p + n;
    float x = 3.0 * log(c / max(fourU, 1e-6));
    return max(x, 0.0) * scatteringDistance;
}

// Importance weight for a screen-space disk sample. Samples are distributed
// using the reference channel at sampledRadius, then the profile is evaluated
// at the actual Euclidean distance between the entry and exit surface points.
// The radial Jacobian cancels the 1/r term in R(r), so the expression remains
// finite at the origin. Normalization of the finite sample set is performed by
// the caller to preserve energy exactly.
vec3 BurleyProfileImportanceWeight(float sampledRadius, float actualRadius,
                                   vec3 shape, float referenceShape)
{
    vec3 channelExp = exp(-shape * max(actualRadius, 0.0) / 3.0);
    float referenceExp = exp(-referenceShape * max(sampledRadius, 0.0) / 3.0);
    vec3 numerator = shape * channelExp * (1.0 + channelExp * channelExp);
    float denominator = referenceShape * referenceExp *
                        (1.0 + referenceExp * referenceExp);
    return numerator / max(denominator, 1e-8);
}

vec3 DirectBurleyTransmission(BRDFData brdf, vec3 lightDirection,
                              SubsurfaceParams sss)
{
    vec3 N = brdf.normal;
    vec3 V = brdf.viewDirection;
    vec3 L = lightDirection;

    float backNoL = max(dot(-N, L), 0.0);
    float NoV = max(dot(N, V), 0.0);
    if (backNoL <= 0.0 || NoV <= 0.0 || sss.subsurfaceAmount <= 0.0)
        return vec3(0.0);

    // The thickness map stores normal thickness. Oblique incidence increases
    // the optical path through the slab.
    float opticalPath = sss.thickness / max(backNoL, 0.2);
    vec3 transmittance = BurleyTransmittance(
        sss.scatteringDistance, brdf.albedo, opticalPath);

    vec3 entryF = FresnelSchlick(backNoL, brdf.fresnel0);
    vec3 exitF = FresnelSchlick(NoV, brdf.fresnel0);
    vec3 interfaceTransmission = (vec3(1.0) - entryF) * (vec3(1.0) - exitF);

    return transmittance * interfaceTransmission *
           (backNoL / PI) * clamp(sss.subsurfaceAmount, 0.0, 1.0);
}

// Transmission is evaluated separately from the reflected GGX/SSS result so
// that front lighting and back lighting never spend the same energy twice.
void CalculateDirectTransmission_Subsurface(
    BRDFData brdfData, SubsurfaceParams sss,
	sampler2DShadow punctualShadowMap[PUNCTUAL_SHADOW_ATLAS_COUNT], float directionalShadow,
    inout vec3 transmission)
{
    transmission = vec3(0.0);

    vec3 L = uDirectionLight.direction.rgb;
    vec3 directionalTerm = DirectBurleyTransmission(brdfData, L, sss);
    transmission += directionalTerm * uDirectionLight.color.rgb *
                    uDirectionLight.intensity * directionalShadow;

    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3 toLight = pointLight.position.xyz - brdfData.positionWS;
        float distanceToLight = length(toLight);
        if (distanceToLight <= 1e-4 || distanceToLight > pointLight.radius)
            continue;

        L = toLight / distanceToLight;
        float attenuation = 1.0 - distanceToLight / pointLight.radius;
        attenuation *= attenuation;
        float shadow = SamplePointShadowMapBRDF(
            brdfData.positionWS, brdfData.normal, L,
            punctualShadowMap, int(i));
        transmission += DirectBurleyTransmission(brdfData, L, sss) *
                        pointLight.color.rgb * pointLight.intensity *
                        attenuation * shadow;
    }

    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3 toLight = spotLight.position.xyz - brdfData.positionWS;
        float distanceToLight = length(toLight);
        if (distanceToLight <= 1e-4 || distanceToLight > spotLight.radius)
            continue;

        L = toLight / distanceToLight;
        float cone = dot(normalize(spotLight.direction.xyz), L);
        float outer = cos(spotLight.outerConeAngle);
        if (cone < outer)
            continue;

        float inner = cos(spotLight.innerConeAngle);
        float coneAttenuation = clamp(
            (cone - outer) / max(inner - outer, 1e-4), 0.0, 1.0);
        float attenuation = 1.0 - distanceToLight / spotLight.radius;
        attenuation *= attenuation;
        float shadow = SampleSpotShadowMapBRDF(
            brdfData.positionWS, brdfData.normal, L,
            punctualShadowMap, int(i));
        transmission += DirectBurleyTransmission(brdfData, L, sss) *
                        spotLight.color.rgb * spotLight.intensity *
                        attenuation * coneAttenuation * shadow;
    }
}

#endif
