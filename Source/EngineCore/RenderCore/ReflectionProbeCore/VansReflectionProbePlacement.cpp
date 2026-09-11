#include "VansReflectionProbePlacement.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <numeric>
#include <map>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace VansGraphics
{
    namespace
    {
        struct Receiver
        {
            glm::vec3 position{}, normal{};
            float area = 0.0f;
            bool covered = false;
            uint32_t region = 0;
        };
        using SurfaceKey = std::array<int32_t, 5>;
        struct KeyHash
        {
            size_t operator()(const SurfaceKey& key) const
            {
                size_t hash = 1469598103934665603ull;
                for (int32_t value : key) { hash ^= static_cast<uint32_t>(value); hash *= 1099511628211ull; }
                return hash;
            }
        };
        bool Finite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
        bool Inside(const glm::vec3& p, const glm::vec3& minimum, const glm::vec3& maximum)
        {
            return glm::all(glm::greaterThanEqual(p, minimum)) && glm::all(glm::lessThanEqual(p, maximum));
        }
        float Influence(const VansReflectionProbeDesc& probe, const glm::vec3& p)
        {
            if (!probe.enabled || probe.type == ReflectionProbeType::Sky) return 0.0f;
            const float distance = probe.shape == ReflectionProbeShape::Box
                ? glm::length(glm::max(glm::max(probe.boxMin - p, p - probe.boxMax), glm::vec3(0.0f)))
                : (std::max)(0.0f, glm::distance(probe.position, p) - probe.radius);
            return 1.0f - glm::clamp(distance / (std::max)(probe.blendDistance, 0.001f), 0.0f, 1.0f);
        }
        bool Visible(const VansTriangleGeometryQuery& geometry, const Receiver& receiver,
            const glm::vec3& capture, float clearance)
        {
            const glm::vec3 direction = capture - receiver.position;
            const float distance = glm::length(direction);
            if (glm::dot(receiver.normal, direction) < -0.0001f) return false;
            const float bias = (std::min)(0.02f, clearance * 0.1f);
            if (distance <= bias * 2.0f) return true;
            const glm::vec3 origin = receiver.position + receiver.normal * bias;
            const glm::vec3 ray = capture - origin;
            VansGeometryHit hit;
            return !geometry.Raycast(origin, ray, (std::max)(0.0f, glm::length(ray) - bias), hit, bias * 0.1f);
        }
        std::string CaptureName(const VansReflectionProbeDesc& probe)
        {
            uint64_t hash = 1469598103934665603ull;
            auto add = [&](float value)
            {
                uint32_t bits; std::memcpy(&bits, &value, sizeof(bits));
                hash ^= bits; hash *= 1099511628211ull;
            };
            for (int axis = 0; axis < 3; ++axis)
            { add(probe.capturePosition[axis]); add(probe.boxMin[axis]); add(probe.boxMax[axis]); }
            hash ^= probe.resolution; hash *= 1099511628211ull;
            std::ostringstream name; name << "Auto Surface " << std::hex << std::setw(16) << std::setfill('0') << hash;
            return name.str();
        }
    }

    bool VansReflectionProbePlacement::Generate(const VansSceneGeometrySnapshot& geometry,
        const ReflectionProbePlacementSettings& settings, const std::vector<VansReflectionProbeDesc>& overrides,
        VansReflectionProbePlacementResult& result, std::string& error)
    {
        error.clear();
        if (!Finite(settings.volumeMin) || !Finite(settings.volumeMax)
            || glm::any(glm::lessThanEqual(settings.volumeMax, settings.volumeMin)))
        { error = "Reflection placement requires a finite, non-empty volume"; return false; }
        const float cell = settings.cellSize;
        const float clearance = settings.minCaptureClearance;
        if (!std::isfinite(cell) || cell < 0.05f || !std::isfinite(clearance) || clearance < 0.001f
            || !std::isfinite(settings.indoorSpacing) || !std::isfinite(settings.corridorSpacing)
            || !std::isfinite(settings.outdoorSpacing) || settings.indoorSpacing < cell
            || settings.corridorSpacing < cell || settings.outdoorSpacing < cell || settings.maxProbeCount == 0)
        { error = "Reflection placement spacing, clearance and budget are invalid"; return false; }
        const glm::vec3 gridSize = glm::ceil((settings.volumeMax - settings.volumeMin) / cell);
        if (glm::any(glm::greaterThan(gridSize, glm::vec3(1000000.0f))))
        { error = "Reflection surface-analysis grid exceeds its coordinate budget"; return false; }
        const glm::ivec3 dimensions(gridSize);
        const float maxSpacing = (std::max)({settings.indoorSpacing, settings.corridorSpacing, settings.outdoorSpacing});
        const float rayDistance = glm::length(settings.volumeMax - settings.volumeMin) + maxSpacing;
        VansReflectionProbePlacementResult pending;
        std::vector<Receiver> receivers;
        std::unordered_map<SurfaceKey, Receiver, KeyHash> surfaces;
        uint64_t clippingWork = 0;
        try
        {
            auto addSurface = [&](const VansGeometryTriangle& triangle)
            {
                const auto minimum = glm::max(settings.volumeMin, glm::min(triangle.a, glm::min(triangle.b, triangle.c)));
                const auto maximum = glm::min(settings.volumeMax, glm::max(triangle.a, glm::max(triangle.b, triangle.c)));
                if (glm::any(glm::greaterThan(minimum, maximum))) return;
                const glm::ivec3 first = glm::clamp(glm::ivec3(glm::floor((minimum - settings.volumeMin) / cell)), glm::ivec3(0), dimensions - 1);
                const glm::ivec3 last = glm::clamp(glm::ivec3(glm::floor((maximum - settings.volumeMin) / cell)), glm::ivec3(0), dimensions - 1);
                int axis = std::abs(triangle.normal.y) > std::abs(triangle.normal.x) ? 1 : 0;
                if (std::abs(triangle.normal.z) > std::abs(triangle.normal[axis])) axis = 2;
                for (int z = first.z; z <= last.z; ++z) for (int y = first.y; y <= last.y; ++y) for (int x = first.x; x <= last.x; ++x)
                {
                    if (++clippingWork > 50000000ull) throw std::runtime_error("Reflection surface analysis exceeded its work budget; increase analysis cell size");
                    const glm::vec3 cellMin = settings.volumeMin + glm::vec3(x, y, z) * cell;
                    glm::vec3 position; float area;
                    if (!VansTriangleGeometryQuery::ClipSurfaceToBox(triangle, cellMin, glm::min(cellMin + cell, settings.volumeMax), position, area)) continue;
                    for (int side = 0; side < (triangle.twoSided ? 2 : 1); ++side)
                    {
                        const glm::vec3 normal = side ? -triangle.normal : triangle.normal;
                        const int face = axis * 2 + (normal[axis] < 0.0f ? 1 : 0);
                        const int plane = static_cast<int>(std::floor((position[axis] - settings.volumeMin[axis]) / (cell * 0.25f)));
                        const SurfaceKey key{x, y, z, face, plane};
                        auto& receiver = surfaces[key];
                        // 保留真实表面上的代表点；不把两片不同表面的位置平均到空中。
                        if (receiver.area == 0.0f || glm::distance(position, cellMin + cell * 0.5f)
                            < glm::distance(receiver.position, cellMin + cell * 0.5f))
                        { receiver.position = position; receiver.normal = normal; }
                        receiver.area += area;
                    }
                    if (surfaces.size() > 1000000u) throw std::runtime_error("Reflection surface analysis exceeded its receiver budget");
                }
            };
            for (const auto& triangle : geometry.opaque.GetTriangles()) addSurface(triangle);
            for (const auto& triangle : geometry.transmissionReceivers) addSurface(triangle);
            std::vector<std::pair<SurfaceKey, Receiver>> ordered(surfaces.begin(), surfaces.end());
            std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            receivers.reserve(ordered.size() + geometry.dynamicReceivers.size() * 7);
            for (auto& entry : ordered) receivers.push_back(std::move(entry.second));
            surfaces.clear(); ordered.clear();
            for (const auto& bounds : geometry.dynamicReceivers)
            {
                const glm::vec3 center = (bounds.minimum + bounds.maximum) * 0.5f;
                auto addReceiver = [&](glm::vec3 p)
                {
                    if (!Inside(p, settings.volumeMin, settings.volumeMax)) return;
                    Receiver receiver; receiver.position = p; receiver.area = cell * cell; receivers.push_back(receiver);
                };
                addReceiver(center);
                for (int axis = 0; axis < 3; ++axis)
                { auto p = center; p[axis] = bounds.minimum[axis]; addReceiver(p); p[axis] = bounds.maximum[axis]; addReceiver(p); }
            }
            pending.receiverCount = static_cast<uint32_t>(receivers.size());
            for (auto& receiver : receivers)
                for (const auto& probe : overrides)
                    if (Influence(probe, receiver.position) > 0.0f && Visible(geometry.opaque, receiver, probe.capturePosition, clearance))
                    { receiver.covered = true; break; }

            // 候选点覆盖用空间桶查询，不为每个候选扫描全场受光面。
            using CellKey = std::array<int, 3>;
            const auto keyAt = [&](const glm::vec3& p, float size)
            {
                const glm::ivec3 key(glm::floor((p - settings.volumeMin) / size));
                return CellKey{key.x, key.y, key.z};
            };
            std::map<CellKey, std::vector<uint32_t>> receiverCells;
            std::map<CellKey, uint32_t> regionIds;
            std::vector<double> regionArea, regionRemaining;
            uint64_t candidateCellVisits = 0, candidateAttempts = 0;
            const float regionSize = (std::min)({settings.indoorSpacing, settings.corridorSpacing, settings.outdoorSpacing});
            for (uint32_t i = 0; i < receivers.size(); ++i)
            {
                auto& receiver = receivers[i];
                receiverCells[keyAt(receiver.position, cell)].push_back(i);
                auto [entry, added] = regionIds.emplace(keyAt(receiver.position, regionSize), static_cast<uint32_t>(regionIds.size()));
                if (added) { regionArea.push_back(0); regionRemaining.push_back(0); }
                receiver.region = entry->second;
            }
            auto candidate = [&](const glm::vec3& position, VansReflectionProbeDesc& probe, glm::vec3* roomCenter = nullptr)
            {
                if (++candidateAttempts > 500000ull)
                    throw std::runtime_error("Reflection candidate search exceeded its work budget; increase analysis cell size");
                if (!Inside(position, settings.volumeMin + clearance, settings.volumeMax - clearance)) return false;
                VansGeometryHit nearest;
                if (geometry.opaque.NearestSurface(position, clearance, nearest)) return false;
                uint32_t backfaces = 0, opaqueHits = 0, openUpwardRays = 0;
                std::array<float, 6> axisDistance; axisDistance.fill(rayDistance);
                for (int z = -1; z <= 1; ++z) for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x)
                {
                    if (!x && !y && !z) continue;
                    VansGeometryHit hit;
                    if (!geometry.opaque.Raycast(position, glm::vec3(x, y, z), rayDistance, hit)) { if (y > 0) ++openUpwardRays; continue; }
                    if (!hit.twoSided) { ++opaqueHits; if (hit.backface) ++backfaces; }
                    if (std::abs(x) + std::abs(y) + std::abs(z) == 1)
                    {
                        const int axis = x ? 0 : (y ? 1 : 2); const int sign = (x + y + z) < 0 ? 0 : 1;
                        axisDistance[axis * 2 + sign] = hit.distance;
                    }
                }
                if (backfaces >= 3 && float(backfaces) > float(opaqueHits) * glm::clamp(settings.solidThreshold, 0.05f, 0.95f)) return false;
                const float widthX = axisDistance[0] + axisDistance[1], widthZ = axisDistance[4] + axisDistance[5];
                const bool corridor = (std::max)(widthX, widthZ) > (std::min)(widthX, widthZ) * 3.0f
                    && (std::min)(widthX, widthZ) < settings.indoorSpacing;
                // 露天判断只看上半球；地面本来就遮挡下半球，不能要求全方向一半以上射线未命中。
                const bool outdoor = openUpwardRays >= 5 && axisDistance[3] > settings.indoorSpacing;
                const float spacing = corridor ? settings.corridorSpacing : (outdoor ? settings.outdoorSpacing : settings.indoorSpacing);
                // 平行射线的中位边界，避免单根梁柱把整个房间的影响盒截短。
                // 横向偏移也必须可见，不能从墙的另一侧发射探测射线。
                const float spread = (std::min)(cell * 0.5f, spacing * 0.15f);
                for (int axis = 0; axis < 3; ++axis) for (int side = 0; side < 2; ++side)
                {
                    std::vector<float> distances{axisDistance[axis * 2 + side]};
                    glm::vec3 direction(0); direction[axis] = side ? 1.0f : -1.0f;
                    for (int u = -1; u <= 1; ++u) for (int v = -1; v <= 1; ++v)
                    {
                        if (!u && !v) continue;
                        glm::vec3 offset(0); offset[(axis+1)%3] = u * spread; offset[(axis+2)%3] = v * spread;
                        VansGeometryHit hit;
                        if (geometry.opaque.Raycast(position, offset, glm::length(offset) + clearance, hit)) continue;
                        const bool found = geometry.opaque.Raycast(position + offset, direction, rayDistance, hit);
                        if (found && !hit.twoSided && hit.backface) continue;
                        distances.push_back(found ? hit.distance : rayDistance);
                    }
                    std::sort(distances.begin(), distances.end());
                    axisDistance[axis * 2 + side] = distances[(distances.size()-1)/2];
                }
                probe.position = probe.capturePosition = position;
                if (roomCenter)
                {
                    *roomCenter = position;
                    for (int axis = 0; axis < 3; ++axis)
                        if (axisDistance[axis*2] + axisDistance[axis*2+1] <= spacing * 1.05f)
                            (*roomCenter)[axis] += (axisDistance[axis*2+1] - axisDistance[axis*2]) * 0.5f;
                }
                probe.boxMin = glm::max(settings.volumeMin, position - spacing * 0.5f);
                probe.boxMax = glm::min(settings.volumeMax, position + spacing * 0.5f);
                for (int axis = 0; axis < 3; ++axis)
                {
                    probe.boxMin[axis] = (std::max)(probe.boxMin[axis], position[axis] - axisDistance[axis * 2] - 0.01f);
                    probe.boxMax[axis] = (std::min)(probe.boxMax[axis], position[axis] + axisDistance[axis * 2 + 1] + 0.01f);
                }
                probe.shape = ReflectionProbeShape::Box; probe.boxProjection = true; probe.autoGenerated = true;
                probe.nearPlane = (std::min)(0.05f, clearance * 0.25f); probe.farPlane = (std::max)(rayDistance, 1000.0f);
                probe.blendDistance = (std::max)(0.05f, (std::min)(clearance * 0.5f, spacing * 0.05f));
                probe.resolution = settings.uniformProbeResolution;
                // 室内局部捕获优先于露天大范围捕获，避免同权重时按数组顺序混入室外天空。
                probe.priority = corridor ? 1.0f : (outdoor ? 0.0f : 2.0f);
                return true;
            };
            struct Candidate { VansReflectionProbeDesc probe; std::vector<uint32_t> coverage; };
            std::vector<Candidate> candidates;
            std::map<CellKey, std::vector<glm::vec3>> positions;
            std::vector<bool> reachable(receivers.size(), false);
            uint64_t coverageReferences = 0;
            const auto addCandidate = [&](const glm::vec3& position, const VansReflectionProbeDesc* prepared = nullptr)
            {
                if (!Finite(position) || !Inside(position, settings.volumeMin + clearance, settings.volumeMax - clearance)) return;
                auto& bucket = positions[keyAt(position, (std::max)(clearance, cell * 0.25f))];
                for (const auto& previous : bucket)
                {
                    VansGeometryHit hit; const glm::vec3 ray = position - previous;
                    if (!geometry.opaque.Raycast(previous, ray, glm::length(ray), hit)) return;
                }
                VansReflectionProbeDesc probe;
                if (prepared) probe = *prepared;
                else if (!candidate(position, probe)) { ++pending.rejectedCaptureCount; return; }
                bucket.push_back(position);
                Candidate item; item.probe = probe;
                const auto first = keyAt(probe.boxMin, cell), last = keyAt(probe.boxMax, cell);
                for (int z=first[2]; z<=last[2]; ++z) for (int y=first[1]; y<=last[1]; ++y) for (int x=first[0]; x<=last[0]; ++x)
                {
                    if (++candidateCellVisits > 100000000ull)
                        throw std::runtime_error("Reflection coverage search exceeded its work budget; increase analysis cell size");
                    const auto found = receiverCells.find({x,y,z}); if (found == receiverCells.end()) continue;
                    for (uint32_t i : found->second)
                    {
                        const auto& receiver = receivers[i];
                        // 只把完整影响域内且确实可见的表面算作覆盖，淡出尾部不用于填补空缺。
                        if (receiver.covered || !Inside(receiver.position, probe.boxMin, probe.boxMax)
                            || !Visible(geometry.opaque, receiver, position, clearance)) continue;
                        item.coverage.push_back(i); reachable[i] = true;
                    }
                }
                if (item.coverage.empty()) return;
                coverageReferences += item.coverage.size();
                if (candidates.size() >= 200000u || coverageReferences > 32000000ull)
                    throw std::runtime_error("Reflection candidate coverage exceeded its work budget; increase analysis cell size");
                candidates.push_back(std::move(item));
            };
            std::vector<uint32_t> seeds(receivers.size()); std::iota(seeds.begin(), seeds.end(), 0u);
            std::stable_sort(seeds.begin(), seeds.end(), [&](uint32_t a, uint32_t b) { return receivers[a].area > receivers[b].area; });
            const auto seedCandidate = [&](uint32_t seed)
            {
                const auto& target = receivers[seed]; if (target.covered) return;
                const glm::vec3 normal = glm::dot(target.normal, target.normal) > 0.5f ? target.normal : glm::vec3(0, 1, 0);
                const glm::vec3 near = target.position + normal * (std::max)(cell * 0.5f, clearance * 2);
                VansReflectionProbeDesc room;
                glm::vec3 roomCenter;
                if (candidate(near, room, &roomCenter))
                {
                    glm::vec3 center = near;
                    // 仅沿有两侧边界的轴居中；露天空地保持接近受光表面的捕获高度。
                    for (int axis : {0,2,1})
                    {
                        glm::vec3 next = center; next[axis] = roomCenter[axis];
                        VansGeometryHit hit; const glm::vec3 ray = next-center;
                        if (!geometry.opaque.Raycast(center, ray, glm::length(ray)+clearance, hit)) center = next;
                    }
                    addCandidate(center, center == near ? &room : nullptr);
                    if (center != near) addCandidate(near, &room);
                }
                addCandidate(target.position + normal * (std::max)(cell * 1.5f, clearance * 2));
                if (!reachable[seed]) addCandidate(target.position + normal * (clearance * 2));
            };
            // 大区域先建立稀疏候选；再补充没有任何可见候选的局部空缺。
            // 细分只增加搜索位置，不直接增加最终探针数量。
            std::map<SurfaceKey, uint32_t> seedCells;
            const float seedCell = (std::max)(cell, regionSize * 0.5f);
            for (uint32_t seed : seeds)
            {
                const auto& receiver = receivers[seed]; if (receiver.covered) continue;
                int axis = std::abs(receiver.normal.y) > std::abs(receiver.normal.x) ? 1 : 0;
                if (std::abs(receiver.normal.z) > std::abs(receiver.normal[axis])) axis = 2;
                const auto coarse = keyAt(receiver.position, seedCell);
                const int face = axis*2 + (receiver.normal[axis] < 0 ? 1 : 0);
                if (seedCells.emplace(SurfaceKey{coarse[0],coarse[1],coarse[2],face,0}, seed).second) seedCandidate(seed);
            }
            seedCells.clear();
            for (uint32_t seed : seeds)
            {
                const auto& receiver = receivers[seed]; if (receiver.covered || reachable[seed]) continue;
                const auto local = keyAt(receiver.position, cell * 2.0f);
                int axis = std::abs(receiver.normal.y) > std::abs(receiver.normal.x) ? 1 : 0;
                if (std::abs(receiver.normal.z) > std::abs(receiver.normal[axis])) axis = 2;
                const int face = axis*2 + (receiver.normal[axis] < 0 ? 1 : 0);
                if (seedCells.emplace(SurfaceKey{local[0],local[1],local[2],face,0}, seed).second) seedCandidate(seed);
            }
            // 全场候选按新增可见表面积竞争预算，区域达到容差后让预算转向未覆盖区域。
            // 不可达表面仍保留在最终覆盖率分母中，不能用过滤样本把统计变成 100%。
            for (uint32_t i = 0; i < receivers.size(); ++i)
            {
                const auto& receiver = receivers[i];
                if (reachable[i] || receiver.covered) regionArea[receiver.region] += receiver.area;
                if (reachable[i] && !receiver.covered) regionRemaining[receiver.region] += receiver.area;
            }
            const float tolerance = std::isfinite(settings.refinementThreshold) ? glm::clamp(settings.refinementThreshold, 0.0f, 0.1f) : 0.0f;
            const auto gain = [&](const Candidate& item)
            {
                double area = 0;
                for (uint32_t i : item.coverage)
                {
                    const auto& receiver = receivers[i];
                    if (!receiver.covered && regionRemaining[receiver.region] > regionArea[receiver.region] * tolerance)
                        area += receiver.area;
                }
                return area;
            };
            using Ranked = std::pair<double, size_t>;
            std::priority_queue<Ranked> queue;
            for (size_t i=0; i<candidates.size(); ++i) queue.emplace(gain(candidates[i]), candidates.size()-1-i);
            const size_t budget = settings.maxProbeCount > overrides.size() ? settings.maxProbeCount - overrides.size() : 0;
            while (!queue.empty() && pending.probes.size() < budget)
            {
                const auto ranked = queue.top(); queue.pop();
                auto& item = candidates[candidates.size()-1-ranked.second];
                const double area = gain(item);
                if (area <= 0) continue;
                if (!queue.empty() && area < queue.top().first) { queue.emplace(area, ranked.second); continue; }
                item.probe.name = CaptureName(item.probe); item.probe.regionId = static_cast<uint32_t>(pending.probes.size());
                pending.probes.push_back(item.probe);
                for (uint32_t i : item.coverage)
                {
                    auto& receiver = receivers[i]; if (receiver.covered) continue;
                    receiver.covered = true; regionRemaining[receiver.region] -= receiver.area;
                }
            }
            pending.candidateCount = static_cast<uint32_t>(candidates.size());
            double totalArea = 0.0, coveredArea = 0.0;
            for (const auto& receiver : receivers)
            {
                totalArea += receiver.area;
                if (receiver.covered) { ++pending.coveredReceiverCount; coveredArea += receiver.area; }
            }
            pending.coveredSurfaceFraction = totalArea > 0.0 ? static_cast<float>(coveredArea / totalArea) : 1.0f;
            pending.reachableSurfaceFraction = totalArea > 0.0 ? static_cast<float>(std::accumulate(regionArea.begin(), regionArea.end(), 0.0) / totalArea) : 1.0f;
            bool remainingDemand = false;
            for (size_t i=0; i<regionArea.size(); ++i) remainingDemand |= regionRemaining[i] > regionArea[i] * tolerance + 0.0001;
            pending.budgetExhausted = pending.probes.size() >= budget && remainingDemand;
            result = std::move(pending);
            return true;
        }
        catch (const std::exception& exception) { error = exception.what(); return false; }
    }
}
