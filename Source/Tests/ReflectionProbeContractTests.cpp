#include "../EngineCore/RenderCore/ReflectionProbeCore/VansReflectionProbeSpatialIndex.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>

namespace
{
    using namespace VansGraphics;
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    VansReflectionProbeGPU Box(glm::vec3 center, glm::vec3 halfSize, float blend = 2.0f)
    {
        VansReflectionProbeGPU result{};
        result.positionAndRadius = glm::vec4(center, glm::length(halfSize));
        result.boxMinAndType = glm::vec4(center - halfSize, 1.0f);
        result.boxMaxAndPriority = glm::vec4(center + halfSize, 1.0f);
        result.fadeAndIntensity = {blend, 1.0f, 0.0f, 0.0f};
        result.regionAndFlags = {0u, 1u, 0u, 0u};
        return result;
    }

    float Influence(const VansReflectionProbeGPU& probe, glm::vec3 p)
    {
        if ((probe.regionAndFlags.y & 1u) == 0u) return 0.0f;
        if (probe.boxMinAndType.w > 0.5f)
        {
            const glm::vec3 q = glm::max(glm::max(glm::vec3(probe.boxMinAndType) - p,
                p - glm::vec3(probe.boxMaxAndPriority)), glm::vec3(0.0f));
            return 1.0f - glm::smoothstep(0.0f, std::max(probe.fadeAndIntensity.x, 0.001f), glm::length(q));
        }
        const float radius = std::max(probe.positionAndRadius.w, 0.001f);
        const float blend = std::clamp(probe.fadeAndIntensity.x, 0.001f, radius);
        return 1.0f - glm::smoothstep(std::max(radius - blend, 0.0f), radius,
            glm::length(p - glm::vec3(probe.positionAndRadius)));
    }

    struct Result
    {
        float coverage = 0.0f;
        std::array<uint32_t, 4> ids = {~0u, ~0u, ~0u, ~0u};
        std::array<float, 4> weights{};
    };
    Result Evaluate(const std::vector<VansReflectionProbeGPU>& probes,
        const uint32_t* candidates, uint32_t count, glm::vec3 position)
    {
        Result result;
        std::vector<std::pair<float, uint32_t>> weighted;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t id = candidates[i];
            const float influence = Influence(probes[id], position);
            result.coverage += influence;
            const float weight = influence * std::exp2(std::clamp(probes[id].boxMaxAndPriority.w, -8.0f, 8.0f)) *
                std::max(probes[id].fadeAndIntensity.y, 0.0f);
            if (weight > 0.0f) weighted.emplace_back(weight, id);
        }
        // 独立的稳定排序 oracle，验证完整扫描与索引的选择结果。
        std::stable_sort(weighted.begin(), weighted.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < std::min(size_t(4), weighted.size()); ++i)
        {
            result.ids[i] = weighted[i].second;
            result.weights[i] = weighted[i].first;
        }
        return result;
    }

    uint64_t Compare(const std::vector<VansReflectionProbeGPU>& probes,
        const VansReflectionProbeSpatialIndex& index, const std::vector<glm::vec3>& positions)
    {
        std::vector<uint32_t> all(probes.size());
        std::iota(all.begin(), all.end(), 0u);
        uint64_t candidateCount = 0;
        for (const auto& position : positions)
        {
            const auto range = index.QueryRange(position);
            Require(size_t(range.x) + range.y <= index.GetWords().size(), "Candidate range exceeds storage");
            const auto* candidates = index.GetWords().data() + range.x;
            Require(std::is_sorted(candidates, candidates + range.y), "Probe order changed");
            const auto reference = Evaluate(probes, all.data(), uint32_t(all.size()), position);
            const auto actual = Evaluate(probes, candidates, range.y, position);
            Require(reference.ids == actual.ids, "Top-four IDs changed");
            Require(reference.weights == actual.weights, "Top-four weights changed");
            Require(reference.coverage == actual.coverage, "Coverage changed or a positive-influence probe was omitted");
            candidateCount += range.y;
        }
        return candidateCount;
    }
}

