#ifndef GI_WORLD_QUERY_GLSL
#define GI_WORLD_QUERY_GLSL
#include "../GI/GIProbeFeedbackStatus.glsl"
#ifndef GI_WORLD_SET
#define GI_WORLD_SET 2
#endif
layout(set=GI_WORLD_SET,binding=0,std140) uniform GIWorldParameters
{
    vec4 gwTerrain; // size, height scale, offset, unused
    uvec4 gwHeight; // width, height, level count, enabled
    uvec4 gwTable; // hash capacity, pages, step budget, hardware trace present
    vec4 gwCenter; // xyz anchor, extinction multiplier
    vec4 gwGrid; // base voxel, coverage radius, levels, available level mask
    vec4 gwVoxelMin; // 所有已加载体素来源 的最粗层边界，仅用于裁剪查询
    vec4 gwVoxelMax;
    uvec4 gwHeightLevels[16];
};
layout(set=GI_WORLD_SET,binding=1,std430) readonly buffer GIWorldHeights { float gwHeights[]; };
layout(set=GI_WORLD_SET,binding=2,std430) readonly buffer GIWorldRanges { vec2 gwRanges[]; };
struct GIWorldPage { ivec4 key; uvec4 data; };
layout(set=GI_WORLD_SET,binding=3,std430) readonly buffer GIWorldPages { GIWorldPage gwPages[]; };
layout(set=GI_WORLD_SET,binding=4,std430) readonly buffer GIWorldVoxels { uvec2 gwVoxels[]; };
layout(set=GI_WORLD_SET,binding=5,std430) readonly buffer GIWorldMaterials { vec4 gwMaterials[]; };
layout(set=GI_WORLD_SET,binding=6,std430) readonly buffer GIWorldTerrainColors { vec4 gwTerrainColors[]; };

