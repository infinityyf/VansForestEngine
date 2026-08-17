#version 450
#extension GL_GOOGLE_include_directive : require

// TileLight：先引入 CameraData（提供 ScreenParams），再定义 TILE_LIGHT，再引入 TileLightData
#include "../Common/CameraData.glsl"
#define TILE_LIGHT
#define SCREEN_SPACE_PUNCTUAL_SHADOW
#include "../Common/TileLightData.glsl"

#include "../Lights/LightsData.glsl"
#include "../BRDF/BRDFData.glsl"
// 面光源发光贴图数组：最多 32 层，每层 256×256，完整 mip（在 RectLightLTC.glsl 引入前声明）。
#define RECT_LIGHT_EMISSIVE_ENABLED
layout( set = 1, binding = 15 ) uniform sampler2DArray rectLightEmissive;
#include "../Lighting/RectLightLTC.glsl"
#include "../BRDF/BRDFSkin.glsl"
#include "../BRDF/BRDFCloth.glsl"
#include "../BRDF/BRDFSubsurface.glsl"
#include "../BRDF/TreeLeafData.glsl"
#include "../BRDF/BRDFVegetation.glsl"
#include "../Common/CameraData.glsl"

// IES profile 纹理数组：最多 32 层，每层 256×128，格式 R16F，用于方向性光照衰减（binding=16）
layout( set = 1, binding = 16 ) uniform sampler2DArray iesProfileTexture;

// =============================================================================
// SampleIESProfile — 从 IES profile 纹理数组采样方向衰减系数 [0,1]
// 参数：
//   profileIndex  — 纹理数组层索引（来自 PointLightData/SpotLightData.iesProfileIndex）
//   lightDir      — 世界空间中从光源指向被照点的方向向量（已归一化）
//   nadirDir      — 光源的 NADIR 轴（IES type C 垂直 0° 方向），向下为正
//                   点光源使用 vec3(0,-1,0)；聚光灯使用 spotLight.direction.xyz
// 参数化：
//   φ  = 垂直角（与 nadirDir 夹角，0° = nadir，90° = 水平，180° = zenith）
//       → UV.y = (cos(φ) * 0.5) + 0.5
//   θ  = 水平角，由沿 nadirDir 平面投影得到
//       → UV.x = θ * INV_TWO_PI + 0.5（范围 [0,1]）
// 返回：归一化到 [0,1] 的坎德拉系数（0 = 被遮挡方向，1 = 峰值方向）
// =============================================================================
float SampleIESProfile(int profileIndex, vec3 lightDir, vec3 nadirDir)
{
    // 垂直角 φ：lightDir 与 nadirDir 的夹角
    // dot(lightDir, nadirDir) = cos(φ)，范围 [-1, 1]
    float cosVert = clamp(dot(lightDir, nadirDir), -1.0, 1.0);
    float uv_y    = cosVert * 0.5 + 0.5;  // [0,1]，0 = zenith，1 = nadir

    // 水平角 θ：将 lightDir 投影到垂直于 nadirDir 的平面，再用 atan2 计算角度
    vec3  projOnPlane = normalize(lightDir - cosVert * nadirDir + vec3(1e-8));

    // 构建参考系（任意与 nadirDir 正交的向量作为 θ=0 参考方向）
    vec3  refX = normalize(abs(nadirDir.z) < 0.99 ? cross(nadirDir, vec3(0.0, 0.0, 1.0))
                                                   : cross(nadirDir, vec3(0.0, 1.0, 0.0)));
    vec3  refY = cross(nadirDir, refX);
    float theta = atan(dot(projOnPlane, refY), dot(projOnPlane, refX));  // [-π, π]
    float uv_x  = theta * INV_TWO_PI + 0.5;  // [0, 1]

    return texture(iesProfileTexture, vec3(uv_x, uv_y, float(profileIndex))).r;
}

layout(set = 1, binding = 0) uniform sampler2D normalInput;
layout(set = 1, binding = 1) uniform sampler2D gbufferInput0;
layout(set = 1, binding = 2) uniform sampler2D gbufferInput1;
layout(set = 1, binding = 3) uniform sampler2D gbufferInput2;
layout(set = 1, binding = 4) uniform sampler2D depthInput;

