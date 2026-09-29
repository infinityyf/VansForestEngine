#ifndef SURFACE_WEATHER_GLSL
#define SURFACE_WEATHER_GLSL

#include "WeatherFrameData.glsl"

layout(set = 0, binding = 42) uniform sampler2D surfaceWeatherRippleNormalSampler;

const uint SurfaceWeatherWetFilmBit = 1u;
const uint SurfaceWeatherPuddleBit = 2u;
const uint SurfaceWeatherRippleBit = 4u;

uint SurfaceWeatherHashCell(ivec2 cell, uint seed)
{
    uvec2 bits = uvec2(cell);
    uint value = bits.x * 0x9e3779b9u;
    value ^= bits.y * 0x85ebca6bu;
    value ^= seed * 0xc2b2ae35u;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float SurfaceWeatherHash01(ivec2 cell, uint seed)
{
    return float(SurfaceWeatherHashCell(cell, seed) & 0x00ffffffu) / 16777215.0;
}

float SurfaceWeatherValueNoise(vec2 position, uint seed)
{
    ivec2 cell = ivec2(floor(position));
    vec2 fraction = fract(position);
    vec2 blend = fraction * fraction * (3.0 - 2.0 * fraction);
    float n00 = SurfaceWeatherHash01(cell, seed);
    float n10 = SurfaceWeatherHash01(cell + ivec2(1, 0), seed);
    float n01 = SurfaceWeatherHash01(cell + ivec2(0, 1), seed);
    float n11 = SurfaceWeatherHash01(cell + ivec2(1, 1), seed);
    return mix(mix(n00, n10, blend.x), mix(n01, n11, blend.x), blend.y);
}

float SurfaceWeatherPuddleAmount(vec2 worldXZ)
{
    float scaleMeters = max(surfaceWeatherFrame.puddleField0.x, 0.001);
    float detailScale = max(surfaceWeatherFrame.puddleField0.y, 1.001);
    float threshold = clamp(surfaceWeatherFrame.puddleField0.z, 0.0, 1.0);
    float softness = max(surfaceWeatherFrame.puddleField0.w, 0.0001);
    uint seed = uint(max(round(surfaceWeatherFrame.puddleField1.y), 0.0));
    vec2 p = worldXZ / scaleMeters;
    float noiseValue = SurfaceWeatherValueNoise(p, seed);
    noiseValue += SurfaceWeatherValueNoise(p * detailScale, seed ^ 0x68bc21ebu) * 0.5;
    noiseValue += SurfaceWeatherValueNoise(p * detailScale * detailScale,
        seed ^ 0x02e5be93u) * 0.25;
    noiseValue /= 1.75;
    float basinDepth = smoothstep(threshold - softness, threshold + softness, noiseValue);
    float pixelDepth = 1.0 - basinDepth;
    float water = clamp((clamp(surfaceWeatherFrame.state.w, 0.0, 1.0) - pixelDepth) / 0.4,
        0.0, 1.0);
    return clamp(water * surfaceWeatherFrame.puddleField1.x, 0.0, 1.0);
}

vec3 SurfaceWeatherRippleWorldNormal(vec2 worldXZ)
{
    vec2 uv = worldXZ / max(surfaceWeatherFrame.surface.x, 0.05);
    vec3 ripple = texture(surfaceWeatherRippleNormalSampler, uv).xyz;
    return dot(ripple, ripple) > 1e-6 ? normalize(ripple) : vec3(0.0, 1.0, 0.0);
}

vec3 SurfaceWeatherAddWaterRipple(vec3 worldNormal, vec2 worldXZ)
{
    vec3 baseNormal = normalize(worldNormal);
    float rippleActive = step(0.5, surfaceWeatherFrame.state.x) *
        step(0.0001, surfaceWeatherFrame.state.y);
    vec3 rippleNormal = SurfaceWeatherRippleWorldNormal(worldXZ);
    // Water keeps its FFT, river and detail normals. Rain contributes only the
    // horizontal ripple perturbation, projected into the existing normal plane.
    // No ground puddle amount, wet-film response or normal flattening is used.
    vec3 perturbation = vec3(rippleNormal.x, 0.0, rippleNormal.z);
    perturbation -= baseNormal * dot(baseNormal, perturbation);
    return normalize(baseNormal + perturbation * rippleActive);
}

void SurfaceWeatherApplyGround(
    inout vec3 albedo,
    inout float roughness,
    inout vec3 surfaceNormal,
    vec3 geometricNormal,
    mat3 tangentFrame,
    vec2 worldXZ,
    uint effects,
    out float puddleAmount)
{
    float wetFilm = (effects & SurfaceWeatherWetFilmBit) != 0u
        ? clamp(surfaceWeatherFrame.state.z, 0.0, 1.0) : 0.0;
    puddleAmount = (effects & SurfaceWeatherPuddleBit) != 0u
        ? SurfaceWeatherPuddleAmount(worldXZ) : 0.0;

    vec3 wetAlbedo = albedo * surfaceWeatherFrame.groundResponse.x;
    float wetRoughness = min(roughness, surfaceWeatherFrame.groundResponse.y);
    albedo = mix(albedo, wetAlbedo, wetFilm);
    roughness = mix(roughness, wetRoughness, wetFilm);
    albedo = mix(albedo, wetAlbedo, puddleAmount);
    roughness = mix(roughness, surfaceWeatherFrame.groundResponse.z, puddleAmount);

    vec3 originalNormal = normalize(surfaceNormal);
    vec3 waterNormal = normalize(geometricNormal);
    if ((effects & SurfaceWeatherRippleBit) != 0u)
    {
        vec3 rippleYUp = SurfaceWeatherRippleWorldNormal(worldXZ);
        vec3 rippleNormal = normalize(tangentFrame * vec3(
            rippleYUp.x, rippleYUp.z, rippleYUp.y));
        float rippleActive = step(0.5, surfaceWeatherFrame.state.x) *
            step(0.0001, surfaceWeatherFrame.state.y);
        waterNormal = normalize(mix(waterNormal, rippleNormal, rippleActive));
    }
    surfaceNormal = normalize(mix(originalNormal, waterNormal, puddleAmount));
}

#endif
