#ifndef GI_WORLD_SCATTER_DATA_GLSL
#define GI_WORLD_SCATTER_DATA_GLSL
// 各区域先追踪后统一照明，必须使用本帧前缀分配的独立区间，不能覆盖同一首址。
// 总容量为全局帧射线预算；关闭世界 GI 时不存在。
// xyz 为有效散射反照率，w 为 float32 距离，-1 表示未抽到事件。
layout(set=GI_WORLD_SET,binding=7,std430) buffer GIWorldScatterBuffer
{
    uvec4 gwScatterOffsets[2];
    vec4 gwScatterEvents[];
};
uint GIWorldScatterAddress(uint region,uint ray) { return gwScatterOffsets[region/4u][region%4u]+ray; }
#endif
