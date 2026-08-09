#include "VansParticleEmitter.h"

#include <algorithm>
#include <cmath>

namespace VansGraphics
{
void VansParticleEmitter::Initialize()
{
    m_ParticlePool.Resize(m_MaxParticles);
	m_ParticlePool.AllocSeedRandom();
	ResetSimulation();
}

void VansParticleEmitter::ResetSimulation()
{
	m_SpawnAccum = 0.0f;
	m_ParticlePool.m_AliveCount = 0;
	std::fill(m_ParticlePool.m_Flags.begin(), m_ParticlePool.m_Flags.end(), 0u);
	for (BurstConfig& burst : m_SpawnConfig.m_Bursts)
	{
		burst.cyclesDone = 0;
		burst.nextTime = -1.0f;
	}
	m_RandomState = m_RandomSeed != 0 ? m_RandomSeed : 0x9e3779b9u;
}

void VansParticleEmitter::SetRandomSeed(uint32_t seed)
{
	m_RandomSeed = seed != 0 ? seed : 0x9e3779b9u;
	m_RandomState = m_RandomSeed;
}

uint32_t VansParticleEmitter::NextRandomSeed()
{
	m_RandomState ^= m_RandomState << 13;
	m_RandomState ^= m_RandomState >> 17;
	m_RandomState ^= m_RandomState << 5;
	return m_RandomState != 0 ? m_RandomState : 0x9e3779b9u;
}

void VansParticleEmitter::EmitBurst(uint32_t count, const glm::mat4& localToWorld)
{
	SpawnParticles(count, localToWorld);
}

void VansParticleEmitter::SpawnParticles(uint32_t count, const glm::mat4& localToWorld)
{
    if (count == 0)
        return;

    const uint32_t available = m_MaxParticles - m_ParticlePool.m_AliveCount;
    count = std::min(count, available);
    if (count == 0)
        return;

    const uint32_t startIndex = m_ParticlePool.m_AliveCount;
    const uint32_t endIndex = startIndex + count;

    for (uint32_t i = startIndex; i < endIndex; ++i)
	{
        m_ParticlePool.m_Flags[i] = VansParticlePool::FLAG_ALIVE;
		m_ParticlePool.m_SeedRandom[i] = NextRandomSeed();
	}

    m_ParticlePool.m_AliveCount = endIndex;

    for (auto& module : m_InitModules)
    {
        if (module && module->m_Enabled)
            module->ExecuteInit(m_ParticlePool, startIndex, endIndex, localToWorld);
    }

    for (auto& module : m_UpdateModules)
    {
        if (module && module->m_Enabled)
            module->ExecuteInit(m_ParticlePool, startIndex, endIndex, localToWorld);
    }
}

void VansParticleEmitter::Update(float deltaTime, const glm::mat4& localToWorld)
{
    if (!m_Enabled)
        return;

    uint32_t spawnCount = 0;
    if (m_SpawnConfig.m_Type == VansSpawnType::RateOverTime)
    {
        m_SpawnAccum += m_SpawnConfig.m_Rate * deltaTime;
        spawnCount = static_cast<uint32_t>(m_SpawnAccum);
        m_SpawnAccum -= static_cast<float>(spawnCount);
    }
    else if (m_SpawnConfig.m_Type == VansSpawnType::Burst)
    {
        for (auto& burst : m_SpawnConfig.m_Bursts)
        {
            if (burst.cyclesDone < burst.cycles || burst.cycles == 0)
            {
                if (burst.nextTime < 0.0f)
                    burst.nextTime = burst.time;

                spawnCount += burst.count;
                ++burst.cyclesDone;
                burst.nextTime += burst.interval;
            }
        }
    }

    if (spawnCount > 0)
        SpawnParticles(spawnCount, localToWorld);

    for (auto& module : m_UpdateModules)
    {
        if (module && module->m_Enabled)
            module->Execute(m_ParticlePool, deltaTime, localToWorld);
    }

    uint32_t index = 0;
    while (index < m_ParticlePool.m_AliveCount)
    {
        m_ParticlePool.m_Age[index] += deltaTime;
        const float lifetime = m_ParticlePool.m_LifeTime[index];
        if (lifetime > 0.0f)
            m_ParticlePool.m_NormalizedAge[index] = m_ParticlePool.m_Age[index] / lifetime;

        if (m_ParticlePool.m_Age[index] >= lifetime)
            m_ParticlePool.SwapRemoveAt(index);
        else
            ++index;
    }
}

void VansParticleEmitter::FillInstanceData(std::vector<VansParticleInstanceData>& outBuffer) const
{
    for (uint32_t i = 0; i < m_ParticlePool.m_AliveCount; ++i)
    {
        VansParticleInstanceData instance;
        instance.m_WorldPosition = m_ParticlePool.m_Position[i];
        instance.m_Size = m_ParticlePool.m_Size[i];
        instance.m_Color = m_ParticlePool.m_Color[i];
        instance.m_Rotation = glm::radians(m_ParticlePool.m_Rotation[i]);
        instance.m_FrameIndex = m_ParticlePool.m_FrameIndex.empty() ? 0.0f : m_ParticlePool.m_FrameIndex[i];
        instance.m_Padding = glm::vec2(0.0f);
        outBuffer.push_back(instance);
    }
}
}
