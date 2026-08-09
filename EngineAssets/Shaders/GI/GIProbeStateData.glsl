#ifndef GI_PROBE_STATE_DATA_GLSL_INCLUDED
#define GI_PROBE_STATE_DATA_GLSL_INCLUDED

struct GIProbeState
{
    vec4 relocationAndConfidence;
    vec4 distanceStats;
    uvec4 metadata;
};

#endif
