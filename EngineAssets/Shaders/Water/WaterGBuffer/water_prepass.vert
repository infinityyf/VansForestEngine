#version 450
#extension GL_GOOGLE_include_directive : require

// ============================================================
// water_prepass.vert — 水面 GBuffer Pass 顶点着色器
//
// 设计文档 §3.5.3 CDLOD 顶点 Morph
//
// W-01: sampler2DArray — Clipmap Texture2DArray (256²×10 RGBA16F)
// W-04: SSBO 波分量（vertex 阶段可选读取，主要用于采样位移贴图）
// W-05: M=65 网格支持（通过 waterParams.meshDim UBO 参数）
//
// 顶点输入：网格空间位置 [0, 1]²（统一 M×M 网格模板）
// 输出：世界坐标位置 + 视空间线性深度（供 frag 写入 WaterGBuf）
// ============================================================

// ── 顶点输入（8B 网格空间位置）────────────────────────────────
layout(location = 0) in vec2 inMeshPos;   // [0, 1]² 网格空间 XZ

// ── Push Constants（每 Patch 不同，20B）──────────────────────
layout(push_constant) uniform WaterPatchPC
{
    vec2  patchWorldOrigin;   // Patch 左下角世界坐标 (x, z)
    float patchWorldSize;     // Patch 世界空间边长
    int   lodLevel;           // LOD 级别 [0, MAX_LOD_COUNT)
    float waterLevel;         // 水面 Y 高度（世界空间）
} pc;

// ── Set 1：Water GBuffer Pass 参数 ────────────────────────────
layout(set = 1, binding = 0) uniform WaterGBufferParams
{
    mat4  waterVPMatrix;       // 水面 pass 专用 VP
    mat4  waterViewMatrix;     // 水面 pass 专用 View
    vec4  waterCameraPosition; // 水面 pass 专用相机位置
    float minLodDist;
    int   lodLevels;
    int   meshDim;
    float clipmapBaseScale; // LOD 0 Clipmap 世界覆盖边长（m），默认 128
    float maxWaveAmp;
    float detailBalance;     // LOD 缩放因子（默认 2.0）
    float morphStartRatio;  // morph zone 起点比例 [0, 1)，默认 0.6
    float pad1;
    vec4  waveTimeAndScale;    // x=time, y=ampScale, z=chopScale, w=normalIntensity
    vec4  pad3[8];             // W-04: 预留填充
} waterParams;

// W-01: Texture2DArray 位移贴图
layout(set = 1, binding = 1) uniform sampler2DArray waterDisplacementMap;

// W-04: 波形 SSBO（vertex 阶段可选，用于直接计算法线）
layout(set = 1, binding = 2, std430) readonly buffer WaveSSBO
{
    // GerstnerWaveGPU 结构（32B 对齐）— CPU 端定义
    float waveData[];  // stride=8 floats per wave
} waveBuffer;

// ── CDLOD LOD 距离环 ────────────────────────────────────────
float LodRange(int level)
{
    return waterParams.minLodDist * pow(max(waterParams.detailBalance, 1.0), float(level));
}

// ── W-01: WorldToClipmapUV — 将世界 XZ 映射到指定 LOD 的 Clipmap UV ──
// Clipmap 以相机位置为中心（无 Snap），与 compute shader 生成坐标系一致。
vec2 WorldToClipmapUV(vec2 worldXZ, float lodScale)
{
    vec2 camXZ = waterParams.waterCameraPosition.xz;
    vec2 relative = worldXZ - camXZ;
    return (relative / lodScale) + 0.5;
}

// ── 采样位移贴图 ────────────────────────────────────────────
vec4 SampleDisplacement(vec2 worldXZ, float lodScale, int lodIdx)
{
    vec2 uv = WorldToClipmapUV(worldXZ, lodScale);
    // sampler2DArray: 第三个分量 = layer index ∈ [0, layerCount)
    return textureLod(waterDisplacementMap, vec3(uv, float(lodIdx)), 0.0);
}

// ── 采样水面法线（通过差分计算梯度）─────────────────────────
vec3 SampleWaterNormal(vec2 worldXZ, float lodScale, int lodIdx)
{
    vec2 texel = 1.0 / vec2(textureSize(waterDisplacementMap, 0).xy);
    float worldStep = max(lodScale, 1.0) * texel.x;
    float hL = textureLod(waterDisplacementMap, vec3(WorldToClipmapUV(worldXZ - vec2(worldStep, 0.0), lodScale), float(lodIdx)), 0.0).y;
    float hR = textureLod(waterDisplacementMap, vec3(WorldToClipmapUV(worldXZ + vec2(worldStep, 0.0), lodScale), float(lodIdx)), 0.0).y;
    float hD = textureLod(waterDisplacementMap, vec3(WorldToClipmapUV(worldXZ - vec2(0.0, worldStep), lodScale), float(lodIdx)), 0.0).y;
    float hU = textureLod(waterDisplacementMap, vec3(WorldToClipmapUV(worldXZ + vec2(0.0, worldStep), lodScale), float(lodIdx)), 0.0).y;
    vec2 gradient = vec2((hR - hL) / (2.0 * worldStep),
                         (hU - hD) / (2.0 * worldStep));
    return normalize(vec3(-gradient.x * waterParams.waveTimeAndScale.w,
                           1.0,
                          -gradient.y * waterParams.waveTimeAndScale.w));
}

