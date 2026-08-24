#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../Common/CameraData.glsl"
#include "../../Common/Common.glsl"
#include "../../Common/MotionVector.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in float inHeight;
layout(location = 3) in vec4 motionCurrentClip;
layout(location = 4) in vec4 motionPreviousClip;

// 地形描述符集合（set 1）。
layout(set = 1, binding = 0) uniform sampler2D heightMap;
layout(set = 1, binding = 1) uniform sampler2D splatMap0;
layout(set = 1, binding = 2) uniform sampler2D splatMap1;
layout(set = 1, binding = 3) uniform sampler2D terrainAlbedos[8];
layout(set = 1, binding = 4) uniform sampler2D terrainNormals[8];
layout(set = 1, binding = 5) uniform sampler2D terrainRoughness[8];
layout(set = 1, binding = 6) uniform TerrainParams {
    ivec4 layerCountPacked;   // .x = layerCount
    float tilingFactors[8];   // std140：数组元素按 vec4 步长对齐
    vec4 heightfieldParams;   // x=terrainSize, y=maxHeight, z=heightOffset, w=patchGridSize
} terrainParams;

// GBuffer 输出。
layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outGbuffer0;
layout(location = 2) out vec4 outGbuffer1;
layout(location = 3) out vec4 outGbuffer2;
layout(location = 4) out vec2 outMotionVector;

// 使用高度图中心差分计算地形几何法线。
vec3 CalculateTerrainNormal(vec2 uv) {
    ivec2 texSize = textureSize(heightMap, 0);
    vec2 texelSize = 1.0 / vec2(texSize);
    float terrainSize = terrainParams.heightfieldParams.x;
    float maxHeight = terrainParams.heightfieldParams.y;

    float hL = texture(heightMap, uv - vec2(texelSize.x, 0.0)).r * maxHeight;
    float hR = texture(heightMap, uv + vec2(texelSize.x, 0.0)).r * maxHeight;
    float hD = texture(heightMap, uv - vec2(0.0, texelSize.y)).r * maxHeight;
    float hU = texture(heightMap, uv + vec2(0.0, texelSize.y)).r * maxHeight;

    vec2 worldStep = (vec2(terrainSize) / vec2(texSize)) * 2.0;
    vec3 tangent   = vec3(worldStep.x, hR - hL, 0.0);
    vec3 bitangent = vec3(0.0, hU - hD, worldStep.y);
    return normalize(cross(bitangent, tangent));
}

// 根据几何法线构建 TBN，用于混合法线贴图。
mat3 BuildTBN(vec3 geometricNormal) {
    // 选择一个不与法线平行的参考向量。
    vec3 up = abs(geometricNormal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, geometricNormal));
    vec3 bitangent = cross(geometricNormal, tangent);
    return mat3(tangent, bitangent, geometricNormal);
}

void main() {
    // 1. 读取 splatmap 权重。
    vec4 splat0 = texture(splatMap0, inUV);  // 层 0-3（R/G/B/A）
    vec4 splat1 = texture(splatMap1, inUV);  // 层 4-7（R/G/B/A）
    float weights[8] = float[8](
        splat0.r, splat0.g, splat0.b, splat0.a,
        splat1.r, splat1.g, splat1.b, splat1.a
    );

    // 归一化权重，避免累计亮度随绘制权重漂移。
    float totalWeight = 0.0;
    int layerCount = terrainParams.layerCountPacked.x;
    for (int i = 0; i < layerCount; ++i)
        totalWeight += weights[i];
    if (totalWeight > 0.001)
        for (int i = 0; i < layerCount; ++i)
            weights[i] /= totalWeight;

    // 2. 混合各 PBR 地表层贴图。
    vec3 blendedAlbedo    = vec3(0.0);
    vec3 blendedNormal    = vec3(0.0);
    float blendedRoughness = 0.0;
    float blendedAO        = 0.0;

    for (int i = 0; i < layerCount; ++i) {
        float w = weights[i];
        if (w < 0.001) continue;

        vec2 tiledUV = inUV * terrainParams.tilingFactors[i];

        // Albedo 由 sampler 负责 sRGB 到 linear 的转换。
        vec3 albedo = texture(terrainAlbedos[i], tiledUV, MaterialMipBias).rgb;
        blendedAlbedo += albedo * w;

        // 切线空间法线以 0..1 存储，这里解码到 -1..1。
        vec3 nrm = texture(terrainNormals[i], tiledUV, MaterialMipBias).rgb * 2.0 - 1.0;
        blendedNormal += nrm * w;

        // ARM 贴图：当前资源约定 G=AO，A 反向推导 roughness。
        vec4 arm = texture(terrainRoughness[i], tiledUV, MaterialMipBias);
        blendedAO += arm.g * w;
        blendedRoughness += (1.0 - arm.a) * w;
    }

    // 3. 计算最终世界空间法线。
    vec3 geometricNormal = CalculateTerrainNormal(inUV);
    mat3 TBN = BuildTBN(geometricNormal);
    vec3 finalNormal = normalize(TBN * normalize(blendedNormal));

    // 4. 写入 GBuffer。
    outNormal   = vec4(finalNormal, 1.0);
    outGbuffer0 = vec4(blendedAlbedo, blendedRoughness);
    outGbuffer1 = vec4(0.0, blendedAO, float(MATERIAL_ID_PBR), 1.0);

    float linearDepth = (ViewMatrix * vec4(inWorldPos, 1.0)).z;
    outGbuffer2 = vec4(inWorldPos, -linearDepth);
    outMotionVector = VansMotionVectorFromClip(motionCurrentClip, motionPreviousClip);
}
