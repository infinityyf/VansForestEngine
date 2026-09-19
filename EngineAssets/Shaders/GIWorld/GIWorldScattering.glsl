#ifndef GI_WORLD_SCATTERING_GLSL
#define GI_WORLD_SCATTERING_GLSL
const uint GI_WORLD_SCATTER_GROUPS=8u;
const float GI_WORLD_SCATTER_PROBABILITY=1.0/float(GI_WORLD_SCATTER_GROUPS);
// 次数从 1/4 降为 1/8，单次体素上限翻倍，额外体素遍历最坏总量不增加。
uint GIWorldScatterStepBudget() { return gwTable.z*2u; }
// 树冠采用各向同性、单次散射的低频近似；自由程 PDF 为 T(s)*sigma(s)。
float GIWorldScatterOpticalDepth(float u) { return -log(max(1.0-u,1e-7)); }
vec3 GIWorldScatterRadiance(vec3 albedo,vec3 incoming,float transmission,float probability)
{ return clamp(albedo,0.0,1.0)*max(incoming,vec3(0))*clamp(transmission,0.0,1.0)/probability; }
vec3 GIWorldSparseScatterRadiance(vec3 albedo,vec3 incoming,float transmission,float sourceProbability,float radianceLimit)
{
    // 限幅必须在稀疏采样的概率补偿之前，否则减少查询次数会无故压暗期望能量。
    vec3 value=GIWorldScatterRadiance(albedo,incoming,transmission,sourceProbability);
    return min(value,vec3(radianceLimit))/GI_WORLD_SCATTER_PROBABILITY;
}
vec3 GIWorldCompositeRadiance(vec3 surface,float transmission,vec3 scattering,float radianceLimit)
{ return min(min(max(surface*transmission,vec3(0)),vec3(radianceLimit))+scattering,vec3(65504.0)); }
// 相函数重建低频入射光；使用比事件定位更粗的足迹，限制穿过整个森林的步数。
float GIWorldScatterFootprint(float probeSpacing) { return max(probeSpacing/8.0,gwGrid.x*4.0); }
// 照明查询必须越过已知世界边界；短距离 miss 不等于天空可见。
float GIWorldEnvironmentDistance(vec3 p)
{
    float distance=length(max(abs(gwVoxelMin.xyz-p),abs(gwVoxelMax.xyz-p)));
    if(gwHeight.w!=0u)
    {
        vec2 range=gwRanges[gwHeightLevels[gwHeight.z-1u].x];
        vec3 lo=vec3(-gwTerrain.x*.5,range.x,-gwTerrain.x*.5);
        vec3 hi=vec3(gwTerrain.x*.5,range.y,gwTerrain.x*.5);
        distance=max(distance,length(max(abs(lo-p),abs(hi-p))));
    }
    return max(distance+gwGrid.x,1.0);
}
#endif
