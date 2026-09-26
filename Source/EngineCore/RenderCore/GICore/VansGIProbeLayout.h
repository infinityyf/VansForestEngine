#pragma once

#include "VansGISettings.h"
#include "VansLayoutConstraints.h"
#include "../GeometryCore/VansSceneGeometrySnapshot.h"
#include <array>
#include <memory>

namespace VansGraphics
{
    constexpr uint32_t GIInvalidAddress = UINT32_MAX;
    constexpr uint32_t GIMaxLayoutDepth = 24u;
    constexpr uint32_t GILeafAddressBit = 0x80000000u;

    struct alignas(16) GIProbeLayoutRegion
    {
        glm::vec4 volumeMinAndRootSpacing{};
        glm::vec4 volumeSizeAndBias{};
        glm::uvec4 rootDimensionsAndOffset{};
        glm::uvec4 metadata{}; // 最大深度、作者 stableId、物理位置首址、物理位置数量。
    };
    struct alignas(16) GIProbeLayoutNode
    {
        // x：子节点首址，或带高位标记的叶地址；yzw：分割面的世界坐标位模式。
        // 直接比较上传的分割面，避免反复归一化造成边界错侧。
        glm::uvec4 addressAndSplit{GIInvalidAddress, 0u, 0u, 0u};
        glm::uvec4 lighting{GIInvalidAddress, GIInvalidAddress, 0u, 0u}; // 独立父层记录、父节点；命中叶时不读取。
        bool IsBranch() const { return (addressAndSplit.x & GILeafAddressBit) == 0u; }
        uint32_t LeafAddress() const
        { return IsBranch() || addressAndSplit.x == GIInvalidAddress ? GIInvalidAddress : addressAndSplit.x & ~GILeafAddressBit; }
        void SetLeaf(uint32_t leaf) { addressAndSplit.x = GILeafAddressBit | leaf; }
        void SetBranch(uint32_t firstChild, glm::vec3 split)
        { addressAndSplit = glm::uvec4(firstChild, glm::floatBitsToUint(split)); }
    };
    struct alignas(16) GIProbeLayoutCell
    {
        glm::vec4 minimumAndSpacing{};
        std::array<uint32_t, 8> probes{};
        glm::uvec4 metadata{}; // 区域、深度、粗邻居面位掩码、插值模板（无约束时为无效地址）。
    };
    struct alignas(16) GIProbeLayoutPosition
    {
        glm::vec4 positionAndSpacing{};
        glm::uvec4 metadata{}; // x：区域；y：局部可见性距离上限；z：独立父层源标记；w：父层深度。
    };
    struct alignas(16) GIProbeLayoutStencil
    {
        // 每行对应一个真实探针，每列对应原几何角点；模板可由不同叶共享。
        std::array<float, 64> weights{};
    };
    static_assert(sizeof(GIProbeLayoutRegion) == 64 && sizeof(GIProbeLayoutNode) == 32);
    static_assert(sizeof(GIProbeLayoutCell) == 64 && sizeof(GIProbeLayoutPosition) == 32);
    static_assert(sizeof(GIProbeLayoutStencil) == 256);

    struct GIProbeLayoutStats
    {
        uint64_t rootCellCount = 0, demandedRootCount = 0;
        uint64_t rejectedPositions = 0, acceptedSplits = 0, balancingSplits = 0;
        uint64_t budgetLimitedSplits = 0, unsupportedLeaves = 0, transitionFaces = 0;
        uint64_t constrainedPositions = 0;
        uint64_t parentCells = 0, parentPositions = 0, coverageSplits = 0;
        float finestSpacing = 0.0f, coarsestSpacing = 0.0f;
    };

    // 布局发布时复制一次的不可变位置数据，可跨线程读取，不持有 GPU 资源。
    struct GIProbeLayoutSnapshot
    {
        std::vector<GIProbeLayoutRegion> regions;
        std::vector<GIProbeLayoutPosition> positions;
    };

    // 几何派生布局，不持有 GPU、编辑器、Reflection 或保存文件。
    // 粗细边界约束在构建时消元；运行时仅对最多八个真实探针加权，不混合不同原点的距离矩。
    class VansGIProbeLayout
    {
    public:
        bool Build(const std::vector<GIResolvedRegion>& regions, const GIProbePlacementSettings& settings,
            const VansSceneGeometrySnapshot& geometry, const IVansLayoutConstraints* constraints,
            std::string& error);
        uint32_t LocateLeaf(uint32_t region, const glm::vec3& position, uint32_t* visitedNodes = nullptr) const;
        uint32_t LocateNode(uint32_t region, const glm::vec3& position, uint32_t* visitedNodes = nullptr) const;
        const std::vector<GIProbeLayoutRegion>& Regions() const { return m_Regions; }
        const std::vector<uint32_t>& Roots() const { return m_Roots; }
        const std::vector<GIProbeLayoutNode>& Nodes() const { return m_Nodes; }
        const std::vector<GIProbeLayoutCell>& Leaves() const { return m_Leaves; }
        const std::vector<GIProbeLayoutCell>& ParentCells() const { return m_ParentCells; }
        const std::vector<GIProbeLayoutPosition>& Positions() const { return m_Positions; }
        const std::vector<std::array<uint32_t, 6>>& CoarseNeighbors() const { return m_CoarseNeighbors; }
        const std::vector<GIProbeLayoutStencil>& Stencils() const { return m_Stencils; }
        const GIProbeLayoutStats& Stats() const { return m_Stats; }
        std::shared_ptr<const GIProbeLayoutSnapshot> CapturePositionSnapshot() const;
    private:
        std::vector<GIProbeLayoutRegion> m_Regions;
        std::vector<uint32_t> m_Roots;
        std::vector<GIProbeLayoutNode> m_Nodes;
        std::vector<GIProbeLayoutCell> m_Leaves;
        std::vector<GIProbeLayoutCell> m_ParentCells;
        std::vector<GIProbeLayoutPosition> m_Positions;
        std::vector<std::array<uint32_t, 6>> m_CoarseNeighbors;
        std::vector<GIProbeLayoutStencil> m_Stencils;
        GIProbeLayoutStats m_Stats;
    };

    // 单一只读 GPU 寻址数据；规则布局只携带区域记录，不复制规则网格的位置。
    std::vector<glm::uvec4> BuildGIProbeLayoutGPUData(
        const std::vector<GIResolvedRegion>& regions, const VansGIProbeLayout* layout);
}