layout(set = 1, binding = 5, rgba32f ) uniform image2D ssao;
layout(set = 1, binding = 6) uniform sampler2D ssgi;
layout(set = 1, binding = 7, rgba32f ) uniform image2D ssr;
layout(set = 1, binding = 8) uniform sampler2DArray cascadeShadowMap;
layout(set = 1, binding = 9) uniform sampler2DArrayShadow punctualShadowMap;
layout( set = 1, binding = 13 ) uniform sampler2D fogResult;
layout( set = 1, binding = 14 ) uniform sampler2D screenSpaceShadow;
struct GIRegionParams
{
    vec4 volumeMin;
    vec4 volumeSizeAndBias;
    vec4 traceParams;
    vec4 gridDimensionsAndPriority;
};
layout(set = 1, binding = 19) uniform giProbeInfo
{
    vec4 screenSize;
    vec4 regionInfo;
    GIRegionParams regions[8];
    vec4 deferredProbeDebug;
};
layout(set = 1, binding = 20) uniform sampler2D giVisibilityAtlas[8];
layout(set = 1, binding = 21) uniform sampler2D giIrradianceAtlas[8];
#include "../GI/GIProbeStateData.glsl"
layout(set = 1, binding = 22, std430) readonly buffer DeferredGIProbeStateBuffer0 { GIProbeState states[]; } giProbeState0;
layout(set = 1, binding = 23, std430) readonly buffer DeferredGIProbeStateBuffer1 { GIProbeState states[]; } giProbeState1;
layout(set = 1, binding = 24, std430) readonly buffer DeferredGIProbeStateBuffer2 { GIProbeState states[]; } giProbeState2;
layout(set = 1, binding = 25, std430) readonly buffer DeferredGIProbeStateBuffer3 { GIProbeState states[]; } giProbeState3;
layout(set = 1, binding = 26, std430) readonly buffer DeferredGIProbeStateBuffer4 { GIProbeState states[]; } giProbeState4;
layout(set = 1, binding = 27, std430) readonly buffer DeferredGIProbeStateBuffer5 { GIProbeState states[]; } giProbeState5;
layout(set = 1, binding = 28, std430) readonly buffer DeferredGIProbeStateBuffer6 { GIProbeState states[]; } giProbeState6;
layout(set = 1, binding = 29, std430) readonly buffer DeferredGIProbeStateBuffer7 { GIProbeState states[]; } giProbeState7;
GIProbeState LoadDeferredGIProbeState(uint regionIndex, uint probeLinearIndex)
{
    switch (regionIndex)
    {
    case 0u: return giProbeState0.states[probeLinearIndex];
    case 1u: return giProbeState1.states[probeLinearIndex];
    case 2u: return giProbeState2.states[probeLinearIndex];
    case 3u: return giProbeState3.states[probeLinearIndex];
    case 4u: return giProbeState4.states[probeLinearIndex];
    case 5u: return giProbeState5.states[probeLinearIndex];
    case 6u: return giProbeState6.states[probeLinearIndex];
    default: return giProbeState7.states[probeLinearIndex];
    }
}
#define GI_LOAD_PROBE_STATE(regionIndex, probeLinearIndex) LoadDeferredGIProbeState(regionIndex, probeLinearIndex)
#include "../GI/GIProbeCommon.glsl"

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outDiffuseExitantRadiance;

