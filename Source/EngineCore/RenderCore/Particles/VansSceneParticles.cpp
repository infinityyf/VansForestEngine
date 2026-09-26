#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "../VansScene.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../RuntimeCore/VansThreadContract.h"
#include "../../GameplayActionAdapters/VFX/VansVFXActionService.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../Util/VansLog.h"
#include <algorithm>

namespace VansGraphics
{
bool VansScene::ResolveParticleSource(const ParticleSourceBinding& binding,glm::mat4& world) const
{
    if (!m_RuntimeWorld || !m_RuntimeWorld->IsAlive(binding.sourceEntity)
        || !m_RuntimeWorld->Entities().IsHierarchyActive(binding.sourceEntity)) return false;
    const auto* storage = m_RuntimeWorld->FindStorage<Vans::VansRuntimeTransformComponent>(
        Vans::VansRuntimeComponentType_Transform);
    const auto* transform = storage ? storage->Get(storage->FindFirstOwnedBy(binding.sourceEntity)) : nullptr;
    if (!transform || !Vans::VansTransformStore::IsAllocated(transform->transformStoreId)) return false;
    world = Vans::VansTransformStore::Read(transform->transformStoreId).GetModelMatrix();
    if (binding.source.IsAnchor())
    {
        glm::mat4 model(1); std::uint64_t revision = 0;
        if (!m_SkeletonAnchorRegistry.ResolveModelSpaceTransform(binding.anchor,model,revision)) return false;
        world *= model;
    }
    return true;
}
void VansScene::RetireCompletedParticlePulses()
{
    m_ParticleSources.erase(std::remove_if(m_ParticleSources.begin(),m_ParticleSources.end(),[&](const auto& binding) {
        if (!binding.autoRelease || binding.pendingStart) return false;
        const auto* runtime = m_ParticleManager.Resolve(binding.instance);
        if (runtime && !runtime->IsFinished()) return false;
        if (runtime) m_ParticleManager.Destroy(binding.instance);
        return true;
    }),m_ParticleSources.end());
}
void VansScene::PrepareParticleSources()
{
    RetireCompletedParticlePulses();
    for (auto& binding : m_ParticleSources)
    {
        binding.pendingStart = false;
        if (binding.detached) continue;
        const auto* runtime = m_ParticleManager.Resolve(binding.instance);
        if (!runtime) continue;
        glm::mat4 world(1);
        if (!m_RuntimeWorld->IsAlive(binding.owner) || !m_RuntimeWorld->Entities().IsHierarchyActive(binding.owner)
            || !ResolveParticleSource(binding,world))
        {
            m_ParticleManager.Queue(binding.instance,VansParticleControl::DetachAndDrain);
            binding.detached = true;
        }
        else
        {
            binding.lastSourcePosition = glm::vec3(world[3]);
            m_ParticleManager.SetOwnerWorldTransform(binding.instance,world);
        }
    }
}
Vans::VansVFXSceneBackend VansScene::MakeVFXSceneBackend()
{
    Vans::VansVFXSceneBackend backend;
    const auto play = [this](const Vans::VansVFXSpawnRequest& request,bool pulse,std::string& error) -> Vans::VansGenerationHandle {
        VANS_ASSERT_MAIN_THREAD();
        if (!m_RuntimeWorld || !m_RuntimeWorld->IsAlive(request.owner)
            || !m_RuntimeWorld->Entities().IsHierarchyActive(request.owner)) { error = "VFX owner is unavailable"; return {}; }
        RetireCompletedParticlePulses();
        ParticleSourceBinding binding;
        binding.owner = request.owner; binding.effect = request.effect; binding.source = request.source;
        binding.autoRelease = pulse;
        binding.sourceEntity = m_RuntimeWorld->Entities().FindByGuid(request.source.entityGuid.ToString());
        if (binding.source.IsAnchor())
        {
            const auto component = m_RuntimeWorld->FindComponentByGuid(binding.source.animationComponentGuid.ToString(),Vans::VansRuntimeComponentType_Animation);
            const auto* storage = m_RuntimeWorld->FindStorage<Vans::VansRuntimeAnimationComponent>(Vans::VansRuntimeComponentType_Animation);
            const auto* animation = storage ? storage->Get(component) : nullptr;
            const auto* header = storage ? storage->GetHeader(component) : nullptr;
            if (!animation || !header || header->owner != binding.sourceEntity) { error = "VFX source animation does not belong to its entity"; return {}; }
            binding.anchor = m_SkeletonAnchorRegistry.MakeAnchorHandle({animation->skeletonInstanceId,animation->skeletonInstanceGeneration},
                binding.source.kind == Vans::VansSceneParentKind::Bone ? Vans::VansTransformAnchorKind::Bone : Vans::VansTransformAnchorKind::Socket,
                binding.source.anchorGuid.ToString());
        }
        glm::mat4 world(1);
        if (!ResolveParticleSource(binding,world)) { error = "VFX source cannot resolve its current pose"; return {}; }
        const auto sameSource = [&](const auto& active) {
            return active.owner == binding.owner && active.effect == binding.effect && active.sourceEntity == binding.sourceEntity
                && active.source.kind == binding.source.kind && active.source.anchorGuid == binding.source.anchorGuid
                && active.source.animationComponentGuid == binding.source.animationComponentGuid
                && active.anchor.instanceId == binding.anchor.instanceId && active.anchor.instanceGeneration == binding.anchor.instanceGeneration;
        };
        if (pulse)
        {
            for (auto& active : m_ParticleSources)
            {
                if (!active.autoRelease || active.detached || !sameSource(active)) continue;
                const auto* runtime = m_ParticleManager.Resolve(active.instance);
                if (!runtime || (!active.pendingStart && runtime->IsFinished())) continue;
                if (!m_ParticleManager.Queue(active.instance,VansParticleControl::RefreshEmission))
                { error = "VFX pulse could not refresh its active instance"; return {}; }
                VANS_LOG("[VFX] Refresh effect=" << request.effect.ToString() << " instance=" << active.instance.index
                    << ":" << active.instance.generation << " playTime=" << runtime->GetPlayTime()
                    << " alive=" << runtime->AliveInstanceCount());
                return active.instance;
            }
        }
        const auto concurrent = std::count_if(m_ParticleSources.begin(),m_ParticleSources.end(),sameSource);
        if (concurrent >= request.maxConcurrentPerSource) { error = "VFX source concurrency limit reached"; return {}; }
        const auto asset = Vans::VansProjectManager::Get().GetAssetObjectRepository().ResolveLatest<VansParticleAsset>(request.effect);
        if (!asset) { error = "VFX particle asset is not loaded in memory"; return {}; }
        if (pulse && asset->m_Loop) { error = "VFX.Pulse requires a finite non-looping effect"; return {}; }
        binding.instance = m_ParticleManager.Create(asset);
        if (!binding.instance.IsValid()) { error = "Particle scene capacity exhausted"; return {}; }
        binding.lastSourcePosition = glm::vec3(world[3]);
        if (!m_ParticleManager.SetOwnerWorldTransform(binding.instance,world))
        {
            m_ParticleManager.Destroy(binding.instance);
            error = "Particle source transform could not be initialized";
            return {};
        }
        m_ParticleManager.Queue(binding.instance,VansParticleControl::Play);
        m_ParticleManager.Queue(binding.instance,VansParticleControl::DeferFirstUpdate);
        m_ParticleSources.push_back(binding);
        VANS_LOG("[VFX] Spawn effect=" << request.effect.ToString() << " source=" << binding.source.entityGuid.ToString()
            << " instance=" << binding.instance.index << ":" << binding.instance.generation);
        return binding.instance;
    };
    backend.spawn = [play](const auto& request,std::string& error) { return play(request,false,error); };
    backend.pulse = [play](const auto& request,std::string& error) { return play(request,true,error).IsValid(); };
    backend.stop = [this](auto instance,Vans::VansVFXStopMode mode) {
        if (mode == Vans::VansVFXStopMode::DetachAndDrain)
            for (auto& binding : m_ParticleSources) if (binding.instance == instance) binding.detached = true;
        return m_ParticleManager.Queue(instance,mode == Vans::VansVFXStopMode::Immediate ? VansParticleControl::Stop
            : mode == Vans::VansVFXStopMode::DetachAndDrain ? VansParticleControl::DetachAndDrain : VansParticleControl::StopEmitting);
    };
    backend.finished = [this](auto instance) { const auto* runtime = m_ParticleManager.Resolve(instance); return !runtime || runtime->IsFinished(); };
    backend.destroy = [this](auto instance) {
        m_ParticleSources.erase(std::remove_if(m_ParticleSources.begin(),m_ParticleSources.end(),[&](const auto& binding) { return binding.instance == instance; }),m_ParticleSources.end());
        return !m_ParticleManager.Resolve(instance) || m_ParticleManager.Destroy(instance);
    };
    return backend;
}
VansSceneParticleDiagnostics VansScene::CaptureParticleDiagnostics(bool includePoints) const
{
    VANS_ASSERT_MAIN_THREAD();
    VansSceneParticleDiagnostics result;
    result.activeInstances = m_ParticleManager.ActiveCount(); result.pointCapacity = m_ParticleManager.PointCapacity();
    result.rejectedInstances = m_ParticleManager.RejectedInstances();
    result.resimulationSteps = m_ParticleManager.ResimulationSteps();
    result.pendingResimulations = m_ParticleManager.PendingResimulations();
    result.simulationMilliseconds = m_ParticleManager.SimulationMilliseconds();
    result.resimulationMilliseconds = m_ParticleManager.ResimulationMilliseconds();
    result.mainThreadOverlapMilliseconds = m_ParticleManager.MainThreadOverlapMilliseconds();
    result.waitMilliseconds = m_ParticleManager.WaitMilliseconds();
    result.rendering = m_ParticleRenderSystem.Diagnostics();
    for (const auto& binding : m_ParticleSources)
    {
        const auto* runtime = m_ParticleManager.Resolve(binding.instance); if (!runtime) continue;
        VansParticleEffectDiagnostics effect;
        effect.instance = binding.instance; effect.effectGuid = binding.effect.ToString(); effect.sourceGuid = binding.source.entityGuid.ToString();
        effect.sourcePosition = binding.lastSourcePosition; effect.detached = binding.detached;
        effect.playTime = runtime->GetPlayTime(); effect.alivePoints = runtime->AliveInstanceCount(); effect.substepOverruns = runtime->SubstepOverruns();
        static const char* states[] = {"Stopped", "Delayed", "Emitting", "Draining", "Finished"};
        effect.state = runtime->IsPaused() ? "Paused" : states[static_cast<unsigned>(runtime->GetState())];
        for (size_t i=0; i<runtime->GetAsset()->m_Emitters.size(); ++i)
            if (const auto* emitter = runtime->GetEmitter(i)) { effect.droppedSpawns += emitter->DroppedSpawns(); effect.breaks += emitter->BreakCount(); }
        if (includePoints) effect.ribbons = runtime->GetFrameData().ribbons;
        result.effects.push_back(std::move(effect));
    }
    return result;
}
std::shared_ptr<const VansParticleRenderAsset> VansScene::PrepareParticleRenderAsset(
    std::shared_ptr<const VansParticleAsset> asset)
{
    VANS_ASSERT_MAIN_THREAD();
    if (!asset) return {};
    const auto found = m_ParticleRenderAssets.find(asset.get());
    if (found != m_ParticleRenderAssets.end()) if (auto cached = found->second.lock()) return cached;
    auto binding = std::make_shared<VansParticleRenderAsset>(); binding->definition = asset;
    const auto texture = [&](const std::string& guid) -> VansTexture* {
        return static_cast<VansTexture*>(FindTextureAssetByGuid(guid));
    };
    binding->textures.resize(asset->m_Emitters.size());
    for (std::size_t i=0; i<asset->m_Emitters.size(); ++i)
    {
        if (!asset->m_Emitters[i]) continue;
        const auto& config = asset->m_Emitters[i]->m_RendererConfig;
        if (config.m_Type == VansParticleRendererType::None) continue;
        if (config.m_LightingMode == VansParticleLightingMode::SixWayLit)
        {
            binding->textures[i].positiveAxes = texture(config.m_SixWayLighting.m_PositiveAxesTextureGuid);
            binding->textures[i].negativeAxes = texture(config.m_SixWayLighting.m_NegativeAxesTextureGuid);
        }
        else binding->textures[i].color = texture(config.m_TextureGuid);
    }
    for (auto iterator=m_ParticleRenderAssets.begin(); iterator!=m_ParticleRenderAssets.end();)
        if (iterator->second.expired()) iterator = m_ParticleRenderAssets.erase(iterator); else ++iterator;
    m_ParticleRenderAssets[asset.get()] = binding;
    return binding;
}
}
