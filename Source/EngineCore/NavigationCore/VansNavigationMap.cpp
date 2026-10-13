#include "VansNavigationMap.h"
#include "Detail/VansNavigationTileData.h"
#include "Detail/VansNavigationTileCacheLayerCodec.h"
#include "Detail/VansNavigationNativeTileTransaction.h"
#include "Detail/VansNavigationTraversalState.h"
#include <DetourNavMesh.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <cmath>
#include <limits>

namespace Vans
{
void VansNavigationMap::TileDataDeleter::operator()(unsigned char* data) const noexcept
{
    dtFree(data);
}
std::uint64_t VansNavigationMap::EstimateNativeBytes(const VansNavigationProfileBuildData& profile)
{
    std::uint64_t total=sizeof(dtNavMesh)+static_cast<std::uint64_t>(profile.bake.maximumTiles)*(sizeof(dtMeshTile)+sizeof(void*))+
        sizeof(std::vector<OwnedTileData>)+static_cast<std::uint64_t>(profile.bake.maximumTiles)*sizeof(OwnedTileData);
    for(const auto& tile:profile.tiles) total+=tile.bytes.size();
    total+=sizeof(std::map<VansNavigationTileCell,VansNavigationTileLayerData>);
    return total;
}
std::uint64_t VansNavigationMap::EstimateNativeBytesUnderLease() const
{
    if(!m_NavMesh) return 0;
    std::uint64_t total=sizeof(dtNavMesh)+static_cast<std::uint64_t>(m_NavMesh->getMaxTiles())*(sizeof(dtMeshTile)+sizeof(void*))+
        sizeof(m_TileData)+m_TileData.capacity()*sizeof(OwnedTileData)+m_TilePayloadBytes+m_LayerDataBytes+sizeof(m_LayerData);
    return total;
}
VansNavigationMap::~VansNavigationMap()
{
    if (m_NavMesh) dtFreeNavMesh(m_NavMesh);
}

bool VansNavigationMap::HasFilter(const std::string& id) const
{
    std::shared_lock<std::shared_mutex> lease(m_AccessMutex);
    return m_NavMesh && std::any_of(m_Filters.begin(),m_Filters.end(),[&](const auto& filter){return filter.id==id;});
}
bool VansNavigationMap::HasLink(VansNavigationLinkKey key) const
{
    std::shared_lock<std::shared_mutex> lease(m_AccessMutex);
    if(!key.value || !m_NavMesh) return false;
    for(int slot=0;slot<m_NavMesh->getMaxTiles();++slot)
    {
        const auto* tile=static_cast<const dtNavMesh*>(m_NavMesh)->getTile(slot);
        if(!tile || !tile->header) continue;
        for(int index=0;index<tile->header->offMeshConCount;++index)
            if(tile->offMeshCons[index].userId==key.value) return true;
    }
    return false;
}
bool VansNavigationMap::CaptureDebugGeometry(const VansNavigationDebugView& view,const std::string& filterId,
    VansNavigationDebugGeometry& output,std::string& error) const
try
{
    if(!view.maximumTiles || view.maximumTiles>kMaximumNavigationDebugTiles ||
        !view.maximumTriangles || view.maximumTriangles>kMaximumNavigationDebugTriangles || view.maximumLinks>kMaximumNavigationDebugLinks)
    { error="Navigation debug view capacity is invalid"; return false; }
    for(const auto& plane:view.planes)
        if(!std::isfinite(plane.x) || !std::isfinite(plane.y) || !std::isfinite(plane.z) || !std::isfinite(plane.w) ||
            std::abs(glm::length(glm::vec3(plane))-1.f)>.01f)
        { error="Navigation debug view requires finite unit frustum planes"; return false; }
    std::shared_lock<std::shared_mutex> lease(m_AccessMutex);
    if(!m_NavMesh) { error="Navigation debug Map is unavailable"; return false; }
    const auto filter=std::find_if(m_Filters.begin(),m_Filters.end(),[&](const auto& value){return value.id==filterId;});
    if(filter==m_Filters.end()) { error="Navigation debug Filter is unavailable: "+filterId; return false; }
    const auto tolerance=m_Asset->FindProfile(m_ProfileId)->bake.cellSize*.01f;
    const auto visible=[&](const glm::vec3& minimum,const glm::vec3& maximum)
    {
        for(const auto& plane:view.planes)
        {
            const glm::vec3 farthest(plane.x>=0?maximum.x:minimum.x,plane.y>=0?maximum.y:minimum.y,plane.z>=0?maximum.z:minimum.z);
            if(glm::dot(glm::vec3(plane),farthest)+plane.w<0) return false;
        }
        return true;
    };
    VansNavigationDebugGeometry candidate;
    candidate.triangles.reserve(view.maximumTriangles);
    bool linksLimited=false;
    for(int slot=0;slot<m_NavMesh->getMaxTiles();++slot)
    {
        const auto* tile=static_cast<const dtNavMesh*>(m_NavMesh)->getTile(slot);
        if(!tile || !tile->header) continue;
        const auto* h=tile->header;
        const glm::vec3 minimum(h->bmin[0],h->bmin[1],h->bmin[2]),maximum(h->bmax[0],h->bmax[1],h->bmax[2]);
        if(!visible(minimum,maximum)) continue;
        if(candidate.tileCount==view.maximumTiles) { candidate.limited=true; break; }
        ++candidate.tileCount;
        if(view.maximumLinks && !linksLimited) for(int i=0;i<h->offMeshConCount;++i)
        {
            const auto& connection=tile->offMeshCons[i]; const auto& polygon=tile->polys[connection.poly];
            const auto* start=tile->verts+polygon.verts[0]*3; const auto* end=tile->verts+polygon.verts[1]*3;
            VansNavigationDebugLink value; value.key={connection.userId}; value.radiusMeters=connection.rad;
            value.positions={glm::vec3(start[0],start[1],start[2]),glm::vec3(end[0],end[1],end[2])};
            value.bidirectional=(connection.flags&DT_OFFMESH_CON_BIDIR)!=0; value.areaAllowed=filter->allowed[polygon.getArea()];
            if(!visible(glm::min(value.positions[0],value.positions[1]),glm::max(value.positions[0],value.positions[1]))) continue;
            if(candidate.links.size()==view.maximumLinks) { linksLimited=true; break; }
            if(candidate.links.empty()) candidate.links.reserve(view.maximumLinks);
            candidate.links.push_back(value);
        }
        for(int polygon=0;polygon<h->polyCount && !candidate.limited;++polygon)
        {
            const auto& poly=tile->polys[polygon];
            if(poly.getType()!=DT_POLYTYPE_GROUND) continue;
            const auto& detail=tile->detailMeshes[polygon];
            for(unsigned int index=0;index<detail.triCount;++index)
            {
                const auto* triangle=&tile->detailTris[(detail.triBase+index)*4];
                VansNavigationDebugTriangle value; value.area=poly.getArea(); value.traversable=filter->allowed[value.area];
                for(int corner=0;corner<3;++corner)
                {
                    const auto vertex=triangle[corner];
                    const auto* p=vertex<poly.vertCount?&tile->verts[poly.verts[vertex]*3]:
                        &tile->detailVerts[(detail.vertBase+vertex-poly.vertCount)*3];
                    value.positions[corner]=glm::vec3(p[0],p[1],p[2]);
                }
                for(int edge=0;edge<3;++edge)
                {
                    if(!((triangle[3]>>(edge*2)) & DT_DETAIL_EDGE_BOUNDARY)) continue;
                    const auto& a=value.positions[edge]; const auto& b=value.positions[(edge+1)%3];
                    value.tileEdges[edge]=(std::abs(a.x-minimum.x)<=tolerance && std::abs(b.x-minimum.x)<=tolerance) ||
                        (std::abs(a.x-maximum.x)<=tolerance && std::abs(b.x-maximum.x)<=tolerance) ||
                        (std::abs(a.z-minimum.z)<=tolerance && std::abs(b.z-minimum.z)<=tolerance) ||
                        (std::abs(a.z-maximum.z)<=tolerance && std::abs(b.z-maximum.z)<=tolerance);
                }
                if(!visible(glm::min(value.positions[0],glm::min(value.positions[1],value.positions[2])),
                    glm::max(value.positions[0],glm::max(value.positions[1],value.positions[2])))) continue;
                if(candidate.triangles.size()==view.maximumTriangles) { candidate.limited=true; break; }
                candidate.triangles.push_back(value);
            }
        }
        if(candidate.limited) break;
    }
    candidate.limited|=linksLimited;
    output=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation debug snapshot allocation failed"; return false; }
bool VansNavigationMap::IsReady() const
{
    std::shared_lock<std::shared_mutex> lease(m_AccessMutex);
    return m_NavMesh != nullptr;
}
std::shared_ptr<const VansNavigationMeshAsset> VansNavigationMap::GetAsset() const
{
    std::shared_lock<std::shared_mutex> lease(m_AccessMutex);
    return m_Asset;
}
bool VansNavigationMap::Initialize(std::shared_ptr<const VansNavigationMeshAsset> asset,
	const VansNavigationSettings& settings, const std::string& profileId,
	std::string& error)
try
{
    if (!ValidateNavigationSettings(settings, error)) return false;
    if (!asset || !asset->IsReady())
	{
		error = "Navigation mesh contains no Detour data";
		return false;
    }
    const auto* profile = asset->FindProfile(profileId);
    VansNavigationBakeSettings currentBake;
    if (!profile || !ResolveNavigationBakeSettings(settings, asset->GetMapId(), profileId, currentBake, error))
    { if (error.empty()) error = "Navigation asset Profile is unavailable: " + profileId; return false; }
    if (profile->bakeSettingsHash != HashNavigationBakeSettings(currentBake))
    { error = "Navigation asset is Stale: Map/Profile build inputs changed"; return false; }
    auto profileToPublish=profileId;
    // 资产只验证稳定 Area 编码；查询目录始终来自当前配置，删除/改 ID 必须重烘焙。
    for (const auto& baked : asset->GetBakedAreas().definitions)
    {
        const auto* current = FindNavigationAreaByName(settings.areas, baked.name);
        if (!current || current->id != baked.id)
        { error = "Navigation asset is Stale: Area removed or its ID changed: " + baked.name; return false; }
    }
    std::vector<VansNavigationCompiledFilter> filters;
    filters.reserve(settings.queryFilters.size());
    for (const auto& definition : settings.queryFilters)
    {
        VansNavigationCompiledFilter compiled;
        if (!CompileNavigationFilter(settings.areas, definition, compiled, error)) return false;
        filters.push_back(std::move(compiled));
    }
    const auto& bake = profile->bake;
    if(!profile->baseInputHash || (!bake.buildTileCacheLayers && !profile->layerData.empty()))
    { error="Navigation base layer policy/fingerprint is invalid"; return false; }
    const auto serviceScratchBytes=EstimateNavigationQueryBytes(settings.query)+
        EstimateNavigationAvoidanceBytes(settings.query.maximumActiveAgents,settings.avoidance);
    std::uint64_t peakBytes=serviceScratchBytes+asset->EstimateResidentBytes()+
        EstimateInitializationBytes(*asset,settings,profileId);
    std::uint64_t peakTiles=profile->tiles.size();
    {
        std::shared_lock<std::shared_mutex> lease(m_AccessMutex);
        if(m_Asset)
        {
            peakBytes+=EstimateNativeBytesUnderLease()+m_Filters.capacity()*sizeof(VansNavigationCompiledFilter);
            const auto traversal=std::atomic_load(&m_Traversal);
            if(traversal) peakBytes+=traversal->EstimateBytes()*m_MaximumReaderSnapshots;
            for(const auto& filter:m_Filters) peakBytes+=filter.id.capacity()+1;
            peakTiles+=m_ResidentTileCount;
            if(m_Asset!=asset) peakBytes+=m_Asset->EstimateResidentBytes();
        }
    }
    if (peakTiles > static_cast<std::uint64_t>(settings.maximumResidentTiles) ||
        peakBytes > static_cast<std::uint64_t>(settings.maximumResidentBytes))
    { error = "Navigation Map exceeds aggregate resident capacity"; return false; }
    VansNavigationProfileBuildData loaded;
    if(!asset->ReadProfile(profileId,loaded,error)) return false;
    std::uint64_t rowBytes=loaded.layerData.capacity()*sizeof(VansNavigationTileLayerData);
    for(const auto& row:loaded.layerData) rowBytes+=row.EstimateBytes()-sizeof(row);
    const auto nonLayerBytes=asset->EstimateResidentBytes()+loaded.EstimateBytes()-rowBytes;
    if(nonLayerBytes>static_cast<std::uint64_t>(settings.maximumResidentBytes) ||
        !ValidateNavigationTileLayerData(loaded.layerData,loaded.baseInputHash,bake,asset->GetBakedAreas(),
            static_cast<std::uint64_t>(settings.maximumResidentBytes)-nonLayerBytes,error) ||
        !ValidateNavigationTileSet(loaded.tiles,bake,asset->GetBakedAreas(),error)) return false;
    // Keep the borrowed data alive until after the candidate mesh is destroyed
    // on every failure path. Reserve before any native registration.
    std::map<VansNavigationTileCell,VansNavigationTileLayerData> layerData;
    std::uint64_t layerDataBytes=0;
    // 不可变资产持有基础压缩层，Map 只拥有局部提交和删除事实。
    std::vector<OwnedTileData> tileData;
    tileData.reserve(loaded.tiles.size());
    std::vector<OwnedTileData> ownedSlots(static_cast<std::size_t>(bake.maximumTiles));
    auto traversal=std::make_shared<VansNavigationTraversalState>();
    std::vector<VansNavigationNativeTileBuffer> additions;
    additions.reserve(loaded.tiles.size());
    std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)>
        navMesh(dtAllocNavMesh(), &dtFreeNavMesh);
    if (!navMesh) { error = "Detour could not allocate navigation map"; return false; }
    dtNavMeshParams params{};
    params.orig[0] = bake.tileOrigin.x; params.orig[1] = bake.tileOrigin.y; params.orig[2] = bake.tileOrigin.z;
    params.tileWidth = params.tileHeight = bake.cellSize * bake.tileSizeCells;
    params.maxTiles = bake.maximumTiles; params.maxPolys = bake.maximumPolygonsPerTile;
    if (dtStatusFailed(navMesh->init(&params)))
    { error = "Detour rejected navigation map layout"; return false; }
    for (const auto& tile : loaded.tiles)
    {
        unsigned char* data = static_cast<unsigned char*>(dtAlloc(tile.bytes.size(), DT_ALLOC_PERM));
        if (!data) { error = "Detour could not allocate navigation Tile"; return false; }
        OwnedTileData allocation{std::unique_ptr<unsigned char,TileDataDeleter>(data),tile.bytes.size()};
        tileData.push_back(std::move(allocation));
        std::memcpy(data, tile.bytes.data(), tile.bytes.size());
        additions.push_back({data,static_cast<int>(tile.bytes.size())});
    }
    if(!additions.empty() && ApplyNavigationNativeTileTransaction(*navMesh,0,{},additions).state!=VansNavigationNativeTileCommit::Applied)
    { error = "Detour rejected navigation Tile registration"; return false; }
    std::uint64_t payloadBytes=0;
    for(std::size_t index=0;index<loaded.tiles.size();++index)
    {
        const auto& tile=loaded.tiles[index];
        const auto slot=navMesh->decodePolyIdTile(navMesh->getTileRefAt(tile.x,tile.z,tile.layer));
        tileData[index].contentEpoch=1;
        payloadBytes+=tileData[index].size;
        ownedSlots[slot]=std::move(tileData[index]);
    }

	// 候选数据完整建立后再替换当前实例；失败不得破坏已有有效资源。
    std::lock_guard<std::mutex> updateLease(m_UpdateMutex);
    std::unique_lock<std::shared_mutex> writeLease(m_AccessMutex, std::try_to_lock);
    if (!writeLease.owns_lock())
    { error = "Navigation map write deferred by an active query"; return false; }
    const auto previousTraversal=std::atomic_load(&m_Traversal);
    if(m_ContentRevision==std::numeric_limits<std::uint64_t>::max() ||
        (previousTraversal && previousTraversal->revision==std::numeric_limits<std::uint64_t>::max()))
    { error="Navigation Map identity or traversal epoch exceeds capacity"; return false; }
    traversal->revision=previousTraversal?previousTraversal->revision+1:1;
    if (m_NavMesh) dtFreeNavMesh(m_NavMesh);
    m_NavMesh = navMesh.release();
    m_TileData = std::move(ownedSlots);
    m_ResidentTileCount=loaded.tiles.size(); m_TilePayloadBytes=payloadBytes;
    m_LayerData=std::move(layerData); m_LayerDataBytes=layerDataBytes;
    dtMeshHeader firstHeader{};
    std::memcpy(&firstHeader,loaded.tiles.front().bytes.data(),sizeof(firstHeader));
    m_RasterOriginMeters=firstHeader.bmin[1]; m_TileContentEpoch=1;
    m_MaximumResidentBytes=settings.maximumResidentBytes; m_MaximumResidentTiles=settings.maximumResidentTiles;
    m_ServiceScratchBytes=serviceScratchBytes;
    m_MaximumReaderSnapshots=static_cast<std::size_t>(settings.query.maximumActiveQueries)+2u;
    m_Asset = std::move(asset);
    m_AssetResidentBytes=m_Asset->EstimateResidentBytes();
    m_ProfileId = std::move(profileToPublish);
    m_Filters = std::move(filters);
    ++m_ContentRevision;
    std::atomic_store(&m_Traversal,std::shared_ptr<const VansNavigationTraversalState>(std::move(traversal)));
	error.clear();
	return true;
}
catch(const std::bad_alloc&) { error="Navigation Map candidate allocation failed"; return false; }

std::string VansNavigationMap::GetProfileId() const
{
    std::shared_lock<std::shared_mutex> lease(m_AccessMutex);
    return m_ProfileId;
}
std::optional<VansNavigationBakeSettings> VansNavigationMap::GetBakeSettings() const
{
    std::shared_lock<std::shared_mutex> lease(m_AccessMutex);
    if (!m_Asset) return std::nullopt;
    return m_Asset->FindProfile(m_ProfileId)->bake;
}
std::size_t VansNavigationMap::EstimateResidentBytes() const
{
    std::shared_lock<std::shared_mutex> lease(m_AccessMutex);
    if (!m_Asset) return 0;
    std::size_t total = EstimateNativeBytesUnderLease()+m_Filters.capacity()*sizeof(VansNavigationCompiledFilter);
    for(const auto& filter:m_Filters) total+=filter.id.capacity()+1;
    const auto traversal=std::atomic_load(&m_Traversal);
    if(traversal) total+=traversal->EstimateBytes()*m_MaximumReaderSnapshots;
    return total;
}
std::uint64_t VansNavigationMap::EstimateInitializationBytes(const VansNavigationMeshAsset& asset,
    const VansNavigationSettings& settings,const std::string& profileId)
{
    const auto* profile=asset.FindProfile(profileId);
    if(!profile) return 0;
    const auto* config=FindNavigationMap(settings,asset.GetMapId()); if(!config) return 0;
    std::uint64_t bytes=EstimateNativeBytes(*profile)+asset.EstimateProfileReadBytes(profileId)+settings.queryFilters.size()*sizeof(VansNavigationCompiledFilter)+
        EstimateNavigationNativeTileTransactionBytes(0,profile->tiles.size())+profile->tiles.size()*sizeof(OwnedTileData)+
        sizeof(VansNavigationTraversalState)*(static_cast<std::uint64_t>(settings.query.maximumActiveQueries)+2u);
    for(const auto& filter:settings.queryFilters) bytes+=filter.id.size()+32;
    return bytes;
}

}
