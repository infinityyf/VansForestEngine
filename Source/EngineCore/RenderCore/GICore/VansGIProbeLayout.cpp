#include "VansGIProbeLayout.h"

#include <glm/gtc/type_precision.hpp>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <cstring>
#include <functional>

namespace VansGraphics
{
    std::vector<glm::uvec4> BuildGIProbeLayoutGPUData(
        const std::vector<GIResolvedRegion>& regions, const VansGIProbeLayout* layout)
    {
        if (layout && layout->Regions().size() != regions.size())
            throw std::invalid_argument("GI layout region order does not match runtime regions");
        std::vector<glm::uvec4> data(4, glm::uvec4(0));
        auto append = [&](const void* source, size_t bytes)
        {
            const uint32_t offset = uint32_t(data.size());
            data.resize(data.size() + (bytes + 15u) / 16u, glm::uvec4(0));
            if (bytes) std::memcpy(data.data() + offset, source, bytes);
            return offset;
        };
        data[0] = {layout ? 1u : 0u, uint32_t(regions.size()), 4u, 0u};
        for (uint32_t i = 0; i < regions.size(); ++i)
        {
            const auto& source = regions[i];
            GIProbeLayoutRegion region;
            if (layout && !source.scrolling) region = layout->Regions()[i];
            else
            {
                region.volumeMinAndRootSpacing = glm::vec4(source.volumeMin, source.probeSpacing);
                region.volumeSizeAndBias = glm::vec4(source.volumeSize, source.normalBias);
                region.rootDimensionsAndOffset = glm::uvec4(source.gridDimensions, 0u);
                region.metadata = {0u, source.stableId, 0u, uint32_t(source.probeCount)};
            }
            region.volumeSizeAndBias.w = source.normalBias;
            append(&region, sizeof(region));
            const glm::vec4 trace(source.maxRayDistance, source.volumeFadeDistance, source.priority, source.worldOnly ? 1.0f : 0.0f);
            append(&trace, sizeof(trace));
        }
        if (layout)
        {
            const auto roots = append(layout->Roots().data(), layout->Roots().size() * sizeof(uint32_t));
            const auto nodes = append(layout->Nodes().data(), layout->Nodes().size() * sizeof(GIProbeLayoutNode));
            const auto leaves = append(layout->Leaves().data(), layout->Leaves().size() * sizeof(GIProbeLayoutCell));
            const auto positions = append(layout->Positions().data(), layout->Positions().size() * sizeof(GIProbeLayoutPosition));
            const auto stencils = append(layout->Stencils().data(), layout->Stencils().size() * sizeof(GIProbeLayoutStencil));
            data[0].w = roots;
            data[1] = {nodes, leaves, positions, stencils};
            data[2] = {uint32_t(layout->Roots().size()), uint32_t(layout->Nodes().size()),
                uint32_t(layout->Leaves().size()), uint32_t(layout->Positions().size())};
            data[3].x = uint32_t(layout->Stencils().size());
            data[3].y = append(layout->ParentCells().data(), layout->ParentCells().size() * sizeof(GIProbeLayoutCell));
            data[3].z = uint32_t(layout->ParentCells().size());
        }
        if (std::any_of(regions.begin(), regions.end(), [](const GIResolvedRegion& region) { return region.scrolling; }))
        {
            data[3].w = uint32_t(data.size());
            for (const auto& region : regions)
            {
                const glm::uvec4 address(region.scrollOffset, region.scrolling ? 1u : 0u);
                const glm::uvec4 center(glm::floatBitsToUint(region.blendCenter), region.scrollEpoch);
                append(&address, sizeof(address)); append(&center, sizeof(center));
            }
        }
        return data;
    }

    namespace
    {
        constexpr uint64_t MaxLayoutCells = 4u * 1024u * 1024u;
        using PositionKey = std::array<int64_t, 4>;
        using ParentPositionKey = std::array<int64_t, 5>; // 区域、层级、世界格点。父子状态隔离，同层共享角点。
        struct ParentPosition { uint32_t references = 0, address = GIInvalidAddress; };
        bool Finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
        glm::i64vec3 Corner(uint32_t index) { return {index & 1u, (index >> 1u) & 1u, (index >> 2u) & 1u}; }

        struct Cell
        {
            glm::i64vec3 minimum{};
            uint32_t region = 0, depth = 0, firstChild = GIInvalidAddress;
            bool supported = false, intersectsRegion = false;
            uint32_t parent = GIInvalidAddress;
            double area = 0.0, error = 0.0, score = 0.0;
        };
        struct PositionRecord
        {
            int64_t references = 0;
            uint32_t address = GIInvalidAddress;
            bool valid = false;
            bool physical = false;
        };
        struct Candidate
        {
            double score = 0;
            uint32_t cell = 0;
            bool operator<(const Candidate& other) const
            { return score < other.score || (score == other.score && cell > other.cell); }
        };
        struct Split
        {
            uint32_t parent = 0;
            std::array<Cell, 8> children;
        };
        struct CornerConstraint
        {
            glm::uvec4 metadata{};
            std::array<uint32_t, 8> sources{};
            std::array<float, 8> weights{};
        };
        struct ReferenceUpdate
        {
            PositionRecord* record;
            int64_t references;
            bool physical;
        };

