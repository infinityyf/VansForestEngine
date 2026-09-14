#include "VansWaterGeometryClipmap.h"
#include "../../PcgCore/VansPcgSplineField.h"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <cmath>

namespace VansGraphics
{
namespace
{
struct Edge
{
    float minimum=0,maximum=0;
    std::size_t patch=0;
    std::uint32_t mask=0;
};
using EdgeLine=std::array<std::vector<Edge>,2>;
void AdjacentPatches(const std::vector<WaterGeometryPatch>& patches,
    const std::function<void(const Edge&,const Edge&)>& visit)
{
    std::map<std::pair<int,float>,EdgeLine> lines;
    for(std::size_t i=0;i<patches.size();++i)
    {
        const auto& p=patches[i];const auto end=p.worldOrigin+p.worldSize;
        lines[{0,p.worldOrigin.x}][0].push_back({p.worldOrigin.y,end.y,i,EdgeLeft});
        lines[{0,end.x}][1].push_back({p.worldOrigin.y,end.y,i,EdgeRight});
        lines[{1,p.worldOrigin.y}][0].push_back({p.worldOrigin.x,end.x,i,EdgeDown});
        lines[{1,end.y}][1].push_back({p.worldOrigin.x,end.x,i,EdgeUp});
    }
    for(auto& [key,line]:lines)
    {
        for(auto& side:line)std::sort(side.begin(),side.end(),[](const auto& a,const auto& b){return a.minimum<b.minimum;});
        std::size_t a=0,b=0;
        while(a<line[0].size() && b<line[1].size())
        {
            const auto& left=line[0][a];const auto& right=line[1][b];
            if(std::max(left.minimum,right.minimum)<std::min(left.maximum,right.maximum)-.0001f)visit(left,right);
            if(left.maximum<right.maximum)++a;else ++b;
        }
    }
}
std::array<WaterGeometryPatch,4> Children(const WaterGeometryPatch& parent)
{
    std::array<WaterGeometryPatch,4> children;
    for(int i=0;i<4;++i)
    {
        auto& p=children[i];p=parent;p.worldSize=parent.worldSize*.5f;p.lodLevel=parent.lodLevel-1;
        p.worldOrigin=parent.worldOrigin+glm::vec2(float(i%2),float(i/2))*p.worldSize;
        p.worldCenter=p.worldOrigin+p.worldSize*.5f;p.outerEdgeMask=0;
    }
    return children;
}
}

bool VansWaterGeometryClipmap::ValidateRiverFieldBudget(const Vans::VansPcgSplineFieldSnapshot& field,
    const VansWaterGeometryConfig& config,std::string& error)
{
    // Conservative, camera-independent admission includes a neighbour halo and
    // the geometric sum of coarser 2:1 balancing rings. Reject before publication.
    double patches=16+12*(std::clamp(config.m_LodCount,1,MAX_LOD_COUNT)-1);
    for (const auto& [key,tile]:field.tiles) if (tile->hasRiver)
    {
        const double spacing=std::min(field.texelSize,tile->minimumRiverWidth/8.f);
        const double leafSize=.5*spacing*std::clamp(config.m_MeshDim-1,16,256);
        if (!(leafSize>0)) {error="River water mesh spacing is invalid.";return false;}
        const double side=std::ceil(field.texelSize*Vans::VANS_SPLINE_TILE_SIZE/leafSize)+8;
        patches+=4*side*side;
        if (patches>8192)
        {error="River water mesh exceeds the reserved 8192-patch budget; reduce coverage or increase field texel size / water mesh resolution.";return false;}
    }
    return true;
}

bool VansWaterGeometryClipmap::RefineForRiverFields(const Vans::VansPcgSplineFieldSnapshot& field)
{
    if(std::none_of(field.tiles.begin(),field.tiles.end(),[](const auto& item){return item.second->hasRiver;}))return true;
    constexpr std::size_t MaximumPatches=8192;
    const float tileSize=field.texelSize*Vans::VANS_SPLINE_TILE_SIZE;
    const int count=int((field.resolution+Vans::VANS_SPLINE_TILE_SIZE-1)/Vans::VANS_SPLINE_TILE_SIZE);
    const auto influence=[&](WaterGeometryPatch& patch) {
        const auto start=glm::max(glm::ivec2(glm::floor((patch.worldOrigin+field.worldSize*.5f)/tileSize)),glm::ivec2(0));
        const auto end=glm::min(glm::ivec2(glm::floor((patch.worldOrigin+patch.worldSize+field.worldSize*.5f)/tileSize)),glm::ivec2(count-1));
        float spacing=patch.worldSize;
        patch.riverInfluenced=false;
        for(int z=start.y;z<=end.y;++z)for(int x=start.x;x<=end.x;++x)
        {
            const auto* tile=field.FindTile(x,z);if(!tile || !tile->hasRiver)continue;
            spacing=std::min(spacing,std::min(field.texelSize,tile->minimumRiverWidth/8.0f));
            if(!patch.riverInfluenced){patch.minimumRiverHeight=tile->minimumWaterHeight;patch.maximumRiverHeight=tile->maximumWaterHeight;}
            else {patch.minimumRiverHeight=std::min(patch.minimumRiverHeight,tile->minimumWaterHeight);patch.maximumRiverHeight=std::max(patch.maximumRiverHeight,tile->maximumWaterHeight);}
            patch.riverInfluenced=true;
        }
        return spacing;
    };
    std::vector<WaterGeometryPatch> refined;
    std::function<bool(WaterGeometryPatch)> refine=[&](WaterGeometryPatch patch) {
        const float spacing=influence(patch);
        if(patch.riverInfluenced && patch.worldSize/float(m_Config.m_MeshDim-1)>spacing)
        {
            for(const auto& child:Children(patch))if(!refine(child))return false;
            return true;
        }
        if(refined.size()>=MaximumPatches)return false;
        patch.outerEdgeMask=0;refined.push_back(patch);return true;
    };
    for(const auto& patch:m_Patches)if(!refine(patch))return false;
    // 按边线排序扫描邻接区间，避免逐块 O(N²) 查邻居。
    for(unsigned pass=0;pass<24;++pass)
    {
        std::set<std::size_t> split;
        AdjacentPatches(refined,[&](const auto& a,const auto& b) {
            const auto sa=refined[a.patch].worldSize,sb=refined[b.patch].worldSize;
            if(sa>sb*2.01f)split.insert(a.patch);
            if(sb>sa*2.01f)split.insert(b.patch);
        });
        if(split.empty())break;
        if(pass==23 || refined.size()+split.size()*3>MaximumPatches)return false;
        std::vector<WaterGeometryPatch> balanced;
        for(std::size_t i=0;i<refined.size();++i)
        {
            if(!split.count(i)){balanced.push_back(refined[i]);continue;}
            for(auto child:Children(refined[i])){influence(child);balanced.push_back(child);}
        }
        refined=std::move(balanced);
    }
    AdjacentPatches(refined,[&](const auto& a,const auto& b) {
        if(refined[a.patch].worldSize<refined[b.patch].worldSize*.75f)refined[a.patch].outerEdgeMask|=a.mask;
        if(refined[b.patch].worldSize<refined[a.patch].worldSize*.75f)refined[b.patch].outerEdgeMask|=b.mask;
    });
    m_Patches=std::move(refined);return true;
}
}
