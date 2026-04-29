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
    vec3 T1 = normalize(V - N * dot(V, N));
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

    // 单面光源：背面贡献为 0
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
    float specPolygon = LTC_EvaluateRect(Minv, N, V, P, p0, p1, p2, p3, twoSided);

    // LTC2.x = amplitude, LTC2.y = GGX Fresnel/horizon term
    vec4 t2 = LTC_SampleAmp(NoV, roughness);
    vec3 specFresnel = F0 * t2.x + (1.0 - F0) * t2.y;

    // ── 漫反射：单位矩阵 LTC（即纯余弦投影积分）─────────────────────────
    float diffPolygon = LTC_EvaluateRect(mat3(1.0), N, V, P, p0, p1, p2, p3, twoSided);

    // 距离衰减 + 光源色 × 强度
    float atten = RectLightDistanceAttenuation(distSq, range, rl.shadowParams.y);
    vec3 lightTerm = rl.color_twoSided.rgb * rl.up_intensity.w * atten;

    // 1/π 已经隐含在 LTC 拟合中（参考 Frostbite / Filament 实现）
    outDiffuse  = diffuseColor * diffPolygon * lightTerm;
    outSpecular = specFresnel  * specPolygon * lightTerm;
}

#endif // RECT_LIGHT_LTC_GLSL
