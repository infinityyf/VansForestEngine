// =============================================================================
// RectLightLTC.glsl  —  Linearly Transformed Cosines for rectangular area lights
//
// Reference:
//   - Heitz, Dupuy, Hill, Neubelt 2016: "Real-Time Polygonal-Light Shading with
//     Linearly Transformed Cosines" (https://eheitzresearch.wordpress.com/415-2/)
//   - selfshadow/ltc_code MIT-licensed reference implementation.
//
// Dependencies (must be #included before this file):
//   - LightsData.glsl     (RectLightData, GetRectLight, GetRectLightCount)
//   - BRDFData.glsl       (LTC1 / LTC2 samplers + LTC_SampleMatrix / LTC_SampleAmp)
//
// Convention:
//   rl.normal_halfH.xyz  = "light forward" — the direction the rect radiates.
//   The 4 rectangle corners are p ± rl.right * halfW ± rl.up * halfH.
//   Front face is the side the normal points away from (i.e. surface point is
//   in front when dot(rl.normal, rl.position - surface) > 0).
// =============================================================================

#ifndef RECT_LIGHT_LTC_GLSL
#define RECT_LIGHT_LTC_GLSL

// ── Polygon irradiance integral (Heitz et al. 2016, "Real-Time Polygonal-Light
//    Shading with Linearly Transformed Cosines", listing 1) ─────────────────
float LTC_IntegrateEdge(vec3 v1, vec3 v2)
{
    float x = dot(v1, v2);
    float y = abs(x);

    // Rational approximation of theta * sin(theta) / sin(theta) where
    // theta = acos(x). Constants taken from the paper supplementary code.
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;

    float theta_sintheta = (x > 0.0) ? v : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return cross(v1, v2).z * theta_sintheta;
}

// Evaluate LTC over a rectangle (4 corners in the local Z-up cosine frame).
// Returns the unnormalised diffuse irradiance E in [0..2π].
float LTC_EvaluateRect(mat3 Minv, vec3 N, vec3 V, vec3 P,
                       vec3 p0, vec3 p1, vec3 p2, vec3 p3, bool twoSided)
{
    // Construct an orthonormal basis aligned with the surface tangent frame.
    // When V ≈ N (view direction nearly collinear with surface normal, e.g. top-down view),
    // V - N*dot(V,N) approaches zero and normalize() produces NaN / garbage, causing the
    // LTC integral to collapse to zero (looks like the light disappears into shadow).
    // Fix: fall back to an arbitrary perpendicular vector when the projected component is
    // too small, matching the approach used in Heitz et al. reference implementation.
    vec3 T1_raw = V - N * dot(V, N);
    float T1_len = length(T1_raw);
    vec3 T1;
    if (T1_len > 1e-4)
    {
        T1 = T1_raw / T1_len;
    }
    else
    {
        // Pick a stable fallback tangent perpendicular to N.
        vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        T1 = normalize(cross(up, N));
    }
    vec3 T2 = cross(N, T1);

    // Rotate Minv into that frame (rows of Minv multiplied by the basis).
    Minv = Minv * transpose(mat3(T1, T2, N));

    // Transform polygon vertices into the LTC cosine space.
    vec3 L0 = Minv * (p0 - P);
    vec3 L1 = Minv * (p1 - P);
    vec3 L2 = Minv * (p2 - P);
    vec3 L3 = Minv * (p3 - P);

    // Project to unit hemisphere.
    L0 = normalize(L0);
    L1 = normalize(L1);
    L2 = normalize(L2);
    L3 = normalize(L3);

    // Sum signed edge integrals.
    float sum = 0.0;
    sum += LTC_IntegrateEdge(L0, L1);
    sum += LTC_IntegrateEdge(L1, L2);
    sum += LTC_IntegrateEdge(L2, L3);
    sum += LTC_IntegrateEdge(L3, L0);

    return twoSided ? abs(sum) : max(0.0, -sum);
}

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

// Method A-Spec：镜面反射方向与光源平面求交，用于低粗糙度 specular tinting。
// outValid：交点在光源正面且射线前向时为 true，否则应回退到辐照度 UV。
vec2 ComputeRectLightSpecularUV(RectLightData rl, vec3 N, vec3 V, vec3 P, out bool outValid)
{
    vec3 center = rl.position_halfW.xyz;
    vec3 R      = rl.right_range.xyz;
    vec3 U      = rl.up_intensity.xyz;
    vec3 lightN = rl.normal_halfH.xyz;
    float halfW = rl.position_halfW.w;
    float halfH = rl.normal_halfH.w;

    // 镜面反射方向（从着色点射出）
    vec3 reflDir = reflect(-V, N);

    float denom = dot(reflDir, lightN);
    outValid = false;
    if (abs(denom) < 1e-5) return vec2(0.5);

    float t = dot(center - P, lightN) / denom;
    if (t <= 0.0) return vec2(0.5);  // 交点在背面

    outValid = true;
    vec3 hit   = P + reflDir * t;
    vec3 delta = hit - center;
    float u = dot(delta, R) / halfW * 0.5 + 0.5;
    float v = 1.0 - (dot(delta, U) / halfH * 0.5 + 0.5);  // Y-flip
    return clamp(vec2(u, v), 0.0, 1.0);
}

