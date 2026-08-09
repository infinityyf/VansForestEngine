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
    uint shadowMetaIndex;
    float iesProfileIndex;
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
    uint shadowMetaIndex;
    float iesProfileIndex;
    float iesIntensityScale;
    float pad0;
};

// ── RectLight (area light, evaluated via LTC) ─────────────────────────────
// Layout strictly mirrors the compact 96-byte VansRectLight record.
//   position_halfW  : xyz = world-space center,         w = half width  (along Right)
//   normal_halfH    : xyz = light forward (radiates +Z),w = half height (along Up)
//   right_range     : xyz = world Right basis,          w = influence range
//   up_intensity    : xyz = world Up basis,             w = intensity
//   color_twoSided  : rgb = colour,                     w = 0 or 1 (two-sided)
//   shadowMetaIndex : indirection into the cached punctual-shadow metadata.
//                     z = emissiveTextureSlot (-1 => no texture, >=0 => rectLightEmissive 层索引)
//                     w = texLodBias (发光贴图 LOD 偏移，默认 0.0)
struct RectLightData
{
    vec4 position_halfW;
    vec4 normal_halfH;
    vec4 right_range;
    vec4 up_intensity;
    vec4 color_twoSided;
    uint shadowMetaIndex;
    float attenuationExp;
    float textureSlot;
    float texLodBias;
};

struct PunctualShadowData
{
    uint firstView;
    uint viewCount;
    uint flags;
    uint ownerKey;
    float atlasWeight;
    float sourceRadius;
    float maxShadowDistance;
    float importance;
};

struct PunctualShadowViewData
{
    mat4 worldToShadow;
    vec4 atlasScaleBias;
    vec4 atlasClamp;
    vec4 texelBiasParams;
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
#define MAX_PUNCTUAL_SHADOWS 160
#define MAX_PUNCTUAL_SHADOW_VIEWS 480

#define INVALID_SHADOW_INDEX 0xffffffffu
#define PUNCTUAL_SHADOW_HAS_ATLAS          (1u << 0u)
#define PUNCTUAL_SHADOW_FALLBACK_ELIGIBLE  (1u << 1u)
#define PUNCTUAL_SHADOW_HERO               (1u << 2u)
#define PUNCTUAL_SHADOW_AFFECTS_FOG        (1u << 3u)
#define PUNCTUAL_SHADOW_AFFECTS_GI         (1u << 4u)
#define PUNCTUAL_SHADOW_OWNER_SIGNATURE    0xA5000000u
#define PUNCTUAL_SHADOW_OWNER_ATLAS_SHIFT  16u
#define PUNCTUAL_SHADOW_OWNER_ATLAS_MASK   (0x3u << PUNCTUAL_SHADOW_OWNER_ATLAS_SHIFT)
#define PUNCTUAL_SHADOW_ATLAS_COUNT        2u

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
    uint uPunctualShadowViewCount;
    vec4 softShadowParams;
    DirectionLightData uDirectionLight;
    PointLightData uPointLights[MAX_POINT_LIGHTS];
    SpotLightData uSpotLights[MAX_SPOT_LIGHTS];
    RectLightData uRectLights[MAX_RECT_LIGHTS];
    PunctualShadowData uPunctualShadows[MAX_PUNCTUAL_SHADOWS];
    PunctualShadowViewData uPunctualShadowViews[MAX_PUNCTUAL_SHADOW_VIEWS];
};

#ifdef SCREEN_SPACE_PUNCTUAL_SHADOW
layout(set = 1, binding = 17) uniform sampler2D screenSpaceShadowHZB;

layout(std140, set = 1, binding = 18) uniform ScreenSpaceShadowParamsUBO
{
    vec4 screenSize;
    vec4 punctualRayParams;
    vec4 directionalRayParams;
    vec4 fadeParams;
} uSSS;

float ScreenSpaceContactEdgeFade(vec2 uv)
{
    vec2 pixel = uv * uSSS.screenSize.xy;
    vec2 edge = min(pixel, uSSS.screenSize.xy - pixel);
    return clamp(min(edge.x, edge.y) / max(uSSS.fadeParams.x, 1.0), 0.0, 1.0);
}

