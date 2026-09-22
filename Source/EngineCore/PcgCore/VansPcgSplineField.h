#pragma once
#include "VansPcgSplineEvaluator.h"
#include "../TerrainCore/VansTerrainAsset.h"
#include <map>
#include <memory>

namespace Vans
{
constexpr std::uint32_t VANS_SPLINE_TILE_SIZE = 64;
constexpr std::uint32_t VANS_SPLINE_TILE_BORDER = 1;
constexpr std::uint32_t VANS_SPLINE_TILE_EXTENT = VANS_SPLINE_TILE_SIZE + 2 * VANS_SPLINE_TILE_BORDER;
constexpr std::uint32_t VANS_SPLINE_MAX_ATLAS_PAGES = 2048;

struct VansPcgSplineFieldTile
{
    std::uint32_t x = 0, z = 0;
    std::uint64_t fingerprint = 0;
    std::uint64_t terrainShapeFingerprint = 0;
    std::vector<glm::vec2> heights;
    std::vector<glm::vec2> velocities;
    // R=道路核心，G=河流水面有效区，B=河流地表湿润，A=地形变形/细节抑制。
    std::vector<glm::vec4> coverage;
    // 独立派生排除场，不修改任何植被作者 Mask。
    std::vector<float> vegetationExclusion;
    // x=河内过渡权重，y=法线流动权重，z=水深，w=岸沿高差。
    std::vector<glm::vec4> riverProperties;
    // 独立于流速与湿岸的水面混合场，0=全局水面，1=河流水面。
    std::vector<float> waterBlend;
    float minimumWaterHeight = 0;
    float maximumWaterHeight = 0;
    float minimumRiverWidth = 0;
    float maximumHeightConflict = 0;
    bool hasRiver = false;
};

struct VansPcgRoadVertex
{
    glm::vec3 position{};
    glm::vec3 normal{0, 1, 0};
    glm::vec2 uv{};
    glm::vec4 tangent{1, 0, 0, -1};
};

struct VansPcgRoadMesh
{
    std::string splineId;
    VansAssetGuid material;
    VansAssetGuid roadDecalMaterial;
    VansPcgRoadRenderMode renderMode = VansPcgRoadRenderMode::Mesh;
    float projectedDepth = 2.0f;
    std::uint64_t fingerprint = 0;
    std::vector<VansPcgRoadVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct VansPcgSplineFieldSnapshot
{
    VansAssetGuid terrainGuid;
    std::uint64_t sourceFingerprint = 0;
    std::uint64_t terrainFingerprint = 0;
    bool hasVegetationExclusion = false;
    float worldSize = 0;
    float texelSize = 0;
    std::uint32_t resolution = 0;
    std::map<std::uint64_t, std::shared_ptr<const VansPcgSplineFieldTile>> tiles;
    std::map<std::string, std::shared_ptr<const VansPcgRoadMesh>> roads;
    std::shared_ptr<const VansTerrainAsset> effectiveTerrain;
    // 包含本次删除的页，消费者需清除旧位置，而不只是上传新位置。
    std::vector<std::uint64_t> changedTiles;
    std::vector<std::string> warnings;
    // One representative world position per tile where terrain cannot hide the mask boundary.
    std::vector<glm::vec3> uncoveredBankPoints;
    std::size_t rebuiltTileCount = 0;

    static std::uint64_t TileKey(std::uint32_t x, std::uint32_t z) { return (std::uint64_t(z) << 32) | x; }
    float SampleVegetationExclusion(float x, float z) const;
    bool SampleRiver(glm::vec2 world, float& height, glm::vec2& velocity, glm::vec4& properties) const;
    // x=混合权重，yz=对世界 XZ 的导数；与 GPU 双线性重建和端部平滑一致。
    glm::vec3 SampleWaterBlend(glm::vec2 world) const;
    const VansPcgSplineFieldTile* FindTile(std::uint32_t x, std::uint32_t z) const;
};

class VansPcgSplineFieldBuilder
{
public:
    // 纯 CPU、不可变输入/输出；调用方将任务放入 Job，RenderCore 只消费成功发布的完整快照。
    static std::shared_ptr<const VansPcgSplineFieldSnapshot> Build(
        const VansPcgSplineAsset& asset, std::shared_ptr<const VansTerrainAsset> baseTerrain,
        std::shared_ptr<const VansPcgSplineFieldSnapshot> previous, std::string& error);
};
}
