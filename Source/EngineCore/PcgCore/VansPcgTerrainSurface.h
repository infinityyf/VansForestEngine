#pragma once
#include "VansPcgPointGenerator.h"
#include <memory>
namespace Vans
{
struct VansTerrainAsset;
struct VansPcgSurfaceHit
{
    std::array<float,3> position{};
    std::array<float,3> normal{0,1,0};
};
bool RaycastPcgTerrainSurface(std::shared_ptr<const VansTerrainAsset> terrain,
    const std::array<float,3>& origin, const std::array<float,3>& direction,
    float maximumDistance, VansPcgSurfaceHit& hit);
// 显式绑定的高度场快照。只返回几何高度和法线，不判断植物适宜度。
// 与 TerrainSampleBaseWorldHeight 使用相同的线性纹理采样坐标。
VansPcgSurfaceSampler CreatePcgTerrainSurface(std::shared_ptr<const VansTerrainAsset> terrain, std::string& error);
}