vec3 SampleDeferredProbeIrradianceForRegion(uint regionIndex, GIRegionParams region, vec3 worldPos, vec3 normal)
{
    ivec3 probeCounts = ivec3(region.gridDimensionsAndPriority.xyz);
    vec3 volumeMin = region.volumeMin.xyz;
    vec3 volumeSize = region.volumeSizeAndBias.xyz;

    switch (regionIndex)
    {
    case 0u:
        return GI_SampleProbeIrradianceAtlasScreenVisible(0u, probeCounts,
            giIrradianceAtlas[0], giVisibilityAtlas[0], worldPos, normal,
            volumeMin, volumeSize, region.volumeSizeAndBias.w, region.traceParams.z);
    case 1u:
        return GI_SampleProbeIrradianceAtlasScreenVisible(1u, probeCounts,
            giIrradianceAtlas[1], giVisibilityAtlas[1], worldPos, normal,
            volumeMin, volumeSize, region.volumeSizeAndBias.w, region.traceParams.z);
    case 2u:
        return GI_SampleProbeIrradianceAtlasScreenVisible(2u, probeCounts,
            giIrradianceAtlas[2], giVisibilityAtlas[2], worldPos, normal,
            volumeMin, volumeSize, region.volumeSizeAndBias.w, region.traceParams.z);
    case 3u:
        return GI_SampleProbeIrradianceAtlasScreenVisible(3u, probeCounts,
            giIrradianceAtlas[3], giVisibilityAtlas[3], worldPos, normal,
            volumeMin, volumeSize, region.volumeSizeAndBias.w, region.traceParams.z);
    case 4u:
        return GI_SampleProbeIrradianceAtlasScreenVisible(4u, probeCounts,
            giIrradianceAtlas[4], giVisibilityAtlas[4], worldPos, normal,
            volumeMin, volumeSize, region.volumeSizeAndBias.w, region.traceParams.z);
    case 5u:
        return GI_SampleProbeIrradianceAtlasScreenVisible(5u, probeCounts,
            giIrradianceAtlas[5], giVisibilityAtlas[5], worldPos, normal,
            volumeMin, volumeSize, region.volumeSizeAndBias.w, region.traceParams.z);
    case 6u:
        return GI_SampleProbeIrradianceAtlasScreenVisible(6u, probeCounts,
            giIrradianceAtlas[6], giVisibilityAtlas[6], worldPos, normal,
            volumeMin, volumeSize, region.volumeSizeAndBias.w, region.traceParams.z);
    default:
        return GI_SampleProbeIrradianceAtlasScreenVisible(7u, probeCounts,
            giIrradianceAtlas[7], giVisibilityAtlas[7], worldPos, normal,
            volumeMin, volumeSize, region.volumeSizeAndBias.w, region.traceParams.z);
    }
}

float SampleScreenSpaceShadow(vec2 uv)
{
    return clamp(texture(screenSpaceShadow, uv).r, 0.0, 1.0);
}

float DielectricF0FromIOR(float ior)
{
    float ratio = (ior - 1.0) / (ior + 1.0);
    return ratio * ratio;
}

vec3 SampleDeferredProbeIrradiance(vec3 worldPos, vec3 normal)
{
    vec3 probeIrradiance = vec3(0.0);
    float probeWeight = 0.0;
    float selectedPriority = -3.402823e38;
    const uint regionCount = min(uint(regionInfo.x), 8u);
    for (uint regionIndex = 0u; regionIndex < regionCount; ++regionIndex)
    {
        GIRegionParams region = regions[regionIndex];
        const float weight = GI_IsInsideVolume(worldPos, region.volumeMin.xyz, region.volumeSizeAndBias.xyz)
            ? GI_VolumeFade(worldPos, region.volumeMin.xyz, region.volumeSizeAndBias.xyz,
                max(region.traceParams.z, 0.0))
            : 0.0;
        const float priority = region.gridDimensionsAndPriority.w;
        if (weight > 0.0 && (priority > selectedPriority ||
            (priority == selectedPriority && weight > probeWeight)))
        {
            probeIrradiance = SampleDeferredProbeIrradianceForRegion(
                regionIndex, region, worldPos, normalize(normal));
            probeWeight = weight;
            selectedPriority = priority;
        }
    }
    return max(probeIrradiance * probeWeight, vec3(0.0));
}

vec3 ComputeDeferredGINormal(vec3 worldPos, vec3 shadingNormal)
{
    vec3 fallback = dot(shadingNormal, shadingNormal) > 1e-6
        ? normalize(shadingNormal)
        : vec3(0.0, 1.0, 0.0);
    vec3 dx = dFdx(worldPos);
    vec3 dy = dFdy(worldPos);
    vec3 geometric = cross(dx, dy);
    if (dot(geometric, geometric) <= 1e-8)
        return fallback;
    geometric = normalize(geometric);
    return dot(geometric, fallback) < 0.0 ? -geometric : geometric;
}

