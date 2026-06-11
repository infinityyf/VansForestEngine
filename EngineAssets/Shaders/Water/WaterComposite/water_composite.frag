#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../water_common.glsl"

// ============================================================
// water_composite.frag — 水面 Composite Pass（全屏合成）
//
// BRDF 方案：
//   直接光  → Blinn-Phong 高光
//   环境光  → Split-Sum BRDF LUT 近似
//   合成    → kS × Reflection + kD × Refraction
//
// 依赖全局 Set 0（已由 C++ 侧绑定）：
//   binding 3  → BRDFLUT（RG 双通道，BRDF Integration Map）
//   binding 5  → PreConvSpecularEnvironment（预卷积反射 Cubemap）
// ============================================================

layout(location = 0) in  vec2 inUV;
layout(location = 0) out vec4 outColor;

// ── Set 0：全局 PBR 资源（与 BRDFData.glsl 的 PBRLutSetBind=0 一致）──
layout(set = 0, binding = 3) uniform sampler2D   BRDFLUT;
layout(set = 0, binding = 5) uniform samplerCube PreConvSpecularEnvironment;

// ── Set 1：Water Composite Pass 自有资源 ─────────────────────
layout(set = 1, binding = 0) uniform sampler2D waterGBufNormal;
layout(set = 1, binding = 1) uniform sampler2D waterGBufPosDepth;   // RGBA16F: RGB=worldPos, A=linearDepth
layout(set = 1, binding = 3) uniform sampler2D sceneGBuf2;
layout(set = 1, binding = 4) uniform sampler2D waterReflection;
layout(set = 1, binding = 5) uniform sampler2D waterRefraction;
layout(set = 1, binding = 6) uniform sampler2D waterCaustics;
layout(set = 1, binding = 7) uniform sampler2D waterFoamTexture;
layout(set = 1, binding = 9) uniform sampler2D waterSSSScatter;  // W-16 Phase 2: SSS 散射输出

layout(set = 1, binding = 2) uniform WaterCompositeParams
{
    vec4  deepWaterColor;       // offset  0
    vec4  shallowWaterColor;    // offset 16
    float fresnelPower;         // offset 32
    float waterLevel;           // offset 36
    float specularIntensity;    // offset 40
    float foamIntensity;        // offset 44
    vec4  absorptionCoeff;      // offset 48
    vec4  scatteringCoeff;      // offset 64
    float sssAnisotropy;        // offset 80
    float waterRoughness;       // offset 84
    float waterIOR;             // offset 88 — Inspector optimization: IOR → dynamic F0
    float padComp1;             // offset 92
    vec4  cameraPosition;       // offset 96
    mat4  invViewProjMatrix;    // offset 112
    vec4  mainLightDir;         // offset 176
} p;

// ============================================================
// PBR 微面元函数（精简自 BRDFData.glsl）
// ============================================================

// 标准 Schlick Fresnel（用于直接光）
vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 粗糙度修正 Schlick Fresnel（用于环境光 Split-Sum）
vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 粗糙度 → 预卷积 Cubemap mip level
float GetMipLevelFromRoughness(float roughness)
{
    return roughness * 9.0;
}

// Blinn-Phong NDF（归一化）
float BlinnPhongNDF(float NdotH, float roughness)
{
    float alpha   = roughness * roughness;
    float specPower = 2.0 / max(alpha, 0.0001) - 2.0;
    return (specPower + 2.0) / (2.0 * PI) * pow(max(NdotH, 0.0), specPower);
}

// 简化 Smith 几何遮蔽函数
float GeometrySmithSimple(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float G1V = NdotV / (NdotV * (1.0 - k) + k);
    float G1L = NdotL / (NdotL * (1.0 - k) + k);
    return G1V * G1L;
}

// ============================================================
// 直接光 Blinn-Phong 高光
// ============================================================
vec3 EvaluateDirectLight(vec3 N, vec3 V, vec3 L, vec3 lightColor, vec3 F0)
{
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    if (NdotL <= 0.0 || NdotV <= 0.0)
        return vec3(0.0);

    vec3  H     = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    // Fresnel
    vec3 F = FresnelSchlick(VdotH, F0);

    // Blinn-Phong NDF
    float D = BlinnPhongNDF(NdotH, p.waterRoughness);

    // Smith 几何遮蔽
    float G = GeometrySmithSimple(NdotV, NdotL, p.waterRoughness);

    // Cook-Torrance: f = D*G*F/(4*NdotV*NdotL), L_o = f * L_i * NdotL = D*G*F*L_i/(4*NdotV)
    float spec = D * G / (4.0 * max(NdotV, 0.01));

    return spec * F * lightColor * p.specularIntensity;
}