        struct Builder
        {
            const std::vector<GIResolvedRegion>& descriptions;
            GIProbePlacementSettings settings;
            const VansSceneGeometrySnapshot& geometry;
            VansTriangleGeometryQuery transmission;
            std::vector<GIProbeLayoutRegion> regions;
            std::vector<uint32_t> roots;
            std::vector<Cell> cells;
            std::map<PositionKey, PositionRecord> positions;
            std::map<ParentPositionKey, ParentPosition> parentPositions;
            uint64_t parentPositionCount = 0;
            std::priority_queue<Candidate> candidates;
            GIProbeLayoutStats stats;
            int64_t referencedPositions = 0;
            double rootSpacing = 0.0;
            uint32_t maxDepth = 0;

            Builder(const std::vector<GIResolvedRegion>& source, GIProbePlacementSettings config,
                const VansSceneGeometrySnapshot& snapshot) : descriptions(source), settings(config), geometry(snapshot)
            {
                NormalizeGIProbePlacementSettings(settings);
                rootSpacing = settings.minProbeSpacing;
                while (rootSpacing * 2.0 <= settings.maxProbeSpacing && maxDepth < GIMaxLayoutDepth)
                { rootSpacing *= 2.0; ++maxDepth; }
                if (rootSpacing * 2.0 <= settings.maxProbeSpacing)
                    throw std::runtime_error("GI spacing range exceeds bounded layout depth");
                transmission.Build(geometry.transmissionReceivers);
            }
            int64_t Units(const Cell& cell) const { return int64_t(1) << (maxDepth - cell.depth); }
            double Spacing(const Cell& cell) const { return double(settings.minProbeSpacing) * double(Units(cell)); }
            glm::vec3 World(uint32_t region, glm::i64vec3 coordinate) const
            { return glm::vec3(glm::dvec3(descriptions[region].volumeMin) + glm::dvec3(coordinate) * double(settings.minProbeSpacing)); }
            PositionKey Key(const Cell& cell, uint32_t corner) const
            {
                const auto coordinate = cell.minimum + Corner(corner) * Units(cell);
                return {cell.region, coordinate.x, coordinate.y, coordinate.z};
            }
            PositionRecord& Position(const PositionKey& key)
            {
                auto [iterator, inserted] = positions.try_emplace(key);
                if (!inserted) return iterator->second;
                auto& value = iterator->second;
                const uint32_t region = uint32_t(key[0]);
                const auto position = World(region, {key[1], key[2], key[3]});
                const float reach = float(rootSpacing * 3.0 + descriptions[region].normalBias);
                const float clearance = (std::min)(0.02f, settings.minProbeSpacing * 0.04f);
                VansGeometryHit nearest;
                value.valid = true;
                if (geometry.opaque.NearestSurface(position, reach, nearest))
                {
                    if (nearest.distance < clearance) value.valid = false;
                    else if (nearest.backface && !nearest.twoSided)
                    {
                        value.valid = false;
                        for (int z = -1; z <= 1 && !value.valid; ++z)
                        for (int y = -1; y <= 1 && !value.valid; ++y)
                        for (int x = -1; x <= 1 && !value.valid; ++x)
                        {
                            if (!x && !y && !z) continue;
                            VansGeometryHit hit;
                            if (geometry.opaque.Raycast(position, {x,y,z}, reach, hit) && (!hit.backface || hit.twoSided))
                                value.valid = true;
                        }
                    }
                }
                if(value.valid&&descriptions[region].worldOnly&&geometry.additionalPositionValid)value.valid=geometry.additionalPositionValid(position,clearance);
                if (!value.valid) ++stats.rejectedPositions;
                return value;
            }
            Cell MakeCell(uint32_t region, glm::i64vec3 minimum, uint32_t depth) const
            {
                Cell cell; cell.region = region; cell.minimum = minimum; cell.depth = depth;
                const auto& description = descriptions[region];
                const float spacing = float(Spacing(cell));
                const auto lo = glm::max(World(region, minimum), description.volumeMin);
                const auto hi = glm::min(World(region, minimum + glm::i64vec3(Units(cell))), description.volumeMin + description.volumeSize);
                if (glm::any(glm::greaterThanEqual(lo, hi))) return cell;
                cell.intersectsRegion = true;
                // 支持区只扩展实际配置的接收点偏移。
                const float bias = (std::max)(description.normalBias, 0.0f);
                const glm::vec3 expandedLo = lo - bias, expandedHi = hi + bias;
                cell.supported = geometry.opaque.IntersectsBox(expandedLo, expandedHi) || transmission.IntersectsBox(expandedLo, expandedHi);
                for (const auto& receiver : geometry.dynamicReceivers)
                    if (!cell.supported) cell.supported = glm::all(glm::lessThanEqual(receiver.minimum, expandedHi))
                        && glm::all(glm::greaterThanEqual(receiver.maximum, expandedLo));
                VansGeometrySurfaceMeasure fieldMeasure;
                if(description.worldOnly&&geometry.additionalSurface){fieldMeasure=geometry.additionalSurface(expandedLo,expandedHi);cell.supported=cell.supported||fieldMeasure.area>0;}
                if (!cell.supported) return cell;
                auto measure = geometry.opaque.MeasureSurface(lo, hi);
                const auto transparentMeasure = transmission.MeasureSurface(lo, hi);
                measure.area += transparentMeasure.area; measure.areaNormal += transparentMeasure.areaNormal;
                measure.area+=fieldMeasure.area;measure.areaNormal+=fieldMeasure.areaNormal;
                cell.area = measure.area;
                const double coherence = measure.area > 1e-12 ? glm::length(measure.areaNormal) / measure.area : 1.0;
                cell.error = spacing * (1.0 - std::clamp(coherence, 0.0, 1.0));
                cell.score = cell.area * cell.error * std::exp2(std::clamp(double(description.priority), -16.0, 16.0));
                return cell;
            }
            void Enqueue(uint32_t index)
            {
                const auto& cell = cells[index];
                if (cell.supported && cell.depth < maxDepth && cell.error > double(settings.minProbeSpacing) * 0.25)
                    candidates.push({cell.score, index});
            }
            uint32_t Find(uint32_t region, glm::dvec3 coordinate) const
            {
                const auto& data = regions[region];
                const glm::dvec3 rootCoordinate = glm::floor(coordinate / double(int64_t(1) << maxDepth));
                const glm::uvec3 dimensions(data.rootDimensionsAndOffset);
                if (glm::any(glm::lessThan(rootCoordinate, glm::dvec3(0))) ||
                    glm::any(glm::greaterThanEqual(rootCoordinate, glm::dvec3(dimensions)))) return GIInvalidAddress;
                const glm::uvec3 root(rootCoordinate);
                uint32_t index = roots[data.rootDimensionsAndOffset.w + (root.z * dimensions.y + root.y) * dimensions.x + root.x];
                for (uint32_t level = 0; index != GIInvalidAddress && level <= maxDepth; ++level)
                {
                    const auto& cell = cells[index];
                    if (!cell.supported) return GIInvalidAddress;
                    if (cell.firstChild == GIInvalidAddress) return index;
                    const auto upper = glm::greaterThanEqual(coordinate, glm::dvec3(cell.minimum) + double(Units(cell)) * 0.5);
                    index = cell.firstChild + uint32_t(upper.x) + 2u * uint32_t(upper.y) + 4u * uint32_t(upper.z);
                }
                return GIInvalidAddress;
            }
            uint32_t Neighbor(const Cell& cell, glm::ivec3 direction) const
            {
                auto point = glm::dvec3(cell.minimum) + double(Units(cell)) * 0.5;
                for (int axis = 0; axis < 3; ++axis)
                    if (direction[axis]) point[axis] = double(cell.minimum[axis]) +
                        (direction[axis] > 0 ? double(Units(cell)) + 0.25 : -0.25);
                return Find(cell.region, point);
            }
            void AddReferences(const Cell& cell, int64_t change, std::map<PositionKey, int64_t>& delta)
            {
                if (!cell.supported) return;
                for (uint32_t corner = 0; corner < 8; ++corner)
                {
                    const auto key = Key(cell, corner);
                    Position(key); delta[key] += change;
                }
            }
            bool IsHanging(const PositionKey& key) const
            {
                const glm::i64vec3 point(key[1], key[2], key[3]);
                for (uint32_t octant = 0; octant < 8u; ++octant)
                {
                    const auto nearby = glm::dvec3(point) + (glm::dvec3(Corner(octant)) * 2.0 - 1.0) * 0.25;
                    const uint32_t neighbor = Find(uint32_t(key[0]), nearby);
                    if (neighbor == GIInvalidAddress) continue;
                    const auto& cell = cells[neighbor]; const auto upper = cell.minimum + glm::i64vec3(Units(cell));
                    for (int axis = 0; axis < 3; ++axis)
                        if (point[axis] != cell.minimum[axis] && point[axis] != upper[axis]) return true;
                }
                return false;
            }
            int64_t EvaluateReferences(const std::map<PositionKey, int64_t>& delta, std::vector<ReferenceUpdate>& updates)
            {
                int64_t count = referencedPositions;
                updates.clear(); updates.reserve(delta.size());
                for (const auto& [key, change] : delta)
                {
                    auto& record = positions.at(key); const auto references = record.references;
                    if (references + change < 0) throw std::logic_error("GI corner reference underflow");
                    const bool physical = record.valid && references + change > 0 && !IsHanging(key);
                    count += int64_t(physical) - int64_t(record.physical);
                    updates.push_back({&record, references + change, physical});
                }
                return count;
            }
            void CommitReferences(const std::vector<ReferenceUpdate>& updates, int64_t count)
            {
                referencedPositions = count;
                for (const auto& update : updates)
                {
                    update.record->references = update.references;
                    update.record->physical = update.physical;
                }
            }
            ParentPositionKey ParentKey(const Cell& cell, uint32_t corner) const
            {
                const auto key = Key(cell, corner);
                return {key[0], cell.depth, key[1], key[2], key[3]};
            }
            void AddParentReferences(const Cell& cell, std::map<ParentPositionKey, uint32_t>& delta)
            {
                if (!cell.intersectsRegion || Spacing(cell) > settings.parentProbeMaxSize) return;
                for (uint32_t corner = 0; corner < 8; ++corner)
                    if (Position(Key(cell, corner)).valid) ++delta[ParentKey(cell, corner)];
            }
            uint64_t CountParentReferences(const std::map<ParentPositionKey, uint32_t>& delta)
            {
                uint64_t count = parentPositionCount;
                for (const auto& [key, references] : delta)
                    if (references && parentPositions[key].references == 0u) ++count;
                return count;
            }
            void CommitParentReferences(const std::map<ParentPositionKey, uint32_t>& delta, uint64_t count)
            {
                for (const auto& [key, references] : delta) parentPositions[key].references += references;
                parentPositionCount = count;
            }
            // 无接收需求的空间只细分到配置允许的最粗覆盖层，不参与原几何误差或边界约束。
            void PopulateEmptyCoverage(uint32_t index, std::map<ParentPositionKey, uint32_t>& delta)
            {
                const Cell cell = cells[index];
                if (cell.supported || !cell.intersectsRegion) return;
                if (Spacing(cell) <= settings.parentProbeMaxSize)
                { AddParentReferences(cell, delta); return; }
                if (cell.depth >= maxDepth || cells.size() + 8u > MaxLayoutCells)
                    throw std::runtime_error("GI parent coverage exceeds bounded layout capacity");
                const uint32_t first = uint32_t(cells.size());
                cells[index].firstChild = first;
                for (uint32_t corner = 0; corner < 8; ++corner)
                {
                    auto child = MakeCell(cell.region, cell.minimum + Corner(corner) * (Units(cell) / 2), cell.depth + 1);
                    child.parent = index;
                    cells.push_back(child);
                }
                ++stats.coverageSplits;
                for (uint32_t corner = 0; corner < 8; ++corner) PopulateEmptyCoverage(first + corner, delta);
            }
            void BuildRoots()
            {
                regions.resize(descriptions.size());
                for (uint32_t region = 0; region < descriptions.size(); ++region)
                {
                    const auto& source = descriptions[region]; auto& destination = regions[region];
                    if (!Finite(source.volumeMin) || !Finite(source.volumeSize) || !std::isfinite(source.normalBias) ||
                        !std::isfinite(source.priority) || glm::any(glm::lessThanEqual(source.volumeSize, glm::vec3(0))))
                        throw std::runtime_error("GI layout region requires finite positive bounds");
                    destination.volumeMinAndRootSpacing = glm::vec4(source.volumeMin, float(rootSpacing));
                    destination.volumeSizeAndBias = glm::vec4(source.volumeSize, source.normalBias);
                    destination.metadata = {maxDepth, source.stableId, 0, 0};
                    if (!source.enabled) continue;
                    for (int axis = 0; axis < 3; ++axis)
                        if (float(double(source.volumeMin[axis]) + settings.minProbeSpacing) <= source.volumeMin[axis])
                            throw std::runtime_error("GI minimum spacing is below float precision at the region origin");
                    const auto size = glm::ceil(glm::dvec3(source.volumeSize) / rootSpacing);
                    const double count = size.x * size.y * size.z;
                    if (count + roots.size() > MaxLayoutCells) throw std::runtime_error("GI root lookup exceeds spatial table budget");
                    const glm::uvec3 dimensions(size);
                    destination.rootDimensionsAndOffset = glm::uvec4(dimensions, uint32_t(roots.size()));
                    roots.resize(roots.size() + size_t(count), GIInvalidAddress);
                    stats.rootCellCount += uint64_t(count);
                    for (uint32_t z = 0; z < dimensions.z; ++z)
                    for (uint32_t y = 0; y < dimensions.y; ++y)
                    for (uint32_t x = 0; x < dimensions.x; ++x)
                    {
                        Cell cell = MakeCell(region, glm::i64vec3(x,y,z) * (int64_t(1) << maxDepth), 0);
                        std::map<PositionKey, int64_t> delta; AddReferences(cell, 1, delta);
                        std::vector<ReferenceUpdate> updates;
                        const auto countAfter = EvaluateReferences(delta, updates);
                        if (cells.size() >= MaxLayoutCells) throw std::runtime_error("GI root nodes exceed spatial table budget");
                        const uint32_t index = uint32_t(cells.size());
                        roots[destination.rootDimensionsAndOffset.w + (z * dimensions.y + y) * dimensions.x + x] = index;
                        cells.push_back(cell);
                        std::map<ParentPositionKey, uint32_t> parentDelta;
                        PopulateEmptyCoverage(index, parentDelta);
                        const auto parentCountAfter = CountParentReferences(parentDelta);
                        if (uint64_t(countAfter) + parentCountAfter > settings.maxProbeCount)
                            throw std::runtime_error("GI coarse and parent coverage exceeds probe budget; no partial layout was published");
                        CommitReferences(updates, countAfter);
                        CommitParentReferences(parentDelta, parentCountAfter);
                        Enqueue(index);
                        stats.demandedRootCount += cell.supported;
                    }
                }
            }
            void Refine()
            {
                while (!candidates.empty())
                {
                    const uint32_t selected = candidates.top().cell; candidates.pop();
                    if (cells[selected].firstChild != GIInvalidAddress) continue;
                    // 面、边、角邻居的平衡细分均与本次细分作为一个事务计费。
                    std::set<uint32_t> required{selected}; std::vector<uint32_t> pending{selected};
                    for (size_t cursor = 0; cursor < pending.size(); ++cursor)
                    {
                        const auto& cell = cells[pending[cursor]];
                        for (int z = -1; z <= 1; ++z)
                        for (int y = -1; y <= 1; ++y)
                        for (int x = -1; x <= 1; ++x)
                        {
                            if (!x && !y && !z) continue;
                            const glm::ivec3 direction(x,y,z);
                            const uint32_t neighbor = Neighbor(cell, direction);
                            if (neighbor != GIInvalidAddress && cells[neighbor].depth < cell.depth && required.insert(neighbor).second)
                                pending.push_back(neighbor);
                        }
                    }
                    if (cells.size() + required.size() * 8u > MaxLayoutCells)
                        throw std::runtime_error("GI refinement exceeds spatial table budget");
                    std::vector<Split> splits;
                    std::map<ParentPositionKey, uint32_t> parentDelta;
                    std::map<PositionKey, int64_t> delta;
                    for (uint32_t parent : required)
                    {
                        const auto& cell = cells[parent]; Split split; split.parent = parent;
                        AddReferences(cell, -1, delta);
                        AddParentReferences(cell, parentDelta);
                        for (uint32_t child = 0; child < 8; ++child)
                        {
                            split.children[child] = MakeCell(cell.region, cell.minimum + Corner(child) * (Units(cell) / 2), cell.depth + 1);
                            split.children[child].parent = parent;
                            AddReferences(split.children[child], 1, delta);
                            // 不受光的子格也可能解除邻格角点的粗层约束，必须重新计费这些已有位置。
                            for (uint32_t corner = 0; corner < 8u; ++corner)
                            {
                                const auto key = Key(split.children[child], corner);
                                if (positions.find(key) != positions.end()) delta.try_emplace(key, 0);
                            }
                        }
                        splits.push_back(std::move(split));
                    }
                    // 在局部构建树上暂存完整平衡事务，按消元后的独立有效位置计费；不估算虚拟角点开销。
                    const size_t previousCellCount = cells.size();
                    const auto previousCoverageSplits = stats.coverageSplits;
                    for (const auto& split : splits)
                    {
                        cells[split.parent].firstChild = uint32_t(cells.size());
                        for (const auto& child : split.children) cells.push_back(child);
                    }
                    const size_t splitChildrenEnd = cells.size();
                    for (size_t child = previousCellCount; child < splitChildrenEnd; ++child)
                        PopulateEmptyCoverage(uint32_t(child), parentDelta);
                    std::vector<ReferenceUpdate> updates;
                    const auto countAfter = EvaluateReferences(delta, updates);
                    const auto parentCountAfter = CountParentReferences(parentDelta);
                    if (uint64_t(countAfter) + parentCountAfter > settings.maxProbeCount)
                    {
                        for (const auto& split : splits) cells[split.parent].firstChild = GIInvalidAddress;
                        cells.resize(previousCellCount); stats.coverageSplits = previousCoverageSplits;
                        ++stats.budgetLimitedSplits; continue;
                    }
                    CommitReferences(updates, countAfter);
                    CommitParentReferences(parentDelta, parentCountAfter);
                    for (size_t child = previousCellCount; child < cells.size(); ++child) Enqueue(uint32_t(child));
                    stats.acceptedSplits += required.size(); stats.balancingSplits += required.size() - 1;
                }
            }
        };
    }

