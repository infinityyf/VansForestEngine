#include "VansVFXActionService.h"
#include "VansVFXActionCapability.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

namespace Vans
{
VansVFXActionService::VansVFXActionService(IVansGameplayServiceRuntime& runtime,VansVFXSceneBackend backend)
    : m_Runtime(runtime),m_Backend(std::move(backend)) {}
const VansActionServiceCapability& VansVFXActionService::Capability() const { return VansVFXActionCapability(); }
VansActionCommandResult VansVFXActionService::Execute(const VansActionCommand& command)
{
    if (command.stableName == "VFX.Spawn" || command.stableName == "VFX.Pulse")
    {
        VansVFXSpawnRequest request;
        request.owner = command.context.Entity(VansActionContextSlots::Owner);
        std::string error;
        const auto* source = FindObjectField(command.payload,"source");
        const auto limit = ReadSerializedIntField(command.payload,"maxConcurrentPerSource",16);
        if (!request.owner.IsValid() || !VansAssetGuid::TryParse(ReadSerializedStringField(command.payload,"effect"),request.effect)
            || !source || !TryReadSceneParentReference(*source,request.source,error) || limit < 1 || limit > 64)
            return {VansActionError::Rejected,{},{},error.empty() ? "VFX requires an effect GUID, source reference, and capacity in [1,64]" : error};
        request.maxConcurrentPerSource = static_cast<std::uint32_t>(limit);
        if (command.stableName == "VFX.Pulse")
        {
            if (!m_Backend.pulse) return {VansActionError::Dependency,{},{},"VFX pulse backend is unavailable"};
            if (!m_Backend.pulse(request,error)) return {VansActionError::Rejected,{},{},std::move(error)};
            return {};
        }
        if (!m_Backend.spawn) return {VansActionError::Dependency,{},{},"VFX scene backend is unavailable"};
        const auto instance = m_Backend.spawn(request,error);
        if (!instance.IsValid()) return {VansActionError::Rejected,{},{},std::move(error)};
        return {VansActionError::None,m_Effects.Emplace(Effect{instance}),{}, {}};
    }
    if (command.stableName != "VFX.Stop") return {VansActionError::InvalidDefinition,{},{},"Unknown VFX command"};
    const auto* value = FindObjectField(command.payload,"resource");
    const auto index = value ? ReadSerializedIntField(*value,"index",-1) : -1;
    const auto generation = value ? ReadSerializedIntField(*value,"generation",0) : 0;
    if (index < 0 || index > UINT32_MAX || generation <= 0 || generation > UINT32_MAX)
        return {VansActionError::Rejected,{},{},"VFX resource handle is invalid"};
    auto* effect = m_Effects.Resolve({static_cast<std::uint32_t>(index),static_cast<std::uint32_t>(generation)});
    if (!effect) return {VansActionError::Rejected,{},{},"VFX resource handle is stale"};
    const auto mode = ReadSerializedStringField(command.payload,"mode","Drain");
    if (mode != "Drain" && mode != "DetachAndDrain" && mode != "Immediate")
        return {VansActionError::Rejected,{},{},"VFX stop mode must be Drain, DetachAndDrain or Immediate"};
    if (!effect->instance.IsValid()) return {};
    if (!m_Backend.stop || !m_Backend.stop(effect->instance,
        mode == "Immediate" ? VansVFXStopMode::Immediate : mode == "DetachAndDrain" ? VansVFXStopMode::DetachAndDrain : VansVFXStopMode::Drain))
        return {VansActionError::Execution,{},{},"VFX stop was rejected"};
    return {};
}
bool VansVFXActionService::Release(VansGenerationHandle resource,std::string& error)
{
    auto* effect = m_Effects.Resolve(resource);
    if (!effect) { error = "VFX resource handle is stale"; return false; }
    if (effect->instance.IsValid() && (!m_Backend.destroy || !m_Backend.destroy(effect->instance)))
    { error = "VFX instance destruction failed"; return false; }
    return m_Effects.Release(resource);
}
void VansVFXActionService::Tick(double)
{
    std::vector<VansGenerationHandle> completed;
    m_Effects.ForEach([&](auto handle,Effect& effect) {
        if (effect.justSpawned) { effect.justSpawned = false; return; }
        if (effect.instance.IsValid() && m_Backend.finished && m_Backend.finished(effect.instance))
        {
            if (!m_Backend.destroy || !m_Backend.destroy(effect.instance)) return;
            effect.instance = {};
        }
        // 尚未转交 World 的完成令牌仍由原账本释放，不能绕过账本提前复用。
        if (!effect.instance.IsValid() && m_Runtime.ForgetCompletedWorldResource(Capability().service,handle))
            completed.push_back(handle);
    });
    for (auto handle : completed) m_Effects.Release(handle);
}
}