float TraceScreenSpaceContactShadow(
    vec3 positionWS,
    vec3 normalWS,
    vec3 lightDirectionWS,
    float maxTraceDistance)
{
    float ndotl = dot(normalize(normalWS), normalize(lightDirectionWS));
    float normalFade = smoothstep(-0.05, 0.20, ndotl);
    if (normalFade <= 0.0 || maxTraceDistance <= 0.02)
        return 1.0;

    float traceDistance = min(maxTraceDistance, uSSS.punctualRayParams.x);
    float thickness = uSSS.punctualRayParams.y;
    float normalBias = uSSS.punctualRayParams.z;
    int maxSteps = max(int(uSSS.punctualRayParams.w), 1);

    vec3 rayOrigin = positionWS + normalWS * normalBias;
    vec3 rayEnd = rayOrigin + normalize(lightDirectionWS) * traceDistance;

    vec3 startSS;
    vec3 endSS;
    if (!HiZ_ProjectToScreenChecked(ViewMatrix, ProjectionMatrix, rayOrigin, startSS))
        return 1.0;
    if (any(lessThan(startSS.xy, vec2(0.0))) || any(greaterThan(startSS.xy, vec2(1.0))))
        return 1.0;
    bool hasEndProjection = HiZ_ProjectToScreenChecked(ViewMatrix, ProjectionMatrix, rayEnd, endSS);
    if (!hasEndProjection)
        return 1.0;

    vec2 rayUV = endSS.xy - startSS.xy;
    float pixelSpan = length(rayUV * uSSS.screenSize.xy);
    // A sub-pixel ray cannot produce a stable contact shadow. Reject it before
    // entering the hierarchy instead of spending the full iteration budget.
    if (pixelSpan < 1.5)
        return 1.0;

    float minTravel = max(normalBias * 1.5, 0.03);
    float localThickness = thickness * (1.0 + startSS.z * 0.01);
    HiZTraceResult trace = TraceHiZ_UV_Bounded(
        screenSpaceShadowHZB,
        ViewMatrix,
        ProjectionMatrix,
        rayOrigin,
        normalize(lightDirectionWS),
        traceDistance,
        1.0,
        localThickness,
        maxSteps);

    float shadow = 1.0;
    if (trace.hit)
    {
        float rayUvLengthSq = max(dot(rayUV, rayUV), 1e-8);
        float hitT = clamp(dot(trace.uv - startSS.xy, rayUV) / rayUvLengthSq, 0.0, 1.0);
        float travel = hitT * traceDistance;
        float receiverSeparation = abs(trace.depth - startSS.z);
        if (travel >= minTravel && receiverSeparation > normalBias * 0.3)
        {
            float distanceFade = 1.0 - smoothstep(0.65, 1.0, hitT);
            shadow = 1.0 - 0.92 * distanceFade;
        }
    }

    float fade = ScreenSpaceContactEdgeFade(startSS.xy) * normalFade;
    return mix(1.0, shadow, fade * clamp(uSSS.fadeParams.w, 0.0, 1.0));
}

float SamplePunctualScreenSpaceShadow(
    vec3 positionWS,
    vec3 normalWS,
    vec3 lightDirectionWS,
    float distanceToLight)
{
    return TraceScreenSpaceContactShadow(
        positionWS,
        normalWS,
        lightDirectionWS,
        distanceToLight);
}
#else
float SamplePunctualScreenSpaceShadow(
    vec3 positionWS,
    vec3 normalWS,
    vec3 lightDirectionWS,
    float distanceToLight)
{
    return 1.0;
}
#endif

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

