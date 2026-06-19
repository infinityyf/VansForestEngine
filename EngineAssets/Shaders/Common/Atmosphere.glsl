
#include "Common.glsl"
#define RayleighScatterSigmaH0 vec3(5.802, 13.558, 33.1) * 1e-6
#define MieScatterSigmaH0  vec3(3.996,3.996,3.996) * 1e-6

#define MieAbsorpSigma vec3(4.4,4.4,4.4) * 1e-6
#define OzoneAbsorpSigma vec3(0.650, 1.881, 0.085) * 1e-6

#if !defined(AtmosphereCBBind)
    #define AtmosphereCBBind 1
#endif
#if !defined(AtmosphereBinding)
    #define AtmosphereBinding 0
#endif
layout(set=AtmosphereCBBind, binding=AtmosphereBinding) uniform AtmosphereUniformBuffer
{
    vec4 sunDirection;
    float sunLuminance;
    float planetRadius;
    float initSeaLevel;
    float atmosphereWidth;
    float rayleighScalarHeight;
    float mieScalarHeight;
    float mieAnisotropy;
    float ozoneLevelCenterHeight;
    float ozoneLevelWidth;
    // std140 在上方 9 个 float（offset 16..48）后自动补齐到 offset 64
    // 内容：CPU 预计算的大气衰减后太阳色（baseColor × 仰角衰减）
    // 供无法直接读 LightsData 的 shader（如 VolumeCloud.frag）使用
    vec4 effectiveSunColor;
};

struct AtmosphereParam
{
    float planetRadius;
    float atmosphereWidth;
    float rayleighScalarHeight;
    float mieScalarHeight;
    float mieAnisotropy;
    float ozoneLevelCenterHeight;
    float ozoneLevelWidth;
    float sunLuminance;
    vec3 sunDirection;
    vec3 viewDirection;
};

vec3 RayleighScatterCoeff(AtmosphereParam param, float height)
{
    float H = param.rayleighScalarHeight;
    float rho_h = exp(- height / H);
    return RayleighScatterSigmaH0 * rho_h;
}

float RayleiPhase(AtmosphereParam param, float cos_theta)
{
    return (3.0 / (16.0 * PI)) * ( 1 + cos_theta * cos_theta );
}

vec3 MieCoeff(AtmosphereParam param, float height)
{
    float H = param.mieScalarHeight;
    float rho_h = exp(- height / H);
    return MieScatterSigmaH0  * rho_h;
}

float MiePhase(AtmosphereParam param, float cos_theta)
{
    float g = param.mieAnisotropy;
    float denom = max(1e-6, 1.0 + g*g - 2.0*g*cos_theta);
    float phase = (3.0 / (8.0 * PI)) * (1.0 - g*g)/(2.0 + g*g) * (1.0 + cos_theta*cos_theta) / pow(denom, 1.5);
    return phase;
}

//HG phase
float HGPhase(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(max(denom, 1e-6), 1.5));
}

vec3 Scattering(AtmosphereParam param, vec3 scatter_position)
{
    float cos_theta = dot(param.sunDirection, param.viewDirection);
    float height =  length(scatter_position) - param.planetRadius;
    vec3 rayleigh = RayleighScatterCoeff(param, height) * RayleiPhase(param, cos_theta);
    vec3 mie = MieCoeff(param, height) * MiePhase(param, cos_theta);
    return rayleigh + mie;
}

vec3 MieAbsorption(AtmosphereParam param, float height)
{
    float H = param.mieScalarHeight;
    float rho_h = exp(- height / H);
    return MieAbsorpSigma * rho_h;
}

vec3 OzoneAbsorption(AtmosphereParam param, float height)
{
    float center = param.ozoneLevelCenterHeight;
    float width = param.ozoneLevelWidth;
    float rho_h = max(0, 1.0 - (abs(height - center) / width));
    return OzoneAbsorpSigma * rho_h;
}

vec3 Transmittance(AtmosphereParam param, vec3 start_position, vec3 end_position)
{
    int sample_count = 8;
    vec3 direction = normalize(end_position - start_position);
    float distance = length(end_position - start_position);
    float step = distance / sample_count;

    vec3 sum = vec3(0,0,0);
    vec3 current_position = start_position + direction * step * 0.5;

    for (int i = 0; i < sample_count; i++)
    {
        float height = length(current_position) - param.planetRadius;

        vec3 scattering = RayleighScatterCoeff(param, height) + MieCoeff(param, height);
        vec3 absorption = OzoneAbsorption(param, height) + MieAbsorption(param, height);
        vec3 extinction = scattering + absorption;

        sum += extinction * step;
        current_position += direction * step;
    }

    return exp(-sum);
}

float RayIntersectSphere(vec3 center, float radius, vec3 rayStart, vec3 rayDir)
{
    float OS = length(center - rayStart);
    float SH = dot(center - rayStart, rayDir);
    float OH = sqrt(max(0.0, OS*OS - SH*SH));

    // ray miss sphere
    if(OH > radius) return -1;

    float PH = sqrt(max(0.0, radius*radius - OH*OH));

    // use min distance
    float t1 = SH - PH;
    float t2 = SH + PH;
    float t = (t1 < 0) ? t2 : t1;

    return min(t,100000);
}

vec3 SingleScatter(AtmosphereParam param, vec3 start_position)
{
    vec3 color = vec3(0,0,0);
    int sample_count = 32;
    float distanceAtmosphere = RayIntersectSphere(vec3(0, 0, 0), param.planetRadius + param.atmosphereWidth, start_position, param.viewDirection);
    float distanceSurface = RayIntersectSphere(vec3(0, 0, 0), param.planetRadius, start_position, param.viewDirection);
    if(distanceSurface > 0)
    {
        distanceAtmosphere = min(distanceAtmosphere, distanceSurface);
    }
    float step = distanceAtmosphere / sample_count;
    vec3 pScatterPosition = start_position;
    for(int i=0; i < sample_count; i++)
    {
        pScatterPosition +=  param.viewDirection * step;
        distanceAtmosphere = RayIntersectSphere(vec3(0, 0, 0), param.planetRadius + param.atmosphereWidth, pScatterPosition, param.sunDirection);
        if(distanceAtmosphere < 1)
        {
            break;
        }
        vec3 atmospherePosition = pScatterPosition + param.sunDirection * distanceAtmosphere;

        //single scatter
        vec3 t1 = Transmittance(param, pScatterPosition, atmospherePosition);
        vec3 s = Scattering(param, pScatterPosition);
        vec3 t2 = Transmittance(param, start_position, pScatterPosition);

        vec3 result = t1 * s * t2 * step *param.sunLuminance;
        color += result;
    }

    return color;
}
