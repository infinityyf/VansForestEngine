#ifndef GI_PROBE_IRRADIANCE_HISTORY_GLSL
#define GI_PROBE_IRRADIANCE_HISTORY_GLSL
// 与 VansGIProbeIrradianceHistory 对齐；这里保存主光照/散射的积分 E，不是方向 Li。
// 浮点分量不能由量化后的总图集反复相减恢复。
struct GIProbeIrradianceHistory
{
    uvec2 integration;
    vec4 primary[36];
    vec4 scattering[36];
};
layout(set=0,binding=7,std430) buffer IrradianceHistoryBuffer
{
    GIProbeIrradianceHistory histories[];
};
#endif
