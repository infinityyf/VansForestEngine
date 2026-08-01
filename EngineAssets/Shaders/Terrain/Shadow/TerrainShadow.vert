#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

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

// 地形阴影只读取方向光矩阵，SSBO 前缀布局必须与 LightsData.glsl 保持一致。
layout(set = LightCBBind, binding = LightBinding, std430) readonly buffer TerrainShadowLightsData
{
    uint uPointLightCount;
    uint uSpotLightCount;
    uint uShadowAtlasSize;
    uint uShadowAtlasCount;
    vec4 softShadowParams;
    DirectionLightData uDirectionLight;
};

// 顶点输入（基础 16x16 patch，局部坐标 0..16）。
layout(location = 0) in f16vec3 inPos;
layout(location = 1) in f16vec2 inUV;
layout(location = 2) in f16vec3 inNormal;

// 实例输入。
layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceLod;
layout(location = 6) in float instanceStitchFlags;

layout(location = 0) out float shadowDepth;

layout(push_constant) uniform CascadePushConst
{
    int cascadeIndex;
} pushConst;

void main()
{
    vec2 heightUV;
    float height;
    vec3 worldPos = TerrainBuildWorldPosition(inPos.xz, instanceOffset, instanceScale, instanceStitchFlags, heightUV, height);
    worldPos = TerrainApplyGeometryNoise(worldPos);

    vec4 clipCoord = uDirectionLight.shadowMatrix[pushConst.cascadeIndex] * vec4(worldPos, 1.0);
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z;
}