uint GetPunctualShadowMetaIndex(uint lightType, int lightIndex)
{
    if (lightIndex < 0)
        return INVALID_SHADOW_INDEX;
    if (lightType == 0u)
        return uint(lightIndex) < uPointLightCount ? uPointLights[lightIndex].shadowMetaIndex : INVALID_SHADOW_INDEX;
    if (lightType == 1u)
        return uint(lightIndex) < uSpotLightCount ? uSpotLights[lightIndex].shadowMetaIndex : INVALID_SHADOW_INDEX;
    return uint(lightIndex) < GetRectLightCount() ? uRectLights[lightIndex].shadowMetaIndex : INVALID_SHADOW_INDEX;
}

bool IsPunctualShadowOwner(PunctualShadowData shadow, uint lightType, int lightIndex)
{
    if (lightIndex < 0 || lightIndex > 255)
        return false;
    uint expected = PUNCTUAL_SHADOW_OWNER_SIGNATURE |
        ((lightType & 0x3u) << 8u) | (uint(lightIndex) & 0xffu);
    return (shadow.ownerKey & ~PUNCTUAL_SHADOW_OWNER_ATLAS_MASK) == expected;
}

uint GetPunctualShadowAtlasIndex(PunctualShadowData shadow)
{
    return (shadow.ownerKey & PUNCTUAL_SHADOW_OWNER_ATLAS_MASK) >>
        PUNCTUAL_SHADOW_OWNER_ATLAS_SHIFT;
}

float GetPunctualLightRange(uint lightType, int lightIndex)
{
    if (lightType == 0u)
        return uPointLights[lightIndex].radius;
    if (lightType == 1u)
        return uSpotLights[lightIndex].radius;
    return uRectLights[lightIndex].right_range.w;
}

bool HasPunctualShadowFlag(uint flags, uint flag)
{
    return (flags & flag) != 0u;
}

int GetPunctualShadowQuality(uint metaIndex)
{
    if (metaIndex == INVALID_SHADOW_INDEX || metaIndex >= uint(MAX_PUNCTUAL_SHADOWS))
        return 0;
    PunctualShadowData shadow = uPunctualShadows[metaIndex];
    if (!HasPunctualShadowFlag(shadow.flags, PUNCTUAL_SHADOW_HAS_ATLAS) ||
        shadow.firstView == INVALID_SHADOW_INDEX || shadow.firstView >= uPunctualShadowViewCount)
        return 0;
    if (HasPunctualShadowFlag(shadow.flags, PUNCTUAL_SHADOW_HERO))
        return 3;
    return uPunctualShadowViews[shadow.firstView].texelBiasParams.w <= 128.0 ? 1 : 2;
}

