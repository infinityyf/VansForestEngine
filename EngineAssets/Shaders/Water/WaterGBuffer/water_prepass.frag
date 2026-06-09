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
    float oceanBaseScale;
    float maxWaveAmp;
    float pad0;
    float pad1;
    float pad2;
    vec4  waveTimeAndScale;
    vec4  pad3[8];
} waterParams;

// W-08: 法线贴图（可选，由 CPU 端绑定）
layout(set = 1, binding = 3) uniform sampler2D waterNormalMap;

// Attachment 0: WaterGBuf_Normal（RGBA16F, 世界空间法线 XYZ, A 留用）
layout(location = 0) out vec4 outWaterNormal;
// Attachment 1: WaterGBuf_WorldPosDepth（RGBA16F, RGB=世界空间位置, A=视空间线性深度）
layout(location = 1) out vec4 outWaterPosDepth;

// ── 工具：从世界 XZ 采样法线贴图 ────────────────────────────
vec3 SampleDetailNormal(vec2 worldXZ)
{
    // 法线贴图 UV：基于世界坐标平铺
    float waveSpeed = 0.03;  // W-08: UV 流动速率（可改为 UBO 参数）
    vec2 uv = worldXZ * vec2(0.1)  // 平铺缩放
            + vec2(0.0, waterParams.waveTimeAndScale.x * waveSpeed);  // 时间流动

    vec3 texNormal = texture(waterNormalMap, uv).xyz * 2.0 - 1.0;
    // 切线空间法线 → 世界空间（假设世界 XZ 平面切线）
    return normalize(vec3(texNormal.x * 0.5, texNormal.z, texNormal.y * 0.5));
}

void main()
{
    const bool DEBUG_VISUALIZE_LOD = true;

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

    // 直接写入世界空间法线（RGB = XYZ, Y 朝上）
    outWaterNormal   = vec4(worldNormal, 0.0);

    // 世界空间位置 + 视空间线性深度
    outWaterPosDepth = vec4(inWorldPos, inLinearDepth);
}
