#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../../Common/CameraData.glsl"
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

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out float outHeight;

void main() {
    vec2 heightUV;
    float height;
    vec3 worldPos = TerrainBuildWorldPosition(inPos.xz, instanceOffset, instanceScale, instanceStitchFlags, heightUV, height);

    gl_Position = VPMatrix * vec4(worldPos, 1.0);

    outUV = heightUV;
    outWorldPos = worldPos;
    outHeight = height;
}