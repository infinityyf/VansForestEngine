
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VansClothProfile.h"
#include "../Util/VansLog.h"
#include <limits>
#include <algorithm>

namespace VansEngine
{
    void VansClothProfile::ResetToDefaults()
    {
        *this = VansClothProfile{};
    }

    // =========================================================================

    // =========================================================================

    std::vector<uint32_t> VansClothProfile::ResolveIndices(
        const std::vector<float>& rawPosFloat4,
        int vertexCount) const
    {
        std::vector<uint32_t> result;
        result.reserve(m_PinnedLocalPositions.size());
        const float fallbackTolerance = std::max(m_PinnedMatchTolerance, 0.5f);

        for (const glm::vec3& pinnedPos : m_PinnedLocalPositions)
        {
            float    bestDist = std::numeric_limits<float>::max();
            int      bestIdx  = -1;

            for (int v = 0; v < vertexCount; ++v)
            {


                float vx = rawPosFloat4[v * 8 + 0];
                float vy = rawPosFloat4[v * 8 + 1];
                float vz = rawPosFloat4[v * 8 + 2];

                float dx   = vx - pinnedPos.x;
                float dy   = vy - pinnedPos.y;
                float dz   = vz - pinnedPos.z;
                float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestIdx  = v;
                }
            }

            if (bestIdx >= 0 && bestDist <= m_PinnedMatchTolerance)
            {
                result.push_back(static_cast<uint32_t>(bestIdx));
            }
            else if (bestIdx >= 0 && bestDist <= fallbackTolerance)
            {
                result.push_back(static_cast<uint32_t>(bestIdx));
                VANS_LOG_WARN("[VansClothProfile] ResolveIndices: ????????????("
                              << pinnedPos.x << ", " << pinnedPos.y << ", " << pinnedPos.z
                              << ") ??????? " << bestIdx
                              << "???=" << bestDist
                              << "?????=" << m_PinnedMatchTolerance
                              << "?????=" << fallbackTolerance);
            }
            else
            {
                VANS_LOG_WARN("[VansClothProfile] ResolveIndices: pinned point ("
                              << pinnedPos.x << ", " << pinnedPos.y << ", " << pinnedPos.z
                              << ") has no matching vertex. tolerance=" << m_PinnedMatchTolerance
                              << ", bestDistance=" << bestDist);
            }
        }

        return result;
    }

    glm::mat4 VansClothProfile::GetSkeletonOffsetMatrix() const
    {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), m_SkeletonOffset.m_Position);
        glm::mat4 R = glm::mat4(1.0f);
        R = glm::rotate(R, glm::radians(m_SkeletonOffset.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        R = glm::rotate(R, glm::radians(m_SkeletonOffset.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        R = glm::rotate(R, glm::radians(m_SkeletonOffset.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 S = glm::scale(glm::mat4(1.0f), m_SkeletonOffset.m_Scale);
        return T * R * S;
    }
}

