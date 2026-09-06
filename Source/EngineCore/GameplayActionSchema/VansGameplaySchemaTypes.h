#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../RuntimeCore/VansStableIdentity.h"
#include "../SceneRuntime/VansRuntimeHandle.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Vans
{
struct VansGameplayTagIdTag;
struct VansActionIdTag;
struct VansActionSetIdTag;
struct VansAttributeIdTag;
struct VansEffectIdTag;
struct VansCueIdTag;
struct VansTargetingPolicyIdTag;
struct VansActionServiceIdTag;
struct VansActionExecutorIdTag;
struct VansActionGraphNodeTypeIdTag;
struct VansActionConcurrencyGroupIdTag;
struct VansActionPayloadTypeIdTag;
struct VansActionFieldIdTag;

using VansGameplayTagId = VansStableId<VansGameplayTagIdTag>;
using VansActionId = VansStableId<VansActionIdTag>;
using VansActionSetId = VansStableId<VansActionSetIdTag>;
using VansAttributeId = VansStableId<VansAttributeIdTag>;
using VansEffectId = VansStableId<VansEffectIdTag>;
using VansCueId = VansStableId<VansCueIdTag>;
using VansTargetingPolicyId = VansStableId<VansTargetingPolicyIdTag>;
using VansActionServiceId = VansStableId<VansActionServiceIdTag>;
using VansActionExecutorId = VansStableId<VansActionExecutorIdTag>;
using VansActionGraphNodeTypeId = VansStableId<VansActionGraphNodeTypeIdTag>;
using VansActionConcurrencyGroupId = VansStableId<VansActionConcurrencyGroupIdTag>;
using VansActionPayloadTypeId = VansStableId<VansActionPayloadTypeIdTag>;
using VansActionFieldId = VansStableId<VansActionFieldIdTag>;

template <typename Tag>
struct VansGameplayHandle
{
	VansGenerationHandle value;

	constexpr bool IsValid() const { return value.IsValid(); }
	constexpr explicit operator bool() const { return IsValid(); }
	friend constexpr bool operator==(VansGameplayHandle left, VansGameplayHandle right)
	{
		return left.value == right.value;
	}
	friend constexpr bool operator!=(VansGameplayHandle left, VansGameplayHandle right)
	{
		return !(left == right);
	}
};

struct VansActionSpecHandleTag;
struct VansActionSetHandleTag;
struct VansActionHandleTag;
struct VansActionTaskHandleTag;
struct VansActionResourceHandleTag;
struct VansAttributeModifierHandleTag;
struct VansActiveEffectHandleTag;
struct VansCueHandleTag;
struct VansTargetDataHandleTag;
struct VansActionSchedulerHandleTag;

using VansActionSpecHandle = VansGameplayHandle<VansActionSpecHandleTag>;
using VansActionSetHandle = VansGameplayHandle<VansActionSetHandleTag>;
using VansActionHandle = VansGameplayHandle<VansActionHandleTag>;
using VansActionTaskHandle = VansGameplayHandle<VansActionTaskHandleTag>;
using VansActionResourceHandle = VansGameplayHandle<VansActionResourceHandleTag>;
using VansAttributeModifierHandle = VansGameplayHandle<VansAttributeModifierHandleTag>;
using VansActiveEffectHandle = VansGameplayHandle<VansActiveEffectHandleTag>;
using VansCueHandle = VansGameplayHandle<VansCueHandleTag>;
using VansTargetDataHandle = VansGameplayHandle<VansTargetDataHandleTag>;
using VansActionSchedulerHandle = VansGameplayHandle<VansActionSchedulerHandleTag>;

enum class VansGameplayDiagnosticSeverity : std::uint8_t
{
	Info,
	Warning,
	Error,
	Fatal
};

struct VansGameplayDiagnostic
{
	VansGameplayDiagnosticSeverity severity = VansGameplayDiagnosticSeverity::Error;
	std::string code;
	std::string message;
	std::string assetPath;
	std::string fieldPath;
};

using VansGameplayDiagnostics = std::vector<VansGameplayDiagnostic>;

enum class VansActionError : std::uint16_t
{
	None,
	InvalidDefinition,
	Rejected,
	Dependency,
	Execution,
	Timeout,
	Cancelled,
	Resource,
	Budget,
	Internal
};

inline const char* VansActionErrorCategoryName(VansActionError error)
{
	switch (error)
	{
	case VansActionError::None: return "None";
	case VansActionError::InvalidDefinition: return "InvalidDefinition";
	case VansActionError::Rejected: return "Rejected";
	case VansActionError::Dependency: return "Dependency";
	case VansActionError::Execution: return "Execution";
	case VansActionError::Timeout: return "Timeout";
	case VansActionError::Cancelled: return "Cancelled";
	case VansActionError::Resource: return "Resource";
	case VansActionError::Budget: return "Budget";
	case VansActionError::Internal: return "Internal";
	}
	return "Internal";
}

inline const char* VansActionDefaultReasonCode(VansActionError error)
{
	switch (error)
	{
	case VansActionError::None: return "";
	case VansActionError::InvalidDefinition: return "Core.InvalidDefinition";
	case VansActionError::Rejected: return "Core.Rejected";
	case VansActionError::Dependency: return "Core.Dependency";
	case VansActionError::Execution: return "Core.Execution";
	case VansActionError::Timeout: return "Core.Timeout";
	case VansActionError::Cancelled: return "Core.Cancelled";
	case VansActionError::Resource: return "Core.Resource";
	case VansActionError::Budget: return "Core.Budget";
	case VansActionError::Internal: return "Core.Internal";
	}
	return "Core.Internal";
}

enum class VansActionActivationDisposition : std::uint8_t
{
	Activated,
	Queued
};

struct VansActionResult
{
	VansActionError error = VansActionError::None;
	VansActionHandle action;
	std::string message;
	VansActionActivationDisposition disposition = VansActionActivationDisposition::Activated;
	std::string reasonCode;

	explicit operator bool() const { return error == VansActionError::None; }
	std::string_view StableReasonCode() const
	{
		return reasonCode.empty() ? std::string_view(VansActionDefaultReasonCode(error))
			: std::string_view(reasonCode);
	}
};

enum class VansActionValueKind : std::uint8_t
{
	Serialized,
	Entity,
	TargetData,
	Resource
};

struct VansActionValue
{
	VansActionValueKind kind = VansActionValueKind::Serialized;
	VansSerializedValue serialized = VansSerializedValue::Object({});
	VansEntityHandle entity;
	VansTargetDataHandle targetData;
	VansGenerationHandle resource;

	static VansActionValue Serialized(VansSerializedValue value)
	{
		VansActionValue result;
		result.serialized = std::move(value);
		return result;
	}
	static VansActionValue Entity(VansEntityHandle value)
	{
		VansActionValue result;
		result.kind = VansActionValueKind::Entity;
		result.entity = value;
		return result;
	}
	static VansActionValue TargetData(VansTargetDataHandle value)
	{
		VansActionValue result;
		result.kind = VansActionValueKind::TargetData;
		result.targetData = value;
		return result;
	}
	static VansActionValue Resource(VansGenerationHandle value)
	{
		VansActionValue result;
		result.kind = VansActionValueKind::Resource;
		result.resource = value;
		return result;
	}
};

struct VansActionContextSlot
{
	VansActionFieldId id;
	std::string name;
	VansActionValue value;
};

namespace VansActionContextSlots
{
inline constexpr std::string_view Owner = "Core.Owner";
inline constexpr std::string_view Instigator = "Core.Instigator";
inline constexpr std::string_view Source = "Core.Source";
inline constexpr std::string_view PrimaryTarget = "Core.PrimaryTarget";
inline constexpr std::string_view TargetData = "Core.TargetData";
inline constexpr std::string_view Payload = "Core.Payload";
}

struct VansActionContext
{
	std::uint64_t correlationId = 0;
	std::uint64_t randomSeed = 0;

	bool Set(std::string_view name, VansActionValue value)
	{
		if (name.empty()) return false;
		const VansActionFieldId id = VansMakeStableId<VansActionFieldIdTag>(name);
		for (VansActionContextSlot& slot : slots)
			if (slot.id == id)
			{
				slot.name = std::string(name);
				slot.value = std::move(value);
				return true;
			}
		slots.push_back({ id, std::string(name), std::move(value) });
		return true;
	}
	bool Remove(std::string_view name)
	{
		const VansActionFieldId id = VansMakeStableId<VansActionFieldIdTag>(name);
		for (auto it = slots.begin(); it != slots.end(); ++it)
			if (it->id == id)
			{
				slots.erase(it);
				return true;
			}
		return false;
	}
	const VansActionValue* Find(std::string_view name) const
	{
		const VansActionFieldId id = VansMakeStableId<VansActionFieldIdTag>(name);
		for (const VansActionContextSlot& slot : slots)
			if (slot.id == id) return &slot.value;
		return nullptr;
	}
	VansActionValue* Find(std::string_view name)
	{
		const VansActionFieldId id = VansMakeStableId<VansActionFieldIdTag>(name);
		for (VansActionContextSlot& slot : slots)
			if (slot.id == id) return &slot.value;
		return nullptr;
	}
	bool SetEntity(std::string_view name, VansEntityHandle value)
	{
		return Set(name, VansActionValue::Entity(value));
	}
	VansEntityHandle Entity(std::string_view name) const
	{
		const VansActionValue* value = Find(name);
		return value && value->kind == VansActionValueKind::Entity ? value->entity : VansEntityHandle{};
	}
	bool SetTargetData(std::string_view name, VansTargetDataHandle value)
	{
		return Set(name, VansActionValue::TargetData(value));
	}
	VansTargetDataHandle TargetData(std::string_view name) const
	{
		const VansActionValue* value = Find(name);
		return value && value->kind == VansActionValueKind::TargetData
			? value->targetData : VansTargetDataHandle{};
	}
	bool SetSerialized(std::string_view name, VansSerializedValue value)
	{
		return Set(name, VansActionValue::Serialized(std::move(value)));
	}
	const VansSerializedValue* Serialized(std::string_view name) const
	{
		const VansActionValue* value = Find(name);
		return value && value->kind == VansActionValueKind::Serialized ? &value->serialized : nullptr;
	}
	VansSerializedValue* Serialized(std::string_view name)
	{
		VansActionValue* value = Find(name);
		return value && value->kind == VansActionValueKind::Serialized ? &value->serialized : nullptr;
	}
	const std::vector<VansActionContextSlot>& Slots() const { return slots; }

private:
	std::vector<VansActionContextSlot> slots;
};
}

namespace std
{
template <typename Tag>
struct hash<Vans::VansGameplayHandle<Tag>>
{
	std::size_t operator()(Vans::VansGameplayHandle<Tag> handle) const noexcept
	{
		const std::size_t first = std::hash<std::uint32_t>{}(handle.value.index);
		const std::size_t second = std::hash<std::uint32_t>{}(handle.value.generation);
		return first ^ (second + 0x9e3779b9u + (first << 6) + (first >> 2));
	}
};
}
