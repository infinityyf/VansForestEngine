#ifndef GRASS_SCREEN_AO_GLSL
#define GRASS_SCREEN_AO_GLSL
// 正反半球继续使用现有 SSAO/滤波通道。只在 Grass 分支调用。
// 透视校正的屏幕线段深度区间与有限厚度相交，避免四个世界空间点跳过薄叶。
float GrassRayOcclusion(sampler2D positions,sampler2D materials,
    vec3 origin,vec3 N,vec3 direction)
{
    const float radius=1.0;
    vec3 start=(ViewMatrix*vec4(origin+N*0.005,1.0)).xyz;
    vec3 delta=mat3(ViewMatrix)*direction*radius;
    float nearDepth=max(NearPlane,0.001);
    if(-start.z<=nearDepth) return 0.0;
    float extent=delta.z>0.0?min(1.0,(-nearDepth-start.z)/delta.z):1.0;
    vec3 end=start+delta*extent;
    vec4 clipStart=ProjectionMatrix*vec4(start,1.0);
    vec4 clipEnd=ProjectionMatrix*vec4(end,1.0);
    vec2 uv=clipStart.xy/clipStart.w*vec2(0.5,-0.5)+0.5;
    vec2 uvDelta=(clipEnd.xy/clipEnd.w-clipStart.xy/clipStart.w)*vec2(0.5,-0.5);
    if(any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1)))) return 0.0;
    float screenExtent=1.0;
    for(int axis=0;axis<2;++axis)
    {
        if(uvDelta[axis]>1e-6) screenExtent=min(screenExtent,(1.0-uv[axis])/uvDelta[axis]);
        if(uvDelta[axis]<-1e-6) screenExtent=min(screenExtent,-uv[axis]/uvDelta[axis]);
    }
    float reciprocalStart=-1.0/start.z;
    float reciprocalEnd=mix(reciprocalStart,-1.0/end.z,screenExtent);
    uvDelta*=screenExtent;
    ivec2 size=textureSize(positions,0);
    vec2 pixelTravel=abs(uvDelta*vec2(size));
    int steps=clamp(int(ceil(max(pixelTravel.x,pixelTravel.y))),1,12);
    float blocked=0.0;
    for(int step=0;step<steps;++step)
    {
        float t0=float(step)/float(steps),t1=float(step+1)/float(steps);
        vec2 sampleUV=uv+uvDelta*(0.5*(t0+t1));
        ivec2 pixel=clamp(ivec2(sampleUV*vec2(size)),ivec2(0),size-1);
        vec4 surface=texelFetch(positions,pixel,0);
        if(surface.w<=0.0) continue;
        vec3 surfaceDelta=surface.xyz-origin;
        // 排除接收叶面及紧邻的自相交，不把它当成背面实体。
        if(dot(surfaceDelta,surfaceDelta)<0.0004 || abs(dot(surfaceDelta,N))<0.005) continue;
        float distance=length(surfaceDelta);
        if(distance>=radius) continue;
        float z0=1.0/mix(reciprocalStart,reciprocalEnd,t0);
        float z1=1.0/mix(reciprocalStart,reciprocalEnd,t1);
        int material=int(round(texelFetch(materials,pixel,0).z));
        float thickness=material==MATERIAL_ID_GRASS?0.04:0.25;
        float minimum=min(z0,z1),maximum=max(z0,z1);
        if(maximum>surface.w+0.003 && minimum<surface.w+thickness)
        {
            float separation=max(minimum-surface.w,0.0);
            float weight=(1.0-smoothstep(radius*0.5,radius,distance))*
                (1.0-smoothstep(thickness*0.5,thickness,separation));
            blocked=max(blocked,weight);
            if(blocked>=0.999) break;
        }
    }
    return blocked;
}
float GrassHemisphereAO(sampler2D positions,sampler2D materials,vec3 origin,vec3 N)
{
    vec3 T=normalize(cross(abs(N.y)<0.99?vec3(0,1,0):vec3(1,0,0),N));
    vec3 B=cross(N,T);
    float occlusion=0.0;
    for(int ray=0;ray<8;++ray)
    {
        float u=(float(ray)+0.5)/8.0;
        float angle=float(ray)*2.39996323;
        vec3 direction=(T*cos(angle)+B*sin(angle))*sqrt(u)+N*sqrt(1.0-u);
        occlusion+=GrassRayOcclusion(positions,materials,origin,N,direction);
    }
    return clamp(1.0-occlusion/8.0,0.0,1.0);
}
#endif
