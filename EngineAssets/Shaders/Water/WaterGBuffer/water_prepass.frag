#version 450
#extension GL_GOOGLE_include_directive : require

// ============================================================
// water_prepass.frag — 水面 GBuffer Pass 片元着色器
//
// 输出（对应 SetupVansWaterGBufferPass 的 Attachment 顺序）：
//   location 0 → WaterGBuf_Normal（RGBA16F）：世界空间法线 XYZ，A 留用
//   location 1 → WaterGBuf_WorldPosDepth（RGBA16F）：RGB=世界空间位置，A=视空间线性深度
//
// W-08: 法线贴图混合 — 从 VansWaterMaterial 法线纹理采样并与 Gerstner/FFT 法线混合
// ============================================================

#include "../../Common/CameraData.glsl"
#include "../water_common.glsl"

layout(location = 0) in  vec3  inWorldPos;
layout(location = 1) in  float inLinearDepth;
layout(location = 2) in  vec3  inWorldNormal;
layout(location = 3) flat in int inLodLevel;

// ── Set 1：Water GBuffer Pass 输入 ───────────────────────────
// binding 0-2 由 vertex shader 使用，frag 只使用 binding 0 的参数和 binding 3 的法线贴图
layout(set = 1, binding = 0) uniform WaterGBufferParams
{
    mat4  waterVPMatrix;
    mat4  waterViewMatrix;
    vec4  waterCameraPosition;
    float minLodDist;
    int   lodLevels;
    int   meshDim;
    float clipmapBaseScale; // LOD 0 Clipmap 世界覆盖边长（m），默认 128
    float maxWaveAmp;
    float detailBalance;     // LOD 缩放因子（默认 2.0）
    float morphStartRatio;  // morph zone 起点比例 [0, 1)，默认 0.6
    float pad1;
    vec4  waveTimeAndScale;    // x=time, y=ampScale, z=chopScale, w=normalIntensity
    vec4  pad3[8];
} waterParams;

// N-01: Detail Normal Texture2DArray（compute 生成，替代旧 sampler2D）
layout(set = 1, binding = 3) uniform sampler2DArray waterDetailNormalArray;

// Attachment 0: WaterGBuf_Normal（RGBA16F, 世界空间法线 XYZ, A 留用）
layout(location = 0) out vec4 outWaterNormal;
// Attachment 1: WaterGBuf_WorldPosDepth（RGBA16F, RGB=世界空间位置, A=视空间线性深度）
layout(location = 1) out vec4 outWaterPosDepth;

// ── N-01: 世界空间直接平铺采样切线空间 detail normal ──
vec3 SampleDetailNormalTS(vec2 worldXZ)
{
    // 世界空间直接平铺：每 detailWorldCoverage 米重复一次
    float detailWorldCoverage = waterParams.pad3[1].z;
    if (detailWorldCoverage <= 0.0) detailWorldCoverage = 32.0;
    vec2 uv = worldXZ / detailWorldCoverage;  // fract 由 CLAMP_TO_EDGE 替代…
    // 实际上用 fract 实现重复平铺
    uv = fract(uv);

    // 始终采样 layer 0（单层纹理）
    vec3 packed = textureLod(waterDetailNormalArray, vec3(uv, 0.0), 0.0).xyz;
    return packed * 2.0 - 1.0;  // [0,1] → [-1,1] 切线空间
}

// ── 从宏法线构建 TBN，将切线空间 detail normal 变换到世界空间 ──
vec3 TransformDetailToWorld(vec3 detailTS, vec3 macroNormalWS)
{
    // 切线空间：Y 轴 = 水面法线方向（水平面时为世界 Y-up）
    // T = 垂直于宏法线且在 XZ 平面附近，B = cross(N, T)
    vec3 N = normalize(macroNormalWS);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 T = cross(up, N);
    float Tlen = length(T);
    if (Tlen < 0.0001)
        T = vec3(1.0, 0.0, 0.0);   // 宏法线接近垂直，任意水平方向均可
    else
        T /= Tlen;
    vec3 B = cross(N, T);           // 右手系：N × B → T, B × T → N

    // detailTS: x=T方向, y=N方向(surface normal), z=B方向
    return normalize(T * detailTS.x + N * detailTS.y + B * detailTS.z);
}

// ── N-01: LOD 衰减 ────────────────────────────────────────
float GetDetailNormalLodFade(int lodLevel)
{
    // LOD 0-2: 全强度, LOD 3-5: 渐变衰减, LOD 6+: 零
    return 1.0 - smoothstep(2.5, 5.5, float(lodLevel));
}

void main()
{
    const bool DEBUG_VISUALIZE_LOD = false;

    if (DEBUG_VISUALIZE_LOD)
    {
        const vec3 lodColors[10] = vec3[10](
            vec3(1.0, 0.0, 0.0),  // LOD0 红
            vec3(1.0, 0.5, 0.0),  // LOD1 橙
            vec3(1.0, 1.0, 0.0),  // LOD2 黄
            vec3(0.0, 1.0, 0.0),  // LOD3 绿
            vec3(0.0, 1.0, 1.0),  // LOD4 青
            vec3(0.0, 0.3, 1.0),  // LOD5 蓝
            vec3(0.5, 0.0, 1.0),  // LOD6 紫
            vec3(1.0, 0.0, 1.0),  // LOD7 品红
            vec3(1.0, 1.0, 1.0),  // LOD8 白
            vec3(0.4, 0.4, 0.4)   // LOD9 灰
        );
        int lodIndex = clamp(inLodLevel, 0, 9);
        outWaterNormal   = vec4(lodColors[lodIndex], 1.0);
        outWaterPosDepth = vec4(inWorldPos, inLinearDepth);
        return;
    }

    vec3 worldNormal = normalize(inWorldNormal);

    // ── N-01: 细节法线混合 ──────────────────────────────────
    // detailIntensity 控制切线空间 detail normal 的 XZ 扰动强度
    // intensity=0 → detailTS.xz=0 → normalize → (0,1,0) → 宏法线不变
    float detailIntensity = waterParams.pad3[0].x;
    vec3 detailTS = SampleDetailNormalTS(inWorldPos.xz);
    detailTS.xz *= detailIntensity;
    detailTS = normalize(detailTS);
    vec3 detailWS = TransformDetailToWorld(detailTS, worldNormal);
    worldNormal = normalize(worldNormal + detailWS);

    // 写入世界空间法线（RGB = XYZ, Y 朝上）
    outWaterNormal   = vec4(worldNormal, 0.0);

    // 世界空间位置 + 视空间线性深度
    outWaterPosDepth = vec4(inWorldPos, inLinearDepth);
}