bool EvaluateSubsurfaceSourceAtUV(vec2 uv, int centerMaterialIndex,
                                  sampler2DArray directionalShadows,
                                  sampler2DArrayShadow punctualShadows,
                                  out vec3 diffuseSource,
                                  out vec3 samplePosition,
                                  out vec3 sampleNormal)
{
    vec4 sampleNormalData = texture(normalInput, uv);
    vec4 sampleGBuffer0 = texture(gbufferInput0, uv);
    vec4 sampleGBuffer1 = texture(gbufferInput1, uv);
    vec4 sampleGBuffer2 = texture(gbufferInput2, uv);

    int sampleMaterialID = int(round(sampleGBuffer1.z));
    int sampleMaterialIndex = int(round(sampleGBuffer1.w));
    if (sampleMaterialID != MATERIAL_ID_SUBSURFACE ||
        sampleMaterialIndex != centerMaterialIndex)
        return false;

    MaterialPayload sampleMaterial =
        materialDataBuffer.materials[sampleMaterialIndex];
    float sampleIOR = clamp(sampleMaterial.padding, 1.0, 2.5);

    ivec2 aoSize = imageSize(ssao);
    ivec2 aoCoord = clamp(ivec2(uv * vec2(aoSize)), ivec2(0), aoSize - 1);
    float sampleAO = min(sampleGBuffer1.y, imageLoad(ssao, aoCoord).r);

    BRDFData sampleBRDF;
    // Pre-and-post scatter texturing: half of the apparent albedo is applied
    // before diffusion and half after it. This prevents texture detail from
    // being blurred twice while retaining wavelength-dependent color bleed.
    sampleBRDF.albedo = sqrt(max(sampleGBuffer0.rgb, vec3(0.0)));
    sampleBRDF.normal = normalize(sampleNormalData.xyz);
    sampleBRDF.roughness = clamp(sampleGBuffer0.w, 0.045, 1.0);
    sampleBRDF.metallic = 0.0;
    sampleBRDF.ao = pow(clamp(sampleAO, 0.0, 1.0), 2.0);
    sampleBRDF.fresnel0 = vec3(DielectricF0FromIOR(sampleIOR));
    sampleBRDF.positionWS = sampleGBuffer2.xyz;
    sampleBRDF.viewDirection = normalize(cameraPosition.xyz - sampleBRDF.positionWS);
    sampleBRDF.indirectDiffuse = texture(ssgi, uv).rgb;
    sampleBRDF.indirectSpecular = vec4(0.0);

    LightResult sampleLighting;
    CalculateDirectLight(sampleBRDF, directionalShadows, sampleGBuffer2.w,
                         punctualShadows, SampleScreenSpaceShadow(uv),
                         sampleLighting);

    float NoV = max(dot(sampleBRDF.normal, sampleBRDF.viewDirection), 0.0);
    vec3 F = fresnelSchlickRoughness(
        NoV, sampleBRDF.fresnel0, sampleBRDF.roughness);
    vec3 ambientDiffuse = sampleBRDF.indirectDiffuse * sampleBRDF.ao *
                          (vec3(1.0) - F) * sampleBRDF.albedo;

    diffuseSource = sampleLighting.directDiffuse + ambientDiffuse;
    samplePosition = sampleBRDF.positionWS;
    sampleNormal = sampleBRDF.normal;
    return true;
}

