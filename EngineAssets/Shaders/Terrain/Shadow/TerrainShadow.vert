#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../Common/CameraData.glsl"
#include "../../Lights/LightsData.glsl"
#include "../TerrainCommon.glsl"

// 顶点输入 (基础 16x16 Patch, 局部坐标 0..16)
layout( location = 0 ) in f16vec3 inPos; 
layout( location = 1 ) in f16vec2 inUV;
layout( location = 2 ) in f16vec3 inNormal;

// Instance 输入
layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceLod;
layout(location = 6) in float instanceStitchFlags; // 新增：缝合标记

layout( location = 0 ) out float shadowDepth;

layout( push_constant ) uniform CascadePushConst
{
    int cascadeIndex;
} pushConst;

void main() {
    vec2 heightUV;
    float height;
    vec3 worldPos = TerrainBuildWorldPosition(inPos.xz, instanceOffset, instanceScale, instanceStitchFlags, heightUV, height);

    vec4 clipCoord = uDirectionLight.shadowMatrix[pushConst.cascadeIndex] * vec4(worldPos, 1.0);
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z;
}