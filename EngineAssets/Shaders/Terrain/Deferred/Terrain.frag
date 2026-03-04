#version 450
#extension GL_GOOGLE_include_directive : require
#include "../../Common/CameraData.glsl"
layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in float inHeight;

// 新增：在 Fragment Shader 中访问高度图以计算法线
layout(set = 1, binding = 0) uniform sampler2D heightMap;
layout(set = 1, binding = 1) uniform sampler2D albedoMap;

layout(location = 0) out vec4 outNormal; // GBuffer Normal
layout(location = 1) out vec4 outGbuffer0;
layout(location = 2) out vec4 outGbuffer1;
layout(location = 3) out vec4 outGbuffer2;

// 必须与 Vertex Shader 中的定义保持一致
const float TERRAIN_SIZE = 1024.0;
const float MAX_HEIGHT = 500.0;

// 计算地形法线 (Central Difference Method)
vec3 CalculateTerrainNormal(vec2 uv) {
    // 获取纹理尺寸
    ivec2 texSize = textureSize(heightMap, 0);
    vec2 texelSize = 1.0 / vec2(texSize);

    // 采样周围四个点的高度 (Left, Right, Down, Up)
    float hL = texture(heightMap, uv - vec2(texelSize.x, 0.0)).r * MAX_HEIGHT;
    float hR = texture(heightMap, uv + vec2(texelSize.x, 0.0)).r * MAX_HEIGHT;
    float hD = texture(heightMap, uv - vec2(0.0, texelSize.y)).r * MAX_HEIGHT;
    float hU = texture(heightMap, uv + vec2(0.0, texelSize.y)).r * MAX_HEIGHT;

    // 计算世界空间步长 (World Space Step)
    // 两个采样点之间的世界距离 = (地形总大小 / 纹理分辨率) * 2
    vec2 worldStep = (vec2(TERRAIN_SIZE) / vec2(texSize)) * 2.0;

    // 构建切线向量 (Tangent) 和 副切线向量 (Bitangent)
    // Tangent 指向 X 正方向: (dx, dh, 0)
    vec3 tangent = vec3(worldStep.x, hR - hL, 0.0);
    // Bitangent 指向 Z 正方向: (0, dh, dz)
    vec3 bitangent = vec3(0.0, hU - hD, worldStep.y);

    // 计算法线 (Y-Up 坐标系)
    // Cross(Bitangent, Tangent) 得到向上的法线
    return normalize(cross(bitangent, tangent));
}

void main() {
    // 计算法线
    vec3 normal = CalculateTerrainNormal(inUV);

    // 简单的基于高度的颜色调试
    vec3 color = texture(albedoMap, inUV).rgb;

    // 简单的网格线效果 (可选)
    // if (fract(inWorldPos.x / 16.0) < 0.05 || fract(inWorldPos.z / 16.0) < 0.05) color = vec3(0,0,0);

    // 输出法线 (映射到 0..1 范围，假设 GBuffer 是 UNORM)
    outNormal = vec4(normal * 0.5 + 0.5, 1.0); 
    
    outGbuffer0 = vec4(color, 1.0); // Albedo
    outGbuffer1 = vec4(0.0, 1.0, 0.0, 1.0); // Roughness=1.0, Metalness=0.0
    
    float linearDepth = (ViewMatrix * vec4(inWorldPos, 1.0)).z;
    outGbuffer2 = vec4(inWorldPos, -linearDepth);
}