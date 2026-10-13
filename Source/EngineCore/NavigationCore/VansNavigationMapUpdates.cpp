#include "VansNavigationMap.h"
#include "Detail/VansNavigationNativeTileTransaction.h"
#include "Detail/VansNavigationTileData.h"
#include "Detail/VansNavigationTileCacheLayerCodec.h"
#include "Detail/VansNavigationTraversalState.h"
#include <DetourNavMesh.h>
#include <array>
#include <cstring>
#include <limits>

namespace Vans
{
namespace
{
bool ValidCell(VansNavigationTileCell cell)
{ return cell.x>=-(1<<28) && cell.x<=(1<<28) && cell.z>=-(1<<28) && cell.z<=(1<<28); }

}

std::size_t VansNavigationMap::GetResidentTileCount() const
{
    std::shared_lock<std::shared_mutex> read(m_AccessMutex);
    return m_ResidentTileCount;
}
std::uint64_t VansNavigationMap::GetTraversalRevision() const
{
    const auto traversal=std::atomic_load(&m_Traversal);
    return traversal?traversal->revision:0;
}
std::size_t VansNavigationMap::GetPendingCellCount() const
{
    const auto traversal=std::atomic_load(&m_Traversal);
    return traversal?traversal->dirty.size():0;
}

bool VansNavigationMap::CaptureResidentCells(std::vector<VansNavigationTileCell>& output,std::string& error) const
try
{
    std::shared_lock<std::shared_mutex> read(m_AccessMutex);
    if(!m_NavMesh) { error="Navigation Map is unavailable for resident cell capture";return false; }
    std::vector<VansNavigationTileCell> cells;cells.reserve(m_ResidentTileCount);
    for(int index=0;index<m_NavMesh->getMaxTiles();++index)
    {
        const auto* tile=static_cast<const dtNavMesh*>(m_NavMesh)->getTile(index);
        if(tile && tile->header) cells.push_back({tile->header->x,tile->header->y});
    }
    std::sort(cells.begin(),cells.end());cells.erase(std::unique(cells.begin(),cells.end()),cells.end());
    output=std::move(cells);error.clear();return true;
}
catch(const std::bad_alloc&) { error="Navigation resident cell capture allocation failed";return false; }

bool VansNavigationMap::InvalidateTiles(const std::vector<VansNavigationTileCell>& cells,std::string& error)
try
{
    std::lock_guard<std::mutex> update(m_UpdateMutex);
    std::shared_lock<std::shared_mutex> read(m_AccessMutex);
    return InvalidateTilesUnderLease(cells,error);
}
catch(const std::bad_alloc&) { error="Navigation dirty metadata allocation failed"; return false; }
bool VansNavigationMap::InvalidateTilesUnderLease(const std::vector<VansNavigationTileCell>& cells,std::string& error)
{
    const auto current=std::atomic_load(&m_Traversal);
    if(!m_NavMesh || !current || current->unavailable)
    { error="Navigation Map is unavailable for geometry updates"; return false; }
    if(cells.empty()) { error.clear(); return true; }
    const auto maximumCells=static_cast<std::size_t>(m_NavMesh->getMaxTiles());
    if(cells.size()>maximumCells || m_InputEpoch==std::numeric_limits<std::uint64_t>::max() ||
        current->revision==std::numeric_limits<std::uint64_t>::max())
    { error="Navigation dirty cell count or input epoch exceeds capacity"; return false; }
    for(const auto cell:cells) if(!ValidCell(cell))
    { error="Navigation dirty cell exceeds supported fixed-grid coordinates"; return false; }
    std::uint64_t resident=EstimateNativeBytesUnderLease()+m_ServiceScratchBytes+m_AssetResidentBytes+
        m_Filters.capacity()*sizeof(VansNavigationCompiledFilter);
    for(const auto& filter:m_Filters) resident+=filter.id.capacity()+1;
    const auto candidateCount=(std::min)(maximumCells,current->dirty.size()+cells.size());
    const auto candidateBytes=sizeof(VansNavigationTraversalState)+candidateCount*sizeof(VansNavigationTileInputStamp);
    if(resident+candidateBytes*m_MaximumReaderSnapshots+current->EstimateBytes()+
        cells.size()*sizeof(VansNavigationTileCell)>m_MaximumResidentBytes)
    { error="Navigation dirty metadata exceeds aggregate resident capacity"; return false; }
    auto ordered=cells;
    std::sort(ordered.begin(),ordered.end());
    ordered.erase(std::unique(ordered.begin(),ordered.end()),ordered.end());
    auto candidate=std::make_shared<VansNavigationTraversalState>();
    candidate->revision=current->revision+1;
    candidate->dirty.reserve((std::min)(maximumCells,current->dirty.size()+ordered.size()));
    std::size_t old=0,changed=0;
    while(old<current->dirty.size() || changed<ordered.size())
    {
        if(candidate->dirty.size()==maximumCells)
        { error="Navigation pending dirty cells exceed Map capacity"; return false; }
        if(changed==ordered.size() || (old<current->dirty.size() && current->dirty[old].cell<ordered[changed]))
            candidate->dirty.push_back(current->dirty[old++]);
        else
        {
            if(old<current->dirty.size() && current->dirty[old].cell==ordered[changed]) ++old;
            candidate->dirty.push_back({ordered[changed++],m_InputEpoch+1});
        }
    }
    // Snapshots are borrowed only during a bounded query quantum. Account for
    // every worker reader and the owner replacement, not just current bytes.
    if(resident+candidate->EstimateBytes()*m_MaximumReaderSnapshots+current->EstimateBytes()+
        ordered.capacity()*sizeof(VansNavigationTileCell)>m_MaximumResidentBytes)
    { error="Navigation dirty metadata exceeds aggregate resident capacity"; return false; }
    ++m_InputEpoch;
    std::atomic_store(&m_Traversal,std::shared_ptr<const VansNavigationTraversalState>(std::move(candidate)));
    error.clear(); return true;
}

bool VansNavigationMap::CaptureTileUpdate(VansNavigationTileUpdate& output,std::string& error) const
try
{
    std::lock_guard<std::mutex> update(m_UpdateMutex);
    std::shared_lock<std::shared_mutex> read(m_AccessMutex);
    const auto current=std::atomic_load(&m_Traversal);
    if(!m_NavMesh || !current || current->unavailable)
    { error="Navigation Map is unavailable for a Tile build"; return false; }
    VansNavigationTileUpdate candidate;
    candidate.mapIdentity=m_ContentRevision; candidate.rasterOriginMeters=m_RasterOriginMeters;
    candidate.cells.reserve((std::min)(current->dirty.size(),kMaximumNavigationTileUpdateBatch));
    for(const auto& stamp:current->dirty)
    {
        candidate.cells.push_back(stamp);
        if(candidate.cells.size()==kMaximumNavigationTileUpdateBatch) break;
    }
    output=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation Tile request allocation failed"; return false; }

bool VansNavigationMap::CaptureLayerRead(const VansNavigationTileUpdate& request,std::uint64_t maximumBytes,
    VansNavigationLayerRead& output,std::string& error) const
try
{
    std::lock_guard<std::mutex> update(m_UpdateMutex);
    std::shared_lock<std::shared_mutex> read(m_AccessMutex);
    const auto current=std::atomic_load(&m_Traversal);
    if(!m_NavMesh || !current || current->unavailable || request.mapIdentity!=m_ContentRevision ||
        request.cells.size()>kMaximumNavigationTileUpdateBatch)
    { error="Navigation layer capture belongs to an unavailable or replaced Map"; return false; }
    for(std::size_t index=0;index<request.cells.size();++index)
    {
        const auto& stamp=request.cells[index];
        if(!stamp.inputEpoch || !ValidCell(stamp.cell) || (index && !(request.cells[index-1].cell<stamp.cell)))
        { error="Navigation layer capture cells must be unique, ordered and stamped"; return false; }
    }
    VansNavigationLayerRead candidate;
    candidate.asset=m_Asset; candidate.profileId=m_ProfileId; candidate.maximumBytes=maximumBytes;
    std::uint64_t bytes=output.EstimateBytes()-sizeof(output);
    const auto metadata=request.cells.size()*(sizeof(VansNavigationTileLayerData)+sizeof(VansNavigationTileCell));
    if(bytes<=maximumBytes && metadata<=maximumBytes-bytes)
    {
        bytes+=metadata; candidate.current.reserve(request.cells.size()); candidate.cells.reserve(request.cells.size());
        for(const auto& stamp:request.cells)
        {
            const auto* pending=current->Find(stamp.cell);
            const auto found=m_LayerData.find(stamp.cell);
            if(!pending || pending->inputEpoch!=stamp.inputEpoch) continue;
            candidate.cells.push_back(stamp.cell);
            if(found==m_LayerData.end()) continue;
            const auto cost=found->second.EstimateBytes()-sizeof(VansNavigationTileLayerData);
            if(cost>maximumBytes-bytes)
            {
                // Budget omission must not fall back to an older cooked row.
                candidate.current.push_back({stamp.cell.x,stamp.cell.z,found->second.baseInputHash,{},{},{}});
                continue;
            }
            bytes+=cost; candidate.current.push_back(found->second);
        }
    }
    output=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation layer capture allocation failed"; return false; }

VansNavigationTileCommitResult VansNavigationMap::ApplyTileUpdate(const VansNavigationTileUpdate& request,
    const VansNavigationTileBuildOutput& candidate,std::uint64_t maximumTransactionBytes,std::string& error)
try
{
    std::lock_guard<std::mutex> update(m_UpdateMutex);
    const auto& tiles=candidate.tiles;
    const auto current=std::atomic_load(&m_Traversal);
    if(!current || current->unavailable || !m_NavMesh || request.mapIdentity!=m_ContentRevision)
    { error="Navigation Tile result belongs to an unavailable or replaced Map"; return {VansNavigationTileCommitState::Stale,0}; }
    if(request.cells.empty() || request.cells.size()>kMaximumNavigationTileUpdateBatch || !maximumTransactionBytes)
    { error="Navigation Tile result has an invalid batch capacity"; return {}; }
    if(candidate.EstimateBytes()>maximumTransactionBytes || candidate.EstimateBytes()>m_MaximumResidentBytes)
    { error="Navigation Tile candidate bytes exceed transaction capacity"; return {}; }
    for(std::size_t index=0;index<request.cells.size();++index)
    {
        const auto& stamp=request.cells[index];
        if(!stamp.inputEpoch || !ValidCell(stamp.cell) || (index && !(request.cells[index-1].cell<stamp.cell)))
        { error="Navigation Tile result cells must be unique, ordered and stamped"; return {}; }
    }
    const auto* profile=m_Asset->FindProfile(m_ProfileId);
    std::uint64_t resident=EstimateNativeBytesUnderLease()+m_AssetResidentBytes+m_ServiceScratchBytes+
        m_Filters.capacity()*sizeof(VansNavigationCompiledFilter);
    for(const auto& filter:m_Filters) resident+=filter.id.capacity()+1;
    resident+=current->EstimateBytes()*m_MaximumReaderSnapshots;
    const auto validationBytes=candidate.EstimateBytes()+tiles.size()*128u;
    if(resident>m_MaximumResidentBytes || validationBytes>m_MaximumResidentBytes-resident ||
        validationBytes>maximumTransactionBytes)
    { error="Navigation Tile validation exceeds CPU or aggregate resident capacity"; return {}; }
    const auto validationBudget=(std::min)(maximumTransactionBytes,m_MaximumResidentBytes-resident);
    if(!tiles.empty() && !ValidateNavigationTileSet(tiles,profile->bake,m_Asset->GetBakedAreas(),error)) return {};
    std::uint64_t rowBytes=candidate.layerData.capacity()*sizeof(VansNavigationTileLayerData);
    for(const auto& row:candidate.layerData) rowBytes+=row.EstimateBytes()-sizeof(row);
    if(!ValidateNavigationTileLayerData(candidate.layerData,candidate.baseInputHash,profile->bake,
        m_Asset->GetBakedAreas(),validationBudget-(candidate.EstimateBytes()-rowBytes),error)) return {};
    const auto requested=[&](VansNavigationTileCell cell)
    {
        const auto found=std::lower_bound(request.cells.begin(),request.cells.end(),cell,
            [](const auto& stamp,const auto& key){return stamp.cell<key;});
        return found!=request.cells.end() && found->cell==cell;
    };
    for(const auto& row:candidate.layerData) if(!requested({row.x,row.z}))
    { error="Navigation layer result contains an unrequested cell"; return {}; }
    for(const auto& tile:tiles)
    {
        const VansNavigationTileCell cell{tile.x,tile.z};
        const auto found=std::lower_bound(request.cells.begin(),request.cells.end(),cell,
            [](const auto& stamp,const auto& key){return stamp.cell<key;});
        if(found==request.cells.end() || !(found->cell==cell))
        { error="Navigation Tile result contains an unrequested cell"; return {}; }
    }
    std::vector<VansNavigationTileCell> validCells;
    validCells.reserve(request.cells.size());
    for(const auto& stamp:request.cells)
    {
        const auto* pending=current->Find(stamp.cell);
        if(pending && pending->inputEpoch==stamp.inputEpoch) validCells.push_back(stamp.cell);
    }
    if(validCells.empty())
    { error="Navigation Tile input changed before publication"; return {VansNavigationTileCommitState::Stale,0}; }
    if(current->revision==std::numeric_limits<std::uint64_t>::max() || m_TileContentEpoch==std::numeric_limits<std::uint64_t>::max())
    { error="Navigation Tile content epoch exceeds capacity"; return {}; }
    const auto metadataBytes=2u*sizeof(VansNavigationTraversalState)+current->dirty.size()*sizeof(VansNavigationTileInputStamp)+
        validCells.size()*256u*sizeof(dtTileRef);
    if(candidate.EstimateBytes()+metadataBytes>maximumTransactionBytes)
    { error="Navigation Tile transaction metadata exceeds CPU capacity"; return {}; }
    const auto selected=[&](VansNavigationTileCell cell){return std::binary_search(validCells.begin(),validCells.end(),cell);};
    std::uint64_t preparedLayerBytes=0;
    std::size_t retainedLayerCount=m_LayerData.size();
    for(const auto cell:validCells) if(m_LayerData.count(cell)) --retainedLayerCount;
    for(const auto& row:candidate.layerData) if(selected({row.x,row.z}))
    { preparedLayerBytes+=row.EstimateBytes()+96u; ++retainedLayerCount; }
    std::vector<VansNavigationTileCell> deletedBaseCells;
    deletedBaseCells.reserve(validCells.size());
    for(const auto cell:validCells)
    {
        const auto row=std::lower_bound(profile->layerData.begin(),profile->layerData.end(),cell,
            [](const auto& value,const auto& key){return VansNavigationTileCell{value.x,value.z}<key;});
        if(row==profile->layerData.end() || !(VansNavigationTileCell{row->x,row->z}==cell) ||
            std::any_of(candidate.layerData.begin(),candidate.layerData.end(),[&](const auto& value)
                {return VansNavigationTileCell{value.x,value.z}==cell;})) continue;
        deletedBaseCells.push_back(cell); preparedLayerBytes+=sizeof(VansNavigationTileLayerData)+96u; ++retainedLayerCount;
    }
    if(retainedLayerCount>static_cast<std::size_t>(profile->bake.maximumTiles))
    { error="Navigation layer publication exceeds Map cell capacity"; return {}; }
    if(candidate.EstimateBytes()+metadataBytes+preparedLayerBytes>maximumTransactionBytes)
    { error="Navigation layer publication metadata exceeds CPU capacity"; return {}; }
    auto next=std::make_shared<VansNavigationTraversalState>();
    next->revision=current->revision+1; next->dirty.reserve(current->dirty.size());
    for(const auto& stamp:current->dirty) if(!selected(stamp.cell)) next->dirty.push_back(stamp);
    auto unavailable=std::make_shared<VansNavigationTraversalState>();
    unavailable->revision=next->revision; unavailable->unavailable=true;
    std::unique_lock<std::shared_mutex> write(m_AccessMutex,std::try_to_lock);
    if(!write.owns_lock())
    { error="Navigation Tile publication deferred by an active query"; return {VansNavigationTileCommitState::Deferred,0}; }
    std::vector<dtTileRef> removals;
    removals.reserve(validCells.size()*256u);
    std::uint64_t removedBytes=0;
    for(const auto cell:validCells)
    {
        std::array<const dtMeshTile*,256> layers{};
        const int count=m_NavMesh->getTilesAt(cell.x,cell.z,layers.data(),static_cast<int>(layers.size()));
        for(int index=0;index<count;++index)
        {
            const auto ref=m_NavMesh->getTileRef(layers[index]); removals.push_back(ref);
            removedBytes+=m_TileData[m_NavMesh->decodePolyIdTile(ref)].size;
        }
    }
    std::size_t addedCount=0; std::uint64_t addedBytes=0;
    for(const auto& tile:tiles) if(selected({tile.x,tile.z})) { ++addedCount; addedBytes+=tile.bytes.size(); }
    if(m_ResidentTileCount+addedCount>m_MaximumResidentTiles ||
        m_ResidentTileCount-removals.size()+addedCount>static_cast<std::size_t>(profile->bake.maximumTiles))
    { error="Navigation Tile candidate exceeds resident or native slot capacity"; return {}; }
    const std::uint64_t transactionBytes=candidate.EstimateBytes()+preparedLayerBytes+addedBytes+addedCount*sizeof(OwnedTileData)+
        deletedBaseCells.capacity()*sizeof(VansNavigationTileCell)+
        validCells.capacity()*sizeof(VansNavigationTileCell)+request.cells.capacity()*sizeof(VansNavigationTileInputStamp)+
        removals.capacity()*sizeof(dtTileRef)+next->EstimateBytes()+unavailable->EstimateBytes()+
        EstimateNavigationNativeTileTransactionBytes(removals.size(),addedCount);

    if(transactionBytes>maximumTransactionBytes || resident+transactionBytes>m_MaximumResidentBytes)
    { error="Navigation Tile transaction exceeds CPU or aggregate resident capacity"; return {}; }
    // Allocate every layer node before native mutation. node handles transfer
    // ownership after commit without another fallible allocation.
    std::map<VansNavigationTileCell,VansNavigationTileLayerData> preparedLayers;
    std::uint64_t publishedLayerBytes=0;
    for(const auto& row:candidate.layerData) if(selected({row.x,row.z}))
    {
        const auto inserted=preparedLayers.emplace(VansNavigationTileCell{row.x,row.z},row);
        publishedLayerBytes+=inserted.first->second.EstimateBytes()+96u;
    }
    for(const auto cell:deletedBaseCells)
    {
        const auto inserted=preparedLayers.emplace(cell,VansNavigationTileLayerData{cell.x,cell.z,candidate.baseInputHash,{},{},{}});
        publishedLayerBytes+=inserted.first->second.EstimateBytes()+96u;
    }
    std::vector<OwnedTileData> allocations;
    std::vector<VansNavigationNativeTileBuffer> additions;
    allocations.reserve(addedCount); additions.reserve(addedCount);
    for(const auto& tile:tiles) if(selected({tile.x,tile.z}))
    {
        auto* data=static_cast<unsigned char*>(dtAlloc(tile.bytes.size(),DT_ALLOC_PERM));
        if(!data) { error="Detour could not allocate a navigation Tile candidate"; return {}; }
        allocations.push_back({std::unique_ptr<unsigned char,TileDataDeleter>(data),tile.bytes.size(),m_TileContentEpoch+1});
        std::memcpy(data,tile.bytes.data(),tile.bytes.size()); additions.push_back({data,static_cast<int>(tile.bytes.size())});
    }
    const auto committed=ApplyNavigationNativeTileTransaction(*m_NavMesh,m_ResidentTileCount,removals,additions);
    if(committed.state==VansNavigationNativeTileCommit::RollbackFailed)
    {
        // Destroy native borrowers before releasing either old or new buffers.
        dtFreeNavMesh(m_NavMesh); m_NavMesh=nullptr;
        std::vector<OwnedTileData>().swap(m_TileData); m_ResidentTileCount=0; m_TilePayloadBytes=0;
        m_LayerData.clear(); m_LayerDataBytes=0;
        std::atomic_store(&m_Traversal,std::shared_ptr<const VansNavigationTraversalState>(std::move(unavailable)));
        error="Navigation native Tile rollback failed; Map is unavailable";
        return {VansNavigationTileCommitState::Invalidated,0};
    }
    if(committed.state!=VansNavigationNativeTileCommit::Applied)
    { error="Detour rejected navigation Tile publication; original Tile bytes and refs retained"; return {}; }
    // Publication has no remaining allocation or fallible native operation.
    for(const auto ref:removals) m_TileData[m_NavMesh->decodePolyIdTile(ref)]=OwnedTileData{};
    std::size_t index=0;
    for(const auto& tile:tiles) if(selected({tile.x,tile.z}))
    {
        const auto ref=m_NavMesh->getTileRefAt(tile.x,tile.z,tile.layer);
        m_TileData[m_NavMesh->decodePolyIdTile(ref)]=std::move(allocations[index++]);
    }
    for(const auto cell:validCells)
    {
        const auto old=m_LayerData.find(cell);
        if(old!=m_LayerData.end())
        { m_LayerDataBytes-=old->second.EstimateBytes()+96u; m_LayerData.erase(old); }
    }
    while(!preparedLayers.empty()) m_LayerData.insert(preparedLayers.extract(preparedLayers.begin()));
    m_LayerDataBytes+=publishedLayerBytes;
    ++m_TileContentEpoch; m_ResidentTileCount=m_ResidentTileCount-removals.size()+addedCount;
    m_TilePayloadBytes=m_TilePayloadBytes-removedBytes+addedBytes;
    std::atomic_store(&m_Traversal,std::shared_ptr<const VansNavigationTraversalState>(std::move(next)));
    error.clear(); return {VansNavigationTileCommitState::Applied,validCells.size()};
}
catch(const std::bad_alloc&) { error="Navigation Tile transaction allocation failed"; return {}; }
}