struct GIWorldHit { float t; vec3 normal; vec4 material; float transmission; bool valid; bool backface; uint failure; vec4 scattering; };
GIWorldHit GIWorldMiss(float t)
{ GIWorldHit h;h.t=t;h.normal=vec3(0,1,0);h.material=vec4(0.5,0.5,0.5,1);h.transmission=1;h.valid=true;h.backface=false;h.failure=0u;h.scattering=vec4(0,0,0,-1);return h; }
// 远级数百米射线的 float 间距大于固定 epsilon。必须实际跨过一个可表示的 t，
// 否则负方向仍落在已离开的高度单元，会漏掉地表或反复消耗追踪步数。
float GIWorldAdvance(float t)
{
    float next=uintBitsToFloat(floatBitsToUint(t)+(t>=0.0?1u:0xffffffffu));
    return max(t+1e-5,next);
}
bool GIWorldSlab(vec3 o,vec3 d,vec3 lo,vec3 hi,inout float begin,inout float end)
{
    for(int a=0;a<3;++a)
    { if(abs(d[a])<1e-12){if(o[a]<lo[a]||o[a]>hi[a])return false;}
      else{float x=(lo[a]-o[a])/d[a],y=(hi[a]-o[a])/d[a];begin=max(begin,min(x,y));end=min(end,max(x,y));if(begin>end)return false;} }
    return true;
}
float GIWorldHeightAt(vec2 world)
{
    vec2 p=clamp((world/gwTerrain.x+0.5)*vec2(gwHeight.xy)-0.5,vec2(0),vec2(gwHeight.xy)-1.0);
    uvec2 a=uvec2(p),b=min(a+1u,gwHeight.xy-1u);vec2 f=fract(p);
    return mix(mix(gwHeights[a.y*gwHeight.x+a.x],gwHeights[a.y*gwHeight.x+b.x],f.x),
        mix(gwHeights[b.y*gwHeight.x+a.x],gwHeights[b.y*gwHeight.x+b.x],f.x),f.y);
}
vec3 GIWorldHeightNormal(vec2 world)
{
    vec2 p=clamp((world/gwTerrain.x+0.5)*vec2(gwHeight.xy)-0.5,vec2(0),vec2(gwHeight.xy)-1.0);
    uvec2 a=uvec2(p),b=min(a+1u,gwHeight.xy-1u);vec2 f=fract(p);
    float h00=gwHeights[a.y*gwHeight.x+a.x],h10=gwHeights[a.y*gwHeight.x+b.x];
    float h01=gwHeights[b.y*gwHeight.x+a.x],h11=gwHeights[b.y*gwHeight.x+b.x];
    vec2 slope=vec2(mix(h10-h00,h11-h01,f.y),mix(h01-h00,h11-h10,f.x))*vec2(gwHeight.xy)/gwTerrain.x;
    return normalize(vec3(-slope.x,1,-slope.y));
}
// 地形光栅网格的三角插值/LOD 与双线性高度场存在毫米到厘米差异。
// 只校正已确认的地形接收面，不移动墙壁、草、树或探针，也不改变硬件射线。
vec3 GIWorldReceiverPosition(vec3 position,bool terrainReceiver,float epsilon)
{
    if(terrainReceiver && gwHeight.w!=0u && all(lessThanEqual(abs(position.xz),vec2(gwTerrain.x*0.5))))
        position.y=max(position.y,GIWorldHeightAt(position.xz)+epsilon);
    return position;
}
float GIWorldExit2D(vec3 o,vec3 d,vec2 lo,vec2 hi,float t,float limit)
{
    for(int a=0;a<2;++a){int axis=a*2;if(abs(d[axis])<1e-12)continue;
        float exitT=((d[axis]>0?hi[a]:lo[a])-o[axis])/d[axis];if(exitT>t+1e-6)limit=min(limit,exitT);}
    return limit;
}
GIWorldHit GIWorldTraceHeight(vec3 o,vec3 d,float minimum,float maximum)
{
    GIWorldHit hit=GIWorldMiss(maximum);
    if(gwHeight.w==0u)return hit;
    vec2 range=gwRanges[gwHeightLevels[gwHeight.z-1u].x];
    float t=minimum,end=maximum,halfSize=gwTerrain.x*0.5;
    if(!GIWorldSlab(o,d,vec3(-halfSize,range.x,-halfSize),vec3(halfSize,range.y,halfSize),t,end))return hit;
    uint iterations=0u;
    while(t<end && iterations++<gwTable.z)
    {
        bool advanced=false;
        for(int level=int(gwHeight.z)-1;level>=0;--level)
        {
            float scale=float(1u<<uint(level));
            vec2 uv=(o.xz+d.xz*GIWorldAdvance(t))/gwTerrain.x+0.5;
            uvec4 info=gwHeightLevels[level];
            uvec2 cell=uvec2(clamp(floor((uv*vec2(gwHeight.xy)+0.5)/scale),vec2(0),vec2(info.yz)-1));
            vec2 lo=max((vec2(cell)*scale-0.5)/vec2(gwHeight.xy)-0.5,vec2(-0.5))*gwTerrain.x;
            vec2 hi=min((vec2(cell+1u)*scale-0.5)/vec2(gwHeight.xy)-0.5,vec2(0.5))*gwTerrain.x;
            float nextT=GIWorldExit2D(o,d,lo,hi,t,end);
            vec2 y=o.y+d.y*vec2(t,nextT),bounds=gwRanges[info.x+cell.y*info.y+cell.x];
            if(min(y.x,y.y)>bounds.y || max(y.x,y.y)<bounds.x)
            {t=GIWorldAdvance(nextT);advanced=true;break;}
            if(level==0)
            {
                float dt=nextT-t;
                float f0=o.y+d.y*t-GIWorldHeightAt(o.xz+d.xz*t);
                float fm=o.y+d.y*(t+dt*0.5)-GIWorldHeightAt(o.xz+d.xz*(t+dt*0.5));
                float f1=o.y+d.y*nextT-GIWorldHeightAt(o.xz+d.xz*nextT);
                float a=2.0*(f0+f1-2.0*fm),b=f1-f0-a;vec2 roots=vec2(2);
                // 近线性的双线性段会因三次高度采样的舍入产生微小二次项。
                // 直接相减求根会把有效交点压成 0，产生错误地表命中和 GI 黑斑。
                float linearTolerance=1e-6*max(1.0,max(abs(f0),max(abs(fm),abs(f1))));
                if(abs(a)<=linearTolerance){if(abs(b)>1e-10)roots.x=-f0/b;else if(abs(f0)<1e-5)roots.x=0;}
                else
                {
                    float disc=b*b-4*a*f0;
                    if(disc>=0)
                    {
                        float q=-0.5*(b+(b>=0?sqrt(disc):-sqrt(disc)));
                        roots=abs(q)>1e-20?vec2(q/a,f0/q):vec2(-b/(2*a));
                        roots=vec2(min(roots.x,roots.y),max(roots.x,roots.y));
                    }
                }
                for(int r=0;r<2;++r)if(roots[r]>=-1e-5 && roots[r]<=1.00001)
                {
                    hit.t=t+clamp(roots[r],0,1)*dt;vec2 p=o.xz+d.xz*hit.t;hit.normal=GIWorldHeightNormal(p);
                    hit.backface=dot(hit.normal,d)>0;
                    uvec2 colorCell=uvec2(clamp((p/gwTerrain.x+0.5)*gwTerrain.w,vec2(0),vec2(gwTerrain.w)-1));
                    hit.material=gwTerrainColors[colorCell.y*uint(gwTerrain.w)+colorCell.x];return hit;
                }
                t=GIWorldAdvance(nextT);advanced=true;break;
            }
        }
        if(!advanced)break;
    }
    hit.valid=t>=end;hit.failure=hit.valid?0u:GI_FEEDBACK_HEIGHT_BUDGET;return hit;
}
uint GIWorldHash(ivec4 c)
{uint h=uint(c.x)*73856093u^uint(c.y)*19349663u^uint(c.z)*83492791u^uint(c.w)*2654435761u;h^=h>>16u;h*=2246822519u;return h^(h>>13u);}
// -1 已知空，-2 未发布，非负为砖块的体素地址。
int GIWorldFindPage(ivec4 key)
{
    uint slot=GIWorldHash(key)&(gwTable.x-1u);
    for(uint i=0u;i<gwTable.x;++i)
    {GIWorldPage p=gwPages[slot];if(p.data.x==0xffffffffu)return -1;
     if(all(equal(p.key,key)))return p.data.y==0u?-2:int(p.data.x);slot=(slot+1u)&(gwTable.x-1u);}
    return -2;
}
vec3 GIWorldNormal(uint packed)
{
    vec2 p=vec2(packed&255u,(packed>>8u)&255u)/255.0*2.0-1.0;vec3 n=vec3(p,1-abs(p.x)-abs(p.y));
    if(n.z<0)n.xy=(1-abs(p.yx))*vec2(p.x<0?-1:1,p.y<0?-1:1);return normalize(n);
}
// 按射线与同心覆盖盒的交点确定本段 LOD。空砖/粗格都不能跳过选级边界。
// 只在进入下一段时重新求交，避免每次体素迭代计算 log2 或遍历全部层。
uint GIWorldRayLevel(vec3 o,vec3 d,float t,float end,uint minimumLevel,out float until)
{
    uint root=uint(gwGrid.z)-1u;until=end;
    float probe=GIWorldAdvance(t),previousRadius=-1.0;
    for(uint level=minimumLevel;level<root;++level)
    {
        if((uint(gwGrid.w)&(1u<<level))==0u)continue;
        float radius=min(gwGrid.y,gwGrid.x*32.0*float(1u<<level));
        if(radius==previousRadius)continue;
        previousRadius=radius;
        float enter=probe,leave=end;
        if(!GIWorldSlab(o,d,gwCenter.xyz-vec3(radius),gwCenter.xyz+vec3(radius),enter,leave) || leave<=probe)continue;
        if(enter<=probe){until=min(until,leave);return level;}
        // 尚未进入更细盒，但当前较粗段必须在入口结束。
        until=min(until,enter);
    }
    return root;
}
GIWorldHit GIWorldTraceVoxels(vec3 o,vec3 d,float minimum,float maximum,bool transmission,float minimumVoxelSize,float scatterOpticalDepth,uint stepBudget)
{
    GIWorldHit hit=GIWorldMiss(maximum);
    if((uint(gwGrid.w)&(1u<<(uint(gwGrid.z)-1u)))==0u){hit.valid=false;hit.failure=GI_FEEDBACK_VOXEL_COVERAGE;return hit;}
    if(gwTable.y==0u)return hit;
    float t=minimum,end=maximum;
    if(!GIWorldSlab(o,d,gwVoxelMin.xyz,gwVoxelMax.xyz,t,end))return hit;
    // 远级探针只重建低频光照；其射线经过相机附近时也不必重新追踪最细格。
    // 尺度向下取整到已有 mip，原距离策略仍可选更粗级；接收可见性传 0 保留原精度。
    uint minimumLevel=uint(clamp(floor(log2(max(minimumVoxelSize/gwGrid.x,1.0))),0.0,gwGrid.z-1.0));
    uint iteration=0u,rayLevel=minimumLevel;float levelEnd=t;
    while(t<end && iteration++<stepBudget)
    {
        if(t>=levelEnd)rayLevel=GIWorldRayLevel(o,d,t,end,minimumLevel,levelEnd);
        vec3 position=o+d*GIWorldAdvance(t);uint level=rayLevel;
        float size=0;ivec3 cell,brick;int page=-2;
        for(;level<uint(gwGrid.z);++level)
        {
            if((uint(gwGrid.w)&(1u<<level))==0u)continue;
            size=gwGrid.x*float(1u<<level);
            vec3 gridPosition=position/size;cell=ivec3(floor(gridPosition));
            // 负方向从边界进入前一格。近水平射线的微小 t 偏移可能被世界坐标舍入吞掉，
            // 不能靠反复累加 epsilon 跨过边界，否则会耗尽预算并使探针永久等不到发布。
            for(int axis=0;axis<3;++axis)
                if(d[axis]<0.0 && gridPosition[axis]==float(cell[axis]))--cell[axis];
            brick=ivec3(floor(vec3(cell)/8.0));
            page=GIWorldFindPage(ivec4(brick,int(level)));
            if(page!=-2)break;
        }
        if(page==-2){hit.valid=false;hit.failure=GI_FEEDBACK_VOXEL_PAGE;return hit;}
        vec3 lo,hi;uvec2 voxel=uvec2(0);
        if(page<0){lo=vec3(brick)*(size*8);hi=lo+size*8;}
        else{ivec3 c=cell-brick*8;voxel=gwVoxels[uint(page)+uint(c.x+8*(c.y+8*c.z))];lo=vec3(cell)*size;hi=lo+size;}
        float nextT=levelEnd;for(int a=0;a<3;++a)if(abs(d[a])>1e-12)nextT=min(nextT,((d[a]>0?hi[a]:lo[a])-o[a])/d[a]);
        nextT=max(nextT,t);
        if((voxel.x&0x10000u)!=0u)
        {hit.t=t;hit.normal=GIWorldNormal(voxel.y>>16u);hit.backface=dot(hit.normal,d)>0;hit.material=gwMaterials[voxel.y&65535u];return hit;}
        if(transmission)
        {
            float sigma=max(unpackHalf2x16(voxel.x).x,0.0)*gwCenter.w;
            float opticalDepth=sigma*(nextT-t);
            // 自由程采样的概率已包含出射路径 T*sigma；照明阶段不再乘一次全程 T。
            // 只记录一个事件，不改变最近不透明命中和几何固定射线。
            if(scatterOpticalDepth>=0.0 && sigma>0.0)
            {
                if(scatterOpticalDepth<opticalDepth)
                {
                    hit.scattering=vec4(clamp(gwMaterials[voxel.y&65535u].rgb,0.0,1.0),t+scatterOpticalDepth/sigma);
                    scatterOpticalDepth=-1.0;
                }
                else scatterOpticalDepth-=opticalDepth;
            }
            hit.transmission*=exp(-opticalDepth);
        }
        t=GIWorldAdvance(nextT);
    }
    hit.valid=t>=end;hit.failure=hit.valid?0u:GI_FEEDBACK_VOXEL_BUDGET;return hit;
}
GIWorldHit GIWorldTraceVoxels(vec3 o,vec3 d,float minimum,float maximum,bool transmission,float minimumVoxelSize,float scatterOpticalDepth)
{ return GIWorldTraceVoxels(o,d,minimum,maximum,transmission,minimumVoxelSize,scatterOpticalDepth,gwTable.z); }
GIWorldHit GIWorldTraceVoxels(vec3 o,vec3 d,float minimum,float maximum,bool transmission,float minimumVoxelSize)
{ return GIWorldTraceVoxels(o,d,minimum,maximum,transmission,minimumVoxelSize,-1.0); }
GIWorldHit GIWorldTraceVoxels(vec3 o,vec3 d,float minimum,float maximum,bool transmission)
{ return GIWorldTraceVoxels(o,d,minimum,maximum,transmission,0.0); }
GIWorldHit GIWorldTrace(vec3 o,vec3 d,float minimum,float maximum,bool transmission,float minimumVoxelSize,float scatterOpticalDepth,uint voxelSteps)
{
    GIWorldHit terrain=GIWorldTraceHeight(o,d,minimum,maximum);
    GIWorldHit plants=GIWorldTraceVoxels(o,d,minimum,terrain.t,transmission,minimumVoxelSize,scatterOpticalDepth,voxelSteps);
    GIWorldHit hit=plants.t<terrain.t?plants:terrain;
    hit.scattering=plants.scattering;hit.transmission=plants.transmission;hit.valid=terrain.valid&&plants.valid;hit.failure=terrain.failure|plants.failure;return hit;
}
GIWorldHit GIWorldTrace(vec3 o,vec3 d,float minimum,float maximum,bool transmission,float minimumVoxelSize,float scatterOpticalDepth)
{ return GIWorldTrace(o,d,minimum,maximum,transmission,minimumVoxelSize,scatterOpticalDepth,gwTable.z); }
GIWorldHit GIWorldTrace(vec3 o,vec3 d,float minimum,float maximum,bool transmission,float minimumVoxelSize)
{ return GIWorldTrace(o,d,minimum,maximum,transmission,minimumVoxelSize,-1.0); }
GIWorldHit GIWorldTrace(vec3 o,vec3 d,float minimum,float maximum,bool transmission)
{ return GIWorldTrace(o,d,minimum,maximum,transmission,0.0); }
#endif