// ============================================================
// 环境光 Split-Sum BRDF（参考 BRDFData.glsl:214 AmbientBRDF）
// ============================================================
void EvaluateEnvironmentBRDF(vec3 N, vec3 V, vec3 F0,
                             vec4 reflectionTex,      // RGBA: RGB=SSR颜色, A=命中标志
                             vec3 refractionColor,
                             out vec3 outReflectionContrib,
                             out vec3 outRefractionContrib)
{
    float NdotV = max(dot(N, V), 0.0);

    // ── 1. kS = F (Fresnel), kD = 1 - F（与 AmbientBRDF 完全一致）──
    vec3 F = FresnelSchlickRoughness(NdotV, F0, p.waterRoughness);
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;  // 水面 metallic=0, 无需 kD *= (1-metallic)

    // 折射贡献 = kD * refractionColor
    outRefractionContrib = kD * refractionColor;

    // ── 2. BRDF Integration LUT（与 AmbientBRDF 一致）───────────
    vec2 lutUV = vec2(NdotV, p.waterRoughness);
    lutUV.y = 1.0 - lutUV.y;
    vec2 envBRDF = texture(BRDFLUT, lutUV).rg;

    // ── 3. 预卷积环境反射 + SSR 混合（与 AmbientBRDF 一致）─────
    vec3 R = reflect(-V, N);
    float lod = GetMipLevelFromRoughness(p.waterRoughness);
    vec3 prefilteredColor = textureLod(PreConvSpecularEnvironment, R, lod).rgb;

    // SSR hit → mix(ibl, ssr, hitFlag): hit→SSR, miss→IBL
    float ssrHit = reflectionTex.a;
    vec3 reflectionColor = reflectionTex.rgb;
    prefilteredColor = mix(prefilteredColor, reflectionColor, ssrHit);

    // ── 4. specular = prefilteredColor * (F * LUT.r + LUT.g) ───
    outReflectionContrib = prefilteredColor * (F * envBRDF.x + envBRDF.y);
}

// ============================================================
// Schlick Phase Function — W-16 Phase 2
//
// p(cosθ, g) = (1-g²) / [4π · (1+g²-2g·cosθ)^1.5]
//
// 比 Henyey-Greenstein 更高效：无 acos，仅 1 次 sqrt
// ============================================================
float SchlickPhase(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / max(4.0 * PI * denom * sqrt(denom), 1e-6);
}

// ============================================================
void main()
{
    vec2 suv = clamp(vec2(inUV.x, 1.0 - inUV.y), 0.001, 0.999);

    // ── 读取 Water GBuffer ─────────────────────────────────
    vec4 posDepth = texture(waterGBufPosDepth, suv);
    float wd = posDepth.a;
    if (wd >= 9999.0) discard;

    // 场景遮挡测试
    float sd = texture(sceneGBuf2, suv).w;
    if (sd > 0.1 && sd < wd + 0.05) discard;

    // 世界空间法线和位置（直接从 GBuffer 读取）
    vec3 N = normalize(texture(waterGBufNormal, suv).rgb);
    vec3 W = posDepth.rgb;

    // ── 读取预计算纹理 ─────────────────────────────────────
    vec3 refraction = texture(waterRefraction, suv).rgb;
    vec4 reflection = texture(waterReflection, suv);  // RGBA: RGB=SSR, A=hit flag
    // 焦散叠加到场景颜色
    vec3 caustics = texture(waterCaustics, suv).rgb;
    // float foam    = texture(waterFoamTexture, suv).r;

    // ── 水面材质参数 ───────────────────────────────────────
    // Inspector optimization: F0 = ((IOR-1)/(IOR+1))^2 动态计算
    float iorF0 = ((p.waterIOR - 1.0) / (p.waterIOR + 1.0));
    iorF0 = iorF0 * iorF0;
    vec3 WATER_F0 = vec3(iorF0);

    // ── 光照向量 ──────────────────────────────────────────
    vec3 V = normalize(p.cameraPosition.xyz - W);
    vec3 L = normalize(p.mainLightDir.xyz);

    // ── 1. 直接光（主方向光 Blinn-Phong）─────────────────
    vec3 directSpecular = EvaluateDirectLight(N, V, L, vec3(1.0), WATER_F0);

    // ── 2. 环境光（Split-Sum BRDF LUT）────────────────────
    vec3 envReflContrib, envRefrContrib;
    EvaluateEnvironmentBRDF(N, V, WATER_F0,
        reflection,       // vec4: RGB=SSR result, A=hit flag
        refraction,
        envReflContrib, envRefrContrib);

    // ── 3. 合成 ──────────────────────────────────────────
    vec3 color  = envReflContrib + envRefrContrib;
    color      += directSpecular;
    color += caustics;

    // ── 4. SSS 次表面散射（W-16 Phase 2）────────────────────
    vec3 sssTexture = texture(waterSSSScatter, suv).rgb;
    float VdotL_sss = dot(V, L);
    float g_sss     = p.sssAnisotropy;
    float phase     = SchlickPhase(VdotL_sss, g_sss);
    float NdotL_sss = max(dot(N, L), 0.0);
    vec3  sssContrib = sssTexture * phase * NdotL_sss;
    color += sssContrib;
    // color = mix(color, vec3(1.0), foam * p.foamIntensity * 0.5);

    outColor = vec4(color, 1.0);
}
