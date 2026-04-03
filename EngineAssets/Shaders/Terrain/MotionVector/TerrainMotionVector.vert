#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require

#include "../../Common/CameraData.glsl"

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

// 高度图
layout(set = 1, binding = 0) uniform sampler2D heightMap;

// Clip-space positions passed to the fragment shader
layout( location = 0 ) out vec4 vCurrentClipPos;
layout( location = 1 ) out vec4 vPreviousClipPos;

const float TERRAIN_SIZE = 1024.0;
const float MAX_HEIGHT = 500.0;
const float PATCH_SIZE = 16.0;

void main() {
    vec2 localPos = inPos.xz; 
    
    // --- 边缘缝合 (Stitching) ---
    int flags = int(instanceStitchFlags);
    
    bool isLeft   = (localPos.x < 0.1);
    bool isRight  = (localPos.x > PATCH_SIZE - 0.1);
    bool isTop    = (localPos.y < 0.1);
    bool isBottom = (localPos.y > PATCH_SIZE - 0.1);

    bool stitchLeft   = isLeft   && ((flags & 1) != 0);
    bool stitchRight  = isRight  && ((flags & 2) != 0);
    bool stitchTop    = isTop    && ((flags & 4) != 0);
    bool stitchBottom = isBottom && ((flags & 8) != 0);

    vec2 snappedLocalPos = localPos;

    if (stitchLeft || stitchRight) {
        snappedLocalPos.y = floor(localPos.y * 0.5) * 2.0;
    }
    
    if (stitchTop || stitchBottom) {
        snappedLocalPos.x = floor(localPos.x * 0.5) * 2.0;
    }
    
    // --- 结束缝合 ---

    // 1. 计算世界坐标
    vec2 worldPosXZ = snappedLocalPos * instanceScale + instanceOffset;

    // 2. 计算高度图 UV
    vec2 heightUV = worldPosXZ / TERRAIN_SIZE;
    heightUV += 0.5;
    
    // 3. 采样高度
    float height = texture(heightMap, heightUV).r * MAX_HEIGHT;

    // 4. 组合最终世界坐标
    vec3 worldPos = vec3(worldPosXZ.x, height - 23, worldPosXZ.y);

    // Current-frame clip position
    vec4 currentClip = VPMatrix * vec4(worldPos, 1.0);

    // Previous-frame clip position (camera motion only — terrain is static)
    vec4 previousClip = LastVPMatrix * vec4(worldPos, 1.0);

    gl_Position      = currentClip;
    vCurrentClipPos  = currentClip;
    vPreviousClipPos = previousClip;
}
