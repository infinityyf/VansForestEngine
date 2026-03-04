#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../Common/CameraData.glsl"
#include "../../Lights/LightsData.glsl"

// 顶点输入 (基础 16x16 Patch, 局部坐标 0..16)
layout( location = 0 ) in f16vec3 inPos; 
layout( location = 1 ) in f16vec2 inUV;
layout( location = 2 ) in f16vec3 inNormal;

// Instance 输入
layout(location = 3) in vec2 instanceOffset;
layout(location = 4) in float instanceScale;
layout(location = 5) in float instanceLod;
layout(location = 6) in float instanceStitchFlags; // 新增：缝合标记

// 高度图
layout(set = 1, binding = 0) uniform sampler2D heightMap;

layout( location = 0 ) out float shadowDepth;

const float TERRAIN_SIZE = 1024.0;
const float MAX_HEIGHT = 500.0;
const float PATCH_SIZE = 16.0;

void main() {
    vec2 localPos = inPos.xz; 
    
    // --- 边缘缝合 (Stitching) ---
    int flags = int(instanceStitchFlags);
    
    // 检查是否在边缘 (使用 epsilon 防止浮点误差)
    bool isLeft   = (localPos.x < 0.1);
    bool isRight  = (localPos.x > PATCH_SIZE - 0.1);
    bool isTop    = (localPos.y < 0.1);
    bool isBottom = (localPos.y > PATCH_SIZE - 0.1);

    // 检查是否需要缝合
    // Bit 0: Left, Bit 1: Right, Bit 2: Top, Bit 3: Bottom
    bool stitchLeft   = isLeft   && ((flags & 1) != 0);
    bool stitchRight  = isRight  && ((flags & 2) != 0);
    bool stitchTop    = isTop    && ((flags & 4) != 0);
    bool stitchBottom = isBottom && ((flags & 8) != 0);

    vec2 snappedLocalPos = localPos;

    // 如果需要缝合，将坐标吸附到偶数网格点 (模拟下一级 LOD)
    // floor(x / 2.0) * 2.0 将 1->0, 3->2, 5->4...
    
    if (stitchLeft || stitchRight) {
        // 左右边缘：Y 轴坐标需要简化
        snappedLocalPos.y = floor(localPos.y * 0.5) * 2.0;
    }
    
    if (stitchTop || stitchBottom) {
        // 上下边缘：X 轴坐标需要简化
        snappedLocalPos.x = floor(localPos.x * 0.5) * 2.0;
    }
    
    // --- 结束缝合 ---

    // 1. 计算世界坐标 (使用吸附后的局部坐标)
    vec2 worldPosXZ = snappedLocalPos * instanceScale + instanceOffset;

    // 2. 计算高度图 UV
    vec2 heightUV = worldPosXZ / TERRAIN_SIZE;
    heightUV += 0.5; // 调整到 0..1 范围
    
    // 3. 采样高度
    float height = texture(heightMap, heightUV).r * MAX_HEIGHT;

    // 4. 组合最终世界坐标
    //整体平移到原点为中心
    vec3 worldPos = vec3(worldPosXZ.x, height - 23, worldPosXZ.y);

    vec4 clipCoord = uDirectionLight.shadowMatrix * vec4(worldPos, 1.0);
    clipCoord.z = clipCoord.z * 0.5 + 0.5;
    gl_Position = clipCoord;
    shadowDepth = clipCoord.z;
}