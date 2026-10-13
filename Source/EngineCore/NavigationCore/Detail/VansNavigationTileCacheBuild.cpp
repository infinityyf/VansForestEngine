#include "VansNavigationTileCacheBuild.h"
#include "VansNavigationTileData.h"
#include "VansNavigationTileCacheLayerCodec.h"
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMesh.h>
#include <Recast.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <atomic>
#include <map>

namespace Vans
{
namespace
{
bool Cancelled(const VansNavigationBuildControl* control,std::string& error)
{
    if(!control || !control->IsCancelled()) return false;
    error="Navigation Carve build was cancelled"; return true;
}
VansNavigationObstacleGeometry Expanded(const VansNavigationObstacleGeometry& source,const VansNavigationBakeSettings& bake)
{
    auto shape=source;
    // Carve 表达实体占据空间；导航多边形表达 Agent 的足底中心。
    if(shape.kind==VansNavigationObstacleGeometryKind::Cylinder)
    {
        shape.radius+=bake.agentRadius;
        shape.center.y-=bake.agentHeight;
        shape.height+=bake.agentHeight;
    }
    else
    {
        shape.halfExtents+=glm::vec3(bake.agentRadius,bake.agentHeight*.5f,bake.agentRadius);
        shape.center.y-=bake.agentHeight*.5f;
    }
    return shape;
}
bool Overlaps(const glm::vec3& a,const glm::vec3& b,const glm::vec3& c,const glm::vec3& d)
{ return glm::all(glm::lessThanEqual(a,d)) && glm::all(glm::lessThanEqual(c,b)); }

class BuildAllocator final : public dtTileCacheAlloc
{
    struct alignas(std::max_align_t) Header { std::size_t bytes; };
public:
    explicit BuildAllocator(std::uint64_t capacity):capacity_(capacity) {}
    void SetCapacity(std::uint64_t capacity) { capacity_=capacity; }
    void* alloc(std::size_t size) override
    {
        if(size>capacity_-live_ || sizeof(Header)>capacity_-live_-size) return nullptr;
        auto* header=static_cast<Header*>(dtAlloc(size+sizeof(Header),DT_ALLOC_TEMP));
        if(!header) return nullptr;
        header->bytes=size+sizeof(Header); live_+=header->bytes;
        return header+1;
    }
    void free(void* pointer) override
    {
        if(!pointer) return;
        auto* header=static_cast<Header*>(pointer)-1; live_-=header->bytes; dtFree(header);
    }
private:
    std::uint64_t capacity_,live_=0;
};
class AreaProcess final : public dtTileCacheMeshProcess
{
public:
    AreaProcess(const VansNavigationAreaSettings& areas,int maximumPolygons):areas_(areas),maximumPolygons_(maximumPolygons) {}
    bool ConfigureLinks(const std::vector<VansNavigationLinkGeometry>& links,const VansNavigationBakeSettings& bake,int x,int z,std::string& error)
    { return links_.Compile(links,areas_,bake,x,z,error); }
    void process(dtNavMeshCreateParams* params,unsigned char* areas,unsigned short* flags) override
    {
        if(params->polyCount>maximumPolygons_) failed=true;
        for(int i=0;i<params->polyCount;++i)
        {
            if(areas[i]==0 || !FindNavigationAreaById(areas_,static_cast<std::uint8_t>(areas[i]-1))) failed=true;
            areas[i]=static_cast<unsigned char>(areas[i]-1); flags[i]=1;
        }
        params->buildBvTree=true;
        links_.Apply(*params);
    }
    bool failed=false;
    std::uint64_t Bytes() const
    {
        std::uint64_t bytes=areas_.definitions.capacity()*sizeof(VansNavigationAreaDefinition)+areas_.defaultArea.capacity()+1;
        for(const auto& area:areas_.definitions) bytes+=area.name.capacity()+1;
        return bytes+links_.Bytes();
    }
private:
    VansNavigationAreaSettings areas_; int maximumPolygons_;
    VansNavigationNativeLinks links_;
};
bool NativeRangeFits(const VansNavigationObstacleGeometry& shape,const VansNavigationBakeSettings& bake)
{
    glm::vec3 low,high; NavigationObstacleGeometryBounds(shape,low,high);
    const double width=static_cast<double>(bake.cellSize)*bake.tileSizeCells;
    const double cellsX=std::floor((static_cast<double>(high.x)-bake.tileOrigin.x)/width)-
        std::floor((static_cast<double>(low.x)-bake.tileOrigin.x)/width)+1;
    const double cellsZ=std::floor((static_cast<double>(high.z)-bake.tileOrigin.z)/width)-
        std::floor((static_cast<double>(low.z)-bake.tileOrigin.z)/width)+1;
    return std::isfinite(cellsX) && std::isfinite(cellsZ) && cellsX>0 && cellsZ>0 &&
        cellsX<=DT_MAX_TOUCHED_TILES && cellsZ<=DT_MAX_TOUCHED_TILES && cellsX*cellsZ<=DT_MAX_TOUCHED_TILES;
}
}

namespace
{
bool SameShape(const VansNavigationObstacleGeometry& a,const VansNavigationObstacleGeometry& b)
{
    return a.id==b.id && a.kind==b.kind && a.center==b.center && a.halfExtents==b.halfExtents &&
        a.radius==b.radius && a.height==b.height && a.yawRadians==b.yawRadians;
}
struct NativeCell
{
    struct Obstacle { VansNavigationObstacleGeometry shape; dtObstacleRef ref=0; };
    VansNavigationTileCacheLayerCompressor compressor;
    BuildAllocator allocator;
    AreaProcess process;
    std::vector<Obstacle> active;
    std::unique_ptr<dtTileCache,decltype(&dtFreeTileCache)> cache{nullptr,&dtFreeTileCache};
    std::unique_ptr<dtNavMesh,decltype(&dtFreeNavMesh)> staging{nullptr,&dtFreeNavMesh};
    NativeCell(const VansNavigationAreaSettings& areas,int polygons,std::uint64_t capacity):
        allocator(capacity),process(areas,polygons) {}
    bool Initialize(std::vector<VansNavigationTileCacheLayerPayload>& layers,const VansNavigationBakeSettings& bake,
        std::size_t obstacles,std::string& error)
    {
        dtTileCacheParams params{};
        rcVcopy(params.orig,&bake.tileOrigin.x); params.cs=bake.cellSize; params.ch=bake.cellHeight;
        params.width=params.height=bake.tileSizeCells; params.walkableHeight=bake.agentHeight;
        params.walkableRadius=bake.agentRadius; params.walkableClimb=bake.agentMaxClimb;
        params.maxSimplificationError=bake.edgeMaxError; params.maxTiles=1;
        while(params.maxTiles<static_cast<int>(layers.size())) params.maxTiles*=2;
        params.maxObstacles=static_cast<int>((std::max)(std::size_t(1),obstacles));
        dtNavMeshParams nav{};
        rcVcopy(nav.orig,params.orig); nav.tileWidth=nav.tileHeight=bake.cellSize*bake.tileSizeCells;
        nav.maxTiles=params.maxTiles; nav.maxPolys=bake.maximumPolygonsPerTile;
        cache.reset(dtAllocTileCache()); staging.reset(dtAllocNavMesh());
        if(!cache || !staging || dtStatusFailed(cache->init(&params,&allocator,&compressor,&process)) ||
            dtStatusFailed(staging->init(&nav)))
        { error="Navigation TileCache/staging initialization failed"; return false; }
        for(auto& layer:layers)
        {
            // 不可变 compressed bytes 的 owner 比 native cache 长寿；native 不取得释放权。
            if(dtStatusFailed(cache->addTile(layer.bytes.data(),static_cast<int>(layer.bytes.size()),0,nullptr)))
            { error="Navigation TileCache layer registration failed"; return false; }
        }
        return true;
    }
    bool Synchronize(const std::vector<VansNavigationObstacleGeometry>& desired,const VansNavigationBuildControl* control,
        bool initialize,std::string& error)
    {
        std::size_t queued=0;
        const auto drain=[&]()
        {
            bool done=false;
            for(int pass=0;pass<=cache->getTileCount() && !done;++pass)
            {
                if(Cancelled(control,error)) return false;
                if(dtStatusFailed(cache->update(0,staging.get(),&done)) || process.failed)
                { error="Navigation TileCache update failed or exceeded Area/polygon capacity"; return false; }
            }
            if(!done) { error="Navigation TileCache exceeded its bounded update queue"; return false; }
            queued=0; return true;
        };
        // 先清空 removal batches，确保 pool 和 64 request queue 均释放后再添加。
        for(const auto& old:active)
        {
            const auto next=std::lower_bound(desired.begin(),desired.end(),old.shape.id,
                [](const auto& shape,const auto& id){return shape.id<id;});
            if(next!=desired.end() && SameShape(old.shape,*next)) continue;
            if(Cancelled(control,error)) return false;
            if(dtStatusFailed(cache->removeObstacle(old.ref)))
            { error="Navigation TileCache obstacle removal failed"; return false; }
            if(++queued==64 && !drain()) return false;
        }
        if(queued && !drain()) return false;
        std::vector<Obstacle> next; next.reserve(desired.size());
        for(const auto& shape:desired)
        {
            const auto old=std::lower_bound(active.begin(),active.end(),shape.id,
                [](const auto& obstacle,const auto& id){return obstacle.shape.id<id;});
            if(old!=active.end() && SameShape(old->shape,shape)) { next.push_back(*old); continue; }
            if(Cancelled(control,error)) return false;
            dtObstacleRef ref=0;
            const auto status=shape.kind==VansNavigationObstacleGeometryKind::Cylinder?
                cache->addObstacle(&shape.center.x,shape.radius,shape.height,&ref):
                cache->addBoxObstacle(&shape.center.x,&shape.halfExtents.x,shape.yawRadians,&ref);
            if(dtStatusFailed(status))
            { error="Navigation TileCache obstacle queue rejected a validated shape"; return false; }
            next.push_back({shape,ref});
            if(++queued==64 && !drain()) return false;
        }
        if(queued && !drain()) return false;
        if(initialize) for(int i=0;i<cache->getTileCount();++i)
        {
            const auto* layer=cache->getTile(i);
            if(!layer->header) continue;
            if(Cancelled(control,error)) return false;
            if(dtStatusFailed(cache->buildNavMeshTile(cache->getTileRef(layer),staging.get())) || process.failed)
            { error="Navigation TileCache candidate generation failed"; return false; }
        }
        active=std::move(next); return true;
    }
    std::uint64_t Bytes() const
    {
        std::uint64_t result=sizeof(NativeCell)+process.Bytes()+active.capacity()*sizeof(Obstacle);
        for(const auto& obstacle:active) result+=obstacle.shape.id.capacity()+1;
        // native pool/index allocation，加对齐与 allocator 元数据上界。
        if(cache)
        {
            const auto* p=cache->getParams();
            result+=sizeof(dtTileCache)+static_cast<std::uint64_t>(p->maxTiles)*(sizeof(dtCompressedTile)+sizeof(void*)*2+32)+
                static_cast<std::uint64_t>(p->maxObstacles)*(sizeof(dtTileCacheObstacle)+32);
        }
        if(staging)
        {
            result+=sizeof(dtNavMesh)+static_cast<std::uint64_t>(staging->getMaxTiles())*(sizeof(dtMeshTile)+sizeof(void*)*2+32);
            for(int i=0;i<staging->getMaxTiles();++i)
            {
                const auto* tile=static_cast<const dtNavMesh*>(staging.get())->getTile(i);
                if(tile->header) result+=static_cast<std::uint64_t>(tile->dataSize)+32;
            }
        }
        return result;
    }
};
}
struct VansNavigationCachedCell
{
    // declaration 顺序保证 native 先销毁，再释放其借用的 layer bytes。
    std::vector<VansNavigationTileCacheLayerPayload> layers;
    std::vector<VansNavigationTilePayload> baseTiles;
    glm::vec3 low{0},high{0};
    bool layersReady=false,baseReady=false;
    std::uint64_t lastUsed=0;
    std::unique_ptr<NativeCell> native;
    std::uint64_t Bytes() const
    {
        std::uint64_t result=sizeof(VansNavigationCachedCell)+128+
            layers.capacity()*sizeof(VansNavigationTileCacheLayerPayload)+baseTiles.capacity()*sizeof(VansNavigationTilePayload);
        for(const auto& layer:layers) result+=layer.bytes.capacity();
        for(const auto& tile:baseTiles) result+=tile.bytes.capacity();
        if(native) result+=native->Bytes();
        return result;
    }
};
namespace
{
VansNavigationCachedBuildResult BuildCell(VansNavigationCachedCell& cell,int x,int z,
    const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
    const std::vector<VansNavigationObstacleGeometry>& obstacles,const std::vector<VansNavigationLinkGeometry>& links,std::uint64_t maximumBytes,
    const VansNavigationBuildControl* control,std::vector<VansNavigationTilePayload>& output,std::string& error)
{
    using Result=VansNavigationCachedBuildResult;
    if(Cancelled(control,error)) return Result::Failed;
    if(!cell.layersReady)
    {
        if(!obstacles.empty() || !cell.baseReady) return Result::Missing;
        std::uint64_t bytes=0; for(const auto& tile:cell.baseTiles) bytes+=tile.bytes.size()+sizeof(tile);
        if(bytes>=maximumBytes) { error="Navigation cached base export exceeds build budget"; return Result::Failed; }
        auto candidate=cell.baseTiles;
        if(Cancelled(control,error)) return Result::Failed;
        output=std::move(candidate); error.clear(); return Result::Built;
    }
    std::vector<VansNavigationObstacleGeometry> desired;
    desired.reserve(obstacles.size());
    for(const auto& source:obstacles)
    {
        auto shape=Expanded(source,bake);
        if(!ValidateNavigationObstacleGeometry(shape,error)) return Result::Failed;
        glm::vec3 low,high; NavigationObstacleGeometryBounds(shape,low,high);
        if(!Overlaps(low,high,cell.low,cell.high)) continue;
        if(!NativeRangeFits(shape,bake)) return Result::Missing;
        desired.push_back(std::move(shape));
    }
    // 重建 transient 及导出与原 shared kernel 使用相同保守上界。
    const auto pixels=static_cast<std::uint64_t>(bake.tileSizeCells)*bake.tileSizeCells;
    const auto fixed=sizeof(NativeCell)+sizeof(dtTileCache)+sizeof(dtNavMesh)+links.size()*80u+4096u+
        obstacles.size()*(sizeof(dtTileCacheObstacle)+sizeof(VansNavigationObstacleGeometry)*2+1024)+
        cell.layers.size()*(sizeof(dtCompressedTile)+sizeof(dtMeshTile)+pixels*1040u+2048u);
    if(fixed>=maximumBytes)
    { error="Navigation cached layers/staging/export exceed build byte budget"; return Result::Failed; }
    struct ResetOnFailure
    {
        VansNavigationCachedCell& cell; bool success=false;
        ~ResetOnFailure() { if(!success) cell.native.reset(); }
    } guard{cell};
    const bool initialize=!cell.native || desired.size()>static_cast<std::size_t>(cell.native->cache->getParams()->maxObstacles);
    if(initialize)
    {
        cell.native=std::make_unique<NativeCell>(areas,bake.maximumPolygonsPerTile,maximumBytes-fixed);
        if(!cell.native->process.ConfigureLinks(links,bake,x,z,error)) return Result::Failed;
        if(!cell.native->Initialize(cell.layers,bake,desired.size(),error)) return Result::Failed;
    }
    cell.native->allocator.SetCapacity(maximumBytes-fixed);
    if(!cell.native->Synchronize(desired,control,initialize,error)) return Result::Failed;
    guard.success=true;
    if(obstacles.empty())
    {
        // 原 Recast detail 数据在首次移除时生成；随后恢复字节完全一致。
        if(!cell.baseReady) return Result::Missing;
        std::uint64_t bytes=0; for(const auto& tile:cell.baseTiles) bytes+=tile.bytes.size()+sizeof(tile);
        if(bytes>=maximumBytes-fixed) { error="Navigation cached base export exceeds build budget"; return Result::Failed; }
        auto candidate=cell.baseTiles;
        if(Cancelled(control,error)) { guard.success=false; return Result::Failed; }
        output=std::move(candidate); error.clear(); return Result::Built;
    }
    std::vector<VansNavigationTilePayload> candidate;
    candidate.reserve(cell.layers.size());
    std::uint64_t exported=0;
    for(int i=0;i<cell.native->staging->getMaxTiles();++i)
    {
        const auto* tile=static_cast<const dtNavMesh*>(cell.native->staging.get())->getTile(i);
        if(!tile->header) continue;
        if(!tile->data || tile->dataSize<=0 || tile->dataSize>8*1024*1024 ||
            static_cast<std::uint64_t>(tile->dataSize)>maximumBytes-fixed-exported)
        { error="Navigation TileCache candidate exceeds payload capacity"; guard.success=false; return Result::Failed; }
        exported+=static_cast<std::uint64_t>(tile->dataSize);
        candidate.push_back({x,z,tile->header->layer,std::vector<unsigned char>(tile->data,tile->data+tile->dataSize)});
        auto& bytes=candidate.back().bytes;
        // staging 的链接含其私有 native ref/salt；发布 Map 将重新建立所有链接。
        // 导出恢复 dtCreateNavMeshData 的无链接布局，禁止把构建上下文的引用持久化。
        const auto linkOffset=reinterpret_cast<const unsigned char*>(tile->links)-tile->data;
        std::memset(bytes.data()+linkOffset,0,static_cast<std::size_t>(tile->header->maxLinkCount)*sizeof(dtLink));
        const auto polyOffset=reinterpret_cast<const unsigned char*>(tile->polys)-tile->data;
        for(int polygon=0;polygon<tile->header->polyCount;++polygon)
        {
            auto poly=tile->polys[polygon]; poly.firstLink=0;
            std::memcpy(bytes.data()+polyOffset+static_cast<std::size_t>(polygon)*sizeof(dtPoly),&poly,sizeof(poly));
        }
    }
    std::sort(candidate.begin(),candidate.end(),[](const auto& a,const auto& b){return a.layer<b.layer;});
    if((!candidate.empty() && !ValidateNavigationTileSet(candidate,bake,areas,error)) || Cancelled(control,error))
    { guard.success=false; return Result::Failed; }
    output=std::move(candidate); error.clear(); return Result::Built;
}
}
namespace
{
void CaptureLayerData(const VansNavigationCachedCell& cell,int x,int z,std::uint64_t hash,VansNavigationTileLayerData* output)
{
    if(!output || !cell.layersReady || cell.layers.empty()) return;
    VansNavigationTileLayerData row; row.x=x; row.z=z; row.baseInputHash=hash;
    row.boundsMinimum=cell.low; row.boundsMaximum=cell.high; row.layers=cell.layers;
    *output=std::move(row);
}
}
struct VansNavigationBuildContext::State
{
    std::atomic_flag busy=ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> publishedBytes{0},publishedBuilt{0},publishedReused{0},publishedEvicted{0};
    std::uint64_t maximumBytes=0,baseHash=0,bakeHash=0,clock=0,built=0,reused=0,evicted=0;
    float rasterOrigin=0;
    std::map<std::pair<int,int>,std::unique_ptr<VansNavigationCachedCell>> cells;
    explicit State(std::uint64_t maximum):maximumBytes(maximum) {}
    std::uint64_t Bytes() const
    {
        std::uint64_t result=sizeof(State)+sizeof(VansNavigationBuildContext)+64;
        for(const auto& entry:cells) result+=entry.second->Bytes();
        return result;
    }
    void Trim(const std::pair<int,int>& keep)
    {
        while(!cells.empty() && Bytes()>maximumBytes)
        {
            auto victim=cells.end();
            for(auto i=cells.begin();i!=cells.end();++i)
                if(i->first!=keep && (victim==cells.end() || i->second->lastUsed<victim->second->lastUsed)) victim=i;
            if(victim==cells.end()) victim=cells.find(keep);
            cells.erase(victim); ++evicted;
        }
    }
};
VansNavigationBuildContext::VansNavigationBuildContext(std::uint64_t maximum):m_State(std::make_unique<State>(maximum))
{ m_State->publishedBytes.store(m_State->Bytes()); }
VansNavigationBuildContext::~VansNavigationBuildContext()=default;
VansNavigationBuildContextStats VansNavigationBuildContext::CaptureStats() const
{
    return {m_State->publishedBytes.load(std::memory_order_acquire),m_State->publishedBuilt.load(),
        m_State->publishedReused.load(),m_State->publishedEvicted.load()};
}
bool VansNavigationBuildContextAccess::Begin(VansNavigationBuildContext& context,std::uint64_t baseHash,
    std::uint64_t bakeHash,float rasterOrigin,std::string& error)
{
    auto& state=*context.m_State;
    if(state.busy.test_and_set(std::memory_order_acquire))
    { error="Navigation build context already has a worker owner"; return false; }
    if(state.baseHash!=baseHash || state.bakeHash!=bakeHash || state.rasterOrigin!=rasterOrigin)
    {
        state.cells.clear(); state.baseHash=baseHash; state.bakeHash=bakeHash; state.rasterOrigin=rasterOrigin;
    }
    return true;
}
void VansNavigationBuildContextAccess::End(VansNavigationBuildContext& context)
{
    auto& state=*context.m_State;
    state.publishedBuilt.store(state.built); state.publishedReused.store(state.reused); state.publishedEvicted.store(state.evicted);
    state.publishedBytes.store(state.Bytes(),std::memory_order_release);
    state.busy.clear(std::memory_order_release);
}
std::uint64_t VansNavigationBuildContextAccess::WorkingBytes(const VansNavigationBuildContext& context)
{ return context.m_State->Bytes(); }
VansNavigationCachedBuildResult VansNavigationBuildContextAccess::BuildCached(VansNavigationBuildContext& context,int x,int z,
    const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
    const std::vector<VansNavigationObstacleGeometry>& obstacles,const std::vector<VansNavigationLinkGeometry>& links,
    std::uint64_t maximumBytes,const VansNavigationBuildControl* control,
    std::vector<VansNavigationTilePayload>& output,std::string& error,VansNavigationTileLayerData* layerData)
{
    auto& state=*context.m_State; const auto key=std::make_pair(x,z); const auto found=state.cells.find(key);
    if(found==state.cells.end()) return VansNavigationCachedBuildResult::Missing;
    found->second->lastUsed=++state.clock;
    std::uint64_t exportBytes=0;
    if(layerData && found->second->layersReady && !found->second->layers.empty())
    {
        exportBytes=sizeof(VansNavigationTileLayerData)+found->second->layers.size()*sizeof(VansNavigationTileCacheLayerPayload);
        for(const auto& layer:found->second->layers) exportBytes+=layer.bytes.size();
    }
    if(exportBytes>=maximumBytes) { error="Navigation layer export exceeds build capacity"; return VansNavigationCachedBuildResult::Failed; }
    const auto result=BuildCell(*found->second,x,z,bake,areas,obstacles,links,maximumBytes-exportBytes,control,output,error);
    if(result==VansNavigationCachedBuildResult::Built)
    { CaptureLayerData(*found->second,x,z,state.baseHash,layerData); ++state.reused; }
    state.Trim(key); return result;
}
bool VansNavigationBuildContextAccess::Seed(VansNavigationBuildContext& context,const std::vector<VansNavigationTileLayerData>& rows,
    const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,std::uint64_t maximumBytes,std::string& error)
{
    auto& state=*context.m_State;
    std::vector<VansNavigationTileLayerData> matching;
    std::uint64_t bytes=rows.size()*sizeof(VansNavigationTileLayerData),metadata=0;
    if(bytes>maximumBytes) { error="Navigation layer seed metadata exceeds build capacity"; return false; }
    matching.reserve(rows.size());
    for(const auto& row:rows)
    {
        if(row.baseInputHash!=state.baseHash || state.cells.count({row.x,row.z})) continue;
        const auto cost=row.EstimateBytes()-sizeof(row)+sizeof(VansNavigationCachedCell)+128u;
        if(cost>maximumBytes-bytes) { error="Navigation layer seed exceeds build capacity"; return false; }
        bytes+=cost; metadata+=sizeof(VansNavigationCachedCell)+128u; matching.push_back(row);
    }
    if(!ValidateNavigationTileLayerData(matching,state.baseHash,bake,areas,maximumBytes-metadata,error)) return false;
    for(auto& row:matching)
    {
        auto cell=std::make_unique<VansNavigationCachedCell>();
        cell->layers=std::move(row.layers); cell->low=row.boundsMinimum; cell->high=row.boundsMaximum;
        cell->layersReady=true; cell->lastUsed=++state.clock;
        const auto key=std::make_pair(row.x,row.z);
        state.cells.emplace(key,std::move(cell)); state.Trim(key);
    }
    error.clear(); return true;
}
void VansNavigationBuildContextAccess::StoreBase(VansNavigationBuildContext& context,int x,int z,
    const std::vector<VansNavigationTilePayload>& tiles)
{
    auto& state=*context.m_State;
    if(!state.maximumBytes) return;
    std::uint64_t bytes=sizeof(VansNavigationCachedCell)+128+tiles.size()*sizeof(VansNavigationTilePayload);
    for(const auto& tile:tiles) bytes+=tile.bytes.size();
    if(bytes>state.maximumBytes) return;
    const auto key=std::make_pair(x,z); const auto found=state.cells.find(key);
    if(found==state.cells.end())
    {
        auto cell=std::make_unique<VansNavigationCachedCell>();
        cell->baseTiles=tiles; cell->baseReady=true; cell->lastUsed=++state.clock;
        state.cells.emplace(key,std::move(cell));
    }
    else
    { found->second->baseTiles=tiles; found->second->baseReady=true; found->second->lastUsed=++state.clock; }
    state.Trim(key);
}
void VansNavigationBuildContextAccess::StoreCell(VansNavigationBuildContext& context,int x,int z,
    std::unique_ptr<VansNavigationCachedCell> cell)
{
    auto& state=*context.m_State;
    ++state.built;
    if(!state.maximumBytes || cell->Bytes()>state.maximumBytes) return;
    const auto key=std::make_pair(x,z);
    const auto old=state.cells.find(key);
    if(old!=state.cells.end() && old->second->baseReady)
    { cell->baseTiles=std::move(old->second->baseTiles); cell->baseReady=true; }
    cell->lastUsed=++state.clock; state.cells[key]=std::move(cell); state.Trim(key);
}
namespace
{
VansNavigationTileCacheBuildResult BuildLayerCell(rcContext& context,const rcCompactHeightfield& compact,
    int border,int heightCells,const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
    int x,int z,std::uint64_t maximumBytes,const VansNavigationBuildControl* control,
    std::unique_ptr<VansNavigationCachedCell>& output,std::string& error)
{
    using Result=VansNavigationTileCacheBuildResult;
    if(Cancelled(control,error)) return Result::Failed;
    if(bake.tileSizeCells>255 || bake.maximumVerticesPerPolygon!=DT_VERTS_PER_POLYGON) return Result::RecastRequired;
    const auto working=static_cast<std::uint64_t>(compact.width)*compact.height*1040u+
        static_cast<std::uint64_t>(compact.spanCount)*128u+65536u;
    if(working>=maximumBytes) { error="Navigation layer construction exceeds build capacity"; return Result::Failed; }
    auto cell=std::make_unique<VansNavigationCachedCell>();
    cell->low={compact.bmin[0],compact.bmin[1],compact.bmin[2]};
    cell->high={compact.bmax[0],compact.bmax[1],compact.bmax[2]};
    std::unique_ptr<rcHeightfieldLayerSet,decltype(&rcFreeHeightfieldLayerSet)>
        layers(rcAllocHeightfieldLayerSet(),&rcFreeHeightfieldLayerSet);
    if(!layers || !rcBuildHeightfieldLayers(&context,compact,border,heightCells,*layers))
    { error="Recast could not build navigation TileCache layers"; return Result::Failed; }
    if(layers->nlayers>DT_MAX_TOUCHED_TILES) return Result::RecastRequired;
    cell->layers.reserve(layers->nlayers);
    std::uint64_t retained=cell->layers.capacity()*sizeof(VansNavigationTileCacheLayerPayload);
    VansNavigationTileCacheLayerCompressor compressor;
    for(int i=0;i<layers->nlayers;++i)
    {
        if(Cancelled(control,error)) return Result::Failed;
        const auto& layer=layers->layers[i];
        if(layer.width>255 || layer.height>255 || layer.hmin<0 || layer.hmax>65535 || layer.hmax-layer.hmin>254) return Result::RecastRequired;
        dtTileCacheLayerHeader header{};
        header.magic=DT_TILECACHE_MAGIC; header.version=DT_TILECACHE_VERSION;
        header.tx=x; header.ty=z; header.tlayer=i;
        rcVcopy(header.bmin,layer.bmin); rcVcopy(header.bmax,layer.bmax);
        header.width=static_cast<unsigned char>(layer.width); header.height=static_cast<unsigned char>(layer.height);
        header.minx=static_cast<unsigned char>(layer.minx); header.maxx=static_cast<unsigned char>(layer.maxx);
        header.miny=static_cast<unsigned char>(layer.miny); header.maxy=static_cast<unsigned char>(layer.maxy);
        header.hmin=static_cast<unsigned short>(layer.hmin); header.hmax=static_cast<unsigned short>(layer.hmax);
        const auto pixels=static_cast<std::uint64_t>(layer.width)*layer.height;
        const auto encoded=static_cast<std::uint64_t>(compressor.maxCompressedSize(static_cast<int>(pixels*3u)))+sizeof(dtTileCacheLayerHeader);
        if(working+retained>=maximumBytes || encoded*2u>maximumBytes-working-retained)
        { error="Navigation layer compression exceeds build capacity"; return Result::Failed; }
        unsigned char* data=nullptr; int bytes=0;
        const auto status=dtBuildTileCacheLayer(&compressor,&header,layer.heights,layer.areas,layer.cons,&data,&bytes);
        std::unique_ptr<unsigned char,decltype(&dtFree)> owner(data,&dtFree);
        if(dtStatusFailed(status) || !data || bytes<=0)
        { error="Navigation TileCache layer compression failed"; return Result::Failed; }
        cell->layers.push_back({x,z,i,std::vector<unsigned char>(data,data+bytes)});
        retained+=cell->layers.back().bytes.capacity();
    }
    if(!ValidateNavigationTileCacheLayerSet(cell->layers,bake,areas,maximumBytes-working,error)) return Result::Failed;
    cell->layersReady=true; output=std::move(cell); error.clear(); return Result::Built;
}
}
VansNavigationTileCacheBuildResult BuildNavigationTileCacheLayers(rcContext& context,const rcCompactHeightfield& compact,
    int border,int heightCells,const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
    int x,int z,std::uint64_t maximumBytes,const VansNavigationBuildControl* control,
    VansNavigationTileLayerData& output,std::string& error,VansNavigationBuildContext* buildContext)
{
    std::unique_ptr<VansNavigationCachedCell> cell;
    const auto result=BuildLayerCell(context,compact,border,heightCells,bake,areas,x,z,maximumBytes,control,cell,error);
    if(result!=VansNavigationTileCacheBuildResult::Built) return result;
    VansNavigationTileLayerData row; row.x=x; row.z=z; row.boundsMinimum=cell->low; row.boundsMaximum=cell->high;
    if(buildContext)
    {
        std::uint64_t bytes=sizeof(VansNavigationTileLayerData)+cell->layers.size()*sizeof(VansNavigationTileCacheLayerPayload);
        for(const auto& layer:cell->layers) bytes+=layer.bytes.size();
        if(bytes*2u+sizeof(VansNavigationCachedCell)>maximumBytes)
        { error="Navigation base layer/context copy exceeds build capacity"; return VansNavigationTileCacheBuildResult::Failed; }
        row.layers=cell->layers; VansNavigationBuildContextAccess::StoreCell(*buildContext,x,z,std::move(cell));
    }
    else row.layers=std::move(cell->layers);
    output=std::move(row); error.clear(); return result;
}
VansNavigationTileCacheBuildResult BuildNavigationCarvedTileCache(rcContext& context,const rcCompactHeightfield& compact,
    int border,int heightCells,const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
    int x,int z,const std::vector<VansNavigationObstacleGeometry>& obstacles,const std::vector<VansNavigationLinkGeometry>& links,std::uint64_t maximumBytes,
    const VansNavigationBuildControl* control,std::vector<VansNavigationTilePayload>& output,std::string& error,
    VansNavigationBuildContext* buildContext,VansNavigationTileLayerData* layerData)
{
    using Result=VansNavigationTileCacheBuildResult;
    if(Cancelled(control,error)) return Result::Failed;
    if(bake.tileSizeCells>255 || bake.maximumVerticesPerPolygon!=DT_VERTS_PER_POLYGON) return Result::RecastRequired;
    const glm::vec3 cellLow(compact.bmin[0],compact.bmin[1],compact.bmin[2]);
    const glm::vec3 cellHigh(compact.bmax[0],compact.bmax[1],compact.bmax[2]);
    for(const auto& source:obstacles)
    {
        const auto shape=Expanded(source,bake);
        if(!ValidateNavigationObstacleGeometry(shape,error)) return Result::Failed;
        glm::vec3 low,high; NavigationObstacleGeometryBounds(shape,low,high);
        if(Overlaps(low,high,cellLow,cellHigh) && !NativeRangeFits(shape,bake)) return Result::RecastRequired;
    }
    std::unique_ptr<VansNavigationCachedCell> cell;
    const auto built=BuildLayerCell(context,compact,border,heightCells,bake,areas,x,z,maximumBytes,control,cell,error);
    if(built!=Result::Built) return built;
    if(cell->layers.empty())
    {
        output.clear(); error.clear();
        if(buildContext) VansNavigationBuildContextAccess::StoreCell(*buildContext,x,z,std::move(cell));
        return Result::Built;
    }
    // Recast intermediates have already been released; native borrows owned layer bytes.
    std::uint64_t exportBytes=0;
    if(layerData)
    {
        exportBytes=sizeof(VansNavigationTileLayerData)+cell->layers.size()*sizeof(VansNavigationTileCacheLayerPayload);
        for(const auto& layer:cell->layers) exportBytes+=layer.bytes.size();
    }
    if(exportBytes>=maximumBytes) { error="Navigation layer export exceeds build capacity"; return Result::Failed; }
    const auto result=BuildCell(*cell,x,z,bake,areas,obstacles,links,maximumBytes-exportBytes,control,output,error);
    if(result!=VansNavigationCachedBuildResult::Built)
        return result==VansNavigationCachedBuildResult::Missing?Result::RecastRequired:Result::Failed;
    CaptureLayerData(*cell,x,z,0,layerData);
    if(buildContext) VansNavigationBuildContextAccess::StoreCell(*buildContext,x,z,std::move(cell));
    return Result::Built;
}

void MarkNavigationCarveAreas(rcCompactHeightfield& compact,const VansNavigationBakeSettings& bake,
    const std::vector<VansNavigationObstacleGeometry>& obstacles)
{
    for(const auto& source:obstacles)
    {
        const auto shape=Expanded(source,bake);
        const float cosine=std::cos(shape.yawRadians),sine=std::sin(shape.yawRadians);
        for(int row=0;row<compact.height;++row) for(int column=0;column<compact.width;++column)
        {
            const auto& cell=compact.cells[column+row*compact.width];
            for(unsigned int i=cell.index;i<cell.index+cell.count;++i)
            {
                if(compact.areas[i]==RC_NULL_AREA) continue;
                const glm::vec3 point(compact.bmin[0]+(column+.5f)*compact.cs,
                    compact.bmin[1]+compact.spans[i].y*compact.ch,compact.bmin[2]+(row+.5f)*compact.cs);
                const auto delta=point-shape.center;
                bool inside=false;
                if(shape.kind==VansNavigationObstacleGeometryKind::Cylinder)
                    inside=delta.y>=0 && delta.y<=shape.height && delta.x*delta.x+delta.z*delta.z<=shape.radius*shape.radius;
                else
                {
                    const glm::vec3 local(cosine*delta.x-sine*delta.z,delta.y,sine*delta.x+cosine*delta.z);
                    inside=glm::all(glm::lessThanEqual(glm::abs(local),shape.halfExtents));
                }
                if(inside) compact.areas[i]=RC_NULL_AREA;
            }
        }
    }
}
}
