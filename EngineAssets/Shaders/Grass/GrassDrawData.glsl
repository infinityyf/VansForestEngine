#ifndef GRASS_DRAW_DATA_GLSL
#define GRASS_DRAW_DATA_GLSL
layout(push_constant) uniform GrassDrawPC
{
    int materialIndex;
    int objectIndex;
    uint vertexFeatureMask;
    uint boneCount;
    uint subBladeCount;
    float grassHeight;
    float lodMidDist;
    float lodFarDist;
    float aoStrength;
    float normalStrength;
    float transmissionStrength;
    uint instanceCount;
    float indirectDiffuseStrength;
} pc;
#endif
