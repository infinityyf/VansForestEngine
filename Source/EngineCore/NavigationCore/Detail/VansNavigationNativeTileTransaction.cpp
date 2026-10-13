#include "VansNavigationNativeTileTransaction.h"
#include <algorithm>
#include <new>

namespace Vans
{
namespace
{
struct RemovedTile
{
    dtTileRef reference=0;
    unsigned char* data=nullptr;
    int size=0;
};
}
std::size_t EstimateNavigationNativeTileTransactionBytes(std::size_t removedCount,std::size_t addedCount)
{
    return removedCount*(sizeof(RemovedTile)+sizeof(dtTileRef)*2u)+
        addedCount*(sizeof(VansNavigationNativeTileBuffer)+sizeof(dtTileRef))+
        sizeof(std::vector<RemovedTile>)+sizeof(std::vector<dtTileRef>)*3u+
        sizeof(std::vector<VansNavigationNativeTileBuffer>);
}
VansNavigationNativeTileCommitResult ApplyNavigationNativeTileTransaction(dtNavMesh& mesh,std::size_t residentTileCount,
    const std::vector<dtTileRef>& removals,const std::vector<VansNavigationNativeTileBuffer>& additions)
try
{
    if(residentTileCount>static_cast<std::size_t>(mesh.getMaxTiles()) || removals.size()>residentTileCount ||
        additions.size()>static_cast<std::size_t>(mesh.getMaxTiles())) return {};
    auto unique=removals;
    std::sort(unique.begin(),unique.end());
    if(std::adjacent_find(unique.begin(),unique.end())!=unique.end()) return {};
    std::vector<RemovedTile> before;
    before.reserve(removals.size());
    for(const auto reference:removals)
    {
        const auto* tile=mesh.getTileByRef(reference);
        if(!tile || !tile->header || !tile->data || (tile->flags & DT_TILE_FREE_DATA)) return {};
        before.push_back({reference,tile->data,tile->dataSize});
    }
    for(const auto& tile:additions)
        if(!tile.data || tile.size<static_cast<int>(sizeof(dtMeshHeader))) return {};
    if(residentTileCount-removals.size()+additions.size()>static_cast<std::size_t>(mesh.getMaxTiles()))
        return {VansNavigationNativeTileCommit::Rejected,DT_FAILURE|DT_OUT_OF_MEMORY};
    std::vector<dtTileRef> added;
    added.reserve(additions.size());
    std::size_t removed=0;
    const auto rollback=[&](dtStatus failure)
    {
        bool restored=true;
        for(auto iterator=added.rbegin();iterator!=added.rend();++iterator)
            if(dtStatusFailed(mesh.removeTile(*iterator,nullptr,nullptr))) restored=false;
        for(std::size_t index=0;index<removed;++index)
        {
            const auto& old=before[index];
            if(dtStatusFailed(mesh.addTile(old.data,old.size,0,old.reference,nullptr))) restored=false;
        }
        return VansNavigationNativeTileCommitResult{restored?VansNavigationNativeTileCommit::RolledBack:
            VansNavigationNativeTileCommit::RollbackFailed,failure};
    };
    for(const auto& old:before)
    {
        const auto status=mesh.removeTile(old.reference,nullptr,nullptr);
        if(dtStatusFailed(status)) return rollback(status);
        ++removed;
    }
    for(const auto& tile:additions)
    {
        dtTileRef reference=0;
        const auto status=mesh.addTile(tile.data,tile.size,0,0,&reference);
        if(dtStatusFailed(status)) return rollback(status);
        added.push_back(reference);
    }
    return {VansNavigationNativeTileCommit::Applied,DT_SUCCESS};
}
catch(const std::bad_alloc&)
{ return {VansNavigationNativeTileCommit::Rejected,DT_FAILURE|DT_OUT_OF_MEMORY}; }
}
