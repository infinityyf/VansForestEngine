#ifndef GI_RECEIVER_BIAS_COMMON_GLSL
#define GI_RECEIVER_BIAS_COMMON_GLSL
#include "../GI/GIReceiverOrigin.glsl"
// 每帧当前表面的几何测量，不复用视图或世界遮挡历史。
struct GIReceiverBiasRecord
{
    vec4 surface;
    vec4 limits; // x DDGI 偏移，y 屏幕起点偏移，z 对齐填充，w region + 1。
};
// 在 1.25 倍偏移处开始压缩，进入/离开限制区时连续；不改变没有障碍的偏移。
float GI_ClampReceiverBias(float requested, float distanceToObstacle, float epsilon)
{
    if(distanceToObstacle<0.0) return requested;
    return min(requested,max(epsilon,0.8*distanceToObstacle));
}

// 消费端定义绑定才声明当前帧缓冲；RT 测距端只复用上面的规则。
#ifdef GI_RECEIVER_BIAS_BINDING
#define GI_RECEIVER_BIAS_READ_GLSL
#include "../GI/GIReceiverVisibilityData.glsl"
layout(set=1,binding=GI_RECEIVER_BIAS_BINDING,std430) readonly buffer ReceiverBiasInput
{
    uvec4 receiverBiasHeader;
    GIReceiverBiasRecord receiverBiasRecords[];
};
vec4 receiverBiasLimits=vec4(0.0);
void GI_SetReceiverBias(ivec2 pixel,vec4 position,vec3 normal,float material,float footprint)
{
    receiverBiasLimits=vec4(0.0);
    if(receiverBiasHeader.x==0u) return;
    ivec2 size=ivec2(receiverBiasHeader.yz);
    ivec2 base=ivec2(floor((vec2(pixel)-2.0)*0.25));
    float best=3.402823e38;
    for(int y=0;y<2;++y) for(int x=0;x<2;++x)
    {
        ivec2 tap=base+ivec2(x,y);
        if(any(lessThan(tap,ivec2(0))) || any(greaterThanEqual(tap,size))) continue;
        GIReceiverBiasRecord value=receiverBiasRecords[tap.y*size.x+tap.x];
        if(value.limits.w<=0.0 || !GI_ReceiverSurfaceMatches(value.surface,position,normal,material,footprint*2.0)) continue;
        float d=dot(value.surface.xyz-position.xyz,value.surface.xyz-position.xyz);
        if(d>=best) continue;
        best=d;receiverBiasLimits=value.limits;
    }
}
float GI_ReceiverNormalBias(uint region,float requested)
{
    return receiverBiasLimits.w==float(region+1u)?min(max(requested,0.0),receiverBiasLimits.x):requested;
}
float GI_ReceiverScreenBias()
{
    return receiverBiasLimits.w>0.0?receiverBiasLimits.y:0.05;
}
#define GI_RECEIVER_NORMAL_BIAS(region,bias) GI_ReceiverNormalBias(region,bias)
#endif
#endif
