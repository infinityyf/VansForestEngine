#include "VansTerrainLod.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace VansGraphics
{
    namespace
    {
        bool IsPowerOfTwo(uint32_t value)
        {
            return value != 0 && (value & (value - 1u)) == 0;
        }

        uint32_t PatchCellSize(const TerrainLodPatch& patch)
        {
            return 1u << patch.level;
        }
    }

    size_t VansTerrainLodSelector::NodeKeyHash::operator()(const NodeKey& key) const
    {
        size_t seed = static_cast<size_t>(key.x) * 0x9E3779B185EBCA87ull;
        seed ^= static_cast<size_t>(key.z) + 0x9E3779B9u + (seed << 6u) + (seed >> 2u);
        seed ^= static_cast<size_t>(key.level) + 0x85EBCA77u + (seed << 6u) + (seed >> 2u);
        return seed;
    }

    void VansTerrainLodSelector::SetError(std::string* error, const std::string& message) const
    {
        if (error)
            *error = message;
    }

    bool VansTerrainLodSelector::Configure(const TerrainLodSettings& settings, std::string* error)
    {
        m_Configured = false;
        m_RootCellCount = 0;
        m_MaxLevel = 0;
        m_VisibilityRanges.clear();

        if (!std::isfinite(settings.terrainSize) || !std::isfinite(settings.minPatchSize) ||
            settings.terrainSize <= 0.0f || settings.minPatchSize <= 0.0f)
        {
            SetError(error, "Terrain size and minimum patch size must be finite positive values.");
            return false;
        }

        const double cellCountFloat = static_cast<double>(settings.terrainSize) /
            static_cast<double>(settings.minPatchSize);
        if (cellCountFloat > static_cast<double>(1u << 30u))
        {
            SetError(error, "Terrain quadtree exceeds the supported integer coordinate range.");
            return false;
        }
        const uint32_t cellCount = static_cast<uint32_t>(std::llround(cellCountFloat));
        if (std::abs(cellCountFloat - static_cast<double>(cellCount)) > 1.0e-4 || !IsPowerOfTwo(cellCount))
        {
            SetError(error, "Terrain size must equal the minimum patch size multiplied by a power of two.");
            return false;
        }
        if (!std::isfinite(settings.baseDistance) || settings.baseDistance <= 0.0f)
        {
            SetError(error, "CDLOD base distance must be a finite positive value.");
            return false;
        }
        if (!std::isfinite(settings.distanceRatio) || settings.distanceRatio < 2.0f)
        {
            SetError(error, "CDLOD distance ratio must be at least 2.0 to preserve a balanced transition band.");
            return false;
        }
        if (!std::isfinite(settings.morphStartRatio) ||
            settings.morphStartRatio <= 0.0f || settings.morphStartRatio >= 1.0f)
        {
            SetError(error, "CDLOD morph start ratio must be between zero and one.");
            return false;
        }
        if (!std::isfinite(settings.minHeight) || !std::isfinite(settings.maxHeight) ||
            settings.minHeight > settings.maxHeight)
        {
            SetError(error, "Terrain height bounds are invalid.");
            return false;
        }

        m_Settings = settings;
        m_RootCellCount = cellCount;
        while ((1u << m_MaxLevel) < m_RootCellCount)
            ++m_MaxLevel;

        m_VisibilityRanges.resize(static_cast<size_t>(m_MaxLevel) + 1u);
        m_VisibilityRanges[0] = settings.baseDistance;
        for (uint32_t level = 1; level <= m_MaxLevel; ++level)
        {
            const double nextRange = static_cast<double>(m_VisibilityRanges[level - 1u]) *
                static_cast<double>(settings.distanceRatio);
            if (!std::isfinite(nextRange) ||
                nextRange > static_cast<double>(std::numeric_limits<float>::max()))
            {
                m_VisibilityRanges.clear();
                SetError(error, "CDLOD visibility ranges exceed the supported floating-point range.");
                return false;
            }
            m_VisibilityRanges[level] = static_cast<float>(nextRange);
        }

        m_Configured = true;
        if (error)
            error->clear();
        return true;
    }

    float VansTerrainLodSelector::GetPatchWorldSize(const TerrainLodPatch& patch) const
    {
        return m_Settings.minPatchSize * static_cast<float>(PatchCellSize(patch));
    }

    glm::vec2 VansTerrainLodSelector::GetPatchWorldOrigin(const TerrainLodPatch& patch) const
    {
        const float terrainMin = -m_Settings.terrainSize * 0.5f;
        return glm::vec2(
            terrainMin + static_cast<float>(patch.gridX) * m_Settings.minPatchSize,
            terrainMin + static_cast<float>(patch.gridZ) * m_Settings.minPatchSize);
    }

    float VansTerrainLodSelector::DistanceToPatchAABB(
        uint32_t gridX,
        uint32_t gridZ,
        uint32_t level,
        const glm::vec3& cameraPosition) const
    {
        TerrainLodPatch patch;
        patch.gridX = gridX;
        patch.gridZ = gridZ;
        patch.level = level;
        const glm::vec2 origin = GetPatchWorldOrigin(patch);
        const float size = GetPatchWorldSize(patch);

        const glm::vec3 boundsMin(origin.x, m_Settings.minHeight, origin.y);
        const glm::vec3 boundsMax(origin.x + size, m_Settings.maxHeight, origin.y + size);
        const glm::vec3 delta = glm::max(glm::max(boundsMin - cameraPosition, glm::vec3(0.0f)), cameraPosition - boundsMax);
        return glm::length(delta);
    }

    void VansTerrainLodSelector::SelectNode(
        uint32_t gridX,
        uint32_t gridZ,
        uint32_t level,
        const glm::vec3& cameraPosition,
        std::vector<TerrainLodPatch>& outPatches) const
    {
        const float distance = DistanceToPatchAABB(gridX, gridZ, level, cameraPosition);
        if (level > 0 && distance < m_VisibilityRanges[level - 1u])
        {
            const uint32_t childLevel = level - 1u;
            const uint32_t childCells = 1u << childLevel;
            SelectNode(gridX, gridZ, childLevel, cameraPosition, outPatches);
            SelectNode(gridX + childCells, gridZ, childLevel, cameraPosition, outPatches);
            SelectNode(gridX, gridZ + childCells, childLevel, cameraPosition, outPatches);
            SelectNode(gridX + childCells, gridZ + childCells, childLevel, cameraPosition, outPatches);
            return;
        }

        TerrainLodPatch patch;
        patch.gridX = gridX;
        patch.gridZ = gridZ;
        patch.level = level;
        SetPatchMorphRange(patch);
        outPatches.push_back(patch);
    }

    void VansTerrainLodSelector::SetPatchMorphRange(TerrainLodPatch& patch) const
    {
        if (patch.level >= m_MaxLevel)
            return;

        const float innerRange = patch.level == 0u ? 0.0f : m_VisibilityRanges[patch.level - 1u];
        patch.morphEnd = m_VisibilityRanges[patch.level];
        patch.morphStart = innerRange +
            (patch.morphEnd - innerRange) * m_Settings.morphStartRatio;
    }

    VansTerrainLodSelector::PatchLookup VansTerrainLodSelector::BuildLookup(
        const std::vector<TerrainLodPatch>& patches) const
    {
        PatchLookup lookup;
        lookup.reserve(patches.size() * 2u);
        for (size_t index = 0; index < patches.size(); ++index)
        {
            const TerrainLodPatch& patch = patches[index];
            lookup.emplace(NodeKey{ patch.gridX, patch.gridZ, patch.level }, index);
        }
        return lookup;
    }

    const TerrainLodPatch* VansTerrainLodSelector::FindPatchAt(
        int64_t cellX,
        int64_t cellZ,
        const std::vector<TerrainLodPatch>& patches,
        const PatchLookup& lookup) const
    {
        if (cellX < 0 || cellZ < 0 ||
            cellX >= static_cast<int64_t>(m_RootCellCount) ||
            cellZ >= static_cast<int64_t>(m_RootCellCount))
        {
            return nullptr;
        }

        const uint32_t x = static_cast<uint32_t>(cellX);
        const uint32_t z = static_cast<uint32_t>(cellZ);
        for (uint32_t level = 0; level <= m_MaxLevel; ++level)
        {
            const uint32_t size = 1u << level;
            const NodeKey key{ x & ~(size - 1u), z & ~(size - 1u), level };
            const auto found = lookup.find(key);
            if (found != lookup.end())
                return &patches[found->second];
        }
        return nullptr;
    }

    const TerrainLodPatch* VansTerrainLodSelector::FindNeighbor(
        const TerrainLodPatch& patch,
        uint32_t edge,
        const std::vector<TerrainLodPatch>& patches,
        const PatchLookup& lookup) const
    {
        const int64_t size = static_cast<int64_t>(PatchCellSize(patch));
        const int64_t middleX = static_cast<int64_t>(patch.gridX) + size / 2;
        const int64_t middleZ = static_cast<int64_t>(patch.gridZ) + size / 2;

        switch (edge)
        {
        case TerrainEdge_Left:
            return FindPatchAt(static_cast<int64_t>(patch.gridX) - 1, middleZ, patches, lookup);
        case TerrainEdge_Right:
            return FindPatchAt(static_cast<int64_t>(patch.gridX) + size, middleZ, patches, lookup);
        case TerrainEdge_Top:
            return FindPatchAt(middleX, static_cast<int64_t>(patch.gridZ) - 1, patches, lookup);
        case TerrainEdge_Bottom:
            return FindPatchAt(middleX, static_cast<int64_t>(patch.gridZ) + size, patches, lookup);
        default:
            return nullptr;
        }
    }

    void VansTerrainLodSelector::SplitPatch(
        const TerrainLodPatch& patch,
        std::vector<TerrainLodPatch>& outChildren) const
    {
        const uint32_t childLevel = patch.level - 1u;
        const uint32_t childCells = 1u << childLevel;
        const uint32_t childX[4] = { patch.gridX, patch.gridX + childCells, patch.gridX, patch.gridX + childCells };
        const uint32_t childZ[4] = { patch.gridZ, patch.gridZ, patch.gridZ + childCells, patch.gridZ + childCells };
        for (size_t child = 0; child < 4; ++child)
        {
            TerrainLodPatch result;
            result.gridX = childX[child];
            result.gridZ = childZ[child];
            result.level = childLevel;
            SetPatchMorphRange(result);
            outChildren.push_back(result);
        }
    }

    void VansTerrainLodSelector::Balance(std::vector<TerrainLodPatch>& patches) const
    {
        for (uint32_t pass = 0; pass < m_MaxLevel; ++pass)
        {
            const PatchLookup lookup = BuildLookup(patches);
            std::unordered_set<NodeKey, NodeKeyHash> splitNodes;

            for (const TerrainLodPatch& patch : patches)
            {
                constexpr uint32_t edges[] = {
                    TerrainEdge_Left, TerrainEdge_Right, TerrainEdge_Top, TerrainEdge_Bottom
                };
                for (uint32_t edge : edges)
                {
                    const TerrainLodPatch* neighbor = FindNeighbor(patch, edge, patches, lookup);
                    if (neighbor && neighbor->level > patch.level + 1u)
                        splitNodes.insert(NodeKey{ neighbor->gridX, neighbor->gridZ, neighbor->level });
                }
            }

            if (splitNodes.empty())
                return;

            std::vector<TerrainLodPatch> balanced;
            balanced.reserve(patches.size() + splitNodes.size() * 3u);
            for (const TerrainLodPatch& patch : patches)
            {
                if (splitNodes.find(NodeKey{ patch.gridX, patch.gridZ, patch.level }) != splitNodes.end())
                    SplitPatch(patch, balanced);
                else
                    balanced.push_back(patch);
            }
            patches.swap(balanced);
        }
    }

    void VansTerrainLodSelector::BuildEdgeFlags(std::vector<TerrainLodPatch>& patches) const
    {
        const PatchLookup lookup = BuildLookup(patches);
        constexpr uint32_t edges[] = {
            TerrainEdge_Left, TerrainEdge_Right, TerrainEdge_Top, TerrainEdge_Bottom
        };

        for (TerrainLodPatch& patch : patches)
        {
            uint32_t coarserMask = 0;
            uint32_t transitionMask = 0;
            uint32_t tessBoundaryMask = 0;
            for (uint32_t edge : edges)
            {
                const TerrainLodPatch* neighbor = FindNeighbor(patch, edge, patches, lookup);
                if (!neighbor)
                    continue;

                if (neighbor->level > patch.level)
                    coarserMask |= edge;
                if (neighbor->level != patch.level)
                    transitionMask |= edge;
                if (neighbor->level != patch.level || neighbor->tessellated != patch.tessellated)
                    tessBoundaryMask |= edge;
            }
            patch.edgeFlags = coarserMask | (transitionMask << 4u) | (tessBoundaryMask << 8u);
        }
    }

    void VansTerrainLodSelector::Select(
        const glm::vec3& cameraPosition,
        bool tessellationEnabled,
        float tessellationDistance,
        std::vector<TerrainLodPatch>& outPatches) const
    {
        outPatches.clear();
        if (!m_Configured)
            return;

        SelectNode(0, 0, m_MaxLevel, cameraPosition, outPatches);
        Balance(outPatches);
        for (TerrainLodPatch& patch : outPatches)
        {
            const float distance = DistanceToPatchAABB(
                patch.gridX, patch.gridZ, patch.level, cameraPosition);
            patch.tessellated = tessellationEnabled && distance < tessellationDistance;
        }
        BuildEdgeFlags(outPatches);
    }

    bool VansTerrainLodSelector::ValidateSelection(
        const std::vector<TerrainLodPatch>& patches,
        std::string* error) const
    {
        if (!m_Configured || patches.empty())
        {
            SetError(error, "CDLOD selection is empty or the selector is not configured.");
            return false;
        }

        uint64_t coveredCells = 0;
        for (const TerrainLodPatch& patch : patches)
        {
            if (patch.level > m_MaxLevel)
            {
                SetError(error, "CDLOD selection contains an invalid level.");
                return false;
            }
            const uint32_t size = PatchCellSize(patch);
            if ((patch.gridX & (size - 1u)) != 0 || (patch.gridZ & (size - 1u)) != 0 ||
                patch.gridX + size > m_RootCellCount || patch.gridZ + size > m_RootCellCount)
            {
                SetError(error, "CDLOD selection contains a misaligned patch.");
                return false;
            }
            coveredCells += static_cast<uint64_t>(size) * static_cast<uint64_t>(size);
        }

        const uint64_t expectedCells = static_cast<uint64_t>(m_RootCellCount) * m_RootCellCount;
        if (coveredCells != expectedCells)
        {
            SetError(error, "CDLOD selection does not cover the terrain exactly once.");
            return false;
        }

        const PatchLookup lookup = BuildLookup(patches);
        constexpr uint32_t edges[] = {
            TerrainEdge_Left, TerrainEdge_Right, TerrainEdge_Top, TerrainEdge_Bottom
        };
        for (const TerrainLodPatch& patch : patches)
        {
            for (uint32_t edge : edges)
            {
                const TerrainLodPatch* neighbor = FindNeighbor(patch, edge, patches, lookup);
                if (neighbor && std::abs(static_cast<int>(patch.level) - static_cast<int>(neighbor->level)) > 1)
                {
                    SetError(error, "CDLOD selection violates the 2:1 neighbor rule.");
                    return false;
                }
            }
        }

        if (error)
            error->clear();
        return true;
    }
}
