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

vec2 TerrainApplyStitch(vec2 localPos, float stitchFlags)
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

    vec2 snappedLocalPos = localPos;
    if (stitchLeft || stitchRight)
    {
        snappedLocalPos.y = floor(localPos.y * 0.5) * 2.0;
    }

    if (stitchTop || stitchBottom)
    {
        snappedLocalPos.x = floor(localPos.x * 0.5) * 2.0;
    }

    return snappedLocalPos;
}

vec3 TerrainBuildWorldPosition(vec2 localPos, vec2 instanceOffset, float instanceScale, float stitchFlags, out vec2 heightUV, out float rawHeight)
{
    vec2 snappedLocalPos = TerrainApplyStitch(localPos, stitchFlags);
    vec2 worldPosXZ = snappedLocalPos * instanceScale + instanceOffset;
    heightUV = TerrainWorldXZToHeightUV(worldPosXZ);
    rawHeight = TerrainSampleRawHeight(heightUV);
    return vec3(worldPosXZ.x, TerrainRawHeightToWorldY(rawHeight), worldPosXZ.y);
}

// ── Tessellation parameters（binding 7，TCS + TES 读取） ──────────────────
// 注：displacementStrength 已移除（原法线贴图 Y 位移逻辑被程序化噪声替代）
layout(set = 1, binding = 7) uniform TessellationParams {
    float maxTessLevel;
    float tessDistance;
    float tessPower;
    float padding;  // 原 displacementStrength，现为 padding
} tessParams;

// ── 程序化噪声细节参数（binding 8，TES + FS 读取） ────────────
layout(set = 1, binding = 8) uniform NoiseDetailParams {
    float noiseStrength;      // 噪声强度（世界单位），默认 0.03
    float noiseFrequency;     // 基础频率（世界单位倒数），默认 0.8
    float noiseLacunarity;    // 频率倍增系数，默认 2.0（与 hill() 一致）
    float noiseGain;          // 振幅衰减系数，默认 0.52（与 hill() 一致）
    int   noiseOctaves;       // octave 数量，默认 4
    float noiseWarpStrength;  // 域扭曲强度，默认 0.0（0=关闭）
    float fadeStart;          // 距离衰减起始比例 [0,1]，默认 0.7
    float noisePadding;
} noiseParams;
