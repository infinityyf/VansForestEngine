#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
    // 世界格点决定物理槽身份；滚动只替换新进入的格点，不移动重叠区域的 atlas 数据。
    class VansGIScrollingGrid
    {
    public:
        bool Initialize(glm::uvec3 dimensions, float spacing, glm::vec3 center, std::string& error)
        {
            if (glm::any(glm::lessThan(dimensions, glm::uvec3(8))) ||
                glm::any(glm::greaterThan(dimensions, glm::uvec3(256))) ||
                !std::isfinite(spacing) || spacing <= 0.0f)
            { error = "GI scrolling grid requires 8..256 cells per axis and finite positive spacing"; return false; }
            glm::ivec3 origin;
            if (!ResolveOrigin(dimensions, spacing, center, origin, error)) return false;
            m_Dimensions = dimensions; m_Spacing = spacing; m_Center = center; m_Origin = origin;
            error.clear(); return true;
        }

        bool Move(glm::vec3 center, std::vector<uint32_t>& entering, std::string& error)
        {
            glm::ivec3 next;
            if (m_Spacing <= 0.0f || !ResolveOrigin(m_Dimensions, m_Spacing, center, next, error))
            { if (m_Spacing <= 0.0f) error = "GI scrolling grid is not initialized"; return false; }
            entering.clear();
            const glm::ivec3 dimensions(m_Dimensions);
            const glm::ivec3 low = glm::clamp(m_Origin - next, glm::ivec3(0), dimensions);
            const glm::ivec3 high = glm::clamp(m_Origin + dimensions - next, glm::ivec3(0), dimensions);
            m_Origin = next; m_Center = center;
            if (glm::any(glm::lessThanEqual(high, low)))
            {
                entering.resize(ProbeCount());
                for (uint32_t id = 0; id < entering.size(); ++id) entering[id] = id;
            }
            else
            {
                const uint32_t overlap = uint32_t(high.x-low.x) * uint32_t(high.y-low.y) * uint32_t(high.z-low.z);
                entering.reserve(ProbeCount() - overlap);
                // 六个不相交的盒子，仅枚举进入的平面/边/角，成本与修改量一致。
                auto box = [&](glm::ivec3 a, glm::ivec3 b)
                {
                    for (int z = a.z; z < b.z; ++z) for (int y = a.y; y < b.y; ++y) for (int x = a.x; x < b.x; ++x)
                        entering.push_back(PhysicalIndex(next + glm::ivec3(x, y, z)));
                };
                box({0,0,0}, {low.x,dimensions.y,dimensions.z});
                box({high.x,0,0}, dimensions);
                box({low.x,0,0}, {high.x,low.y,dimensions.z});
                box({low.x,high.y,0}, {high.x,dimensions.y,dimensions.z});
                box({low.x,low.y,0}, {high.x,high.y,low.z});
                box({low.x,low.y,high.z}, {high.x,high.y,dimensions.z});
                std::sort(entering.begin(), entering.end());
            }
            error.clear(); return true;
        }

        uint32_t ProbeCount() const { return m_Dimensions.x * m_Dimensions.y * m_Dimensions.z; }
        glm::uvec3 Dimensions() const { return m_Dimensions; }
        glm::ivec3 Origin() const { return m_Origin; }
        glm::uvec3 RingOffset() const { return Modulo(m_Origin); }
        glm::vec3 Minimum() const { return glm::vec3(m_Origin) * m_Spacing; }
        glm::vec3 Center() const { return m_Center; }
        float Spacing() const { return m_Spacing; }
        // 连续包络内缩两个格子，滚动前后均处于实际数据范围内，不能跟随格点跳变。
        glm::vec3 BlendSize() const { return (glm::vec3(m_Dimensions) - 4.0f) * m_Spacing; }
        glm::vec3 BlendMinimum() const { return m_Center - BlendSize() * 0.5f; }
        uint32_t PhysicalIndex(glm::ivec3 worldCell) const
        {
            const auto p = Modulo(worldCell);
            return (p.z * m_Dimensions.y + p.y) * m_Dimensions.x + p.x;
        }
        glm::ivec3 WorldCell(uint32_t physical) const
        {
            const glm::ivec3 p(physical % m_Dimensions.x, (physical / m_Dimensions.x) % m_Dimensions.y,
                physical / (m_Dimensions.x * m_Dimensions.y));
            return m_Origin + glm::ivec3(Modulo(p - glm::ivec3(RingOffset())));
        }
        glm::vec3 Position(uint32_t physical) const { return (glm::vec3(WorldCell(physical)) + 0.5f) * m_Spacing; }

    private:
        static bool ResolveOrigin(glm::uvec3 dimensions, float spacing, glm::vec3 center,
            glm::ivec3& origin, std::string& error)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const double cell = std::floor(double(center[axis]) / double(spacing));
                // 先限制范围再转整数，同时保留单精度着色器分辨子格位置的余量。
                if (!std::isfinite(center[axis]) || !std::isfinite(cell) || std::abs(cell) + dimensions[axis] > 1048576.0 ||
                    !std::isfinite(double(center[axis]) + double(dimensions[axis]) * spacing) ||
                    std::abs(double(center[axis])) + double(dimensions[axis]) * spacing > 3.0e38)
                { error = "GI scrolling grid exceeds finite world-coordinate precision; rebase the world origin"; return false; }
                origin[axis] = int32_t(cell) - int32_t(dimensions[axis] / 2u);
            }
            return true;
        }
        glm::uvec3 Modulo(glm::ivec3 value) const
        {
            const auto dimensions = glm::ivec3(m_Dimensions);
            return glm::uvec3((value % dimensions + dimensions) % dimensions);
        }
        glm::uvec3 m_Dimensions{0};
        glm::ivec3 m_Origin{0};
        glm::vec3 m_Center{0};
        float m_Spacing = 0.0f;
    };
}
