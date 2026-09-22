#ifndef AMBIENT_SKY_TRANSMITTANCE_GLSL
#define AMBIENT_SKY_TRANSMITTANCE_GLSL

layout(set = 1, binding = 35) uniform sampler3D ambientSkyCacheX;
layout(set = 1, binding = 36) uniform sampler3D ambientSkyCacheY;
layout(set = 1, binding = 37) uniform sampler3D ambientSkyCacheZ;
layout(set = 1, binding = 38) uniform AmbientSkyCacheInfo
{
    vec4 originAndSpacing;
    uvec4 gridAndQuery; // xyz = dimensions, w = debug mode
    ivec4 ringOffset; // xyz = logical-to-physical toroidal offset
};

struct AmbientSkyTransmittanceSample
{
    float visibility;
    float confidence;
};

uint AmbientSkyCacheDebugMode()
{
    return gridAndQuery.w;
}

AmbientSkyTransmittanceSample SampleAmbientSkyTransmittance(vec3 position, vec3 reflection, float roughness)
{
    AmbientSkyTransmittanceSample result = AmbientSkyTransmittanceSample(1.0, 0.0);
    vec3 dimensions = vec3(gridAndQuery.xyz);
    vec3 cell = (position - originAndSpacing.xyz) / max(originAndSpacing.w, 1e-4);
    if (any(lessThan(cell, vec3(0.0))) || any(greaterThanEqual(cell, dimensions)))
        return result;
    // The texture stores a toroidal physical grid. Repeat addressing keeps
    // trilinear filtering continuous across the ring seam.
    vec3 uvw = (cell + 0.5 + vec3(ringOffset.xyz)) / dimensions;

    vec3 direction = normalize(reflection);
    vec3 weights = abs(direction);
    float weightSum = max(weights.x + weights.y + weights.z, 1e-5);
    weights /= weightSum;
    vec4 x = texture(ambientSkyCacheX, uvw);
    vec4 y = texture(ambientSkyCacheY, uvw);
    vec4 z = texture(ambientSkyCacheZ, uvw);
    float plusX = direction.x >= 0.0 ? x.r : x.b;
    float confX = direction.x >= 0.0 ? x.g : x.a;
    float plusY = direction.y >= 0.0 ? y.r : y.b;
    float confY = direction.y >= 0.0 ? y.g : y.a;
    float plusZ = direction.z >= 0.0 ? z.r : z.b;
    float confZ = direction.z >= 0.0 ? z.g : z.a;
    float weightedConfidence = weights.x * confX + weights.y * confY + weights.z * confZ;
    if (weightedConfidence <= 1e-4)
        return result;

    float weightedTransmission = weights.x * plusX + weights.y * plusY + weights.z * plusZ;
    float cachedVisibility = clamp(weightedTransmission / weightedConfidence, 0.0, 1.0);
    float roughnessStrength = smoothstep(0.25, 0.55, clamp(roughness, 0.0, 1.0));
    result.confidence = clamp(weightedConfidence, 0.0, 1.0);
    result.visibility = mix(1.0, cachedVisibility, result.confidence * roughnessStrength);
    return result;
}

#endif
