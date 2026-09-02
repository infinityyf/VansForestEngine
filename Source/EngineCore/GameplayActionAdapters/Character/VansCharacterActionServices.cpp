#include "VansCharacterActionServices.h"

#include "../VansStandardActionServices.h"
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
	const VansActionServiceCapability* capability = VansFindStandardActionServiceCapability(
		VansMakeStableId<VansActionServiceIdTag>("Service.Animation"));
	if (!capability)
	{
		error = "Standard Animation Action Service capability is missing";
		return {};
	}
	return std::shared_ptr<VansAnimationActionService>(
		new VansAnimationActionService(world, *capability));
}

VansActionCommandResult VansAnimationActionService::Execute(
	const VansActionCommand& command)
{
	if (command.stableName != "Animation.Play")
	{
		return { VansActionError::DefinitionInvalid, {}, VansSerializedValue::Object({}),
			"The scene Animation service currently requires Animation.Play" };
	}
	VansRuntimeAnimationComponent* component = FindOwnedEnabledComponent<
		VansRuntimeAnimationComponent>(m_World, VansRuntimeComponentType_Animation,
			command.context.owner);
	VansGraphics::VansAnimationController* controller = component && component->animationNode
		? component->animationNode->GetCharacterMotionController() : nullptr;
	const std::string state = ReadSerializedStringField(command.payload, "clip");
	const VansSerializedValue* rateValue = FindObjectField(command.payload, "rate");
	const float rate = rateValue
		? static_cast<float>(ReadSerializedNumber(*rateValue, 1.0)) : 1.0f;
	if (!controller || state.empty())
	{
		return { VansActionError::TargetInvalid, {}, VansSerializedValue::Object({}),
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
	const VansActionServiceCapability* capability = VansFindStandardActionServiceCapability(
		VansMakeStableId<VansActionServiceIdTag>("Service.Navigation"));
	if (!capability)
	{
		error = "Standard Navigation Action Service capability is missing";
		return {};
	}
	return std::shared_ptr<VansNavigationActionService>(
		new VansNavigationActionService(world, *capability));
}

VansActionCommandResult VansNavigationActionService::Execute(
	const VansActionCommand& command)
{
	if (command.stableName != "Navigation.BlockMovement")
	{
		return { VansActionError::DefinitionInvalid, {}, VansSerializedValue::Object({}),
			"The scene Navigation service currently requires Navigation.BlockMovement" };
	}
	VansRuntimeCharacterControllerComponent* component = FindOwnedEnabledComponent<
		VansRuntimeCharacterControllerComponent>(m_World,
			VansRuntimeComponentType_CharacterController, command.context.owner);
	if (!component || !component->controllerNode)
	{
		return { VansActionError::TargetInvalid, {}, VansSerializedValue::Object({}),
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
