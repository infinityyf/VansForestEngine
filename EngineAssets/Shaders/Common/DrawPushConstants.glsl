#ifndef VANS_DRAW_PUSH_CONSTANTS_GLSL
#define VANS_DRAW_PUSH_CONSTANTS_GLSL

layout(push_constant) uniform DrawPushConsts
{
    int materialIndex;
    int objectIndex;
    uint vertexFeatureMask;
    int passUser0;
} materialConst;

#endif
