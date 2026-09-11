#ifndef REFLECTION_PROBE_DEBUG_INCLUDED
#define REFLECTION_PROBE_DEBUG_INCLUDED
#include "ReflectionProbeData.glsl"

bool ReflectionProbeIsIsolatedDebugView()
{
    return reflectionProbeDebugView >= 8u && reflectionProbeDebugView <= 14u;
}

// 使用几何表面重建反射方向，不读取材质法线。优先选择较短的有效邻边，减小轮廓跨面的误差。
vec3 ReflectionProbeDebugSurfaceEdge(vec4 center, vec4 negative, vec4 positive)
{
    vec3 a = center.xyz - negative.xyz;
    vec3 b = positive.xyz - center.xyz;
    float la = dot(a, a), lb = dot(b, b);
    bool va = negative.w > 0.0 && la > 1e-20;
    bool vb = positive.w > 0.0 && lb > 1e-20;
    return va && (!vb || la < lb) ? a : (vb ? b : vec3(0.0));
}

vec3 ReflectionProbeDebugSurfaceNormal(sampler2D positions, ivec2 pixel, vec3 viewDirection)
{
    ivec2 last = textureSize(positions, 0) - 1;
    pixel = clamp(pixel, ivec2(0), last);
    vec4 center = texelFetch(positions, pixel, 0);
    vec3 dx = ReflectionProbeDebugSurfaceEdge(center,
        texelFetch(positions, max(pixel - ivec2(1, 0), ivec2(0)), 0),
        texelFetch(positions, min(pixel + ivec2(1, 0), last), 0));
    vec3 dy = ReflectionProbeDebugSurfaceEdge(center,
        texelFetch(positions, max(pixel - ivec2(0, 1), ivec2(0)), 0),
        texelFetch(positions, min(pixel + ivec2(0, 1), last), 0));
    float lx = dot(dx, dx), ly = dot(dy, dy);
    if (lx <= 1e-20 || ly <= 1e-20) return viewDirection;
    vec3 n = cross(dx * inversesqrt(lx), dy * inversesqrt(ly));
    float area = dot(n, n);
    if (area <= 1e-6 || any(isnan(n)) || any(isinf(n))) return viewDirection;
    return n * inversesqrt(area);
}

// 输入只有捕获辐亮度与覆盖率；不经过接收材质的 BRDF、AO、SSR 或光照合成。
vec3 ReflectionProbeDebugRadiance(ReflectionProbeSample probe, vec3 sky, uint view, float exposure)
{
    float coverage = clamp(probe.coverage, 0.0, 1.0);
    if (view == 11u) return vec3(coverage);
    vec3 value = probe.specular;
    if (view == 9u) value = mix(sky, probe.specular, coverage);
    else if (view == 10u) value = sky * (1.0 - coverage);
    return value * exposure;
}
#endif