// Unified cached-atlas sampler. qualityProfile: 0=hard, 1=4 tap,
// 2=8 tap, 3=12 tap. Hardware comparison filtering supplies bilinear PCF
// for every tap, so this replaces the legacy four-fetch manual comparison.
float SamplePunctualShadowAtlas(
    vec3 positionWS,
    vec3 normalWS,
    vec3 lightDirectionWS,
    sampler2DArrayShadow shadowMap,
    uint lightType,
    int lightIndex,
    int qualityProfile)
{
    uint metaIndex = GetPunctualShadowMetaIndex(lightType, lightIndex);
    if (metaIndex == INVALID_SHADOW_INDEX || metaIndex >= uint(MAX_PUNCTUAL_SHADOWS))
        return 1.0;

    PunctualShadowData shadow = uPunctualShadows[metaIndex];
    if (!HasPunctualShadowFlag(shadow.flags, PUNCTUAL_SHADOW_HAS_ATLAS) ||
        shadow.firstView == INVALID_SHADOW_INDEX || shadow.viewCount == 0u ||
        !IsPunctualShadowOwner(shadow, lightType, lightIndex))
        return 1.0;

    uint atlasIndex = GetPunctualShadowAtlasIndex(shadow);
    if (atlasIndex >= PUNCTUAL_SHADOW_ATLAS_COUNT)
        return 1.0;
#ifdef PUNCTUAL_SHADOW_CONSUMER_FOG
    if (!HasPunctualShadowFlag(shadow.flags, PUNCTUAL_SHADOW_AFFECTS_FOG))
        return 1.0;
#endif
#ifdef PUNCTUAL_SHADOW_CONSUMER_GI
    if (!HasPunctualShadowFlag(shadow.flags, PUNCTUAL_SHADOW_AFFECTS_GI))
        return 1.0;
#endif

    if (shadow.firstView >= uPunctualShadowViewCount ||
        shadow.viewCount > uPunctualShadowViewCount - shadow.firstView)
        return 1.0;

    // All faces of one allocation share resolution/bias parameters. Compute the
    // receiver bias first, then choose a point-light face from the biased
    // position so a normal offset cannot project through the wrong cube face.
    PunctualShadowViewData shadowView = uPunctualShadowViews[shadow.firstView];
    float lightRange = max(GetPunctualLightRange(lightType, lightIndex), 0.001);
    vec3 safeNormal = normalWS * inversesqrt(max(dot(normalWS, normalWS), 1e-8));
    vec3 safeLightDirection = lightDirectionWS * inversesqrt(max(dot(lightDirectionWS, lightDirectionWS), 1e-8));
    float normalSlope = 1.0 - clamp(dot(safeNormal, safeLightDirection), 0.0, 1.0);
    float worldTexel = lightRange / max(shadowView.texelBiasParams.w, 1.0);
    vec3 biasedPosition = positionWS + safeNormal *
        worldTexel * shadowView.texelBiasParams.z * (1.0 + normalSlope * 2.0);

    uint face = 0u;
    if (lightType == 0u)
        face = uint(GetCubemapFaceIndex(biasedPosition - uPointLights[lightIndex].position.xyz));
    if (face >= shadow.viewCount)
        return 1.0;
    shadowView = uPunctualShadowViews[shadow.firstView + face];

    vec4 clip = shadowView.worldToShadow * vec4(biasedPosition, 1.0);
    if (clip.w <= 1e-6)
        return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    vec2 localUV = ndc.xy * 0.5 + 0.5;
    float receiverDepth = ndc.z * 0.5 + 0.5;
    if (receiverDepth <= 0.0 || receiverDepth >= 1.0)
        return 1.0;

    // The projection contains a gutter guard band. Clamp tiny face/cone-edge
    // excursions into that guard band instead of returning fully lit, which
    // otherwise creates bright seams and visible light leaks.
    localUV = clamp(localUV, vec2(0.0), vec2(1.0));

    vec2 atlasUV = localUV * shadowView.atlasScaleBias.xy + shadowView.atlasScaleBias.zw;
    atlasUV = clamp(atlasUV, shadowView.atlasClamp.xy, shadowView.atlasClamp.zw);
    float depthReference = clamp(
        receiverDepth - shadowView.texelBiasParams.y * shadowView.texelBiasParams.x,
        0.0,
        1.0);

    int tapCount = qualityProfile <= 0 ? 1 : (qualityProfile == 1 ? 4 : (qualityProfile == 2 ? 8 : 12));
    float distanceToLight = lightRange;
    if (lightType == 0u)
        distanceToLight = length(uPointLights[lightIndex].position.xyz - positionWS);
    else if (lightType == 1u)
        distanceToLight = length(uSpotLights[lightIndex].position.xyz - positionWS);
    else
        distanceToLight = length(uRectLights[lightIndex].position_halfW.xyz - positionWS);

    float penumbraTexels = clamp(
        0.75 + shadow.sourceRadius * (distanceToLight / lightRange) * 12.0,
        0.75,
        qualityProfile >= 3 ? 5.0 : 3.5);
    vec2 atlasTexel = vec2(1.0 / max(float(uShadowAtlasSize), 1.0));
    float stableAngle = RandomInterLeaved(floor(atlasUV * float(uShadowAtlasSize) / 8.0)) * TWO_PI;
    vec2 rotation = vec2(cos(stableAngle), sin(stableAngle));

    float visibility = 0.0;
    for (int tap = 0; tap < 12; ++tap)
    {
        if (tap >= tapCount)
            break;
        vec2 kernel = tapCount == 1 ? vec2(0.0) : fibonacciSpiralDirection[tap];
        kernel = vec2(kernel.x * rotation.x - kernel.y * rotation.y,
                      kernel.x * rotation.y + kernel.y * rotation.x);
        vec2 sampleUV = clamp(
            atlasUV + kernel * penumbraTexels * atlasTexel,
            shadowView.atlasClamp.xy,
            shadowView.atlasClamp.zw);
        visibility += texture(shadowMap, vec4(sampleUV, float(atlasIndex), depthReference));
    }
    visibility /= float(tapCount);
    return mix(1.0, visibility, clamp(shadow.atlasWeight, 0.0, 1.0));
}




