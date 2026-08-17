#include "VansCameraActionService.h"

#include "../VansStandardActionServices.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Vans
{
namespace
{
VansActionCommandResult Failure(VansActionError error, std::string message)
{
	return { error, {}, VansSerializedValue::Object({}), std::move(message) };
}

VansCameraViewId ViewId(const VansSerializedValue& payload)
{
	const std::string name = ReadSerializedStringField(payload, "view");
	if (name.empty() || name == "Main" || name == "Camera.View.Main")
		return VansCameraRuntime::MainView();
	return VansMakeStableId<VansCameraViewIdTag>(name);
}

glm::vec3 Vector3(const VansSerializedValue* object, glm::vec3 fallback = glm::vec3(0.0f))
{
	if (!object || object->kind != VansSerializedValue::Kind::Object) return fallback;
	return {
		static_cast<float>(ReadSerializedNumber(
			FindObjectField(*object, "x") ? *FindObjectField(*object, "x") : VansSerializedValue{}, fallback.x)),
		static_cast<float>(ReadSerializedNumber(
			FindObjectField(*object, "y") ? *FindObjectField(*object, "y") : VansSerializedValue{}, fallback.y)),
		static_cast<float>(ReadSerializedNumber(
			FindObjectField(*object, "z") ? *FindObjectField(*object, "z") : VansSerializedValue{}, fallback.z))
	};
}

bool ReadResource(const VansSerializedValue& payload, VansGenerationHandle& resource)
{
	const VansSerializedValue* object = FindObjectField(payload, "resource");
	if (!object || object->kind != VansSerializedValue::Kind::Object) return false;
	const std::int64_t index = ReadSerializedIntField(*object, "index", -1);
	const std::int64_t generation = ReadSerializedIntField(*object, "generation", 0);
	if (index < 0 || index > UINT32_MAX || generation <= 0 || generation > UINT32_MAX) return false;
	resource = { static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(generation) };
	return true;
}

std::int32_t Priority(const VansSerializedValue& payload)
{
	const std::int64_t value = ReadSerializedIntField(payload, "priority", 0);
	return static_cast<std::int32_t>(std::clamp<std::int64_t>(
		value, INT32_MIN, INT32_MAX));
}

VansCameraContributionDomainId TransientDomain()
{
	return VansMakeStableId<VansCameraContributionDomainIdTag>("Camera.GAF.Transient");
}
}

VansCameraContributionDomainId VansCameraActionService::Domain()
{
	return VansMakeStableId<VansCameraContributionDomainIdTag>("Camera.GAF");
}

std::shared_ptr<VansCameraActionService> VansCameraActionService::Create(
	VansCameraRuntime& runtime,
	const VansGameplayAssetLibrary& assets,
	std::string& error,
	TargetPositionResolver targetResolver)
{
	auto service = std::shared_ptr<VansCameraActionService>(
		new VansCameraActionService(runtime, std::move(targetResolver)));
	if (!service->InitializeProfiles(assets, error)) return {};
	return service;
}

VansCameraActionService::VansCameraActionService(
	VansCameraRuntime& runtime,
	TargetPositionResolver targetResolver)
	: m_Runtime(&runtime), m_TargetResolver(std::move(targetResolver))
{
	const VansActionServiceCapability* capability = VansFindStandardActionServiceCapability(
		VansMakeStableId<VansActionServiceIdTag>("Service.Camera"));
	if (capability) m_Capability = *capability;
}

VansCameraActionService::~VansCameraActionService()
{
	if (!m_Runtime) return;
	m_Runtime->ReleaseDomain(Domain());
	m_Runtime->ReleaseDomain(TransientDomain());
	for (const auto& [id, shake] : m_Shakes)
	{
		(void)id;
		m_Runtime->UnregisterShake(shake);
	}
	for (const auto& [id, rig] : m_Rigs)
	{
		(void)id;
		m_Runtime->UnregisterRig(rig);
	}
}

bool VansCameraActionService::InitializeProfiles(
	const VansGameplayAssetLibrary& assets,
	std::string& error)
{
	if (!m_Runtime || !m_Capability.service)
	{
		error = "Camera Action Service capability or runtime is unavailable";
		return false;
	}
	m_Assets = &assets;
	for (const VansCameraRigDefinition& definition : assets.CameraRigs())
	{
		const VansCameraRigHandle handle = m_Runtime->RegisterRig(definition, error);
		if (!handle)
		{
			for (const auto& [id, registered] : m_Rigs)
			{
				(void)id;
				m_Runtime->UnregisterRig(registered);
			}
			m_Rigs.clear();
			return false;
		}
		m_Rigs.emplace(definition.id, handle);
	}
	for (const VansCameraShakeDefinition& definition : assets.CameraShakes())
	{
		const VansCameraShakeHandle handle = m_Runtime->RegisterShake(definition, error);
		if (!handle)
		{
			for (const auto& [id, registered] : m_Shakes)
			{
				(void)id;
				m_Runtime->UnregisterShake(registered);
			}
			m_Shakes.clear();
			for (const auto& [id, registered] : m_Rigs)
			{
				(void)id;
				m_Runtime->UnregisterRig(registered);
			}
			m_Rigs.clear();
			return false;
		}
		m_Shakes.emplace(definition.id, handle);
	}
	return true;
}

std::uint64_t VansCameraActionService::ResourceKey(VansGenerationHandle resource)
{
	return (static_cast<std::uint64_t>(resource.generation) << 32u) | resource.index;
}

VansSerializedValue VansCameraActionService::ResourceValue(VansGenerationHandle resource)
{
	return VansSerializedValue::Object({
		{ "index", VansSerializedValue::Int(resource.index) },
		{ "generation", VansSerializedValue::Int(resource.generation) }
	});
}

VansActionCommandResult VansCameraActionService::CreateContribution(
	VansCameraContribution contribution)
{
	const VansGenerationHandle owner = m_OwnerTokens.Emplace(OwnerToken{});
	contribution.owner = { Domain(), owner };
	std::string error;
	const VansCameraContributionHandle handle =
		m_Runtime->AddContribution(std::move(contribution), error);
	if (!handle)
	{
		m_OwnerTokens.Release(owner);
		return Failure(VansActionError::ExecutionFailed, std::move(error));
	}
	m_ResourceOwners.emplace(ResourceKey(handle.value), owner);
	VansSerializedValue payload = VansSerializedValue::Object({});
	SetSerializedObjectField(payload, "resource", ResourceValue(handle.value));
	return { VansActionError::None, handle.value, std::move(payload), {} };
}

VansActionCommandResult VansCameraActionService::ExecuteShot(const VansActionCommand& command)
{
	const std::string name = ReadSerializedStringField(command.payload, "rig");
	const VansCameraRigDefinition* referenced = m_Assets ? m_Assets->ResolveCameraRig(name) : nullptr;
	const auto found = m_Rigs.find(referenced ? referenced->id :
		VansMakeStableId<VansCameraRigIdTag>(name));
	const VansCameraRigDefinition* rig = found == m_Rigs.end()
		? nullptr : m_Runtime->ResolveRig(found->second);
	if (!rig) return Failure(VansActionError::DefinitionMissing, "Camera Shot rig is unresolved");
	VansCameraContribution contribution;
	contribution.view = ViewId(command.payload);
	contribution.kind = VansCameraContributionKind::Shot;
	contribution.value = rig->initialView;
	contribution.rig = found->second;
	contribution.bindingContext = {
		command.context.owner.index, command.context.owner.generation };
	contribution.blendMode = VansCameraBlendMode::Exclusive;
	contribution.order.priority = Priority(command.payload);
	VansActionCommandResult result = CreateContribution(std::move(contribution));
	if (result) m_Runtime->Advance(0.0);
	return result;
}

VansActionCommandResult VansCameraActionService::ExecuteLens(const VansActionCommand& command)
{
	const VansSerializedValue* lens = FindObjectField(command.payload, "lens");
	if (!lens || lens->kind != VansSerializedValue::Kind::Object)
		return Failure(VansActionError::DefinitionInvalid, "Camera Lens payload is invalid");
	VansCameraContribution contribution;
	contribution.view = ViewId(command.payload);
	contribution.kind = VansCameraContributionKind::Lens;
	contribution.value = m_Runtime->ResolveView(contribution.view).snapshot;
	contribution.order.priority = Priority(command.payload);
	contribution.channels = 0;
	if (const VansSerializedValue* value = FindObjectField(*lens, "fieldOfView"))
	{
		contribution.value.lens.fieldOfView = static_cast<float>(ReadSerializedNumber(*value));
		contribution.channels |= VansCameraChannel_FieldOfView;
	}
	if (const VansSerializedValue* value = FindObjectField(*lens, "nearClip"))
	{
		contribution.value.lens.nearClip = static_cast<float>(ReadSerializedNumber(*value));
		contribution.channels |= VansCameraChannel_NearClip;
	}
	if (const VansSerializedValue* value = FindObjectField(*lens, "nearPlane"))
	{
		contribution.value.lens.nearClip = static_cast<float>(ReadSerializedNumber(*value));
		contribution.channels |= VansCameraChannel_NearClip;
	}
	if (const VansSerializedValue* value = FindObjectField(*lens, "farClip"))
	{
		contribution.value.lens.farClip = static_cast<float>(ReadSerializedNumber(*value));
		contribution.channels |= VansCameraChannel_FarClip;
	}
	if (const VansSerializedValue* value = FindObjectField(*lens, "farPlane"))
	{
		contribution.value.lens.farClip = static_cast<float>(ReadSerializedNumber(*value));
		contribution.channels |= VansCameraChannel_FarClip;
	}
	if (contribution.channels == 0)
		return Failure(VansActionError::DefinitionInvalid, "Camera Lens has no recognized channel");
	return CreateContribution(std::move(contribution));
}

VansActionCommandResult VansCameraActionService::ExecuteShake(const VansActionCommand& command)
{
	const std::string name = ReadSerializedStringField(command.payload, "shake");
	const VansCameraShakeDefinition* referenced = m_Assets ? m_Assets->ResolveCameraShake(name) : nullptr;
	const auto found = m_Shakes.find(referenced ? referenced->id :
		VansMakeStableId<VansCameraShakeIdTag>(name));
	if (found == m_Shakes.end())
		return Failure(VansActionError::DefinitionMissing, "Camera Shake profile is unresolved");
	VansCameraContribution contribution;
	contribution.view = ViewId(command.payload);
	contribution.kind = VansCameraContributionKind::Shake;
	contribution.blendMode = VansCameraBlendMode::Additive;
	contribution.space = VansCameraSpace::CameraLocal;
	contribution.channels = VansCameraChannel_Position | VansCameraChannel_Rotation;
	contribution.shake = found->second;
	const VansSerializedValue* scale = FindObjectField(command.payload, "scale");
	contribution.shakeScale = static_cast<float>(
		scale ? ReadSerializedNumber(*scale, 1.0) : 1.0);
	contribution.shakeSeed = command.context.randomSeed;
	return CreateContribution(std::move(contribution));
}

VansActionCommandResult VansCameraActionService::ExecuteImpulse(const VansActionCommand& command)
{
	VansCameraContribution contribution;
	contribution.view = ViewId(command.payload);
	if (m_NextTransientWriter == UINT32_MAX) m_NextTransientWriter = 1;
	contribution.owner = { TransientDomain(), { m_NextTransientWriter++, 1 } };
	contribution.kind = VansCameraContributionKind::Impulse;
	contribution.blendMode = VansCameraBlendMode::Additive;
	contribution.space = VansCameraSpace::CameraLocal;
	contribution.channels = VansCameraChannel_Position | VansCameraChannel_Rotation;
	contribution.value.pose.position = Vector3(FindObjectField(command.payload, "translation"));
	contribution.value.pose.rotationDegrees = Vector3(FindObjectField(command.payload, "rotation"));
	contribution.consumeAfterResolve = true;
	std::string error;
	if (!m_Runtime->AddContribution(std::move(contribution), error))
		return Failure(VansActionError::ExecutionFailed, std::move(error));
	return {};
}

VansActionCommandResult VansCameraActionService::ExecuteLockOn(const VansActionCommand& command)
{
	glm::vec3 target;
	const VansSerializedValue* targetValue = FindObjectField(command.payload, "target");
	bool resolved = targetValue && targetValue->kind == VansSerializedValue::Kind::Object &&
		FindObjectField(*targetValue, "x") && FindObjectField(*targetValue, "y") &&
		FindObjectField(*targetValue, "z");
	if (resolved) target = Vector3(targetValue);
	else if (m_TargetResolver && command.context.primaryTarget.IsValid())
		resolved = m_TargetResolver(command.context.primaryTarget, target);
	if (!resolved)
		return Failure(VansActionError::TargetInvalid, "Camera LockOn target position is unresolved");
	VansCameraContribution contribution;
	contribution.view = ViewId(command.payload);
	contribution.kind = VansCameraContributionKind::LockOn;
	contribution.value = m_Runtime->ResolveView(contribution.view).snapshot;
	const glm::vec3 direction = target - contribution.value.pose.position;
	const float length = glm::length(direction);
	if (length <= 0.0001f)
		return Failure(VansActionError::TargetInvalid, "Camera LockOn target overlaps the camera");
	const glm::vec3 normalized = direction / length;
	contribution.value.pose.rotationDegrees.x = glm::degrees(std::asin(
		std::clamp(normalized.y, -1.0f, 1.0f)));
	contribution.value.pose.rotationDegrees.y = glm::degrees(std::atan2(normalized.z, normalized.x));
	contribution.channels = VansCameraChannel_Rotation;
	contribution.order.priority = Priority(command.payload);
	return CreateContribution(std::move(contribution));
}

VansActionCommandResult VansCameraActionService::ExecuteUpdateLockOn(
	const VansActionCommand& command)
{
	VansGenerationHandle resource;
	if (!ReadResource(command.payload, resource) ||
		m_ResourceOwners.find(ResourceKey(resource)) == m_ResourceOwners.end())
		return Failure(VansActionError::InvalidHandle, "Camera LockOn resource is invalid");
	const VansCameraContribution* current =
		m_Runtime->ResolveContribution({ resource });
	if (!current || current->kind != VansCameraContributionKind::LockOn)
		return Failure(VansActionError::InvalidHandle, "Camera resource is not an active LockOn");
	glm::vec3 target;
	const VansSerializedValue* targetValue = FindObjectField(command.payload, "target");
	bool resolved = targetValue && targetValue->kind == VansSerializedValue::Kind::Object &&
		FindObjectField(*targetValue, "x") && FindObjectField(*targetValue, "y") &&
		FindObjectField(*targetValue, "z");
	if (resolved) target = Vector3(targetValue);
	else if (m_TargetResolver && command.context.primaryTarget.IsValid())
		resolved = m_TargetResolver(command.context.primaryTarget, target);
	if (!resolved)
		return Failure(VansActionError::TargetInvalid, "Camera LockOn target position is unresolved");
	VansCameraContribution contribution = *current;
	const VansCameraViewSnapshot view = m_Runtime->ResolveView(contribution.view).snapshot;
	const glm::vec3 direction = target - view.pose.position;
	const float length = glm::length(direction);
	if (length <= 0.0001f)
		return Failure(VansActionError::TargetInvalid, "Camera LockOn target overlaps the camera");
	const glm::vec3 normalized = direction / length;
	contribution.value.pose.rotationDegrees.x = glm::degrees(std::asin(
		std::clamp(normalized.y, -1.0f, 1.0f)));
	contribution.value.pose.rotationDegrees.y = glm::degrees(std::atan2(normalized.z, normalized.x));
	std::string error;
	if (!m_Runtime->UpdateContribution({ resource }, std::move(contribution), error))
		return Failure(VansActionError::ExecutionFailed, std::move(error));
	return {};
}

VansActionCommandResult VansCameraActionService::ExecuteRelease(const VansActionCommand& command)
{
	VansGenerationHandle resource;
	if (!ReadResource(command.payload, resource))
		return Failure(VansActionError::InvalidHandle, "Camera Release resource is invalid");
	std::string error;
	if (!Release(resource, error))
		return Failure(VansActionError::InvalidHandle, std::move(error));
	return {};
}

VansActionCommandResult VansCameraActionService::Execute(const VansActionCommand& command)
{
	if (command.stableName == "Camera.Shot") return ExecuteShot(command);
	if (command.stableName == "Camera.Lens") return ExecuteLens(command);
	if (command.stableName == "Camera.Shake") return ExecuteShake(command);
	if (command.stableName == "Camera.Impulse") return ExecuteImpulse(command);
	if (command.stableName == "Camera.LockOn") return ExecuteLockOn(command);
	if (command.stableName == "Camera.UpdateLockOn") return ExecuteUpdateLockOn(command);
	if (command.stableName == "Camera.Release") return ExecuteRelease(command);
	return Failure(VansActionError::DefinitionInvalid, "Camera command is unsupported");
}

bool VansCameraActionService::Release(VansGenerationHandle resource, std::string& error)
{
	const auto found = m_ResourceOwners.find(ResourceKey(resource));
	if (found == m_ResourceOwners.end())
	{
		error = "Camera resource handle is stale or belongs to another service";
		return false;
	}
	m_Runtime->ReleaseContribution({ resource });
	m_OwnerTokens.Release(found->second);
	m_ResourceOwners.erase(found);
	return true;
}
}
