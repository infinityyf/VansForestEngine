#ifndef TERRAIN_COMMON_GLSL
#define TERRAIN_COMMON_GLSL

#include "TerrainNoise.glsl"
#include "../Common/PcgSplineFields.glsl"

layout(set = 1, binding = 0) uniform sampler2D heightMap;

layout(set = 1, binding = 6) uniform TerrainParams
{
    ivec4 layerCountPacked;
    float tilingFactors[8];
    vec4 heightfieldParams; // x=terrainSize, y=maxHeight, z=heightOffset, w=patchGridResolution
} terrainParams;

layout(set = 1, binding = 7) uniform TessellationParams
{
    float maxTessLevel;
    float tessDistance;
    float targetEdgePixels;
    float padding;
} tessParams;

layout(set = 1, binding = 8) uniform NoiseDetailParams
{
    float noiseStrength;
    float noiseFrequency;
    float noiseLacunarity;
    float noiseGain;
    int noiseOctaves;
    float noiseWarpStrength;
    float fadeStart;
    float noisePadding;
} noiseParams;

const uint TerrainEdgeLeft = 1u;
const uint TerrainEdgeRight = 2u;
const uint TerrainEdgeTop = 4u;
const uint TerrainEdgeBottom = 8u;
const uint TerrainEdgeMask = 15u;

float TerrainSize()
{
    return terrainParams.heightfieldParams.x;
}

float TerrainMaxHeight()
{
    return terrainParams.heightfieldParams.y;
}

float TerrainHeightOffset()
{
    return terrainParams.heightfieldParams.z;
}

float TerrainPatchGridResolution()
{
    return terrainParams.heightfieldParams.w;
}

vec2 TerrainWorldXZToHeightUV(vec2 worldXZ)
{
    return worldXZ / TerrainSize() + vec2(0.5);
}

float TerrainSampleBaseWorldHeight(vec2 worldXZ)
{
    return texture(heightMap, TerrainWorldXZToHeightUV(worldXZ)).r * TerrainMaxHeight() + TerrainHeightOffset();
}

int TerrainGeometryNoiseOctaves()
{
    return noiseParams.noiseOctaves;
}

float TerrainEvaluateNoise(vec2 worldXZ, int octaves)
{
    if (noiseParams.noiseWarpStrength > 0.001)
    {
        return terrainDetailFbmWarped(
            worldXZ * noiseParams.noiseFrequency,
            octaves,
            noiseParams.noiseGain,
            noiseParams.noiseLacunarity,
            noiseParams.noiseWarpStrength);
    }

    return terrainDetailFbm(
        worldXZ * noiseParams.noiseFrequency,
        octaves,
        noiseParams.noiseGain,
        noiseParams.noiseLacunarity);
}

float TerrainNoiseFade(vec2 worldXZ, float baseWorldHeight)
{
    if (noiseParams.noiseStrength <= 0.0 || tessParams.tessDistance <= 0.0)
        return 0.0;

    float distanceToCamera = distance(vec3(worldXZ.x, baseWorldHeight, worldXZ.y), cameraPosition.xyz);
    return 1.0 - smoothstep(
        tessParams.tessDistance * noiseParams.fadeStart,
        tessParams.tessDistance,
        distanceToCamera);
}

float TerrainSampleDetailedWorldHeight(vec2 worldXZ)
{
    float baseHeight = TerrainSampleBaseWorldHeight(worldXZ);
    float fade = TerrainNoiseFade(worldXZ, baseHeight);
    if (fade <= 0.001)
        return baseHeight;

    return baseHeight + TerrainEvaluateNoise(worldXZ, TerrainGeometryNoiseOctaves()) *
        noiseParams.noiseStrength * fade * (1.0-PcgCoverage(worldXZ).a);
}

vec2 TerrainDetailedNoiseGradient(vec3 worldPosition)
{
    float fade = TerrainNoiseFade(worldPosition.xz, worldPosition.y);
    if (fade <= 0.001)
        return vec2(0.0);

    const float gradientStep = 0.02;
    vec2 gradient;
    if (noiseParams.noiseWarpStrength > 0.001)
    {
        gradient = terrainDetailGradientWarped(
            worldPosition.xz,
            noiseParams.noiseFrequency,
            TerrainGeometryNoiseOctaves(),
            noiseParams.noiseGain,
            noiseParams.noiseLacunarity,
            noiseParams.noiseWarpStrength,
            gradientStep);
    }
    else
    {
        gradient = terrainDetailGradient(
            worldPosition.xz,
            noiseParams.noiseFrequency,
            TerrainGeometryNoiseOctaves(),
            noiseParams.noiseGain,
            noiseParams.noiseLacunarity,
            gradientStep);
    }

    if(pcgMetadata[0].y==0u)return gradient*noiseParams.noiseStrength*fade;
    float suppression=PcgCoverage(worldPosition.xz).a;
    vec2 suppressionGradient=vec2(
        PcgCoverage(worldPosition.xz+vec2(gradientStep,0)).a-PcgCoverage(worldPosition.xz-vec2(gradientStep,0)).a,
        PcgCoverage(worldPosition.xz+vec2(0,gradientStep)).a-PcgCoverage(worldPosition.xz-vec2(0,gradientStep)).a)/(2.0*gradientStep);
    return (gradient*(1.0-suppression)-TerrainEvaluateNoise(worldPosition.xz,TerrainGeometryNoiseOctaves())*suppressionGradient)*noiseParams.noiseStrength*fade;
}

