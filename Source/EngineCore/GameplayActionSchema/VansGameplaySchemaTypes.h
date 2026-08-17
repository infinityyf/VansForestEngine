#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../RuntimeCore/VansStableIdentity.h"
#include "../SceneRuntime/VansRuntimeHandle.h"

#include <cstdint>
#include <string>
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

struct VansPredictionKey
{
	std::uint32_t connection = 0;
	std::uint32_t sequence = 0;

	bool IsValid() const { return sequence != 0; }
	friend bool operator==(const VansPredictionKey& left, const VansPredictionKey& right)
	{
		return left.connection == right.connection && left.sequence == right.sequence;
	}
};

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
	InvalidHandle,
	DefinitionMissing,
	DefinitionInvalid,
	NotGranted,
	RequirementsFailed,
	TargetInvalid,
	CostUnavailable,
	CooldownActive,
	ConcurrencyBlocked,
	AuthorityDenied,
	ServiceMissing,
	CommitFailed,
	ExecutionFailed,
	Cancelled,
	TimedOut,
	InternalInvariant,
	InvalidState,
	ConcurrencyRejected,
	ConcurrencyQueueExpired,
	BudgetExceeded
};

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

	explicit operator bool() const { return error == VansActionError::None; }
};

struct VansActionContext
{
	VansEntityHandle owner;
	VansEntityHandle instigator;
	VansEntityHandle source;
	VansEntityHandle primaryTarget;
	VansTargetDataHandle targetData;
	VansPredictionKey predictionKey;
	std::uint64_t randomSeed = 0;
	VansSerializedValue payload = VansSerializedValue::Object({});
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
