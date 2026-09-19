#ifndef ENVIRONMENT_FILTER_GLSL
#define ENVIRONMENT_FILTER_GLSL

// 每条积分样本代表 1/(N*pdf) 立体角。按覆盖面积读取辐射 mip，避免
// HDR 太阳被少量点采样重复成孤立亮斑；不裁剪辐射能量，也不模糊材质。
float EnvironmentSampleLod(vec3 direction, float pdf, uint sampleCount,
    float faceResolution, float maxLod)
{
    vec3 d = abs(direction);
    float major = max(max(d.x, d.y), d.z);
    float texelSolidAngle = 4.0 * major * major * major /
        (faceResolution * faceResolution);
    float sampleSolidAngle = 1.0 / max(float(sampleCount) * pdf, 1e-8);
    return clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle), 0.0, maxLod);
}
#endif
