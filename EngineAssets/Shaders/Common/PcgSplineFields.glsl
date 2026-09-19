#ifndef PCG_SPLINE_FIELDS_GLSL
#define PCG_SPLINE_FIELDS_GLSL
layout(std430,set=2,binding=0) readonly buffer PcgSplineMetadata { uvec4 pcgMetadata[]; };
layout(set=2,binding=1) uniform sampler2D pcgHeightAtlas;
layout(set=2,binding=2) uniform sampler2D pcgVelocityAtlas;
layout(set=2,binding=3) uniform sampler2D pcgCoverageAtlas;
layout(set=2,binding=4) uniform sampler2D pcgRiverPropertiesAtlas;
layout(set=2,binding=6) uniform sampler2D pcgWaterBlendAtlas;

bool PcgLocate(vec2 worldXZ,out uvec4 page,out vec2 local)
{
    page=uvec4(0);local=vec2(0);
    uint resolution=pcgMetadata[0].y;
    if(resolution==0u)return false;
    float worldSize=uintBitsToFloat(pcgMetadata[0].x);
    vec2 pixel=(worldXZ/worldSize+0.5)*float(resolution);
    if(any(lessThan(pixel,vec2(0))) || any(greaterThanEqual(pixel,vec2(resolution))))return false;
    uvec2 tile=uvec2(floor(pixel/float(pcgMetadata[1].x)));
    page=pcgMetadata[2u+tile.y*pcgMetadata[0].z+tile.x];
    local=pixel-vec2(tile)*float(pcgMetadata[1].x)+float(pcgMetadata[1].z);
    return page.x!=0u;
}
vec2 PcgAtlasPixel(uint page,vec2 local)
{
    uint columns=pcgMetadata[0].w;float extent=float(pcgMetadata[1].y);
    // 采样限制在本页 guard，避免相邻无关页参与过滤。
    return vec2(page%columns,page/columns)*extent+clamp(local,vec2(.5),vec2(extent-.5));
}
vec4 PcgCoverage(vec2 worldXZ)
{
    uvec4 page;vec2 local;if(!PcgLocate(worldXZ,page,local))return vec4(0);
    return textureLod(pcgCoverageAtlas,PcgAtlasPixel(page.x-1u,local)/vec2(textureSize(pcgCoverageAtlas,0)),0);
}
float PcgRiverWetness(vec2 worldXZ)
{
    return PcgCoverage(worldXZ).b;
}
// x=河流权重，yz=世界 XZ 导数。双线性重建后再平滑，端点导数严格为零。
vec3 PcgWaterBlend(vec2 worldXZ)
{
    uvec4 page;vec2 local;if(!PcgLocate(worldXZ,page,local))return vec3(0);
    vec2 pixel=PcgAtlasPixel(page.x-1u,local)-.5;
    ivec2 origin=ivec2(floor(pixel));vec2 f=fract(pixel);
    uint index=page.x-1u;
    ivec2 pageMin=ivec2(index%pcgMetadata[0].w,index/pcgMetadata[0].w)*int(pcgMetadata[1].y);
    float v[4];
    for(int y=0;y<2;++y)for(int x=0;x<2;++x)
    {
        ivec2 at=clamp(origin+ivec2(x,y),pageMin,pageMin+ivec2(pcgMetadata[1].y-1u));
        v[y*2+x]=texelFetch(pcgWaterBlendAtlas,at,0).r;
    }
    float w=clamp(mix(mix(v[0],v[1],f.x),mix(v[2],v[3],f.x),f.y),0,1);
    vec2 gradient=vec2(mix(v[1]-v[0],v[3]-v[2],f.y),mix(v[2]-v[0],v[3]-v[1],f.x))*
        float(pcgMetadata[0].y)/uintBitsToFloat(pcgMetadata[0].x);
    return vec3(w*w*(3-2*w),gradient*(6*w*(1-w)));
}
bool PcgRiverSurface(vec2 worldXZ,out float height,out vec2 velocity)
{
    height=0;velocity=vec2(0);uvec4 page;vec2 local;
    if(!PcgLocate(worldXZ,page,local))return false;
    vec2 pixel=PcgAtlasPixel(page.x-1u,local)-.5;
    ivec2 origin=ivec2(floor(pixel));vec2 fraction=fract(pixel);float sum=0;
    uint pageIndex=page.x-1u;
    ivec2 pageMin=ivec2(pageIndex%pcgMetadata[0].w,pageIndex/pcgMetadata[0].w)*int(pcgMetadata[1].y);
    // 未覆盖的零填充值不得把岸边高度拉向零。
    for(int y=0;y<2;++y)for(int x=0;x<2;++x)
    {
        ivec2 at=clamp(origin+ivec2(x,y),pageMin,pageMin+ivec2(pcgMetadata[1].y-1u));
        float weight=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);
        weight*=texelFetch(pcgCoverageAtlas,at,0).g;
        height+=weight*texelFetch(pcgHeightAtlas,at,0).g;
        velocity+=weight*texelFetch(pcgVelocityAtlas,at,0).rg;sum+=weight;
    }
    if(sum<=0)return false;
    height/=sum;velocity/=sum;
    // 同 CPU 波包采样：从坡面入口就减慢 Flow，避免水面已融合而纹理仍高速平移。
    float blend=clamp(textureLod(pcgWaterBlendAtlas,PcgAtlasPixel(page.x-1u,local)/vec2(textureSize(pcgWaterBlendAtlas,0)),0).r,0,1);
    velocity*=blend*blend*(3-2*blend);
    return true;
}
float PcgHeightOr(vec2 worldXZ,float otherwise)
{
    float height;vec2 velocity;
    return PcgRiverSurface(worldXZ,height,velocity)?height:otherwise;
}
vec2 PcgRiverHeightGradient(vec2 worldXZ,float centerHeight)
{
    float stepSize=uintBitsToFloat(pcgMetadata[0].x)/float(max(pcgMetadata[0].y,1u));
    return vec2(PcgHeightOr(worldXZ+vec2(stepSize,0),centerHeight)-PcgHeightOr(worldXZ-vec2(stepSize,0),centerHeight),
        PcgHeightOr(worldXZ+vec2(0,stepSize),centerHeight)-PcgHeightOr(worldXZ-vec2(0,stepSize),centerHeight))/(2*stepSize);
}
vec4 PcgRiverProperties(vec2 worldXZ)
{
    uvec4 page;vec2 local;if(!PcgLocate(worldXZ,page,local))return vec4(0);
    return textureLod(pcgRiverPropertiesAtlas,PcgAtlasPixel(page.x-1u,local)/vec2(textureSize(pcgRiverPropertiesAtlas,0)),0);
}
#endif
