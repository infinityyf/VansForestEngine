#pragma once

#include <DetourNavMesh.h>
#include <cstddef>
#include <vector>

namespace Vans
{
// Internal native boundary. The Map owns every allocation and the writer lease;
// callers validate cooked layout before borrowing these buffers into Detour.
struct VansNavigationNativeTileBuffer
{
    unsigned char* data=nullptr;
    int size=0;
};
enum class VansNavigationNativeTileCommit { Applied,Rejected,RolledBack,RollbackFailed };
struct VansNavigationNativeTileCommitResult
{
    VansNavigationNativeTileCommit state=VansNavigationNativeTileCommit::Rejected;
    dtStatus status=DT_FAILURE;
};
std::size_t EstimateNavigationNativeTileTransactionBytes(std::size_t removedCount,std::size_t addedCount);
VansNavigationNativeTileCommitResult ApplyNavigationNativeTileTransaction(dtNavMesh& mesh,std::size_t residentTileCount,
    const std::vector<dtTileRef>& removals,const std::vector<VansNavigationNativeTileBuffer>& additions);
}
