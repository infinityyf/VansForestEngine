#ifndef VANS_HAIR_VISIBILITY_DATA_GLSL
#define VANS_HAIR_VISIBILITY_DATA_GLSL
// CPU VansRenderPassManager 分配同样的 3 个 uint 深度/像素。
const uint HAIR_LAYER_COUNT = 3u;
layout(std430, set=1, binding=1) buffer HairLayerDepths { uint hairLayerDepths[]; };
uint HairPixelBase(ivec2 pixel, uint width)
{
    return (uint(pixel.y) * width + uint(pixel.x)) * HAIR_LAYER_COUNT;
}
#endif
