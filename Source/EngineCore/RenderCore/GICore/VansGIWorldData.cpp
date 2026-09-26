#include "VansGIWorldData.h"
#include "../../TerrainCore/VansTerrainAsset.h"
#include "../../TerrainCore/VansTerrainHeightEncoding.h"
#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <cfloat>
#include <climits>
#include <cstring>
#include <numeric>

namespace VansGraphics
{
    bool GIWorldCell::operator<(const GIWorldCell& b) const
    { return std::tie(level, x, y, z) < std::tie(b.level, b.x, b.y, b.z); }
    bool GIWorldCell::operator==(const GIWorldCell& b) const
    { return x == b.x && y == b.y && z == b.z && level == b.level; }
    uint32_t GIWorldHash(const GIWorldCell& c)
    {
        uint32_t h = uint32_t(c.x) * 73856093u ^ uint32_t(c.y) * 19349663u ^ uint32_t(c.z) * 83492791u ^ uint32_t(c.level) * 2654435761u;
        h ^= h >> 16u; h *= 2246822519u; return h ^ (h >> 13u);
    }
    uint32_t GIWorldEncodeNormal(glm::vec3 n)
    {
        const float length = glm::length(n);
        n = length > 1e-12f ? n / length : glm::vec3(0,1,0);
        n /= std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
        glm::vec2 p(n.x, n.y);
        if (n.z < 0) p = (glm::vec2(1) - glm::abs(glm::vec2(p.y, p.x))) * glm::vec2(p.x < 0 ? -1.f : 1.f, p.y < 0 ? -1.f : 1.f);
        return glm::packUnorm2x8(p * 0.5f + 0.5f);
    }
    glm::vec3 GIWorldDecodeNormal(uint32_t packed)
    {
        glm::vec2 p = glm::unpackUnorm2x8(uint16_t(packed)) * 2.f - 1.f;
        glm::vec3 n(p, 1.f - std::abs(p.x) - std::abs(p.y));
        if (n.z < 0) { n.x = (1.f - std::abs(p.y)) * (p.x < 0 ? -1.f : 1.f); n.y = (1.f - std::abs(p.x)) * (p.y < 0 ? -1.f : 1.f); }
        return glm::normalize(n);
    }
    glm::vec4 GIWorldTerrainColorData::Evaluate(uint32_t x,uint32_t y) const
    {
        const glm::vec2 fraction=glm::fract((glm::vec2(x,y)+.5f)*glm::vec2(width,height)/float(Resolution)-.5f);
        const size_t first=(size_t(y)*Resolution+x)*4;glm::vec4 weights[2];
        for(uint32_t map=0;map<2;++map)
            weights[map]=glm::mix(glm::mix(glm::unpackUnorm4x8(corners[map][first]),glm::unpackUnorm4x8(corners[map][first+1]),fraction.x),
                glm::mix(glm::unpackUnorm4x8(corners[map][first+2]),glm::unpackUnorm4x8(corners[map][first+3]),fraction.x),fraction.y);
        glm::vec4 color(0);float total=0;
        for(size_t layer=0;layer<layers.size();++layer){const float w=weights[layer/4][layer%4];color+=layers[layer]*w;total+=w;}
        return total>0?color/total:(layers.empty()?glm::vec4(.5f,.5f,.5f,1):layers[0]);
    }
    bool GIWorldTerrainColorData::Build(uint32_t sourceWidth,uint32_t sourceHeight,std::array<std::vector<uint32_t>,2> sourceCorners,
        std::vector<glm::vec4> layerColors,std::string& error)
    {
        error.clear();
        if(!sourceWidth || !sourceHeight || sourceWidth>16384 || sourceHeight>16384 || layerColors.size()>8 ||
            sourceCorners[0].size()!=size_t(Resolution)*Resolution*4 || sourceCorners[1].size()!=sourceCorners[0].size())
        {error="Invalid GI terrain color footprint";return false;}
        for(auto color:layerColors)for(int c=0;c<4;++c)if(!std::isfinite(color[c])){error="Invalid GI terrain layer color";return false;}
        width=sourceWidth;height=sourceHeight;corners=std::move(sourceCorners);layers=std::move(layerColors);
        colors.resize(size_t(Resolution)*Resolution);
        for(uint32_t y=0;y<Resolution;++y)for(uint32_t x=0;x<Resolution;++x)colors[y*Resolution+x]=Evaluate(x,y);
        return true;
    }
    bool GIWorldTerrainColorData::ApplyPatch(uint32_t map,uint32_t x,uint32_t y,uint32_t w,uint32_t h,
        const std::vector<uint8_t>& pixels,Patch& changed,std::string& error)
    {
        error.clear();changed={};
        if(map>=2 || !width || !height || !w || !h || x>=width || y>=height || w>width-x || h>height-y || pixels.size()!=uint64_t(w)*h*4)
        {error="Invalid GI terrain color patch";return false;}
        const auto lo=glm::ivec2(glm::clamp(glm::floor((glm::vec2(x,y)-.5f)*float(Resolution)/glm::vec2(width,height)-.5f),glm::vec2(0),glm::vec2(Resolution-1)));
        const auto hi=glm::ivec2(glm::clamp(glm::ceil((glm::vec2(x+w,y+h)+.5f)*float(Resolution)/glm::vec2(width,height)-.5f),glm::vec2(0),glm::vec2(Resolution-1)));
        for(int row=lo.y;row<=hi.y;++row)
        {
            uint32_t first=Resolution,last=0;
            for(int column=lo.x;column<=hi.x;++column)
            {
                const glm::ivec2 a(glm::floor((glm::vec2(column,row)+.5f)*glm::vec2(width,height)/float(Resolution)-.5f));
                const size_t address=(size_t(row)*Resolution+column)*4;
                for(uint32_t corner=0;corner<4;++corner)
                {
                    const glm::uvec2 p(glm::clamp(a+glm::ivec2(corner&1u,corner>>1u),glm::ivec2(0),glm::ivec2(width-1,height-1)));
                    if(p.x<x || p.x>=x+w || p.y<y || p.y>=y+h)continue;
                    const size_t source=(size_t(p.y-y)*w+p.x-x)*4;
                    corners[map][address+corner]=uint32_t(pixels[source])|(uint32_t(pixels[source+1])<<8u)|
                        (uint32_t(pixels[source+2])<<16u)|(uint32_t(pixels[source+3])<<24u);
                }
                const auto color=Evaluate(uint32_t(column),uint32_t(row));auto& previous=colors[size_t(row)*Resolution+column];
                if(color==previous)continue;
                previous=color;first=std::min(first,uint32_t(column));last=uint32_t(column);
            }
            if(first!=Resolution)
            {
                changed.spans.push_back({uint32_t(row)*Resolution+first,last-first+1});
                changed.first=glm::min(changed.first,glm::uvec2(first,row));changed.last=glm::max(changed.last,glm::uvec2(last,row));
            }
        }
        return true;
    }
    bool GIWorldHeightData::Build(const Vans::VansTerrainAsset& source, std::string& error)
    {
        error.clear();
        if (source.width < 2 || source.height < 2 || source.heights.size() != uint64_t(source.width) * source.height ||
            !std::isfinite(source.settings.terrainSize) || source.settings.terrainSize <= 0 ||
            !std::isfinite(source.settings.maxHeight) || !std::isfinite(source.settings.heightOffset))
        { error = "Invalid GI terrain height source"; return false; }
        width = source.width; height = source.height;
        parameters = {source.settings.terrainSize, source.settings.maxHeight, source.settings.heightOffset, float(width)};
        heights.resize(source.heights.size());
        for (size_t i = 0; i < heights.size(); ++i)
            heights[i] = Vans::VansTerrainHeightEncoding::DecodeWorld(
                source.heights[i], parameters.y, parameters.z);
        ranges.clear(); levels.clear();
        // 含边界钳制的 N+1 个采样单元，不遗漏两侧半 texel。
        uint32_t w = width + 1, h = height + 1;
        levels.push_back({0,w,h,0}); ranges.resize(size_t(w)*h);
        for (uint32_t z=0;z<h;++z) for (uint32_t x=0;x<w;++x)
        {
            const uint32_t x0=x ? x-1 : 0, z0=z ? z-1 : 0, x1=std::min(x,width-1), z1=std::min(z,height-1);
            float a=heights[z0*width+x0], b=heights[z0*width+x1], c=heights[z1*width+x0], d=heights[z1*width+x1];
            ranges[z*w+x]={std::min({a,b,c,d})-1e-4f,std::max({a,b,c,d})+1e-4f};
        }
        while(w>1 || h>1)
        {
            auto previous=levels.back(); w=(w+1)/2;h=(h+1)/2;
            auto offset=uint32_t(ranges.size()); levels.push_back({offset,w,h,0}); ranges.resize(offset+size_t(w)*h);
            for(uint32_t z=0;z<h;++z)for(uint32_t x=0;x<w;++x)
            {
                glm::vec2 range(std::numeric_limits<float>::max(),-std::numeric_limits<float>::max());
                for(uint32_t dz=0;dz<2;++dz)for(uint32_t dx=0;dx<2;++dx)
                { if(x*2+dx>=previous.y || z*2+dz>=previous.z)continue;
                  auto child=ranges[previous.x+(z*2+dz)*previous.y+x*2+dx];range.x=std::min(range.x,child.x);range.y=std::max(range.y,child.y); }
                ranges[offset+z*w+x]=range;
            }
        }
        return true;
    }
    bool GIWorldHeightData::ApplyPatch(uint32_t x,uint32_t z,uint32_t w,uint32_t h,
        const std::vector<uint8_t>& pixels,Patch& changed,std::string& error)
    {
        error.clear();changed={};
        if(!w || !h || x>=width || z>=height || w>width-x || h>height-z ||
            pixels.size()!=uint64_t(w)*h*2 || levels.empty())
        {error="Invalid GI terrain patch";return false;}
        float low=FLT_MAX,high=-FLT_MAX;
        for(uint32_t row=z;row<=z+h;++row)for(uint32_t col=x;col<=x+w;++col)
        {const auto old=ranges[levels[0].x+row*levels[0].y+col];low=std::min(low,old.x);high=std::max(high,old.y);}
        for(uint32_t row=0;row<h;++row)
        {
            const uint32_t offset=(z+row)*width+x;
            changed.heights.push_back({offset,w});
            for(uint32_t col=0;col<w;++col)
            {
                uint16_t value;std::memcpy(&value,pixels.data()+(size_t(row)*w+col)*2,2);
                float& dst=heights[offset+col];
                const float next=Vans::VansTerrainHeightEncoding::DecodeWorld(
                    value, parameters.y, parameters.z);
                low=std::min({low,dst,next});high=std::max({high,dst,next});dst=next;
            }
        }
        // 修改一个采样点会影响两侧单元，包含地图边缘半 texel 的钳制区。
        glm::uvec2 first(x,z),last(x+w,z+h);
        const glm::vec2 lo=glm::max((glm::vec2(first)-.5f)/glm::vec2(width,height)-.5f,glm::vec2(-.5f))*parameters.x;
        const glm::vec2 hi=glm::min((glm::vec2(last)+.5f)/glm::vec2(width,height)-.5f,glm::vec2(.5f))*parameters.x;
        for(size_t level=0;level<levels.size();++level)
        {
            const auto info=levels[level];
            for(uint32_t row=first.y;row<=last.y;++row)
            {
                changed.ranges.push_back({info.x+row*info.y+first.x,last.x-first.x+1});
                for(uint32_t col=first.x;col<=last.x;++col)
                {
                    glm::vec2 range(FLT_MAX,-FLT_MAX);
                    if(level==0)
                    {
                        for(uint32_t dz=0;dz<2;++dz)for(uint32_t dx=0;dx<2;++dx)
                        {
                            const auto px=std::min(col?col-1+dx:0u,width-1);
                            const auto pz=std::min(row?row-1+dz:0u,height-1);
                            const float v=heights[pz*width+px];range.x=std::min(range.x,v);range.y=std::max(range.y,v);
                        }
                        range+=glm::vec2(-1e-4f,1e-4f);
                        low=std::min(low,range.x);high=std::max(high,range.y);
                    }
                    else
                    {
                        const auto child=levels[level-1];
                        for(uint32_t dz=0;dz<2;++dz)for(uint32_t dx=0;dx<2;++dx)
                        {
                            if(col*2+dx>=child.y || row*2+dz>=child.z)continue;
                            const auto v=ranges[child.x+(row*2+dz)*child.y+col*2+dx];
                            range.x=std::min(range.x,v.x);range.y=std::max(range.y,v.y);
                        }
                    }
                    ranges[info.x+row*info.y+col]=range;
                }
            }
            first/=2u;last/=2u;
        }
        changed.minimum={lo.x,low,lo.y};changed.maximum={hi.x,high,hi.y};
        return true;
    }
    float GIWorldHeightData::Sample(glm::vec2 world) const
    {
        if(heights.empty()) return 0;
        glm::vec2 p=glm::clamp((world/parameters.x+0.5f)*glm::vec2(width,height)-0.5f,glm::vec2(0),glm::vec2(width-1,height-1));
        glm::uvec2 a(p),b=glm::min(a+1u,glm::uvec2(width-1,height-1)); glm::vec2 f=glm::fract(p);
        return glm::mix(glm::mix(heights[a.y*width+a.x],heights[a.y*width+b.x],f.x),
            glm::mix(heights[b.y*width+a.x],heights[b.y*width+b.x],f.x),f.y);
    }
    namespace
    {
        bool Overlap(const std::array<glm::vec3,3>& p, glm::vec3 center, float half)
        {
            std::array<glm::vec3,3> v{p[0]-center,p[1]-center,p[2]-center};
            std::array<glm::vec3,3> e{v[1]-v[0],v[2]-v[1],v[0]-v[2]};
            auto separated=[&](glm::vec3 a) { float r=half*(std::abs(a.x)+std::abs(a.y)+std::abs(a.z));
                float x=glm::dot(a,v[0]),y=glm::dot(a,v[1]),z=glm::dot(a,v[2]);return std::min({x,y,z})>r || std::max({x,y,z})< -r; };
            for(int axis=0;axis<3;++axis){glm::vec3 a(0);a[axis]=1;if(separated(a))return false;for(auto edge:e)if(separated(glm::cross(edge,a)))return false;}
            return !separated(glm::cross(e[0],e[1]));
        }
        float ClippedArea(const std::array<glm::vec3,3>& triangle, glm::vec3 lo, glm::vec3 hi)
        {
            std::vector<glm::vec3> polygon(triangle.begin(),triangle.end()),next;
            for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side)
            {
                next.clear();if(polygon.empty())return 0;
                auto distance=[&](glm::vec3 v){return side?hi[axis]-v[axis]:v[axis]-lo[axis];};
                auto previous=polygon.back();float dp=distance(previous);
                for(auto current:polygon){float dc=distance(current);
                    if((dp>=0)!=(dc>=0))next.push_back(previous+(current-previous)*(dp/(dp-dc)));
                    if(dc>=0)next.push_back(current);previous=current;dp=dc;}
                polygon.swap(next);
            }
            float area=0;for(size_t i=2;i<polygon.size();++i)area+=glm::length(glm::cross(polygon[i-1]-polygon[0],polygon[i]-polygon[0]))*.5f;
            return area;
        }
        glm::vec3 Barycentric(glm::vec3 p,const std::array<glm::vec3,3>& t)
        {
            auto a=t[1]-t[0],b=t[2]-t[0],c=p-t[0];float aa=glm::dot(a,a),ab=glm::dot(a,b),bb=glm::dot(b,b),d=aa*bb-ab*ab;
            if(std::abs(d)<1e-15f)return {1,0,0};
            float y=(bb*glm::dot(c,a)-ab*glm::dot(c,b))/d,z=(aa*glm::dot(c,b)-ab*glm::dot(c,a))/d;
            auto weights=glm::max(glm::vec3(1-y-z,y,z),glm::vec3(0));return weights/(weights.x+weights.y+weights.z);
        }
    }
    namespace
    {
        void IndexTemplateLevel(GIWorldTemplateLevel& level)
        {
            level.minimum=glm::vec3(FLT_MAX);level.maximum=glm::vec3(-FLT_MAX);
            level.spatialBricks.clear();
            for(const auto& item:level.cells)
            {
                glm::vec3 lo=glm::vec3(item.first.x,item.first.y,item.first.z)*level.voxelSize;
                level.minimum=glm::min(level.minimum,lo);level.maximum=glm::max(level.maximum,lo+level.voxelSize);
                glm::ivec3 b(glm::floor(glm::vec3(item.first.x,item.first.y,item.first.z)/8.f));
                level.spatialBricks[{b.x,b.y,b.z,0}].push_back(item.first);
            }
            if(level.cells.empty())level.minimum=level.maximum=glm::vec3(0);
        }
        void TransformedBounds(const GIWorldTemplateLevel& model,const glm::mat4& transform,glm::vec3& lo,glm::vec3& hi)
        {
            lo=glm::vec3(FLT_MAX);hi=glm::vec3(-FLT_MAX);
            for(uint32_t c=0;c<8;++c)
            {
                glm::vec3 p;for(int a=0;a<3;++a)p[a]=(c&(1u<<a))?model.maximum[a]:model.minimum[a];
                p=glm::vec3(transform*glm::vec4(p,1));lo=glm::min(lo,p);hi=glm::max(hi,p);
            }
        }
        bool PreferSurface(float weight,uint32_t surface,float oldWeight,uint32_t oldSurface)
        { return weight>oldWeight || (weight==oldWeight && surface<oldSurface); }
    }
    size_t GIWorldTemplate::CellCount() const
    { size_t count=cells.size();for(const auto& level:coarseLevels)count+=level.cells.size();return count; }
    const GIWorldTemplateLevel& GIWorldTemplate::SelectLevel(float worldVoxelSize,const glm::mat4& transform) const
    {
        const float maxScale=std::max({glm::length(glm::vec3(transform[0])),glm::length(glm::vec3(transform[1])),glm::length(glm::vec3(transform[2]))});
        const GIWorldTemplateLevel* selected=this;
        for(const auto& level:coarseLevels)
        { if(level.voxelSize*maxScale>worldVoxelSize)break;selected=&level; }
        return *selected;
    }
    bool BakeGIWorldTemplate(const std::vector<GIWorldTriangle>& triangles,float size,uint32_t maxCells,GIWorldTemplate& output,std::string& error)
    {
        error.clear(); GIWorldTemplate result;result.voxelSize=size;
        if(!std::isfinite(size)||size<=0){error="Invalid voxel size";return false;}
        result.minimum=glm::vec3(std::numeric_limits<float>::max());result.maximum=-result.minimum;
        uint64_t visits=0;
        std::map<GIWorldCell,float> dominant;
        for(const auto& t:triangles)
        {
            auto lo=glm::min(t.positions[0],glm::min(t.positions[1],t.positions[2]));auto hi=glm::max(t.positions[0],glm::max(t.positions[1],t.positions[2]));
            if(glm::any(glm::isnan(lo))||glm::any(glm::isinf(lo))||glm::any(glm::isnan(hi))||glm::any(glm::isinf(hi))) {error="Non-finite voxel triangle";return false;}
            result.minimum=glm::min(result.minimum,lo);result.maximum=glm::max(result.maximum,hi);
            glm::ivec3 a(glm::floor(lo/size)),b(glm::floor(hi/size));
            const auto n=glm::cross(t.positions[1]-t.positions[0],t.positions[2]-t.positions[0]);if(glm::dot(n,n)<1e-20f)continue;
            for(int z=a.z;z<=b.z;++z)for(int y=a.y;y<=b.y;++y)for(int x=a.x;x<=b.x;++x)
            {
                if(++visits>100000000ull){error="Voxel template build exceeds work budget";return false;}
                glm::vec3 center=(glm::vec3(x,y,z)+0.5f)*size;if(!Overlap(t.positions,center,size*0.50001f))continue;
                const auto w=Barycentric(center,t.positions);const auto uv=t.uvs[0]*w.x+t.uvs[1]*w.y+t.uvs[2]*w.z;
                if(t.alpha && t.alpha(uv)<t.alphaCutoff)continue;
                const GIWorldCell key{x,y,z,0};auto& v=result.cells[key];
                const uint32_t surface=(t.material&65535u)|(GIWorldEncodeNormal(n)<<16u);
                const float contribution=0.35f*ClippedArea(t.positions,center-glm::vec3(size*.5f),center+glm::vec3(size*.5f))/(size*size*size);
                if(t.porous && !(v.optical&0x10000u))
                {
                    const float old=glm::unpackHalf1x16(uint16_t(v.optical));
                    v.optical=glm::packHalf1x16(std::min(old+contribution,64.f/size));
                    if(PreferSurface(contribution,surface,dominant[key],v.surface)){v.surface=surface;dominant[key]=contribution;}
                }
                else if(!t.porous)
                {
                    // 叶片不得覆盖已经命中的木质材质；同类材料按贡献确定，避免提交顺序影响。
                    if(!(v.optical&0x10000u) || PreferSurface(contribution,surface,dominant[key],v.surface))
                    {v.surface=surface;dominant[key]=contribution;}
                    v.optical=0x10000u;
                }
                if(result.cells.size()>maxCells){error="Voxel template exceeds cell budget";return false;}
            }
        }
        IndexTemplateLevel(result);
        // 各级缓存总数也计入资产预算。粗 mip 保存光学量，不把混合父格填成实心。
        size_t total=result.cells.size();
        for(uint32_t mip=1;mip<8 && !result.cells.empty();++mip)
        {
            const auto& child=mip==1?static_cast<const GIWorldTemplateLevel&>(result):result.coarseLevels.back();
            GIWorldTemplateLevel parent;parent.voxelSize=child.voxelSize*2;
            struct Aggregate{double sigma=0;float dominant=0;uint32_t surface=0;};
            std::map<GIWorldCell,Aggregate> aggregate;
            for(const auto& item:child.cells)
            {
                const auto& v=item.second;
                const float sigma=(v.optical&0x10000u)?4.f/child.voxelSize:glm::unpackHalf1x16(uint16_t(v.optical));
                if(sigma<=0)continue;
                glm::ivec3 key(glm::floor(glm::vec3(item.first.x,item.first.y,item.first.z)*.5f));
                auto& dst=aggregate[{key.x,key.y,key.z,0}];dst.sigma+=sigma/8.0;
                if(PreferSurface(sigma,v.surface,dst.dominant,dst.surface)){dst.dominant=sigma;dst.surface=v.surface;}
            }
            for(const auto& item:aggregate)
                parent.cells[item.first]={glm::packHalf1x16(float(std::min(item.second.sigma,65504.0))),item.second.surface};
            total+=parent.cells.size();
            if(total>maxCells){error="Voxel template mip chain exceeds cell budget";return false;}
            IndexTemplateLevel(parent);result.coarseLevels.push_back(std::move(parent));
        }
        output=std::move(result);return true;
    }
    GIWorldInstance MakeGIWorldInstance(std::shared_ptr<const GIWorldTemplate> model,const glm::mat4& transform)
    {
        GIWorldInstance i;i.model=std::move(model);i.transform=transform;i.inverse=glm::inverse(transform);
        i.minimum=glm::vec3(std::numeric_limits<float>::max());i.maximum=-i.minimum;
        for(uint32_t c=0;c<8;++c){glm::vec3 p;for(int a=0;a<3;++a)p[a]=(c&(1u<<a))?i.model->maximum[a]:i.model->minimum[a];
            p=glm::vec3(transform*glm::vec4(p,1));i.minimum=glm::min(i.minimum,p);i.maximum=glm::max(i.maximum,p);}
        return i;
    }
    struct VansGIWorldBuilder::SourceSnapshot
    {
        struct Bounds { glm::vec3 lo{FLT_MAX},hi{-FLT_MAX}; };
        struct Node { Bounds bounds; uint32_t first=0,count=0,left=0,right=0; };
        std::vector<GIWorldInstance> instances;
        std::vector<Bounds> bounds;
        std::vector<uint32_t> order;
        std::vector<Node> nodes;
        std::vector<GIWorldCell> coarsePages;
        std::map<GIWorldCell,std::vector<uint32_t>> coarseCandidates;
        bool coarseOverflow=false;
        uint32_t levelCount=0;

        uint32_t BuildNode(uint32_t first,uint32_t count)
        {
            const uint32_t address=uint32_t(nodes.size());nodes.emplace_back();
            Bounds box;
            for(uint32_t j=first;j<first+count;++j)
            {box.lo=glm::min(box.lo,bounds[order[j]].lo);box.hi=glm::max(box.hi,bounds[order[j]].hi);}
            nodes[address].bounds=box;
            if(count<=8){nodes[address].first=first;nodes[address].count=count;return address;}
            const auto extent=box.hi-box.lo;int axis=extent.y>extent.x?1:0;if(extent.z>extent[axis])axis=2;
            const uint32_t middle=first+count/2;
            std::nth_element(order.begin()+first,order.begin()+middle,order.begin()+first+count,[&](uint32_t a,uint32_t b)
            {
                const float ca=bounds[a].lo[axis]+bounds[a].hi[axis],cb=bounds[b].lo[axis]+bounds[b].hi[axis];
                return ca==cb?a<b:ca<cb;
            });
            const uint32_t left=BuildNode(first,count/2),right=BuildNode(middle,count-count/2);
            nodes[address].left=left;nodes[address].right=right;return address;
        }
        static bool Intersects(const Bounds& a,const Bounds& b)
        {return glm::all(glm::lessThanEqual(a.lo,b.hi))&&glm::all(glm::lessThanEqual(b.lo,a.hi));}
        void Query(uint32_t node,const Bounds& box,std::vector<uint32_t>& found,uint64_t& tests) const
        {
            const auto& n=nodes[node];if(!Intersects(n.bounds,box))return;
            if(n.count)
            {
                for(uint32_t j=n.first;j<n.first+n.count;++j)
                {++tests;const auto index=order[j];if(Intersects(bounds[index],box))found.push_back(index);}
            }
            else{Query(n.left,box,found,tests);Query(n.right,box,found,tests);}
        }
        bool Append(uint32_t index,int level,const GIWorldSettings& settings,const Bounds* clip,size_t capacity,
            std::map<GIWorldCell,std::vector<uint32_t>>& candidates,std::vector<GIWorldCell>& pages) const
        {
            const auto& instance=instances[index];const float size=settings.voxelSize*float(1u<<level)*8;
            Bounds box;TransformedBounds(instance.model->SelectLevel(size/8,instance.transform),instance.transform,box.lo,box.hi);
            if(clip){box.lo=glm::max(box.lo,clip->lo);box.hi=glm::min(box.hi,clip->hi);}
            if(glm::any(glm::greaterThan(box.lo,box.hi)))return true;
            const glm::ivec3 a(glm::floor(box.lo/size)),b(glm::floor(box.hi/size));
            for(int z=a.z;z<=b.z;++z)for(int y=a.y;y<=b.y;++y)for(int x=a.x;x<=b.x;++x)
            {
                const GIWorldCell key{x,y,z,level};auto found=candidates.find(key);
                if(found==candidates.end())
                {
                    // 失败的层会整体撤销，不再扫描其余实例或巨大包围盒。
                    if(candidates.size()>=capacity)return false;
                    found=candidates.emplace(key,std::vector<uint32_t>{}).first;pages.push_back(key);
                }
                found->second.push_back(index);
            }
            return true;
        }
    };
    const std::vector<GIWorldInstance>& VansGIWorldBuilder::Instances() const
    {static const std::vector<GIWorldInstance> empty;return m_Sources?m_Sources->instances:empty;}
    std::vector<GIWorldBounds> GIWorldChangedInstanceRegions(
        const std::vector<GIWorldInstance>& previous,const std::vector<GIWorldInstance>& next)
    {
        using Key=std::pair<uintptr_t,std::array<uint32_t,16>>;
        struct Entry { int64_t count=0; const GIWorldInstance* instance=nullptr; };
        std::map<Key,Entry> changes;
        auto append=[&](const auto& instances,int delta)
        {
            for(const auto& instance:instances)
            {
                Key key;key.first=reinterpret_cast<uintptr_t>(instance.model.get());
                static_assert(sizeof(instance.transform)==sizeof(key.second));
                std::memcpy(key.second.data(),&instance.transform,sizeof(instance.transform));
                auto& entry=changes[key];entry.count+=delta;entry.instance=&instance;
            }
        };
        append(previous,-1);append(next,1);
        std::vector<GIWorldBounds> changed;
        for(const auto& item:changes)if(item.second.count)
        {
            const auto& instance=*item.second.instance;
            GIWorldBounds bounds{glm::vec3(FLT_MAX),glm::vec3(-FLT_MAX)};
            auto include=[&](const GIWorldTemplateLevel& model)
            {
                glm::vec3 lo,hi;TransformedBounds(model,instance.transform,lo,hi);
                bounds.minimum=glm::min(bounds.minimum,lo);bounds.maximum=glm::max(bounds.maximum,hi);
            };
            include(*instance.model);for(const auto& level:instance.model->coarseLevels)include(level);
            changed.push_back(bounds);
        }
        return changed;
    }
    GIWorldDirtyRegions::GIWorldDirtyRegions(std::vector<GIWorldBounds> regions)
    {
        if(regions.empty())return;
        m_Nodes.reserve(regions.size()*2-1);Build(regions,0,regions.size());
    }
    void GIWorldDirtyRegions::Build(std::vector<GIWorldBounds>& regions,size_t first,size_t count)
    {
        GIWorldBounds bounds{glm::vec3(FLT_MAX),glm::vec3(-FLT_MAX)};
        for(size_t i=first;i<first+count;++i)
        {bounds.minimum=glm::min(bounds.minimum,regions[i].minimum);bounds.maximum=glm::max(bounds.maximum,regions[i].maximum);}
        const size_t node=m_Nodes.size();m_Nodes.push_back({bounds,0});
        if(count>1)
        {
            const auto extent=bounds.maximum-bounds.minimum;
            int axis=extent.y>extent.x?1:0;if(extent.z>extent[axis])axis=2;
            const size_t middle=first+count/2;
            std::nth_element(regions.begin()+first,regions.begin()+middle,regions.begin()+first+count,
                [axis](const auto& a,const auto& b){return a.minimum[axis]+a.maximum[axis]<b.minimum[axis]+b.maximum[axis];});
            Build(regions,first,count/2);Build(regions,middle,count-count/2);
        }
        m_Nodes[node].end=m_Nodes.size();
    }
    bool GIWorldDirtyRegions::Intersects(const GIWorldBounds& bounds,float radius) const
    {
        const float squared=radius*radius;
        for(size_t i=0;i<m_Nodes.size();)
        {
            const auto& node=m_Nodes[i];
            const auto distance=glm::max(glm::max(node.bounds.minimum-bounds.maximum,bounds.minimum-node.bounds.maximum),glm::vec3(0));
            if(glm::dot(distance,distance)>squared)i=node.end;
            else if(node.end==i+1)return true;
            else ++i;
        }
        return false;
    }
    bool GIWorldDirtyRegions::IntersectsPage(const GIWorldCell& page,float voxelSize) const
    {
        const float size=voxelSize*float(1u<<page.level)*8.f;
        const auto lo=glm::vec3(page.x,page.y,page.z)*size;
        return Intersects({lo,lo+size});
    }
    void VansGIWorldBuilder::Reset(const GIWorldSettings& settings,std::vector<GIWorldInstance> instances,glm::vec3 center)
    {
        m_Settings=settings;m_Sources.reset();
        if(settings.enabled)
        {
            auto source=std::make_shared<SourceSnapshot>();source->instances=std::move(instances);
            // 远场最多占一半砖池，保留相机邻域的精度空间；所有来源仍进入同一体素体系。
            const uint32_t coarseBudget=std::max(settings.maxBricks/2,1u);
            source->coarseOverflow=true;
            for(uint32_t count=settings.levelCount;count<=GIWorldMaxLevels;++count)
            {
                source->levelCount=count;source->coarseCandidates.clear();source->coarsePages.clear();
                bool fits=true;
                for(uint32_t index=0;index<source->instances.size();++index)
                    if(!source->Append(index,int(count)-1,settings,nullptr,coarseBudget,source->coarseCandidates,source->coarsePages))
                    {fits=false;break;}
                if(fits){source->coarseOverflow=false;break;}
            }
            if(source->coarseOverflow){source->coarseCandidates.clear();source->coarsePages.clear();}
            source->bounds.resize(source->instances.size());source->order.resize(source->instances.size());
            std::iota(source->order.begin(),source->order.end(),0u);
            for(uint32_t index=0;index<source->instances.size();++index)
            {
                const auto& instance=source->instances[index];auto& bounds=source->bounds[index];
                // 光学 mip 可以扩展模型边界，索引必须覆盖所有被选中的 mip。
                for(uint32_t level=0;level<source->levelCount;++level)
                {
                    glm::vec3 lo,hi;TransformedBounds(instance.model->SelectLevel(settings.voxelSize*float(1u<<level),instance.transform),instance.transform,lo,hi);
                    bounds.lo=glm::min(bounds.lo,lo);bounds.hi=glm::max(bounds.hi,hi);
                }
            }
            if(!source->instances.empty())source->BuildNode(0,uint32_t(source->instances.size()));
            m_Sources=std::move(source);
        }
        PlanPages(center);
    }
    void VansGIWorldBuilder::Recenter(const VansGIWorldBuilder& source,glm::vec3 center)
    {m_Settings=source.m_Settings;m_Sources=source.m_Sources;PlanPages(center);}
    void VansGIWorldBuilder::PlanPages(glm::vec3 center)
    {
        m_Center=center;m_Pages.clear();m_Candidates.clear();m_Next=0;m_Overflow=false;m_AvailableLevels=0;
        m_Build.reset();m_TemplateSamples=0;m_PlanningInstanceTests=0;
        m_LevelCount=m_Sources?m_Sources->levelCount:0;
        if(!m_Sources)return;
        m_Pages=m_Sources->coarsePages;m_Overflow=m_Sources->coarseOverflow;
        if(!m_Overflow)m_AvailableLevels=1u<<(m_LevelCount-1);
        const size_t capacity=m_Settings.maxBricks-m_Pages.size();
        for(int level=0;level<int(m_LevelCount)-1;++level)
        {
            const float radius=std::min(m_Settings.coverageDistance,m_Settings.voxelSize*float(1u<<level)*32);
            const SourceSnapshot::Bounds clip{center-glm::vec3(radius),center+glm::vec3(radius)};
            std::vector<uint32_t> candidates;
            if(!m_Sources->nodes.empty())m_Sources->Query(0,clip,candidates,m_PlanningInstanceTests);
            // 恢复来源顺序，保证光学累加和材质选择可重复。
            std::sort(candidates.begin(),candidates.end());
            const size_t first=m_Pages.size();bool full=false;
            for(auto index:candidates)
                if(!m_Sources->Append(index,level,m_Settings,&clip,capacity,m_Candidates,m_Pages)){full=true;break;}
            if(full){for(size_t j=first;j<m_Pages.size();++j)m_Candidates.erase(m_Pages[j]);m_Pages.resize(first);m_Overflow=true;}
            else m_AvailableLevels|=1u<<level;
        }
    }
    bool VansGIWorldBuilder::BuildNext(GIWorldBrick& brick,uint32_t maxWork)
    {
        if(m_Next>=m_Pages.size() || maxWork==0)return false;
        if(!m_Build){m_Build.emplace();m_Build->brick.key=m_Pages[m_Next];}
        auto& build=*m_Build;const float h=m_Settings.voxelSize*float(1u<<build.brick.key.level);
        const glm::vec3 origin=glm::vec3(build.brick.key.x,build.brick.key.y,build.brick.key.z)*(h*8);
        const auto& candidates=build.brick.key.level==int(m_LevelCount)-1?
            m_Sources->coarseCandidates.at(build.brick.key):m_Candidates.at(build.brick.key);
        uint32_t work=0;
        while(build.candidate<candidates.size())
        {
            if(work>=maxWork)return false;
            const auto& instance=m_Sources->instances[candidates[build.candidate]];
            if(!build.instanceReady)
            {
                ++work;build.model=&instance.model->SelectLevel(h,instance.transform);
                const auto& model=*build.model;
                build.normalMatrix=glm::transpose(glm::mat3(instance.inverse));
                glm::vec3 localLo(FLT_MAX),localHi(-FLT_MAX);
                for(uint32_t corner=0;corner<8;++corner)
                {
                    glm::vec3 p=origin;for(int axis=0;axis<3;++axis)if(corner&(1u<<axis))p[axis]+=h*8;
                    p=glm::vec3(instance.inverse*glm::vec4(p,1));localLo=glm::min(localLo,p);localHi=glm::max(localHi,p);
                }
                build.first=glm::ivec3(glm::floor(localLo/(model.voxelSize*8)));
                build.last=glm::ivec3(glm::floor(localHi/(model.voxelSize*8)));
                const glm::mat3 transform(instance.transform);const float determinant=std::abs(glm::determinant(transform));
                build.volume=determinant*std::pow(model.voxelSize,3.f);build.scale=std::cbrt(determinant);
                build.minScale=std::min({glm::length(transform[0]),glm::length(transform[1]),glm::length(transform[2])});
                build.extent=(glm::abs(transform[0])+glm::abs(transform[1])+glm::abs(transform[2]))*(model.voxelSize*.5f);
                build.boxVolume=8*build.extent.x*build.extent.y*build.extent.z;
                build.bucket=model.spatialBricks.lower_bound({build.first.x,INT_MIN,INT_MIN,0});
                build.cell=0;build.instanceReady=true;
                continue;
            }
            const auto& model=*build.model;
            if(build.bucket==model.spatialBricks.end() || build.bucket->first.x>build.last.x)
            {++build.candidate;build.instanceReady=false;continue;}
            const auto& bucket=*build.bucket;
            if(bucket.first.y<build.first.y || bucket.first.y>build.last.y || bucket.first.z<build.first.z ||
                bucket.first.z>build.last.z || build.cell>=bucket.second.size())
            {++work;++build.bucket;build.cell=0;continue;}
            ++work;++m_TemplateSamples;
            const auto& key=bucket.second[build.cell++];const auto& src=model.cells.at(key);
            const glm::vec3 center=glm::vec3(instance.transform*glm::vec4((glm::vec3(key.x,key.y,key.z)+.5f)*model.voxelSize,1));
            const glm::vec3 lo=center-build.extent,hi=center+build.extent;
            const glm::ivec3 a=glm::max(glm::ivec3(glm::floor((lo-origin)/h)),glm::ivec3(0));
            const glm::ivec3 b=glm::min(glm::ivec3(glm::floor((hi-origin)/h)),glm::ivec3(7));
            for(int z=a.z;z<=b.z;++z)for(int y=a.y;y<=b.y;++y)for(int x=a.x;x<=b.x;++x)
            {
                glm::vec3 cellLo=origin+glm::vec3(x,y,z)*h;
                glm::vec3 overlap=glm::max(glm::min(hi,cellLo+h)-glm::max(lo,cellLo),glm::vec3(0));
                float fraction=overlap.x*overlap.y*overlap.z/std::max(build.boxVolume,1e-20f)*build.volume/(h*h*h);
                if(fraction<=0)continue;
                const size_t address=x+8*(y+8*z);auto& dst=build.brick.voxels[address];
                const bool solid=(src.optical&0x10000u)&&h<=model.voxelSize*build.minScale*1.5f&&fraction>.25f;
                const float sigma=(src.optical&0x10000u)?4.f/(model.voxelSize*build.scale):glm::unpackHalf1x16(uint16_t(src.optical))/build.scale;
                const float contribution=sigma*fraction;
                const uint32_t surface=(src.surface&65535u)|(GIWorldEncodeNormal(build.normalMatrix*GIWorldDecodeNormal(src.surface>>16u))<<16u);
                if((solid && !(dst.optical&0x10000u)) ||
                    (solid==bool(dst.optical&0x10000u) && PreferSurface(contribution,surface,build.dominant[address],dst.surface)))
                {dst.surface=surface;build.dominant[address]=contribution;}
                // 粗格可能汇集大量微小贡献，逐项转 half 会将它们全部舍成零。
                build.extinction[address]+=contribution;
                dst.optical|=uint32_t(solid?0x10000u:0);
            }
        }
        for(size_t cell=0;cell<build.brick.voxels.size();++cell)
            build.brick.voxels[cell].optical|=glm::packHalf1x16(std::min(build.extinction[cell],65504.f));
        brick=build.brick;m_Build.reset();++m_Next;return true;
    }
}
