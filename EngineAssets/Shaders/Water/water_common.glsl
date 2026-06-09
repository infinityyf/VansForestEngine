// water_common.glsl — 水面系统共享常量与工具函数
// 设计文档 §10.2
#ifndef WATER_COMMON_GLSL
#define WATER_COMMON_GLSL

#define MAX_LOD_COUNT        10
#define LOD_DATA_RESOLUTION  256
#define WATER_IOR            1.33
#define PI                   3.14159265359

// ----------------------------------------------------------------
// oct 编码 / 解码（Cigolle 2014）
// 将单位向量编码到 [-1, 1]² 的 RG 通道中
// ----------------------------------------------------------------
vec2 OctEncode(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0)
    {
        vec2 s = sign(n.xy);
        n.xy   = (1.0 - abs(n.yx)) * s;
    }
    return n.xy;
}

vec3 OctDecode(vec2 f)
{
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy += sign(n.xy) * (-t);
    return normalize(n);
}

// ----------------------------------------------------------------
// 线性深度：将裁剪空间深度 [0,1] 转为视空间线性深度 [near, far]
// ----------------------------------------------------------------
float LinearizeDepth(float d, float near, float far)
{
    return near * far / (far - d * (far - near));
}

#endif // WATER_COMMON_GLSL
