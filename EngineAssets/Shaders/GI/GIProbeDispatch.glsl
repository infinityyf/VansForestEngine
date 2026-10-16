#ifndef GI_PROBE_DISPATCH_GLSL
#define GI_PROBE_DISPATCH_GLSL
// 与 VansGIProbeDispatchParameters 共用唯一 112 字节布局；查询策略不再藏在网格尺度中。
layout(push_constant) uniform GIProbeDispatchParameters
{
    vec4 gridParams;       // xyz: Probe 间距，w: 保留。
    vec4 dispatchParams;
    vec4 frameParams;
    vec4 regionParams;
    vec4 lightingParams;
    vec4 temporalParams;
    vec4 traceParams;      // 最大距离、主查询体素下限、散射入射体素下限、保留。
} giDispatch;
#endif
