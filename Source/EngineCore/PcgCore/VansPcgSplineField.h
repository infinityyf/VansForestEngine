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
constexpr std::uint32_t VANS_SPLINE_MAX_DOMAINS_PER_TILE = 8;
constexpr std::uint32_t VANS_SPLINE_MAX_ATLAS_PAGES = 2048;

struct VansPcgRiverCoordinateTile
{
    std::string splineId;
    float cycleSeconds = 2;
    bool flowEnabled = true;
    // xy=连续米制坐标，z=未归一化融合权重，w=该域属性有效；域身份属于页，不能线性过滤。
    std::vector<glm::vec4> coordinates;
    std::vector<glm::vec4> jacobians;
};

struct VansPcgSplineFieldTile
{
    std::uint32_t x = 0, z = 0;
    std::uint64_t fingerprint = 0;
    std::uint64_t terrainShapeFingerprint = 0;
    std::vector<glm::vec2> heights;
    std::vector<glm::vec2> velocities;
    std::vector<glm::vec4> coverage;
    // 独立派生排除场，不修改任何植被作者 Mask。
    std::vector<float> vegetationExclusion;
    std::vector<VansPcgRiverCoordinateTile> domains;
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
