#include "../Common/Common.glsl"
#include "../BRDF/BRDFData.glsl"

struct DirectionLightData
{
    vec4 direction;
    vec4 color;
    float intensity;
    mat4x4 shadowMatrix[CASCADE_COUNT];
    vec4 cascadeSplits;  // view-space Z distances for each cascade boundary
};

struct PointLightData
{
    vec4 position;
    vec4 color;
    float intensity;
    float radius;
    float shadowIndex;
    mat4x4 shadowMatrix[6];
};

struct SpotLightData
{
    vec4 position;
    vec4 direction;
    vec4 color;
    float intensity;
    float radius;
    float innerConeAngle;
    float outerConeAngle;
    mat4x4 shadowMatrix;
    float shadowIndex;
};

struct LightResult
{
    vec3 directDiffuse;
    vec3 directSpecular;
    vec3 ambientDiffuse;
    vec3 ambientSpecular;
};

#define MAX_DIRECTION_LIGHTS 1
#define MAX_POINT_LIGHTS 64
#define MAX_SPOT_LIGHTS  64

#if !defined(LightCBBind)
    #define LightCBBind 0
#endif
#if !defined(LightBinding)
    #define LightBinding 1
#endif
layout(set=LightCBBind, binding=LightBinding, std430) readonly buffer LightsData
{
    uint uPointLightCount;
    uint uSpotLightCount;
    uint uShadowAtlasSize;
    uint uShadowAtlasCount;
    vec4 softShadowParams;
    DirectionLightData uDirectionLight;
    PointLightData uPointLights[MAX_POINT_LIGHTS];
    SpotLightData uSpotLights[MAX_SPOT_LIGHTS];
};

PointLightData GetPointLight(int index)
{
    return uPointLights[index];
}

SpotLightData GetSpotLight(int index)
{
    return uSpotLights[index];
}

int GetCubemapFaceIndex(vec3 dir)
{
    vec3 absDir = abs(dir);
    int face = 0;
    if (absDir.x > absDir.y && absDir.x > absDir.z)
        face = dir.x > 0.0 ? 0 : 1; // +X : -X
    else if (absDir.y > absDir.z)
        face = dir.y > 0.0 ? 2 : 3; // +Y : -Y
    else
        face = dir.z > 0.0 ? 4 : 5; // +Z : -Z
    return face;
}