vec3 EvaluateScreenSpaceBurleyDiffusion(
    BRDFData centerBRDF, SubsurfaceParams sss, int materialIndex,
    vec2 centerUV, vec3 localDiffuse,
    sampler2DArray directionalShadows, sampler2DArrayShadow punctualShadows)
{
    float amount = clamp(sss.subsurfaceAmount, 0.0, 1.0);
    vec3 distance = max(sss.scatteringDistance, vec3(5e-5));
    float maxDistance = max(max(distance.r, distance.g), distance.b);
    float viewDepth = max((ViewMatrix * vec4(centerBRDF.positionWS, 1.0)).z * -1.0, 1e-3);
    vec2 uvPerMetre = 0.5 * vec2(abs(ProjectionMatrix[0][0]),
                                 abs(ProjectionMatrix[1][1])) / viewDepth;
    // Burley normalized diffusion has a sharp peak and a long tail.  Testing
    // only the characteristic distance incorrectly disabled SSS even when the
    // full kernel covered several pixels.  Eight scattering distances contain
    // practically all of the profile energy and are used as the pixel-footprint
    // criterion, matching the diffuse-BRDF limit only when the entire kernel is
    // genuinely sub-pixel.
    const float burleySupportRadius = 8.0;
    float projectedSupportPixels = burleySupportRadius * maxDistance *
                                   uvPerMetre.y * ScreenParams.y;
    if (amount <= 0.0 || projectedSupportPixels < 0.5)
        return localDiffuse;

    vec3 shape = 1.0 / distance;
    float referenceShape = 1.0 / maxDistance;
    vec3 postScatterAlbedo = sqrt(max(centerBRDF.albedo, vec3(0.0)));
    vec3 fallbackSource = localDiffuse /
                          max(postScatterAlbedo, vec3(1e-3));

    const int sampleCount = 13;
    const float goldenAngle = 2.39996323;
    float rotation = TWO_PI * fract(sin(dot(gl_FragCoord.xy,
        vec2(12.9898, 78.233))) * 43758.5453);
    vec3 accumulated = vec3(0.0);
    vec3 accumulatedWeight = vec3(0.0);

    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        float u = (float(sampleIndex) + 0.5) / float(sampleCount);
        float radius = SampleBurleyRadius(u, maxDistance);
        float angle = rotation + goldenAngle * float(sampleIndex);
        vec2 direction = vec2(cos(angle), sin(angle));
        vec2 sampleUV = centerUV + direction * radius * uvPerMetre;

        vec3 source = fallbackSource;
        vec3 samplePosition = centerBRDF.positionWS;
        vec3 sampleNormal = centerBRDF.normal;
        bool insideViewport = all(greaterThanEqual(sampleUV, vec2(0.0))) &&
                              all(lessThanEqual(sampleUV, vec2(1.0)));
        bool valid = insideViewport && EvaluateSubsurfaceSourceAtUV(
            sampleUV, materialIndex, directionalShadows, punctualShadows,
            source, samplePosition, sampleNormal);

        // Screen-space diffusion must not cross silhouettes, disconnected
        // objects that share a material, or strongly folded backfaces.
        float surfaceSeparation = length(samplePosition - centerBRDF.positionWS);
        valid = valid && surfaceSeparation <= max(radius * 2.5, 0.001) &&
                dot(sampleNormal, centerBRDF.normal) > -0.25;
        if (!valid)
        {
            source = fallbackSource;
            surfaceSeparation = radius;
        }

        vec3 profileWeight = BurleyProfileImportanceWeight(
            radius, surfaceSeparation, shape, referenceShape);
        accumulated += source * profileWeight;
        accumulatedWeight += profileWeight;
    }

    // Normalize the finite deterministic sample set per channel. This makes
    // the constant-radiance response exactly energy preserving even with only
    // nine samples.
    vec3 diffused = postScatterAlbedo * accumulated /
                    max(accumulatedWeight, vec3(1e-5));
    return mix(localDiffuse, diffused, amount);
}