float SamplePointShadowMap(vec3 position_world, sampler2DArrayShadow shadowMap, int lightIndex)
{
    return SamplePunctualShadowAtlas(position_world, normalize(uPointLights[lightIndex].position.xyz - position_world),
        normalize(uPointLights[lightIndex].position.xyz - position_world),
        shadowMap, 0u, lightIndex, 0);
}

float SampleSpotShadowMap(vec3 position_world, sampler2DArrayShadow shadowMap, int lightIndex)
{
    return SamplePunctualShadowAtlas(position_world, normalize(uSpotLights[lightIndex].position.xyz - position_world),
        normalize(uSpotLights[lightIndex].position.xyz - position_world),
        shadowMap, 1u, lightIndex, 0);
}

// Hard rect-shadow sampling for volumetric fog (Phase 4).
float SampleRectShadowMap(vec3 position_world, sampler2DArrayShadow shadowMap, int lightIndex)
{
    return SamplePunctualShadowAtlas(position_world, normalize(uRectLights[lightIndex].position_halfW.xyz - position_world),
        normalize(uRectLights[lightIndex].position_halfW.xyz - position_world),
        shadowMap, 2u, lightIndex, 0);
}

// 计算世界空间法线偏置量（米）。
// 使用 tan(θ) 斜率模型而非旧的 (1-NdL)² 近似：
//   tan(θ) 在 grazing 时正确趋向无穷（被 max 限制），在正向光照时为 0。
// 调用方在投影前将 position_world += normalWS * normalOffset 然后投影，
// 避免 NDC 空间固定偏置因透视压缩在中等距离（2–5m）产生数十厘米的 peter-panning。
float SamplePointShadowMapBRDF(vec3 position_world, vec3 normalWS, vec3 lightDirectionWS, sampler2DArrayShadow shadowMap, int lightIndex)
{
    uint metaIndex = uPointLights[lightIndex].shadowMetaIndex;
    int quality = GetPunctualShadowQuality(metaIndex);
    return SamplePunctualShadowAtlas(position_world, normalWS, lightDirectionWS, shadowMap, 0u, lightIndex, quality);
}

float SampleSpotShadowMapBRDF(vec3 position_world, vec3 normalWS, vec3 lightDirectionWS, sampler2DArrayShadow shadowMap, int lightIndex)
{
    uint metaIndex = uSpotLights[lightIndex].shadowMetaIndex;
    int quality = GetPunctualShadowQuality(metaIndex);
    return SamplePunctualShadowAtlas(position_world, normalWS, lightDirectionWS, shadowMap, 1u, lightIndex, quality);
}

// RectLight uses the same metadata/view indirection as point and spot lights.
float SampleRectShadowMapBRDF(vec3 position_world, vec3 normalWS, vec3 lightDirectionWS, sampler2DArrayShadow shadowMap, int lightIndex)
{
    uint metaIndex = uRectLights[lightIndex].shadowMetaIndex;
    int quality = GetPunctualShadowQuality(metaIndex);
    return SamplePunctualShadowAtlas(position_world, normalWS, lightDirectionWS, shadowMap, 2u, lightIndex, quality);
}

