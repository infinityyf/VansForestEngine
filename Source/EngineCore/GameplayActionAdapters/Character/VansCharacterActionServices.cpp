#include "VansCharacterActionServices.h"

#include "../VansActionServiceAdapter.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../PhysicsCore/VansCharacterControllerNode.h"
#include "../../SceneRuntime/VansComponentStorage.h"
#include "../../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../../SceneRuntime/VansRuntimeWorld.h"

#include <utility>

namespace Vans
{
namespace
{
using ValueKind = VansActionCommandValueKind;
using ResourcePolicy = VansActionCommandResourcePolicy;

template <typename T>
T* FindOwnedEnabledComponent(
	VansRuntimeWorld& world,
	std::uint16_t type,
	VansEntityHandle owner)
{
	IVansComponentStorage* raw = world.FindStorage(type);
	if (!raw) return nullptr;
	auto* storage = static_cast<VansComponentStorage<T>*>(raw);
	const auto& headers = storage->Headers();
	auto& values = storage->DenseData();
	for (std::size_t index = 0; index < headers.size() && index < values.size(); ++index)
		if (headers[index].owner == owner && headers[index].effectiveEnabled)
			return &values[index];
	return nullptr;
}
}

const VansActionServiceCapability& VansAnimationActionCapability()
{
	static const VansActionServiceCapability capability =
		VansActionServiceCapabilityDescriptor("Service.Animation", {
			VansActionCommandCapability("Animation.Play", ResourcePolicy::Create, {
				VansActionCommandField("clip", ValueKind::String, true),
				VansActionCommandField("slot", ValueKind::String, false,
					VansSerializedValue::String({})),
				VansActionCommandNumberField("layer", ValueKind::Int, false,
					VansSerializedValue::Int(0), 0.0, 255.0),
				VansActionCommandNumberField("rate", ValueKind::Float, false,
					VansSerializedValue::Float(1.0), 0.01, 100.0),
				VansActionCommandField("loop", ValueKind::Bool, false,
					VansSerializedValue::Bool(false))
			}),
			VansActionCommandCapability("Animation.Stop", ResourcePolicy::Release, {
				VansActionCommandResourceField(),
				VansActionCommandNumberField("blendOut", ValueKind::Float, false,
					VansSerializedValue::Float(0.0), 0.0, 1.0)
			}),
			VansActionCommandCapability("Animation.SetRate", ResourcePolicy::Update, {
				VansActionCommandResourceField(),
				VansActionCommandNumberField("rate", ValueKind::Float, false,
					VansSerializedValue::Float(1.0), 0.01, 100.0)
			}),
			VansActionCommandCapability("Animation.JumpMarker", ResourcePolicy::Update, {
				VansActionCommandResourceField(),
				VansActionCommandField("marker", ValueKind::String, true)
			}),
			VansActionCommandCapability("Animation.WaitMarker", ResourcePolicy::Create, {
				VansActionCommandResourceField(),
				VansActionCommandField("marker", ValueKind::String, true),
				VansActionCommandNumberField("timeout", ValueKind::Float, false,
					VansSerializedValue::Float(0.0), 0.0, 3600.0)
			})
		});
	return capability;
}

const VansActionServiceCapability& VansNavigationActionCapability()
{
	static const VansActionServiceCapability capability =
		VansActionServiceCapabilityDescriptor("Service.Navigation", {
			VansActionCommandCapability("Navigation.BlockMovement", ResourcePolicy::Create, {
				VansActionCommandField("reason", ValueKind::String, false,
					VansSerializedValue::String({}))
			}),
			VansActionCommandCapability("Navigation.RequestPath", ResourcePolicy::Create, {
				VansActionCommandField("destination", ValueKind::Object, true,
					VansSerializedValue::Object({})),
				VansActionCommandField("agentProfile", ValueKind::String, false,
					VansSerializedValue::String({}))
			}),
			VansActionCommandCapability("Navigation.Move", ResourcePolicy::Create, {
				VansActionCommandField("destination", ValueKind::Object, true,
					VansSerializedValue::Object({})),
				VansActionCommandNumberField("acceptanceRadius", ValueKind::Float, false,
					VansSerializedValue::Float(0.1), 0.0, 1000000.0)
			}),
			VansActionCommandCapability("Navigation.Cancel", ResourcePolicy::Release, {
				VansActionCommandResourceField()
			})
		});
	return capability;
}

VansAnimationActionService::VansAnimationActionService(
	VansRuntimeWorld& world,
	VansActionServiceCapability capability)
	: m_World(world)
	, m_Capability(std::move(capability))
{
}

std::shared_ptr<VansAnimationActionService> VansAnimationActionService::Create(
	VansRuntimeWorld& world,
	std::string& error)
{
	(void)error;
	return std::shared_ptr<VansAnimationActionService>(
		new VansAnimationActionService(world, VansAnimationActionCapability()));
}

VansActionCommandResult VansAnimationActionService::Execute(
	const VansActionCommand& command)
{
	if (command.stableName != "Animation.Play")
	{
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			"The scene Animation service currently requires Animation.Play" };
	}
	VansRuntimeAnimationComponent* component = FindOwnedEnabledComponent<
		VansRuntimeAnimationComponent>(m_World, VansRuntimeComponentType_Animation,
			command.context.Entity(VansActionContextSlots::Owner));
	VansGraphics::VansAnimationController* controller = component && component->animationNode
		? component->animationNode->GetCharacterMotionController() : nullptr;
	const std::string state = ReadSerializedStringField(command.payload, "clip");
	const VansSerializedValue* rateValue = FindObjectField(command.payload, "rate");
	const float rate = rateValue
		? static_cast<float>(ReadSerializedNumber(*rateValue, 1.0)) : 1.0f;
	if (!controller || state.empty())
	{
		return { VansActionError::Rejected, {}, VansSerializedValue::Object({}),
			"Animation.Play requires an enabled owner Animation component and state" };
	}
	PlaybackResource playback;
	playback.controller = controller;
	playback.previousState = controller->GetCurrentStateName();
	playback.previousRate = controller->GetSpeed();
	controller->SetSpeed(rate);
	controller->Play(state);
	const VansGenerationHandle resource = m_Playbacks.Emplace(std::move(playback));
	return { VansActionError::None, resource, VansSerializedValue::Object({}), {} };
}

