#ifndef GI_RECEIVER_TRANSPORT_CACHE_GLSL
#define GI_RECEIVER_TRANSPORT_CACHE_GLSL
// 局部传输的集中入口：同表面边界过渡、历史复用和入队。
// 零支持边缘两侧使用同一种照明估计，再在有限的邻域内接回 DDGI。
#include "GIReceiverTransportBoundary.glsl"
float GI_ReceiverTransportWeight(ivec2 cacheCoord,uint region,float support,vec4 position,
    vec3 normal,float material,float footprint,bool vegetation)
{
    if(transportHeader.w==0u || region==0xffffffffu || vegetation ||
        !receiverRecordValid || receiverRecord.metadata.x!=region || receiverRecord.metadata.z==0u) return 0.0;
    if(support>0.0 && (receiverRecord.metadata.z&~receiverRecord.metadata.w)==0u) return 0.0;
    SSGIRegionParams r=regions[region];
    GIProbeCandidates c=GI_GatherProbeCandidates(region,ivec3(r.gridDimensionsAndPriority.xyz),
        position.xyz,normal,r.volumeMin.xyz,r.volumeSizeAndBias.xyz,r.volumeSizeAndBias.w);
    bool hasPublished=false,hasBlocked=false;
    for(uint i=0u;i<8u;++i)
    {
        uint p=c.probes[i];
        if(p==0xffffffffu || c.weights[i]<=0.0) continue;
        GIProbeState state=LoadScreenCacheProbeState(region,p);
        if(state.metadata.x!=1u) continue;
        hasPublished=true; bool known=false;
        for(uint j=0u;j<16u;++j)
            if((receiverRecord.metadata.z&(1u<<j))!=0u && GI_ReceiverProbe(receiverRecord,j)==p)
            { known=true;hasBlocked=hasBlocked || (receiverRecord.metadata.w&(1u<<j))==0u; }
        if(!known) return 0.0;
    }
    if(!hasPublished) return 0.0;
    if(support<=0.0) return receiverRecord.metadata.w==0u?1.0:0.0;
    if(!hasBlocked) return 0.0; // 完整可见区域保持既有路径。
    float transition=0.0;
    transition=GI_ReceiverTransportBoundaryWeight(cacheCoord,region,position,normal,material,footprint);
    return transition; // 注释上一行可独立关闭边界过渡。
}
bool GI_ReceiverTransportHistory(ivec2 pixel, vec4 position, vec3 normal, float material, float footprint,out vec3 irradiance)
{
    if(historyFrameParams.x<0.5 || (LastVPMatrix*vec4(position.xyz,1.0)).w<=0.0) return false;
    vec2 uv=(vec2(pixel)+0.5)/screenSize.xy-texelFetch(inputMotionVector,pixel,0).xy+historyFrameParams.yz;
    if(any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1)))) return false;
    ivec2 previous=ivec2(floor(uv*screenSize.xy));
    if(!SSGI_HistorySurfaceMatches(texelFetch(inputHistorySurface,previous,0),
        position,normal,normal,material,footprint)) return false;
    vec4 value=texelFetch(inputGIHistory,previous,0);
    // 仅复用同一种真实传输来源；正常 DDGI 历史不成为无支持区域的替代亮度。
    if(value.a>=0.0) return false;
    irradiance=max(value.rgb,vec3(0.0));
    return true;
}
bool GI_QueueReceiverTransport(GIReceiverTransportJob job, uint pixelIndex)
{
    atomicAdd(transportHeader.x,1u);
    // 过载时按上一帧需求均匀抽样；逐帧变换哈希，避免固定屏幕区域长期抢占队列。
    uint demand=max(transportHeader.z,GI_RECEIVER_TRANSPORT_JOBS);
    float probability=min(1.0,float(GI_RECEIVER_TRANSPORT_JOBS)*0.875/float(demand));
    if(demand==GI_RECEIVER_TRANSPORT_JOBS) probability=1.0;
    uint seed=GI_ReceiverTransportHash(pixelIndex ^ (uint(FrameIndex)*0x9e3779b9u));
    if(float(seed)*(1.0/4294967296.0)>=probability) return false;
    uint slot=atomicAdd(transportHeader.y,1u);
    if(slot>=GI_RECEIVER_TRANSPORT_JOBS) return false;
    transportJobs[slot]=job;
    return true;
}

#endif
