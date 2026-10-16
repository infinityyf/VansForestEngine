#ifndef SSGI_HISTORY_GLSL
#define SSGI_HISTORY_GLSL

layout(set = 1, binding = SSGI_HISTORY_BINDING) uniform SSGIHistoryInfo
{
    vec4 historyScreenSize;
    vec4 historyFrameParams;
    vec4 lightingMinimum;
    vec4 lightingMaximum;
};

bool SSGI_LightingResponseAt(vec3 worldPosition)
{
    if (historyFrameParams.w < 0.5) return false;
    if (historyFrameParams.w < 1.5) return true;
    return all(greaterThanEqual(worldPosition, lightingMinimum.xyz)) &&
        all(lessThanEqual(worldPosition, lightingMaximum.xyz));
}

#endif
