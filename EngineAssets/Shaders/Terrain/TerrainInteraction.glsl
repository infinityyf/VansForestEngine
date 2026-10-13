#ifndef TERRAIN_INTERACTION_GLSL
#define TERRAIN_INTERACTION_GLSL
layout(set=1,binding=9,std140) uniform TerrainInteractionParams
{
    vec4 worldRect;
    uvec4 channels;
    vec4 response;
    vec4 heightWeights;
    vec4 heightFade;
} terrainInteraction;
layout(set=1,binding=12) uniform sampler2D terrainInteractionHeight;

// 窗口外严格返回中性值，边缘渐隐避免随相机滚动露出新区域时出现断崖。
float TerrainInteractionCoverage(vec2 worldXZ, out vec2 uv)
{
    uv=(worldXZ-terrainInteraction.worldRect.xy)/terrainInteraction.worldRect.zw;
    if (any(lessThan(uv,vec2(0))) || any(greaterThanEqual(uv,vec2(1)))) return 0;
    vec2 edge=min(uv,1-uv)*terrainInteraction.worldRect.zw;
    float fade=terrainInteraction.response.w;
    return fade>0?smoothstep(0,fade,min(edge.x,edge.y)):1;
}
float TerrainInteractionHeightOffset(vec2 worldXZ)
{
    if (terrainInteraction.channels.z==0u) return 0;
    vec2 uv; float coverage=TerrainInteractionCoverage(worldXZ,uv);
    if (coverage<=0) return 0;
    float value=dot(textureLod(terrainInteractionHeight,uv,0),terrainInteraction.heightWeights);
    float distanceFade=1-smoothstep(terrainInteraction.heightFade.x,terrainInteraction.heightFade.y,
        distance(worldXZ,cameraPosition.xz));
    return clamp(value,-terrainInteraction.response.z,terrainInteraction.response.z)*coverage*distanceFade;
}
vec2 TerrainInteractionHeightGradient(vec2 worldXZ)
{
    if (terrainInteraction.channels.z==0u) return vec2(0);
    vec2 stepMeters=terrainInteraction.worldRect.zw/vec2(textureSize(terrainInteractionHeight,0));
    return vec2(
        TerrainInteractionHeightOffset(worldXZ+vec2(stepMeters.x,0))-TerrainInteractionHeightOffset(worldXZ-vec2(stepMeters.x,0)),
        TerrainInteractionHeightOffset(worldXZ+vec2(0,stepMeters.y))-TerrainInteractionHeightOffset(worldXZ-vec2(0,stepMeters.y)))/(2*stepMeters);
}
#endif
