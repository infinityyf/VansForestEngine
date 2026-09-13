#include "VansParticleEmitterRuntime.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>

namespace VansGraphics
{
namespace
{
bool Finite(const glm::vec3& p)
{ return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
glm::mat4 SampleTransform(const glm::mat4& a, const glm::mat4& b, float t)
{
    glm::mat4 result;
    for (int column=0; column<4; ++column) result[column] = glm::mix(a[column], b[column], t);
    return result;
}
}
VansParticleEmitterRuntime::VansParticleEmitterRuntime(const VansParticleEmitter& definition)
    : m_Enabled(definition.m_Enabled), m_Definition(definition)
{
    m_ParticlePool.Resize(definition.m_MaxParticles);
    m_ParticlePool.AllocSeedRandom();
    if (IsRibbon()) m_ParticlePool.AllocRibbon();
    ResetSimulation();
}
void VansParticleEmitterRuntime::ResetSimulation()
{
    m_ParticlePool.m_AliveCount = 0;
    m_AnchorValid = false;
    m_RootAttached = false;
    m_RandomState = m_RandomSeed;
    m_DroppedSpawns = m_BreakCount = 0;
    ResetEmission();
}
void VansParticleEmitterRuntime::ResetEmission()
{
    DetachRoot();
    m_SpawnAccum = 0;
    m_EmissionTime = 0;
    m_BurstStates.assign(m_Definition.m_SpawnConfig.m_Bursts.size(), {});
    ++m_RibbonId;
    m_RootAttached = IsRibbon() && m_Definition.m_RendererConfig.m_Ribbon.rootMode == VansRibbonRootMode::FollowSource;
}
void VansParticleEmitterRuntime::SetRandomSeed(uint32_t seed)
{ m_RandomSeed = m_RandomState = seed ? seed : 0x9e3779b9u; }
uint32_t VansParticleEmitterRuntime::NextRandomSeed()
{
    m_RandomState ^= m_RandomState << 13;
    m_RandomState ^= m_RandomState >> 17;
    m_RandomState ^= m_RandomState << 5;
    return m_RandomState ? m_RandomState : 0x9e3779b9u;
}
void VansParticleEmitterRuntime::SetAnchor(const glm::mat4& transform)
{
    if (!m_AnchorValid && Finite(glm::vec3(transform[3])))
        ResetAnchor(transform);
}
void VansParticleEmitterRuntime::ResetAnchor(const glm::mat4& transform)
{
    m_Anchor = transform; m_RenderRootPosition = glm::vec3(transform[3]);
    m_AnchorValid = Finite(m_RenderRootPosition);
}
uint32_t VansParticleEmitterRuntime::SpawnParticles(uint32_t requested, const glm::mat4& transform,
    float remainingTime, bool detachedRoot)
{
    const auto firstSequence = m_Sequence + 1;
    m_Sequence += requested;
    auto& pool = m_ParticlePool;
    const uint32_t reserve = IsRibbon() && !detachedRoot
        && m_Definition.m_RendererConfig.m_Ribbon.rootMode == VansRibbonRootMode::FollowSource ? 1u : 0u;
    const uint32_t limit = pool.m_MaxCount - reserve;
    const uint32_t count = std::min(requested, limit > pool.m_AliveCount ? limit - pool.m_AliveCount : 0u);
    m_DroppedSpawns += requested - count;
    const uint32_t start = pool.m_AliveCount, end = start + count;
    for (uint32_t i=start; i<end; ++i)
    {
        pool.m_Flags[i] = VansParticlePool::FLAG_ALIVE;
        pool.m_SeedRandom[i] = NextRandomSeed();
        pool.m_Position[i] = glm::vec3(transform[3]);
        pool.m_Velocity[i] = glm::vec3(0);
        pool.m_Color[i] = glm::vec4(1);
        pool.m_Size[i] = pool.m_InitialSize[i] = 1;
        pool.m_Rotation[i] = pool.m_Age[i] = pool.m_NormalizedAge[i] = 0;
        pool.m_LifeTime[i] = 1;
        pool.m_StepDelta[i] = remainingTime;
        if (!pool.m_RibbonId.empty())
        { pool.m_RibbonId[i] = m_RibbonId; pool.m_SpawnSequence[i] = firstSequence + i-start; }
        if (!pool.m_AngularVelocity.empty()) pool.m_AngularVelocity[i] = 0;
        if (!pool.m_FrameIndex.empty()) pool.m_FrameIndex[i] = 0;
        if (!pool.m_InitialVelocity.empty()) pool.m_InitialVelocity[i] = glm::vec3(0);
    }
    pool.m_AliveCount = end;
    for (const auto& module : m_Definition.m_InitModules)
        if (module && module->m_Enabled) module->ExecuteInit(pool, start, end, transform);
    for (uint32_t i=start; i<end; ++i) pool.m_InitialSize[i] = pool.m_Size[i];
    for (const auto& module : m_Definition.m_UpdateModules)
        if (module && module->m_Enabled) module->ExecuteInit(pool, start, end, transform);
    return count;
}
void VansParticleEmitterRuntime::EmitBurst(uint32_t count, const glm::mat4& transform)
{
    SetAnchor(transform);
    SpawnParticles(count, transform, 0);
}
void VansParticleEmitterRuntime::DetachRoot()
{
    if (!m_RootAttached) return;
    m_RootAttached = false;
    auto& pool = m_ParticlePool;
    if (!m_AnchorValid || !pool.m_AliveCount) return;
    glm::vec3 velocity(0);
    uint64_t newest = 0;
    for (uint32_t i=0; i<pool.m_AliveCount; ++i)
        if (pool.m_RibbonId[i] == m_RibbonId && pool.m_SpawnSequence[i] > newest)
        { newest = pool.m_SpawnSequence[i]; velocity = pool.m_Velocity[i]; }
    if (!newest) return;
    const auto index = pool.m_AliveCount;
    auto anchor = m_Anchor; anchor[3] = glm::vec4(m_RenderRootPosition,1);
    if (SpawnParticles(1, anchor, 0, true))
    {
        pool.m_Velocity[index] = velocity;
        pool.m_Size[index] = pool.m_InitialSize[index] = m_Definition.m_RendererConfig.m_Ribbon.rootWidth;
        pool.m_Color[index] = m_Definition.m_RendererConfig.m_Ribbon.rootColor;
    }
}
void VansParticleEmitterRuntime::StopEmitting(bool forceDetach)
{
    if (forceDetach || m_Definition.m_RendererConfig.m_Ribbon.stopAttachment == VansRibbonStopAttachment::DetachOnStop)
        DetachRoot();
}
void VansParticleEmitterRuntime::ResumeEmission()
{
    // 已脱离的旧尾不能重新接回源；仍附着时保持原来的连接身份。
    if (!IsRibbon() || m_RootAttached) return;
    ++m_RibbonId;
    m_RootAttached = m_Definition.m_RendererConfig.m_Ribbon.rootMode == VansRibbonRootMode::FollowSource;
}
void VansParticleEmitterRuntime::Skip(float seconds, const glm::mat4& transform, bool emitting)
{
    emitting = emitting && m_Enabled;
    DetachRoot();
    ++m_RibbonId;
    ++m_Sequence;
    ++m_BreakCount;
    auto& pool = m_ParticlePool;
    for (uint32_t i=0; i<pool.m_AliveCount;)
    {
        pool.m_Age[i] += seconds;
        if (pool.m_Age[i] >= pool.m_LifeTime[i]) pool.SwapRemoveAt(i);
        else { pool.m_NormalizedAge[i] = pool.m_Age[i] / pool.m_LifeTime[i]; ++i; }
    }
    if (emitting && m_Definition.m_SpawnConfig.m_Type == VansSpawnType::RateOverTime)
    {
        const double due = m_SpawnAccum + m_Definition.m_SpawnConfig.m_Rate * seconds;
        const auto dropped = static_cast<uint64_t>(due);
        m_DroppedSpawns += dropped; m_Sequence += dropped; m_SpawnAccum = due-dropped;
    }
    if (emitting)
    {
        m_EmissionTime += seconds;
        for (std::size_t i=0; i<m_Definition.m_SpawnConfig.m_Bursts.size(); ++i)
        {
            const auto& burst = m_Definition.m_SpawnConfig.m_Bursts[i];
            uint64_t due = burst.interval > 0 && m_EmissionTime >= burst.time
                ? uint64_t(std::floor((m_EmissionTime-burst.time)/burst.interval))+1 : (m_EmissionTime >= burst.time ? 1u : 0u);
            if (burst.cycles) due = std::min<uint64_t>(due, burst.cycles);
            if (due > m_BurstStates[i].cyclesDone)
            { m_DroppedSpawns += (due-m_BurstStates[i].cyclesDone)*burst.count; m_BurstStates[i].cyclesDone = due; }
        }
    }
    m_Anchor = transform; m_AnchorValid = Finite(glm::vec3(transform[3]));
    m_RootAttached = emitting && IsRibbon() && m_Definition.m_RendererConfig.m_Ribbon.rootMode == VansRibbonRootMode::FollowSource;
}
void VansParticleEmitterRuntime::Update(float deltaTime, const glm::mat4& transform, bool emitting)
{
    if (!std::isfinite(deltaTime) || deltaTime < 0) return;
    emitting = emitting && m_Enabled;
    SetAnchor(transform);
    if (!Finite(glm::vec3(transform[3]))) { StopEmitting(true); emitting = false; }
    const glm::mat4 current = Finite(glm::vec3(transform[3])) ? transform : m_Anchor;
    if (IsRibbon() && glm::distance(glm::vec3(m_Anchor[3]), glm::vec3(current[3])) > m_Definition.m_RendererConfig.m_Ribbon.maxSegmentLength)
    {
        DetachRoot(); ++m_RibbonId; ++m_Sequence; ++m_BreakCount;
        m_Anchor = current;
        m_RootAttached = emitting && m_Definition.m_RendererConfig.m_Ribbon.rootMode == VansRibbonRootMode::FollowSource;
    }
    auto& pool = m_ParticlePool;
    std::fill_n(pool.m_StepDelta.begin(), pool.m_AliveCount, deltaTime);
    const auto& spawn = m_Definition.m_SpawnConfig;
    if (emitting && spawn.m_Type == VansSpawnType::RateOverTime && spawn.m_Rate > 0)
    {
        const double previous = m_SpawnAccum;
        const double amount = previous + spawn.m_Rate * double(deltaTime);
        const auto count = static_cast<uint32_t>(std::floor(amount + 1.0e-7));
        m_SpawnAccum = std::max(0.0, amount-count);
        const uint32_t acceptedBudget = std::min(count, pool.m_MaxCount-pool.m_AliveCount);
        for (uint32_t point=0; point<acceptedBudget; ++point)
        {
            const float born = std::clamp(float((point + 1.0-previous)/spawn.m_Rate), 0.0f, deltaTime);
            SpawnParticles(1, SampleTransform(m_Anchor, current, deltaTime > 0 ? born/deltaTime : 1), deltaTime-born);
        }
        m_Sequence += count-acceptedBudget;
        m_DroppedSpawns += count-acceptedBudget;
    }
    else if (emitting && spawn.m_Type == VansSpawnType::Burst)
    {
        for (std::size_t i=0; i<spawn.m_Bursts.size(); ++i)
        {
            const auto& burst = spawn.m_Bursts[i];
            auto& state = m_BurstStates[i];
            const double end = m_EmissionTime + deltaTime;
            uint64_t total = burst.interval > 0 && end >= burst.time
                ? uint64_t(std::floor((end-burst.time)/burst.interval + 1.0e-7))+1 : (end >= burst.time ? 1u : 0u);
            if (burst.cycles) total = std::min<uint64_t>(total, burst.cycles);
            while (state.cyclesDone < total && pool.m_AliveCount < pool.m_MaxCount)
            {
                const float born = std::clamp(float(burst.time + state.cyclesDone * double(burst.interval)-m_EmissionTime), 0.0f, deltaTime);
                SpawnParticles(burst.count, SampleTransform(m_Anchor, current, deltaTime > 0 ? born/deltaTime : 1), deltaTime-born);
                ++state.cyclesDone;
            }
            if (total > state.cyclesDone)
            { m_DroppedSpawns += (total-state.cyclesDone)*burst.count; state.cyclesDone = total; }
        }
    }
    if (emitting) m_EmissionTime += deltaTime;
    // 年龄先求到本步结束，曲线只计算本次值，不逐帧累乘。
    for (uint32_t i=0; i<pool.m_AliveCount;)
    {
        pool.m_Age[i] += pool.m_StepDelta[i];
        if (pool.m_Age[i] >= pool.m_LifeTime[i] || !Finite(pool.m_Position[i])) pool.SwapRemoveAt(i);
        else { pool.m_NormalizedAge[i] = pool.m_Age[i]/pool.m_LifeTime[i]; ++i; }
    }
    for (const auto& module : m_Definition.m_UpdateModules)
        if (module && module->m_Enabled) module->Execute(pool, deltaTime, current);
    for (uint32_t i=0; i<pool.m_AliveCount; ++i)
        pool.m_Position[i] += pool.m_Velocity[i] * pool.m_StepDelta[i];
    m_Anchor = current;
}
void VansParticleEmitterRuntime::FillInstanceData(std::vector<VansParticleInstanceData>& out) const
{
    std::vector<uint32_t> order(m_ParticlePool.m_AliveCount);
    std::iota(order.begin(), order.end(), 0u);
    const auto sort = m_Definition.m_RendererConfig.m_SortMode;
    if (sort == VansParticleSortMode::OldestFirst || sort == VansParticleSortMode::NewestFirst)
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            return sort == VansParticleSortMode::OldestFirst ? m_ParticlePool.m_Age[a] > m_ParticlePool.m_Age[b] : m_ParticlePool.m_Age[a] < m_ParticlePool.m_Age[b];
        });
    for (const uint32_t i : order)
    {
        VansParticleInstanceData instance{};
        instance.m_WorldPosition = m_ParticlePool.m_Position[i];
        instance.m_Size = m_ParticlePool.m_Size[i];
        instance.m_Color = m_ParticlePool.m_Color[i];
        instance.m_Rotation = glm::radians(m_ParticlePool.m_Rotation[i]);
        instance.m_FrameIndex = m_ParticlePool.m_FrameIndex.empty() ? 0 : m_ParticlePool.m_FrameIndex[i];
        out.push_back(instance);
    }
}
void VansParticleEmitterRuntime::FillRibbonData(std::vector<VansParticleRibbonStrip>& strips) const
{
    if (!IsRibbon() || !m_Enabled) return;
    const auto& pool = m_ParticlePool;
    const auto& config = m_Definition.m_RendererConfig.m_Ribbon;
    std::vector<uint32_t> order(pool.m_AliveCount);
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return pool.m_RibbonId[a] != pool.m_RibbonId[b] ? pool.m_RibbonId[a] < pool.m_RibbonId[b]
            : pool.m_SpawnSequence[a] > pool.m_SpawnSequence[b];
    });
    VansParticleRibbonStrip strip;
    auto flush = [&] {
        if (strip.points.size() > 1)
        {
            // 按世界距离柔化自由端，长度变化不移动幸存点的 UV。
            if (config.tipFadeDistance > 0)
            {
                float distance = 0;
                for (size_t i=strip.points.size(); i-- > 0;)
                {
                    if (i+1 < strip.points.size()) distance += glm::distance(strip.points[i].position,strip.points[i+1].position);
                    strip.points[i].color.a *= std::min(distance/config.tipFadeDistance,1.0f);
                }
            }
            strips.push_back(std::move(strip));
        }
        strip = {};
    };
    for (const auto index : order)
    {
        const uint64_t id = pool.m_RibbonId[index], sequence = pool.m_SpawnSequence[index];
        const auto position = pool.m_Position[index];
        if (!Finite(position)) { flush(); continue; }
        if (!strip.points.empty() && (strip.ribbonId != id || strip.points.back().sequence != sequence+1
            || glm::distance(strip.points.back().position, position) > config.maxSegmentLength)) flush();
        if (strip.points.empty())
        {
            strip.ribbonId = id;
            if (id == m_RibbonId && m_RootAttached && sequence == m_Sequence && pool.m_AliveCount < pool.m_MaxCount
                && glm::distance(m_RenderRootPosition, position) <= config.maxSegmentLength)
            {
                strip.hasSourceRoot = true;
                strip.points.push_back({m_RenderRootPosition, config.rootWidth, config.rootColor, 0, sequence+1});
            }
        }
        strip.points.push_back({position, pool.m_Size[index], pool.m_Color[index], pool.m_Age[index]*config.uvFlowSpeed, sequence});
    }
    flush();
}
void VansParticleEmitterRuntime::FillVolumetricInstanceData(
    std::vector<VansVolumetricParticleInstanceData>& outBuffer) const
{
    const VansParticleVolumetricConfig& volumetric = m_Definition.m_RendererConfig.m_Volumetric;
    if (!volumetric.m_Enabled)
        return;

    const glm::vec3 albedo = glm::clamp(
        volumetric.m_SingleScatteringAlbedo, glm::vec3(0.0f), glm::vec3(1.0f));
    const glm::vec3 emissive = glm::max(volumetric.m_EmissivePerMeter, glm::vec3(0.0f));
    for (uint32_t i = 0; i < m_ParticlePool.m_AliveCount; ++i)
    {
        const glm::vec4 color = m_ParticlePool.m_Color[i];
        const float radius = 0.5f * std::max(m_ParticlePool.m_Size[i], 0.0f) *
            std::max(volumetric.m_RadiusScale, 0.0f);
        const float density = std::max(color.a, 0.0f) *
            std::max(volumetric.m_DensityMultiplier, 0.0f);
        if (radius <= 1.0e-5f || density <= 1.0e-7f)
            continue;

        VansVolumetricParticleInstanceData instance{};
        instance.m_WorldPositionRadius = glm::vec4(m_ParticlePool.m_Position[i], radius);
        instance.m_ScatteringAlbedoExtinction = glm::vec4(
            albedo * glm::max(glm::vec3(color), glm::vec3(0.0f)),
            std::max(volumetric.m_ExtinctionPerMeter, 0.0f) * density);
        instance.m_EmissiveAnisotropy = glm::vec4(
            emissive * glm::max(glm::vec3(color), glm::vec3(0.0f)),
            glm::clamp(volumetric.m_Anisotropy, -0.9f, 0.9f));
        instance.m_LightingEdgeCloud = glm::vec4(
            std::max(volumetric.m_DirectLightingScale, 0.0f),
            std::max(volumetric.m_SkyLightingScale, 0.0f),
            glm::clamp(volumetric.m_EdgeSoftness, 0.001f, 1.0f),
            volumetric.m_ReceiveCloudShadows ? 1.0f : 0.0f);
        instance.m_DistanceAndPadding.x =
            std::max(volumetric.m_MaxDistanceMeters, 0.01f);
        instance.m_Metadata.x = m_ParticlePool.m_SeedRandom.empty()
            ? i + 1u : m_ParticlePool.m_SeedRandom[i];
        instance.m_Metadata.w = volumetric.m_InjectionPriority;
        outBuffer.push_back(instance);
    }
}
}
