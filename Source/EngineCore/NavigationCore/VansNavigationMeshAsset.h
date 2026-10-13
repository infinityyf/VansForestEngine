#pragma once

#include "VansNavigationSource.h"
#include "VansNavigationSourceSnapshot.h"
#include <memory>

namespace Vans
{
class VansNavigationBuilder;
class VansNavigationMeshCodec;
class VansNavigationMap;
class VansNavigationBuildControl;
class VansNavigationMeshAsset;

struct VansNavigationTilePayload
{
    int x = 0;
    int z = 0;
    int layer = 0;
    std::vector<unsigned char> bytes;
};
// CPU 构建后端消费的不可变基础压缩层，与 Detour tile payload 分别拥有。
struct VansNavigationTileCacheLayerPayload
{
    int x=0,z=0,layer=0;
    std::vector<unsigned char> bytes;
};
// 一个水平 cell 的有效基础层；指纹与 raster bounds 随候选一起发布。
struct VansNavigationTileLayerData
{
    int x=0,z=0;
    std::uint64_t baseInputHash=0;
    glm::vec3 boundsMinimum{0},boundsMaximum{0};
    std::vector<VansNavigationTileCacheLayerPayload> layers;
    std::uint64_t EstimateBytes() const
    {
        std::uint64_t bytes=sizeof(*this)+layers.capacity()*sizeof(VansNavigationTileCacheLayerPayload);
        for(const auto& layer:layers) bytes+=layer.bytes.capacity();
        return bytes;
    }
};
// 同一 worker 量子的拥有型结果；查询 tiles 与基础层属于同一输入。
struct VansNavigationTileBuildOutput
{
    std::uint64_t baseInputHash=0;
    std::vector<VansNavigationTilePayload> tiles;
    std::vector<VansNavigationTileLayerData> layerData;
    std::uint64_t EstimateBytes() const
    {
        std::uint64_t bytes=sizeof(*this)+tiles.capacity()*sizeof(VansNavigationTilePayload)+
            layerData.capacity()*sizeof(VansNavigationTileLayerData);
        for(const auto& tile:tiles) bytes+=tile.bytes.capacity();
        for(const auto& cell:layerData) bytes+=cell.EstimateBytes()-sizeof(cell);
        return bytes;
    }
};

struct VansNavigationProfileMetadata
{
    std::string profileId;
    VansNavigationBakeSettings bake;
    std::uint64_t bakeSettingsHash = 0;
	std::uint64_t inputHash = 0;
    std::uint64_t baseInputHash = 0;
	std::vector<VansNavigationContribution> contributions;
};
struct VansNavigationProfileBuildData : VansNavigationProfileMetadata
{
    std::vector<VansNavigationTilePayload> tiles;
    std::vector<VansNavigationTileLayerData> layerData;
    std::uint64_t EstimateBytes() const;
};
// Owned CPU request. Current rows (including empty deletion facts) take precedence
// over the immutable cooked base; no live Map/native/Scene object enters a worker.
struct VansNavigationLayerRead
{
    std::shared_ptr<const VansNavigationMeshAsset> asset;
    std::string profileId;
    std::vector<VansNavigationTileCell> cells;
    std::vector<VansNavigationTileLayerData> current;
    std::uint64_t maximumBytes=0;
    std::uint64_t EstimateBytes() const;
    bool Read(std::uint64_t baseInputHash,const VansNavigationBuildControl* control,
        std::vector<VansNavigationTileLayerData>& output,std::string& error) const;
};

// 发布后只读的烘焙输入和 cooked 数据；不拥有运行 native mesh 或查询缓冲。
class VansNavigationMeshAsset final
{
public:
    VansNavigationMeshAsset(const VansNavigationMeshAsset&) = delete;
    VansNavigationMeshAsset& operator=(const VansNavigationMeshAsset&) = delete;
    bool IsReady() const { return !m_Profiles.empty() && GetTileCount() != 0; }
    const std::string& GetMapId() const { return m_MapId; }
    const std::vector<VansNavigationProfileBuildData>& GetProfiles() const { return m_Profiles; }
    const VansNavigationProfileBuildData* FindProfile(const std::string& id) const
    {
        for (const auto& profile : m_Profiles) if (profile.profileId == id) return &profile;
        return nullptr;
    }
    const VansNavigationAreaSettings& GetBakedAreas() const { return m_BakedAreas; }
    const VansNavigationSource& GetSource() const { return m_Source; }
    std::size_t GetPayloadBytes() const;
    std::uint64_t EstimateProfileReadBytes(const std::string& profileId) const;
    bool ReadProfile(const std::string& profileId,VansNavigationProfileBuildData& output,std::string& error) const;
    std::size_t GetTileCount() const
    {
        std::size_t total = 0;
        for (const auto& profile : m_Profiles) total += profile.tiles.size();
        return total;
    }
    std::uint64_t EstimateResidentBytes() const;

private:
    friend class VansNavigationBuilder;
    friend class VansNavigationMeshCodec;
    bool PublishProfiles(std::vector<VansNavigationProfileBuildData> profiles,std::uint64_t maximumBytes,std::string& error);
    VansNavigationMeshAsset() = default;
    std::string m_MapId;
    std::vector<VansNavigationProfileBuildData> m_Profiles;
    VansNavigationAreaSettings m_BakedAreas;
    VansNavigationSource m_Source;
};
}