// SamplePunctualShadowAtlas already resolves Atlas against unshadowed visibility:
//   atlasResolved = mix(1, atlasVisibility, atlasWeight).
// Replacing the unshadowed endpoint with a selected screen-space fallback can
// therefore be done exactly without sampling the Atlas a second time.
float BlendPunctualShadowFallback(
    float atlasResolved,
    float fallbackVisibility,
    uint lightType,
    int lightIndex)
{
    uint metaIndex = GetPunctualShadowMetaIndex(lightType, lightIndex);
    float atlasWeight = 0.0;
    if (metaIndex != INVALID_SHADOW_INDEX && metaIndex < uint(MAX_PUNCTUAL_SHADOWS))
    {
        PunctualShadowData shadow = uPunctualShadows[metaIndex];
        if (HasPunctualShadowFlag(shadow.flags, PUNCTUAL_SHADOW_HAS_ATLAS) &&
            IsPunctualShadowOwner(shadow, lightType, lightIndex))
            atlasWeight = clamp(shadow.atlasWeight, 0.0, 1.0);
    }
    return clamp(atlasResolved + (fallbackVisibility - 1.0) * (1.0 - atlasWeight), 0.0, 1.0);
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

void CalculateDirectDiffuse(vec3 positionWS, vec3 normalWS, sampler2D shadowMap, sampler2DArrayShadow punctualShadowMap, vec4 surfaceAlbedoRoughness, inout vec3 diffuseResult)
{
    diffuseResult = vec3(0.0);
    vec3 albedo = clamp(surfaceAlbedoRoughness.rgb, vec3(0.0), vec3(1.0));
    vec3 samplePos = positionWS;

    // GI stores outgoing diffuse radiance, including the Lambert albedo / PI term.
    float dirNoL = max(dot(normalWS, uDirectionLight.direction.xyz), 0.0);
    float dirShadow = SampleGICascadeShadow(samplePos, normalWS, shadowMap);
    diffuseResult += dirNoL * uDirectionLight.color.rgb * uDirectionLight.intensity * dirShadow * albedo * INV_PI;

    for (uint lightIndex = 0u; lightIndex < uPointLightCount; ++lightIndex)
    {
        PointLightData pointLight = GetPointLight(int(lightIndex));
        vec3 lightDirection = pointLight.position.xyz - samplePos;
        float distance = length(lightDirection);
        if (pointLight.radius <= 1e-5 || distance >= pointLight.radius)
            continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / pointLight.radius);
        attenuation *= attenuation;
        attenuation *= SamplePointShadowMapBRDF(
            samplePos, normalWS, lightDirection, punctualShadowMap, int(lightIndex));

        float noL = max(dot(normalWS, lightDirection), 0.0);
        diffuseResult += noL * pointLight.color.rgb * pointLight.intensity * attenuation * albedo * INV_PI;
    }

    for (uint lightIndex = 0u; lightIndex < uSpotLightCount; ++lightIndex)
    {
        SpotLightData spotLight = GetSpotLight(int(lightIndex));
        vec3 lightDirection = spotLight.position.xyz - samplePos;
        float distance = length(lightDirection);
        if (spotLight.radius <= 1e-5 || distance >= spotLight.radius)
            continue;

        lightDirection /= max(distance, 1e-5);
        float attenuation = 1.0 - (distance / spotLight.radius);
        attenuation *= attenuation;

        float coneAngle = dot(normalize(spotLight.direction.xyz), lightDirection);
        if (coneAngle < cos(spotLight.outerConeAngle))
            continue;

        attenuation *= SampleSpotShadowMapBRDF(
            samplePos, normalWS, lightDirection, punctualShadowMap, int(lightIndex));

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / max(innerConeAngle - outerConeAngle, 1e-5), 0.0, 1.0);

        float noL = max(dot(normalWS, lightDirection), 0.0);
        diffuseResult += noL * spotLight.color.rgb * spotLight.intensity * attenuation * coneAttenuation * albedo * INV_PI;
    }

    // Probe-hit shading has no camera direction, so it uses a diffuse-only
    // solid-angle approximation for rectangular emitters instead of the
    // view-dependent LTC BRDF path.  This keeps area lights in the DDGI
    // transport graph and uses the same punctual shadow atlas as raster.
    for (uint lightIndex = 0u; lightIndex < GetRectLightCount(); ++lightIndex)
    {
        RectLightData rectLight = GetRectLight(int(lightIndex));
        vec3 toLight = rectLight.position_halfW.xyz - samplePos;
        float distanceSquared = dot(toLight, toLight);
        float range = rectLight.right_range.w;
        if (distanceSquared >= range * range || distanceSquared <= 1e-6)
            continue;

        float distance = sqrt(distanceSquared);
        vec3 lightDirection = toLight / distance;
        float lightFacing = dot(rectLight.normal_halfH.xyz, -lightDirection);
        if (rectLight.color_twoSided.w > 0.5)
            lightFacing = abs(lightFacing);
        if (lightFacing <= 0.0)
            continue;

        float area = 4.0 * rectLight.position_halfW.w * rectLight.normal_halfH.w;
        float rangeFade = 1.0 - distance / max(range, 1e-4);
        rangeFade *= rangeFade;
        float solidAngle = area * lightFacing / max(distanceSquared, 1e-4);
        float shadow = SampleRectShadowMapBRDF(samplePos, normalWS, lightDirection,
            punctualShadowMap, int(lightIndex));
        diffuseResult += max(dot(normalWS, lightDirection), 0.0) *
            rectLight.color_twoSided.rgb * rectLight.up_intensity.w * solidAngle *
            rangeFade * shadow * albedo * INV_PI;
    }
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

