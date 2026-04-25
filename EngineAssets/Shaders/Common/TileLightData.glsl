#ifndef TILE_LIGHT_DATA_GLSL
#define TILE_LIGHT_DATA_GLSL

// ============================================================================
// TileLightData.glsl — TileLight 屏幕空间分格数据结构
//
// 依赖：调用方须在 include 本文件前先 include CameraData.glsl（提供 ScreenParams）
// 用法（读取 pass）：
//   layout(set=0, binding=9)  readonly buffer TileLightGrid      → tileLightHeaders[]
//   layout(set=0, binding=10) readonly buffer TileLightIndexList → tileLightIndices[]
// ============================================================================

// --- Tile 尺寸（与 FogVolumeCommon.glsl 中 TILE_SIZE 保持一致）---
#define TILE_LIGHT_TILE_SIZE 8

// --- 每 tile 最大索引槽数（固定步长分配，无需 atomic）---
#define TILE_LIGHT_MAX_PT_PER_TILE 64  // 与 MAX_POINT_LIGHTS 对齐
#define TILE_LIGHT_MAX_SP_PER_TILE 64  // 与 MAX_SPOT_LIGHTS 对齐

// --- TileLight Header：记录某 tile 内点光/聚光的索引范围 ---
struct TileLightHeader
{
    uint pointOffset;  // 在 tileLightIndices 中点光源索引起始位置（固定步长）
    uint pointCount;   // 该 tile 覆盖的点光源实际数量
    uint spotOffset;   // 聚光灯索引起始位置
    uint spotCount;    // 该 tile 覆盖的聚光灯实际数量
};

// --- Set 0 只读绑定（BuildTileLightList.comp 写，其余 pass 读）---
layout(set = 0, binding = 9, std430)
readonly buffer TileLightGrid
{
    TileLightHeader tileLightHeaders[];
};

layout(set = 0, binding = 10, std430)
readonly buffer TileLightIndexList
{
    uint tileLightIndices[];
};

// ---------------------------------------------------------------------------
// GetFragTileLightHeader — 根据当前片元坐标返回对应 tile 的 TileLightHeader
// 仅供 Fragment Shader 调用（依赖 gl_FragCoord 和 ScreenParams）
// 使用方须在 include 本文件前 #define TILE_LIGHT
// ---------------------------------------------------------------------------
#ifdef TILE_LIGHT
TileLightHeader GetFragTileLightHeader()
{
    // gl_FragCoord.xy：Vulkan 坐标系，(0,0) 在左上角，y 向下递增
    uint tileX     = uint(gl_FragCoord.x) / uint(TILE_LIGHT_TILE_SIZE);
    uint tileY     = uint(gl_FragCoord.y) / uint(TILE_LIGHT_TILE_SIZE);
    uint tileGridX = (uint(ScreenParams.x) + uint(TILE_LIGHT_TILE_SIZE) - 1u)
                     / uint(TILE_LIGHT_TILE_SIZE);
    uint tileIdx   = tileY * tileGridX + tileX;
    return tileLightHeaders[tileIdx];
}
#endif // TILE_LIGHT

#endif // TILE_LIGHT_DATA_GLSL
