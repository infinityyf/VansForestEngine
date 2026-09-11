#include "VansDecalActionService.h"
#include "../VansActionServiceAdapter.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

namespace Vans
{
const VansActionServiceCapability& VansDecalActionCapability()
{
    static const auto capability = VansActionServiceCapabilityDescriptor("Service.Decal", {
        VansActionCommandCapability("Decal.SpawnImpact", VansActionCommandResourcePolicy::None, {
            VansActionCommandField("template", VansActionCommandValueKind::String, true),
            VansActionCommandField("surfaceImpact", VansActionCommandValueKind::Object, true)
        })});
    return capability;
}
VansActionCommandResult VansDecalActionService::Execute(const VansActionCommand& command)
{
    const auto* surface = FindObjectField(command.payload, "surfaceImpact");
    VansSurfaceImpact impact;
    std::string error;
    if (command.command != VansMakeStableId<VansActionFieldIdTag>("Decal.SpawnImpact") || !surface ||
        !VansDecodeSurfaceImpact(*surface, impact, error))
        return {VansActionError::InvalidDefinition, {}, {}, error.empty() ? "Invalid impact decal command" : error};
    // 射空、人物 HurtBody 及不支持的原生碰撞体不创建硬表面贴花。
    if (impact.kind != VansSurfaceImpactKind::Rigid && impact.kind != VansSurfaceImpactKind::Terrain)
        return {VansActionError::None, {}, VansSerializedValue::Object({{"spawned", VansSerializedValue::Bool(false)}}), {}};
    const bool spawned = m_Backend.spawnImpact && m_Backend.spawnImpact(
        ReadSerializedStringField(command.payload, "template"), impact, error);
    return {VansActionError::None, {}, VansSerializedValue::Object({{"spawned", VansSerializedValue::Bool(spawned)}}), error};
}
}
