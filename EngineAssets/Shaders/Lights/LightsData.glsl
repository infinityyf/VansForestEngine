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




float SamplePointShadowMap(vec3 position_world, sampler2D shadowMap, int lightIndex)
{
    PointLightData light = uPointLights[lightIndex];
    int shadowBaseSlot = int(light.shadowIndex);
    if (shadowBaseSlot < 0)
        return 1.0;
    vec3 direction = position_world - light.position.xyz;

    //获取采样的方向
    int shadowDirectionIndex = GetCubemapFaceIndex(direction);

    int atlasSlot = shadowBaseSlot + shadowDirectionIndex;
    ivec2 shadowOffset = ivec2(atlasSlot % uShadowAtlasCount, atlasSlot / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = light.shadowMatrix[shadowDirectionIndex];
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    if (clipCoord.w <= 1e-6)
        return 1.0;
    clipCoord /= clipCoord.w;
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    clipCoord.xy  = clipCoord.xy * 0.5 + 0.5;

    if (any(lessThanEqual(clipCoord.xy, vec2(0.0))) ||
        any(greaterThanEqual(clipCoord.xy, vec2(1.0))) ||
        clipCoord.z <= 0.0 || clipCoord.z >= 1.0)
        return 1.0;

    ivec2 shadowUV = ivec2(clipCoord.xy * uShadowAtlasSize);

    float shadowMapDepth = texelFetch(shadowMap, shadowUV + shadowOffset,0).r;

    return shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
}

float SampleSpotShadowMap(vec3 position_world, sampler2D shadowMap, int lightIndex)
{
    SpotLightData light = uSpotLights[lightIndex];
    int atlasSlot = int(light.shadowIndex);
    if (atlasSlot < 0)
        return 1.0;
    ivec2 shadowOffset = ivec2(atlasSlot % uShadowAtlasCount, atlasSlot / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = light.shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    if (clipCoord.w <= 1e-6)
        return 1.0;
    clipCoord /= clipCoord.w;
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    clipCoord.xy  = clipCoord.xy * 0.5 + 0.5;

    if (any(lessThanEqual(clipCoord.xy, vec2(0.0))) ||
        any(greaterThanEqual(clipCoord.xy, vec2(1.0))) ||
        clipCoord.z <= 0.0 || clipCoord.z >= 1.0)
        return 1.0;

    ivec2 shadowUV = ivec2(clipCoord.xy * uShadowAtlasSize);

    float shadowMapDepth = texelFetch(shadowMap, shadowUV + shadowOffset,0).r;

    return shadowMapDepth < clipCoord.z ? 0.0 : 1.0;
}

// Hard rect-shadow sampling for volumetric fog (Phase 4).
float SampleRectShadowMap(vec3 position_world, sampler2D shadowMap, int lightIndex)
{
    RectLightData light = uRectLights[lightIndex];
    int slotIndex = int(light.shadowParams.x);
    if (slotIndex < 0)
        return 1.0;
    ivec2 shadowOffset = ivec2(slotIndex % uShadowAtlasCount, slotIndex / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    mat4x4 shadowMatrix = light.shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(position_world, 1.0);
    if (clipCoord.w <= 1e-6)
        return 1.0;
    clipCoord /= clipCoord.w;
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    clipCoord.xy = clipCoord.xy * 0.5 + 0.5;

    if (clipCoord.x <= 0.0 || clipCoord.x >= 1.0 ||
        clipCoord.y <= 0.0 || clipCoord.y >= 1.0 ||
        clipCoord.z <= 0.0 || clipCoord.z >= 1.0)
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

float FetchPunctualShadowDepth(
    sampler2D shadowMap,
    ivec2 texel,
    ivec2 atlasMin,
    ivec2 atlasMax)
{
    return texelFetch(shadowMap, clamp(texel, atlasMin, atlasMax), 0).r;
}

float ComparePunctualShadowBilinear(
    sampler2D shadowMap,
    vec2 atlasTexelPosition,
    ivec2 atlasMin,
    ivec2 atlasMax,
    float receiverDepth)
{
    ivec2 base = ivec2(floor(atlasTexelPosition));
    vec2 f = fract(atlasTexelPosition);
    float c00 = FetchPunctualShadowDepth(shadowMap, base, atlasMin, atlasMax) < receiverDepth ? 0.0 : 1.0;
    float c10 = FetchPunctualShadowDepth(shadowMap, base + ivec2(1, 0), atlasMin, atlasMax) < receiverDepth ? 0.0 : 1.0;
    float c01 = FetchPunctualShadowDepth(shadowMap, base + ivec2(0, 1), atlasMin, atlasMax) < receiverDepth ? 0.0 : 1.0;
    float c11 = FetchPunctualShadowDepth(shadowMap, base + ivec2(1, 1), atlasMin, atlasMax) < receiverDepth ? 0.0 : 1.0;
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

float SamplePunctualShadowAtlasSoft(
    sampler2D shadowMap,
    ivec2 atlasOffset,
    vec2 localShadowUV,
    float receiverDepth,
    float filterRadiusTexels)
{
    if (receiverDepth <= 0.0 || receiverDepth >= 1.0)
        return 1.0;

    if (localShadowUV.x <= 0.0 || localShadowUV.x >= 1.0 ||
        localShadowUV.y <= 0.0 || localShadowUV.y >= 1.0)
        return 1.0;

    const int PUNCTUAL_SOFT_SAMPLE_COUNT = 24;
    float sampleCountInverse = 1.0 / float(PUNCTUAL_SOFT_SAMPLE_COUNT);

    ivec2 atlasMin = atlasOffset;
    ivec2 atlasMax = atlasOffset + ivec2(int(uShadowAtlasSize) - 1);

    vec2 localTexelPosition = localShadowUV * float(uShadowAtlasSize) - 0.5;
    float jitterAngle = RandomInterLeaved(floor(localTexelPosition / 8.0) + vec2(atlasOffset)) * TWO_PI;
    vec2 jitter = vec2(sin(jitterAngle), cos(jitterAngle));

    float visibility = 0.0;
    for (int i = 0; i < PUNCTUAL_SOFT_SAMPLE_COUNT; ++i)
    {
        float sampleDistNorm = sqrt((float(i) + 0.5) * sampleCountInverse);
        vec2 offset = fibonacciSpiralDirection[i] * sampleDistNorm;
        offset = vec2(offset.x * jitter.y + offset.y * jitter.x,
                      offset.x * -jitter.x + offset.y * jitter.y);

        vec2 atlasTexelPosition = vec2(atlasOffset) + localTexelPosition
            + offset * filterRadiusTexels;
        visibility += ComparePunctualShadowBilinear(
            shadowMap, atlasTexelPosition, atlasMin, atlasMax, receiverDepth);
    }

    return visibility * sampleCountInverse;
}

float SamplePointShadowMapBRDF(vec3 position_world, vec3 normalWS, vec3 lightDirectionWS, sampler2D shadowMap, int lightIndex)
{
    PointLightData light = uPointLights[lightIndex];
    int shadowBaseSlot = int(light.shadowIndex);
    if (shadowBaseSlot < 0)
        return 1.0;
    vec3 toLight = position_world - light.position.xyz;
    int shadowDirectionIndex = GetCubemapFaceIndex(toLight);

    int atlasSlot = shadowBaseSlot + shadowDirectionIndex;
    ivec2 shadowOffset = ivec2(atlasSlot % uShadowAtlasCount,
                               atlasSlot / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    // 在世界空间沿法线偏置接收点，再投影；避免 NDC 固定 bias 的透视放大问题
    float normalOffset = ComputePunctualNormalOffset(normalWS, lightDirectionWS);
    vec3 biasedPos = position_world + normalWS * normalOffset;

    mat4x4 shadowMatrix = light.shadowMatrix[shadowDirectionIndex];
    vec4 clipCoord = shadowMatrix * vec4(biasedPos, 1.0);
    if (clipCoord.w <= 1e-6)
        return 1.0;
    clipCoord /= clipCoord.w;

    float receiverDepth = clipCoord.z * 0.5 + 0.5;
    vec2 localShadowUV = clipCoord.xy * 0.5 + 0.5;

    float filterRadiusTexels = ComputePunctualSoftShadowRadius(length(toLight), light.radius);
    return SamplePunctualShadowAtlasSoft(shadowMap, shadowOffset, localShadowUV, receiverDepth, filterRadiusTexels);
}

float SampleSpotShadowMapBRDF(vec3 position_world, vec3 normalWS, vec3 lightDirectionWS, sampler2D shadowMap, int lightIndex)
{
    SpotLightData light = uSpotLights[lightIndex];
    int atlasSlot = int(light.shadowIndex);
    if (atlasSlot < 0)
        return 1.0;
    ivec2 shadowOffset = ivec2(atlasSlot % uShadowAtlasCount,
                               atlasSlot / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    // 在世界空间沿法线偏置接收点，再投影；避免 NDC 固定 bias 的透视放大问题
    float normalOffset = ComputePunctualNormalOffset(normalWS, lightDirectionWS);
    vec3 biasedPos = position_world + normalWS * normalOffset;

    mat4x4 shadowMatrix = light.shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(biasedPos, 1.0);
    if (clipCoord.w <= 1e-6)
        return 1.0;
    clipCoord /= clipCoord.w;

    float receiverDepth = clipCoord.z * 0.5 + 0.5;
    vec2 localShadowUV = clipCoord.xy * 0.5 + 0.5;

    float distanceToLight = length(light.position.xyz - position_world);
    float filterRadiusTexels = ComputePunctualSoftShadowRadius(distanceToLight, light.radius);
    return SamplePunctualShadowAtlasSoft(shadowMap, shadowOffset, localShadowUV, receiverDepth, filterRadiusTexels);
}

// RectLight shadow sampling — Phase 3.
// Atlas slot:  pointCount*6 + spotCount + shadowIndex   (mirrors VansScene::DrawRectShadow).
float SampleRectShadowMapBRDF(vec3 position_world, vec3 normalWS, vec3 lightDirectionWS, sampler2D shadowMap, int lightIndex)
{
    RectLightData light = uRectLights[lightIndex];
    int slotIndex = int(light.shadowParams.x);
    if (slotIndex < 0)
        return 1.0;
    ivec2 shadowOffset = ivec2(slotIndex % uShadowAtlasCount,
                               slotIndex / uShadowAtlasCount);
    shadowOffset *= int(uShadowAtlasSize);

    float normalOffset = ComputePunctualNormalOffset(normalWS, lightDirectionWS);
    vec3 biasedPos = position_world + normalWS * normalOffset;

    mat4x4 shadowMatrix = light.shadowMatrix;
    vec4 clipCoord = shadowMatrix * vec4(biasedPos, 1.0);
    if (clipCoord.w <= 1e-6)
        return 1.0;
    clipCoord /= clipCoord.w;

    float receiverDepth = clipCoord.z * 0.5 + 0.5;
    vec2 localShadowUV = clipCoord.xy * 0.5 + 0.5;

    float distanceToLight = length(light.position_halfW.xyz - position_world);
    float filterRadiusTexels = ComputePunctualSoftShadowRadius(distanceToLight, light.right_range.w);
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

// Directional-light PCSS on a single cascade layer.  Raw blocker depths are
// always fetched with texelFetch; the final filter performs compare-before-
// bilinear interpolation, avoiding the invalid "linear depth then compare"
// behaviour that produced contour bands at shadow discontinuities.
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
    float minimumBiasWorld = uDirectionLight.cascadeTexelSize[cascadeIdx] * 0.25;
    return max(minimumBiasWorld, normalBiasWorld) * uDirectionLight.cascadeDepthScale[cascadeIdx];
}

float FetchCascadeDepthR32(sampler2DArray shadowMap, ivec2 texel, int cascadeIdx)
{
    ivec3 size = textureSize(shadowMap, 0);
    texel = clamp(texel, ivec2(0), size.xy - ivec2(1));
    return texelFetch(shadowMap, ivec3(texel, cascadeIdx), 0).r;
}

float CompareCascadeDepthR32(sampler2DArray shadowMap, vec2 uv, int cascadeIdx, float receiverDepth)
{
    ivec2 size = textureSize(shadowMap, 0).xy;
    ivec2 texel = ivec2(floor(uv * vec2(size)));
    float shadowDepth = FetchCascadeDepthR32(shadowMap, texel, cascadeIdx);
    return (shadowDepth < receiverDepth) ? 0.0 : 1.0;
}

float CompareCascadeDepthBilinear(
    sampler2DArray shadowMap,
    vec2 uv,
    int cascadeIdx,
    float receiverDepth)
{
    ivec2 size = textureSize(shadowMap, 0).xy;
    vec2 texelPosition = uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(texelPosition));
    vec2 f = fract(texelPosition);

    float c00 = FetchCascadeDepthR32(shadowMap, base, cascadeIdx) < receiverDepth ? 0.0 : 1.0;
    float c10 = FetchCascadeDepthR32(shadowMap, base + ivec2(1, 0), cascadeIdx) < receiverDepth ? 0.0 : 1.0;
    float c01 = FetchCascadeDepthR32(shadowMap, base + ivec2(0, 1), cascadeIdx) < receiverDepth ? 0.0 : 1.0;
    float c11 = FetchCascadeDepthR32(shadowMap, base + ivec2(1, 1), cascadeIdx) < receiverDepth ? 0.0 : 1.0;
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

vec2 CascadeDiskSample(int sampleIndex, int sampleCount, bool clumped)
{
    float u = (float(sampleIndex) + 0.5) / float(sampleCount);
    float radius = clumped ? u : sqrt(u);
    return fibonacciSpiralDirection[sampleIndex] * radius;
}

vec2 RotateCascadeDiskSample(vec2 sampleOffset, int cascadeIdx)
{
    // One stable rotation per cascade.  A circular low-discrepancy kernel does
    // not need per-pixel/frame noise, which would trade contouring for shimmer.
    float angle = float(cascadeIdx) * 1.61803398875;
    float c = cos(angle);
    float s = sin(angle);
    return vec2(c * sampleOffset.x - s * sampleOffset.y,
                s * sampleOffset.x + c * sampleOffset.y);
}

float CascadeSunAngularRadius()
{
    // Physical solar angular radius is about 0.266 degrees.  Preserve the
    // existing 0.3 softness setting as the neutral artistic scale.
    const float solarAngularRadius = 0.00464258;
    float artisticScale = clamp(softShadowParams.y / 0.3, 0.0, 4.0);
    return solarAngularRadius * artisticScale;
}

float CascadeFootprintConfidence(vec2 uv, ivec2 size, float radiusTexels)
{
    vec2 edgeTexels = min(uv, vec2(1.0) - uv) * vec2(size);
    float nearestEdge = min(edgeTexels.x, edgeTexels.y);
    return smoothstep(max(radiusTexels * 0.5, 1.0), radiusTexels + 1.5, nearestEdge);
}

float FindCascadeAverageBlockerDepth(
    sampler2DArray shadowMap,
    CascadeProjection p,
    int cascadeIdx,
    float receiverDepth,
    float searchRadiusTexels,
    out float blockerCount)
{
    const int blockerSampleCount = 16;
    ivec2 size = textureSize(shadowMap, 0).xy;
    vec2 texelSize = 1.0 / vec2(size);
    float blockerDepthSum = 0.0;
    blockerCount = 0.0;

    for (int i = 0; i < blockerSampleCount; ++i)
    {
        vec2 disk = RotateCascadeDiskSample(
            CascadeDiskSample(i, blockerSampleCount, true), cascadeIdx);
        vec2 sampleUV = p.uv + disk * texelSize * searchRadiusTexels;
        if (any(lessThanEqual(sampleUV, vec2(0.0))) || any(greaterThanEqual(sampleUV, vec2(1.0))))
            continue;

        ivec2 sampleTexel = ivec2(floor(sampleUV * vec2(size)));
        float depth = FetchCascadeDepthR32(shadowMap, sampleTexel, cascadeIdx);
        if (depth < receiverDepth)
        {
            blockerDepthSum += depth;
            blockerCount += 1.0;
        }
    }

    return blockerCount > 0.0 ? blockerDepthSum / blockerCount : 1.0;
}

float FilterCascadePCF(
    sampler2DArray shadowMap,
    CascadeProjection p,
    int cascadeIdx,
    float receiverDepth,
    float filterRadiusTexels)
{
    const int filterSampleCount = 24;
    ivec2 size = textureSize(shadowMap, 0).xy;
    vec2 texelSize = 1.0 / vec2(size);
    float visibility = 0.0;

    for (int i = 0; i < filterSampleCount; ++i)
    {
        vec2 disk = RotateCascadeDiskSample(
            CascadeDiskSample(i, filterSampleCount, false), cascadeIdx);
        vec2 sampleUV = p.uv + disk * texelSize * filterRadiusTexels;
        visibility += CompareCascadeDepthBilinear(
            shadowMap, sampleUV, cascadeIdx, receiverDepth);
    }

    return visibility / float(filterSampleCount);
}

float SampleCascadeShadowMap_PCSS(
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

    ivec2 size = textureSize(cascadeShadowMap, 0).xy;
    float receiverDepth = p.depth - ComputeCascadeReceiverBias(normalWS, cascadeIdx);
    float maxRadiusTexels = max(uDirectionLight.cascadeFilterRadius[cascadeIdx], 1.0);
    float depthRange = 1.0 / max(uDirectionLight.cascadeDepthScale[cascadeIdx], 1e-6);
    float worldUnitsPerTexel = max(uDirectionLight.cascadeTexelSize[cascadeIdx], 1e-6);
    float tanAngularRadius = tan(CascadeSunAngularRadius());

    // This is an upper bound on the penumbra of a blocker between the light
    // near plane and the receiver, expressed in shadow texels.
    float receiverDistanceFromNear = max(p.depth, 0.0) * depthRange;
    float searchRadiusTexels = clamp(
        receiverDistanceFromNear * tanAngularRadius / worldUnitsPerTexel,
        2.0,
        maxRadiusTexels);

    projectionValid *= CascadeFootprintConfidence(
        p.uv, size, max(searchRadiusTexels, 1.0));
    if (projectionValid <= 0.0)
        return 1.0;

    float blockerCount = 0.0;
    float averageBlockerDepth = FindCascadeAverageBlockerDepth(
        cascadeShadowMap,
        p,
        cascadeIdx,
        receiverDepth,
        searchRadiusTexels,
        blockerCount);

    if (blockerCount <= 0.0)
        return 1.0;

    float blockerSeparationWorld = max(p.depth - averageBlockerDepth, 0.0) * depthRange;
    float filterRadiusTexels = clamp(
        blockerSeparationWorld * tanAngularRadius / worldUnitsPerTexel,
        0.75,
        maxRadiusTexels);

    projectionValid *= CascadeFootprintConfidence(p.uv, size, filterRadiusTexels);
    return FilterCascadePCF(
        cascadeShadowMap,
        p,
        cascadeIdx,
        receiverDepth,
        filterRadiusTexels);
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
    float shadow = SampleCascadeShadowMap_PCSS(positionWS, normalWS, cascadeShadowMap, cascadeIdx, currentValid);
    float lastSplit = uDirectionLight.cascadeSplits[CASCADE_COUNT - 1];
    float lastCascadeFade = 1.0 - smoothstep(lastSplit * 0.85, lastSplit, viewDepth);

    if (currentValid <= 0.0)
    {
        // Prefer the adjacent coarser cascade.  Searching arbitrary layers hid
        // broken cascade matrices and could send near pixels to the farthest
        // layer, producing metre-sized shadow texels.
        int farCascade = cascadeIdx + 1;
        if (farCascade < CASCADE_COUNT)
        {
            float valid = 0.0;
            float fallbackShadow = SampleCascadeShadowMap_PCSS(positionWS, normalWS, cascadeShadowMap, farCascade, valid);
            if (valid > 0.0)
                return mix(1.0, fallbackShadow, lastCascadeFade);
        }
        int nearCascade = cascadeIdx - 1;
        if (nearCascade >= 0)
        {
            float valid = 0.0;
            float fallbackShadow = SampleCascadeShadowMap_PCSS(positionWS, normalWS, cascadeShadowMap, nearCascade, valid);
            if (valid > 0.0)
                return mix(1.0, fallbackShadow, lastCascadeFade);
        }
        return 1.0;
    }

    if (currentValid < 0.95)
    {
        int fallbackCascade = cascadeIdx < CASCADE_COUNT - 1 ? cascadeIdx + 1 : cascadeIdx - 1;
        if (fallbackCascade >= 0 && fallbackCascade < CASCADE_COUNT)
        {
            float valid = 0.0;
            float fallbackShadow = SampleCascadeShadowMap_PCSS(
                positionWS, normalWS, cascadeShadowMap, fallbackCascade, valid);
            if (valid > currentValid)
            {
                shadow = mix(fallbackShadow, shadow, currentValid);
                currentValid = valid;
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
            float prevShadow = SampleCascadeShadowMap_PCSS(positionWS, normalWS, cascadeShadowMap, cascadeIdx - 1, prevValid);
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
            float nextShadow = SampleCascadeShadowMap_PCSS(positionWS, normalWS, cascadeShadowMap, cascadeIdx + 1, nextValid);
            if (nextValid > 0.0)
                shadow = mix(shadow, nextShadow, blendFactor);
        }
    }

    return mix(1.0, shadow, lastCascadeFade);
}

float FetchGICascadeDepth(sampler2D shadowMap, ivec2 texel)
{
    ivec2 size = textureSize(shadowMap, 0);
    return texelFetch(shadowMap, clamp(texel, ivec2(0), size - ivec2(1)), 0).r;
}

float CompareGICascadeDepthBilinear(sampler2D shadowMap, vec2 texelPosition, float receiverDepth)
{
    ivec2 base = ivec2(floor(texelPosition - vec2(0.5)));
    vec2 f = fract(texelPosition - vec2(0.5));
    float c00 = FetchGICascadeDepth(shadowMap, base) < receiverDepth ? 0.0 : 1.0;
    float c10 = FetchGICascadeDepth(shadowMap, base + ivec2(1, 0)) < receiverDepth ? 0.0 : 1.0;
    float c01 = FetchGICascadeDepth(shadowMap, base + ivec2(0, 1)) < receiverDepth ? 0.0 : 1.0;
    float c11 = FetchGICascadeDepth(shadowMap, base + ivec2(1, 1)) < receiverDepth ? 0.0 : 1.0;
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

// The GI pass binds one cascade layer as sampler2D.  Use a compact, stable PCF
// here instead of filtering R32 depth and comparing the interpolated value.
float SampleGICascadeShadow(vec3 positionWS, vec3 normalWS, sampler2D shadowMap)
{
    const int cascadeIdx = RAYTRACING_CASCADE_INDEX;
    CascadeProjection p = ProjectCascade(positionWS, cascadeIdx);
    if (p.valid <= 0.0)
        return 1.0;

    ivec2 size = textureSize(shadowMap, 0);
    float receiverDepth = p.depth - ComputeCascadeReceiverBias(normalWS, cascadeIdx);
    float radiusTexels = min(max(uDirectionLight.cascadeFilterRadius[cascadeIdx] * 0.1, 1.0), 2.0);
    float confidence = CascadeFootprintConfidence(p.uv, size, radiusTexels);
    if (confidence <= 0.0)
        return 1.0;

    const int sampleCount = 8;
    float visibility = 0.0;
    for (int i = 0; i < sampleCount; ++i)
    {
        vec2 disk = RotateCascadeDiskSample(CascadeDiskSample(i, sampleCount, true), cascadeIdx);
        vec2 texelPosition = p.uv * vec2(size) + disk * radiusTexels;
        visibility += CompareGICascadeDepthBilinear(shadowMap, texelPosition, receiverDepth);
    }
    return mix(1.0, visibility / float(sampleCount), confidence);
}

void CalculateDirectDiffuse(vec3 positionWS, vec3 normalWS, sampler2D shadowMap, sampler2D punctualShadowMap, float sampleRadius, vec4 surfaceAlbedoRoughness, inout vec3 diffuseResult)
{
    diffuseResult = vec3(0.0);

    vec3 T, B;
    BuildTBN(normalWS, T, B);

    const int sampleCount = 4;
    uint n = uint(sampleCount);
    float invN = 1.0 / float(sampleCount);
    float radius = min(sampleRadius, 10.0);
    vec3 albedo = clamp(surfaceAlbedoRoughness.rgb, vec3(0.0), vec3(1.0));

    for (uint diskIndex = 0u; diskIndex < n; ++diskIndex)
    {
        vec2 d2 = DiskSample(diskIndex, n);
        vec3 samplePos = positionWS + (T * d2.x + B * d2.y) * radius;

        // GI probe 缓存 hit 点向外反射的 diffuse radiance；Lambert BRDF 为 albedo / PI。
        float dirNoL = max(dot(normalWS, uDirectionLight.direction.xyz), 0.0);
        float dirShadow = SampleGICascadeShadow(samplePos, normalWS, shadowMap);
        diffuseResult += dirNoL * uDirectionLight.color.rgb * uDirectionLight.intensity * dirShadow * albedo * INV_PI;

        for (uint lightIndex = 0u; lightIndex < uPointLightCount; ++lightIndex)
        {
            PointLightData pointLight = GetPointLight(int(lightIndex));
            vec3 lightDirection = pointLight.position.xyz - samplePos;
            float distance = length(lightDirection);
            if (distance > pointLight.radius)
                continue;

            lightDirection /= distance;
            float attenuation = 1.0 - (distance / pointLight.radius);
            attenuation *= attenuation;
            attenuation = min(attenuation, SamplePointShadowMap(
                samplePos, punctualShadowMap, int(lightIndex)));

            float noL = max(dot(normalWS, lightDirection), 0.0);
            diffuseResult += noL * pointLight.color.rgb * pointLight.intensity * attenuation * albedo * INV_PI;
        }

        for (uint lightIndex = 0u; lightIndex < uSpotLightCount; ++lightIndex)
        {
            SpotLightData spotLight = GetSpotLight(int(lightIndex));
            vec3 lightDirection = spotLight.position.xyz - samplePos;
            float distance = length(lightDirection);
            if (distance > spotLight.radius)
                continue;

            lightDirection /= distance;
            float attenuation = 1.0 - (distance / spotLight.radius);
            attenuation *= attenuation;
            attenuation = min(attenuation, SampleSpotShadowMap(
                samplePos, punctualShadowMap, int(lightIndex)));

            float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
            if (coneAngle < cos(spotLight.outerConeAngle))
                continue;

            float innerConeAngle = cos(spotLight.innerConeAngle);
            float outerConeAngle = cos(spotLight.outerConeAngle);
            float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

            float noL = max(dot(normalWS, lightDirection), 0.0);
            diffuseResult += noL * spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation * albedo * INV_PI;
        }
    }

    diffuseResult *= invN;
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

        float shadowValue = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
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

        float shadowValue = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
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
                float shadowVal = SampleRectShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirRect, punctualShadowMap, int(i));
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

        float shadowValue = SamplePointShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
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

        float shadowValue = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
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
                    float shadowVal = SampleRectShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirRect, punctualShadowMap, int(i));
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
