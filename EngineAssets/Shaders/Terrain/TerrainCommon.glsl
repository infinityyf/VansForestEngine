// Terrain 顶点阶段公共逻辑：统一高度参数、UV 映射和边缘缝合。

#include "TerrainNoise.glsl"

layout(set = 1, binding = 0) uniform sampler2D heightMap;
layout(set = 1, binding = 6) uniform TerrainParams {
    ivec4 layerCountPacked;
    float tilingFactors[8];
    vec4 heightfieldParams; // x=terrainSize, y=maxHeight, z=heightOffset, w=patchGridSize
} terrainParams;

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

float TerrainPatchGridSize()
{
    return terrainParams.heightfieldParams.w;
}

vec2 TerrainWorldXZToHeightUV(vec2 worldPosXZ)
{
    return worldPosXZ / TerrainSize() + vec2(0.5);
}

float TerrainSampleRawHeight(vec2 heightUV)
{
    return texture(heightMap, heightUV).r * TerrainMaxHeight();
}

float TerrainRawHeightToWorldY(float rawHeight)
{
    return rawHeight + TerrainHeightOffset();
}

float TerrainSampleLocalRawHeight(vec2 localPos, vec2 instanceOffset, float instanceScale)
{
    vec2 worldPosXZ = localPos * instanceScale + instanceOffset;
    return TerrainSampleRawHeight(TerrainWorldXZToHeightUV(worldPosXZ));
}

float TerrainSampleStitchedRawHeight(vec2 localPos, vec2 instanceOffset, float instanceScale, float stitchFlags)
{
    int flags = int(stitchFlags);
    float patchSize = TerrainPatchGridSize();

    bool isLeft = localPos.x < 0.1;
    bool isRight = localPos.x > patchSize - 0.1;
    bool isTop = localPos.y < 0.1;
    bool isBottom = localPos.y > patchSize - 0.1;

    bool stitchLeft = isLeft && ((flags & 1) != 0);
    bool stitchRight = isRight && ((flags & 2) != 0);
    bool stitchTop = isTop && ((flags & 4) != 0);
    bool stitchBottom = isBottom && ((flags & 8) != 0);

    // 细 LOD 与粗 LOD 相邻时，不能移动边缘 XZ；只把高度投影到粗 LOD 边缘线段上。
    if (stitchLeft || stitchRight)
    {
        float edgeX = stitchLeft ? 0.0 : patchSize;
        float coarse0 = clamp(floor(localPos.y * 0.5) * 2.0, 0.0, patchSize);
        float coarse1 = min(coarse0 + 2.0, patchSize);
        float denom = max(coarse1 - coarse0, 0.0001);
        float t = clamp((localPos.y - coarse0) / denom, 0.0, 1.0);
        float h0 = TerrainSampleLocalRawHeight(vec2(edgeX, coarse0), instanceOffset, instanceScale);
        float h1 = TerrainSampleLocalRawHeight(vec2(edgeX, coarse1), instanceOffset, instanceScale);
        return mix(h0, h1, t);
    }

    if (stitchTop || stitchBottom)
    {
        float edgeY = stitchTop ? 0.0 : patchSize;
        float coarse0 = clamp(floor(localPos.x * 0.5) * 2.0, 0.0, patchSize);
        float coarse1 = min(coarse0 + 2.0, patchSize);
        float denom = max(coarse1 - coarse0, 0.0001);
        float t = clamp((localPos.x - coarse0) / denom, 0.0, 1.0);
        float h0 = TerrainSampleLocalRawHeight(vec2(coarse0, edgeY), instanceOffset, instanceScale);
        float h1 = TerrainSampleLocalRawHeight(vec2(coarse1, edgeY), instanceOffset, instanceScale);
        return mix(h0, h1, t);
    }

    return TerrainSampleLocalRawHeight(localPos, instanceOffset, instanceScale);
}

vec3 TerrainBuildWorldPosition(vec2 localPos, vec2 instanceOffset, float instanceScale, float stitchFlags, out vec2 heightUV, out float rawHeight)
{
    vec2 worldPosXZ = localPos * instanceScale + instanceOffset;
    heightUV = TerrainWorldXZToHeightUV(worldPosXZ);
    rawHeight = TerrainSampleStitchedRawHeight(localPos, instanceOffset, instanceScale, stitchFlags);
    return vec3(worldPosXZ.x, TerrainRawHeightToWorldY(rawHeight), worldPosXZ.y);
}

// 细分参数（binding 7，TCS 与 TES 读取）。
// 注：displacementStrength 已移除，原法线贴图 Y 位移逻辑由程序化噪声替代。
layout(set = 1, binding = 7) uniform TessellationParams {
    float maxTessLevel;
    float tessDistance;
    float tessPower;
    float padding;  // 原 displacementStrength，现为 padding
} tessParams;

// 程序化噪声细节参数（binding 8，TES 与 FS 读取）。
layout(set = 1, binding = 8) uniform NoiseDetailParams {
    float noiseStrength;      // 噪声强度（世界单位），默认 0.03
    float noiseFrequency;     // 基础频率（世界单位倒数），默认 0.8
    float noiseLacunarity;    // 频率倍增系数，默认 2.0（与 hill() 一致）
    float noiseGain;          // 振幅衰减系数，默认 0.52（与 hill() 一致）
    int   noiseOctaves;       // octave 数量，默认 4
    float noiseWarpStrength;  // 域扭曲强度，默认 0.0 表示关闭
    float fadeStart;          // 距离衰减起始比例 [0,1]，默认 0.7
    float noisePadding;
} noiseParams;
