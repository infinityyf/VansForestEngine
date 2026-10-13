#pragma once

#include "VansNavigationMeshAsset.h"
#include <shared_mutex>
#include <optional>
#include <mutex>
#include <map>

class dtNavMesh;
namespace Vans
{
class VansNavigationQueryContext;
struct VansNavigationTraversalState;
// 运行实例拥有 native 网格及其写事务生命周期，多个独占查询可以共享只读实例。
class VansNavigationMap final
{
public:
    VansNavigationMap() = default;
    ~VansNavigationMap();
    VansNavigationMap(const VansNavigationMap&) = delete;
    VansNavigationMap& operator=(const VansNavigationMap&) = delete;
    bool Initialize(std::shared_ptr<const VansNavigationMeshAsset> asset,
        const VansNavigationSettings& settings, const std::string& profileId, std::string& error);
    bool IsReady() const;
    std::shared_ptr<const VansNavigationMeshAsset> GetAsset() const;
    std::string GetProfileId() const;
    std::optional<VansNavigationBakeSettings> GetBakeSettings() const;
    std::size_t EstimateResidentBytes() const;
    static std::uint64_t EstimateInitializationBytes(const VansNavigationMeshAsset& asset,
        const VansNavigationSettings& settings,const std::string& profileId);
    bool HasFilter(const std::string& id) const;
    bool HasLink(VansNavigationLinkKey key) const;
    // Owner-thread update ports. Workers consume the captured CPU request and
    // never mutate the native mesh. A busy read quantum defers publication.
    bool InvalidateTiles(const std::vector<VansNavigationTileCell>& cells,std::string& error);
    bool CaptureTileUpdate(VansNavigationTileUpdate& output,std::string& error) const;
    // Capture current owned overrides and a bounded cooked read request; no I/O here.
    bool CaptureLayerRead(const VansNavigationTileUpdate& update,std::uint64_t maximumBytes,
        VansNavigationLayerRead& output,std::string& error) const;
    VansNavigationTileCommitResult ApplyTileUpdate(const VansNavigationTileUpdate& update,
        const VansNavigationTileBuildOutput& candidate,std::uint64_t maximumTransactionBytes,std::string& error);
    std::size_t GetResidentTileCount() const;
    bool CaptureResidentCells(std::vector<VansNavigationTileCell>& output,std::string& error) const;
    std::uint64_t GetTraversalRevision() const;
    std::size_t GetPendingCellCount() const;
    bool CaptureDebugGeometry(const VansNavigationDebugView& view,const std::string& filterId,
        VansNavigationDebugGeometry& output,std::string& error) const;

private:
    friend class VansNavigationQueryContext;
    struct TileDataDeleter
    {
        void operator()(unsigned char* data) const noexcept;
    };
    struct OwnedTileData
    {
        std::unique_ptr<unsigned char, TileDataDeleter> data;
        std::size_t size = 0;
        std::uint64_t contentEpoch = 0;
    };
    static std::uint64_t EstimateNativeBytes(const VansNavigationProfileBuildData& profile);
    std::uint64_t EstimateNativeBytesUnderLease() const;
    bool InvalidateTilesUnderLease(const std::vector<VansNavigationTileCell>& cells,std::string& error);
    mutable std::shared_mutex m_AccessMutex;
    mutable std::mutex m_UpdateMutex;
    // Detour only borrows these allocations. Removing a Tile must not free data
    // that a Map write transaction may still need to restore.
    std::vector<OwnedTileData> m_TileData;
    std::size_t m_ResidentTileCount=0;
    std::uint64_t m_TilePayloadBytes=0,m_AssetResidentBytes=0;
    std::map<VansNavigationTileCell,VansNavigationTileLayerData> m_LayerData;
    std::uint64_t m_LayerDataBytes=0;
    float m_RasterOriginMeters=0;
    std::uint64_t m_InputEpoch=0,m_TileContentEpoch=1;
    std::uint64_t m_MaximumResidentBytes=0,m_MaximumResidentTiles=0,m_ServiceScratchBytes=0;
    std::size_t m_MaximumReaderSnapshots=0;
    std::shared_ptr<const VansNavigationTraversalState> m_Traversal;
    dtNavMesh* m_NavMesh = nullptr;
    std::shared_ptr<const VansNavigationMeshAsset> m_Asset;
    std::string m_ProfileId;
    std::vector<VansNavigationCompiledFilter> m_Filters;
    std::uint64_t m_ContentRevision = 1;
};
}
