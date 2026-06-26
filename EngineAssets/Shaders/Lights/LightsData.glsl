#include "../Common/Common.glsl"
#include "../BRDF/BRDFData.glsl"

struct DirectionLightData
{
    vec4 direction;
    vec4 color;
    float intensity;
    mat4x4 shadowMatrix[CASCADE_COUNT];
    vec4 cascadeSplits;  // view-space Z distances for each cascade boundary
    vec4 cascadeTexelSize;
    vec4 cascadeDepthScale;
    vec4 cascadeNormalBias;
    vec4 cascadeFilterRadius;
};

struct PointLightData
{
    vec4 position;
    vec4 color;
    float intensity;
    float radius;
    float shadowIndex;
    float iesProfileIndex;  // IES profile 层索引（-1 = 无 IES），与 VansPointLight::m_IESProfileIndex 对应
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
    float iesProfileIndex;    // IES profile 层索引（-1 = 无 IES），与 VansSpotLight::m_IESProfileIndex 对应
    float iesIntensityScale;  // IES profile 强度缩放（默认 1.0）
    float pad0;               // 填充至 144 字节（与 VansSpotLight::m_pad0 对应）
};

// ── RectLight (area light, evaluated via LTC) ─────────────────────────────
// Layout strictly mirrors VansRectLight (160 bytes, std430-compatible).
//   position_halfW  : xyz = world-space center,         w = half width  (along Right)
//   normal_halfH    : xyz = light forward (radiates +Z),w = half height (along Up)
//   right_range     : xyz = world Right basis,          w = influence range
//   up_intensity    : xyz = world Up basis,             w = intensity
//   color_twoSided  : rgb = colour,                     w = 0 or 1 (two-sided)
//   shadowMatrix    : VP matrix shared with PunctualShadow atlas (Phase 3)
//   shadowParams    : x = shadowIndex (-1 => no shadow), y = attenuation exponent
//                     z = emissiveTextureSlot (-1 => no texture, >=0 => rectLightEmissive 层索引)
//                     w = texLodBias (发光贴图 LOD 偏移，默认 0.0)
struct RectLightData
{
    vec4 position_halfW;
    vec4 normal_halfH;
    vec4 right_range;
    vec4 up_intensity;
    vec4 color_twoSided;
    mat4 shadowMatrix;
    vec4 shadowParams;
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
#define MAX_RECT_LIGHTS  32

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
    RectLightData uRectLights[MAX_RECT_LIGHTS];
};

PointLightData GetPointLight(int index)
{
    return uPointLights[index];
}

SpotLightData GetSpotLight(int index)
{
    return uSpotLights[index];
}

// RectLight 计数从 softShadowParams.z 读取（CPU 端在 UpdateLightCPUData 中写入）。
// 之所以不复用 m_LightCounts[3]：该槽位为 punctual shadow atlas tilesPerRow，shader 已大量依赖。
uint GetRectLightCount()
{
    return uint(softShadowParams.z);
}

RectLightData GetRectLight(int index)
{
    return uRectLights[index];
}

// 用于 Tile 粗筛：以"前向半深"为球心，半径同时罩住矩形 4 个角与 m_Range 处的远端圆。
// 双面发光时退化为以 Position 为球心的更大球。
vec3 GetRectSphereCenter(RectLightData rl)
{
    if (rl.color_twoSided.w > 0.5)
        return rl.position_halfW.xyz;
    return rl.position_halfW.xyz + rl.normal_halfH.xyz * (rl.right_range.w * 0.5);
}

