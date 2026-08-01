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

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

float SampleScreenSpaceShadow(vec2 uv)
{
    return clamp(texture(screenSpaceShadow, uv).r, 0.0, 1.0);
}

float DielectricF0FromIOR(float ior)
{
    float ratio = (ior - 1.0) / (ior + 1.0);
    return ratio * ratio;
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
        brdfData.fresnel0 = ComputeSkinF0(brdfData.albedo);
        brdfData.metallic = 0.0;
        brdfData.roughness = clamp(brdfData.roughness, 0.045, 1.0);
        CalculateDirectLight_Skin(brdfData, curvature, skin, cascadeShadowMap, linearDepth, punctualShadowMap, sssShadow, lightResult);
        AmbientBRDF_Skin(brdfData, skin, viewDirection, lightResult.ambientDiffuse, lightResult.ambientSpecular);
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
        veg.translucency   = translucency;
        veg.scatterWidth   = 0.55;    // 草片法线变化密，wrap 稍宽可减少暗面硬切
        veg.sssDistortion  = 0.25;
        veg.sssAmbient     = 0.06;
        veg.sssPower        = 9.0;    // 草叶更细，透射锥比树叶略宽

        CalculateDirectLight_Vegetation(brdfData, veg, cascadeShadowMap, linearDepth, punctualShadowMap, sssShadow, lightResult);
        AmbientBRDF_Vegetation(brdfData, viewDirection,
                               lightResult.ambientDiffuse, lightResult.ambientSpecular);
        lightResult.ambientSpecular = vec3(0.0); // grass blades: no ambient specular
    }
    else if (matID == MATERIAL_ID_TREE)
    {
        float leafTranslucency = clamp(normalData.w, 0.0, 1.0);
        if (leafTranslucency > 0.0)
        {
            // 树叶是薄片材质：使用 vegetation wrap diffuse 减少背光硬黑。
            // 它只改变当前像素如何接收 direct/SSGI/probe GI，不把树加入 probe SH 更新源。
            brdfData.ao = clamp(min(ao, ssaoValue), 0.0, 1.0);

            VegetationParams veg;
            veg.translucency   = leafTranslucency;
            veg.scatterWidth   = 0.5;
            veg.sssDistortion  = 0.25;
            veg.sssAmbient     = 0.06;
            veg.sssPower       = 12.0; // 树叶保留更集中的逆光透射高亮

            CalculateDirectLight_Vegetation(brdfData, veg, cascadeShadowMap, linearDepth, punctualShadowMap, sssShadow, lightResult);
            AmbientBRDF_Vegetation(brdfData, viewDirection,
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
