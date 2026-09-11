#ifndef GI_PROBE_STATE_DATA_GLSL_INCLUDED
#define GI_PROBE_STATE_DATA_GLSL_INCLUDED

struct GIProbeState
{
    // xyz：下一次追踪的目标偏移；w：已经发布的光照置信度。
    vec4 relocationAndConfidence;
    // xyz：最近一次实际追踪偏移；有效光照的所有消费者必须使用它。
    vec4 traceOffsetAndBackface;
    // x：0 待首次更新、1 已发布；y：更新帧；z：当前位置样本数；w：近距背面数。
    uvec4 metadata;
};

// 图集历史只有在相同追踪位置才继续累计，等待时间不代表新样本。
bool GI_ProbeTraceOriginChanged(GIProbeState state, float cell)
{
    return length(state.relocationAndConfidence.xyz - state.traceOffsetAndBackface.xyz) > max(cell * 0.002, 1e-6);
}

#endif
