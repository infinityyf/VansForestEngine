#ifndef TERRAIN_DEFERRED_COMMON_GLSL
#define TERRAIN_DEFERRED_COMMON_GLSL

#include "../Weather/SurfaceWeather.glsl"

layout(set = 1, binding = 1) uniform sampler2D splatMap0;
layout(set = 1, binding = 2) uniform sampler2D splatMap1;
layout(set = 1, binding = 3) uniform sampler2D terrainAlbedos[8];
layout(set = 1, binding = 4) uniform sampler2D terrainNormals[8];
layout(set = 1, binding = 5) uniform sampler2D terrainRoughness[8];
layout(set = 1, binding = 10) uniform sampler2D terrainInteractionAlbedo;
layout(set = 1, binding = 11) uniform sampler2D terrainInteractionNormal;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec4 outGbuffer0;
layout(location = 2) out vec4 outGbuffer1;
layout(location = 3) out vec4 outGbuffer2;
layout(location = 4) out vec2 outMotionVector;

vec3 TerrainHeightfieldNormal(vec2 heightUV, vec2 heightDetailGradient)
{
    ivec2 heightSize = max(textureSize(heightMap, 0), ivec2(2));
    vec2 texelUV = 1.0 / vec2(heightSize - ivec2(1));
    vec2 worldStep = vec2(TerrainSize()) * texelUV;

    float heightLeft = texture(heightMap, heightUV - vec2(texelUV.x, 0.0)).r * TerrainMaxHeight();
    float heightRight = texture(heightMap, heightUV + vec2(texelUV.x, 0.0)).r * TerrainMaxHeight();
    float heightTop = texture(heightMap, heightUV - vec2(0.0, texelUV.y)).r * TerrainMaxHeight();
    float heightBottom = texture(heightMap, heightUV + vec2(0.0, texelUV.y)).r * TerrainMaxHeight();

    float slopeX = (heightRight - heightLeft) / (2.0 * worldStep.x) + heightDetailGradient.x;
    float slopeZ = (heightBottom - heightTop) / (2.0 * worldStep.y) + heightDetailGradient.y;
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
    vec2 heightDetailGradient)
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

    vec3 geometricNormal = TerrainHeightfieldNormal(terrainUV,
        heightDetailGradient + TerrainInteractionHeightGradient(worldPosition.xz));
    // 交互输出改变表面基底，原有 Terrain 材质切线法线仍在此基底上保留。
    // 当高度和法线由同一表面产生时，覆盖目标基底而非重复叠加同一高度梯度。
    if (terrainInteraction.channels.x!=0u || terrainInteraction.channels.y!=0u)
    {
        vec2 interactionUV; float coverage=TerrainInteractionCoverage(worldPosition.xz,interactionUV);
        if (coverage>0)
        {
            if (terrainInteraction.channels.x!=0u)
            {
                vec4 albedo=textureLod(terrainInteractionAlbedo,interactionUV,0);
                float alpha=clamp(albedo.a,0,1);
                if (alpha>1e-6)
                    blendedAlbedo=mix(blendedAlbedo,albedo.rgb/alpha,alpha*coverage*terrainInteraction.response.x);
            }
            if (terrainInteraction.channels.y!=0u)
            {
                vec4 normal=textureLod(terrainInteractionNormal,interactionUV,0);
                float alpha=clamp(normal.a,0,1);
                vec3 decoded=alpha>1e-6?normal.xyz/alpha*2-1:vec3(0);
                if (dot(decoded,decoded)>1e-8)
                    geometricNormal=normalize(mix(geometricNormal,normalize(decoded),
                        alpha*coverage*terrainInteraction.response.y));
            }
        }
    }

    // River coverage and the rain film are sources for the Type 2 wet-surface
    // response. Accumulated puddle water is evaluated independently below.
    float riverWetness = clamp(PcgRiverWetness(worldPosition.xz), 0.0, 1.0);
    vec3 riverWetAlbedo = blendedAlbedo * surfaceWeatherFrame.groundResponse.x;
    float riverWetRoughness = min(blendedRoughness, surfaceWeatherFrame.groundResponse.y);
    blendedAlbedo = mix(blendedAlbedo, riverWetAlbedo, riverWetness);
    blendedRoughness = mix(blendedRoughness, riverWetRoughness, riverWetness);

    mat3 tangentFrame = TerrainCotangentFrame(geometricNormal, worldPosition, terrainUV);
    vec3 finalNormal = normalize(tangentFrame * normalize(blendedTangentNormal));
    float puddleAmount = 0.0;
    SurfaceWeatherApplyGround(blendedAlbedo, blendedRoughness, finalNormal,
        geometricNormal, tangentFrame, worldPosition.xz,
        SurfaceWeatherWetFilmBit | SurfaceWeatherPuddleBit | SurfaceWeatherRippleBit,
        puddleAmount);

    // Terrain uses the otherwise-unused normal alpha to carry accumulated
    // water to the deferred dielectric F0 selection. GBuffer1.w remains the
    // existing terrain sentinel, so no other PBR material is affected.
    outNormal = vec4(finalNormal, puddleAmount);
    outGbuffer0 = vec4(blendedAlbedo, blendedRoughness);
    outGbuffer1 = vec4(0.0, blendedAO, float(MATERIAL_ID_PBR), -1024.0);
    float linearDepth = (ViewMatrix * vec4(worldPosition, 1.0)).z;
    outGbuffer2 = vec4(worldPosition, -linearDepth);
    outMotionVector = VansMotionVectorFromClip(currentClip, previousClip);
}

#endif