float SamplePointShadowMap(vec3 position_world, sampler2D shadowMap, int shadowIndex)
{
    vec3 direction = position_world - uPointLights[shadowIndex].position.xyz;

    //获取采样的方向
    int shadowDirectionIndex = GetCubemapFaceIndex(direction);

    ivec2 shadowOffset = ivec2((shadowIndex * 6 + shadowDirectionIndex) % uShadowAtlasCount, (shadowIndex * 6 + shadowDirectionIndex) / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = uPointLights[shadowIndex].shadowMatrix[shadowDirectionIndex];
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    clipCoord/=  clipCoord.w;
    clipCoord.xy  = clipCoord.xy * 0.5 + 0.5;

    ivec2 shadowUV = ivec2(clipCoord.xy * uShadowAtlasSize);

    float shadowMapDepth = texelFetch(shadowMap, shadowUV + shadowOffset,0).r;

    return shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
}

float SampleSpotShadowMap(vec3 position_world, sampler2D shadowMap, int shadowIndex)
{
    int pointLightCount = int(uPointLightCount);
    ivec2 shadowOffset = ivec2((pointLightCount * 6 + shadowIndex) % uShadowAtlasCount, (pointLightCount * 6 + shadowIndex) / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = uSpotLights[shadowIndex].shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    clipCoord/=  clipCoord.w;
    clipCoord.xy  = clipCoord.xy * 0.5 + 0.5;

    ivec2 shadowUV = ivec2(clipCoord.xy * uShadowAtlasSize);

    float shadowMapDepth = texelFetch(shadowMap, shadowUV + shadowOffset,0).r;

    return shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
}

float ComputePunctualShadowBias(vec3 normalWS, vec3 lightDirectionWS)
{
    float ndl = clamp(dot(normalize(normalWS), normalize(lightDirectionWS)), 0.0, 1.0);
    float grazingFactor = 1.0 - ndl;
    grazingFactor *= grazingFactor;
    return PUNCTUAL_LIGHT_DEPTH_BIAS * (0.1 + grazingFactor * 0.9);
}

float ComputePunctualSoftShadowRadius(float distanceToLight, float lightRadius)
{
    float safeRadius = max(lightRadius, 1e-4);
    float distanceRatio = clamp(distanceToLight / safeRadius, 0.0, 1.0);
    float softnessScale = max(0.75, softShadowParams.y * 6.0);
    return mix(1.5, 4.5, distanceRatio) * softnessScale;
}

float SamplePunctualShadowAtlasSoft(
    sampler2D shadowMap,
    ivec2 atlasOffset,
    vec2 localShadowUV,
    float receiverDepth,
    float filterRadiusTexels)
{
    if (receiverDepth <= 0.0)
        return 1.0;

    if (localShadowUV.x <= 0.0 || localShadowUV.x >= 1.0 ||
        localShadowUV.y <= 0.0 || localShadowUV.y >= 1.0)
        return 1.0;

    const int PUNCTUAL_SOFT_SAMPLE_COUNT = 32;
    float sampleCountInverse = 1.0 / float(PUNCTUAL_SOFT_SAMPLE_COUNT);

    ivec2 localTexel = ivec2(localShadowUV * float(uShadowAtlasSize));
    ivec2 atlasMin = atlasOffset;
    ivec2 atlasMax = atlasOffset + ivec2(int(uShadowAtlasSize) - 1);

    float frameIndex = softShadowParams.x;
    float jitterAngle = RandomInterLeavedWithScale(vec2(atlasOffset + localTexel), mod(frameIndex, 64.0)) * TWO_PI;
    vec2 jitter = vec2(sin(jitterAngle), cos(jitterAngle));

    float visibility = 0.0;
    for (int i = 0; i < PUNCTUAL_SOFT_SAMPLE_COUNT; ++i)
    {
        float sampleDistNorm = 0.0;
        vec2 offset = ComputeFibonacciSpiralDiskSampleClumped(i, sampleCountInverse, sampleDistNorm);
        offset = vec2(offset.x * jitter.y + offset.y * jitter.x,
                      offset.x * -jitter.x + offset.y * jitter.y);

        ivec2 sampleTexel = atlasOffset + localTexel + ivec2(round(offset * filterRadiusTexels));
        sampleTexel = clamp(sampleTexel, atlasMin, atlasMax);

        float shadowDepth = texelFetch(shadowMap, sampleTexel, 0).r;
        visibility += (shadowDepth < receiverDepth) ? 0.0 : 1.0;
    }

    return visibility * sampleCountInverse;
}

float SamplePointShadowMapBRDF(vec3 position_world, vec3 normalWS, vec3 lightDirectionWS, sampler2D shadowMap, int shadowIndex)
{
    vec3 toLight = position_world - uPointLights[shadowIndex].position.xyz;
    int shadowDirectionIndex = GetCubemapFaceIndex(toLight);

    ivec2 shadowOffset = ivec2((shadowIndex * 6 + shadowDirectionIndex) % uShadowAtlasCount,
                               (shadowIndex * 6 + shadowDirectionIndex) / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = uPointLights[shadowIndex].shadowMatrix[shadowDirectionIndex];
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    clipCoord /= clipCoord.w;

    float bias = ComputePunctualShadowBias(normalWS, lightDirectionWS);
    float receiverDepth = clipCoord.z - bias;
    vec2 localShadowUV = clipCoord.xy * 0.5 + 0.5;

    float filterRadiusTexels = ComputePunctualSoftShadowRadius(length(toLight), uPointLights[shadowIndex].radius);
    return SamplePunctualShadowAtlasSoft(shadowMap, shadowOffset, localShadowUV, receiverDepth, filterRadiusTexels);
}

float SampleSpotShadowMapBRDF(vec3 position_world, vec3 normalWS, vec3 lightDirectionWS, sampler2D shadowMap, int shadowIndex)
{
    int pointLightCount = int(uPointLightCount);
    ivec2 shadowOffset = ivec2((pointLightCount * 6 + shadowIndex) % uShadowAtlasCount,
                               (pointLightCount * 6 + shadowIndex) / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = uSpotLights[shadowIndex].shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    clipCoord /= clipCoord.w;

    float bias = ComputePunctualShadowBias(normalWS, lightDirectionWS);
    float receiverDepth = clipCoord.z - bias;
    vec2 localShadowUV = clipCoord.xy * 0.5 + 0.5;

    float distanceToLight = length(uSpotLights[shadowIndex].position.xyz - position_world);
    float filterRadiusTexels = ComputePunctualSoftShadowRadius(distanceToLight, uSpotLights[shadowIndex].radius);
    return SamplePunctualShadowAtlasSoft(shadowMap, shadowOffset, localShadowUV, receiverDepth, filterRadiusTexels);
}

// ============================================================================
// Cascade Shadow Map sampling
// ============================================================================

// Select the best cascade for a given view-space depth.
int SelectCascade(float viewDepth)
{
    for (int i = 0; i < CASCADE_COUNT; ++i)
    {
        if (viewDepth < uDirectionLight.cascadeSplits[i])
            return i;
    }
    return CASCADE_COUNT - 1;
}

// PCF sampling on a single cascade layer (sampler2DArray).
float SampleCascadeShadowMap_PCF(vec3 position_world, sampler2DArray cascadeShadowMap, int cascadeIdx)
{
    vec4 clipCoord = uDirectionLight.shadowMatrix[cascadeIdx] * vec4(position_world, 1.0);
    float rawZ = clipCoord.z * 0.5 + 0.5;
    clipCoord.z = rawZ - DEPTH_BIAS;  // biased depth used for shadow comparison
    vec2 shadowUV = clipCoord.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    // Smooth fade at all six shadow map boundaries (XY + depth range).
    // Without the Z terms, any position beyond the cascade far plane has
    // clipCoord.z > 1, making every PCF sample appear in shadow (visibility=0)
    // while edgeFade (XY only) stays 1, producing a hard black edge with no fade.
    float fadeWidth = 0.05;
    float edgeFade = smoothstep(0.0, fadeWidth, shadowUV.x)
                   * smoothstep(0.0, fadeWidth, 1.0 - shadowUV.x)
                   * smoothstep(0.0, fadeWidth, shadowUV.y)
                   * smoothstep(0.0, fadeWidth, 1.0 - shadowUV.y)
                   * smoothstep(0.0, fadeWidth, rawZ)              // cascade near plane
                   * smoothstep(0.0, fadeWidth, 1.0 - rawZ);      // cascade far plane

    if (edgeFade <= 0.0)
        return 1.0;

    ivec3 sz = textureSize(cascadeShadowMap, 0);
    vec2 texelSize = 1.0 / vec2(sz.xy);

    float sampleCountInverse = 1.0 / float(DISK_SAMPLE_COUNT);
    float blockSearchRadius = 5.0;

    float receiverDepth = clipCoord.z;
    float avgBlockerDepth = 0.0;
    int blockerCount = 0;

    float frameIndex = softShadowParams.x;
    float sampleJitterAngle = RandomInterLeavedWithScale(shadowUV * vec2(sz.xy), mod(frameIndex,64.0)) * 2.0 * PI;
    vec2 jitter = vec2(sin(sampleJitterAngle), cos(sampleJitterAngle));

    for(int i = 0; i < DISK_SAMPLE_COUNT; ++i)
    {
        float sampleDistNorm = 0;
        vec2 offset = ComputeFibonacciSpiralDiskSampleClumped(i, sampleCountInverse, sampleDistNorm);
        offset = vec2(offset.x * jitter.y + offset.y * jitter.x, offset.x * -jitter.x + offset.y * jitter.y);
        vec2 sampleCoord = clamp(shadowUV + offset * texelSize * blockSearchRadius, vec2(0.0), vec2(1.0));
        float texDepth = texture(cascadeShadowMap, vec3(sampleCoord, float(cascadeIdx))).r;
        if(texDepth < receiverDepth)
        {
            avgBlockerDepth += texDepth;
            blockerCount++;
        }
    }

    if(blockerCount == 0)
        return 1.0;

    avgBlockerDepth /= float(blockerCount);
    float lightSizeScale = softShadowParams.y;
    float radius = clamp((receiverDepth - avgBlockerDepth) / texelSize.x * lightSizeScale, 1.0, 50.0);

    float visibility = 0.0;
    for(int i = 0; i < DISK_SAMPLE_COUNT; ++i)
    {
        float sampleDistNorm = 0;
        vec2 offset = ComputeFibonacciSpiralDiskSampleClumped(i, sampleCountInverse, sampleDistNorm);
        offset = vec2(offset.x * jitter.y + offset.y * jitter.x, offset.x * -jitter.x + offset.y * jitter.y);
        vec2 sampleCoord = clamp(shadowUV + offset * texelSize * radius, vec2(0.0), vec2(1.0));
        float texDepth = texture(cascadeShadowMap, vec3(sampleCoord, float(cascadeIdx))).r;
        visibility += (texDepth < clipCoord.z) ? 0.0 : 1.0;
    }

    visibility /= float(DISK_SAMPLE_COUNT);
    return mix(1.0, visibility, edgeFade);
}

// Sample cascade shadow with cross-cascade blending at boundaries.
float SampleCascadeShadow(vec3 position_world, sampler2DArray cascadeShadowMap, float viewDepth)
{
    int cascadeIdx = SelectCascade(viewDepth);
    float shadow = SampleCascadeShadowMap_PCF(position_world, cascadeShadowMap, cascadeIdx);

    // Blend between cascades near the boundary to avoid visible seams
    if (cascadeIdx < CASCADE_COUNT - 1)
    {
        float splitDist = uDirectionLight.cascadeSplits[cascadeIdx];
        float blendBand = splitDist * CASCADE_BLEND_BAND;
        float blendFactor = smoothstep(splitDist - blendBand, splitDist, viewDepth);
        if (blendFactor > 0.0)
        {
            float nextShadow = SampleCascadeShadowMap_PCF(position_world, cascadeShadowMap, cascadeIdx + 1);
            shadow = mix(shadow, nextShadow, blendFactor);
        }
    }

    return shadow;
}

void CalculateDirectDiffuse(vec3 positionWS, vec3 normalWS, sampler2D shadowMap, sampler2D punctualShadowMap, float sampleRadius, vec4 surfaceAlbedoRoughness, inout vec3 diffuseResult)
{
    diffuseResult = vec3(0);

    vec3 T, B;
    BuildTBN(normalWS, T, B);

    int sampleCount = 4;
    uint n = uint(sampleCount);
    float invN = 1.0 / float(sampleCount);
    float rotAngle = 0;
    float c = cos(rotAngle), s = sin(rotAngle);

    vec3 albedo = surfaceAlbedoRoughness.rgb;
    float roughness = surfaceAlbedoRoughness.a;

    // Limit effective area for stability
    float radius = min(sampleRadius, 10.0); // arbitrary max
    for (uint i = 0u; i < n; ++i)
    {
        vec2 d2 = DiskSample(i, n);              // in [0,1] radius
        d2 = vec2(d2.x * c - d2.y * s, d2.x * s + d2.y * c); // rotate
        vec3 samplePos = positionWS + (T * d2.x + B * d2.y) * radius;
        float ndl = max(dot(normalWS, uDirectionLight.direction.xyz), 0);
        // Simple shadow test using the GI cascade layer
        vec4 sClip = uDirectionLight.shadowMatrix[RAYTRACING_CASCADE_INDEX] * vec4(samplePos, 1.0);
        sClip.z = sClip.z * 0.5 + 0.5 - DEPTH_BIAS;
        vec2 sUV = sClip.xy * 0.5 + 0.5;
        sUV.y = 1.0 - sUV.y;
        float shadowV = (texture(shadowMap, sUV).r < sClip.z) ? 0.0 : 1.0;
        diffuseResult += ndl * uDirectionLight.color.rgb *albedo * uDirectionLight.intensity * shadowV;

        for (uint i = 0; i < uPointLightCount; ++i)
        {
            PointLightData pointLight = GetPointLight(int(i));
            vec3 lightDirection = pointLight.position.xyz - positionWS;
            float distance = length(lightDirection);
            if (distance > pointLight.radius) continue;

            lightDirection /= distance;
            float attenuation = 1.0 - (distance / pointLight.radius);
            attenuation *= attenuation;

            // 计算阴影
            float shadowValue = SamplePointShadowMap(positionWS, punctualShadowMap, int(pointLight.shadowIndex));
            attenuation = min(attenuation, shadowValue);

            float ndl = max(dot(normalWS, lightDirection), 0.0); //

            vec3 diffuse = vec3(0);
            diffuse = ndl * pointLight.color.rgb * pointLight.intensity * attenuation * albedo;
            diffuseResult += diffuse;
        }

        //聚光灯计算
        for (uint i = 0; i < uSpotLightCount; ++i)
        {
            SpotLightData spotLight = GetSpotLight(int(i));
            vec3 lightDirection = spotLight.position.xyz - positionWS;
            float distance = length(lightDirection);
            if (distance > spotLight.radius) continue;

            lightDirection /= distance;
            float attenuation = 1.0 - (distance / spotLight.radius);
            attenuation *= attenuation;

            // // 计算阴影
            // float shadowValue = SampleSpotShadowMap(positionWS, punctualShadowMap, int(spotLight.shadowIndex));
            // attenuation = min(attenuation, shadowValue);

            
            float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
            if (coneAngle < cos(spotLight.outerConeAngle)) continue;

            float innerConeAngle = cos(spotLight.innerConeAngle);
            float outerConeAngle = cos(spotLight.outerConeAngle);
            float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

            float ndl = max(dot(normalWS, lightDirection), 0);//
            vec3 diffuse = vec3(0);
            diffuse = ndl * spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation * albedo;

            diffuseResult += diffuse;
        }
    }
    diffuseResult *= invN * 5;
}



// Cascade shadow map version of CalculateDirectLight
void CalculateDirectLight(BRDFData brdfData, sampler2DArray cascadeShadowMap, float viewDepth, sampler2D punctualShadowMap, inout LightResult lightResult)
{
    lightResult.directDiffuse = vec3(0);
    lightResult.directSpecular = vec3(0);

    vec3 diffuseResult = vec3(0);
    vec3 specularResult = vec3(0);
    DirectBRDF(brdfData, uDirectionLight.direction.rgb, diffuseResult, specularResult);
    diffuseResult *= uDirectionLight.color.rgb * uDirectionLight.intensity;

    float shadowValue = SampleCascadeShadow(brdfData.positionWS, cascadeShadowMap, viewDepth);
    diffuseResult *= shadowValue;
    specularResult *= shadowValue;

    lightResult.directDiffuse += diffuseResult;
    lightResult.directSpecular += specularResult;

#ifdef TILE_LIGHT
    // --- TileLight 分格裁剪路径（仅 Fragment Shader，需搭配 TileLightData.glsl + define TILE_LIGHT）---
    TileLightHeader _tileLightHdr = GetFragTileLightHeader();

    // 点光源（仅遍历当前 tile 内的光源）
    for (uint _ptk = 0u; _ptk < _tileLightHdr.pointCount; ++_ptk)
    {
        uint i = tileLightIndices[_tileLightHdr.pointOffset + _ptk];
        PointLightData pointLight = GetPointLight(int(i));
        vec3 lightDirection = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;

        float shadowValue = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(pointLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        vec3 diffuseResult  = vec3(0);
        vec3 specularResult = vec3(0);
        DirectBRDF(brdfData, lightDirection, diffuseResult, specularResult);
        diffuseResult  *= pointLight.color.rgb * pointLight.intensity * attenuation;
        specularResult *= pointLight.color.rgb * pointLight.intensity * attenuation;

        lightResult.directDiffuse  += diffuseResult;
        lightResult.directSpecular += specularResult;
    }

    // 聚光灯
    for (uint _spk = 0u; _spk < _tileLightHdr.spotCount; ++_spk)
    {
        uint i = tileLightIndices[_tileLightHdr.spotOffset + _spk];
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3 lightDirection = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        float shadowValue = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(spotLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle  = cos(spotLight.innerConeAngle);
        float outerConeAngle  = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        vec3 diffuseResult  = vec3(0);
        vec3 specularResult = vec3(0);
        DirectBRDF(brdfData, lightDirection, diffuseResult, specularResult);
        diffuseResult  *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        specularResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

        lightResult.directDiffuse  += diffuseResult;
        lightResult.directSpecular += specularResult;
    }
#else
    // --- 原始 O(N) 全局循环路径（非 TILE_LIGHT 路径）---
    // Point lights
    for (uint i = 0; i < uPointLightCount; ++i)
    {
        PointLightData pointLight = GetPointLight(int(i));
        vec3 lightDirection = pointLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > pointLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;

        float shadowValue = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(pointLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        vec3 diffuseResult = vec3(0);
        vec3 specularResult = vec3(0);
        DirectBRDF(brdfData, lightDirection, diffuseResult, specularResult);
        diffuseResult *= pointLight.color.rgb * pointLight.intensity * attenuation;
        specularResult *= pointLight.color.rgb * pointLight.intensity * attenuation;

        lightResult.directDiffuse += diffuseResult;
        lightResult.directSpecular += specularResult;
    }

    // Spot lights
    for (uint i = 0; i < uSpotLightCount; ++i)
    {
        SpotLightData spotLight = GetSpotLight(int(i));
        vec3 lightDirection = spotLight.position.xyz - brdfData.positionWS;
        float distance = length(lightDirection);
        if (distance > spotLight.radius) continue;

        lightDirection /= distance;
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        float shadowValue = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(spotLight.shadowIndex));
        attenuation = min(attenuation, shadowValue);

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        vec3 diffuseResult = vec3(0);
        vec3 specularResult = vec3(0);
        DirectBRDF(brdfData, lightDirection, diffuseResult, specularResult);
        diffuseResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        specularResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

        lightResult.directDiffuse += diffuseResult;
        lightResult.directSpecular += specularResult;
    }
#endif // TILE_LIGHT
}
