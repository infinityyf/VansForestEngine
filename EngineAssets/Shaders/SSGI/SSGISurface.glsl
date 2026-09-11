#ifndef SSGI_SURFACE_GLSL
#define SSGI_SURFACE_GLSL

// 几何判据只处理表面连续性；光照半球和 BRDF 仍使用材质法线。
vec3 SSGI_NormalFromDerivatives(vec3 dx, vec3 dy, vec3 fallback)
{
    float lx = dot(dx, dx);
    float ly = dot(dy, dy);
    if (lx <= 1e-20 || ly <= 1e-20)
        return fallback;
    // 先去除长度单位，再检查夹角，避免把毫米级合法三角形判为退化。
    vec3 n = cross(dx * inversesqrt(lx), dy * inversesqrt(ly));
    float area = dot(n, n);
    if (area <= 1e-6)
        return fallback;
    n *= inversesqrt(area);
    return dot(n, fallback) < 0.0 ? -n : n;
}

vec3 SSGI_SurfaceDerivative(vec4 center, vec4 negative, vec4 positive)
{
    vec3 a = center.xyz - negative.xyz;
    vec3 b = positive.xyz - center.xyz;
    float la = dot(a, a);
    float lb = dot(b, b);
    // 屏幕边界的重复 texel 和天空不能抢占最短导数。
    bool va = negative.w > 0.0 && la > 1e-20;
    bool vb = positive.w > 0.0 && lb > 1e-20;
    return va && (!vb || la < lb) ? a : (vb ? b : vec3(0.0));
}

float SSGI_PlaneTolerance(vec4 position)
{
    float coordinateScale = max(max(abs(position.x), abs(position.y)), abs(position.z));
    return max(0.005 + position.w * 0.002, coordinateScale * 2e-6);
}

float SSGI_PlaneWeight(vec3 delta, vec3 geometricNormal, float tolerance)
{
    float error = abs(dot(delta, geometricNormal));
    return 1.0 - smoothstep(tolerance * 0.25, tolerance, error);
}

vec2 SSGI_SignNotZero(vec2 v)
{
    return mix(vec2(-1.0), vec2(1.0), greaterThanEqual(v, vec2(0.0)));
}

vec2 SSGI_EncodeNormal(vec3 n)
{
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1e-6);
    vec2 e = n.xz;
    if (n.y < 0.0) e = (1.0 - abs(e.yx)) * SSGI_SignNotZero(e);
    return e * 0.5 + 0.5;
}

vec3 SSGI_DecodeNormal(vec2 e)
{
    vec2 f = e * 2.0 - 1.0;
    vec3 n = vec3(f.x, 1.0 - abs(f.x) - abs(f.y), f.y);
    if (n.y < 0.0) n.xz = (1.0 - abs(n.zx)) * SSGI_SignNotZero(n.xz);
    return normalize(n);
}

// RGBA32F 历史：xyz = 世界坐标，w = 精确的 24 位整数数值。
// oct 法线各 8 位，材质类型 + 1 占 8 位；w=0 无效。必须 texelFetch，
// 先验证各 tap 再插值，不能过滤打包信息，也不能用 NaN float 位型存储。
vec4 SSGI_EncodeSurface(vec3 position, vec3 normal, float material)
{
    uvec2 oct = uvec2(round(clamp(SSGI_EncodeNormal(normal), 0.0, 1.0) * 255.0));
    uint type = uint(clamp(round(material), 0.0, 254.0)) + 1u;
    return vec4(position, float(oct.x | (oct.y << 8u) | (type << 16u)));
}

bool SSGI_HistorySurfaceMatches(vec4 history, vec4 position, vec3 geometricNormal,
    vec3 shadingNormal, float material, float footprint)
{
    if (history.w < 65536.0) return false;
    uint packed = uint(history.w);
    vec3 normal = SSGI_DecodeNormal(vec2(packed & 255u, (packed >> 8u) & 255u) / 255.0);
    if (abs(float((packed >> 16u) - 1u) - round(material)) > 0.25 ||
        dot(normal, shadingNormal) < 0.82) return false;
    vec3 delta = history.xyz - position.xyz;
    float tolerance = SSGI_PlaneTolerance(position);
    return abs(dot(delta, geometricNormal)) < tolerance &&
        length(delta) < max(footprint * 2.5, tolerance * 4.0);
}

#endif