void main() 
{
    vec4 normalData = texture(normalInput, fragTexCoord);
    vec4 gbufferData0 = texture(gbufferInput0, fragTexCoord);
    vec4 gbufferData1 = texture(gbufferInput1, fragTexCoord);
    vec4 gbufferData2 = texture(gbufferInput2, fragTexCoord);
    vec4 depthData = texture(depthInput, fragTexCoord);

    vec3 normal = normalData.xyz;
    vec3 color = gbufferData0.xyz;
    float roughness = gbufferData0.w;
    float metallic = gbufferData1.x;
    float ao = gbufferData1.y;
    float materialID = gbufferData1.z;
    vec3 position_world = gbufferData2.xyz;
    float depth = depthData.x;
    float linearDepth = gbufferData2.w;

    // Dedicated DDGI diagnostic: output only the current pixel's direct probe
    // atlas sample. This intentionally bypasses SSGI, sky, direct lights and
    // every material/BRDF path so the volume transport can be inspected alone.
    if (deferredProbeDebug.x > 0.5)
    {
        vec3 probeIrradiance = SampleDeferredProbeIrradiance(
            position_world, ComputeDeferredGINormal(position_world, normal));
        vec3 debugColor = probeIrradiance * max(deferredProbeDebug.y, 0.001);
        if (any(isnan(debugColor)) || any(isinf(debugColor)))
            debugColor = vec3(1.0, 0.0, 1.0);
        outColor = vec4(max(debugColor, vec3(0.0)), 1.0);
        outDiffuseExitantRadiance = vec4(max(debugColor, vec3(0.0)), 1.0);
        return;
    }


    //获取ssao：这里先使用原始半分辨率结果的安全采样，避免深度加权上采样把 AO 错误压黑。
    ivec2 ssaoSize = imageSize(ssao);
    ivec2 ssaoCoord = clamp(ivec2(fragTexCoord * vec2(ssaoSize)), ivec2(0), ssaoSize - 1);
    float ssaoValue = imageLoad(ssao, ssaoCoord).r;

    vec3 viewDirection = normalize(cameraPosition.xyz - position_world);

    //材质属性
    BRDFData brdfData;
    brdfData.normal = normal;
    brdfData.albedo = color.rgb;
    brdfData.roughness = roughness;
    brdfData.metallic = metallic;
    brdfData.ao = min(ao, ssaoValue);
    brdfData.ao = pow(brdfData.ao, 2.0);
    brdfData.fresnel0 = vec3(0.04);
    brdfData.viewDirection = viewDirection;
    brdfData.positionWS = position_world;
    
    // indirect diffuse — SSGI temporal pass already accumulates and aligns to
    // current frame UV via motion vectors; sample at fragTexCoord.
    // GBuffer / SSR / Fog are produced from current-frame inputs after the RenderPass split.
    // and blurring edges that the temporal pass already correctly resolved.
    brdfData.indirectDiffuse = texture(ssgi, fragTexCoord).rgb;
    brdfData.indirectSpecular = imageLoad(ssr,ivec2(fragTexCoord * ScreenParams.xy)).rgba;
    
    //计算光照
    LightResult lightResult;
    lightResult.directDiffuse = vec3(0);
    lightResult.directSpecular = vec3(0);
    lightResult.ambientDiffuse = vec3(0);
    lightResult.ambientSpecular = vec3(0);

    float sssShadow = SampleScreenSpaceShadow(fragTexCoord);

    int matID = int(round(materialID));
    if (matID == MATERIAL_ID_SKIN)
    {
        // --- Skin BRDF path ---
        // Curvature was stored in normalInput.w by UnlitSkin.frag
        float curvature = normalData.w;
        int skinMaterialIndex = int(round(gbufferData1.w));
        SkinMaterialParams skin = DecodeSkinMaterialParams(skinMaterialIndex);
        skin.scatterMask = clamp(gbufferData1.x, 0.0, 1.0);
        skin.cavity = clamp(gbufferData1.y, 0.0, 1.0);
        skin.authoredThinness = UnpackSkinThinnessFromMaterialID(
            materialID,
            float(MATERIAL_ID_SKIN));
        brdfData.fresnel0 = ComputeSkinF0(brdfData.albedo, skin.ior);
        brdfData.metallic = 0.0;
        brdfData.roughness = clamp(brdfData.roughness, 0.045, 1.0);
        CalculateDirectLight_Skin(brdfData, curvature, skin, cascadeShadowMap, linearDepth, punctualShadowMap, sssShadow, lightResult);
        AmbientBRDF_Skin(brdfData, skin, viewDirection, lightResult.ambientDiffuse, lightResult.ambientSpecular);

        vec3 skinDebugColor = vec3(0.0);
        if (TrySkinDebugView(brdfData, skin, curvature, lightResult, skinDebugColor))
        {
            outDiffuseExitantRadiance = vec4(
                max(lightResult.directDiffuse + lightResult.ambientDiffuse, vec3(0.0)), 1.0);
            outColor = vec4(skinDebugColor, 1.0);
            return;
        }
    }
    else if (matID == MATERIAL_ID_CLOTH)
    {
        // --- Cloth BRDF path ---
        // Cloth.frag stores the global material index in GBuffer1.w and the
        // tangent angle in normal.w. The extension payload uses the same index
        // as the existing PBR payload, so RenderNode binding remains unchanged.
        int clothMaterialIndex = int(round(gbufferData1.w));
        ClothMaterialPayload cloth = GetClothMaterialPayload(clothMaterialIndex);
        vec3 clothTangent;
        vec3 clothBitangent;
        DecodeClothTangentFrame(normal, normalData.w, clothTangent, clothBitangent);
        brdfData.ao = clamp(min(ao, ssaoValue), 0.0, 1.0);
        CalculateDirectLight_Cloth(brdfData, cloth, clothTangent,
                                   cascadeShadowMap, linearDepth, punctualShadowMap,
                                   sssShadow, lightResult);
        AmbientBRDF_Cloth(brdfData, cloth, clothTangent, viewDirection,
                          lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }
    else if (matID == MATERIAL_ID_SUBSURFACE)
    {
        // --- Opaque dielectric BSSRDF path (not skin and not glass) ---
        int mi = int(round(gbufferData1.w));
        MaterialPayload mat = materialDataBuffer.materials[mi];

        SubsurfaceParams sss;
        // Material distances and thickness are authored in millimetres and
        // converted once here to the engine's metre world units.
        sss.scatteringDistance = max(mat.albedo.rgb, vec3(0.005)) *
                                 max(mat.roughness, 0.01) * 0.001;
        sss.thickness = max(normalData.w, 0.0) * 0.001;
        sss.subsurfaceAmount = clamp(gbufferData1.x, 0.0, 1.0);

        // SSS is a dielectric. GBuffer1.x carries the SSS mask, not metalness.
        brdfData.metallic = 0.0;
        float ior = clamp(mat.padding, 1.0, 2.5);
        brdfData.fresnel0 = vec3(DielectricF0FromIOR(ior));

        CalculateDirectLight(brdfData, cascadeShadowMap, linearDepth,
                             punctualShadowMap, sssShadow, lightResult);
        AmbientBRDF(brdfData, viewDirection,
                    lightResult.ambientDiffuse, lightResult.ambientSpecular);

        vec3 localDiffuse = lightResult.directDiffuse +
                            lightResult.ambientDiffuse;
        vec3 diffused = EvaluateScreenSpaceBurleyDiffusion(
            brdfData, sss, mi, fragTexCoord, localDiffuse,
            cascadeShadowMap, punctualShadowMap);

        vec3 transmission = vec3(0.0);
        CalculateDirectTransmission_Subsurface(
            brdfData, sss, cascadeShadowMap, linearDepth,
            punctualShadowMap, sssShadow, transmission);

        lightResult.directDiffuse = diffused + transmission;
        lightResult.ambientDiffuse = vec3(0.0);
    }
    else if (matID == MATERIAL_ID_GRASS)
    {
        // --- Vegetation / Grass BRDF path ---
        // Translucency was stored in normalInput.w by Grass.frag
        float translucency = normalData.w;

        // Grass AO — match the default PBR path's aggressive power curve
        brdfData.ao = pow(min(ao, ssaoValue), 2.0);

        VegetationParams veg;
        veg.subsurfaceColor = brdfData.albedo * vec3(0.55, 0.85, 0.25) * clamp(translucency, 0.0, 1.0);
        veg.opacity = 1.0;
        veg.wrap = 0.5;
        veg.scatterRoughness = 0.6;
        veg.transmissionScale = 1.0;
        veg.specularScale = 0.30;

        CalculateDirectLight_Vegetation(brdfData, veg, cascadeShadowMap, linearDepth, punctualShadowMap, sssShadow, lightResult);
        AmbientBRDF_Vegetation(brdfData, veg, viewDirection,
                               lightResult.ambientDiffuse, lightResult.ambientSpecular);
        lightResult.ambientSpecular = vec3(0.0); // grass blades: no ambient specular
    }
    else if (matID == MATERIAL_ID_TREE)
    {
        float leafTranslucency = clamp(normalData.w, 0.0, 1.0);
        int treeMaterialIndex = int(round(gbufferData1.w));
        if (leafTranslucency > 0.0)
        {
            // Thin leaf pixels use UE-style two-sided foliage transmission.
            // This affects the current pixel lighting only; leaves are not injected into probe SH.
            brdfData.ao = clamp(min(ao, ssaoValue), 0.0, 1.0);
            TreeLeafMaterialPayload leafPayload = GetTreeLeafMaterialPayload(treeMaterialIndex);
            vec4 leafSubsurface = max(leafPayload.subsurfaceColorAndStrength, vec4(0.0));
            vec4 leafScattering = leafPayload.scattering;

            VegetationParams veg;
            veg.subsurfaceColor = brdfData.albedo * leafSubsurface.rgb * leafTranslucency;
            veg.opacity = 1.0;
            veg.wrap = clamp(leafScattering.x, 0.0, 0.9);
            veg.scatterRoughness = max(leafScattering.y, 0.05);
            veg.transmissionScale = 1.0;
            veg.specularScale = max(leafScattering.z, 0.0);

            CalculateDirectLight_Vegetation(brdfData, veg, cascadeShadowMap, linearDepth, punctualShadowMap, sssShadow, lightResult);
            AmbientBRDF_Vegetation(brdfData, veg, viewDirection,
                                   lightResult.ambientDiffuse, lightResult.ambientSpecular);
            lightResult.ambientSpecular *= 0.35;
        }
        else
        {
            // 树干/枝干仍按标准 PBR 接收 GI。
            CalculateDirectLight(brdfData, cascadeShadowMap, linearDepth, punctualShadowMap, sssShadow, lightResult);
            AmbientBRDF(brdfData, viewDirection, lightResult.ambientDiffuse, lightResult.ambientSpecular);
        }
    }
    else if (matID == MATERIAL_ID_EMISSIVE)
    {
        // --- Emissive 直通路径 ---
        // GBuffer0.w (roughness 插槽) 存储了发光强度，直接输出 albedo × intensity
        // 跳过全部 BRDF / 直接光照 / 阴影 / 环境光计算
        float emissiveIntensity   = roughness;             // GBuffer0.w = intensity
        lightResult.directDiffuse = color.rgb * emissiveIntensity;
        // directSpecular / ambientDiffuse / ambientSpecular 保持 vec3(0)
    }
    else
    {
        // --- Default PBR path ---
        CalculateDirectLight(brdfData, cascadeShadowMap, linearDepth, punctualShadowMap, sssShadow, lightResult);
        AmbientBRDF(brdfData, viewDirection, lightResult.ambientDiffuse, lightResult.ambientSpecular);
    }

    // Keep the temporal SSGI source physically scoped: diffuse exitant
    // radiance after direct + probe/ambient diffuse, but before specular,
    // fog and every later composition pass.
    outDiffuseExitantRadiance = vec4(
        max(lightResult.directDiffuse + lightResult.ambientDiffuse, vec3(0.0)), 1.0);

    outColor.rgb = lightResult.directDiffuse + lightResult.directSpecular;
    outColor.rgb += lightResult.ambientDiffuse + lightResult.ambientSpecular;
    if (reflectionProbeDebugView != 0u)
    {
        vec3 reflectionDir = reflect(-viewDirection, normal);
        ReflectionProbeSample debugProbe = SampleReflectionProbes(position_world, normal, reflectionDir, roughness);
        if (reflectionProbeDebugView == 1u)
        {
            float id = float(max(debugProbe.topIndex, 0));
            vec3 idColor = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + id * 2.3999632);
            outColor = vec4(idColor * clamp(debugProbe.topWeight, 0.0, 1.0), 1.0);
        }
        else if (reflectionProbeDebugView == 2u) outColor = vec4(debugProbe.specular, 1.0);
        else if (reflectionProbeDebugView == 3u) outColor = vec4(vec3(brdfData.indirectSpecular.a), 1.0);
        else if (reflectionProbeDebugView == 4u)
        {
            uint region = debugProbe.topIndex >= 0 ? reflectionProbes[debugProbe.topIndex].regionAndFlags.x : 0xffffffffu;
            vec3 regionColor = fract(vec3(0.1031, 0.11369, 0.13787) * float(region + 1u));
            outColor = vec4(regionColor, 1.0);
        }
        else if (reflectionProbeDebugView == 5u) outColor = vec4(abs(debugProbe.parallaxDelta), 1.0);
        else if (reflectionProbeDebugView == 6u) outColor = vec4(debugProbe.specular, 1.0);
        else if (reflectionProbeDebugView == 7u) outColor = vec4(brdfData.indirectSpecular.rgb, 1.0);
        return;
    }
    //outColor.rgb = lightResult.ambientSpecular;
    //混合雾效  fogResult: rgb = in-scatter, a = opacity (1 - transmittance)
    // fogResult 由当前帧 GBuffer / 体积雾流程生成，Deferred 合成时直接按当前 UV 采样。
    vec4 fogData = texture(fogResult, fragTexCoord);
    float fogOpacity = fogData.a;
    outColor.rgb = outColor.rgb * (1.0 - fogOpacity) + fogData.rgb;
    //outColor.rgb = vec3(brdfData.ao,brdfData.ao,brdfData.ao);
    //outColor.rgb = lightResult.ambientDiffuse;
    outColor.a = 1;
}