void CalculateDirectLight(BRDFData brdfData, sampler2DArray cascadeShadowMap, float viewDepth, sampler2DArrayShadow punctualShadowMap, float screenSpaceShadow, inout LightResult lightResult)
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
        if (IsPointShadowFallbackSelected(_tileLightHdr, i))
        {
            shadowValue = BlendPunctualShadowFallback(
                shadowValue,
                SamplePunctualScreenSpaceShadow(brdfData.positionWS, brdfData.normal, lightDirection, distance),
                0u, int(i));
        }
        attenuation *= shadowValue;

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

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle  = cos(spotLight.innerConeAngle);
        float outerConeAngle  = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        float shadowValue = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
        if (IsSpotShadowFallbackSelected(_tileLightHdr, i))
        {
            shadowValue = BlendPunctualShadowFallback(
                shadowValue,
                SamplePunctualScreenSpaceShadow(brdfData.positionWS, brdfData.normal, lightDirection, distance),
                1u, int(i));
        }
        attenuation *= shadowValue;

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
            uint shadowMetaIndex = rl.shadowMetaIndex;
            if (shadowMetaIndex != INVALID_SHADOW_INDEX)
            {
                vec3 lightDirRect = normalize(rl.position_halfW.xyz - brdfData.positionWS);
                float shadowVal = SampleRectShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirRect, punctualShadowMap, int(i));
                if (IsRectShadowFallbackSelected(_tileLightHdr, i))
                {
                    float rectDistance = length(rl.position_halfW.xyz - brdfData.positionWS);
                    shadowVal = BlendPunctualShadowFallback(
                        shadowVal,
                        SamplePunctualScreenSpaceShadow(brdfData.positionWS, brdfData.normal, lightDirRect, rectDistance),
                        2u, int(i));
                }
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
        attenuation *= shadowValue;

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

        float coneAngle = dot(normalize(spotLight.direction.xyz), normalize(lightDirection));
        if (coneAngle < cos(spotLight.outerConeAngle)) continue;

        float innerConeAngle = cos(spotLight.innerConeAngle);
        float outerConeAngle = cos(spotLight.outerConeAngle);
        float coneAttenuation = clamp((coneAngle - outerConeAngle) / (innerConeAngle - outerConeAngle), 0.0, 1.0);

        float shadowValue = SampleSpotShadowMapBRDF(brdfData.positionWS, brdfData.normal, lightDirection, punctualShadowMap, int(i));
        attenuation *= shadowValue;

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
                uint shadowMetaIndex = rl.shadowMetaIndex;
                if (shadowMetaIndex != INVALID_SHADOW_INDEX)
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
