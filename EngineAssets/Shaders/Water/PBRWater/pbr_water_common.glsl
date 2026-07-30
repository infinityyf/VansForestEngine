#ifndef PBR_WATER_COMMON_GLSL
#define PBR_WATER_COMMON_GLSL

const float PBRW_PI = 3.14159265358979323846;

float PBRW_Saturate(float v) { return clamp(v, 0.0, 1.0); }
vec3 PBRW_Saturate(vec3 v) { return clamp(v, vec3(0.0), vec3(1.0)); }

float PBRW_Luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 PBRW_FresnelSchlick(vec3 F0, float cosTheta)
{
    float f = pow(PBRW_Saturate(1.0 - cosTheta), 5.0);
    return F0 + (vec3(1.0) - F0) * f;
}

float PBRW_D_GGX(float NdotH, float roughness)
{
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PBRW_PI * d * d, 1e-6);
}

float PBRW_V_SmithJointGGX(float NdotV, float NdotL, float roughness)
{
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float lambdaV = NdotL * sqrt(max(NdotV * NdotV * (1.0 - a2) + a2, 0.0));
    float lambdaL = NdotV * sqrt(max(NdotL * NdotL * (1.0 - a2) + a2, 0.0));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

float PBRW_HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float d = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * PBRW_PI * d * sqrt(d));
}

vec3 PBRW_BeerLambert(vec3 sigmaT, float distanceMeters)
{
    return exp(-max(sigmaT, vec3(0.0)) * max(distanceMeters, 0.0));
}

float PBRW_ExponentialStep(float t)
{
    const float factor = 8.0;
    return (pow(factor, PBRW_Saturate(t)) - 1.0) / (factor - 1.0);
}

float PBRW_IGN(ivec2 pixel, uint frameIndex)
{
    vec3 p = vec3(pixel, int(frameIndex));
    return fract(52.9829189 * fract(dot(p, vec3(0.06711056, 0.00583715, 0.00083333))));
}

#endif
