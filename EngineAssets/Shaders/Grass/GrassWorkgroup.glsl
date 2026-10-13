#ifndef GRASS_WORKGROUP_GLSL
#define GRASS_WORKGROUP_GLSL
// 与 GrassWorkgroupGPU 一致；尾部线程只检查所在段，不访问池中的空闲范围。
struct GrassWorkgroup
{
    uint baseInstance;
    uint instanceCount;
    uint stateStride;
    uint localBase;
};
#endif
