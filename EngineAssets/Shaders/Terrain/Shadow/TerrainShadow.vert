#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

#ifndef CASCADE_COUNT
#define CASCADE_COUNT 4
#endif

#if !defined(LightCBBind)
    #define LightCBBind 0
#endif

#if !defined(LightBinding)
    #define LightBinding 1
#endif

struct DirectionLightData
{
    vec4 direction;
    vec4 color;
    float intensity;
    mat4x4 shadowMatrix[CASCADE_COUNT];
    vec4 cascadeSplits;
    vec4 cascadeTexelSize;
    vec4 cascadeDepthScale;
    vec4 cascadeNormalBias;
    vec4 cascadeFilterRadius;
};

layout(set = LightCBBind, binding = LightBinding, std430) readonly buffer TerrainShadowLightsData
{
    uint uPointLightCount;
    uint uSpotLightCount;
    uint uShadowAtlasSize;
    uint uShadowAtlasCount;
    vec4 softShadowParams;
    DirectionLightData uDirectionLight;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in uint instanceEdgeFlags;
layout(location = 6) in vec2 instanceMorphRange;

layout(location = 0) out float shadowDepth;

layout(push_constant) uniform CascadePushConst
{
    int cascadeIndex;
} pushConst;

void main()
{
    vec2 heightUV;
    float worldHeight;
    vec3 worldPos = TerrainBuildWorldPosition(
        vec2(inPos.xz),
        instanceOffset,
        instanceScale,
        instanceEdgeFlags,
        instanceMorphRange,
        heightUV,
        worldHeight);

    vec4 clipCoord = uDirectionLight.shadowMatrix[pushConst.cascadeIndex] * vec4(worldPos, 1.0);
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z;
}
