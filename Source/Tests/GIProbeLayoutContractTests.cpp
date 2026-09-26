#include "../EngineCore/RenderCore/GICore/VansGIProbeLayout.h"
#include "../EngineCore/RenderCore/GICore/VansGIScrollingGrid.h"
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <cstring>
#include <limits>

namespace
{
    using namespace VansGraphics;
    void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    void BuildQuery(VansTriangleGeometryQuery& query, std::vector<VansGeometryTriangle> triangles)
    {
        std::string error;
        Check(query.Build(std::move(triangles), error), error.c_str());
    }
    void ValidateScrollingGrid()
    {
        VansGIScrollingGrid grid;
        std::string error;
        Check(grid.Initialize({9,10,11}, 0.75f, {-0.01f,-10.0f,9.0f}, error), error.c_str());
        std::mt19937 random(7319);
        std::uniform_real_distribution<float> step(-2.0f,2.0f);
        uint64_t checked = 0;
        for (uint32_t iteration = 0; iteration < 150; ++iteration)
        {
            std::vector<glm::ivec3> previous(grid.ProbeCount());
            for (uint32_t id = 0; id < previous.size(); ++id) previous[id] = grid.WorldCell(id);
            const auto oldOrigin = grid.Origin();
            const auto next = iteration % 19u == 0u ? grid.Center() + glm::vec3(100,-80,90) :
                grid.Center() + glm::vec3(step(random), step(random), step(random));
            std::vector<uint32_t> entering;
            Check(grid.Move(next, entering, error), error.c_str());
            const std::set<uint32_t> changed(entering.begin(), entering.end());
            Check(changed.size() == entering.size() && std::is_sorted(entering.begin(), entering.end()), "scroll entering planes overlap");
            std::set<uint32_t> addresses;
            const auto dimensions = glm::ivec3(grid.Dimensions());
            for (int z = 0; z < dimensions.z; ++z) for (int y = 0; y < dimensions.y; ++y) for (int x = 0; x < dimensions.x; ++x)
            {
                const auto world = grid.Origin() + glm::ivec3(x,y,z);
                const uint32_t id = grid.PhysicalIndex(world);
                Check(id < grid.ProbeCount() && addresses.insert(id).second && grid.WorldCell(id) == world,
                    "scrolling world/physical mapping is not bijective");
                const bool reused = glm::all(glm::greaterThanEqual(world, oldOrigin)) && glm::all(glm::lessThan(world, oldOrigin + dimensions));
                Check((changed.count(id) == 0) == reused && (!reused || previous[id] == world),
                    "scroll reused stale history or cleared an overlapping world position");
                ++checked;
            }
            Check(glm::all(glm::greaterThanEqual(grid.BlendMinimum(), grid.Minimum() + 0.5f * grid.Spacing())) &&
                glm::all(glm::lessThanEqual(grid.BlendMinimum() + grid.BlendSize(), grid.Minimum() + (glm::vec3(grid.Dimensions()) - 0.5f) * grid.Spacing())),
                "continuous blend envelope escapes actual probe support");
        }
        std::vector<uint32_t> entering;
        Check(grid.Move({0.7499f,0,0}, entering, error), error.c_str());
        const auto before = grid.BlendMinimum();
        Check(grid.Move({0.7501f,0,0}, entering, error), error.c_str());
        Check(entering.size() == 110 && std::abs((grid.BlendMinimum().x-before.x)-0.0002f) < 1e-6f,
            "single-plane scroll snapped the blend envelope or reset full volume");
        const auto savedOrigin = grid.Origin(); const auto savedCenter = grid.Center(); const auto savedEntering = entering;
        Check(!grid.Move({NAN,0,0}, entering, error) && grid.Origin() == savedOrigin && grid.Center() == savedCenter && entering == savedEntering,
            "invalid scrolling input partially mutated state");
        Check(!grid.Move({100000000.0f,0,0}, entering, error), "scrolling accepted imprecise world coordinates");
        Check(!grid.Initialize({0,8,8}, 1, {}, error) && grid.Origin() == savedOrigin, "invalid scrolling dimensions replaced valid grid");
        Check(grid.Move(savedCenter, entering, error) && entering.empty(), "stationary grid clears history");
        GIResolvedRegion region; region.scrolling = true; region.scrollOffset = grid.RingOffset(); region.blendCenter = grid.Center();
        region.volumeMin = grid.Minimum(); region.volumeSize = glm::vec3(grid.Dimensions())*grid.Spacing();
        region.gridDimensions = grid.Dimensions(); region.probeSpacing = grid.Spacing(); region.probeCount = grid.ProbeCount();
        const auto packet = BuildGIProbeLayoutGPUData({region}, nullptr);
        Check(packet.size() == 11 && packet[3].w == 9 && packet[9] == glm::uvec4(grid.RingOffset(),1u) &&
            glm::uintBitsToFloat(packet[10]) == glm::vec4(grid.Center(),0), "GPU scrolling metadata lost ring identity or continuous center");
        std::cout << "[GIProbeLayout] scrolling: checked=" << checked << " negative coordinates, non-power dimensions, history overlap, teleport, continuous fade, atomic rejection PASS\n";
    }
    void Report(const char* label, const VansGIProbeLayout& layout)
    {
        const auto& stats = layout.Stats();
        for (const auto& leaf : layout.Leaves())
            for (uint32_t probe : leaf.probes)
            {
                if (probe == GIInvalidAddress) continue;
                const auto& p = layout.Positions()[probe];
                const float range = glm::uintBitsToFloat(p.metadata.y);
                Check(std::isfinite(range) && range > 0.0f, "invalid local visibility range");
                // 独立枚举每个叶角点，以及对该角点最远的允许位移。
                for (uint32_t corner = 0; corner < 8; ++corner)
                {
                    glm::vec3 delta;
                    for (uint32_t axis = 0; axis < 3; ++axis)
                        delta[axis] = leaf.minimumAndSpacing[axis] + ((corner >> axis) & 1u) * leaf.minimumAndSpacing.w - p.positionAndSpacing[axis];
                    Check(range + .0001f >= glm::length(delta) + .45f * p.positionAndSpacing.w,
                        "local distance clamp clips a receiver in a coarse/fine stencil");
                }
            }
        size_t maximumSources = 0, overEight = 0;
        for (const auto& leaf : layout.Leaves())
        {
            std::set<uint32_t> used;
            for (uint32_t probe : leaf.probes)
                if (probe != GIInvalidAddress) used.insert(probe);
            maximumSources = (std::max)(maximumSources, used.size()); overEight += used.size() > 8;
        }
        std::cout << "[GIProbeLayout] " << label << " probes=" << layout.Positions().size() << " leaves=" << layout.Leaves().size()
            << " spacing=" << stats.finestSpacing << ".." << stats.coarsestSpacing << " transitions=" << stats.transitionFaces
            << " constraints=" << stats.constrainedPositions << " budgetLimited=" << stats.budgetLimitedSplits
            << " physicalSources=" << maximumSources << " overEight=" << overEight << " stencils=" << layout.Stencils().size() << '\n';
    }
    glm::vec3 Corner(uint32_t i) { return {float(i & 1u), float((i >> 1u) & 1u), float((i >> 2u) & 1u)}; }
    std::map<uint32_t, double> Weights(const VansGIProbeLayout& layout, uint32_t index, glm::dvec3 point)
    {
        const auto& leaf = layout.Leaves()[index];
        const auto fraction = glm::clamp((point - glm::dvec3(leaf.minimumAndSpacing)) / double(leaf.minimumAndSpacing.w), glm::dvec3(0), glm::dvec3(1));
        std::map<uint32_t, double> result;
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            const auto blend = glm::mix(1.0 - fraction, fraction, glm::dvec3(Corner(corner)));
            const double weight = blend.x * blend.y * blend.z;
            for (uint32_t slot = 0; slot < 8; ++slot)
            {
                if (leaf.probes[slot] == GIInvalidAddress) continue;
                const double coefficient = leaf.metadata.w == GIInvalidAddress ? double(slot == corner) :
                    layout.Stencils()[leaf.metadata.w].weights[slot * 8u + corner];
                if (coefficient * weight > 0) result[leaf.probes[slot]] += coefficient * weight;
            }
        }
        return result;
    }
    void CheckContinuity(const VansGIProbeLayout& layout, uint32_t a, uint32_t b, glm::dvec3 point)
    {
        const auto first = Weights(layout, a, point); auto difference = first;
        for (const auto& [probe, weight] : Weights(layout, b, point)) difference[probe] -= weight;
        for (const auto& [probe, weight] : difference)
            if (std::abs(weight) > 0.00003)
            {
                std::cerr << "[GIProbeLayout] discontinuous leaves=" << a << ',' << b << " source=" << probe
                    << " difference=" << weight << " point=" << point.x << ',' << point.y << ',' << point.z << '\n';
                throw std::runtime_error("adjacent leaves disagree on physical probe contributions");
            }
    }
    void ValidatePackedLayout(const VansGIProbeLayout& layout, const std::vector<GIResolvedRegion>& regions)
    {
        const auto data = BuildGIProbeLayoutGPUData(regions, &layout);
        Check(data[0].x == 1 && data[0].y == regions.size() && data[0].z == 4, "GPU layout header differs from current protocol");
        const uint32_t roots = data[0].w, nodes = data[1].x, leaves = data[1].y;
        const uint32_t positions = data[1].z, stencils = data[1].w;
        Check(roots == 4u + regions.size() * 5u && nodes >= roots && leaves >= nodes && positions >= leaves &&
            stencils >= positions && data[3].y == stencils + layout.Stencils().size() * 16u &&
            data[3].z == layout.ParentCells().size() && data.size() == data[3].y + layout.ParentCells().size() * 4u,
            "GPU section offsets overlap or exceed allocation");
        uint32_t accumulated = 0;
        for (uint32_t region = 0; region < regions.size(); ++region)
        {
            const auto metadata = data[4u + region * 5u + 3u];
            Check(metadata.z == accumulated, "region physical ranges are not contiguous");
            accumulated += metadata.w;
            Check(accumulated <= layout.Positions().size(), "region range escapes physical position buffer");
            for (uint32_t local = 0; local < metadata.w; ++local)
            {
                glm::vec4 position;
                std::memcpy(&position, &data[positions + 2u * (metadata.z + local)], sizeof(position));
                Check(position == layout.Positions()[metadata.z + local].positionAndSpacing &&
                    layout.Positions()[metadata.z + local].metadata.x == region, "GPU local physical index maps to another region");
            }
        }
        Check(accumulated == layout.Positions().size(), "GPU layout omitted physical probes");
        for (uint32_t leaf = 0; leaf < layout.Leaves().size(); ++leaf)
        {
            const auto& source = layout.Leaves()[leaf];
            const auto range = data[4u + source.metadata.x * 5u + 3u];
            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const uint32_t global = data[leaves + leaf * 4u + 1u + corner / 4u][corner % 4u];
                Check(global == source.probes[corner], "GPU corner address changed during packing");
                if (global == GIInvalidAddress) continue;
                Check(global >= range.z && global - range.z < range.w, "GPU leaf addresses another region atlas");
            }
        }
        auto changed = regions;
        if (!changed.empty()) { changed[0].normalBias = 0.137f; changed[0].volumeFadeDistance = 2.375f; changed[0].priority = 4.5f; }
        const auto regular = BuildGIProbeLayoutGPUData(changed, nullptr);
        Check(regular[0].x == 0 && regular.size() == 4u + regions.size() * 5u, "regular layout allocated sparse position data");
        if (!changed.empty())
        {
            glm::vec4 bias, trace;
            std::memcpy(&bias, &regular[5], sizeof(bias)); std::memcpy(&trace, &regular[8], sizeof(trace));
            Check(bias.w == changed[0].normalBias && trace.y == changed[0].volumeFadeDistance && trace.z == changed[0].priority,
                "runtime query parameters missing from GPU region records");
        }
    }

    void ValidateResourceInvalidation()
    {
        VansGISettings baseline; NormalizeGISettings(baseline);
        auto changed = baseline; changed.regions[0].raysPerProbe += 1u;
        Check(!GISettingsResourceLayoutEquals(baseline, changed), "ray count change reused incompatible hit capacity");
        changed = baseline; changed.placement.maxRaysPerFrame += 1u;
        Check(!GISettingsResourceLayoutEquals(baseline, changed), "regular layout ignored frame budget change");
        baseline.regions.push_back(baseline.regions[0]); baseline.regions.back().stableId = 92;
        changed = baseline; changed.selectedRegionIndex = 1;
        Check(!GISettingsResourceLayoutEquals(baseline, changed), "primary region reorder reused old physical mapping");
        baseline.placement.enabled = true;
        changed = baseline; changed.regions[0].normalBias += 0.125f;
        Check(!GISettingsResourceLayoutEquals(baseline, changed), "geometry demand bias did not rebuild automatic layout");
        changed = baseline; changed.placement.parentProbeMaxSize = 2.5f;
        Check(!GISettingsResourceLayoutEquals(baseline, changed), "parent size did not invalidate physical layout");
        auto normalized = baseline.placement; normalized.parentProbeMaxSize = NAN;
        NormalizeGIProbePlacementSettings(normalized);
        Check(normalized.parentProbeMaxSize == 5.0f, "invalid parent size did not use its configuration default");
        changed = baseline; changed.regions[0].priority += 1.0f;
        Check(!GISettingsResourceLayoutEquals(baseline, changed), "refinement priority did not rebuild automatic layout");
        changed = baseline; changed.regions[0].volumeFadeDistance += 0.25f;
        Check(GISettingsResourceLayoutEquals(baseline, changed), "query fade unnecessarily rebuilt geometry layout");
        baseline.world.enabled = true; baseline.regions[1].worldOnly = true; baseline.regions[1].followView = true;
        changed = baseline; changed.regions[1].normalBias += 0.125f; changed.regions[1].priority += 1.0f;
        Check(GISettingsResourceLayoutEquals(baseline, changed), "rolling regular query parameters unnecessarily rebuilt fixed adaptive regions");
        changed = baseline; changed.regions[1].followView = false;
        Check(!GISettingsResourceLayoutEquals(baseline, changed), "scrolling mode switch reused incompatible layout metadata");
    }
    std::vector<VansGeometryTriangle> Floor()
    {
        return {{{-32,-9.25f,-32}, {-32,-9.25f,32}, {32,-9.25f,32}},
                {{-32,-9.25f,-32}, {32,-9.25f,32}, {32,-9.25f,-32}}};
    }
    GIResolvedRegion Region()
    {
        GIProbeRegionDesc region; region.center = {0,-8,0}; region.probeSpacing = 4;
        region.gridDimensions = {4,2,4}; region.overrideGridDimensions = true;
        return ResolveGIRegion(region);
    }
    void AddWall(std::vector<VansGeometryTriangle>& triangles)
    {
        triangles.push_back({{0.75f,-15,-32},{0.75f,4,32},{0.75f,-15,32},{1,0,0}});
        triangles.push_back({{0.75f,-15,-32},{0.75f,4,-32},{0.75f,4,32},{1,0,0}});
    }
    void AddBox(std::vector<VansGeometryTriangle>& triangles, glm::vec3 minimum, glm::vec3 maximum)
    {
        for (int axis = 0; axis < 3; ++axis) for (int side = 0; side < 2; ++side)
        {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            glm::vec3 a = minimum, b, c, d, n(0);
            a[axis] = side ? maximum[axis] : minimum[axis]; n[axis] = side ? 1.0f : -1.0f;
            b = c = d = a; b[u] = c[u] = maximum[u]; c[v] = d[v] = maximum[v];
            triangles.push_back({a,b,c,n}); triangles.push_back({a,c,d,n});
        }
    }
    uint32_t Oracle(const VansGIProbeLayout& layout, uint32_t region, glm::vec3 point)
    {
        const auto& volume = layout.Regions()[region]; const glm::vec3 minimum(volume.volumeMinAndRootSpacing);
        const glm::vec3 maximum = minimum + glm::vec3(volume.volumeSizeAndBias);
        if (glm::any(glm::lessThan(point, minimum)) || glm::any(glm::greaterThan(point, maximum)))
            return GIInvalidAddress;
        for (uint32_t i = 0; i < layout.Leaves().size(); ++i)
        {
            const auto& leaf = layout.Leaves()[i]; const glm::vec3 lo(leaf.minimumAndSpacing);
            bool contains = leaf.metadata.x == region;
            for (int axis = 0; axis < 3; ++axis)
                contains &= point[axis] >= lo[axis] && (point[axis] < lo[axis] + leaf.minimumAndSpacing.w ||
                    (point[axis] == maximum[axis] && point[axis] <= lo[axis] + leaf.minimumAndSpacing.w));
            if (contains) return i;
        }
        return GIInvalidAddress;
    }
    void ValidateTopology(const VansGIProbeLayout& layout, uint32_t budget, float minimumSpacing)
    {
        Check(layout.Positions().size() <= budget, "global physical probe budget exceeded");
        std::vector<uint32_t> references(layout.Positions().size(), 0);
        for (uint32_t index = 0; index < layout.Leaves().size(); ++index)
        {
            const auto& leaf = layout.Leaves()[index]; const glm::vec3 minimum(leaf.minimumAndSpacing);
            const float spacing = leaf.minimumAndSpacing.w;
            Check(spacing + 1e-6f >= minimumSpacing, "leaf subdivided below configured minimum");
            Check(leaf.metadata.w == GIInvalidAddress || leaf.metadata.w < layout.Stencils().size(), "invalid interpolation stencil address");
            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const uint32_t address = leaf.probes[corner];
                if (address == GIInvalidAddress) continue;
                Check(address < layout.Positions().size(), "leaf has out-of-range physical address");
                ++references[address];
                const auto& probe = layout.Positions()[address];
                Check(probe.metadata.x == leaf.metadata.x, "interpolation source crosses region atlas");
                if (leaf.metadata.w == GIInvalidAddress)
                    Check(glm::length(glm::vec3(probe.positionAndSpacing) - minimum - Corner(corner) * spacing) < 0.0001f,
                        "unconstrained corner does not match its interpolation slot");
            }
            // 每个面的九点独立验证平衡，避免只检查求解器自己的面中心邻居表。
            for (uint32_t face = 0; face < 6; ++face)
            for (float u : {0.13f,0.5f,0.87f}) for (float v : {0.13f,0.5f,0.87f})
            {
                const int axis = int(face / 2u);
                glm::vec3 position = minimum;
                position[axis] += face & 1u ? spacing + minimumSpacing * 0.01f : -minimumSpacing * 0.01f;
                position[(axis+1)%3] += spacing * u; position[(axis+2)%3] += spacing * v;
                const uint32_t neighbor = layout.LocateLeaf(leaf.metadata.x, position);
                if (neighbor != GIInvalidAddress)
                {
                    Check(std::abs(int(layout.Leaves()[neighbor].metadata.y) - int(leaf.metadata.y)) <= 1,
                        "adjacent demanded leaves differ by more than one level");
                    position[axis] = minimum[axis] + ((face & 1u) ? spacing : 0.0f);
                    CheckContinuity(layout, index, neighbor, glm::dvec3(position));
                }
            }
            for (uint32_t face = 0; face < 6; ++face)
            {
                const uint32_t neighbor = layout.CoarseNeighbors()[index][face];
                Check(bool(leaf.metadata.z & (1u << face)) == (neighbor != GIInvalidAddress), "transition face mask lost neighbor");
                if (neighbor != GIInvalidAddress)
                    Check(layout.Leaves()[neighbor].metadata.y + 1 == leaf.metadata.y, "coarse boundary metadata is not balanced");
            }
            for (int z = -1; z <= 1; ++z) for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
            {
                if (std::abs(x) + std::abs(y) + std::abs(z) < 2) continue;
                const glm::ivec3 direction(x,y,z);
                for (float fraction : {0.13f,0.5f,0.87f})
                {
                    glm::vec3 point = minimum + spacing * fraction, outside = point;
                    for (int axis = 0; axis < 3; ++axis) if (direction[axis])
                    {
                        point[axis] = minimum[axis] + (direction[axis] > 0 ? spacing : 0.0f);
                        outside[axis] = point[axis] + direction[axis] * minimumSpacing * 0.01f;
                    }
                    const uint32_t neighbor = layout.LocateLeaf(leaf.metadata.x, outside);
                    if (neighbor == GIInvalidAddress) continue;
                    Check(std::abs(int(layout.Leaves()[neighbor].metadata.y) - int(leaf.metadata.y)) <= 1,
                        "edge or corner neighbor differs by more than one level");
                    CheckContinuity(layout, index, neighbor, glm::dvec3(point));
                }
            }
        }
        for (const auto& cell : layout.ParentCells())
        {
            Check(cell.metadata.w == GIInvalidAddress, "parent GI used a virtual corner stencil");
            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const auto address = cell.probes[corner];
                if (address == GIInvalidAddress) continue;
                Check(address < references.size(), "parent physical address outside atlas");
                ++references[address];
                const auto& probe = layout.Positions()[address];
                Check(probe.metadata.x == cell.metadata.x && probe.metadata.z == 1u && probe.metadata.w == cell.metadata.y,
                    "parent source did not retain independent region and level ownership");
                Check(glm::length(glm::vec3(probe.positionAndSpacing) - glm::vec3(cell.minimumAndSpacing) - Corner(corner) * cell.minimumAndSpacing.w) < .0001f,
                    "parent corner is derived from a different physical origin");
                Check(probe.positionAndSpacing.w == cell.minimumAndSpacing.w, "parent relocation uses child spacing");
                Check(glm::uintBitsToFloat(probe.metadata.y) + .0001f >= cell.minimumAndSpacing.w * (std::sqrt(3.0f) + .45f),
                    "parent visibility range clips the coarse cell");
            }
        }
        for (uint32_t node = 0; node < layout.Nodes().size(); ++node)
        {
            const auto& entry = layout.Nodes()[node];
            Check(entry.lighting.x == GIInvalidAddress || entry.lighting.x < layout.ParentCells().size(), "parent cell index out of range");
            Check(entry.lighting.y == GIInvalidAddress || entry.lighting.y < node, "parent traversal is cyclic or forward-linked");
        }
        for (uint32_t count : references) Check(count > 0, "unreferenced physical probe allocated");
        for (uint32_t index = 0; index < layout.Leaves().size(); ++index)
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            const auto& leaf = layout.Leaves()[index];
            const auto point = glm::dvec3(leaf.minimumAndSpacing) + glm::dvec3(Corner(corner)) * double(leaf.minimumAndSpacing.w);
            double sum = 0; glm::dvec3 reconstructed(0.0);
            for (const auto& [probe, weight] : Weights(layout, index, point))
            {
                Check(weight > 0 && weight <= 1, "invalid physical interpolation weight");
                sum += weight;
                reconstructed += glm::dvec3(layout.Positions()[probe].positionAndSpacing) * weight;
            }
            Check(sum <= 1.000001, "boundary interpolation gained unsupported energy");
            if (std::abs(sum - 1.0) < 1e-7)
                Check(glm::length(reconstructed - point) < 0.0001, "boundary interpolation does not reproduce affine spatial fields");
        }
        std::mt19937 random(1149); std::uniform_real_distribution<float> sample(-0.05f, 1.05f);
        for (uint32_t region = 0; region < layout.Regions().size(); ++region)
        {
            const auto& volume = layout.Regions()[region];
            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 point = glm::vec3(volume.volumeMinAndRootSpacing) + Corner(corner) * glm::vec3(volume.volumeSizeAndBias);
                Check(layout.LocateLeaf(region, point) == Oracle(layout, region, point), "outer boundary differs from inclusive GI volume contract");
            }
            for (uint32_t i = 0; i < 2048; ++i)
            {
                const auto point = glm::vec3(volume.volumeMinAndRootSpacing) +
                    glm::vec3(sample(random),sample(random),sample(random)) * glm::vec3(volume.volumeSizeAndBias);
                uint32_t visited;
                Check(layout.LocateLeaf(region, point, &visited) == Oracle(layout, region, point), "bounded tree lookup differs from exhaustive cell oracle");
                Check(visited <= volume.metadata.x + 1 && visited <= GIMaxLayoutDepth + 1, "query exceeded fixed depth bound");
            }
        }
    }
    void Scenarios()
    {
        GIProbePlacementSettings settings; settings.enabled = true; settings.maxProbeCount = 65536;
        const auto region = Region(); std::string error;
        VansGIProbeLayout layout; VansSceneGeometrySnapshot geometry;
        Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
        Check(!layout.Positions().empty() && layout.Leaves().empty() && !layout.ParentCells().empty(), "empty world lost parent GI coverage");
        ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
        ValidatePackedLayout(layout, {region});
        for (float threshold : {5.0f, 2.5f, 1.1f})
        {
            settings.parentProbeMaxSize = threshold;
            Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
            ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
            for (const auto& cell : layout.ParentCells())
                Check(cell.minimumAndSpacing.w <= threshold, "parent record exceeds configurable maximum size");
            for (int z=0; z<9; ++z) for (int y=0; y<9; ++y) for (int x=0; x<9; ++x)
            {
                const auto point = region.volumeMin + region.volumeSize * glm::vec3(x,y,z) / 8.0f;
                const uint32_t node = layout.LocateNode(0, point);
                Check(node != GIInvalidAddress && layout.Nodes()[node].lighting.x != GIInvalidAddress,
                    "empty region has a hole in coarse GI coverage");
                Check(layout.LocateLeaf(0, point) == GIInvalidAddress, "coverage changed the original leaf miss classification");
            }
        }
        settings.parentProbeMaxSize = 5.0f;
        BuildQuery(geometry.opaque, Floor());
        Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
        Check(layout.Stats().acceptedSplits == 0 && layout.Stats().finestSpacing == 4.0f, "flat plane was densely subdivided");
        Check(!layout.Positions().empty(), "flat receiver lost all probes");
        // 平面在 y=-9.25，实际偏移后仍在 [-12,-8] 根层。
        // 上方 [-8,-4] 根层只被旧 4m*0.35 的扩大范围保留，不服务任何平面查询。
        for (const auto& leaf : layout.Leaves())
            Check(leaf.minimumAndSpacing.y == -12.0f, "flat plane retained a root layer beyond actual sampling demand");
        const float floorBias = (std::max)(region.normalBias, settings.minProbeSpacing * 0.35f);
        for (int z = 0; z <= 32; ++z) for (int x = 0; x <= 32; ++x)
        {
            const glm::vec3 query(region.volumeMin.x + region.volumeSize.x * float(x) / 32.0f,
                -9.25f + floorBias, region.volumeMin.z + region.volumeSize.z * float(z) / 32.0f);
            const auto leaf = layout.LocateLeaf(0, query);
            Check(leaf != GIInvalidAddress, "tightened surface demand lost an actual normal-offset query");
            Check(std::any_of(layout.Leaves()[leaf].probes.begin(), layout.Leaves()[leaf].probes.end(),
                [](uint32_t address) { return address != GIInvalidAddress; }), "surface query has no retained probe support");
        }
        for (const auto& probe : layout.Positions()) Check(probe.positionAndSpacing.y > -9.25f && probe.positionAndSpacing.y < 0,
            "underground surface support or below-ground rejection failed");
        const uint32_t coarseCount = uint32_t(layout.Positions().size());
        ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
        Report("flat", layout);

        auto triangles = Floor(); AddWall(triangles); BuildQuery(geometry.opaque, triangles);
        Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
        Check(layout.Stats().acceptedSplits > 0 && layout.Stats().finestSpacing == 0.5f && layout.Stats().coarsestSpacing == 4.0f,
            "corner refinement did not reach configured density while preserving coarse coverage");
        ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
        Report("corner_min_0.5", layout);
        ValidatePackedLayout(layout, {region});
        const auto originalLeaves = layout.Leaves(); const auto originalPositions = layout.Positions();
        const auto originalRoots = layout.Roots();
        std::vector<VansGeometryTriangle> retessellated;
        for (const auto& t : triangles)
        {
            const glm::vec3 ab=(t.a+t.b)*0.5f, bc=(t.b+t.c)*0.5f, ca=(t.c+t.a)*0.5f;
            retessellated.push_back({t.a,ab,ca,t.normal}); retessellated.push_back({ab,t.b,bc,t.normal});
            retessellated.push_back({ca,bc,t.c,t.normal}); retessellated.push_back({ab,bc,ca,t.normal});
        }
        BuildQuery(geometry.opaque, retessellated);
        Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
        Check(layout.Leaves().size() == originalLeaves.size() && layout.Positions().size() == originalPositions.size(),
            "triangle tessellation changed final refinement demand");
        // 精确比较同输入重建结果，包括共享物理地址，不只比较数量。
        const auto first = layout.Leaves(); const auto positions = layout.Positions();
        Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
        for (size_t i=0; i<first.size(); ++i)
            Check(first[i].minimumAndSpacing == layout.Leaves()[i].minimumAndSpacing && first[i].probes == layout.Leaves()[i].probes
                && first[i].metadata == layout.Leaves()[i].metadata, "layout generation is not deterministic");
        for (size_t i=0; i<positions.size(); ++i)
            Check(positions[i].positionAndSpacing == layout.Positions()[i].positionAndSpacing, "shared positions are unstable");
        settings.minProbeSpacing = 0.25f;
        Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
        Check(layout.Stats().finestSpacing == 0.25f, "minimum spacing was hard-coded to 0.5 m");
        ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
        Report("corner_min_0.25", layout);
        settings.minProbeSpacing = 0.3f;
        Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
        Check(std::abs(layout.Stats().finestSpacing - 0.3f) < 1e-6f && layout.Stats().coarsestSpacing <= settings.maxProbeSpacing,
            "non-power-of-two minimum did not define the refinement lattice");
        ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
        Report("corner_min_0.3", layout);

        settings.minProbeSpacing = 0.5f; settings.maxProbeCount = coarseCount + 150u;
        Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
        Check(layout.Stats().budgetLimitedSplits > 0, "budget-limited refinement not reported");
        ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
        Report("corner_budget", layout);
        const auto retainedRoots = layout.Roots(); const auto retainedCount = layout.Positions().size();
        settings.maxProbeCount = 8;
        Check(!layout.Build({region}, settings, geometry, nullptr, error) && !error.empty(), "insufficient coarse coverage budget silently truncated layout");
        Check(layout.Roots() == retainedRoots && layout.Positions().size() == retainedCount, "failed build published a partial layout");
        settings.enabled = false;
        Check(!layout.Build({region}, settings, geometry, nullptr, error) && layout.Roots() == retainedRoots, "disabled generator replaced authored selection");

        settings.enabled = true; settings.maxProbeCount = 65536;
        BuildQuery(geometry.opaque, {}); geometry.transmissionReceivers = Floor();
        Check(layout.Build({region}, settings, geometry, nullptr, error) && !layout.Positions().empty(), "transparent receivers lost GI support");
        geometry.transmissionReceivers.clear(); geometry.dynamicReceivers = {{{-1,-9,-1},{1,-7,1}}};
        Check(layout.Build({region}, settings, geometry, nullptr, error) && !layout.Positions().empty(), "dynamic receiver volume lost GI support");
        ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);

        geometry.dynamicReceivers.clear(); triangles.clear();
        const glm::vec3 boxMin(-3,-11,-3), boxMax(3,-5,3);
        AddBox(triangles, boxMin, boxMax); BuildQuery(geometry.opaque, triangles);
        Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
        for (const auto& probe : layout.Positions())
            Check(!(glm::all(glm::greaterThan(glm::vec3(probe.positionAndSpacing), boxMin)) &&
                glm::all(glm::lessThan(glm::vec3(probe.positionAndSpacing), boxMax))), "probe allocated inside a closed solid");
        ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
        Report("solid_box", layout);

        // 多个有限物体形成面、边、角交界；不能只在贯穿体积的无限墙上证明采样上限。
        std::mt19937 sceneRandom(20260908);
        std::uniform_real_distribution<float> location(-6.0f, 3.0f), extent(0.6f, 2.5f);
        for (uint32_t scenario = 0; scenario < 8; ++scenario)
        {
            triangles = Floor();
            for (uint32_t box = 0; box < 5; ++box)
            {
                const glm::vec3 minimum(location(sceneRandom), -9.2f, location(sceneRandom));
                const glm::vec3 maximum = minimum + glm::vec3(extent(sceneRandom), extent(sceneRandom), extent(sceneRandom));
                AddBox(triangles, minimum, maximum);
            }
            BuildQuery(geometry.opaque, triangles);
            settings.minProbeSpacing = scenario % 2 ? 0.3f : 0.5f;
            settings.maxProbeCount = scenario < 4 ? 4096u : 700u;
            Check(layout.Build({region}, settings, geometry, nullptr, error), error.c_str());
            ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
            Report(("finite_objects_" + std::to_string(scenario)).c_str(), layout);
        }

        BuildQuery(geometry.opaque, Floor()); settings.minProbeSpacing = settings.maxProbeSpacing = 4.0f;
        auto adjacent = region; adjacent.stableId = 91; adjacent.volumeMin.x += 16; adjacent.center.x += 16;
        VansGIProbeLayout firstRegion, secondRegion;
        Check(firstRegion.Build({region}, settings, geometry, nullptr, error) && secondRegion.Build({adjacent}, settings, geometry, nullptr, error), error.c_str());
        const uint32_t combinedCount = uint32_t(firstRegion.Positions().size() + secondRegion.Positions().size());
        settings.maxProbeCount = combinedCount;
        Check(layout.Build({region,adjacent}, settings, geometry, nullptr, error) && layout.Positions().size() == combinedCount,
            "multiple regions did not share one logical probe budget");
        ValidateTopology(layout, settings.maxProbeCount, settings.minProbeSpacing);
        settings.maxProbeCount = combinedCount - 1;
        ValidatePackedLayout(layout, {region, adjacent});
        const auto positionSnapshot = layout.CapturePositionSnapshot();
        Check(positionSnapshot->positions.size() == combinedCount && positionSnapshot->regions.size() == 2 &&
            positionSnapshot->regions[0].metadata.y == region.stableId && positionSnapshot->regions[1].metadata.y == adjacent.stableId,
            "position snapshot lost physical probes or stable region ownership");
        const auto firstPosition = positionSnapshot->positions.front().positionAndSpacing;
        Check(!layout.Build({region,adjacent}, settings, geometry, nullptr, error) && layout.Positions().size() == combinedCount,
            "last region was silently omitted to satisfy a global budget");
        settings.maxProbeCount = 65536; settings.minProbeSpacing = 0.5f;
        auto farRegion = region; farRegion.volumeMin = glm::vec3(100000000.0f);
        Check(!layout.Build({farRegion}, settings, geometry, nullptr, error) && error.find("precision") != std::string::npos,
            "layout accepted indistinguishable float positions at extreme world coordinates");
        auto disabled = region; disabled.enabled = false;
        Check(layout.Build({disabled}, settings, geometry, nullptr, error) && layout.Positions().empty() && layout.Roots().empty(),
            "disabled authored region generated probes");
        Check(layout.CapturePositionSnapshot()->positions.empty() && positionSnapshot->positions.size() == combinedCount &&
            positionSnapshot->positions.front().positionAndSpacing == firstPosition,
            "layout replacement invalidated a retained immutable position snapshot");
        std::cout << "[GIProbeLayout] position snapshot: stable region IDs, physical positions and retained lifetime PASS\n";
    }
}

bool TestGIProbeLayoutContract()
{
    try { ValidateScrollingGrid(); Scenarios(); ValidateResourceInvalidation(); std::cout << "[GIProbeLayout] PASS: adaptive spacing, bounded lookup, balance, demand, shared corners, GPU packing, resource invalidation, budgets and atomic failure\n"; return true; }
    catch (const std::exception& error) { std::cerr << "[GIProbeLayout] FAIL: " << error.what() << '\n'; return false; }
}
