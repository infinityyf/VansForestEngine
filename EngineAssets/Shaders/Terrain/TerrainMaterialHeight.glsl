#ifndef TERRAIN_MATERIAL_HEIGHT_GLSL
#define TERRAIN_MATERIAL_HEIGHT_GLSL

// 此文件只由细分求值阶段包含，复用材质层的权重、UV 和 MaskMap。
layout(set = 1, binding = 8) uniform HeightDetailParams
{
    float heightDetailStrength;
    float fadeStart;
    vec2 padding;
} heightDetailParams;

layout(set = 1, binding = 1) uniform sampler2D splatMap0;
layout(set = 1, binding = 2) uniform sampler2D splatMap1;
layout(set = 1, binding = 5) uniform sampler2D terrainRoughness[8];

float TerrainHeightDetailFootprint(vec2 worldXZ, float baseHeight)
{
    // 世界坐标和相机决定滤波尺度，不能由地块 LOD 决定，保证共享边采样一致。
    float distanceToCamera = distance(vec3(worldXZ.x, baseHeight, worldXZ.y), cameraPosition.xyz);
    float pixelsPerRadian = max(abs(ProjectionMatrix[1][1]) * ScreenParams.y, 1.0);
    return max(0.02, 2.0 * distanceToCamera * max(tessParams.targetEdgePixels, 1.0) / pixelsPerRadian);
}

float TerrainMaterialHeightFade(vec2 worldXZ, float baseHeight)
{
    if (heightDetailParams.heightDetailStrength <= 0.0 || tessParams.tessDistance <= 0.0)
        return 0.0;

    float distanceToCamera = distance(vec3(worldXZ.x, baseHeight, worldXZ.y), cameraPosition.xyz);
    float fade = 1.0 - smoothstep(
        tessParams.tessDistance * clamp(heightDetailParams.fadeStart, 0.0, 0.95),
        tessParams.tessDistance, distanceToCamera);
    // 地形最外侧也退回基础高度，避免边界被细节抬起。
    vec2 edgeDistance = vec2(TerrainSize() * 0.5) - abs(worldXZ);
    return fade * smoothstep(0.0, 1.0, min(edgeDistance.x, edgeDistance.y));
}

float TerrainSampleMaterialDisplacement(vec2 worldXZ, float baseHeight)
{
    float fade = TerrainMaterialHeightFade(worldXZ, baseHeight);
    if (fade <= 0.0)
        return 0.0;

    float detailCoverage = 1.0 - clamp(PcgCoverage(worldXZ).a, 0.0, 1.0);
    if (detailCoverage <= 0.0)
        return 0.0;

    vec2 terrainUV = TerrainWorldXZToHeightUV(worldXZ);
    vec4 splat0 = textureLod(splatMap0, terrainUV, 0.0);
    vec4 splat1 = textureLod(splatMap1, terrainUV, 0.0);
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

    float footprint = TerrainHeightDetailFootprint(worldXZ, baseHeight);
    float heightDetail = 0.0;
    for (int i = 0; i < layerCount; ++i)
    {
        float weight = weights[i] / totalWeight;
        if (weight <= 0.001)
            continue;

        float tiling = terrainParams.tilingFactors[i];
        float texelsPerMeter = float(max(textureSize(terrainRoughness[i], 0).x,
            textureSize(terrainRoughness[i], 0).y)) * tiling / TerrainSize();
        float lastMip = float(max(textureQueryLevels(terrainRoughness[i]) - 1, 0));
        float mip = clamp(log2(max(footprint * texelsPerMeter, 1.0)), 0.0, lastMip);
        float height = textureLod(terrainRoughness[i], terrainUV * tiling, mip).b;
        // 最小 mip 给出材质平均高度，消除整层抬升；全白雪层自然输出零位移。
        float center = textureLod(terrainRoughness[i], vec2(0.5), lastMip).b;
        float centeredHeight = (height - center) / max(max(center, 1.0 - center), 0.001);
        heightDetail += clamp(centeredHeight, -1.0, 1.0) * weight;
    }
    return heightDetail * heightDetailParams.heightDetailStrength * fade * detailCoverage;
}

vec2 TerrainMaterialHeightGradient(vec3 worldPosition)
{
    vec2 worldXZ = worldPosition.xz;
    float baseHeight = TerrainSampleBaseWorldHeight(worldXZ);
    if (TerrainMaterialHeightFade(worldXZ, baseHeight) <= 0.0)
        return vec2(0.0);

    float stepSize = max(0.02, TerrainHeightDetailFootprint(worldXZ, baseHeight) * 0.5);
    vec2 dx = vec2(stepSize, 0.0);
    vec2 dz = vec2(0.0, stepSize);
    // 对完整位移求梯度，包含材质权重、距离渐隐和道路/河道覆盖变化。
    float left = TerrainSampleMaterialDisplacement(worldXZ - dx, TerrainSampleBaseWorldHeight(worldXZ - dx));
    float right = TerrainSampleMaterialDisplacement(worldXZ + dx, TerrainSampleBaseWorldHeight(worldXZ + dx));
    float top = TerrainSampleMaterialDisplacement(worldXZ - dz, TerrainSampleBaseWorldHeight(worldXZ - dz));
    float bottom = TerrainSampleMaterialDisplacement(worldXZ + dz, TerrainSampleBaseWorldHeight(worldXZ + dz));
    return vec2(right - left, bottom - top) / (2.0 * stepSize);
}

#endif
