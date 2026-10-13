#include "VansNavigationTileData.h"
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourAlloc.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <tuple>
#include <array>
#include <algorithm>
#include <memory>
#include <map>
#include <new>

namespace Vans
{
namespace
{
template<typename T>
T Read(const std::vector<unsigned char>& data, std::size_t offset)
{
    T value;
    std::memcpy(&value, data.data() + offset, sizeof(value));
    return value;
}
}

bool ValidateNavigationTileData(const std::vector<unsigned char>& data,
    const VansNavigationAreaSettings& areas, std::string& error)
{
    static_assert(DT_VERTS_PER_POLYGON == kMaximumNavigationPolygonVertices);
    const auto fail = [&error](const char* message) { error = message; return false; };
    if (data.size() < sizeof(dtMeshHeader) || data.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return fail("Detour tile payload size is invalid");
    const auto header = Read<dtMeshHeader>(data, 0);
    if (header.magic != DT_NAVMESH_MAGIC || header.version != DT_NAVMESH_VERSION ||
        header.polyCount <= 0 || header.polyCount > 65535 || header.vertCount < 3 || header.vertCount > 65535 ||
        header.maxLinkCount <= 0 || header.detailMeshCount < 0 || header.detailVertCount < 0 ||
        header.detailTriCount < 0 || header.bvNodeCount < 0 || header.offMeshConCount < 0 ||
        header.offMeshBase < 0 || header.offMeshBase > header.polyCount ||
        header.detailMeshCount != header.offMeshBase ||
        header.offMeshConCount != header.polyCount - header.offMeshBase || header.layer < 0)
        return fail("Detour tile header counts or ABI are invalid");
    if (!std::isfinite(header.walkableHeight) || header.walkableHeight <= 0 ||
        !std::isfinite(header.walkableRadius) || header.walkableRadius <= 0 ||
        !std::isfinite(header.walkableClimb) || header.walkableClimb < 0 ||
        !std::isfinite(header.bvQuantFactor) || header.bvQuantFactor <= 0)
        return fail("Detour tile dimensions are invalid");
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(header.bmin[axis]) || !std::isfinite(header.bmax[axis]) ||
            header.bmin[axis] > header.bmax[axis]) return fail("Detour tile bounds are invalid");

    // Detour 本身以 int 计算布局。乘法在有界 size_t 域内完成，并检查每个切片。
    std::size_t cursor = (sizeof(dtMeshHeader) + 3u) & ~std::size_t(3u);
    const auto section = [&](std::size_t count, std::size_t stride, std::size_t& offset)
    {
        offset = cursor;
        if (count > data.size() / stride) return false;
        const auto size = (count * stride + 3u) & ~std::size_t(3u);
        if (cursor > data.size() || size > data.size() - cursor) return false;
        cursor += size;
        return true;
    };
    std::size_t vertices, polygons, links, details, detailVertices, triangles, tree, connections;
    if (!section(header.vertCount, sizeof(float) * 3u, vertices) ||
        !section(header.polyCount, sizeof(dtPoly), polygons) ||
        !section(header.maxLinkCount, sizeof(dtLink), links) ||
        !section(header.detailMeshCount, sizeof(dtPolyDetail), details) ||
        !section(header.detailVertCount, sizeof(float) * 3u, detailVertices) ||
        !section(header.detailTriCount, 4u, triangles) ||
        !section(header.bvNodeCount, sizeof(dtBVNode), tree) ||
        !section(header.offMeshConCount, sizeof(dtOffMeshConnection), connections) || cursor != data.size())
        return fail("Detour tile layout is truncated, excessive or has trailing bytes");
    for (std::size_t index = 0; index < static_cast<std::size_t>(header.vertCount) * 3u; ++index)
        if (!std::isfinite(Read<float>(data, vertices + index * sizeof(float))))
            return fail("Detour tile contains a non-finite vertex");
    for (std::size_t index = 0; index < static_cast<std::size_t>(header.detailVertCount) * 3u; ++index)
        if (!std::isfinite(Read<float>(data, detailVertices + index * sizeof(float))))
            return fail("Detour tile contains a non-finite detail vertex");
    for (int index = 0; index < header.polyCount; ++index)
    {
        const auto poly = Read<dtPoly>(data, polygons + sizeof(dtPoly) * index);
        const bool offMesh = index >= header.offMeshBase;
        if (!FindNavigationAreaById(areas, poly.getArea()) ||
            poly.getType() != (offMesh ? DT_POLYTYPE_OFFMESH_CONNECTION : DT_POLYTYPE_GROUND) ||
            poly.vertCount < (offMesh ? 2 : 3) || poly.vertCount > DT_VERTS_PER_POLYGON ||
            (offMesh && poly.vertCount != 2)) return fail("Detour polygon type, area or vertex count is invalid");
        for (int corner = 0; corner < poly.vertCount; ++corner)
        {
            if (poly.verts[corner] >= header.vertCount) return fail("Detour polygon vertex index is invalid");
            const auto neighbor = poly.neis[corner];
            if ((neighbor & DT_EXT_LINK) ? ((neighbor & ~DT_EXT_LINK) > 7u) : (neighbor > header.offMeshBase))
                return fail("Detour polygon neighbor index is invalid");
        }
        if (offMesh) continue;
        const auto detail = Read<dtPolyDetail>(data, details + sizeof(dtPolyDetail) * index);
        if (detail.vertBase > static_cast<unsigned>(header.detailVertCount) ||
            detail.vertCount > static_cast<unsigned>(header.detailVertCount) - detail.vertBase ||
            detail.triBase > static_cast<unsigned>(header.detailTriCount) ||
            detail.triCount > static_cast<unsigned>(header.detailTriCount) - detail.triBase)
            return fail("Detour detail mesh range is invalid");
        for (unsigned triangle = 0; triangle < detail.triCount; ++triangle)
            for (unsigned corner = 0; corner < 3; ++corner)
                if (data[triangles + (detail.triBase + triangle) * 4u + corner] >= poly.vertCount + detail.vertCount)
                    return fail("Detour detail triangle index is invalid");
    }
    for (int index = 0; index < header.bvNodeCount; ++index)
    {
        const auto node = Read<dtBVNode>(data, tree + sizeof(dtBVNode) * index);
        if (node.i >= header.offMeshBase ||
            (node.i < 0 && -static_cast<std::int64_t>(node.i) > header.bvNodeCount - index))
            return fail("Detour bounding tree index is invalid");
        for (int axis = 0; axis < 3; ++axis)
            if (node.bmin[axis] > node.bmax[axis]) return fail("Detour bounding tree bounds are invalid");
    }
    if(header.offMeshConCount>static_cast<int>(kMaximumNavigationLinkCapabilities))
        return fail("Detour off-mesh connection count exceeds bounded Link capacity");
    std::array<std::uint32_t,kMaximumNavigationLinkCapabilities> linkKeys{};
    for (int index = 0; index < header.offMeshConCount; ++index)
    {
        const auto connection = Read<dtOffMeshConnection>(data, connections + sizeof(dtOffMeshConnection) * index);
        if (connection.poly != header.offMeshBase + index || !connection.userId || !std::isfinite(connection.rad) || connection.rad <= 0 ||
            (connection.flags & ~DT_OFFMESH_CON_BIDIR) || (connection.side != 0xff && connection.side > 7))
            return fail("Detour off-mesh connection is invalid");
        for (float value : connection.pos)
            if (!std::isfinite(value)) return fail("Detour off-mesh endpoint is non-finite");
        if(connection.pos[0]==connection.pos[3] && connection.pos[1]==connection.pos[4] && connection.pos[2]==connection.pos[5])
            return fail("Detour off-mesh endpoints must be distinct");
        linkKeys[static_cast<std::size_t>(index)]=connection.userId;
    }
    const auto keyEnd=linkKeys.begin()+header.offMeshConCount;
    std::sort(linkKeys.begin(),keyEnd);
    if(std::adjacent_find(linkKeys.begin(),keyEnd)!=keyEnd) return fail("Detour off-mesh Link identity is duplicated");
    error.clear();
    return true;
}

bool ValidateNavigationTileGridBounds(int x,int z,const float* low,const float* high,
    const VansNavigationBakeSettings& bake,std::string& error)
{
    for(int axis=0;axis<3;++axis)
        if(!std::isfinite(low[axis]) || !std::isfinite(high[axis]) || low[axis]>high[axis])
        { error="Navigation payload bounds are invalid"; return false; }
    const double width=static_cast<double>(bake.cellSize)*bake.tileSizeCells;
    for(int horizontal=0;horizontal<2;++horizontal)
    {
        const int axis=horizontal==0?0:2,coordinate=horizontal==0?x:z;
        const double minimum=bake.tileOrigin[axis]+width*coordinate,maximum=minimum+width;
        const double tolerance=bake.cellSize*.01+std::abs(minimum)*2.e-7;
        if(!std::isfinite(minimum) || !std::isfinite(maximum) ||
            std::abs(low[axis]-minimum)>tolerance || std::abs(high[axis]-maximum)>tolerance)
        { error="Navigation payload bounds do not match the fixed map grid"; return false; }
    }
    error.clear(); return true;
}
bool VansNavigationNativeLinks::Compile(const std::vector<VansNavigationLinkGeometry>& links,
    const VansNavigationAreaSettings& areas,const VansNavigationBakeSettings& bake,int x,int z,std::string& error)
try
{
    if(links.size()>kMaximumNavigationLinkCapabilities) { error="Native Link count exceeds capacity"; return false; }
    std::array<std::uint32_t,kMaximumNavigationLinkCapabilities> keys{};
    for(std::size_t i=0;i<links.size();++i)
    {
        if(!ValidateNavigationLinkGeometry(links[i],areas,error)) return false;
        keys[i]=links[i].key.value;
    }
    const auto end=keys.begin()+links.size(); std::sort(keys.begin(),end);
    if(std::adjacent_find(keys.begin(),end)!=end) { error="Native Link identities collide"; return false; }
    const double width=static_cast<double>(bake.cellSize)*bake.tileSizeCells;
    if(!std::isfinite(width) || width<=0) { error="Native Link Tile layout is invalid"; return false; }
    const auto owned=[&](const auto& link)
    {
        return std::floor((static_cast<double>(link.start.x)-bake.tileOrigin.x)/width)==x &&
            std::floor((static_cast<double>(link.start.z)-bake.tileOrigin.z)/width)==z;
    };
    const auto count=static_cast<std::size_t>(std::count_if(links.begin(),links.end(),owned));
    VansNavigationNativeLinks candidate;
    candidate.m_Vertices.reserve(count*6); candidate.m_Radii.reserve(count); candidate.m_Flags.reserve(count);
    candidate.m_Areas.reserve(count); candidate.m_Directions.reserve(count); candidate.m_Keys.reserve(count);
    for(const auto& link:links)
    {
        if(!owned(link)) continue;
        for(const auto& point:{link.start,link.end})
        { candidate.m_Vertices.push_back(point.x); candidate.m_Vertices.push_back(point.y); candidate.m_Vertices.push_back(point.z); }
        candidate.m_Radii.push_back(link.radiusMeters); candidate.m_Flags.push_back(1); candidate.m_Areas.push_back(link.area);
        candidate.m_Directions.push_back(link.bidirectional?DT_OFFMESH_CON_BIDIR:0); candidate.m_Keys.push_back(link.key.value);
    }
    *this=std::move(candidate); error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Native Link parameter allocation failed"; return false; }
void VansNavigationNativeLinks::Apply(dtNavMeshCreateParams& params) const
{
    params.offMeshConVerts=m_Vertices.data(); params.offMeshConRad=m_Radii.data(); params.offMeshConFlags=m_Flags.data();
    params.offMeshConAreas=m_Areas.data(); params.offMeshConDir=m_Directions.data(); params.offMeshConUserID=m_Keys.data();
    params.offMeshConCount=static_cast<int>(m_Keys.size());
}
std::uint64_t VansNavigationNativeLinks::Bytes() const
{
    return (m_Vertices.capacity()+m_Radii.capacity())*sizeof(float)+m_Flags.capacity()*sizeof(unsigned short)+
        m_Areas.capacity()+m_Directions.capacity()+m_Keys.capacity()*sizeof(unsigned int);
}
bool ValidateNavigationConnectedLinks(const std::vector<VansNavigationTilePayload>& tiles,const VansNavigationBakeSettings& bake,
    const std::vector<VansNavigationLinkGeometry>& links,std::uint64_t maximumBytes,std::string& error)
try
{
    if(links.empty()) { error.clear(); return true; }
    if(links.size()>kMaximumNavigationLinkCapabilities) { error="Navigation Link connection validation exceeds capacity"; return false; }
    int tileCapacity=1; while(tileCapacity<static_cast<int>(tiles.size())) tileCapacity*=2;
    std::uint64_t bytes=4096+static_cast<std::uint64_t>(tileCapacity)*512+links.size()*512;
    if(bytes>=maximumBytes) { error="Navigation Link topology validation exceeds build byte budget"; return false; }
    for(const auto& tile:tiles)
    {
        const auto cost=tile.bytes.size()+64;
        if(cost>maximumBytes-bytes) { error="Navigation Link topology validation exceeds build byte budget"; return false; }
        bytes+=cost;
    }
    dtNavMeshParams params{}; params.orig[0]=bake.tileOrigin.x; params.orig[1]=bake.tileOrigin.y; params.orig[2]=bake.tileOrigin.z;
    params.tileWidth=params.tileHeight=bake.cellSize*bake.tileSizeCells; params.maxTiles=tileCapacity; params.maxPolys=bake.maximumPolygonsPerTile;
    std::unique_ptr<dtNavMesh,decltype(&dtFreeNavMesh)> mesh(dtAllocNavMesh(),&dtFreeNavMesh);
    if(!mesh || dtStatusFailed(mesh->init(&params))) { error="Navigation Link topology initialization failed"; return false; }
    for(const auto& tile:tiles)
    {
        std::unique_ptr<unsigned char,decltype(&dtFree)> copy(static_cast<unsigned char*>(dtAlloc(tile.bytes.size(),DT_ALLOC_PERM)),&dtFree);
        if(!copy) { error="Navigation Link topology Tile allocation failed"; return false; }
        std::memcpy(copy.get(),tile.bytes.data(),tile.bytes.size());
        if(dtStatusFailed(mesh->addTile(copy.get(),static_cast<int>(tile.bytes.size()),DT_TILE_FREE_DATA,0,nullptr)))
        { error="Navigation Link topology Tile admission failed"; return false; }
        copy.release();
    }
    struct Expected { const VansNavigationLinkGeometry* link; bool found=false; };
    std::map<std::uint32_t,Expected> expected;
    for(const auto& link:links) if(!expected.emplace(link.key.value,Expected{&link}).second)
    { error="Navigation Link topology has colliding identities"; return false; }
    for(int slot=0;slot<mesh->getMaxTiles();++slot)
    {
        const auto* tile=static_cast<const dtNavMesh*>(mesh.get())->getTile(slot); if(!tile->header) continue;
        for(int i=0;i<tile->header->offMeshConCount;++i)
        {
            const auto& connection=tile->offMeshCons[i]; const auto found=expected.find(connection.userId);
            if(found==expected.end() || found->second.found) { error="Navigation Link is absent from input or appears in multiple Tile layers"; return false; }
            const auto& link=*found->second.link; const auto& polygon=tile->polys[connection.poly]; bool attached[2]{};
            unsigned int visited=0;
            for(auto index=polygon.firstLink;index!=DT_NULL_LINK;index=tile->links[index].next)
            {
                if(index>=static_cast<unsigned int>(tile->header->maxLinkCount) || ++visited>static_cast<unsigned int>(tile->header->maxLinkCount))
                { error="Navigation Link adjacency is invalid"; return false; }
                const auto& edge=tile->links[index]; const dtMeshTile* targetTile=nullptr; const dtPoly* target=nullptr;
                if(edge.edge<2 && !dtStatusFailed(mesh->getTileAndPolyByRef(edge.ref,&targetTile,&target)) && target->getType()==DT_POLYTYPE_GROUND)
                    attached[edge.edge]=true;
            }
            for(int endpoint=0;endpoint<2;++endpoint)
            {
                const auto& authored=endpoint?link.end:link.start; const auto* actual=tile->verts+polygon.verts[endpoint]*3;
                const double dx=static_cast<double>(actual[0])-authored.x,dz=static_cast<double>(actual[2])-authored.z;
                if(!attached[endpoint] || dx*dx+dz*dz>std::pow(static_cast<double>(link.radiusMeters)+1.e-4,2) ||
                    std::abs(static_cast<double>(actual[1])-authored.y)>bake.agentMaxClimb+2*bake.cellHeight+1.e-4)
                { error="Navigation Link endpoint cannot attach to its Map/Profile within the radius and climb tolerance: "+link.id; return false; }
            }
            found->second.found=true;
        }
    }
    for(const auto& entry:expected) if(!entry.second.found)
    { error="Navigation Link entrance is outside the built Map/Profile: "+entry.second.link->id; return false; }
    error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation Link topology validation allocation failed"; return false; }

bool ValidateNavigationTileSet(const std::vector<VansNavigationTilePayload>& tiles,
    const VansNavigationBakeSettings& bake, const VansNavigationAreaSettings& areas,
    std::string& error)
{
    if (!ValidateNavigationBakeSettings(bake, error) ||
        !ValidateNavigationAreaSettings(areas, error)) return false;
    if (tiles.empty() || tiles.size() > static_cast<std::size_t>(bake.maximumTiles))
    { error = "Navigation Tile count exceeds map capacity or is empty"; return false; }
    std::set<std::tuple<int, int, int>> keys;
    std::size_t totalBytes = 0;
    for (const auto& tile : tiles)
    {
        if (tile.layer < 0 || tile.layer > 255 || !keys.emplace(tile.x, tile.z, tile.layer).second ||
            tile.bytes.size() > 8u * 1024u * 1024u ||
            totalBytes > 255u * 1024u * 1024u - tile.bytes.size())
        { error = "Navigation Tile key, layer or payload byte budget is invalid"; return false; }
        totalBytes += tile.bytes.size();
        if (!ValidateNavigationTileData(tile.bytes, areas, error)) return false;
        const auto header = Read<dtMeshHeader>(tile.bytes, 0);
        if (header.x != tile.x || header.y != tile.z || header.layer != tile.layer ||
            header.polyCount > bake.maximumPolygonsPerTile ||
            header.walkableHeight != bake.agentHeight || header.walkableRadius != bake.agentRadius ||
            header.walkableClimb != bake.agentMaxClimb)
        { error = "Navigation Tile payload does not match its map/profile metadata"; return false; }
        if(!ValidateNavigationTileGridBounds(tile.x,tile.z,header.bmin,header.bmax,bake,error)) return false;
    }
    error.clear();
    return true;
}
}
