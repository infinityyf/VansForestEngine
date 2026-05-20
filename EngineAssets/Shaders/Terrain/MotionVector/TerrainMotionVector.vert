#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../../Common/CameraData.glsl"
#include "../TerrainCommon.glsl"

// ---------------------------------------------------------------------------
// Terrain Motion Vector — vertex shader
//
// Computes current-frame and previous-frame clip positions for each terrain
// vertex so the fragment shader can output a per-pixel screen-space velocity.
//
// Descriptor sets:
//   Set 0 — Global  (CameraData UBO: VPMatrix, LastVPMatrix, …)
//   Set 1 — Terrain (binding 0 = heightMap)
// ---------------------------------------------------------------------------

// 顶点输入 (基础 16x16 Patch, 局部坐标 0..16)
layout( location = 0 ) in f16vec3 inPos; 
layout( location = 1 ) in f16vec2 inUV;
layout( location = 2 ) in f16vec3 inNormal;

// Instance 输入
layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceLod;
layout(location = 6) in float instanceStitchFlags;

// Clip-space positions passed to the fragment shader
layout( location = 0 ) out vec4 vCurrentClipPos;
layout( location = 1 ) out vec4 vPreviousClipPos;

void main() {
    vec2 heightUV;
    float height;
    vec3 worldPos = TerrainBuildWorldPosition(inPos.xz, instanceOffset, instanceScale, instanceStitchFlags, heightUV, height);

    // MotionVector 必须使用未 jitter 的 VP；否则静止镜头下 terrain 仍会写入亚像素速度。
    vec4 currentClip = UnjitteredVPMatrix * vec4(worldPos, 1.0);

    // Previous-frame clip position (camera motion only — terrain is static)
    vec4 previousClip = LastUnjitteredVPMatrix * vec4(worldPos, 1.0);

    // 光栅化位置仍使用 jittered VP，保持 TAA / GBuffer 抖动一致。
    gl_Position      = VPMatrix * vec4(worldPos, 1.0);
    vCurrentClipPos  = currentClip;
    vPreviousClipPos = previousClip;
}
