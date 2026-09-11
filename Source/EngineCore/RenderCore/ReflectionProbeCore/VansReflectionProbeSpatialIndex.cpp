#include "VansReflectionProbeSpatialIndex.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace VansGraphics
{
    namespace
    {
        constexpr uint32_t MaxCells = 262144u;
        constexpr uint64_t MaxReferences = 4194304u;

        uint32_t LinearIndex(const glm::uvec3& cell, const glm::uvec3& dimensions)
        {
            return cell.x + dimensions.x * (cell.y + dimensions.y * cell.z);
        }

        bool Finite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }
    }

    void VansReflectionProbeSpatialIndex::Clear()
    {
        m_Header = {};
        m_Bounds.clear();
        m_Words.assign(2u, 0u);
        m_MaxCandidateCount = 0;
    }

    bool VansReflectionProbeSpatialIndex::Update(const std::vector<VansReflectionProbeGPU>& probes)
    {
        std::vector<Bounds> bounds;
        bounds.reserve(probes.size());
        for (const auto& probe : probes)
        {
            Bounds bound{};
            if (probe.boxMinAndType.w > 0.5f)
            {
                const glm::vec3 fade(std::max(probe.fadeAndIntensity.x, 0.001f));
                bound.minimum = glm::min(glm::vec3(probe.boxMinAndType), glm::vec3(probe.boxMaxAndPriority)) - fade;
                bound.maximum = glm::max(glm::vec3(probe.boxMinAndType), glm::vec3(probe.boxMaxAndPriority)) + fade;
            }
            else
            {
                const glm::vec3 radius(std::max(probe.positionAndRadius.w, 0.001f));
                bound.minimum = glm::vec3(probe.positionAndRadius) - radius;
                bound.maximum = glm::vec3(probe.positionAndRadius) + radius;
            }
            if (!Finite(bound.minimum) || !Finite(bound.maximum))
                throw std::invalid_argument("Reflection probe influence bounds must be finite");
            // 保护 CPU/GPU 浮点归格边界；范围只扩大，不遗漏 blend 边缘。
            const glm::vec3 padding = glm::max(glm::vec3(0.0001f),
                glm::max(glm::abs(bound.minimum), glm::abs(bound.maximum)) *
                (16.0f * std::numeric_limits<float>::epsilon()));
            bound.minimum -= padding;
            bound.maximum += padding;
            bounds.push_back(bound);
        }
        if (bounds == m_Bounds) return false;
        if (bounds.empty())
        {
            Clear();
            return true;
        }

        glm::vec3 origin = bounds.front().minimum;
        glm::vec3 maximum = bounds.front().maximum;
        for (const auto& bound : bounds)
        {
            origin = glm::min(origin, bound.minimum);
            maximum = glm::max(maximum, bound.maximum);
        }
        const glm::vec3 extent = maximum - origin;
        if (!Finite(extent)) throw std::invalid_argument("Reflection probe index extent is too large");

        float cellSize = std::max(8.0f, std::max({extent.x, extent.y, extent.z}) / 256.0f);
        glm::uvec3 dimensions(1u);
        uint32_t cellCount = 1;
        std::vector<glm::uvec3> first(bounds.size()), last(bounds.size());
        for (;;)
        {
            // 上界点在整格边界时也有合法 cell，避免盒外边缘被地址检查丢弃。
            dimensions = glm::uvec3(glm::floor(extent / cellSize)) + glm::uvec3(1u);
            const uint64_t count = uint64_t(dimensions.x) * dimensions.y * dimensions.z;
            uint64_t references = 0;
            if (count <= MaxCells)
            {
                for (size_t i = 0; i < bounds.size(); ++i)
                {
                    first[i] = glm::min(glm::uvec3(glm::max(glm::floor((bounds[i].minimum - origin) / cellSize), glm::vec3(0.0f))), dimensions - 1u);
                    last[i] = glm::min(glm::uvec3(glm::max(glm::floor((bounds[i].maximum - origin) / cellSize), glm::vec3(0.0f))), dimensions - 1u);
                    const glm::uvec3 size = last[i] - first[i] + 1u;
                    references += uint64_t(size.x) * size.y * size.z;
                }
            }
            if (count <= MaxCells && references <= MaxReferences)
            {
                cellCount = uint32_t(count);
                break;
            }
            if (bounds.size() > MaxReferences)
                throw std::length_error("Reflection probe count exceeds index address budget");
            cellSize *= 2.0f;
        }

        std::vector<uint32_t> counts(cellCount, 0u);
        const auto visitCells = [&](size_t probe, const auto& visit)
        {
            for (uint32_t z = first[probe].z; z <= last[probe].z; ++z)
                for (uint32_t y = first[probe].y; y <= last[probe].y; ++y)
                    for (uint32_t x = first[probe].x; x <= last[probe].x; ++x)
                        visit(LinearIndex({x, y, z}, dimensions));
        };
        for (size_t i = 0; i < bounds.size(); ++i)
            visitCells(i, [&](uint32_t cell) { ++counts[cell]; });

        uint32_t offset = cellCount * 2u;
        std::vector<uint32_t> words(offset, 0u);
        uint32_t maxCandidates = 0u;
        for (uint32_t cell = 0; cell < cellCount; ++cell)
        {
            words[cell * 2u] = offset;
            words[cell * 2u + 1u] = counts[cell];
            offset += counts[cell];
            maxCandidates = std::max(maxCandidates, counts[cell]);
        }
        words.resize(offset);
        std::fill(counts.begin(), counts.end(), 0u);
        // 按原 probe ID 写入，保留相等权重的排序与 coverageSum 累加顺序。
        for (uint32_t probe = 0; probe < bounds.size(); ++probe)
            visitCells(probe, [&](uint32_t cell)
            {
                words[words[cell * 2u] + counts[cell]++] = probe;
            });

        m_Header.origin = glm::vec4(origin, 0.0f);
        m_Header.inverseCellSize = glm::vec4(glm::vec3(1.0f / cellSize), 0.0f);
        m_Header.dimensionsAndCellCount = glm::uvec4(dimensions, cellCount);
        m_Bounds = std::move(bounds);
        m_Words = std::move(words);
        m_MaxCandidateCount = maxCandidates;
        return true;
    }

    glm::uvec2 VansReflectionProbeSpatialIndex::QueryRange(const glm::vec3& position) const
    {
        const glm::vec3 gridPosition = (position - glm::vec3(m_Header.origin)) * glm::vec3(m_Header.inverseCellSize);
        const glm::uvec3 dimensions(m_Header.dimensionsAndCellCount);
        if (!Finite(gridPosition) || glm::any(glm::lessThan(gridPosition, glm::vec3(0.0f))) ||
            glm::any(glm::greaterThanEqual(gridPosition, glm::vec3(dimensions)))) return {0u, 0u};
        const uint32_t cell = LinearIndex(glm::uvec3(glm::floor(gridPosition)), dimensions);
        return {m_Words[cell * 2u], m_Words[cell * 2u + 1u]};
    }
}