bool TestReflectionProbeSpatialIndexContract()
{
    try
    {
        using namespace VansGraphics;
        static_assert(sizeof(VansReflectionProbeGPU) == 112);
        static_assert(sizeof(ReflectionProbeBufferHeader) == 80);
        VansReflectionProbeSpatialIndex index;
        Require(index.QueryRange({0, 0, 0}).y == 0u, "Empty scene must have no candidates");

        std::vector<VansReflectionProbeGPU> probes;
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 32; ++x)
                {
                    auto probe = Box({float(x * 12 - 192), float(y * 12 - 24), float(z * 12 - 48)}, glm::vec3(6.0f));
                    const size_t id = probes.size();
                    if (id % 5 == 0) probe.boxMinAndType.w = 0.0f;
                    if (id % 7 == 0) probe.regionAndFlags.y = 0u;
                    if (id % 11 == 0) probe.fadeAndIntensity.y = 0.0f;
                    if (id % 13 == 0) probe.fadeAndIntensity.x = 0.0f;
                    probe.boxMaxAndPriority.w = float(int(id % 5) - 2) * 0.5f;
                    probes.push_back(probe);
                }
        Require(index.Update(probes), "First index build was skipped");
        Require(!index.Update(probes), "Unchanged geometry must not rebuild the index");
        std::mt19937 random(20260908u);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::vector<glm::vec3> positions;
        for (int i = 0; i < 12000; ++i)
            positions.push_back(glm::vec3(-208, -40, -64) + glm::vec3(unit(random), unit(random), unit(random)) * glm::vec3(412, 80, 128));
        for (const auto& probe : probes)
        {
            const glm::vec3 maximum(probe.boxMaxAndPriority);
            positions.push_back(glm::vec3(probe.positionAndRadius));
            positions.push_back(glm::vec3(probe.boxMinAndType));
            positions.push_back(maximum);
            positions.push_back(maximum + glm::vec3(probe.fadeAndIntensity.x, 0, 0));
            positions.push_back(maximum + glm::vec3(std::nextafter(probe.fadeAndIntensity.x, 0.0f), 0, 0));
        }
        const auto header = index.GetHeader();
        const glm::uvec3 dimensions(header.dimensionsAndCellCount);
        for (uint32_t z = 0; z < dimensions.z; ++z)
            for (uint32_t y = 0; y < dimensions.y; ++y)
                for (uint32_t x = 0; x < dimensions.x; ++x)
                    positions.push_back(glm::vec3(header.origin) + glm::vec3(x, y, z) / glm::vec3(header.inverseCellSize));
        const uint64_t candidates = Compare(probes, index, positions);
        Require(double(candidates) / positions.size() < 16.0, "Separated layout lost local-query scaling");

        probes[0].regionAndFlags.y = 1u;
        probes[0].boxMaxAndPriority.w = 8.0f;
        Require(!index.Update(probes), "Bake status or priority must not rebuild geometric index");
        Compare(probes, index, {{-192, -24, -48}, {0, 0, 0}});
        probes[0].positionAndRadius.x = 500.0f;
        Require(index.Update(probes), "Moving a sphere must update spatial bounds");
        Compare(probes, index, {{500, -24, -48}, {-192, -24, -48}});

        // 真正重叠超过八个时也必须完整保留；不能截断以伪造性能上限。
        probes.assign(24, Box(glm::vec3(-10), glm::vec3(3)));
        probes.back().boxMaxAndPriority.w = 8.0f;
        index.Update(probes);
        Require(index.QueryRange(glm::vec3(-10)).y == 24u, "Dense overlap was silently truncated");
        Compare(probes, index, {glm::vec3(-10), glm::vec3(-6.5f)});

        // 大场景索引须有有限内存，且粗化索引不能漏掉实际影响。
        probes.push_back(Box(glm::vec3(1e6f), glm::vec3(2)));
        index.Update(probes);
        Require(index.GetHeader().dimensionsAndCellCount.w <= 262144u, "Cell allocation budget exceeded");
        Require(index.GetWords().size() <= 262144u * 2u + 4194304u, "Index reference budget exceeded");
        Compare(probes, index, {glm::vec3(-10), glm::vec3(1e6f), glm::vec3(0)});
        Require(index.Update({}) && index.QueryRange(glm::vec3(-10)).y == 0u, "Scene removal retained stale candidates");
        std::cout << "[ReflectionProbeIndex] PASS: 1024 mixed probes, " << positions.size()
                  << " queries, mean candidates=" << double(candidates) / positions.size()
                  << "; exact selection/coverage, boundaries, dense overlap, movement, empty scene\n";
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ReflectionProbeIndex] " << error.what() << '\n';
        return false;
    }
}
