#pragma once

#include "VansGILightingChangeTracker.h"
#include <cstdint>
#include <stdexcept>

namespace VansGraphics
{
    struct alignas(16) VansSSGIHistoryParameters
    {
        glm::vec4 screenSize{0.0f};
        // x 为上一成功帧表面有效性，yz 为 previous-current jitter UV；w 为光照响应范围类型。
        glm::vec4 frameParams{0.0f};
        // xyz 为局部范围；w 为本次光源变化的时域响应权重。
        glm::vec4 lightingMinimum{0.0f};
        glm::vec4 lightingMaximum{0.0f};
    };
    static_assert(sizeof(VansSSGIHistoryParameters) == 64);

    // 历史身份与光照响应由同一原生 owner 管理；仅渲染线程在提交边界推进。
    class VansSSGIHistory
    {
    public:
        void Reset() { *this = {}; }
        void Invalidate() { m_HistoryValid = false; }
        uint32_t WriteIndex() const { return m_WriteIndex; }
        bool HasHistory() const { return m_HistoryValid; }

        void NotifyLighting(const VansGILightingChanges& changes, float gatherReach)
        {
            if (!changes.Any()) return;
            m_ResponseFrames = 2u;
            m_NewResponseWeight = glm::max(m_NewResponseWeight,glm::clamp(changes.changeAmount,0.125f,1.0f));
            if (changes.global)
            {
                m_Response.global = true;
                m_Response.local.reset();
            }
            else if (!m_Response.global)
            {
                const glm::vec3 extent(gatherReach);
                const VansGILightingBounds added{changes.local->minimum - extent, changes.local->maximum + extent};
                if (!m_Response.local) m_Response.local = added;
                else
                {
                    m_Response.local->minimum = glm::min(m_Response.local->minimum, added.minimum);
                    m_Response.local->maximum = glm::max(m_Response.local->maximum, added.maximum);
                }
            }
        }

        void SetProbeLightingPending(bool pending) { m_ProbeLightingPending = pending; }

        VansSSGIHistoryParameters PrepareFrame(uint32_t width, uint32_t height, glm::vec2 jitterDelta)
        {
            if (m_Recorded) throw std::logic_error("SSGI history reused before resolving its recorded frame");
            m_Recorded = true;
            VansSSGIHistoryParameters result;
            const float w = float(width ? width : 1u), h = float(height ? height : 1u);
            result.screenSize = glm::vec4(w, h, 1.0f / w, 1.0f / h);
            result.frameParams = glm::vec4(m_HistoryValid ? 1.0f : 0.0f, jitterDelta,
                m_Response.global ? 1.0f : m_Response.local ? 2.0f : 0.0f);
            if (m_Response.local)
            {
                result.lightingMinimum = glm::vec4(m_Response.local->minimum, 0.0f);
                result.lightingMaximum = glm::vec4(m_Response.local->maximum, 0.0f);
            }
            if (m_Response.Any())
                result.lightingMinimum.w = glm::max(0.125f,glm::max(m_NewResponseWeight,m_PreviousResponseWeight));
            return result;
        }

        void ResolveRecordedFrame(bool submitted, bool sceneGraphExecuted = false)
        {
            if (!m_Recorded)
            {
                // 每帧开始的空取消不改变历史；真正执行过的图可能覆盖了出射辐射。
                if (sceneGraphExecuted) Invalidate();
                return;
            }
            if (!submitted)
            {
                m_Recorded = false;
                Invalidate();
                return;
            }
            m_Recorded = false;
            m_WriteIndex ^= 1u;
            // 出射辐射延迟一帧；突变脉冲消费后只保留一帧，不能在 probe 追赶期间持续关闭降噪。
            m_PreviousResponseWeight = m_NewResponseWeight;
            m_NewResponseWeight = 0.0f;
            m_HistoryValid = true;
            // 等待所有相关 probe 完整发布，再覆盖上一帧出射辐射的一帧延迟。
            if (m_Response.Any() && m_ProbeLightingPending) m_ResponseFrames = 2u;
            else if (m_ResponseFrames) --m_ResponseFrames;
            if (!m_ResponseFrames) m_Response = {};
        }

    private:
        uint32_t m_WriteIndex = 0u, m_ResponseFrames = 0u;
        bool m_HistoryValid = false, m_Recorded = false, m_ProbeLightingPending = false;
        float m_NewResponseWeight = 0.0f, m_PreviousResponseWeight = 0.0f;
        VansGILightingChanges m_Response;
    };
}