float GetRectSphereRadius(RectLightData rl)
{
    float halfW = rl.position_halfW.w;
    float halfH = rl.normal_halfH.w;
    float range = rl.right_range.w;
    if (rl.color_twoSided.w > 0.5)
        return sqrt(range * range + halfW * halfW + halfH * halfH);
    return sqrt(0.25 * range * range + halfW * halfW + halfH * halfH);
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

// Hard rect-shadow sampling for volumetric fog (Phase 4).
float SampleRectShadowMap(vec3 position_world, sampler2D shadowMap, int shadowIndex)
{
    int pointLightCount = int(uPointLightCount);
    int spotLightCount  = int(uSpotLightCount);
    int slotIndex = pointLightCount * 6 + spotLightCount + shadowIndex;
    ivec2 shadowOffset = ivec2(slotIndex % uShadowAtlasCount, slotIndex / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = uRectLights[shadowIndex].shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    clipCoord /= clipCoord.w;
    clipCoord.xy = clipCoord.xy * 0.5 + 0.5;

    if (clipCoord.x <= 0.0 || clipCoord.x >= 1.0 ||
        clipCoord.y <= 0.0 || clipCoord.y >= 1.0 || clipCoord.z <= 0.0)
        return 1.0;

    ivec2 shadowUV = ivec2(clipCoord.xy * uShadowAtlasSize);
    float shadowMapDepth = texelFetch(shadowMap, shadowUV + shadowOffset, 0).r;
    return shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
}

// 计算世界空间法线偏置量（米）。
// 使用 tan(θ) 斜率模型而非旧的 (1-NdL)² 近似：
//   tan(θ) 在 grazing 时正确趋向无穷（被 max 限制），在正向光照时为 0。
// 调用方在投影前将 position_world += normalWS * normalOffset 然后投影，
// 避免 NDC 空间固定偏置因透视压缩在中等距离（2–5m）产生数十厘米的 peter-panning。
float ComputePunctualNormalOffset(vec3 normalWS, vec3 lightDirectionWS)
{
    float ndl = clamp(dot(normalize(normalWS), normalize(lightDirectionWS)), 0.0, 1.0);
    // tan(θ) = sqrt(1 - NdL²) / NdL，限制上界防止 grazing 发散
    float slope = min(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.001), PUNCTUAL_SLOPE_BIAS_MAX);
    return PUNCTUAL_NORMAL_OFFSET_BASE * (1.0 + slope * PUNCTUAL_SLOPE_BIAS_SCALE);
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

    // 在世界空间沿法线偏置接收点，再投影；避免 NDC 固定 bias 的透视放大问题
    float normalOffset = ComputePunctualNormalOffset(normalWS, lightDirectionWS);
    vec3 biasedPos = position_world + normalWS * normalOffset;

    mat4x4 shadowMatrix = uPointLights[shadowIndex].shadowMatrix[shadowDirectionIndex];
    vec4 clipCoord = shadowMatrix * vec4(biasedPos, 1.0);
    clipCoord /= clipCoord.w;

    float receiverDepth = clipCoord.z;
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

    // 在世界空间沿法线偏置接收点，再投影；避免 NDC 固定 bias 的透视放大问题
    float normalOffset = ComputePunctualNormalOffset(normalWS, lightDirectionWS);
    vec3 biasedPos = position_world + normalWS * normalOffset;

    mat4x4 shadowMatrix = uSpotLights[shadowIndex].shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(biasedPos, 1.0);
    clipCoord /= clipCoord.w;

    float receiverDepth = clipCoord.z;
    vec2 localShadowUV = clipCoord.xy * 0.5 + 0.5;

    float distanceToLight = length(uSpotLights[shadowIndex].position.xyz - position_world);
    float filterRadiusTexels = ComputePunctualSoftShadowRadius(distanceToLight, uSpotLights[shadowIndex].radius);
    return SamplePunctualShadowAtlasSoft(shadowMap, shadowOffset, localShadowUV, receiverDepth, filterRadiusTexels);
}

// RectLight shadow sampling — Phase 3.
// Atlas slot:  pointCount*6 + spotCount + shadowIndex   (mirrors VansScene::DrawRectShadow).
float SampleRectShadowMapBRDF(vec3 position_world, vec3 normalWS, vec3 lightDirectionWS, sampler2D shadowMap, int shadowIndex)
{
    int pointLightCount = int(uPointLightCount);
    int spotLightCount  = int(uSpotLightCount);
    int slotIndex = pointLightCount * 6 + spotLightCount + shadowIndex;
    ivec2 shadowOffset = ivec2(slotIndex % uShadowAtlasCount,
                               slotIndex / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    float normalOffset = ComputePunctualNormalOffset(normalWS, lightDirectionWS);
    vec3 biasedPos = position_world + normalWS * normalOffset;

    mat4x4 shadowMatrix = uRectLights[shadowIndex].shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(biasedPos, 1.0);
    clipCoord /= clipCoord.w;

    float receiverDepth = clipCoord.z;
    vec2 localShadowUV = clipCoord.xy * 0.5 + 0.5;

    float distanceToLight = length(uRectLights[shadowIndex].position_halfW.xyz - position_world);
    float filterRadiusTexels = ComputePunctualSoftShadowRadius(distanceToLight, uRectLights[shadowIndex].right_range.w);
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
struct CascadeProjection
{
    vec2 uv;
    float depth;
    float valid;
};

CascadeProjection ProjectCascade(vec3 positionWS, int cascadeIdx)
{
    vec4 clip = uDirectionLight.shadowMatrix[cascadeIdx] * vec4(positionWS, 1.0);
    vec3 ndc = clip.xyz / max(abs(clip.w), 1e-6);

    CascadeProjection p;
    p.depth = ndc.z * 0.5 + 0.5;
    p.uv = ndc.xy * 0.5 + 0.5;
    p.uv.y = 1.0 - p.uv.y;
    p.valid = (p.uv.x > 0.0 && p.uv.x < 1.0 &&
               p.uv.y > 0.0 && p.uv.y < 1.0 &&
               p.depth > 0.0 && p.depth < 1.0) ? 1.0 : 0.0;
    return p;
}

float ComputeCascadeReceiverBias(vec3 normalWS, int cascadeIdx)
{
    vec3 n = normalize(normalWS);
    vec3 l = normalize(uDirectionLight.direction.xyz);
    float ndl = clamp(dot(n, l), 0.0, 1.0);
    float slope = min(sqrt(max(0.0, 1.0 - ndl * ndl)) / max(ndl, 0.05), 4.0);
    float normalBiasWorld = uDirectionLight.cascadeNormalBias[cascadeIdx] * (1.0 + slope * 0.75);
    return max(DEPTH_BIAS, normalBiasWorld * uDirectionLight.cascadeDepthScale[cascadeIdx]);
}

float CompareCascadeDepthR32(sampler2DArray shadowMap, vec2 uv, int cascadeIdx, float receiverDepth)
{
    float shadowDepth = texture(shadowMap, vec3(uv, float(cascadeIdx))).r;
    return (shadowDepth < receiverDepth) ? 0.0 : 1.0;
}

float SampleCascadeShadowMap_StablePCF(
    vec3 positionWS,
    vec3 normalWS,
    sampler2DArray cascadeShadowMap,
    int cascadeIdx,
    out float projectionValid)
{
    CascadeProjection p = ProjectCascade(positionWS, cascadeIdx);
    projectionValid = p.valid;
    if (p.valid <= 0.0)
        return 1.0;

    ivec3 sz = textureSize(cascadeShadowMap, 0);
    vec2 texelSize = 1.0 / vec2(sz.xy);

    float receiverDepth = p.depth - ComputeCascadeReceiverBias(normalWS, cascadeIdx);
    float radius = max(uDirectionLight.cascadeFilterRadius[cascadeIdx], 1.35);

    float visibility = 0.0;
    float weightSum = 0.0;
    float totalWeight = 0.0;

    for (int y = -3; y <= 3; ++y)
    {
        for (int x = -3; x <= 3; ++x)
        {
            vec2 o = vec2(float(x), float(y));
            float wx = 1.0 - abs(float(x)) / 4.0;
            float wy = 1.0 - abs(float(y)) / 4.0;
            float w = wx * wy;
            vec2 uv = p.uv + o * texelSize * radius;
            totalWeight += w;
            if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0)
                continue;

            visibility += CompareCascadeDepthR32(cascadeShadowMap, uv, cascadeIdx, receiverDepth) * w;
            weightSum += w;
        }
    }

    if (weightSum <= 1e-4)
    {
        projectionValid = 0.0;
        return 1.0;
    }

    float edgeConfidence = clamp(weightSum / max(totalWeight, 1e-4), 0.0, 1.0);
    projectionValid *= smoothstep(0.45, 0.85, edgeConfidence);
    return visibility / max(weightSum, 1e-4);
}

float ComputeCascadeBlendFactor(float viewDepth, int cascadeIdx)
{
    if (cascadeIdx >= CASCADE_COUNT - 1)
        return 0.0;

    float splitDist = uDirectionLight.cascadeSplits[cascadeIdx];
    float nextSplit = uDirectionLight.cascadeSplits[cascadeIdx + 1];
    float prevSplit = (cascadeIdx > 0) ? uDirectionLight.cascadeSplits[cascadeIdx - 1] : 0.0;
    float cascadeSpan = max(nextSplit - prevSplit, 1.0);
    float band = clamp(cascadeSpan * 0.08, 1.0, 25.0);
    return smoothstep(splitDist - band, splitDist + band, viewDepth);
}

float SampleCascadeShadow(vec3 positionWS, vec3 normalWS, sampler2DArray cascadeShadowMap, float viewDepth)
{
    int cascadeIdx = SelectCascade(viewDepth);
    float currentValid = 0.0;
    float shadow = SampleCascadeShadowMap_StablePCF(positionWS, normalWS, cascadeShadowMap, cascadeIdx, currentValid);

    if (currentValid <= 0.0)
    {
        for (int offset = 1; offset < CASCADE_COUNT; ++offset)
        {
            int nearCascade = cascadeIdx - offset;
            if (nearCascade >= 0)
            {
                float valid = 0.0;
                float fallbackShadow = SampleCascadeShadowMap_StablePCF(positionWS, normalWS, cascadeShadowMap, nearCascade, valid);
                if (valid > 0.0)
                    return fallbackShadow;
            }

            int farCascade = cascadeIdx + offset;
            if (farCascade < CASCADE_COUNT)
            {
                float valid = 0.0;
                float fallbackShadow = SampleCascadeShadowMap_StablePCF(positionWS, normalWS, cascadeShadowMap, farCascade, valid);
                if (valid > 0.0)
                    return fallbackShadow;
            }
        }
        return 1.0;
    }

    if (currentValid < 0.95)
    {
        for (int offset = 1; offset < CASCADE_COUNT; ++offset)
        {
            int nearCascade = cascadeIdx - offset;
            if (nearCascade >= 0)
            {
                float valid = 0.0;
                float fallbackShadow = SampleCascadeShadowMap_StablePCF(positionWS, normalWS, cascadeShadowMap, nearCascade, valid);
                if (valid > currentValid)
                {
                    shadow = mix(fallbackShadow, shadow, currentValid);
                    break;
                }
            }

            int farCascade = cascadeIdx + offset;
            if (farCascade < CASCADE_COUNT)
            {
                float valid = 0.0;
                float fallbackShadow = SampleCascadeShadowMap_StablePCF(positionWS, normalWS, cascadeShadowMap, farCascade, valid);
                if (valid > currentValid)
                {
                    shadow = mix(fallbackShadow, shadow, currentValid);
                    break;
                }
            }
        }
    }

    if (cascadeIdx > 0)
    {
        float prevSplit = uDirectionLight.cascadeSplits[cascadeIdx - 1];
        float currentSplit = uDirectionLight.cascadeSplits[cascadeIdx];
        float prevPrevSplit = (cascadeIdx > 1) ? uDirectionLight.cascadeSplits[cascadeIdx - 2] : 0.0;
        float cascadeSpan = max(currentSplit - prevPrevSplit, 1.0);
        float band = clamp(cascadeSpan * 0.08, 1.0, 25.0);
        float blendFromPrev = 1.0 - smoothstep(prevSplit - band, prevSplit + band, viewDepth);
        if (blendFromPrev > 0.0)
        {
            float prevValid = 0.0;
            float prevShadow = SampleCascadeShadowMap_StablePCF(positionWS, normalWS, cascadeShadowMap, cascadeIdx - 1, prevValid);
            if (prevValid > 0.0)
                shadow = mix(shadow, prevShadow, blendFromPrev);
        }
    }
    if (cascadeIdx < CASCADE_COUNT - 1)
    {
        float blendFactor = ComputeCascadeBlendFactor(viewDepth, cascadeIdx);
        if (blendFactor > 0.0)
        {
            float nextValid = 0.0;
            float nextShadow = SampleCascadeShadowMap_StablePCF(positionWS, normalWS, cascadeShadowMap, cascadeIdx + 1, nextValid);
            if (nextValid > 0.0)
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
// Forward declaration: EvaluateRectLightLTC is defined in Lighting/RectLightLTC.glsl,
// which downstream shaders include AFTER this file (RectLightLTC depends on
// RectLightData/GetRectLight declared above).  GLSL needs the prototype here so
// CalculateDirectLight can reference the function before its definition.
void EvaluateRectLightLTC(
    RectLightData rl,
    vec3 N, vec3 V, vec3 P,
    float roughness, vec3 diffuseColor, vec3 F0,
    out vec3 outDiffuse, out vec3 outSpecular);

// Forward declaration: SampleIESProfile is defined in Deferred.frag after the
// iesProfileTexture sampler2DArray binding (set=1, binding=16).
// profileIndex = -1 means "no IES", in which case callers should skip the call.
float SampleIESProfile(int profileIndex, vec3 lightDir, vec3 nadirDir);

void CalculateDirectLight(BRDFData brdfData, sampler2DArray cascadeShadowMap, float viewDepth, sampler2D punctualShadowMap, float screenSpaceShadow, inout LightResult lightResult)
{
    lightResult.directDiffuse = vec3(0);
    lightResult.directSpecular = vec3(0);

    vec3 diffuseResult = vec3(0);
    vec3 specularResult = vec3(0);
    DirectBRDF(brdfData, uDirectionLight.direction.rgb, diffuseResult, specularResult);
    diffuseResult *= uDirectionLight.color.rgb * uDirectionLight.intensity;

    float shadowValue = min(SampleCascadeShadow(brdfData.positionWS, brdfData.normal, cascadeShadowMap, viewDepth), screenSpaceShadow);
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

#ifdef IES_PROFILE_ENABLED
        int ptIESIdx = int(pointLight.iesProfileIndex);
        if (ptIESIdx >= 0)
        {
            // 点光源 NADIR 轴使用世界空间向下方向（Type C IES 光源通常竖直安装）
            attenuation *= SampleIESProfile(ptIESIdx, lightDirection, vec3(0.0, -1.0, 0.0));
        }
#endif

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

#ifdef IES_PROFILE_ENABLED
        int spIESIdx = int(spotLight.iesProfileIndex);
        if (spIESIdx >= 0)
        {
            // 聚光灯 NADIR 轴 = 聚光灯朝向（direction.xyz 已在 CPU 端归一化）
            attenuation *= SampleIESProfile(spIESIdx, lightDirection, spotLight.direction.xyz)
                         * spotLight.iesIntensityScale;
        }
#endif

        vec3 diffuseResult  = vec3(0);
        vec3 specularResult = vec3(0);
        DirectBRDF(brdfData, lightDirection, diffuseResult, specularResult);
        diffuseResult  *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        specularResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

        lightResult.directDiffuse  += diffuseResult;
        lightResult.directSpecular += specularResult;
    }

    // RectLight (LTC) — TileLight 路径：仅遍历当前 tile 内的面光源
    if (_tileLightHdr.rectCount > 0u)
    {
        vec3 ltcF0_t = mix(brdfData.fresnel0, brdfData.albedo, brdfData.metallic);
        vec3 ltcDiffColor_t = brdfData.albedo * (1.0 - brdfData.metallic);
        for (uint _rck = 0u; _rck < _tileLightHdr.rectCount; ++_rck)
        {
            uint i = tileLightIndices[_tileLightHdr.rectOffset + _rck];
            RectLightData rl = GetRectLight(int(i));
            vec3 rectD = vec3(0.0);
            vec3 rectS = vec3(0.0);
            EvaluateRectLightLTC(
                rl,
                brdfData.normal, brdfData.viewDirection, brdfData.positionWS,
                brdfData.roughness, ltcDiffColor_t, ltcF0_t,
                rectD, rectS);
            int shadowIdx = int(rl.shadowParams.x);
            if (shadowIdx >= 0)
            {
                vec3 lightDirRect = normalize(rl.position_halfW.xyz - brdfData.positionWS);
                float shadowVal = SampleRectShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirRect, punctualShadowMap, shadowIdx);
                rectD *= shadowVal;
                rectS *= shadowVal;
            }
            lightResult.directDiffuse  += rectD;
            lightResult.directSpecular += rectS;
        }
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

#ifdef IES_PROFILE_ENABLED
        int ptIESIdx = int(pointLight.iesProfileIndex);
        if (ptIESIdx >= 0)
        {
            attenuation *= SampleIESProfile(ptIESIdx, lightDirection, vec3(0.0, -1.0, 0.0));
        }
#endif

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

#ifdef IES_PROFILE_ENABLED
        int spIESIdx = int(spotLight.iesProfileIndex);
        if (spIESIdx >= 0)
        {
            attenuation *= SampleIESProfile(spIESIdx, lightDirection, spotLight.direction.xyz)
                         * spotLight.iesIntensityScale;
        }
#endif

        vec3 diffuseResult = vec3(0);
        vec3 specularResult = vec3(0);
        DirectBRDF(brdfData, lightDirection, diffuseResult, specularResult);
        diffuseResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;
        specularResult *= spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation;

        lightResult.directDiffuse += diffuseResult;
        lightResult.directSpecular += specularResult;
    }
#endif // TILE_LIGHT

    // ── RectLight (LTC) ──────────────────────────────────────────────────
    // 非 TILE_LIGHT 路径：全量遍历。TILE_LIGHT 路径已在上面分格内联处理。
#ifndef TILE_LIGHT
    {
        uint rectCount = GetRectLightCount();
        if (rectCount > 0u)
        {
            vec3 ltcF0 = mix(brdfData.fresnel0, brdfData.albedo, brdfData.metallic);
            vec3 ltcDiffColor = brdfData.albedo * (1.0 - brdfData.metallic);
            for (uint i = 0u; i < rectCount && i < uint(MAX_RECT_LIGHTS); ++i)
            {
                RectLightData rl = GetRectLight(int(i));
                vec3 rectD = vec3(0.0);
                vec3 rectS = vec3(0.0);
                EvaluateRectLightLTC(
                    rl,
                    brdfData.normal, brdfData.viewDirection, brdfData.positionWS,
                    brdfData.roughness, ltcDiffColor, ltcF0,
                    rectD, rectS);
                int shadowIdx = int(rl.shadowParams.x);
                if (shadowIdx >= 0)
                {
                    vec3 lightDirRect = normalize(rl.position_halfW.xyz - brdfData.positionWS);
                    float shadowVal = SampleRectShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirRect, punctualShadowMap, shadowIdx);
                    rectD *= shadowVal;
                    rectS *= shadowVal;
                }
                lightResult.directDiffuse  += rectD;
                lightResult.directSpecular += rectS;
            }
        }
    }
#endif // !TILE_LIGHT
}
