// TerrainNoise.glsl — 基于 Scene.glsl (ShaderToy tcS3WD) 的程序化噪声
// 用于地形细分区域的微细节位移
//
// 来源函数对照：
//   n2d()                      ← Scene.glsl n2d()                  (完全一致)
//   terrainDetailFbm()         ← Scene.glsl hill()                 (参数化版本)
//   terrainDetailFbmWarped()   ← Scene.glsl mountain() 思路         (域扭曲可选)
//   terrainDetailGradient()    ← Scene.glsl calcNormal() 思路       (中心差分)

// ─── 2D 平滑值噪声 ────────────────────────────────────────────
// 直接来自 Scene.glsl n2d()
// 使用 sin/fract hash + smoothstep 插值，C1 连续
float n2d(vec2 p) {
    vec2 i = floor(p); p -= i;
    p *= p * (3.0 - p * 2.0);
    return dot(
        mat2(fract(sin(mod(
            vec4(0, 1, 113, 114) + dot(i, vec2(1, 113)),
            6.2831853
        )) * 43758.5453))
        * vec2(1.0 - p.y, p.y),
        vec2(1.0 - p.x, p.x)
    );
}

// ─── FBM 分形噪声（适配自 Scene.glsl hill()） ─────────────────
// 与原始 hill() 的差异：
//   1. gain/lacunarity 参数化（不再硬编码 0.52/2.0）
//   2. 输出归一化到 [-1, 1]（去除 *200 硬编码缩放）
//   3. octaves 由 uniform 控制（替代编译期 iter）
//   4. 编译期循环上限 8，运行时通过 break 截断
float terrainDetailFbm(vec2 p, int octaves, float gain, float lacunarity) {
    float a = 0.0;
    float b = 1.0;
    float maxVal = 0.0;

    for (int i = 0; i < 8; i++) {
        if (i >= octaves) break;
        a += b * n2d(p);
        maxVal += b;
        b *= gain;
        p *= lacunarity;
    }

    // 归一化到 [0, 1]，去直流偏移 → [-1, 1]
    return (a / maxVal - 0.5) * 2.0;
}

// ─── 域扭曲 FBM（可选，适配自 Scene.glsl mountain() 思路） ───
// 用低频噪声扭曲输入坐标，打破网格对齐
// warpStrength: 0.0 = 关闭，0.2~0.3 = 可见扭曲
const mat2 noiseRotateM2 = mat2(0.8, -0.6, 0.6, 0.8);

float terrainDetailFbmWarped(vec2 p, int octaves, float gain, float lacunarity, float warpStrength) {
    // 低频噪声扭曲坐标
    vec2 warp = vec2(
        terrainDetailFbm(p + vec2(0.0, 0.0), 2, gain, lacunarity),
        terrainDetailFbm(p + vec2(5.2, 1.3), 2, gain, lacunarity)
    );
    p += warp * warpStrength;

    // 标准 FBM + 旋转
    float a = 0.0;
    float b = 1.0;
    float maxVal = 0.0;

    for (int i = 0; i < 8; i++) {
        if (i >= octaves) break;
        a += b * n2d(p);
        maxVal += b;
        b *= gain;
        p = noiseRotateM2 * p * lacunarity;
    }

    return (a / maxVal - 0.5) * 2.0;
}

// ─── 噪声梯度 2D（中心差分） ──────────────────────────────────
// worldXZ: 世界空间 XZ 坐标（未乘频率）
// frequency: 噪声基础频率
// eps: 差分步长（世界单位），推荐 0.01~0.05
// 返回值: vec2(∂noise/∂x, ∂noise/∂z)  单位: noise值/世界单位
//
// 使用中心差分以获得更高精度：
//   ∂f/∂x ≈ (f(x+eps) - f(x-eps)) / (2*eps)
//   ∂f/∂z ≈ (f(x, z+eps) - f(x, z-eps)) / (2*eps)
vec2 terrainDetailGradient(
    vec2 worldXZ, float frequency,
    int octaves, float gain, float lacunarity,
    float eps
) {
    vec2 p = worldXZ * frequency;
    float epsFreq = eps * frequency;

    float nRight = terrainDetailFbm(p + vec2(epsFreq, 0.0), octaves, gain, lacunarity);
    float nLeft  = terrainDetailFbm(p - vec2(epsFreq, 0.0), octaves, gain, lacunarity);
    float nUp    = terrainDetailFbm(p + vec2(0.0, epsFreq), octaves, gain, lacunarity);
    float nDown  = terrainDetailFbm(p - vec2(0.0, epsFreq), octaves, gain, lacunarity);

    return vec2(
        (nRight - nLeft) / (2.0 * eps),
        (nUp    - nDown) / (2.0 * eps)
    );
}

// ─── 噪声梯度 2D（域扭曲版本，中心差分） ────────────────────
// 与 terrainDetailGradient 相同，但内部使用 terrainDetailFbmWarped
// 确保梯度与 displacement 使用相同的噪声函数
vec2 terrainDetailGradientWarped(
    vec2 worldXZ, float frequency,
    int octaves, float gain, float lacunarity,
    float warpStrength, float eps
) {
    vec2 p = worldXZ * frequency;
    float epsFreq = eps * frequency;

    float nRight = terrainDetailFbmWarped(p + vec2(epsFreq, 0.0), octaves, gain, lacunarity, warpStrength);
    float nLeft  = terrainDetailFbmWarped(p - vec2(epsFreq, 0.0), octaves, gain, lacunarity, warpStrength);
    float nUp    = terrainDetailFbmWarped(p + vec2(0.0, epsFreq), octaves, gain, lacunarity, warpStrength);
    float nDown  = terrainDetailFbmWarped(p - vec2(0.0, epsFreq), octaves, gain, lacunarity, warpStrength);

    return vec2(
        (nRight - nLeft) / (2.0 * eps),
        (nUp    - nDown) / (2.0 * eps)
    );
}
