#ifndef GI_RECEIVER_TRANSPORT_BOUNDARY_GLSL
#define GI_RECEIVER_TRANSPORT_BOUNDARY_GLSL
float GI_ReceiverTransportBoundaryWeight(ivec2 cacheCoord,uint region,vec4 position,
    vec3 normal,float material,float footprint)
{
    const int radius=4;
    float distanceScale=max(footprint*float(radius*4),1e-5);
    float weight=0.0;
    ivec2 size=ivec2(receiverHeader.yz);
    for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x)
    {
        ivec2 tap=cacheCoord+ivec2(x,y);
        if(any(lessThan(tap,ivec2(0))) || any(greaterThanEqual(tap,size))) continue;
        uint index=uint(tap.y*size.x+tap.x);
        uvec4 metadata=receiverRecords[index].metadata;
        if(metadata.x!=region || metadata.z==0u || metadata.w!=0u) continue;
        GIReceiverVisibilityRecord candidate=receiverRecords[index];
        uint mask=0u;
        for(uint i=0u;i<16u;++i) if(GI_ReceiverProbe(candidate,i)!=0xffffffffu) mask|=1u<<i;
        if(candidate.metadata.z!=mask) continue; // 未知候选不能充当全遮挡边界。
        vec3 delta=candidate.surface.xyz-position.xyz;
        float distance=length(delta)/distanceScale;
        if(distance>=1.0 || !SSGI_HistorySurfaceMatches(candidate.surface,position,
            normal,normal,material,distanceScale)) continue;
        // 紧邻边界的一个缓存单元使用相同估计，其余三个单元连续退回 DDGI。
        weight=max(weight,1.0-smoothstep(0.25,1.0,distance));
    }
    return weight;
}
#endif
