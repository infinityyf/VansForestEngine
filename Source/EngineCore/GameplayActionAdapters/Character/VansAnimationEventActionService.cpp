#include "VansAnimationEventActionService.h"
#include "../VansActionServiceAdapter.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../GameplayActionCore/VansGameplayRuntime.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../Util/VansLog.h"
#include <type_traits>

namespace Vans
{
const VansActionServiceCapability& VansAnimationEventActionCapability()
{
    using V = VansActionCommandValueKind;
    static const auto capability = VansActionServiceCapabilityDescriptor("Service.AnimationEvents", {
        VansActionCommandCapability("AnimationEvents.Subscribe", VansActionCommandResourcePolicy::Create, {
            VansActionCommandField("clip", V::String, true),
            VansActionCommandField("event", V::String, false, VansSerializedValue::String("")),
            VansActionCommandField("animationComponent", V::String, false, VansSerializedValue::String("")),
            VansActionCommandNumberField("minimumWeight", V::Float, false, VansSerializedValue::Float(0.5), 0.0, 1.0),
            VansActionCommandField("oncePerAction", V::Bool, false, VansSerializedValue::Bool(false)),
            VansActionCommandField("forwardOnly", V::Bool, false, VansSerializedValue::Bool(true))
        }),
        VansActionCommandCapability("AnimationEvents.Unsubscribe", VansActionCommandResourcePolicy::Release, {
            VansActionCommandResourceField()
        })
    });
    return capability;
}

VansAnimationEventActionService::VansAnimationEventActionService(VansRuntimeWorld& world, VansGameplayRuntime& gameplay)
    : m_World(world), m_Gameplay(gameplay) {}

const VansActionServiceCapability& VansAnimationEventActionService::Capability() const
{
    return VansAnimationEventActionCapability();
}

VansActionCommandResult VansAnimationEventActionService::Execute(const VansActionCommand& command)
{
    if (command.stableName != "AnimationEvents.Subscribe")
        return { VansActionError::InvalidDefinition, {}, {}, "Use resource release to unsubscribe" };
    Subscription subscription;
    subscription.owner = command.context.Entity(VansActionContextSlots::Owner);
    subscription.action = command.action;
    subscription.clip = ReadSerializedStringField(command.payload, "clip");
    subscription.event = ReadSerializedStringField(command.payload, "event");
    subscription.oncePerAction = ReadSerializedBoolField(command.payload, "oncePerAction", false);
    subscription.forwardOnly = ReadSerializedBoolField(command.payload, "forwardOnly", true);
    if (const auto* weight = FindObjectField(command.payload, "minimumWeight"))
        subscription.minimumWeight = static_cast<float>(ReadSerializedNumber(*weight, 0.5));
    const std::string componentGuid = ReadSerializedStringField(command.payload, "animationComponent");
    for (const auto component : m_World.CollectComponentsOwnedBy(subscription.owner))
    {
        if (component.typeId != VansRuntimeComponentType_Animation) continue;
        if (!componentGuid.empty() && component != m_World.FindComponentByGuid(componentGuid, component.typeId)) continue;
        if (subscription.animation.IsValid())
            return { VansActionError::Rejected, {}, {}, "Select an animationComponent when the owner has multiple Animation components" };
        subscription.animation = component;
    }
    auto* storage = static_cast<VansComponentStorage<VansRuntimeAnimationComponent>*>(
        m_World.FindStorage(VansRuntimeComponentType_Animation));
    const auto* animation = storage ? storage->Get(subscription.animation) : nullptr;
    const auto* controller = animation && animation->animationNode
        ? animation->animationNode->GetCharacterMotionController() : nullptr;
    if (!controller || !controller->GetClip(subscription.clip) || !subscription.action)
        return { VansActionError::Rejected, {}, {}, "AnimationEvents.Subscribe requires a live Action and an owner Clip" };
    return { VansActionError::None, m_Subscriptions.Emplace(std::move(subscription)), VansSerializedValue::Object({}), {} };
}

bool VansAnimationEventActionService::Release(VansGenerationHandle resource, std::string& error)
{
    if (m_Subscriptions.Release(resource)) return true;
    error = "Animation event subscription is stale";
    return false;
}

void VansAnimationEventActionService::PublishEvaluatedEvents()
{
    auto* storage = static_cast<VansComponentStorage<VansRuntimeAnimationComponent>*>(
        m_World.FindStorage(VansRuntimeComponentType_Animation));
    if (!storage) return;
    m_Subscriptions.ForEach([&](VansGenerationHandle, Subscription& subscription)
    {
        const auto* animation = storage->Get(subscription.animation);
        const auto host = m_Gameplay.FindHost(subscription.owner);
        if (!animation || !animation->animationNode || !host || !host->Query(subscription.action)
            || !m_World.IsComponentEffectivelyEnabled(subscription.animation)) return;
        const auto* controller = animation->animationNode->GetCharacterMotionController();
        const auto* clip = controller ? controller->GetClip(subscription.clip) : nullptr;
        if (!clip) return;
        const auto clipId = clip->stableId ? clip->stableId : VansGraphics::VansAnimationStableId(clip->clipName);
        std::unordered_set<std::string> frameOccurrences;
        for (const auto& sampled : animation->animationNode->GetSampledEvents())
        {
            if (sampled.clipId != clipId || sampled.weight < subscription.minimumWeight
                || (subscription.forwardOnly && !sampled.forward)
                || (!subscription.event.empty() && sampled.name != subscription.event)) continue;
            const std::string key = std::to_string(sampled.id) + "@" + std::to_string(sampled.sourceTime);
            const std::string occurrence = key + ":" + std::to_string(sampled.loopIndex);
            if (!frameOccurrences.insert(occurrence).second
                || (subscription.oncePerAction && !subscription.delivered.insert(key).second)) continue;
            VansSerializedValue value;
            std::visit([&](const auto& item)
            {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, bool>) value = VansSerializedValue::Bool(item);
                else if constexpr (std::is_same_v<T, std::int64_t>) value = VansSerializedValue::Int(item);
                else if constexpr (std::is_same_v<T, double>) value = VansSerializedValue::Float(item);
                else if constexpr (std::is_same_v<T, std::string>) value = VansSerializedValue::String(item);
                else if constexpr (std::is_same_v<T, glm::vec3>) value = VansSerializedValue::Array({
                    VansSerializedValue::Float(item.x), VansSerializedValue::Float(item.y), VansSerializedValue::Float(item.z) });
            }, sampled.payload);
            VansActionEvent event;
            event.stableName = std::string(sampled.name);
            event.type = VansMakeStableId<VansActionFieldIdTag>(event.stableName);
            event.source = subscription.owner;
            event.payload = VansSerializedValue::Object({
                { "clip", VansSerializedValue::String(subscription.clip) },
                { "time", VansSerializedValue::Float(sampled.sourceTime) },
                { "loopIndex", VansSerializedValue::Int(sampled.loopIndex) },
                { "weight", VansSerializedValue::Float(sampled.weight) },
                { "forward", VansSerializedValue::Bool(sampled.forward) },
                { "value", std::move(value) }
            });
            std::string error;
            if (!host->EnqueueEvent(subscription.action, std::move(event), error))
                VANS_LOG_WARN("[AnimationEvents] " << error);
        }
    });
}
}
