#pragma once

#include "../VansNavigationMeshAsset.h"
#include "../VansNavigationSourceSnapshot.h"

struct dtNavMeshCreateParams;

namespace Vans
{
// Native construction parameters are private to NavigationCore. Ordinary
// Recast output and TileCache output use exactly the same owned wire arrays.
class VansNavigationNativeLinks final
{
public:
    bool Compile(const std::vector<VansNavigationLinkGeometry>& links,const VansNavigationAreaSettings& areas,
        const VansNavigationBakeSettings& bake,int x,int z,std::string& error);
    void Apply(dtNavMeshCreateParams& params) const;
    std::uint64_t Bytes() const;
private:
    std::vector<float> m_Vertices,m_Radii;
    std::vector<unsigned short> m_Flags;
    std::vector<unsigned char> m_Areas,m_Directions;
    std::vector<unsigned int> m_Keys;
};
bool ValidateNavigationConnectedLinks(const std::vector<VansNavigationTilePayload>& tiles,const VansNavigationBakeSettings& bake,
    const std::vector<VansNavigationLinkGeometry>& links,std::uint64_t maximumBytes,std::string& error);
// Detour tile 与压缩 layer 共用固定网格边界校验。
bool ValidateNavigationTileGridBounds(int x,int z,const float* low,const float* high,
    const VansNavigationBakeSettings& bake,std::string& error);
// 在进入 Detour 前验证 cooked 布局和内部索引；不建立 native 实例。
bool ValidateNavigationTileData(const std::vector<unsigned char>& data,
    const VansNavigationAreaSettings& areas, std::string& error);
bool ValidateNavigationTileSet(const std::vector<VansNavigationTilePayload>& tiles,
    const VansNavigationBakeSettings& bake, const VansNavigationAreaSettings& areas,
    std::string& error);
}
