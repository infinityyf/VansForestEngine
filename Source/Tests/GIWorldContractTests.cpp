#include "../EngineCore/RenderCore/GICore/VansGIWorldData.h"
#include "../EngineCore/RenderCore/GICore/VansGISettings.h"
#include "../EngineCore/TerrainCore/VansTerrainAsset.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <glm/gtc/packing.hpp>
#include <stdexcept>
#include <cstring>
#include <set>
#include <algorithm>

bool TestGIWorldContract()
{
    using namespace VansGraphics;
    try
    {
        auto check=[](bool valid,const char* message){if(!valid)throw std::runtime_error(message);};
        VansGISettings off, edited;edited.world.maxBricks=123;
        check(!off.world.enabled,"World GI must be opt-in");
        check(GISettingsResourceLayoutEquals(off,edited),"Disabled tuning must not trigger allocation");
        edited.world.enabled=true;
        check(!GISettingsResourceLayoutEquals(off,edited),"Enable must use the resource transaction");
        VansGISettings scoped;
        scoped.regions.push_back(scoped.regions.front());
        scoped.regions[1].stableId=2;scoped.regions[1].worldOnly=true;
        scoped.selectedRegionIndex=1;
        check(BuildActiveGIRegionOrder(scoped).size()==1 && BuildActiveGIRegionOrder(scoped)[0]->stableId==1,
            "Disabled world region entered allocation or reordered the ordinary region");
        scoped.world.enabled=true;
        auto active=BuildActiveGIRegionOrder(scoped);
        check(active.size()==2 && ResolveGIRegion(*active[0]).worldOnly && !ResolveGIRegion(*active[1]).worldOnly,
            "World participation did not survive region ordering and resolution");
        auto changed=scoped;changed.regions[0].worldOnly=true;
        check(!GISettingsResourceLayoutEquals(scoped,changed),"Changing region transport domain did not rebuild resources");
        VansGIWorldBuilder builder;builder.Reset(off.world,{},glm::vec3(0));
        check(builder.Pages().empty() && builder.PendingCount()==0,"Disabled field built pages");
        Vans::VansTerrainAsset terrain;terrain.width=terrain.height=2;terrain.heights={0,65535,0,65535};
        terrain.settings.terrainSize=2;terrain.settings.maxHeight=2;terrain.settings.heightOffset=-1;
        GIWorldHeightData height;std::string error;
        {
            // 独立全图参考：逐次刷绘后从完整源像素重新积分，与局部缓存/上传范围核对。
            size_t edits=0;
            for(const glm::uvec2 extent:{glm::uvec2(1,1),glm::uvec2(37,23),glm::uvec2(128,128),glm::uvec2(1024,513)})
            {
                std::array<std::vector<uint32_t>,2> source;
                for(uint32_t map=0;map<2;++map){source[map].resize(size_t(extent.x)*extent.y);
                    for(size_t i=0;i<source[map].size();++i)source[map][i]=uint32_t(i*2654435761u+map*1234567u);}
                const std::vector<glm::vec4> layers={{.1,.2,.3,1},{.7,.1,.2,1},{.2,.8,.4,1},{.9,.7,.1,1},
                    {.4,.4,.9,1},{.8,.3,.5,1},{.1,.9,.8,1},{.6,.5,.3,1}};
                auto rebuild=[&]()
                {
                    std::array<std::vector<uint32_t>,2> corners;
                    for(uint32_t map=0;map<2;++map)for(int y=0;y<128;++y)for(int x=0;x<128;++x)
                    {
                        const int bx=int(std::floor((x+.5)*extent.x/128.0-.5)),by=int(std::floor((y+.5)*extent.y/128.0-.5));
                        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                            corners[map].push_back(source[map][std::clamp(by+dy,0,int(extent.y)-1)*extent.x+std::clamp(bx+dx,0,int(extent.x)-1)]);
                    }
                    return corners;
                };
                GIWorldTerrainColorData incremental;
                check(incremental.Build(extent.x,extent.y,rebuild(),layers,error),"Terrain color initial capture rejected");
                check((incremental.corners[0].size()+incremental.corners[1].size())*sizeof(uint32_t)==524288,"Terrain footprint memory depends on source size");
                for(uint32_t edit=0;edit<48;++edit)
                {
                    const uint32_t map=edit%2,x=edit<2?(edit?extent.x-1:0):(edit*137)%extent.x,
                        y=edit<2?(edit?extent.y-1:0):(edit*53)%extent.y,
                        w=std::min(1+edit%17,extent.x-x),h=std::min(1+edit%13,extent.y-y);
                    const auto before=incremental.colors;std::vector<uint8_t> bytes(size_t(w)*h*4);
                    for(uint32_t row=0;row<h;++row)for(uint32_t column=0;column<w;++column)
                    {
                        const uint32_t value=edit%5?uint32_t(edit*31337+row*786433+column*2654435761u):0u;
                        source[map][(y+row)*extent.x+x+column]=value;
                        for(uint32_t channel=0;channel<4;++channel)bytes[(row*w+column)*4+channel]=uint8_t(value>>(channel*8));
                    }
                    GIWorldTerrainColorData::Patch changed;
                    check(incremental.ApplyPatch(map,x,y,w,h,bytes,changed,error),"Terrain color valid edit rejected");
                    check(incremental.corners==rebuild(),"Terrain footprint missed an edited source corner");
                    std::vector<bool> uploaded(128*128,false);
                    for(auto span:changed.spans)for(uint32_t i=0;i<span.y;++i)uploaded.at(span.x+i)=true;
                    for(int oy=0;oy<128;++oy)for(int ox=0;ox<128;++ox)
                    {
                        const double px=(ox+.5)*extent.x/128.0-.5,py=(oy+.5)*extent.y/128.0-.5;
                        const int bx=int(std::floor(px)),by=int(std::floor(py));const double fx=px-bx,fy=py-by;
                        glm::dvec4 color(0);double sum=0;
                        for(size_t layer=0;layer<8;++layer)
                        {
                            double weight=0;
                            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
                            {
                                const uint32_t packed=source[layer/4][std::clamp(by+dy,0,int(extent.y)-1)*extent.x+std::clamp(bx+dx,0,int(extent.x)-1)];
                                weight+=double((packed>>((layer%4)*8))&255u)/255.0*(dx?fx:1-fx)*(dy?fy:1-fy);
                            }
                            color+=glm::dvec4(layers[layer])*weight;sum+=weight;
                        }
                        color=sum>0?color/sum:glm::dvec4(layers[0]);const size_t address=oy*128+ox;
                        check(glm::all(glm::lessThan(glm::abs(glm::dvec4(incremental.colors[address])-color),glm::dvec4(2e-6))),"Local GI color differs from independent full-source bilinear integration");
                        if(before[address]!=incremental.colors[address])check(uploaded[address] && uint32_t(ox)>=changed.first.x && uint32_t(ox)<=changed.last.x &&
                            uint32_t(oy)>=changed.first.y && uint32_t(oy)<=changed.last.y,"Changed GI texel omitted from upload or lighting bounds");
                    }
                    check(incremental.ApplyPatch(map,x,y,w,h,bytes,changed,error) && changed.spans.empty(),"Repeated brush changed unchanged GI colors");
                    ++edits;
                }
                const auto before=incremental;
                GIWorldTerrainColorData::Patch changed;
                check(!incremental.ApplyPatch(2,0,0,1,1,{0,0,0,0},changed,error) &&
                    !incremental.ApplyPatch(0,extent.x,0,1,1,{0,0,0,0},changed,error) &&
                    !incremental.ApplyPatch(0,0,0,1,1,{0},changed,error) && incremental.corners==before.corners && incremental.colors==before.colors,
                    "Invalid terrain color edit mutated live cache");
            }
            std::cout<<"[GIWorld] terrain color local/full-source equivalence edits="<<edits<<" boundedFootprintBytes=524288 PASS\n";
        }

        check(height.Build(terrain,error),"Height hierarchy failed");
        check(std::abs(height.Sample({0,0}))<1e-5f,"Height center sampling differs from PCG");
        check(std::abs(height.Sample({-1,0})+1)<1e-5f,"Height clamped edge is incorrect");
        check(height.ranges.back().x<=-1 && height.ranges.back().y>=1,"Height parent is not conservative");
        {
            auto patchTerrain=terrain;patchTerrain.width=37;patchTerrain.height=23;
            patchTerrain.heights.resize(37*23);
            for(size_t i=0;i<patchTerrain.heights.size();++i)patchTerrain.heights[i]=uint16_t(i*1877);
            GIWorldHeightData incremental;check(incremental.Build(patchTerrain,error),"Patch fixture failed");
            for(uint32_t edit=0;edit<80;++edit)
            {
                const uint32_t x=(edit*13)%37,z=(edit*7)%23,w=std::min(1+edit%8,37-x),h=std::min(1+edit%5,23-z);
                std::vector<uint8_t> bytes(w*h*2);
                for(uint32_t row=0;row<h;++row)for(uint32_t col=0;col<w;++col)
                {
                    const uint16_t value=uint16_t(edit*3331+row*1559+col*431);
                    patchTerrain.heights[(z+row)*37+x+col]=value;
                    std::memcpy(bytes.data()+(row*w+col)*2,&value,2);
                }
                const auto old=incremental;
                GIWorldHeightData::Patch changed;
                check(incremental.ApplyPatch(x,z,w,h,bytes,changed,error),"Valid height patch rejected");
                GIWorldHeightData reference;check(reference.Build(patchTerrain,error),"Patch reference failed");
                check(incremental.heights==reference.heights && incremental.ranges==reference.ranges,"Incremental min/max differs from complete rebuild");
                std::vector<bool> uploaded(incremental.ranges.size());
                for(auto span:changed.ranges)for(uint32_t i=0;i<span.y;++i)uploaded.at(span.x+i)=true;
                for(size_t i=0;i<uploaded.size();++i)
                    check(uploaded[i] || incremental.ranges[i]==old.ranges[i],"Changed min/max missing from upload spans");
            }
            GIWorldHeightData::Patch rejected;const auto before=incremental;
            check(!incremental.ApplyPatch(UINT32_MAX,0,1,1,{0,0},rejected,error) && incremental.heights==before.heights && incremental.ranges==before.ranges,
                "Invalid height patch changed published CPU data");
            patchTerrain.width=patchTerrain.height=1024;patchTerrain.heights.assign(1024*1024,0);
            check(incremental.Build(patchTerrain,error),"Large terrain patch fixture failed");
            GIWorldHeightData::Patch small;check(incremental.ApplyPatch(500,500,16,16,std::vector<uint8_t>(512,255),small,error),"Small terrain edit failed");
            size_t entries=0;for(auto span:small.ranges)entries+=span.y;
            check(entries<1024,"Small edit rebuilt the whole height hierarchy");
            std::cout<<"GI height patch min/max entries: "<<entries<<" / "<<incremental.ranges.size()<<'\n';
        }
        GIWorldTriangle triangle;triangle.positions={glm::vec3(0,0,0),glm::vec3(1,0,0),glm::vec3(0,1,0)};
        triangle.uvs={glm::vec2(0,0),glm::vec2(1,0),glm::vec2(0,1)};
        triangle.alpha=[](glm::vec2){return 0.f;};
        GIWorldTemplate result;
        check(BakeGIWorldTemplate({triangle},.25f,1024,result,error)&&result.cells.empty(),"Transparent cards must not become walls");
        triangle.alpha=[](glm::vec2){return 1.f;};
        check(BakeGIWorldTemplate({triangle},.25f,1024,result,error)&&!result.cells.empty(),"Whole-model opaque surface was lost");
        for(const auto& c:result.cells)check((c.second.optical&0x10000u)!=0 && !(c.second.optical&0x20000u),"Surface shell was classified as interior");
        triangle.porous=true;
        check(BakeGIWorldTemplate({triangle},.25f,1024,result,error),"Porous voxel bake failed");
        for(const auto& c:result.cells)check((c.second.optical&0xffff0000u)==0,"Leaves must remain porous");
        check(!BakeGIWorldTemplate({triangle},.25f,1,result,error),"Template capacity overflow must be explicit");
        for(auto n:{glm::vec3(0,1,0),glm::normalize(glm::vec3(-1,-2,-3)),glm::vec3(0,0,-1)})
            check(glm::dot(n,GIWorldDecodeNormal(GIWorldEncodeNormal(n)))>.999f,"Packed voxel normal error");
        auto model=std::make_shared<GIWorldTemplate>();
        check(BakeGIWorldTemplate({triangle},.25f,1024,*model,error),"Template failed");
        {
            auto settings=edited.world;settings.maxBricks=1024;
            using Bricks=std::map<GIWorldCell,std::array<GIWorldVoxel,512>>;
            auto rebuild=[&](const std::vector<GIWorldInstance>& instances)
            {
                VansGIWorldBuilder current;current.Reset(settings,instances,glm::vec3(0));
                Bricks output;GIWorldBrick brick;
                while(current.BuildNext(brick))output.emplace(brick.key,brick.voxels);
                return output;
            };
            const auto near=MakeGIWorldInstance(model,glm::translate(glm::mat4(1),glm::vec3(-1,0,-1)));
            const auto far=MakeGIWorldInstance(model,glm::translate(glm::mat4(1),glm::vec3(10000,0,0)));
            std::vector<GIWorldInstance> previous={near,far};auto published=rebuild(previous);
            size_t reused=0,updated=0;
            for(uint32_t edit=0;edit<24;++edit)
            {
                std::vector<GIWorldInstance> next={far};
                if(edit%6!=0)
                {
                    auto transform=glm::translate(glm::mat4(1),glm::vec3(float(int(edit%7)-3),.2f,0));
                    transform=glm::rotate(transform,float(edit)*.31f,glm::normalize(glm::vec3(1,2,3)));
                    transform=glm::scale(transform,glm::vec3(.5f+edit%3,2.f,.7f));
                    next.push_back(MakeGIWorldInstance(model,transform));
                    if(edit%3==0)next.push_back(next.back());
                }
                if(edit==23)next.clear();
                const GIWorldDirtyRegions changed(GIWorldChangedInstanceRegions(previous,next));
                const auto reference=rebuild(next);
                Bricks incremental;
                for(const auto& page:reference)
                {
                    const auto old=published.find(page.first);
                    const bool dirty=old==published.end() || changed.IntersectsPage(page.first,settings.voxelSize);
                    if(dirty){incremental.emplace(page);++updated;}
                    else{incremental.emplace(*old);++reused;}
                }
                check(incremental.size()==reference.size(),"Source edit retained deleted pages");
                for(const auto& page:reference)check(std::memcmp(page.second.data(),incremental.at(page.first).data(),sizeof(page.second))==0,
                    "Source dirty bounds missed changed voxel contents");
                previous=std::move(next);published=std::move(incremental);
            }
            check(reused>0 && updated>0 && published.empty(),"Source edit did not exercise retained, dirty and deleted pages");
            check(GIWorldChangedInstanceRegions({near,far,near},{near,near,far}).empty(),"Reordering identical instances dirtied the field");
            check(!GIWorldChangedInstanceRegions({near,near},{near}).empty(),"Duplicate removal lost an optical density change");
            // 最大光学 mip 可伸出细模型边界；所有被覆盖页都必须进入失效范围。
            const GIWorldDirtyRegions changed(GIWorldChangedInstanceRegions({}, {near}));
            for(const auto& page:rebuild({near}))check(changed.IntersectsPage(page.first,settings.voxelSize),
                "Coarse mip extent was excluded from changed instance bounds");
            std::cout<<"GI instance edits: 24 full-rebuild oracles; reused="<<reused<<" rebuilt="<<updated<<'\n';
        }
        {
            // 两端编辑不应使中间的稳定页失效；保留页与独立全量构建逐体素对照。
            auto settings=edited.world;settings.maxBricks=1024;
            const auto instance=[&](float x){return MakeGIWorldInstance(model,glm::translate(glm::mat4(1),glm::vec3(x,0,0)));};
            const std::vector<GIWorldInstance> before{instance(-40),instance(0),instance(40)},after{instance(-42),instance(0),instance(42)};
            const GIWorldDirtyRegions dirty(GIWorldChangedInstanceRegions(before,after));
            check(!dirty.Intersects({glm::vec3(-2),glm::vec3(2)}),"Separated edits dirtied the unchanged middle");
            // 最大 32 m 光学 mip 的右边界在 x=-8；恰好 8 m 仍应触发光照刷新。
            check(!dirty.Intersects({glm::vec3(0),glm::vec3(0)},7),"Separated edits reset an unreachable middle probe");
            check(dirty.Intersects({glm::vec3(0),glm::vec3(0)},8),"Dirty-region distance omitted a reachable coarse mip");
            std::map<GIWorldCell,std::array<GIWorldVoxel,512>> old;
            VansGIWorldBuilder full;full.Reset(settings,before,glm::vec3(0));GIWorldBrick brick;
            while(full.BuildNext(brick))old.emplace(brick.key,brick.voxels);
            full.Reset(settings,after,glm::vec3(0));size_t middleRetained=0;
            while(full.BuildNext(brick))if(!dirty.IntersectsPage(brick.key,settings.voxelSize) && old.count(brick.key))
            {
                check(std::memcmp(brick.voxels.data(),old.at(brick.key).data(),sizeof(brick.voxels))==0,"Disjoint invalidation retained a changed page");
                ++middleRetained;
            }
            check(middleRetained>0,"Disjoint edits failed to preserve stable pages");
            // 分支跳转、负坐标、重叠、边界接触与欧氏距离都对照独立线性集合查询。
            std::vector<GIWorldBounds> boxes;
            for(int i=0;i<257;++i){const glm::vec3 lo(float(i%17)*11-90,float(i%7)*3-10,float(i/17)*13-80);boxes.push_back({lo,lo+glm::vec3(1,2,3)});}
            const GIWorldDirtyRegions index(boxes);
            size_t compared=0;
            for(int i=0;i<4096;++i)
            {
                const glm::vec3 lo(float(i%43)*5-103,float(i%19)*2-17,float(i%37)*6-109);
                const GIWorldBounds box{lo,lo+glm::vec3(i%3)};const float radius=float(i%11);
                bool expected=false;
                for(const auto& b:boxes)
                {
                    double squared=0;
                    for(int axis=0;axis<3;++axis){const double d=std::max({0.0,double(b.minimum[axis]-box.maximum[axis]),double(box.minimum[axis]-b.maximum[axis])});squared+=d*d;}
                    expected=expected || squared<=double(radius)*radius;
                }
                check(index.Intersects(box,radius)==expected,"Dirty-region tree differs from linear distance oracle");++compared;
            }
            check(index.Intersects({boxes.front().maximum,boxes.front().maximum}),"Dirty bounds excluded an exact boundary contact");
            check(!GIWorldDirtyRegions().Intersects({glm::vec3(0),glm::vec3(0)},100000),"Empty dirty regions invalidated probes");
            std::cout<<"GI disjoint edits: retained="<<middleRetained<<" spatial queries="<<compared<<'\n';
        }
        builder.Reset(edited.world,{MakeGIWorldInstance(model,glm::mat4(1))},glm::vec3(0));
        check(!builder.Pages().empty(),"PCG template did not create world pages");
        auto distant=MakeGIWorldInstance(model,glm::translate(glm::mat4(1),glm::vec3(700,0,0)));
        builder.Reset(edited.world,{distant},glm::vec3(0));
        check(!builder.Pages().empty(),"Global coarse field lost PCG beyond local camera coverage");
        for(const auto& page:builder.Pages())check(page.level==int(edited.world.levelCount)-1,
            "Distant PCG allocated a camera-local fine level");
        builder.Reset(edited.world,{distant},glm::vec3(700,0,0));
        check(std::any_of(builder.Pages().begin(),builder.Pages().end(),[](const auto& p){return p.level==0;}),"Camera-local fine field did not follow distant PCG");
        // Fine pages may be dropped only as a complete level; coarsest residency remains valid.
        auto wide=std::make_shared<GIWorldTemplate>(*model);wide->minimum=glm::vec3(-16);wide->maximum=glm::vec3(16);wide->coarseLevels.clear();
        auto budget=edited.world;budget.maxBricks=64;
        builder.Reset(budget,{MakeGIWorldInstance(wide,glm::mat4(1))},glm::vec3(0));
        check(builder.Overflowed()&&(builder.AvailableLevels()&(1u<<(budget.levelCount-1)))&&builder.Pages().size()<=64,"Budget fallback lost the complete coarse level");
        for(const auto& page:builder.Pages())check((builder.AvailableLevels()&(1u<<page.level))!=0,"Partially published level was retained");
        // 用独立的旧式全扫描检查空间索引：包含负坐标、旋转、缩放和 mip 扩张边界。
        auto referencePages=[](const GIWorldSettings& settings,const std::vector<GIWorldInstance>& instances,glm::vec3 center)
        {
            std::vector<GIWorldCell> pages;std::set<GIWorldCell> present;uint32_t available=0;
            std::vector<int> levels{int(settings.levelCount)-1};
            for(int level=0;level<int(settings.levelCount)-1;++level)levels.push_back(level);
            for(int level:levels)
            {
                const float size=settings.voxelSize*float(1u<<level)*8;
                const float radius=std::min(settings.coverageDistance,size*4);
                const size_t first=pages.size();bool full=false;
                for(const auto& instance:instances)
                {
                    const auto& mip=instance.model->SelectLevel(size/8,instance.transform);
                    glm::vec3 lo(std::numeric_limits<float>::max()),hi(-std::numeric_limits<float>::max());
                    for(uint32_t corner=0;corner<8;++corner)
                    {
                        glm::vec3 point;for(int axis=0;axis<3;++axis)point[axis]=(corner&(1u<<axis))?mip.maximum[axis]:mip.minimum[axis];
                        point=glm::vec3(instance.transform*glm::vec4(point,1));lo=glm::min(lo,point);hi=glm::max(hi,point);
                    }
                    if(level!=int(settings.levelCount)-1){lo=glm::max(lo,center-radius);hi=glm::min(hi,center+radius);}
                    if(glm::any(glm::greaterThan(lo,hi)))continue;
                    const glm::ivec3 a(glm::floor(lo/size)),b(glm::floor(hi/size));
                    for(int z=a.z;z<=b.z;++z)for(int y=a.y;y<=b.y;++y)for(int x=a.x;x<=b.x;++x)
                    {
                        const GIWorldCell key{x,y,z,level};if(present.count(key))continue;
                        if(present.size()>=settings.maxBricks){full=true;continue;}
                        present.insert(key);pages.push_back(key);
                    }
                }
                if(full){for(size_t i=first;i<pages.size();++i)present.erase(pages[i]);pages.resize(first);}
                else available|=1u<<level;
            }
            return std::make_pair(pages,available);
        };
        std::vector<GIWorldInstance> indexedInstances;
        for(int i=0;i<128;++i)
        {
            auto transform=glm::translate(glm::mat4(1),glm::vec3((i%16-8)*17.f,(i%3-1)*5.f,(i/16-4)*23.f));
            transform=glm::rotate(transform,float(i)*.37f,glm::normalize(glm::vec3(1,2,3)));
            transform=glm::scale(transform,glm::vec3(.5f+(i%4),1.f+(i%3),.75f+(i%5)));
            indexedInstances.push_back(MakeGIWorldInstance(model,transform));
        }
        for(uint32_t capacity:{64u,1024u,16384u})
        {
            auto settings=edited.world;settings.maxBricks=capacity;
            builder.Reset(settings,indexedInstances,glm::vec3(0));
            for(auto center:{glm::vec3(0),glm::vec3(-31.75f,2,7.75f),glm::vec3(80,-10,64),glm::vec3(700,0,0)})
            {
                VansGIWorldBuilder moved;moved.Recenter(builder,center);
                check(&moved.Instances()==&builder.Instances(),"Camera movement copied the source snapshot");
                auto resolved=settings;resolved.levelCount=moved.LevelCount();
                const auto reference=referencePages(resolved,indexedInstances,center);
                check(moved.Pages()==reference.first&&moved.AvailableLevels()==reference.second,"Indexed page planning differs from all-source reference");
            }
        }
        {
            auto settings=edited.world;settings.maxBricks=16384;
            builder.Reset(settings,indexedInstances,glm::vec3(700));
            VansGIWorldBuilder moved;moved.Recenter(builder,glm::vec3(0));
            std::map<GIWorldCell,std::array<GIWorldVoxel,512>> indexedBricks;
            GIWorldBrick brick;
            while(moved.BuildNext(brick))indexedBricks.emplace(brick.key,brick.voxels);
            size_t compared=0;
            for(uint32_t level=0;level<settings.levelCount;++level)
            {
                // 单层全局构建不走空间查询，用它核对索引路径实际生成的体素字节。
                auto single=settings;single.levelCount=1;single.voxelSize=settings.voxelSize*float(1u<<level);
                VansGIWorldBuilder reference;reference.Reset(single,indexedInstances,glm::vec3(0));
                check(!reference.Overflowed(),"Full-scan voxel reference exceeded its fixture budget");
                while(reference.BuildNext(brick))
                {
                    auto key=brick.key;key.level=int(level);const auto found=indexedBricks.find(key);if(found==indexedBricks.end())continue;
                    const float size=single.voxelSize*8,radius=std::min(settings.coverageDistance,size*4);
                    const glm::vec3 lo=glm::vec3(key.x,key.y,key.z)*size,hi=lo+size;
                    // 边界砖在原合同中只收集与邻域相交的实例；参考场则包括砖外侧来源。
                    if(level+1<settings.levelCount && (glm::any(glm::lessThan(lo,glm::vec3(-radius))) || glm::any(glm::greaterThan(hi,glm::vec3(radius)))))continue;
                    for(size_t cell=0;cell<512;++cell)
                        check(found->second[cell].optical==brick.voxels[cell].optical && found->second[cell].surface==brick.voxels[cell].surface,
                            "Indexed source query changed optical or material voxel bytes");
                    ++compared;
                }
            }
            check(compared>100,"Indexed payload comparison did not cover enough bricks");
            std::cout<<"GI indexed/full-scan voxel payloads identical: "<<compared<<" bricks\n";
        }
        {
            auto settings=edited.world;settings.maxBricks=16384;
            std::vector<GIWorldInstance> forest;
            for(uint32_t i=0;i<4096;++i)
                forest.push_back(MakeGIWorldInstance(model,glm::translate(glm::mat4(1),glm::vec3((i%64)*128.f+16.3f,16.7f,(i/64)*128.f+16.3f))));
            builder.Reset(settings,std::move(forest),glm::vec3(0));
            VansGIWorldBuilder moved;moved.Recenter(builder,glm::vec3(144,16,144));
            check((moved.AvailableLevels()&(1u<<(settings.levelCount-1)))!=0,"Large source index lost global coarse residency");
            check(moved.PlanningInstanceTests()<256,"Camera query still scans the distant forest");
            const auto reference=referencePages(settings,builder.Instances(),moved.Center());
            check(moved.Pages()==reference.first,"Large source index omitted global or local pages");
            const auto* shared=&builder.Instances();builder.Reset(off.world,{},glm::vec3(0));
            check(&moved.Instances()==shared&&moved.Instances().size()==4096,"Retiring a planner invalidated shared source data");
            GIWorldBrick brick;check(moved.BuildNext(brick),"Shared source snapshot did not survive old planner destruction");
            std::cout<<"GI spatial planning instance tests: "<<moved.PlanningInstanceTests()<<" / "<<4096*(settings.levelCount-1)<<"; sources shared across recenter\n";
            moved.Recenter(builder,glm::vec3(0));
            check(moved.Instances().empty()&&moved.Pages().empty()&&moved.PendingCount()==0,"Disabled recenter retained world sources");
        }
        {
            auto settings=edited.world;settings.maxBricks=64;
            std::vector<GIWorldInstance> forest;
            for(int z=0;z<16;++z)for(int x=0;x<16;++x)
                forest.push_back(MakeGIWorldInstance(model,glm::translate(glm::mat4(1),glm::vec3(x*64.f-480.f,16.7f,z*64.f-480.f))));
            builder.Reset(settings,forest,glm::vec3(0));
            check(builder.LevelCount()>settings.levelCount && builder.LevelCount()<=GIWorldMaxLevels,
                "Distant field did not coarsen when its reserved residency budget was exceeded");
            auto resolved=settings;resolved.levelCount=builder.LevelCount();
            auto reference=referencePages(resolved,forest,glm::vec3(0));
            check(builder.Pages()==reference.first && builder.AvailableLevels()==reference.second,
                "Adaptive far field lost sources compared with global reference");
            const auto root=int(builder.LevelCount())-1;size_t coarse=0;
            for(const auto& page:builder.Pages())if(page.level==root)++coarse;
            check(coarse && coarse<=settings.maxBricks/2 && builder.Pages().size()<=settings.maxBricks,
                "Far field consumed the camera residency reservation");
            check((builder.AvailableLevels()&(1u<<root))!=0,"Adaptive far field has no complete root");
            const auto rootPages=builder.Pages();const auto* shared=&builder.Instances();
            for(auto center:{glm::vec3(-480,16,-480),glm::vec3(480,16,480),glm::vec3(4096,64,-4096)})
            {
                VansGIWorldBuilder moved;moved.Recenter(builder,center);
                check(&moved.Instances()==shared && moved.LevelCount()==builder.LevelCount(),"Camera movement rebuilt or changed adaptive far field");
                for(const auto& page:rootPages)if(page.level==root)
                    check(std::find(moved.Pages().begin(),moved.Pages().end(),page)!=moved.Pages().end(),"Camera movement evicted a global coarse source page");
                const auto expected=referencePages(resolved,forest,center);
                check(moved.Pages()==expected.first,"Camera working set differs from adaptive reference");
            }
            std::cout<<"GI adaptive residency: 256 distributed sources, "<<settings.levelCount<<" -> "<<builder.LevelCount()
                <<" levels, "<<coarse<<" global coarse bricks / "<<settings.maxBricks<<" total capacity\n";
        }
        {
            // 单项小于 half 最小非零值的光学贡献仍须先汇总，不能丢掉整片远林。
            auto tiny=std::make_shared<GIWorldTemplate>();tiny->voxelSize=.25f;tiny->minimum=glm::vec3(0);tiny->maximum=glm::vec3(.25f);
            tiny->cells[{0,0,0,0}]={glm::packHalf1x16(1.f),0};tiny->spatialBricks[{0,0,0,0}].push_back({0,0,0,0});
            std::vector<GIWorldInstance> dense(4096,MakeGIWorldInstance(tiny,glm::translate(glm::mat4(1),glm::vec3(1))));
            auto settings=edited.world;settings.levelCount=11;settings.maxBricks=64;
            builder.Reset(settings,std::move(dense),glm::vec3(0));GIWorldBrick brick;
            check(builder.NextPage().level==10,"Weak optical fixture did not start at its far root");
            uint32_t slices=0;while(!builder.BuildNext(brick,17))check(++slices<10000,"Weak optical aggregation did not complete");
            double mass=0;for(const auto& voxel:brick.voxels)mass+=double(glm::unpackHalf1x16(uint16_t(voxel.optical)))*256*256*256;
            check(std::abs(mass-64)<.01,"Sub-half optical contributions vanished before global accumulation");
            std::cout<<"GI far optical accumulation: 4096 sub-half contributions, mass="<<mass<<" (expected 64)\n";
        }
        auto fine=edited.world;fine.levelCount=1;fine.voxelSize=.25f;fine.maxBricks=1024;fine.coverageDistance=16;
        auto opticalMass=[&](const glm::mat4& transform){builder.Reset(fine,{MakeGIWorldInstance(model,transform)},glm::vec3(0));GIWorldBrick brick;double sum=0;
            while(builder.BuildNext(brick))for(const auto& v:brick.voxels)sum+=glm::unpackHalf1x16(uint16_t(v.optical))*std::pow(fine.voxelSize,3);return sum;};
        double base=opticalMass(glm::mat4(1));double scaled=opticalMass(glm::scale(glm::mat4(1),glm::vec3(4)));
        fine.voxelSize=4.f;
        double coarse=opticalMass(glm::mat4(1));
        check(std::abs(coarse/base-1)<.02,"Coarse LOD changes integrated leaf extinction");
        fine.voxelSize=.25f;

        check(base>0&&std::abs(scaled/base-16)<.1,"Scaled vegetation has holes or does not preserve optical mass");
        GIWorldTriangle splitA=triangle,splitB=triangle;auto midpoint=(triangle.positions[1]+triangle.positions[2])*.5f;
        splitA.positions[2]=midpoint;splitB.positions[1]=midpoint;
        GIWorldTemplate tessellated;check(BakeGIWorldTemplate({splitA,splitB},.25f,1024,tessellated,error),"Split triangle bake failed");
        double originalMass=0,splitMass=0;for(const auto& v:model->cells)originalMass+=glm::unpackHalf1x16(uint16_t(v.second.optical));for(const auto& v:tessellated.cells)splitMass+=glm::unpackHalf1x16(uint16_t(v.second.optical));
        check(std::abs(originalMass-splitMass)<.01,"Leaf extinction changes with mesh tessellation");
        // 分段构建不能提前发布半砖，恢复后必须逐字节等价于不中断构建。
        auto collect=[&](uint32_t work)
        {
            builder.Reset(edited.world,{MakeGIWorldInstance(model,glm::mat4(1)),
                MakeGIWorldInstance(model,glm::translate(glm::mat4(1),glm::vec3(-.1f,.2f,.3f)))},glm::vec3(0));
            std::map<GIWorldCell,std::array<GIWorldVoxel,512>> bricks;
            uint32_t slices=0;bool paused=false;
            while(builder.PendingCount())
            {
                GIWorldBrick brick;const auto pending=builder.PendingCount();
                if(builder.BuildNext(brick,work))bricks.emplace(brick.key,brick.voxels);
                else
                {
                    paused=true;check(builder.PendingCount()==pending,"Partial brick was published before completion");
                    // 后台规划发布时移动整个 builder，部分砖的模型引用和迭代器必须仍有效。
                    auto transferred=std::move(builder);builder=std::move(transferred);
                }
                check(++slices<100000,"Budgeted brick build made no progress");
            }
            if(work==1)check(paused,"Budgeted test did not exercise interrupted work");
            return bricks;
        };
        const auto full=collect(std::numeric_limits<uint32_t>::max()),sliced=collect(1);
        check(full.size()==sliced.size(),"Resumed build changed page coverage");
        for(const auto& page:full)for(size_t i=0;i<512;++i)
        {const auto& b=sliced.at(page.first)[i];check(page.second[i].optical==b.optical && page.second[i].surface==b.surface,"Resumed build differs from uninterrupted build");}
        // 密集模板的粗场必须使用 mip；验证实际访问量和光学总量，而非仅存在 LOD 字段。
        GIWorldTriangle large=triangle;for(auto& p:large.positions)p*=16.f;
        auto detailed=std::make_shared<GIWorldTemplate>();check(BakeGIWorldTemplate({large},.25f,65536,*detailed,error),"Large mip template failed");
        check(!detailed->coarseLevels.empty() && detailed->CellCount()>detailed->cells.size(),"Template mip chain is missing");
        auto buildMass=[&](std::shared_ptr<const GIWorldTemplate> source)
        {
            auto coarseSettings=edited.world;coarseSettings.levelCount=1;coarseSettings.voxelSize=4;
            builder.Reset(coarseSettings,{MakeGIWorldInstance(source,glm::mat4(1))},glm::vec3(0));
            GIWorldBrick brick;double mass=0;
            while(builder.BuildNext(brick))for(const auto& v:brick.voxels)mass+=glm::unpackHalf1x16(uint16_t(v.optical))*64;
            return std::make_pair(mass,builder.TemplateSamples());
        };
        const auto optimized=buildMass(detailed);
        auto unmipped=std::make_shared<GIWorldTemplate>(*detailed);unmipped->coarseLevels.clear();const auto reference=buildMass(unmipped);
        check(optimized.second*8<reference.second,"Coarse world build still scans the fine template");
        check(std::abs(optimized.first/reference.first-1)<.02,"Template mip filtering loses optical mass");
        std::cout<<"GI mip samples: "<<reference.second<<" -> "<<optimized.second<<"; mass ratio="<<optimized.first/reference.first<<'\n';
        // 同格木质与叶片的材质不能取决于三角形提交顺序。
        GIWorldTriangle wood=triangle,leaf=triangle;wood.porous=false;wood.material=3;leaf.material=7;
        GIWorldTemplate firstOrder,secondOrder;
        check(BakeGIWorldTemplate({wood,leaf},.25f,1024,firstOrder,error) && BakeGIWorldTemplate({leaf,wood},.25f,1024,secondOrder,error),"Mixed optical template failed");
        for(const auto& cell:firstOrder.cells)
        {check((cell.second.surface&65535u)==3 && cell.second.surface==secondOrder.cells.at(cell.first).surface,"Leaves overwrote an opaque wood hit material");}
        builder.Reset(edited.world,{},glm::vec3(0));
        check(builder.Pages().empty(),"Deleted plant left stale pages");
        std::cout<<"GI world contracts passed\n";return true;
    }
    catch(const std::exception& error){std::cerr<<"GI world contract: "<<error.what()<<'\n';return false;}
}
