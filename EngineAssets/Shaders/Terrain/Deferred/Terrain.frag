#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../Common/CameraData.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in float inHeight;

// --- Terrain descriptor set (set 1) ---
layout(set = 1, binding = 0) uniform sampler2D heightMap;
layout(set = 1, binding = 1) uniform sampler2D splatMap0;
layout(set = 1, binding = 2) uniform sampler2D splatMap1;
layout(set = 1, binding = 3) uniform sampler2D terrainAlbedos[8];
layout(set = 1, binding = 4) uniform sampler2D terrainNormals[8];
layout(set = 1, binding = 5) uniform sampler2D terrainRoughness[8];
layout(set = 1, binding = 6) uniform TerrainParams {
    ivec4 layerCountPacked;   // .x = layerCount
    float tilingFactors[8];   // std140: each element has vec4 (16-byte) stride
} terrainParams;

// --- GBuffer outputs ---
layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outGbuffer0;
layout(location = 2) out vec4 outGbuffer1;
layout(location = 3) out vec4 outGbuffer2;

const float TERRAIN_SIZE = 1024.0;
const float MAX_HEIGHT = 500.0;

// Compute terrain normal from heightmap via central differences
vec3 CalculateTerrainNormal(vec2 uv) {
    ivec2 texSize = textureSize(heightMap, 0);
    vec2 texelSize = 1.0 / vec2(texSize);

    float hL = texture(heightMap, uv - vec2(texelSize.x, 0.0)).r * MAX_HEIGHT;
    float hR = texture(heightMap, uv + vec2(texelSize.x, 0.0)).r * MAX_HEIGHT;
    float hD = texture(heightMap, uv - vec2(0.0, texelSize.y)).r * MAX_HEIGHT;
    float hU = texture(heightMap, uv + vec2(0.0, texelSize.y)).r * MAX_HEIGHT;

    vec2 worldStep = (vec2(TERRAIN_SIZE) / vec2(texSize)) * 2.0;
    vec3 tangent   = vec3(worldStep.x, hR - hL, 0.0);
    vec3 bitangent = vec3(0.0, hU - hD, worldStep.y);
    return normalize(cross(bitangent, tangent));
}

// Build TBN matrix from geometric normal for normal-map blending
mat3 BuildTBN(vec3 geometricNormal) {
    // Choose a reference vector that isn't parallel to the normal
    vec3 up = abs(geometricNormal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, geometricNormal));
    vec3 bitangent = cross(geometricNormal, tangent);
    return mat3(tangent, bitangent, geometricNormal);
}

void main() {
    // --------------------------------------------------
    // 1. Read splatmap weights
    // --------------------------------------------------
    vec4 splat0 = texture(splatMap0, inUV);  // layers 0-3 (R, G, B, A)
    vec4 splat1 = texture(splatMap1, inUV);  // layers 4-7 (R, G, B, A)
    float weights[8] = float[8](
        splat0.r, splat0.g, splat0.b, splat0.a,
        splat1.r, splat1.g, splat1.b, splat1.a
    );

    // Normalize weights so they sum to 1
    float totalWeight = 0.0;
    int layerCount = terrainParams.layerCountPacked.x;
    for (int i = 0; i < layerCount; ++i)
        totalWeight += weights[i];
    if (totalWeight > 0.001)
        for (int i = 0; i < layerCount; ++i)
            weights[i] /= totalWeight;

    // --------------------------------------------------
    // 2. Blend PBR layer textures
    // --------------------------------------------------
    vec3 blendedAlbedo    = vec3(0.0);
    vec3 blendedNormal    = vec3(0.0);
    float blendedRoughness = 0.0;
    float blendedMetallic  = 0.0;
    float blendedAO        = 0.0;

    for (int i = 0; i < layerCount; ++i) {
        float w = weights[i];
        if (w < 0.001) continue;

        vec2 tiledUV = inUV * terrainParams.tilingFactors[i];

        // Albedo (sRGB -> linear handled by sampler)
        vec3 albedo = texture(terrainAlbedos[i], tiledUV).rgb;
        blendedAlbedo += albedo * w;

        // Normal map (tangent-space, stored as 0..1, decode to -1..1)
        vec3 nrm = texture(terrainNormals[i], tiledUV).rgb * 2.0 - 1.0;
        blendedNormal += nrm * w;

        // Roughness texture (R = AO, G = Roughness, B = Metallic — ARM format)
        // If your textures store only roughness in R channel, adjust accordingly
        vec3 arm = texture(terrainRoughness[i], tiledUV).rgb;
        blendedAO        += arm.r * w;
        blendedRoughness  += arm.g * w;
        blendedMetallic   += arm.b * w;
    }

    // --------------------------------------------------
    // 3. Compute final world-space normal
    // --------------------------------------------------
    vec3 geometricNormal = CalculateTerrainNormal(inUV);
    mat3 TBN = BuildTBN(geometricNormal);
    vec3 finalNormal = normalize(TBN * normalize(blendedNormal));
    //blendedAlbedo = vec3(fract(inUV*terrainParams.tilingFactors[0]), 0.0); // debug UV tiling
    // --------------------------------------------------
    // 4. Output to GBuffer
    // --------------------------------------------------
    outNormal   = vec4(finalNormal, 1.0);
    outGbuffer0 = vec4(blendedAlbedo, blendedRoughness);
    outGbuffer1 = vec4(blendedMetallic, blendedAO, 1.0, 1.0);

    float linearDepth = (ViewMatrix * vec4(inWorldPos, 1.0)).z;
    outGbuffer2 = vec4(inWorldPos, -linearDepth);
}