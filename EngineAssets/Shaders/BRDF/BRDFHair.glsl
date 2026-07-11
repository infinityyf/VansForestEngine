#ifndef VANS_BRDF_HAIR_GLSL
#define VANS_BRDF_HAIR_GLSL

const float HAIR_PI = 3.14159265359;
const float HAIR_TWO_PI = 6.28318530718;

struct HairParams
{
    vec4 absorption;
    vec4 shiftParams;
};

struct HairData
{
    vec3 positionWS;
    vec3 normalWS;
    vec3 tangentWS;
    vec3 viewDirWS;
    vec3 albedo;
    float coverage;
    float roughnessR;
    float roughnessTT;
    float roughnessTRT;
    float shift;
    float ao;
    HairParams params;
};

struct HairLobes
{
    vec3 R;
    vec3 TT;
    vec3 TRT;
    vec3 diffuse;
};

float HairSaturate(float v)
{
    return clamp(v, 0.0, 1.0);
}

float HairGaussian(float beta, float x)
{
    float b = max(beta, 1e-3);
    return exp(-(x * x) / (2.0 * b * b)) / max(sqrt(HAIR_TWO_PI) * b, 1e-3);
}

float HairFresnel(float cosTheta)
{
    float f0 = 0.04;
    float m = HairSaturate(1.0 - cosTheta);
    return f0 + (1.0 - f0) * m * m * m * m * m;
}

vec3 HairAbsorptionFromAlbedo(vec3 albedo, float density)
{
    return -log(clamp(albedo, vec3(0.02), vec3(0.98))) * max(density, 1e-3);
}

float HairCylinderIrradiance(vec3 tangentWS, vec3 lightDirWS)
{
    float tl = clamp(dot(normalize(tangentWS), normalize(lightDirWS)), -1.0, 1.0);
    return sqrt(max(1.0 - tl * tl, 0.0));
}

vec2 HairOctEncode(vec3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-5);
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return n.xy;
}

vec3 HairOctDecode(vec2 e)
{
    vec3 v = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0)
        v.xy = (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
    return normalize(v);
}

HairLobes EvaluateHairLobes(HairData hair, vec3 L)
{
    vec3 V = normalize(hair.viewDirWS);
    vec3 T = normalize(hair.tangentWS);
    vec3 N = normalize(hair.normalWS);

    float shift = hair.shift * hair.params.shiftParams.x;
    vec3 Ts = normalize(T + N * shift * 0.15);

    float sinThetaL = clamp(dot(Ts, L), -1.0, 1.0);
    float sinThetaV = clamp(dot(Ts, V), -1.0, 1.0);
    float sinThetaH = 0.5 * (sinThetaL + sinThetaV);

    float thetaD = 0.5 * abs(asin(sinThetaV) - asin(sinThetaL));
    float cosThetaD = max(cos(thetaD), 0.05);

    vec3 Lp = normalize(L - sinThetaL * Ts);
    vec3 Vp = normalize(V - sinThetaV * Ts);
    float cosPhi = clamp(dot(Lp, Vp), -1.0, 1.0);

    float betaR = max(hair.roughnessR, 0.035);
    float betaTT = max(hair.roughnessTT, 0.025);
    float betaTRT = max(hair.roughnessTRT, 0.08);

    float MR = HairGaussian(betaR, sinThetaH + sin(shift * 0.05));
    float MTT = HairGaussian(betaTT, sinThetaH - sin(shift * 0.025));
    float MTRT = HairGaussian(betaTRT, sinThetaH - sin(shift * 0.075));

    float NR = 0.25 * sqrt(max(0.5 + 0.5 * cosPhi, 0.0));
    float NTT = exp(-3.65 * cosPhi - 3.98);
    float NTRT = NTT / HAIR_TWO_PI;

    vec3 sigmaA = HairAbsorptionFromAlbedo(hair.albedo, hair.params.absorption.a);
    vec3 absorptionTT = exp(-sigmaA * (0.5 / cosThetaD));
    vec3 absorptionTRT = exp(-sigmaA * (0.8 / cosThetaD));

    float F = HairFresnel(cosThetaD);

    HairLobes lobes;
    lobes.R = vec3(F) * MR * NR;
    lobes.TT = absorptionTT * (1.0 - F) * (1.0 - F) * MTT * NTT;
    lobes.TRT = absorptionTRT * F * (1.0 - F) * (1.0 - F) * MTRT * NTRT;
    lobes.diffuse = hair.albedo * HairCylinderIrradiance(Ts, L) / HAIR_PI;
    return lobes;
}

#endif
