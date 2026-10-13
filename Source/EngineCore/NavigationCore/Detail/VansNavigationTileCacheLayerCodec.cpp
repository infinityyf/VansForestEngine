#include "VansNavigationTileCacheLayerCodec.h"
#include "VansNavigationTileData.h"
#include <DetourTileCache.h>
#include <DetourNavMesh.h>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <map>

namespace Vans
{
int VansNavigationTileCacheLayerCompressor::maxCompressedSize(int size)
{
    const auto result=static_cast<std::int64_t>(size)+(static_cast<std::int64_t>(size)+127)/128;
    return size<0 || result>(std::numeric_limits<int>::max)()?0:static_cast<int>(result);
}
dtStatus VansNavigationTileCacheLayerCompressor::compress(const unsigned char* source,int size,
    unsigned char* target,int capacity,int* written)
{
    const int bound=maxCompressedSize(size);
    if(!source || !target || !written || size<0 || capacity<0 || (size>0 && !bound) || capacity<bound)
        return DT_FAILURE|DT_INVALID_PARAM;
    int in=0,out=0;
    while(in<size)
    {
        int run=1;
        while(run<128 && in+run<size && source[in+run]==source[in]) ++run;
        if(run>=3)
        { target[out++]=static_cast<unsigned char>(0x80|(run-1)); target[out++]=source[in]; in+=run; }
        else
        {
            const int start=in;
            do
            {
                ++in; run=1;
                while(run<3 && in+run<size && source[in+run]==source[in]) ++run;
            } while(in<size && in-start<128 && run<3);
            target[out++]=static_cast<unsigned char>(in-start-1);
            std::memcpy(target+out,source+start,static_cast<std::size_t>(in-start)); out+=in-start;
        }
    }
    *written=out; return DT_SUCCESS;
}
dtStatus VansNavigationTileCacheLayerCompressor::decompress(const unsigned char* source,int size,
    unsigned char* target,int capacity,int* written)
{
    if(!source || !target || !written || size<0 || capacity<0) return DT_FAILURE|DT_INVALID_PARAM;
    int in=0,out=0;
    while(in<size)
    {
        const unsigned char code=source[in++]; const int count=(code&127)+1;
        if(count>capacity-out) return DT_FAILURE|DT_BUFFER_TOO_SMALL;
        if(code&128)
        { if(in==size) return DT_FAILURE|DT_INVALID_PARAM; std::memset(target+out,source[in++],count); }
        else
        { if(count>size-in) return DT_FAILURE|DT_INVALID_PARAM; std::memcpy(target+out,source+in,count); in+=count; }
        out+=count;
    }
    *written=out; return DT_SUCCESS;
}
bool ValidateNavigationTileCacheLayerSet(const std::vector<VansNavigationTileCacheLayerPayload>& layers,
    const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,
    std::uint64_t maximumBytes,std::string& error)
try
{
    const auto fail=[&](const char* message){error=message; return false;};
    if(!ValidateNavigationBakeSettings(bake,error) || !ValidateNavigationAreaSettings(areas,error)) return false;
    if(layers.empty())
    {
        if(layers.capacity()*sizeof(VansNavigationTileCacheLayerPayload)>maximumBytes)
            return fail("Navigation compressed layer index exceeds byte capacity");
        error.clear(); return true;
    }
    if(bake.tileSizeCells>255 || bake.maximumVerticesPerPolygon!=DT_VERTS_PER_POLYGON ||
        layers.size()>static_cast<std::size_t>(bake.maximumTiles))
        return fail("Navigation compressed layer count or native configuration is invalid");
    const auto pixels=static_cast<std::uint64_t>(bake.tileSizeCells)*bake.tileSizeCells;
    const auto rawBytes=pixels*3u;
    std::uint64_t retained=layers.capacity()*sizeof(VansNavigationTileCacheLayerPayload);
    for(const auto& layer:layers)
    {
        if(layer.bytes.capacity()>maximumBytes || retained>maximumBytes-layer.bytes.capacity())
            return fail("Navigation compressed layer storage exceeds byte capacity");
        retained+=layer.bytes.capacity();
    }
    // key/邻接验证的临时索引也在解压分配之前预检。
    const auto indexBytes=layers.size()*128u;
    if(retained>maximumBytes || rawBytes>maximumBytes-retained ||
        indexBytes>maximumBytes-retained-rawBytes)
        return fail("Navigation compressed layer validation exceeds byte capacity");
    std::map<std::pair<int,int>,unsigned int> counts;
    std::array<bool,256> knownAreas{};
    for(const auto& area:areas.definitions) knownAreas[static_cast<unsigned int>(area.id)+1u]=true;
    VansNavigationTileCacheLayerCompressor compressor;
    const auto headerBytes=(sizeof(dtTileCacheLayerHeader)+3u)&~std::size_t(3u);
    std::vector<unsigned char> decoded(static_cast<std::size_t>(rawBytes));
    for(const auto& layer:layers)
    {
        auto& count=counts[{layer.x,layer.z}];
        if(layer.layer!=static_cast<int>(count) || ++count>DT_MAX_TOUCHED_TILES)
            return fail("Navigation compressed layer keys must be unique, contiguous and within native touched capacity");
        if(layer.bytes.size()<=headerBytes ||
            layer.bytes.size()>headerBytes+static_cast<std::size_t>(compressor.maxCompressedSize(static_cast<int>(rawBytes))))
            return fail("Navigation compressed layer byte layout is invalid");
        dtTileCacheLayerHeader header{};
        std::memcpy(&header,layer.bytes.data(),sizeof(header));
        if(header.magic!=DT_TILECACHE_MAGIC || header.version!=DT_TILECACHE_VERSION ||
            header.tx!=layer.x || header.ty!=layer.z || header.tlayer!=layer.layer ||
            header.width!=bake.tileSizeCells || header.height!=bake.tileSizeCells ||
            header.minx>header.maxx || header.maxx>=header.width ||
            header.miny>header.maxy || header.maxy>=header.height ||
            header.hmin>header.hmax || header.hmax-header.hmin>254)
            return fail("Navigation compressed layer header or metadata is invalid");
        if(!ValidateNavigationTileGridBounds(layer.x,layer.z,header.bmin,header.bmax,bake,error)) return false;
        const double origin=static_cast<double>(header.bmin[1])-header.hmin*static_cast<double>(bake.cellHeight);
        const double high=origin+header.hmax*static_cast<double>(bake.cellHeight);
        const double tolerance=bake.cellHeight*.01+std::abs(origin)*2.e-7;
        if(!std::isfinite(origin) || std::abs(header.bmax[1]-high)>tolerance)
            return fail("Navigation compressed layer vertical raster metadata is invalid");
        int written=0;
        if(dtStatusFailed(compressor.decompress(layer.bytes.data()+headerBytes,
            static_cast<int>(layer.bytes.size()-headerBytes),decoded.data(),static_cast<int>(rawBytes),&written)) ||
            written!=static_cast<int>(rawBytes))
            return fail("Navigation compressed layer stream is truncated or has an invalid decoded size");
        for(std::size_t pixel=0;pixel<static_cast<std::size_t>(pixels);++pixel)
        {
            const auto height=decoded[pixel],area=decoded[static_cast<std::size_t>(pixels)+pixel];
            if(area && (height==255 || !knownAreas[area]))
                return fail("Navigation compressed layer height or Area is invalid");
            const auto x=pixel%header.width,z=pixel/header.width;
            if(area && (x<header.minx || x>header.maxx || z<header.miny || z>header.maxy))
                return fail("Navigation compressed layer Area lies outside its usable bounds");
            const auto connections=decoded[static_cast<std::size_t>(pixels)*2u+pixel];
            if((!area && connections) || ((connections&15u)&(connections>>4u)))
                return fail("Navigation compressed layer connection mask is invalid");
            const int offsetX[]{-1,0,1,0},offsetZ[]{0,1,0,-1};
            for(int direction=0;direction<4;++direction)
            {
                if(!(connections&(1u<<direction))) continue;
                const int nextX=static_cast<int>(x)+offsetX[direction],nextZ=static_cast<int>(z)+offsetZ[direction];
                if(nextX<0 || nextZ<0 || nextX>=header.width || nextZ>=header.height ||
                    !decoded[static_cast<std::size_t>(pixels)+static_cast<std::size_t>(nextX+nextZ*header.width)])
                    return fail("Navigation compressed layer connection addresses an invalid neighbor");
            }
        }
    }
    error.clear(); return true;
}
catch(const std::bad_alloc&) { error="Navigation compressed layer validation allocation failed"; return false; }
bool ValidateNavigationTileLayerData(const std::vector<VansNavigationTileLayerData>& rows,std::uint64_t baseHash,
    const VansNavigationBakeSettings& bake,const VansNavigationAreaSettings& areas,std::uint64_t maximumBytes,std::string& error)
{
    if(!rows.empty() && (!baseHash || rows.size()>static_cast<std::size_t>(bake.maximumTiles)))
    { error="Navigation layer rows have an invalid base fingerprint or capacity"; return false; }
    std::uint64_t bytes=rows.capacity()*sizeof(VansNavigationTileLayerData);
    for(const auto& row:rows)
    {
        const auto owned=row.EstimateBytes()-sizeof(row);
        if(owned>maximumBytes || bytes>maximumBytes-owned)
        { error="Navigation layer rows exceed byte capacity"; return false; }
        bytes+=owned;
    }
    if(bytes>maximumBytes) { error="Navigation layer rows exceed byte capacity"; return false; }
    for(std::size_t i=0;i<rows.size();++i)
    {
        const auto& row=rows[i];
        if(row.baseInputHash!=baseHash || row.layers.empty() || (i && !(VansNavigationTileCell{rows[i-1].x,rows[i-1].z}<VansNavigationTileCell{row.x,row.z})))
        { error="Navigation layer row ("+std::to_string(row.x)+","+std::to_string(row.z)+") has invalid identity: base "+
            std::to_string(row.baseInputHash)+"/"+std::to_string(baseHash)+", layers "+std::to_string(row.layers.size())+
            ", ordered "+std::to_string(!i || VansNavigationTileCell{rows[i-1].x,rows[i-1].z}<VansNavigationTileCell{row.x,row.z}); return false; }
        for(int axis=0;axis<3;++axis)
            if(!std::isfinite(row.boundsMinimum[axis]) || !std::isfinite(row.boundsMaximum[axis]) ||
                row.boundsMinimum[axis]>row.boundsMaximum[axis])
            { error="Navigation layer row raster bounds are invalid"; return false; }
        const auto layerBytes=row.EstimateBytes()-sizeof(row);
        if(!ValidateNavigationTileCacheLayerSet(row.layers,bake,areas,maximumBytes-(bytes-layerBytes),error)) return false;
        for(const auto& layer:row.layers)
        {
            if(layer.x!=row.x || layer.z!=row.z)
            { error="Navigation layer belongs to a different row"; return false; }
            dtTileCacheLayerHeader header{}; std::memcpy(&header,layer.bytes.data(),sizeof(header));
            for(int axis=0;axis<3;++axis)
            {
                // 栅格 span 的顶面可比输入三角形 bounds 高一个 voxel。
                const double tolerance=axis==1?bake.cellHeight*2.01:bake.cellSize*.01;
                if(header.bmin[axis]+tolerance<row.boundsMinimum[axis] || header.bmax[axis]-tolerance>row.boundsMaximum[axis])
                { error="Navigation layer exceeds its source raster bounds"; return false; }
            }
        }
    }
    error.clear(); return true;
}

}

