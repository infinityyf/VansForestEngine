#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"
#include "../GameplayTags/VansGameplayTags.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansCompiledActionGraph;
enum class VansActionReplicationPolicy : std::uint8_t
{
	LocalOnly,
	OwnerPredicted,
	ServerAuthoritative,
	Replicated
};

enum class VansActionAuthorityPolicy : std::uint8_t
{
	Any,
	LocalOwner,
	AuthorityOnly
};

enum class VansActionConcurrencyPolicy : std::uint8_t
{
	Allow,
	RejectNew,
	CancelExisting,
	QueueNew
};

enum class VansActionCostRefundPolicy : std::uint8_t
{
	Never,
	OnCommitFailure,
	OnCancel,
	Always
};

enum class VansActionCostKind : std::uint8_t
{
	Attribute,
	Inventory,
	Reservation
};

enum class VansActionRequirementKind : std::uint8_t
{
	Attribute,
	PrimaryTarget,
	TargetData,
	Service
};

enum class VansActionRequirementComparison : std::uint8_t
{
	Less,
	LessOrEqual,
	Equal,
	NotEqual,
	GreaterOrEqual,
	Greater
};

struct VansActionRequirementDefinition
{
	VansActionRequirementKind kind = VansActionRequirementKind::Attribute;
	VansAttributeId attribute;
	VansActionRequirementComparison comparison = VansActionRequirementComparison::GreaterOrEqual;
	double value = 0.0;
	std::uint32_t minimumTargets = 1;
	VansActionServiceId service;
};

enum class VansActionEndPolicy : std::uint8_t
{
	Explicit,
	ExecutorResult,
	TimelineEnd,
	FirstTerminal
};

enum class VansActionTransitionTrigger : std::uint8_t
{
	Event,
	Input,
	Completed,
	Failed
};

struct VansActionTransitionRule
{
	std::string name;
	VansActionTransitionTrigger trigger = VansActionTransitionTrigger::Event;
	VansActionFieldId event;
	std::string inputBinding;
	VansActionId targetAction;
	std::string targetActionReference;
	double minimumTimeSeconds = 0.0;
	double maximumTimeSeconds = -1.0;
	std::int32_t priority = 0;
	bool consumeTrigger = false;
	bool cancelSource = true;
	bool inheritPrimaryTarget = true;
	VansGameplayTagQuery requirements;
	VansSerializedValue contextPatch = VansSerializedValue::Object({});
};

struct VansActionInputBufferPolicy
{
	bool enabled = false;
	double durationSeconds = 0.0;
	std::uint32_t maximumEntries = 1;
};

struct VansActionFailureFallback
{
	VansActionId action;
	std::string actionReference;
	std::vector<VansActionError> errors;
	bool inheritPrimaryTarget = true;
	VansSerializedValue contextPatch = VansSerializedValue::Object({});
};

struct VansActionCostDefinition
{
	VansAttributeId attribute;
	double amount = 0.0;
	VansActionCostRefundPolicy refundPolicy = VansActionCostRefundPolicy::Never;
	VansActionCostKind kind = VansActionCostKind::Attribute;
	std::string resource;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

struct VansActionCooldownDefinition
{
	double durationSeconds = 0.0;
	VansGameplayTagId cooldownTag;
};

struct VansActionEffectReference
{
	VansEffectId effect;
	bool removeOnEnd = false;
	std::string assetReference;
};

struct VansActionVariableDefinition
{
	VansActionFieldId id;
	std::string name;
	VansSerializedValue defaultValue;
};

struct VansCompiledActionDefinition
{
	VansActionId id;
	std::string name;
	std::string nameSpace;
	std::uint32_t definitionVersion = 1;
	std::uint32_t schemaVersion = 1;
	std::uint64_t contentHash = 0;
	std::string authoringGuid;

	std::vector<VansGameplayTagId> abilityTags;
	std::string category;
	std::int32_t priority = 0;
	VansActionConcurrencyGroupId concurrencyGroup;
	VansActionConcurrencyPolicy concurrencyPolicy = VansActionConcurrencyPolicy::Allow;
	std::uint32_t concurrencyLimit = 1;
	double concurrencyQueueTimeoutSeconds = 0.0;
	VansActionReplicationPolicy replicationPolicy = VansActionReplicationPolicy::LocalOnly;
	VansActionAuthorityPolicy authorityPolicy = VansActionAuthorityPolicy::Any;

	VansGameplayTagQuery activationRequirements;
	VansGameplayTagQuery blockedByTags;
	VansTargetingPolicyId targetingPolicy;
	std::string targetingPolicyReference;
	std::vector<std::string> triggers;

	std::vector<VansActionRequirementDefinition> commitRequirements;
	std::vector<VansActionCostDefinition> costs;
	std::vector<VansActionCooldownDefinition> cooldowns;
	std::vector<VansGameplayTagId> grantedWhileRunning;
	std::vector<VansActionEffectReference> commitEffects;

	VansActionExecutorId executor;
	std::string executionGraphAsset;
	std::shared_ptr<const VansCompiledActionGraph> executionGraph;
	std::vector<std::string> timelineAssets;
	std::vector<VansActionVariableDefinition> variables;
	std::string timeDomain = "Game";
	VansActionEndPolicy endPolicy = VansActionEndPolicy::ExecutorResult;

	bool cancellable = true;
	bool interruptible = true;
	std::vector<VansActionId> blockedActions;
	std::vector<VansActionId> cancelActions;
	std::vector<std::string> blockedActionReferences;
	std::vector<std::string> cancelActionReferences;
	std::vector<VansActionTransitionRule> transitionRules;
	VansActionInputBufferPolicy inputBuffer;
	VansActionFailureFallback failureFallback;

	std::vector<VansCueId> presentationCues;
	std::vector<std::string> presentationCueReferences;
	std::vector<VansActionServiceId> requiredServices;
	std::vector<std::string> assetDependencies;
	VansSerializedValue extensionData = VansSerializedValue::Object({});
};

class VansActionDefinitionRegistry
{
public:
	bool RegisterRevision(std::shared_ptr<const VansCompiledActionDefinition> definition, std::string& error);
	std::shared_ptr<const VansCompiledActionDefinition> ResolveLatest(VansActionId id) const;
	std::shared_ptr<const VansCompiledActionDefinition> ResolveRevision(
		VansActionId id,
		std::uint32_t definitionVersion) const;
	std::uint32_t LatestRevision(VansActionId id) const;
	std::size_t ActionCount() const { return m_Definitions.size(); }
	static VansGameplayDiagnostics Validate(const VansCompiledActionDefinition& definition);

private:
	using RevisionMap = std::unordered_map<std::uint32_t, std::shared_ptr<const VansCompiledActionDefinition>>;
	std::unordered_map<VansActionId, RevisionMap> m_Definitions;
};
}
