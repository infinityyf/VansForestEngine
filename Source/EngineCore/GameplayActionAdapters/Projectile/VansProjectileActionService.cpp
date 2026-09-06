#include "VansProjectileActionService.h"
#include "VansProjectileActionCapability.h"
#include "../VansActionServiceAdapter.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../ScriptCore/VansTransform.h"
#include "../../ScriptCore/VansCommonUtils.h"
#include "../../RuntimeCore/VansCharacterMotion.h"
#include "../../GameplayActionCore/VansGameplayRuntime.h"
#include "../../SceneCore/VansSceneParticleComponentReader.h"
#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
double Number(const VansSerializedValue& value, const char* name, double fallback)
{
    const auto* field = FindObjectField(value, name);
    return field ? ReadSerializedNumber(*field, fallback) : fallback;
}
}
VansProjectileActionService::VansProjectileActionService(VansRuntimeWorld& world, VansGameplayRuntime& gameplay, VansProjectileSceneBackend backend)
    : m_World(world), m_Gameplay(gameplay), m_Backend(std::move(backend)) {}
const VansActionServiceCapability& VansProjectileActionService::Capability() const { return VansProjectileActionCapability(); }

VansActionCommandResult VansProjectileActionService::Execute(const VansActionCommand& command)
{
    if (command.stableName != "Projectile.Spawn" || !m_Backend.spawn)
        return { VansActionError::InvalidDefinition, {}, {}, "Projectile scene backend unavailable" };
    VansProjectileSpawnRequest request;
    request.owner = command.context.Entity(VansActionContextSlots::Owner);
    request.source = ReadSerializedStringField(command.payload, "source");
    request.mass = static_cast<float>(Number(command.payload, "mass", 0.4));
    request.restitution = static_cast<float>(Number(command.payload, "restitution", 0.25));
    request.friction = static_cast<float>(Number(command.payload, "friction", 0.6));
    request.restitutionCombine = ReadSerializedStringField(command.payload, "restitutionCombine", "Average");
    request.frictionCombine = ReadSerializedStringField(command.payload, "frictionCombine", "Average");
    const auto validCombine = [](const auto& mode) { return mode == "Average" || mode == "Min" || mode == "Multiply" || mode == "Max"; };
    if (!validCombine(request.restitutionCombine) || !validCombine(request.frictionCombine))
        return { VansActionError::Rejected, {}, {}, "Projectile material combine mode must be Average, Min, Multiply or Max" };
    if (const auto* particle = FindObjectField(command.payload, "particle"); particle && !particle->objectFields.empty())
    {
        request.particle = VansSceneParticleComponentReader::ReadParticle(*particle);
        if (!request.particle) return { VansActionError::Rejected, {}, {}, "Projectile particle needs an asset GUID reference" };
    }
    request.collisionLayer = ReadSerializedStringField(command.payload, "collisionLayer", "Default");
    const auto* spin = FindObjectField(command.payload, "angularVelocity");
    if (spin) request.angularVelocity = glm::vec3(Number(*spin, "x", 0), Number(*spin, "y", 0), Number(*spin, "z", 0));
    const float speed = static_cast<float>(Number(command.payload, "speed", 8.0));
    const float lift = static_cast<float>(Number(command.payload, "lift", 3.0));
    const double lifetime = Number(command.payload, "lifetime", 10.0);
    auto* storage = static_cast<VansComponentStorage<VansRuntimeTransformComponent>*>(m_World.FindStorage(VansRuntimeComponentType_Transform));
    bool hasDirection = false;
    if (storage) for (auto handle : m_World.CollectComponentsOwnedBy(request.owner))
    {
        if (handle.typeId != VansRuntimeComponentType_Transform) continue;
        const auto* component = storage->Get(handle);
        if (!component || !VansGraphics::VansTransformStore::IsAllocated(component->transformStoreId)) continue;
        const auto& transform = VansGraphics::VansTransformStore::GetTransform(component->transformStoreId);
        request.velocity = LocomotionLocalToWorldPlanar(glm::vec3(0,0,speed), transform.m_Rotation.y) + glm::vec3(0,lift,0);
        hasDirection = true;
        break;
    }
    if (const auto* velocity = FindObjectField(command.payload, "velocity"); velocity && !velocity->objectFields.empty())
    {
        request.velocity = glm::vec3(Number(*velocity, "x", 0), Number(*velocity, "y", 0), Number(*velocity, "z", 0));
        hasDirection = true;
    }
    if (!hasDirection || !std::isfinite(lifetime) || lifetime < 0 || !std::isfinite(request.mass) || request.mass <= 0
        || !std::isfinite(glm::length(request.velocity)) || !std::isfinite(glm::length(request.angularVelocity)))
        return { VansActionError::Rejected, {}, {}, "Projectile direction or physics parameters are invalid" };
    if (!std::isfinite(request.restitution) || request.restitution < 0 || request.restitution > 1
        || !std::isfinite(request.friction) || request.friction < 0)
        return { VansActionError::Rejected, {}, {}, "Projectile physical material parameters are invalid" };
    std::string error;
    const auto entity = m_Backend.spawn(request, error);
    if (!entity.IsValid()) return { VansActionError::Execution, {}, {}, std::move(error) };
    const auto* record = m_World.Entities().Get(entity);
    return { VansActionError::None, m_Projectiles.Emplace(Projectile{entity,
        lifetime > 0 ? std::optional<double>(lifetime) : std::nullopt}),
        VansSerializedValue::Object({ {"entityGuid", VansSerializedValue::String(record ? record->stableGuid : "")} }), {} };
}
bool VansProjectileActionService::Release(VansGenerationHandle resource, std::string& error)
{
    auto* projectile = m_Projectiles.Resolve(resource);
    if (!projectile) { error = "Projectile resource is stale"; return false; }
    if (projectile->entity.IsValid() && m_World.IsAlive(projectile->entity)
        && (!m_Backend.destroy || !m_Backend.destroy(projectile->entity)))
    { error = "Projectile destruction was rejected"; return false; }
    return m_Projectiles.Release(resource);
}
void VansProjectileActionService::Tick(double deltaSeconds)
{
    std::vector<VansGenerationHandle> completed;
    m_Projectiles.ForEach([&](VansGenerationHandle handle, Projectile& projectile)
    {
        if (projectile.entity.IsValid())
        {
            if (projectile.justSpawned) { projectile.justSpawned = false; return; }
            if (projectile.remainingSeconds)
                *projectile.remainingSeconds -= std::max(0.0, deltaSeconds);
            if (!m_World.IsAlive(projectile.entity)
                || (projectile.remainingSeconds && *projectile.remainingSeconds <= 0
                    && m_Backend.destroy && m_Backend.destroy(projectile.entity)))
                projectile.entity = {};
        }
        if (!projectile.entity.IsValid()
            && m_Gameplay.ForgetCompletedWorldResource(Capability().service, handle)) completed.push_back(handle);
    });
    for (auto handle : completed) m_Projectiles.Release(handle);
}
const VansActionServiceCapability& VansAttachmentActionCapability()
{
    using V = VansActionCommandValueKind;
    static const auto capability = VansActionServiceCapabilityDescriptor("Service.Attachment", {
        VansActionCommandCapability("Attachment.BindSocketProfile", VansActionCommandResourcePolicy::None, {
            VansActionCommandField("object", V::String, true),
            VansActionCommandField("animationComponent", V::String, true),
            VansActionCommandField("socket", V::String, true)
        })
    });
    return capability;
}
std::shared_ptr<IVansActionService> VansCreateAttachmentActionService(VansProjectileSceneBackend backend)
{
    auto adapter = std::make_shared<VansActionServiceAdapter>(VansAttachmentActionCapability());
    std::string error;
    adapter->Bind("Attachment.BindSocketProfile", [backend](const VansActionCommand& command)
    {
        std::string error;
        if (!backend.bindSocket || !backend.bindSocket(command.context.Entity(VansActionContextSlots::Owner),
            ReadSerializedStringField(command.payload, "object"), ReadSerializedStringField(command.payload, "animationComponent"),
            ReadSerializedStringField(command.payload, "socket"), error))
            return VansActionCommandResult{VansActionError::Execution, {}, {}, error.empty() ? "Attachment binding rejected" : error};
        return VansActionCommandResult{};
    }, error);
    return adapter;
}
}
