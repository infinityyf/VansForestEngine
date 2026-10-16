#ifndef VANS_RECT_LIGHT_EMISSION_GLSL
#define VANS_RECT_LIGHT_EMISSION_GLSL

// ── 辐照度向量（Irradiance Vector）单边贡献计算 ─────────────────────────────
// 参考：Heitz et al.2016 LTC 实现；与 LTC_IntegrateEdge 复用相同有理近似以保证一致性。
// 返回 cross(a, b) * (theta / sin(theta))（全 3 分量）。
vec3 CalcIrradianceEdge(vec3 a, vec3 b)
{
    float x = dot(a, b);
    float y = abs(x);
    float fa = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float fb = 3.4175940 + (4.1616724 + y) * y;
    float thetaSinTheta = (x > 0.0) ? fa / fb
                                     : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - fa / fb;
    return cross(a, b) * thetaSinTheta;
}

// ── Diffuse UV 计算：角度 UV（P→光源中心方向在 R/U 平面的投影） ─────────────
// 用 P 到 light center 的方向在光源坐标 R/U 上分解，不依赖 halfW/halfH，
// 全场景范围内 UV 连续变化（类似球面投影），不会因大场景全部 clamp 到边缘。
// outSolidAngle：仍用辐照度向量模长近似（用于 LOD 计算），与 UV 独立。
vec2 ComputeRectLightIrradianceUV(RectLightData rl, vec3 P, out float outSolidAngle)
{
    vec3 center = rl.position_halfW.xyz;
    vec3 R      = rl.right_range.xyz;
    vec3 U      = rl.up_intensity.xyz;
    float halfW = rl.position_halfW.w;
    float halfH = rl.normal_halfH.w;

    // ── Solid angle（LOD 用）：辐照度向量模长 ────────────────────────────
    vec3 Rv = rl.right_range.xyz;
    vec3 Uv = rl.up_intensity.xyz;
    vec3 L0 = normalize(center - Rv * halfW - Uv * halfH - P);
    vec3 L1 = normalize(center + Rv * halfW - Uv * halfH - P);
    vec3 L2 = normalize(center + Rv * halfW + Uv * halfH - P);
    vec3 L3 = normalize(center - Rv * halfW + Uv * halfH - P);
    vec3 I = CalcIrradianceEdge(L0, L1)
           + CalcIrradianceEdge(L1, L2)
           + CalcIrradianceEdge(L2, L3)
           + CalcIrradianceEdge(L3, L0);
    outSolidAngle = length(I);

    // ── UV：P 到光源中心方向在 R/U 平面上的角度分量 ─────────────────────
    // 相比投影法（需在 1×1m 足迹内才有变化），角度法在全场景范围都有 UV 变化。
    // 正下方 → (0.5, 0.5)；偏右 → u > 0.5；偏上 → v > 0.5
    vec3 toLight = normalize(center - P);
    float u = 1.0 - (dot(toLight, R) * 0.5 + 0.5);  // X-flip：P 在光源右侧时 toLight 指左，需翻转
    float v = dot(toLight, U) * 0.5 + 0.5;
    return clamp(vec2(u, v), 0.0, 1.0);
}

// 共用发光纹理过滤尺度；颜色由现有纹理格式在线性空间解码。
float RectLightEmissionBaseLod(float solidAngle)
{
    return max(0.0, 0.5 * log2(max(3.14159 / max(solidAngle, 1e-6), 1.0)));
}
#endif
