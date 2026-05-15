#include "VansParticleData.h"
#include <cassert>

namespace VansGraphics
{
    void VansParticlePool::Resize(uint32_t maxCount)
    {
        m_MaxCount   = maxCount;
        m_AliveCount = 0;

        // 分配所有核心属性数组
        m_Position.resize(maxCount, glm::vec3(0.f));
        m_Velocity.resize(maxCount, glm::vec3(0.f));
        m_Color.resize(maxCount, glm::vec4(1.f));
        m_Size.resize(maxCount, 1.f);
        m_Rotation.resize(maxCount, 0.f);
        m_Age.resize(maxCount, 0.f);
        m_LifeTime.resize(maxCount, 1.f);
        m_NormalizedAge.resize(maxCount, 0.f);
        m_Flags.resize(maxCount, 0u);
    }

    void VansParticlePool::AllocAngularVelocity()
    {
        if (m_AngularVelocity.size() < m_MaxCount)
            m_AngularVelocity.resize(m_MaxCount, 0.f);
    }

    void VansParticlePool::AllocInitialVelocity()
    {
        if (m_InitialVelocity.size() < m_MaxCount)
            m_InitialVelocity.resize(m_MaxCount, glm::vec3(0.f));
    }

    void VansParticlePool::AllocFrameIndex()
    {
        if (m_FrameIndex.size() < m_MaxCount)
            m_FrameIndex.resize(m_MaxCount, 0.f);
    }

    void VansParticlePool::AllocSeedRandom()
    {
        if (m_SeedRandom.size() < m_MaxCount)
            m_SeedRandom.resize(m_MaxCount, 0u);
    }

    void VansParticlePool::SwapRemoveAt(uint32_t index)
    {
        // 用最后一个存活粒子填充死亡槽位，保持数组紧凑
        assert(m_AliveCount > 0);
        uint32_t last = m_AliveCount - 1;

        if (index != last)
        {
            m_Position[index]      = m_Position[last];
            m_Velocity[index]      = m_Velocity[last];
            m_Color[index]         = m_Color[last];
            m_Size[index]          = m_Size[last];
            m_Rotation[index]      = m_Rotation[last];
            m_Age[index]           = m_Age[last];
            m_LifeTime[index]      = m_LifeTime[last];
            m_NormalizedAge[index] = m_NormalizedAge[last];
            m_Flags[index]         = m_Flags[last];

            // 可选扩展属性（按需复制）
            if (!m_AngularVelocity.empty())
                m_AngularVelocity[index] = m_AngularVelocity[last];
            if (!m_InitialVelocity.empty())
                m_InitialVelocity[index] = m_InitialVelocity[last];
            if (!m_FrameIndex.empty())
                m_FrameIndex[index] = m_FrameIndex[last];
            if (!m_SeedRandom.empty())
                m_SeedRandom[index] = m_SeedRandom[last];
        }

        --m_AliveCount;
    }

} // namespace VansGraphics