// ── 输出 ───────────────────────────────────────────────────────
layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out float outLinearDepth;
layout(location = 2) out vec3 outWorldNormal;
layout(location = 3) flat out int outLodLevel;

// ============================================================
void main()
{
    // ── 1. 计算原始世界 XZ ─────────────────────────────────────
    vec2 worldXZ = pc.patchWorldOrigin + inMeshPos * pc.patchWorldSize;

    // ── 2. 计算到相机的 XZ 平面距离 ───────────────────────────
    float distToCamera = length(worldXZ - waterParams.waterCameraPosition.xz);

    // ── 3. 计算 Morph 因子（CDLOD 标准，含 morphStartRatio）──
    // morphStart = innerRange + (outerRange - innerRange) × morphStartRatio
    // [innerRange, morphStart]:  morph=0（全精度，无跨 LOD 混合）
    // [morphStart, outerRange]:  morph=0→1（过渡区，混合相邻两层位移）
    // 默认 morphStartRatio=0.6：外侧 40% 才开始 morph
    float morphValue = 0.0;
    {
        float low        = (pc.lodLevel > 0) ? LodRange(pc.lodLevel - 1) : 0.0;
        float high       = LodRange(pc.lodLevel);
        float morphStart = low + (high - low) * waterParams.morphStartRatio;
        float t          = (distToCamera - morphStart) / max(high - morphStart, 0.001);
        morphValue       = clamp(t, 0.0, 1.0);
    }

    // ── 4. CDLOD 顶点 Morph ──────────────────────────────────
    vec2 morphedMeshPos = inMeshPos;
    if (morphValue > 0.001)
    {
        float gridDim = float(waterParams.meshDim - 1);
        vec2  frac2   = fract(inMeshPos * gridDim * 0.5) * (2.0 / gridDim);
        morphedMeshPos = inMeshPos - frac2 * morphValue;
    }

    // ── 5. 重新计算 Morph 后的世界 XZ ────────────────────────
    vec2 morphedWorldXZ = pc.patchWorldOrigin + morphedMeshPos * pc.patchWorldSize;

    // ── 6. W-01: 计算逐 LOD Clipmap 参数并采样位移 ──────────
    // LOD i Clipmap 覆盖 = clipmapBaseScale × detailBalance^i
    float lodScale = waterParams.clipmapBaseScale * pow(max(waterParams.detailBalance, 1.0), float(pc.lodLevel));
    vec4 waveDisp = SampleDisplacement(morphedWorldXZ, lodScale, pc.lodLevel);

    // P0: 跨 LOD 位移混合 — 在 Morph 过渡区同时采样相邻两层 Clipmap 并插值
    // 消除不同 layer Nyquist 过滤差异导致的可见裂缝（CDLOD_Analysis §6.3）
    if (morphValue > 0.001 && pc.lodLevel < waterParams.lodLevels - 1)
    {
        float nextLodScale = lodScale * max(waterParams.detailBalance, 1.0);
        vec4 waveDispNext = SampleDisplacement(morphedWorldXZ, nextLodScale, pc.lodLevel + 1);
        waveDisp = mix(waveDisp, waveDispNext, morphValue);
    }

    vec2 displacedWorldXZ = morphedWorldXZ + waveDisp.xz;
    vec3 worldPos = vec3(displacedWorldXZ.x, pc.waterLevel + waveDisp.y, displacedWorldXZ.y);
    // 法线应在位移后的位置评估（morphedXZ + disp.xz），而非位移前的位置
    vec3 worldNormal = SampleWaterNormal(displacedWorldXZ, lodScale, pc.lodLevel);
    if (morphValue > 0.001 && pc.lodLevel < waterParams.lodLevels - 1)
    {
        float nextLodScaleN = lodScale * max(waterParams.detailBalance, 1.0);
        vec3 normalNext = SampleWaterNormal(displacedWorldXZ, nextLodScaleN, pc.lodLevel + 1);
        worldNormal = normalize(mix(worldNormal, normalNext, morphValue));
    }

    // ── 7. 计算视空间线性深度 ────────────────────────────────
    vec4 viewPos   = waterParams.waterViewMatrix * vec4(worldPos, 1.0);
    float linearD  = -viewPos.z;

    vec4 clipPos   = waterParams.waterVPMatrix * vec4(worldPos, 1.0);

    // ── 8. 输出 ────────────────────────────────────────────────
    outWorldPos    = worldPos;
    outLinearDepth = linearD;
    outWorldNormal = worldNormal;
    outLodLevel    = pc.lodLevel;

    gl_Position = clipPos;
}
