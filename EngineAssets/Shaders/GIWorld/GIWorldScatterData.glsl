#ifndef GI_WORLD_SCATTER_DATA_GLSL
#define GI_WORLD_SCATTER_DATA_GLSL
#include "../GI/GIRegionCapacity.h"
// 各区域先追踪后统一照明，必须使用本帧前缀分配的独立区间，不能覆盖同一首址。
// 总容量为全局帧射线预算；关闭世界 GI 时不存在。
// Trace 写入反照率/float32 距离；Lighting 读取后复用为散射辐射，Atlas 只读 RGB。
// 是否完整由 radiance.w 表示；未知数据不能作为零散射发布。
#ifndef GI_WORLD_SCATTER_BINDING
#define GI_WORLD_SCATTER_BINDING 7
#endif
layout(set=GI_WORLD_SET,binding=GI_WORLD_SCATTER_BINDING,std430) buffer GIWorldScatterBuffer
{
    uvec4 gwScatterOffsets[GI_MAX_REGION_COUNT/4u];
    vec4 gwScatterEvents[];
};
uint GIWorldScatterAddress(uint region,uint ray) { return gwScatterOffsets[region/4u][region%4u]+ray; }
#endif
