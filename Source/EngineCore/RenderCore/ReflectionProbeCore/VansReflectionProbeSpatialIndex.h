#pragma once

#include "VansReflectionProbe.h"
#include <cstddef>

namespace VansGraphics
{
    struct alignas(16) ReflectionProbeIndexHeader
    {
        glm::vec4 origin = glm::vec4(0.0f);
        glm::vec4 inverseCellSize = glm::vec4(0.125f);
        glm::uvec4 dimensionsAndCellCount = glm::uvec4(0u);
    };

    // 只负责影响域的保守索引，不依赖自动排布、GI、编辑器或 GPU 资源。
    class VansReflectionProbeSpatialIndex
    {
    public:
        bool Update(const std::vector<VansReflectionProbeGPU>& probes);
        void Clear();
        const ReflectionProbeIndexHeader& GetHeader() const { return m_Header; }
        const std::vector<uint32_t>& GetWords() const { return m_Words; }
        glm::uvec2 QueryRange(const glm::vec3& position) const;
        uint32_t GetMaxCandidateCount() const { return m_MaxCandidateCount; }

    private:
        struct Bounds
        {
            glm::vec3 minimum;
            glm::vec3 maximum;
            bool operator==(const Bounds& other) const
            {
                return minimum == other.minimum && maximum == other.maximum;
            }
        };
        ReflectionProbeIndexHeader m_Header;
        std::vector<Bounds> m_Bounds;
        // 前 2*cellCount 个 uint 为 offset/count；其后为原始顺序的 probe ID。
        std::vector<uint32_t> m_Words = {0u, 0u};
        uint32_t m_MaxCandidateCount = 0;
    };
    static_assert(sizeof(ReflectionProbeIndexHeader) == 48);
}