bool VansAnimationActionService::Release(
	VansGenerationHandle resource,
	std::string& error)
{
	PlaybackResource* playback = m_Playbacks.Resolve(resource);
	if (!playback || !playback->controller)
	{
		error = "Animation playback resource is stale";
		return false;
	}
	playback->controller->SetSpeed(playback->previousRate);
	if (!playback->previousState.empty())
		playback->controller->Play(playback->previousState);
	return m_Playbacks.Release(resource);
}

VansNavigationActionService::VansNavigationActionService(
	VansRuntimeWorld& world,
	VansActionServiceCapability capability)
	: m_World(world)
	, m_Capability(std::move(capability))
{
}

std::shared_ptr<VansNavigationActionService> VansNavigationActionService::Create(
	VansRuntimeWorld& world,
	std::string& error)
{
	(void)error;
	return std::shared_ptr<VansNavigationActionService>(
		new VansNavigationActionService(world, VansNavigationActionCapability()));
}

VansActionCommandResult VansNavigationActionService::Execute(
	const VansActionCommand& command)
{
	if (command.stableName != "Navigation.BlockMovement")
	{
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			"The scene Navigation service currently requires Navigation.BlockMovement" };
	}
	VansRuntimeCharacterControllerComponent* component = FindOwnedEnabledComponent<
		VansRuntimeCharacterControllerComponent>(m_World,
			VansRuntimeComponentType_CharacterController,
			command.context.Entity(VansActionContextSlots::Owner));
	if (!component || !component->controllerNode)
	{
		return { VansActionError::Rejected, {}, VansSerializedValue::Object({}),
			"Navigation.BlockMovement requires an enabled owner CharacterController" };
	}
	component->controllerNode->AcquireGameplayMovementBlock();
	const VansGenerationHandle resource = m_Blocks.Emplace(
		MovementBlockResource{ component->controllerNode });
	return { VansActionError::None, resource, VansSerializedValue::Object({}), {} };
}

bool VansNavigationActionService::Release(
	VansGenerationHandle resource,
	std::string& error)
{
	MovementBlockResource* block = m_Blocks.Resolve(resource);
	if (!block || !block->controller)
	{
		error = "Navigation movement block resource is stale";
		return false;
	}
	block->controller->ReleaseGameplayMovementBlock();
	return m_Blocks.Release(resource);
}
}
