#include "VansProjectileActionService.h"
#include "VansProjectileActionCapability.h"
#include "../VansActionServiceAdapter.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/Transform/VansTransformStore.h"
#include "../../ScriptCore/VansCommonUtils.h"
#include "../../RuntimeCore/VansCharacterMotion.h"
#include "../../SceneCore/VansSceneParticleComponentReader.h"
#include "../../GameplayActionSchema/VansGameplayAssetLibrary.h"
#include "../../Util/VansLog.h"
#include "../../EventCore/VansEventBus.h"
#include "../../PhysicsCore/VansPhysicsEvents.h"
#include "../../PhysicsCore/VansPhysicsNode.h"
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
VansProjectileActionService::VansProjectileActionService(VansRuntimeWorld& world, IVansGameplayServiceRuntime& runtime,
    const VansGameplayAssetLibrary& assets, VansProjectileSceneBackend backend)
    : m_World(world), m_Runtime(runtime), m_Assets(assets), m_Backend(std::move(backend))
{
    // Physics lane 在主线程、物理锁外投递。这里只保存接触事实，由服务 Tick 激活动作。
    m_Contacts = VansEventBus::Get().Subscribe<VansEngine::VansPhysicsContactEvent>([this](const auto& event) {
        if (event.type != VansEngine::VansPhysicsContactEventType::CollisionEnter
            && event.type != VansEngine::VansPhysicsContactEventType::CollisionExit) return;
        m_Projectiles.ForEach([&](auto, Projectile& projectile) {
            if (!projectile.fuseSeconds || projectile.detonationMode == "Timer" || !m_World.IsAlive(projectile.entity)) return;
            const bool first = projectile.transformId == event.transformID_A;
            if (!first && projectile.transformId != event.transformID_B) return;
            const auto other = first ? event.transformID_B : event.transformID_A;
            if (event.type == VansEngine::VansPhysicsContactEventType::CollisionExit) { projectile.supports.erase(other); return; }
            const auto normal = first ? event.contactNormal : -event.contactNormal;
            if (normal.y >= projectile.impactNormalMinY) {
                projectile.supports.insert(other);
                projectile.surfaceImpact = true;
            }
        });
    }, VansEventLane::Physics);
}
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
    const double fuse = Number(command.payload, "fuseSeconds", 0.0);
    const auto detonationName = ReadSerializedStringField(command.payload,"detonationAction");
    const auto detonation = detonationName.empty() ? nullptr : m_Assets.ResolveAction(detonationName);
    const auto mode = ReadSerializedStringField(command.payload,"detonationMode","Timer");
    const auto timeoutName = ReadSerializedStringField(command.payload,"timeoutAction");
    const auto timeout = timeoutName.empty() ? nullptr : m_Assets.ResolveAction(timeoutName);
    const auto settleSpeed = Number(command.payload,"settleSpeed",0.15);
    const auto settleSeconds = Number(command.payload,"settleSeconds",0.2);
    const auto minY = Number(command.payload,"impactNormalMinY",0.7071);
    if ((mode != "Timer" && mode != "Settled" && mode != "SurfaceImpact")
        || (!timeoutName.empty() && (!timeout || mode != "SurfaceImpact"))
        || !std::isfinite(settleSpeed) || settleSpeed < 0 || settleSpeed > 10
        || !std::isfinite(settleSeconds) || settleSeconds < 0 || settleSeconds > 10
        || !std::isfinite(minY) || minY < -1 || minY > 1)
        return {VansActionError::Rejected,{},{},"Projectile detonation mode or contact parameters are invalid"};
    if (!std::isfinite(fuse) || fuse < 0 || fuse > 3600 || (fuse > 0) != !detonationName.empty()
        || (!detonationName.empty() && !detonation) || (fuse > 0 && lifetime > 0 && lifetime < fuse))
        return {VansActionError::Rejected,{},{},"Projectile fuse requires a resolved detonation Action and lifetime >= fuse (or zero)"};
    auto* storage = m_World.FindStorage<VansRuntimeTransformComponent>(VansRuntimeComponentType_Transform);
    bool hasDirection = false;
    if (storage) for (auto handle : m_World.CollectComponentsOwnedBy(request.owner))
    {
        if (handle.typeId != VansRuntimeComponentType_Transform) continue;
        const auto* component = storage->Get(handle);
        if (!component || !Vans::VansTransformStore::IsAllocated(component->transformStoreId)) continue;
        const auto& transform = Vans::VansTransformStore::Read(component->transformStoreId);
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
    Projectile projectile{entity,lifetime > 0 ? std::optional<double>(lifetime) : std::nullopt};
    projectile.owner = request.owner;
    projectile.instigator = command.context.Entity(VansActionContextSlots::Instigator);
    if (!projectile.instigator.IsValid()) projectile.instigator = request.owner;
    if (detonation) { projectile.detonationAction=detonation->id; projectile.fuseSeconds=fuse; }
    projectile.detonationMode=mode; projectile.timeoutAction=timeout ? timeout->id : VansActionId{};
    projectile.settleSpeed=static_cast<float>(settleSpeed); projectile.settleSeconds=settleSeconds;
    projectile.impactNormalMinY=static_cast<float>(minY);
    if (storage) if (const auto* transform=storage->Get(storage->FindFirstOwnedBy(entity)))
        projectile.transformId=transform->transformStoreId;
    return { VansActionError::None, m_Projectiles.Emplace(std::move(projectile)),
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
            if (projectile.fuseSeconds && m_World.IsAlive(projectile.entity))
            {
                *projectile.fuseSeconds -= std::max(0.0,deltaSeconds);
                const bool timerExpired = *projectile.fuseSeconds <= 1.e-9;
                bool settled = false;
                if (projectile.detonationMode == "Settled" && timerExpired)
                {
                    const auto* physics=m_World.FindStorage<VansRuntimePhysicsComponent>(VansRuntimeComponentType_Physics);
                    const auto* body=physics ? physics->Get(physics->FindFirstOwnedBy(projectile.entity)) : nullptr;
                    const bool resting=body && body->physicsNode && !projectile.supports.empty()
                        && glm::length(body->physicsNode->GetLinearVelocity()) <= projectile.settleSpeed;
                    projectile.settledFor=resting ? projectile.settledFor+std::max(0.0,deltaSeconds) : 0;
                    settled=resting && projectile.settledFor >= projectile.settleSeconds;
                }
                const bool impact=projectile.detonationMode == "SurfaceImpact" && projectile.surfaceImpact;
                if ((projectile.detonationMode == "Timer" && timerExpired) || settled
                    || (projectile.detonationMode == "SurfaceImpact" && (impact || timerExpired)))
                {
                    // Consume before activation: failure or recursive commands must never detonate twice.
                    projectile.fuseSeconds.reset();
                    glm::vec3 position(0);
                    auto* transforms=m_World.FindStorage<VansRuntimeTransformComponent>(VansRuntimeComponentType_Transform);
                    const auto* transform=transforms ? transforms->Get(transforms->FindFirstOwnedBy(projectile.entity)) : nullptr;
                    const auto* entity=m_World.Entities().Get(projectile.entity);
                    const auto action=projectile.detonationMode == "SurfaceImpact" && !impact
                        ? projectile.timeoutAction : projectile.detonationAction;
                    if (action.IsValid() && transform && VansTransformStore::IsAllocated(transform->transformStoreId) && entity)
                    {
                        position=VansTransformStore::Read(transform->transformStoreId).m_Position;
                        using V=VansSerializedValue;
                        VansActionContext context;
                        context.SetEntity(VansActionContextSlots::Owner,projectile.owner);
                        context.SetEntity(VansActionContextSlots::Instigator,projectile.instigator);
                        context.SetEntity(VansActionContextSlots::Source,projectile.entity);
                        context.SetSerialized(VansActionContextSlots::Payload,V::Object({
                            {"position",V::Object({{"x",V::Float(position.x)},{"y",V::Float(position.y)},{"z",V::Float(position.z)}})},
                            {"projectileGuid",V::String(entity->stableGuid)}}));
                        VansTargetLocation location; location.value={position.x,position.y,position.z};
                        const auto result=m_Runtime.ActivateAction(projectile.owner,action,
                            std::move(context),VansTargetData{{location}});
                        VANS_LOG("[Projectile] Detonation entity=" << entity->stableGuid << " position="
                            << position.x << "," << position.y << "," << position.z << " activated=" << static_cast<bool>(result));
                        if (!result) VANS_LOG_WARN("[Projectile] Detonation Action rejected: " << result.message);
                    }
                    // Keep entity alive until scene's structural destruction barrier; effects use a world snapshot.
                    projectile.remainingSeconds=0;
                }
            }
            if (projectile.remainingSeconds)
                *projectile.remainingSeconds -= std::max(0.0, deltaSeconds);
            if (!m_World.IsAlive(projectile.entity)
                || (projectile.remainingSeconds && *projectile.remainingSeconds <= 0
                    && m_Backend.destroy && m_Backend.destroy(projectile.entity)))
                projectile.entity = {};
        }
        if (!projectile.entity.IsValid()
            && m_Runtime.ForgetCompletedWorldResource(Capability().service, handle)) completed.push_back(handle);
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