// 距离衰减：与 SpotLight 保持类似的 smooth windowing：在 range 处衰减到 0。
float RectLightDistanceAttenuation(float distSq, float range, float exponent)
{
    float invR2 = 1.0 / max(range * range, 1e-6);
    float window = clamp(1.0 - (distSq * invR2) * (distSq * invR2), 0.0, 1.0);
    window *= window;
    float falloff = 1.0 / max(distSq, 1e-4);
    // exponent 接近 2 时退化为标准 inverse-square；提供 1..3 的可调钝化。
    return falloff * window * pow(window, max(exponent - 2.0, 0.0));
}

// 面光源 LTC 主入口。
// 参数：
//   rl              已加载的 RectLightData
//   N               surface normal (world)
//   V               view dir (world, surface→camera)
//   P               surface position (world)
//   roughness       perceptual roughness in [0,1]
//   diffuseColor    Lambert albedo
//   F0              Fresnel reflectance at normal incidence
// 输出：
//   outDiffuse / outSpecular 已乘以光源 color * intensity，但**未乘**阴影。
void EvaluateRectLightLTC(
    RectLightData rl,
    vec3 N, vec3 V, vec3 P,
    float roughness, vec3 diffuseColor, vec3 F0,
    out vec3 outDiffuse, out vec3 outSpecular)
{
    outDiffuse = vec3(0.0);
    outSpecular = vec3(0.0);

    vec3 center = rl.position_halfW.xyz;
    vec3 toLight = center - P;
    float distSq = dot(toLight, toLight);
    float range = rl.right_range.w;
    if (distSq > range * range) return;

    // 单面光源：背面贡献为 0（facing <= 0 表示 P 在光源法线反方向，即光源背面）
    bool twoSided = rl.color_twoSided.w > 0.5;
    float facing = dot(rl.normal_halfH.xyz, -toLight);
    if (!twoSided && facing <= 0.0) return;

    // 4 角点（顺时针 / 逆时针均可，IntegrateEdge 是有向的）
    vec3 R = rl.right_range.xyz;
    vec3 U = rl.up_intensity.xyz;
    float halfW = rl.position_halfW.w;
    float halfH = rl.normal_halfH.w;
    vec3 p0 = center - R * halfW - U * halfH;
    vec3 p1 = center + R * halfW - U * halfH;
    vec3 p2 = center + R * halfW + U * halfH;
    vec3 p3 = center - R * halfW + U * halfH;

    float NoV = clamp(dot(N, V), 0.0, 1.0);

    // ── 高光：使用 LTC1 矩阵 ─────────────────────────────────────────────
    // LTC1 纹理通道：(r,g,b,a) = (m11, m31, m13, m33)，m22 恒为 1（已除以 m22 归一）
    // M^-1 布局：| m11   0   m13 |     selfshadow/ltc_code 参考实现一致
    //            |  0    1    0  |
    //            | m31   0   m33 |
    // GLSL mat3(col0, col1, col2) 列主序：
    vec4 t1 = LTC_SampleMatrix(NoV, roughness);
    mat3 Minv = mat3(
        vec3(t1.x, 0.0, t1.y),   // col 0 = (m11,  0,  m31)
        vec3(0.0,  1.0, 0.0),    // col 1 = (0,    1,   0 )  [m22 恒为 1]
        vec3(t1.z, 0.0, t1.w)    // col 2 = (m13,  0,  m33)
    );

    // LTC2.x = amplitude, LTC2.y = GGX Fresnel/horizon term
    vec4 t2 = LTC_SampleAmp(NoV, roughness);
    vec3 specFresnel = F0 * t2.x + (1.0 - F0) * t2.y;

    float specPolygon, diffPolygon;
    bool backFace = twoSided && (facing <= 0.0);

    if (backFace) {
        // 背面高光：将多边形关于着色点 P 的表面平面做镜像
        // 使多边形从地平线以下翻到以上，LTC 矩阵得以在正确半球内求值，形状正确
        vec3 q0 = p0 - 2.0 * dot(p0 - P, N) * N;
        vec3 q1 = p1 - 2.0 * dot(p1 - P, N) * N;
        vec3 q2 = p2 - 2.0 * dot(p2 - P, N) * N;
        vec3 q3 = p3 - 2.0 * dot(p3 - P, N) * N;
        specPolygon = LTC_EvaluateRect(Minv, N, V, P, q0, q1, q2, q3, false);
        // 背面漫反射：原始多边形位于地平线以下，abs(sum) 正确计算辐照度
        diffPolygon = LTC_EvaluateRect(mat3(1.0), N, V, P, p0, p1, p2, p3, true);
    } else {
        specPolygon = LTC_EvaluateRect(Minv, N, V, P, p0, p1, p2, p3, false);
        diffPolygon = LTC_EvaluateRect(mat3(1.0), N, V, P, p0, p1, p2, p3, false);
    }

    // specFresnel = F0 * t2.x + (1-F0) * t2.y 已将 amplitude(t2.x) 嵌入 F0 权重项，
    // amplitude 只应用一次。去掉单独的 specPolygon *= t2.x 避免双重放大（→ 高光溢出变白）。

    // 距离衰减 + 光源色 × 强度
    // 用光源半尺寸（halfSize）软化 distSq，解决两类奇点：
    //   - 正下方中心最亮：近处 softDistSq ≈ halfSize²，衰减上界有界而非趋向无穷
    //   - 共面亮条：分母不含 perpDist，共面时 distSq = 横向距离²，不为 0
    //   - 远处（distSq >> halfSize²）：自然退化为平方反比衰减
    // 参考：Lagarde & de Rousiers, "Moving Frostbite to Physically Based Rendering"
    float halfSize  = max(halfW, halfH);
    float softDistSq = distSq + halfSize * halfSize;
    float invR2     = 1.0 / max(range * range, 1e-6);
    float wf        = distSq * invR2;
    float window    = clamp(1.0 - wf * wf, 0.0, 1.0);
    window          = window * window * pow(window * window, max(rl.shadowParams.y - 2.0, 0.0));
    float atten     = window / max(softDistSq, 1e-4);
    vec3 baseTint = rl.color_twoSided.rgb * rl.up_intensity.w * atten;

    // diffuse/specular 分别持有独立的 lightTerm，默认相同（均匀色光源）
    vec3 diffLightTerm = baseTint;
    vec3 specLightTerm = baseTint;

#ifdef RECT_LIGHT_EMISSIVE_ENABLED
    // Diffuse 用辐照度向量 UV（能量加权，与视角无关）
    // Specular 用镜面反射方向 UV（低粗糙度时正确反映贴图细节），按粗糙度^2 混合回辐照度 UV
    int texSlot = int(rl.shadowParams.z);
    if (texSlot >= 0)
    {
        float solidAngle;
        vec2 irradianceUV = ComputeRectLightIrradianceUV(rl, P, solidAngle);

        float maxMip = float(textureQueryLevels(rectLightEmissive) - 1);
        // Diffuse LOD：只由立体角（距离/尺寸）决定，diffuse 是视角无关积分，与粗糙度无关。
        float baseLod  = max(0.0, 0.5 * log2(max(3.14159 / max(solidAngle, 1e-6), 1.0)));
        float diffLod  = clamp(baseLod + rl.shadowParams.w, 0.0, maxMip);
        // Specular LOD：在 baseLod 基础上叠加粗糙度偏移（粗糙度越高 → mip 越高 → 越模糊）
        float specLod  = clamp(baseLod + roughness * maxMip * 0.5 + rl.shadowParams.w, 0.0, maxMip);

        // Specular UV：镜面反射方向与光源平面求交
        // 粗糙度低（< 0.5）且交点有效时用反射 UV，否则回退到辐照度 UV。
        // 不在两套 UV 空间之间做线性 mix——不同坐标系直接插值会造成 UV 滑移/拉伸。
        // 模糊效果由 LOD 控制，而非混合 UV。
        bool specUVValid;
        vec2 specUV      = ComputeRectLightSpecularUV(rl, N, V, P, specUVValid);
        bool useSpecUV   = specUVValid && (roughness < 0.5);
        vec2 finalSpecUV = useSpecUV ? specUV : irradianceUV;

        vec3 diffTexColor = textureLod(rectLightEmissive, vec3(irradianceUV,  float(texSlot)), diffLod).rgb;
        // roughness 近 0 时贴图图案只应出现在 specular（镜面反射图像），
        // diffuse 侧退回纯白（不带纹理色调），避免平滑表面的 diffuse 中残留纹理图案。
        // roughness^2 过渡曲线：< 0.5 基本为白，> 0.7 基本为全纹理。
        float diffTexBlend = clamp(roughness * roughness * 4.0, 0.0, 1.0);
        diffTexColor = mix(vec3(1.0), diffTexColor, diffTexBlend);
        vec3 specTexColor = textureLod(rectLightEmissive, vec3(finalSpecUV,   float(texSlot)), specLod).rgb;

        diffLightTerm = diffTexColor * baseTint;
        specLightTerm = specTexColor * baseTint;
    }
#endif // RECT_LIGHT_EMISSIVE_ENABLED

    // 能量守恒：specular 拿走了 specFresnel 的能量，diffuse 需乘以 (1 - specFresnel)。
    // 低粗糙度金属 specFresnel 趋近于 1 → diffuse 趋近于 0，符合 PBR 能量守恒。
    vec3 diffEnergyConserve = vec3(1.0) - specFresnel;
    // 1/π 已经隐含在 LTC 拟合中（参考 Frostbite / Filament 实现）
    outDiffuse  = diffuseColor * diffEnergyConserve * diffPolygon * diffLightTerm;
    outSpecular = specFresnel  * specPolygon * specLightTerm;
}

#endif // RECT_LIGHT_LTC_GLSL
