#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
    enum TerrainEdge : uint32_t
    {
        TerrainEdge_Left   = 1u << 0,
        TerrainEdge_Right  = 1u << 1,
        TerrainEdge_Top    = 1u << 2,
        TerrainEdge_Bottom = 1u << 3
    };

    struct TerrainLodSettings
    {
        float terrainSize = 1024.0f;
        float minPatchSize = 16.0f;
        float minHeight = -23.0f;
        float maxHeight = 477.0f;
        float baseDistance = 64.0f;
        float distanceRatio = 2.0f;
        float morphStartRatio = 0.70f;
    };

    struct TerrainLodPatch
    {
        uint32_t gridX = 0;
        uint32_t gridZ = 0;
        uint32_t level = 0; // 0 为最细层；层级每增加 1，边长扩大一倍。
        uint32_t edgeFlags = 0;
        bool tessellated = false;
        float morphStart = 0.0f;
        float morphEnd = 0.0f;
    };

    // 规则四叉树 CDLOD 选择器。它只负责几何拓扑，不持有渲染资源。
    class VansTerrainLodSelector
    {
    public:
        bool Configure(const TerrainLodSettings& settings, std::string* error = nullptr);

        void Select(
            const glm::vec3& cameraPosition,
            bool tessellationEnabled,
            float tessellationDistance,
            std::vector<TerrainLodPatch>& outPatches) const;

        bool ValidateSelection(
            const std::vector<TerrainLodPatch>& patches,
            std::string* error = nullptr) const;

        float GetPatchWorldSize(const TerrainLodPatch& patch) const;
        glm::vec2 GetPatchWorldOrigin(const TerrainLodPatch& patch) const;
        uint32_t GetRootCellCount() const { return m_RootCellCount; }
        uint32_t GetMaxLevel() const { return m_MaxLevel; }

    private:
        struct NodeKey
        {
            uint32_t x = 0;
            uint32_t z = 0;
            uint32_t level = 0;

            bool operator==(const NodeKey& other) const
            {
                return x == other.x && z == other.z && level == other.level;
            }
        };

        struct NodeKeyHash
        {
            size_t operator()(const NodeKey& key) const;
        };

        using PatchLookup = std::unordered_map<NodeKey, size_t, NodeKeyHash>;

        void SelectNode(
            uint32_t gridX,
            uint32_t gridZ,
            uint32_t level,
            const glm::vec3& cameraPosition,
            std::vector<TerrainLodPatch>& outPatches) const;

        float DistanceToPatchAABB(
            uint32_t gridX,
            uint32_t gridZ,
            uint32_t level,
            const glm::vec3& cameraPosition) const;

        PatchLookup BuildLookup(const std::vector<TerrainLodPatch>& patches) const;
        const TerrainLodPatch* FindPatchAt(
            int64_t cellX,
            int64_t cellZ,
            const std::vector<TerrainLodPatch>& patches,
            const PatchLookup& lookup) const;
        const TerrainLodPatch* FindNeighbor(
            const TerrainLodPatch& patch,
            uint32_t edge,
            const std::vector<TerrainLodPatch>& patches,
            const PatchLookup& lookup) const;

        void Balance(std::vector<TerrainLodPatch>& patches) const;
        void BuildEdgeFlags(std::vector<TerrainLodPatch>& patches) const;
        void SplitPatch(const TerrainLodPatch& patch, std::vector<TerrainLodPatch>& outChildren) const;
        void SetPatchMorphRange(TerrainLodPatch& patch) const;
        void SetError(std::string* error, const std::string& message) const;

        TerrainLodSettings m_Settings;
        uint32_t m_RootCellCount = 0;
        uint32_t m_MaxLevel = 0;
        std::vector<float> m_VisibilityRanges;
        bool m_Configured = false;
    };
}
