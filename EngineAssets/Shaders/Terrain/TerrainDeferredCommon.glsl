#ifndef TERRAIN_DEFERRED_COMMON_GLSL
#define TERRAIN_DEFERRED_COMMON_GLSL

layout(set = 1, binding = 1) uniform sampler2D splatMap0;
layout(set = 1, binding = 2) uniform sampler2D splatMap1;
layout(set = 1, binding = 3) uniform sampler2D terrainAlbedos[8];
layout(set = 1, binding = 4) uniform sampler2D terrainNormals[8];
layout(set = 1, binding = 5) uniform sampler2D terrainRoughness[8];

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outGbuffer0;
layout(location = 2) out vec4 outGbuffer1;
layout(location = 3) out vec4 outGbuffer2;
layout(location = 4) out vec2 outMotionVector;

vec3 TerrainHeightfieldNormal(vec2 heightUV, vec2 noiseGradient)
{
    ivec2 heightSize = max(textureSize(heightMap, 0), ivec2(2));
    vec2 texelUV = 1.0 / vec2(heightSize - ivec2(1));
    vec2 worldStep = vec2(TerrainSize()) * texelUV;

    float heightLeft = texture(heightMap, heightUV - vec2(texelUV.x, 0.0)).r * TerrainMaxHeight();
    float heightRight = texture(heightMap, heightUV + vec2(texelUV.x, 0.0)).r * TerrainMaxHeight();
    float heightTop = texture(heightMap, heightUV - vec2(0.0, texelUV.y)).r * TerrainMaxHeight();
    float heightBottom = texture(heightMap, heightUV + vec2(0.0, texelUV.y)).r * TerrainMaxHeight();

    float slopeX = (heightRight - heightLeft) / (2.0 * worldStep.x) + noiseGradient.x;
    float slopeZ = (heightBottom - heightTop) / (2.0 * worldStep.y) + noiseGradient.y;
    return normalize(vec3(-slopeX, 1.0, -slopeZ));
}

mat3 TerrainCotangentFrame(vec3 normal, vec3 worldPosition, vec2 uv)
{
    vec3 dp1 = dFdx(worldPosition);
    vec3 dp2 = dFdy(worldPosition);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2Perp = cross(dp2, normal);
    vec3 dp1Perp = cross(normal, dp1);
    vec3 tangent = dp2Perp * duv1.x + dp1Perp * duv2.x;
    vec3 bitangent = dp2Perp * duv1.y + dp1Perp * duv2.y;
    float frameLength = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (frameLength <= 1e-8)
    {
        vec3 fallbackTangent = cross(vec3(0.0, 0.0, 1.0), normal);
        float fallbackLength = dot(fallbackTangent, fallbackTangent);
        fallbackTangent = fallbackLength <= 1e-8
            ? vec3(1.0, 0.0, 0.0)
            : fallbackTangent * inversesqrt(fallbackLength);
        return mat3(fallbackTangent, cross(normal, fallbackTangent), normal);
    }

    float inverseLength = inversesqrt(frameLength);
    return mat3(tangent * inverseLength, bitangent * inverseLength, normal);
}

void TerrainWriteDeferred(
    vec2 terrainUV,
    vec3 worldPosition,
    vec4 currentClip,
    vec4 previousClip,
    vec2 noiseGradient)
{
    vec4 splat0 = texture(splatMap0, terrainUV);
    vec4 splat1 = texture(splatMap1, terrainUV);
    float weights[8] = float[8](
        splat0.r, splat0.g, splat0.b, splat0.a,
        splat1.r, splat1.g, splat1.b, splat1.a);

    int layerCount = clamp(terrainParams.layerCountPacked.x, 1, 8);
    float totalWeight = 0.0;
    for (int i = 0; i < layerCount; ++i)
        totalWeight += weights[i];

    if (totalWeight <= 0.001)
    {
        weights[0] = 1.0;
        totalWeight = 1.0;
    }
    for (int i = 0; i < layerCount; ++i)
        weights[i] /= totalWeight;

    vec3 blendedAlbedo = vec3(0.0);
    vec3 blendedTangentNormal = vec3(0.0);
    float blendedRoughness = 0.0;
    float blendedAO = 0.0;

    for (int i = 0; i < layerCount; ++i)
    {
        float weight = weights[i];
        if (weight <= 0.001)
            continue;

        vec2 tiledUV = terrainUV * terrainParams.tilingFactors[i];
        blendedAlbedo += texture(terrainAlbedos[i], tiledUV, MaterialMipBias).rgb * weight;
        blendedTangentNormal +=
            (texture(terrainNormals[i], tiledUV, MaterialMipBias).rgb * 2.0 - 1.0) * weight;

        vec4 arm = texture(terrainRoughness[i], tiledUV, MaterialMipBias);
        blendedAO += arm.g * weight;
        blendedRoughness += (1.0 - arm.a) * weight;
    }

    if (dot(blendedTangentNormal, blendedTangentNormal) <= 1e-8)
        blendedTangentNormal = vec3(0.0, 0.0, 1.0);

    vec3 geometricNormal = TerrainHeightfieldNormal(terrainUV, noiseGradient);
    mat3 tangentFrame = TerrainCotangentFrame(geometricNormal, worldPosition, terrainUV);
    vec3 finalNormal = normalize(tangentFrame * normalize(blendedTangentNormal));

    outNormal = vec4(finalNormal, 1.0);
    outGbuffer0 = vec4(blendedAlbedo, blendedRoughness);
    outGbuffer1 = vec4(0.0, blendedAO, float(MATERIAL_ID_PBR), -1024.0);
    float linearDepth = (ViewMatrix * vec4(worldPosition, 1.0)).z;
    outGbuffer2 = vec4(worldPosition, -linearDepth);
    outMotionVector = VansMotionVectorFromClip(currentClip, previousClip);
}

#endif