    bool VansGIProbeLayout::Build(const std::vector<GIResolvedRegion>& descriptions,
        const GIProbePlacementSettings& settings, const VansSceneGeometrySnapshot& geometry, std::string& error)
    {
        error.clear();
        try
        {
            if (!settings.enabled) throw std::runtime_error("Automatic GI layout generation is disabled");
            Builder builder(descriptions, settings, geometry);
            builder.BuildRoots(); builder.Refine();
            VansGIProbeLayout result;
            result.m_Regions = builder.regions; result.m_Roots = builder.roots;
            result.m_Nodes.resize(builder.cells.size());
            std::vector<bool> validPositions;
            for (auto& [key, record] : builder.positions)
            {
                if (record.references == 0) continue;
                record.address = uint32_t(result.m_Positions.size());
                validPositions.push_back(record.valid);
                result.m_Positions.push_back({glm::vec4(builder.World(uint32_t(key[0]), {key[1],key[2],key[3]}), float(builder.rootSpacing)),
                    {uint32_t(key[0]), 0u, 0u, 0u}});
            }
            for (uint32_t index = 0; index < builder.cells.size(); ++index)
            {
                const auto& cell = builder.cells[index]; auto& node = result.m_Nodes[index];
                node.lighting.y = cell.parent;
                if (cell.firstChild != GIInvalidAddress)
                    node.SetBranch(cell.firstChild, builder.World(cell.region, cell.minimum + glm::i64vec3(builder.Units(cell) / 2)));
                if (!cell.supported || cell.firstChild != GIInvalidAddress) continue;
                GIProbeLayoutCell leaf;
                const float spacing = float(builder.Spacing(cell));
                leaf.minimumAndSpacing = glm::vec4(builder.World(cell.region, cell.minimum), spacing);
                leaf.metadata = {cell.region, cell.depth, 0u, GIInvalidAddress};
                uint32_t supportedCorners = 0;
                for (uint32_t corner = 0; corner < 8; ++corner)
                {
                    const auto& position = builder.positions.at(builder.Key(cell, corner));
                    leaf.probes[corner] = position.address;
                    if (!position.valid) continue;
                    auto& physicalSpacing = result.m_Positions[position.address].positionAndSpacing.w;
                    physicalSpacing = (std::min)(physicalSpacing, spacing); ++supportedCorners;
                }
                if (supportedCorners == 0) ++builder.stats.unsupportedLeaves;
                node.SetLeaf(uint32_t(result.m_Leaves.size()));
                result.m_Leaves.push_back(leaf);
                builder.stats.finestSpacing = builder.stats.finestSpacing == 0.0f ? spacing : (std::min)(builder.stats.finestSpacing, spacing);
                builder.stats.coarsestSpacing = (std::max)(builder.stats.coarsestSpacing, spacing);
            }
            result.m_CoarseNeighbors.resize(result.m_Leaves.size());
            for (auto& neighbors : result.m_CoarseNeighbors) neighbors.fill(GIInvalidAddress);
            std::map<uint32_t, CornerConstraint> constraints;
            for (uint32_t index = 0; index < builder.cells.size(); ++index)
            {
                const auto& cell = builder.cells[index]; const uint32_t leaf = result.m_Nodes[index].LeafAddress();
                if (leaf == GIInvalidAddress) continue;
                for (int z = -1; z <= 1; ++z)
                for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                {
                    if (!x && !y && !z) continue;
                    const glm::ivec3 direction(x,y,z);
                    const uint32_t neighbor = builder.Neighbor(cell, direction);
                    if (neighbor == GIInvalidAddress || builder.cells[neighbor].depth >= cell.depth) continue;
                    if (std::abs(x) + std::abs(y) + std::abs(z) == 1)
                    {
                        const uint32_t axis = x ? 0u : (y ? 1u : 2u);
                        const uint32_t face = axis * 2u + uint32_t(direction[axis] > 0);
                        result.m_CoarseNeighbors[leaf][face] = result.m_Nodes[neighbor].LeafAddress();
                        result.m_Leaves[leaf].metadata.z |= 1u << face;
                        ++builder.stats.transitionFaces;
                    }
                    const auto& coarse = builder.cells[neighbor];
                    for (uint32_t corner = 0; corner < 8; ++corner)
                    {
                        bool touching = true;
                        for (uint32_t axis = 0; axis < 3; ++axis)
                            if (direction[axis]) touching &= ((corner >> axis) & 1u) == uint32_t(direction[axis] > 0);
                        if (!touching) continue;
                        const uint32_t destination = result.m_Leaves[leaf].probes[corner];
                        if (destination == GIInvalidAddress) continue;
                        const auto coordinate = cell.minimum + Corner(corner) * builder.Units(cell);
                        const glm::dvec3 fraction = glm::dvec3(coordinate - coarse.minimum) / double(builder.Units(coarse));
                        CornerConstraint constraint;
                        constraint.metadata = {destination, coarse.depth, result.m_Nodes[neighbor].LeafAddress(), cell.region};
                        bool identity = false;
                        for (uint32_t source = 0; source < 8; ++source)
                        {
                            const auto blend = glm::mix(1.0 - fraction, fraction, glm::dvec3(Corner(source)));
                            const float weight = float(blend.x * blend.y * blend.z);
                            constraint.weights[source] = weight;
                            constraint.sources[source] = result.m_Leaves[constraint.metadata.z].probes[source];
                            identity |= weight == 1.0f && constraint.sources[source] == destination;
                        }
                        if (identity) continue;
                        auto existing = constraints.find(destination);
                        // 边/角可能被多个粗面约束；选择最粗且确定的同一来源，保持唯一写入者。
                        if (existing == constraints.end() || constraint.metadata.y < existing->second.metadata.y ||
                            (constraint.metadata.y == existing->second.metadata.y && constraint.metadata.z < existing->second.metadata.z))
                            constraints[destination] = constraint;
                    }
                }
            }
            // 先消去全部悬挂角点，再编译叶的查询模板。无效的几何角点也参与约束，
            // 但不作为终端采光位置，避免细侧提前丢失粗侧仍有效的插值支持。
            std::vector<std::map<uint32_t, float>> terminalWeights(result.m_Positions.size());
            std::vector<uint8_t> resolved(result.m_Positions.size(), 0u);
            std::function<void(uint32_t)> resolve = [&](uint32_t address)
            {
                if (resolved[address] == 2u) return;
                if (resolved[address] == 1u) throw std::logic_error("GI corner constraints contain a cycle");
                resolved[address] = 1u;
                const auto constraint = constraints.find(address);
                if (constraint == constraints.end())
                {
                    if (validPositions[address]) terminalWeights[address][address] = 1.0f;
                }
                else for (uint32_t corner = 0; corner < 8u; ++corner)
                {
                    const float weight = constraint->second.weights[corner];
                    if (weight <= 0.0f) continue;
                    const uint32_t source = constraint->second.sources[corner];
                    resolve(source);
                    for (const auto& [terminal, value] : terminalWeights[source])
                        terminalWeights[address][terminal] += value * weight;
                }
                resolved[address] = 2u;
            };
            for (uint32_t address = 0; address < resolved.size(); ++address) resolve(address);
            std::map<std::array<float, 64>, uint32_t> sharedStencils;
            std::vector<bool> usedPositions(result.m_Positions.size(), false);
            builder.stats.unsupportedLeaves = 0;
            for (auto& leaf : result.m_Leaves)
            {
                const auto corners = leaf.probes;
                bool constrained = false;
                std::set<uint32_t> sources;
                for (uint32_t corner : corners)
                {
                    constrained |= constraints.count(corner) != 0;
                    for (const auto& [terminal, weight] : terminalWeights[corner]) sources.insert(terminal);
                }
                if (sources.size() > 8u)
                    throw std::runtime_error("GI boundary stencil exceeds eight physical probes: sources=" + std::to_string(sources.size()) +
                        " region=" + std::to_string(leaf.metadata.x) + " depth=" + std::to_string(leaf.metadata.y) +
                        " minimum=" + std::to_string(leaf.minimumAndSpacing.x) + "," + std::to_string(leaf.minimumAndSpacing.y) + "," +
                        std::to_string(leaf.minimumAndSpacing.z));
                if (constrained)
                {
                    GIProbeLayoutStencil stencil;
                    leaf.probes.fill(GIInvalidAddress);
                    uint32_t slot = 0;
                    for (uint32_t source : sources)
                    {
                        leaf.probes[slot] = source;
                        for (uint32_t corner = 0; corner < 8u; ++corner)
                        {
                            const auto weight = terminalWeights[corners[corner]].find(source);
                            if (weight != terminalWeights[corners[corner]].end()) stencil.weights[slot * 8u + corner] = weight->second;
                        }
                        ++slot;
                    }
                    const auto [entry, inserted] = sharedStencils.emplace(stencil.weights, uint32_t(result.m_Stencils.size()));
                    if (inserted) result.m_Stencils.push_back(stencil);
                    leaf.metadata.w = entry->second;
                }
                else for (auto& corner : leaf.probes) if (!validPositions[corner]) corner = GIInvalidAddress;
                if (sources.empty()) ++builder.stats.unsupportedLeaves;
                for (uint32_t source : leaf.probes) if (source != GIInvalidAddress) usedPositions[source] = true;
            }
            // 图集与调度只保留真实查询源；虚拟边界角点不占物理位置、射线或纹理更新。
            std::vector<uint32_t> addresses(result.m_Positions.size(), GIInvalidAddress);
            uint32_t retained = 0;
            for (uint32_t address = 0; address < result.m_Positions.size(); ++address)
                if (usedPositions[address])
                {
                    addresses[address] = retained;
                    result.m_Positions[retained++] = result.m_Positions[address];
                }
            result.m_Positions.resize(retained);
            if (int64_t(retained) != builder.referencedPositions)
                throw std::logic_error("GI physical budget accounting differs from compiled sources: counted=" +
                    std::to_string(builder.referencedPositions) + " compiled=" + std::to_string(retained));
            for (auto& leaf : result.m_Leaves) for (auto& address : leaf.probes)
                if (address != GIInvalidAddress) address = addresses[address];
            // 父层仅引用自己的真实射线位置，绝不执行叶角点的虚拟插值消元。
            for (auto& [key, position] : builder.parentPositions)
            {
                if (!position.references) continue;
                position.address = uint32_t(result.m_Positions.size());
                const float spacing = float(builder.rootSpacing / std::exp2(double(key[1])));
                result.m_Positions.push_back({glm::vec4(builder.World(uint32_t(key[0]), {key[2], key[3], key[4]}), spacing),
                    {uint32_t(key[0]), 0u, 1u, uint32_t(key[1])}});
            }
            for (uint32_t index = 0; index < builder.cells.size(); ++index)
            {
                const auto& cell = builder.cells[index];
                if (!cell.intersectsRegion || builder.Spacing(cell) > builder.settings.parentProbeMaxSize ||
                    (cell.supported && cell.firstChild == GIInvalidAddress)) continue;
                GIProbeLayoutCell record;
                record.minimumAndSpacing = glm::vec4(builder.World(cell.region, cell.minimum), float(builder.Spacing(cell)));
                record.metadata = {cell.region, cell.depth, 0u, GIInvalidAddress};
                record.probes.fill(GIInvalidAddress);
                for (uint32_t corner = 0; corner < 8; ++corner)
                {
                    const auto found = builder.parentPositions.find(builder.ParentKey(cell, corner));
                    if (found != builder.parentPositions.end() && found->second.references)
                        record.probes[corner] = found->second.address;
                }
                result.m_Nodes[index].lighting.x = uint32_t(result.m_ParentCells.size());
                result.m_ParentCells.push_back(record);
            }
            builder.stats.parentPositions = builder.parentPositionCount;
            builder.stats.parentCells = result.m_ParentCells.size();
            // 最终物理地址按区域稳定分组；每区先原叶源、再父层源，统一图集与调度。
            std::vector<uint32_t> remap(result.m_Positions.size());
            std::vector<GIProbeLayoutPosition> ordered;
            ordered.reserve(result.m_Positions.size());
            for (uint32_t region = 0; region < result.m_Regions.size(); ++region)
                for (uint32_t address = 0; address < result.m_Positions.size(); ++address)
                    if (result.m_Positions[address].metadata.x == region)
                    { remap[address] = uint32_t(ordered.size()); ordered.push_back(result.m_Positions[address]); }
            result.m_Positions = std::move(ordered);
            for (auto* records : {&result.m_Leaves, &result.m_ParentCells})
                for (auto& record : *records) for (auto& address : record.probes)
                    if (address != GIInvalidAddress) address = remap[address];
            if (result.m_Positions.size() != uint64_t(builder.referencedPositions) + builder.parentPositionCount ||
                result.m_Positions.size() > builder.settings.maxProbeCount)
                throw std::logic_error("GI parent physical budget differs from published sources");
            // 从最终 stencil 的真实查询源推导局部距离范围，不能使用最小 cell 代替跨层覆盖。
            std::vector<float> visibilityRanges(result.m_Positions.size(), 0.0f);
            for (const auto* records : {&result.m_Leaves, &result.m_ParentCells})
            for (const auto& leaf : *records)
            {
                const glm::vec3 lower(leaf.minimumAndSpacing);
                const glm::vec3 upper = lower + leaf.minimumAndSpacing.w;
                for (uint32_t address : leaf.probes)
                {
                    if (address == GIInvalidAddress) continue;
                    const auto& position = result.m_Positions[address].positionAndSpacing;
                    const auto farthest = glm::max(glm::abs(lower - glm::vec3(position)), glm::abs(upper - glm::vec3(position)));
                    // 包含允许的 0.45 cell 原点位移；该上限只影响距离矩，不缩短光照 trace。
                    const float reach = glm::length(farthest) + position.w * 0.45f;
                    visibilityRanges[address] = (std::max)(visibilityRanges[address], reach);
                }
            }
            for (uint32_t address = 0; address < result.m_Positions.size(); ++address)
                result.m_Positions[address].metadata.y = glm::floatBitsToUint(visibilityRanges[address]);
            builder.stats.constrainedPositions = constraints.size();
            uint32_t physicalBase = 0;
            for (uint32_t region = 0; region < result.m_Regions.size(); ++region)
            {
                auto& metadata = result.m_Regions[region].metadata;
                metadata.z = physicalBase;
                while (physicalBase < result.m_Positions.size() &&
                    result.m_Positions[physicalBase].metadata.x == region) ++physicalBase;
                metadata.w = physicalBase - metadata.z;
            }
            result.m_Stats = builder.stats;
            *this = std::move(result);
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }

    std::shared_ptr<const GIProbeLayoutSnapshot> VansGIProbeLayout::CapturePositionSnapshot() const
    {
        auto snapshot = std::make_shared<GIProbeLayoutSnapshot>();
        snapshot->regions = m_Regions;
        snapshot->positions = m_Positions;
        return snapshot;
    }

    uint32_t VansGIProbeLayout::LocateLeaf(uint32_t regionIndex, const glm::vec3& position, uint32_t* visitedNodes) const
    {
        const uint32_t node = LocateNode(regionIndex, position, visitedNodes);
        return node == GIInvalidAddress ? GIInvalidAddress : m_Nodes[node].LeafAddress();
    }

    uint32_t VansGIProbeLayout::LocateNode(uint32_t regionIndex, const glm::vec3& position, uint32_t* visitedNodes) const
    {
        if (visitedNodes) *visitedNodes = 0;
        if (regionIndex >= m_Regions.size() || !Finite(position)) return GIInvalidAddress;
        const auto& region = m_Regions[regionIndex]; const glm::vec3 minimum(region.volumeMinAndRootSpacing);
        const glm::vec3 size(region.volumeSizeAndBias); const glm::uvec3 dimensions(region.rootDimensionsAndOffset);
        if (glm::any(glm::equal(dimensions, glm::uvec3(0))) || glm::any(glm::lessThan(position, minimum)) ||
            glm::any(glm::greaterThan(position, minimum + size))) return GIInvalidAddress;
        const glm::vec3 coordinate = (position - minimum) / region.volumeMinAndRootSpacing.w;
        glm::uvec3 root = glm::min(glm::uvec3(coordinate), dimensions - 1u);
        // 归一化只给出根单元候选；用一次舍入的世界平面修正相邻单元误差。
        for (int axis = 0; axis < 3; ++axis)
        {
            const auto plane = [&](uint32_t index) { return std::fma(float(index), region.volumeMinAndRootSpacing.w, minimum[axis]); };
            if (root[axis] > 0u && position[axis] < plane(root[axis])) --root[axis];
            if (root[axis] + 1u < dimensions[axis] && position[axis] >= plane(root[axis] + 1u)) ++root[axis];
        }
        uint32_t node = m_Roots[region.rootDimensionsAndOffset.w + (root.z * dimensions.y + root.y) * dimensions.x + root.x];
        for (uint32_t depth = 0; node != GIInvalidAddress && depth <= region.metadata.x && depth <= GIMaxLayoutDepth; ++depth)
        {
            if (visitedNodes) ++*visitedNodes;
            const auto& entry = m_Nodes[node];
            if (!entry.IsBranch()) return node;
            const auto upper = glm::greaterThanEqual(position, glm::uintBitsToFloat(glm::uvec3(entry.addressAndSplit.y,
                entry.addressAndSplit.z, entry.addressAndSplit.w)));
            node = entry.addressAndSplit.x + uint32_t(upper.x) + 2u * uint32_t(upper.y) + 4u * uint32_t(upper.z);
        }
        return GIInvalidAddress;
    }
}