uint TerrainEdgesAtLocalPosition(vec2 localPosition)
{
    const float epsilon = 0.001;
    const float patchSize = TerrainPatchGridResolution();
    uint edges = 0u;
    if (localPosition.x <= epsilon) edges |= TerrainEdgeLeft;
    if (localPosition.x >= patchSize - epsilon) edges |= TerrainEdgeRight;
    if (localPosition.y <= epsilon) edges |= TerrainEdgeTop;
    if (localPosition.y >= patchSize - epsilon) edges |= TerrainEdgeBottom;
    return edges;
}

float TerrainSampleCoarseSurfaceHeight(
    vec2 localPosition,
    vec2 instanceOffset,
    float instanceScale)
{
    float patchSize = TerrainPatchGridResolution();
    vec2 cellOrigin = min(floor(localPosition * 0.5) * 2.0, vec2(patchSize - 2.0));
    vec2 cellCoord = clamp((localPosition - cellOrigin) * 0.5, vec2(0.0), vec2(1.0));

    vec2 world00 = (cellOrigin + vec2(0.0, 0.0)) * instanceScale + instanceOffset;
    vec2 world10 = (cellOrigin + vec2(2.0, 0.0)) * instanceScale + instanceOffset;
    vec2 world01 = (cellOrigin + vec2(0.0, 2.0)) * instanceScale + instanceOffset;
    vec2 world11 = (cellOrigin + vec2(2.0, 2.0)) * instanceScale + instanceOffset;
    float height00 = TerrainSampleDetailedWorldHeight(world00);
    float height10 = TerrainSampleDetailedWorldHeight(world10);
    float height01 = TerrainSampleDetailedWorldHeight(world01);
    float height11 = TerrainSampleDetailedWorldHeight(world11);

    if (cellCoord.x + cellCoord.y <= 1.0)
    {
        return height00 +
            cellCoord.x * (height10 - height00) +
            cellCoord.y * (height01 - height00);
    }

    return height11 +
        (1.0 - cellCoord.y) * (height10 - height11) +
        (1.0 - cellCoord.x) * (height01 - height11);
}

float TerrainComputeMorphAlpha(vec3 fineWorldPosition, vec2 morphRange)
{
    if (morphRange.y <= morphRange.x)
        return 0.0;
    return smoothstep(
        morphRange.x,
        morphRange.y,
        distance(fineWorldPosition, cameraPosition.xyz));
}

vec3 TerrainBuildWorldPosition(
    vec2 localPosition,
    vec2 instanceOffset,
    float instanceScale,
    uint edgeFlags,
    vec2 morphRange,
    out vec2 heightUV,
    out float worldHeight)
{
    vec2 worldXZ = localPosition * instanceScale + instanceOffset;
    heightUV = TerrainWorldXZToHeightUV(worldXZ);

    float fineHeight = TerrainSampleDetailedWorldHeight(worldXZ);
    vec3 fineWorldPosition = vec3(worldXZ.x, fineHeight, worldXZ.y);
    float morphAlpha = TerrainComputeMorphAlpha(fineWorldPosition, morphRange);

    uint positionEdges = TerrainEdgesAtLocalPosition(localPosition);
    uint coarserEdges = edgeFlags & TerrainEdgeMask;
    uint transitionEdges = (edgeFlags >> 4u) & TerrainEdgeMask;

    // 跨 LOD 的粗侧保持自身顶点不变；细侧严格落到粗侧三角面上。
    if ((positionEdges & transitionEdges) != 0u)
        morphAlpha = 0.0;
    if ((positionEdges & coarserEdges) != 0u)
        morphAlpha = 1.0;

    worldHeight = fineHeight;
    if (morphAlpha > 0.001)
    {
        float coarseHeight = TerrainSampleCoarseSurfaceHeight(localPosition, instanceOffset, instanceScale);
        worldHeight = mix(fineHeight, coarseHeight, morphAlpha);
    }

    return vec3(worldXZ.x, worldHeight, worldXZ.y);
}

#endif
