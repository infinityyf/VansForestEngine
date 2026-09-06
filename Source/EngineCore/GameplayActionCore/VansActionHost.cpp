#include "VansActionHost.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EventCore/VansEventBus.h"
#include "../EventCore/VansEventLane.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
enum class VansRuntimeRequirementKind : std::uint8_t
{
	Attribute,
	PrimaryTarget,
	TargetData,
	Service
};

enum class VansRuntimeRequirementComparison : std::uint8_t
{
	Less,
	LessOrEqual,
	Equal,
	NotEqual,
	GreaterOrEqual,
	Greater
};

struct VansRuntimeRequirement
{
	VansRuntimeRequirementKind kind = VansRuntimeRequirementKind::Attribute;
	VansAttributeId attribute;
	VansRuntimeRequirementComparison comparison =
		VansRuntimeRequirementComparison::GreaterOrEqual;
	double value = 0.0;
	std::uint32_t minimumTargets = 1;
	VansActionServiceId service;
};

struct VansRuntimeCost
{
	VansAttributeId attribute;
	double amount = 0.0;
	bool attributeCost = true;
	std::string externalOperation;
	std::string resource;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

struct VansRuntimeCooldown
{
	double durationSeconds = 0.0;
	VansGameplayTagId tag;
};

struct VansRuntimeEffect
{
	VansEffectId effect;
	bool removeOnEnd = false;
};

enum class VansRuntimeTransitionTrigger : std::uint8_t
{
	Event,
	Input,
	Completed,
	Failed
};

struct VansRuntimeTransitionRule
{
	std::string name;
	VansRuntimeTransitionTrigger trigger = VansRuntimeTransitionTrigger::Event;
	VansActionFieldId event;
	std::string inputBinding;
	VansActionId targetAction;
	double minimumTimeSeconds = 0.0;
	double maximumTimeSeconds = -1.0;
	std::int32_t priority = 0;
	bool consumeTrigger = false;
	bool cancelSource = true;
	VansGameplayTagQuery requirements;
	VansSerializedValue contextPatch = VansSerializedValue::Object({});
};

struct VansRuntimeInputBufferPolicy
{
	bool enabled = false;
	double durationSeconds = 0.0;
	std::uint32_t maximumEntries = 1;
};

struct VansRuntimeFailureFallback
{
	VansActionId action;
	std::vector<VansActionError> errors;
	VansSerializedValue contextPatch = VansSerializedValue::Object({});
};

struct VansActionRuntimeProjection
{
	VansGameplayTagQuery activationRequirements;
	VansGameplayTagQuery blockedByTags;
	VansTargetingPolicyId targetingPolicy;
	std::vector<VansRuntimeRequirement> commitRequirements;
	std::vector<VansRuntimeCost> costs;
	std::vector<VansRuntimeCooldown> cooldowns;
	std::vector<VansGameplayTagId> runningTags;
	std::vector<VansRuntimeEffect> effects;
	std::vector<VansCueId> cues;
	std::vector<VansActionServiceId> requiredServices;
	std::vector<VansRuntimeTransitionRule> transitionRules;
	VansRuntimeInputBufferPolicy inputBuffer;
	VansRuntimeFailureFallback failureFallback;
};

namespace
{
template <typename Tag>
VansStableId<Tag> RuntimeStableId(std::string_view name)
{
	return name.empty() ? VansStableId<Tag>{} : VansMakeStableId<Tag>(name);
}

std::string RuntimeReference(const VansSerializedValue* value)
{
	if (!value) return {};
	if (value->kind == VansSerializedValue::Kind::String) return value->stringValue;
	if (value->kind != VansSerializedValue::Kind::Object) return {};
	for (const char* field : { "stableId", "id", "guid", "path", "assetGuid", "assetPath" })
	{
		const std::string reference = ReadSerializedStringField(*value, field);
		if (!reference.empty()) return reference;
	}
	return {};
}

std::vector<std::string> RuntimeStringArray(const VansSerializedValue* value)
{
	std::vector<std::string> result;
	if (!value || value->kind != VansSerializedValue::Kind::Array) return result;
	result.reserve(value->arrayItems.size());
	for (const VansSerializedValue& item : value->arrayItems)
	{
		const std::string text = RuntimeReference(&item);
		if (!text.empty()) result.push_back(text);
	}
	return result;
}

double RuntimeNumberField(
	const VansSerializedValue& object,
	const char* name,
	double fallback = 0.0)
{
	const VansSerializedValue* value = FindObjectField(object, name);
	return value ? ReadSerializedNumber(*value, fallback) : fallback;
}

struct ResolvedGrantConfiguration
{
	double level = 1.0;
	std::string inputBinding;
	std::vector<VansGameplayTagId> dynamicTags;
	std::int32_t charges = -1;
	VansActionGrantPersistence persistence = VansActionGrantPersistence::OwnerLifetime;
};

bool ResolveGrantConfiguration(
	const VansActionGrantDesc& desc,
	const VansGameplayTagDictionary* tags,
	ResolvedGrantConfiguration& result,
	std::string& error)
{
	result = {};
	std::unordered_set<std::string> uniqueTypes;
	for (const VansCompiledActionRecord& extension : desc.extensions)
	{
		if (extension.type.empty() || extension.inputs.kind != VansSerializedValue::Kind::Object)
		{
			error = "Action grant extension is invalid";
			return false;
		}
		if (!uniqueTypes.insert(extension.type).second)
		{
			error = "Action grant extension type is duplicated: " + extension.type;
			return false;
		}
		if (extension.type == "Core.Level")
			result.level = RuntimeNumberField(extension.inputs, "value", 1.0);
		else if (extension.type == "Gameplay.Input.Binding")
			result.inputBinding = ReadSerializedStringField(extension.inputs, "binding");
		else if (extension.type == "Gameplay.Tags.Dynamic")
		{
			for (const std::string& tagName :
				RuntimeStringArray(FindObjectField(extension.inputs, "tags")))
			{
				const VansGameplayTagDefinition* tag = tags ? tags->Find(tagName) : nullptr;
				if (!tag)
				{
					error = "Action grant dynamic Tag is unresolved: " + tagName;
					return false;
				}
				result.dynamicTags.push_back(tag->id);
			}
		}
		else if (extension.type == "Gameplay.Charges")
			result.charges = static_cast<std::int32_t>(
				ReadSerializedIntField(extension.inputs, "count", -1));
		else if (extension.type == "Core.Grant.Lifetime")
		{
			const std::string policy =
				ReadSerializedStringField(extension.inputs, "policy", "OwnerLifetime");
			if (policy == "Transient")
				result.persistence = VansActionGrantPersistence::Transient;
			else if (policy == "Persistent")
				result.persistence = VansActionGrantPersistence::Persistent;
			else if (policy != "OwnerLifetime")
			{
				error = "Action grant lifetime policy is invalid: " + policy;
				return false;
			}
		}
	}
	if (!std::isfinite(result.level) || result.level <= 0.0 || result.charges < -1)
	{
		error = "Action grant extension values are invalid";
		return false;
	}
	return true;
}

VansGameplayTagQuery RuntimeTagQuery(const VansSerializedValue* value)
{
	VansGameplayTagQuery query;
	if (!value || value->kind != VansSerializedValue::Kind::Object) return query;
	for (const std::string& tag : RuntimeStringArray(FindObjectField(*value, "all")))
		query.all.push_back(RuntimeStableId<VansGameplayTagIdTag>(tag));
	for (const std::string& tag : RuntimeStringArray(FindObjectField(*value, "any")))
		query.any.push_back(RuntimeStableId<VansGameplayTagIdTag>(tag));
	for (const std::string& tag : RuntimeStringArray(FindObjectField(*value, "none")))
		query.none.push_back(RuntimeStableId<VansGameplayTagIdTag>(tag));
	query.exact = ReadSerializedBoolField(*value, "exact", false);
	return query;
}

VansRuntimeRequirementComparison RuntimeComparison(std::string_view value)
{
	if (value == "Less") return VansRuntimeRequirementComparison::Less;
	if (value == "LessOrEqual") return VansRuntimeRequirementComparison::LessOrEqual;
	if (value == "Equal") return VansRuntimeRequirementComparison::Equal;
	if (value == "NotEqual") return VansRuntimeRequirementComparison::NotEqual;
	if (value == "Greater") return VansRuntimeRequirementComparison::Greater;
	return VansRuntimeRequirementComparison::GreaterOrEqual;
}

VansRuntimeTransitionTrigger RuntimeTransitionTrigger(std::string_view value)
{
	if (value == "Input") return VansRuntimeTransitionTrigger::Input;
	if (value == "Completed") return VansRuntimeTransitionTrigger::Completed;
	if (value == "Failed") return VansRuntimeTransitionTrigger::Failed;
	return VansRuntimeTransitionTrigger::Event;
}

VansActionError RuntimeActionError(std::string_view value)
{
	if (value == "InvalidDefinition") return VansActionError::InvalidDefinition;
	if (value == "Rejected") return VansActionError::Rejected;
	if (value == "Dependency") return VansActionError::Dependency;
	if (value == "Timeout") return VansActionError::Timeout;
	if (value == "Cancelled") return VansActionError::Cancelled;
	if (value == "Resource") return VansActionError::Resource;
	if (value == "Budget") return VansActionError::Budget;
	if (value == "Internal") return VansActionError::Internal;
	return VansActionError::Execution;
}

bool BuildActionRuntimeProjection(
	const VansCompiledActionDefinition& definition,
	std::shared_ptr<const VansActionRuntimeProjection>& output,
	std::string& error)
{
	auto projection = std::make_shared<VansActionRuntimeProjection>();
	for (const std::string& capability : definition.program.capabilities)
		projection->requiredServices.push_back(
			RuntimeStableId<VansActionServiceIdTag>(capability));
	for (const VansCompiledActionRecord& guard : definition.program.activate.guards)
	{
		if (guard.type == "Gameplay.Tags.Require")
			projection->activationRequirements = RuntimeTagQuery(
				FindObjectField(guard.inputs, "query"));
		else if (guard.type == "Gameplay.Tags.Block")
			projection->blockedByTags = RuntimeTagQuery(
				FindObjectField(guard.inputs, "query"));
	}
	for (const VansCompiledActionRecord& operation : definition.program.activate.operations)
		if (operation.type == "Gameplay.Targeting.Resolve")
			projection->targetingPolicy = RuntimeStableId<VansTargetingPolicyIdTag>(
				RuntimeReference(FindObjectField(operation.inputs, "asset")));
	for (const VansCompiledActionRecord& guard : definition.program.commit.guards)
	{
		VansRuntimeRequirement requirement;
		if (guard.type == "Gameplay.Attributes.Compare")
		{
			requirement.kind = VansRuntimeRequirementKind::Attribute;
			requirement.attribute = RuntimeStableId<VansAttributeIdTag>(
				ReadSerializedStringField(guard.inputs, "attribute"));
			requirement.comparison = RuntimeComparison(
				ReadSerializedStringField(guard.inputs, "comparison", "GreaterOrEqual"));
			requirement.value = RuntimeNumberField(guard.inputs, "value", 0.0);
		}
		else if (guard.type == "Core.Target.MinimumCount")
		{
			requirement.kind = VansRuntimeRequirementKind::TargetData;
			requirement.minimumTargets = static_cast<std::uint32_t>((std::max<std::int64_t>)(
				1, ReadSerializedIntField(guard.inputs, "minimumTargets", 1)));
		}
		else if (guard.type == "Core.Target.PrimaryRequired")
			requirement.kind = VansRuntimeRequirementKind::PrimaryTarget;
		else if (guard.type == "Core.Capability.Available")
		{
			requirement.kind = VansRuntimeRequirementKind::Service;
			requirement.service = RuntimeStableId<VansActionServiceIdTag>(
				ReadSerializedStringField(guard.inputs, "capability"));
		}
		else continue;
		projection->commitRequirements.push_back(requirement);
	}
	for (const VansCompiledActionRecord& operation : definition.program.commit.operations)
	{
		if (operation.type == "Gameplay.Attributes.Consume" ||
			operation.type == "Core.ExternalCost.Commit")
		{
			VansRuntimeCost cost;
			cost.attributeCost = operation.type == "Gameplay.Attributes.Consume";
			cost.externalOperation = ReadSerializedStringField(
				operation.inputs, "operation");
			cost.attribute = RuntimeStableId<VansAttributeIdTag>(
				ReadSerializedStringField(operation.inputs, "attribute"));
			cost.amount = RuntimeNumberField(operation.inputs, "amount", 0.0);
			cost.resource = ReadSerializedStringField(operation.inputs, "resource");
			if (const VansSerializedValue* payload = FindObjectField(operation.inputs, "payload"))
				cost.payload = *payload;
			projection->costs.push_back(std::move(cost));
		}
		else if (operation.type == "Gameplay.Cooldown.Apply")
			projection->cooldowns.push_back({
				RuntimeNumberField(operation.inputs, "duration", 0.0),
				RuntimeStableId<VansGameplayTagIdTag>(
					ReadSerializedStringField(operation.inputs, "tag")) });
		else if (operation.type == "Gameplay.Tags.Grant")
			for (const std::string& tag : RuntimeStringArray(
				FindObjectField(operation.inputs, "tags")))
				projection->runningTags.push_back(
					RuntimeStableId<VansGameplayTagIdTag>(tag));
		else if (operation.type == "Gameplay.Effects.Apply")
		{
			const std::string reference = RuntimeReference(
				FindObjectField(operation.inputs, "asset"));
			if (!reference.empty()) projection->effects.push_back({
				RuntimeStableId<VansEffectIdTag>(reference),
				ReadSerializedBoolField(operation.inputs, "removeOnEnd", false) });
		}
	}
	for (const VansCompiledActionRecord& operation : definition.program.execute.operations)
		if (operation.type == "Gameplay.Cue.Emit")
			for (const std::string& cue : RuntimeStringArray(
				FindObjectField(operation.inputs, "assets")))
				projection->cues.push_back(RuntimeStableId<VansCueIdTag>(cue));
	for (const VansCompiledActionRecord& policy : definition.program.policies)
	{
		if (policy.type == "Core.Policy.InputBuffer")
		{
			projection->inputBuffer.enabled =
				ReadSerializedBoolField(policy.inputs, "enabled", false);
			projection->inputBuffer.durationSeconds =
				RuntimeNumberField(policy.inputs, "duration", 0.0);
			projection->inputBuffer.maximumEntries = static_cast<std::uint32_t>(
				(std::max<std::int64_t>)(1,
					ReadSerializedIntField(policy.inputs, "maximumEntries", 1)));
		}
		else if (policy.type == "Core.Policy.Failure")
		{
			projection->failureFallback.action = RuntimeStableId<VansActionIdTag>(
				RuntimeReference(FindObjectField(policy.inputs, "action")));
			for (const std::string& name : RuntimeStringArray(
				FindObjectField(policy.inputs, "errors")))
				projection->failureFallback.errors.push_back(RuntimeActionError(name));
			if (const VansSerializedValue* patch = FindObjectField(policy.inputs, "contextPatch"))
				projection->failureFallback.contextPatch = *patch;
		}
	}
	for (const VansCompiledActionRecord& transition : definition.program.transitions)
	{
		const bool combo = transition.type == "Core.Transition.Combo";
		if (!combo && transition.type != "Core.Transition.Rule") continue;
		VansRuntimeTransitionRule rule;
		rule.name = ReadSerializedStringField(
			transition.inputs, "name", combo ? "Combo" : "Transition");
		rule.trigger = combo ? VansRuntimeTransitionTrigger::Input : RuntimeTransitionTrigger(
			ReadSerializedStringField(transition.inputs, "trigger", "Event"));
		rule.event = RuntimeStableId<VansActionFieldIdTag>(
			ReadSerializedStringField(transition.inputs, "event"));
		rule.inputBinding = ReadSerializedStringField(transition.inputs, "input");
		rule.targetAction = RuntimeStableId<VansActionIdTag>(
			RuntimeReference(FindObjectField(transition.inputs, "target")));
		rule.minimumTimeSeconds = RuntimeNumberField(
			transition.inputs, combo ? "openTime" : "minimumTime", 0.0);
		rule.maximumTimeSeconds = RuntimeNumberField(
			transition.inputs, combo ? "closeTime" : "maximumTime", -1.0);
		rule.priority = static_cast<std::int32_t>(
			ReadSerializedIntField(transition.inputs, "priority", 0));
		rule.consumeTrigger = ReadSerializedBoolField(transition.inputs, "consume", false);
		rule.cancelSource = ReadSerializedBoolField(transition.inputs, "cancelSource", true);
		rule.requirements = RuntimeTagQuery(FindObjectField(transition.inputs, "requirements"));
		if (const VansSerializedValue* patch = FindObjectField(transition.inputs, "contextPatch"))
			rule.contextPatch = *patch;
		projection->transitionRules.push_back(std::move(rule));
	}
	std::stable_sort(projection->transitionRules.begin(), projection->transitionRules.end(),
		[](const VansRuntimeTransitionRule& left, const VansRuntimeTransitionRule& right)
		{ return left.priority > right.priority; });

	for (const VansRuntimeRequirement& requirement : projection->commitRequirements)
		if ((requirement.kind == VansRuntimeRequirementKind::Attribute &&
			(!requirement.attribute || !std::isfinite(requirement.value))) ||
			(requirement.kind == VansRuntimeRequirementKind::Service && !requirement.service))
		{
			error = "Action runtime requirement is invalid";
			return false;
		}
	for (const VansRuntimeCost& cost : projection->costs)
		if (!std::isfinite(cost.amount) || cost.amount < 0.0 ||
			(cost.attributeCost ? !cost.attribute : cost.resource.empty()) ||
			cost.payload.kind != VansSerializedValue::Kind::Object)
		{
			error = "Action runtime cost is invalid";
			return false;
		}
	for (const VansRuntimeCooldown& cooldown : projection->cooldowns)
		if (!std::isfinite(cooldown.durationSeconds) || cooldown.durationSeconds <= 0.0)
		{
			error = "Action runtime cooldown is invalid";
			return false;
		}
	std::unordered_set<std::string> transitionNames;
	for (const VansRuntimeTransitionRule& rule : projection->transitionRules)
	{
		const bool triggerValid =
			(rule.trigger != VansRuntimeTransitionTrigger::Event || rule.event) &&
			(rule.trigger != VansRuntimeTransitionTrigger::Input || !rule.inputBinding.empty());
		if (rule.name.empty() || !transitionNames.insert(rule.name).second ||
			!rule.targetAction || !triggerValid || !std::isfinite(rule.minimumTimeSeconds) ||
			!std::isfinite(rule.maximumTimeSeconds) || rule.minimumTimeSeconds < 0.0 ||
			(rule.maximumTimeSeconds >= 0.0 &&
				rule.maximumTimeSeconds < rule.minimumTimeSeconds) ||
			rule.contextPatch.kind != VansSerializedValue::Kind::Object)
		{
			error = "Action runtime transition record is invalid";
			return false;
		}
	}
	if (!std::isfinite(projection->inputBuffer.durationSeconds) ||
		projection->inputBuffer.durationSeconds < 0.0 ||
		projection->inputBuffer.maximumEntries == 0 ||
		(projection->inputBuffer.enabled && projection->inputBuffer.durationSeconds <= 0.0))
	{
		error = "Action runtime input buffer policy is invalid";
		return false;
	}
	if (projection->failureFallback.action &&
		projection->failureFallback.contextPatch.kind != VansSerializedValue::Kind::Object)
	{
		error = "Action runtime failure fallback is invalid";
		return false;
	}
	output = std::move(projection);
	return true;
}

bool HasQueryTerms(const VansGameplayTagQuery& query)
{
	return !query.all.empty() || !query.any.empty() || !query.none.empty();
}

bool IsExecutingState(VansActionInstanceState state)
{
	return state == VansActionInstanceState::Running ||
		state == VansActionInstanceState::Waiting ||
		state == VansActionInstanceState::Transitioning;
}

void ApplyTransitionContextPatch(
	VansActionContext& context,
	const VansSerializedValue& patch)
{
	if (patch.kind != VansSerializedValue::Kind::Object) return;
	for (const std::string& slot : RuntimeStringArray(FindObjectField(patch, "clearSlots")))
		context.Remove(slot);
	if (const VansSerializedValue* seed = FindObjectField(patch, "randomSeed");
		seed && seed->kind == VansSerializedValue::Kind::Int && seed->intValue >= 0)
		context.randomSeed = static_cast<std::uint64_t>(seed->intValue);
	VansSerializedValue* contextPayload = context.Serialized(VansActionContextSlots::Payload);
	if (!contextPayload || contextPayload->kind != VansSerializedValue::Kind::Object)
	{
		context.SetSerialized(VansActionContextSlots::Payload, VansSerializedValue::Object({}));
		contextPayload = context.Serialized(VansActionContextSlots::Payload);
	}
	if (const VansSerializedValue* payload = FindObjectField(patch, "payload");
		payload && payload->kind == VansSerializedValue::Kind::Object)
		for (const auto& [name, value] : payload->objectFields)
			SetSerializedObjectField(*contextPayload, name, value);
	for (const auto& [name, value] : patch.objectFields)
		if (name != "payload" && name != "randomSeed" && name != "clearSlots")
			SetSerializedObjectField(*contextPayload, name, value);
}

VansActionContext MergeTransitionInputContext(
	const VansActionContext& inherited,
	const VansActionContext& request)
{
	VansActionContext result = inherited;
	for (const VansActionContextSlot& slot : request.Slots())
		if (slot.name != VansActionContextSlots::Payload) result.Set(slot.name, slot.value);
	if (request.correlationId != 0) result.correlationId = request.correlationId;
	if (request.randomSeed != 0) result.randomSeed = request.randomSeed;
	const VansSerializedValue* requestPayload = request.Serialized(VansActionContextSlots::Payload);
	if (requestPayload && requestPayload->kind == VansSerializedValue::Kind::Object)
	{
		VansSerializedValue* resultPayload = result.Serialized(VansActionContextSlots::Payload);
		if (!resultPayload || resultPayload->kind != VansSerializedValue::Kind::Object)
		{
			result.SetSerialized(VansActionContextSlots::Payload, VansSerializedValue::Object({}));
			resultPayload = result.Serialized(VansActionContextSlots::Payload);
		}
		for (const auto& [name, value] : requestPayload->objectFields)
			SetSerializedObjectField(*resultPayload, name, value);
	}
	return result;
}
}

VansActionHost::VansActionHost(
	VansEntityHandle owner,
	VansActionHostDependencies dependencies)
	: m_Owner(owner)
	, m_Dependencies(dependencies)
	, m_Tags(dependencies.tagDictionary)
	, m_Attributes(dependencies.attributeRegistry)
	, m_Cues(dependencies.cueRegistry)
	, m_Effects(&m_Attributes, &m_Tags, &m_Cues,
		dependencies.limits.maximumActiveEffects, &m_TargetData)
{
}

VansActionHost::~VansActionHost()
{
	Shutdown();
}

void VansActionHost::SetEnabled(bool enabled)
{
	if (m_Enabled == enabled) return;
	m_Enabled = enabled;
	if (enabled || !m_Initialized) return;
	std::vector<VansActionHandle> active;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.state != VansActionInstanceState::Ended) active.push_back({ handle });
	});
	for (VansActionHandle handle : active)
	{
		std::string ignored;
		Cancel(handle, VansActionCancelReason::System, ignored);
	}
}

bool VansActionHost::Initialize(std::string& error)
{
	if (m_Initialized) return true;
	if (!m_Owner.IsValid() || !m_Dependencies.definitions || !m_Dependencies.executors ||
		!m_Dependencies.executors->IsSealed() || !m_Dependencies.drivers ||
		!m_Dependencies.drivers->IsSealed() || !m_Dependencies.tagDictionary ||
		!m_Dependencies.tagDictionary->IsSealed() || !m_Dependencies.attributeRegistry ||
		!m_Dependencies.attributeRegistry->IsSealed() || !m_Dependencies.services ||
		!m_Dependencies.services->IsSealed() ||
		m_Dependencies.limits.maximumActiveActions == 0 ||
		m_Dependencies.limits.maximumTasksPerAction == 0 ||
		m_Dependencies.limits.maximumActiveEffects == 0 ||
		m_Dependencies.limits.maximumPayloadBytes == 0)
	{
		error = "Action Host dependencies are incomplete or not sealed";
		return false;
	}
	if (m_Dependencies.cueRegistry && !m_Dependencies.cueRegistry->IsSealed())
	{
		error = "Action Host Cue registry is not sealed";
		return false;
	}
	if (m_Dependencies.targetingPolicies &&
		(!m_Dependencies.targetingPolicies->IsSealed() ||
			!m_Dependencies.targetingHandlers || !m_Dependencies.targetingHandlers->IsSealed()))
	{
		error = "Action Host Targeting registries are not sealed";
		return false;
	}
	if (m_Dependencies.effectRegistry && !m_Dependencies.effectRegistry->IsSealed())
	{
		error = "Action Host Effect registry is not sealed";
		return false;
	}
	if (!m_Attributes.InitializeDefaults(error)) return false;
	m_Initialized = true;
	return true;
}

void VansActionHost::Shutdown()
{
	if (m_ShuttingDown) return;
	m_ShuttingDown = true;
	std::vector<VansActionHandle> actions;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.state != VansActionInstanceState::Ended) actions.push_back({ handle });
	});
	for (VansActionHandle handle : actions)
	{
		ActionInstance* instance = m_Instances.Resolve(handle.value);
		if (instance) End(handle, *instance, VansActionEndReason::OwnerDestroyed,
			VansActionError::Cancelled, "Action Host shutdown");
	}
	m_Instances.Clear();
	m_DeferredRecycle.clear();
	m_ActionSets.Clear();
	m_Specs.Clear();
	m_Effects.Clear();
	m_Cues.Clear();
	m_TargetData.Clear();
	std::vector<std::string> hostResourceErrors;
	m_HostResources.ReleaseAll(hostResourceErrors);
	m_HostResources = {};
	m_Tags.Clear();
	m_Concurrency.clear();
	m_ConcurrencyQueues.clear();
	m_Cooldowns.clear();
	m_PendingTransitions.clear();
	m_History.clear();
	m_ElapsedSeconds = 0.0;
	m_NextTransitionSequence = 1;
	m_Initialized = false;
	m_ShuttingDown = false;
}

VansActionSpecHandle VansActionHost::Grant(const VansActionGrantDesc& desc, std::string& error)
{
	if (!m_Initialized || !desc.action || desc.source == 0)
	{
		error = "Action grant descriptor is invalid or Host is not initialized";
		return {};
	}
	ResolvedGrantConfiguration configuration;
	if (!ResolveGrantConfiguration(desc, m_Dependencies.tagDictionary,
		configuration, error)) return {};
	const auto definition = m_Dependencies.definitions->Resolve(desc.action);
	if (!definition)
	{
		error = "Action Definition is not registered";
		return {};
	}
	std::shared_ptr<const VansActionRuntimeProjection> runtime;
	if (!BuildActionRuntimeProjection(*definition, runtime, error) ||
		!m_Dependencies.services->ValidateRequired(runtime->requiredServices, error)) return {};
	GrantedSpec spec;
	spec.definition = definition;
	spec.runtime = std::move(runtime);
	spec.extensions = desc.extensions;
	spec.level = configuration.level;
	spec.inputBinding = std::move(configuration.inputBinding);
	spec.dynamicTags = std::move(configuration.dynamicTags);
	spec.charges = configuration.charges;
	spec.source = desc.source;
	spec.persistence = configuration.persistence;
	return { m_Specs.Emplace(std::move(spec)) };
}

bool VansActionHost::Revoke(
	VansActionSpecHandle specHandle,
	VansActionRevokePolicy policy,
	std::string& error)
{
	GrantedSpec* spec = m_Specs.Resolve(specHandle.value);
	if (!spec)
	{
		error = "Action Spec handle is stale";
		return false;
	}
	std::vector<VansActionHandle> running;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.sourceSpec == specHandle && instance.state != VansActionInstanceState::Ending &&
			instance.state != VansActionInstanceState::Ended) running.push_back({ handle });
	});
	if (policy == VansActionRevokePolicy::DeferUntilIdle && !running.empty())
	{
		spec->pendingRemoval = true;
		return true;
	}
	if (policy == VansActionRevokePolicy::CancelRunning)
	{
		for (VansActionHandle action : running)
		{
			std::string ignored;
			if (!Cancel(action, VansActionCancelReason::GrantRevoked, ignored))
			{
				ActionInstance* instance = m_Instances.Resolve(action.value);
				if (instance) End(action, *instance, VansActionEndReason::Cancelled,
					VansActionError::Cancelled, "Action grant revoked");
			}
		}
	}
	return m_Specs.Release(specHandle.value);
}

VansActionSetHandle VansActionHost::ApplyActionSet(
	const VansActionSetDefinition& set,
	std::string& error)
{
	if (!m_Initialized || !set.id || set.name.empty() || set.grants.empty())
	{
		error = "ActionSet is invalid or Host is not initialized";
		return {};
	}
	ActionSetState state;
	state.definition = set;
	for (const VansCompiledActionRecord& policy : set.policies)
	{
		if (policy.type != "Core.Grant.Revoke")
		{
			error = "ActionSet policy implementation is not registered: " + policy.type;
			return {};
		}
		const std::string mode =
			ReadSerializedStringField(policy.inputs, "mode", "CancelRunning");
		if (mode == "KeepRunning") state.revokePolicy = VansActionRevokePolicy::KeepRunning;
		else if (mode == "DeferUntilIdle")
			state.revokePolicy = VansActionRevokePolicy::DeferUntilIdle;
		else if (mode != "CancelRunning")
		{
			error = "ActionSet revoke policy is invalid: " + mode;
			return {};
		}
	}
	const VansActionSetHandle handle{ m_ActionSets.Emplace(std::move(state)) };
	ActionSetState* stored = m_ActionSets.Resolve(handle.value);
	stored->source = SourceForActionSet(handle);
	for (const VansActionGrantDesc& grant : set.grants)
	{
		VansActionGrantDesc fromSet = grant;
		fromSet.source = stored->source;
		const VansActionSpecHandle spec = Grant(fromSet, error);
		if (!spec)
		{
			for (VansActionSpecHandle created : stored->specs)
			{
				std::string ignored;
				Revoke(created, VansActionRevokePolicy::KeepRunning, ignored);
			}
			m_ActionSets.Release(handle.value);
			return {};
		}
		stored->specs.push_back(spec);
	}
	for (const VansCompiledActionRecord& initializer : set.initializers)
	{
		const auto* handlers = m_Dependencies.actionSetInitializers;
		const auto found = handlers ? handlers->find(initializer.type) :
			std::unordered_map<std::string, VansActionSetInitializerHandler>::const_iterator{};
		if (!handlers || found == handlers->end())
		{
			error = "ActionSet initializer implementation is not registered: " + initializer.type;
			std::string ignored;
			RevokeActionSet(handle, ignored);
			return {};
		}
		VansActionSetInitializerCleanup cleanup;
		if (!found->second(*this, stored->source, initializer.inputs, cleanup, error))
		{
			std::string ignored;
			RevokeActionSet(handle, ignored);
			return {};
		}
		if (cleanup) stored->initializerCleanup.push_back(std::move(cleanup));
	}
	return handle;
}

bool VansActionHost::RevokeActionSet(VansActionSetHandle handle, std::string& error)
{
	ActionSetState* set = m_ActionSets.Resolve(handle.value);
	if (!set)
	{
		error = "ActionSet handle is stale";
		return false;
	}
	const VansActionRevokePolicy policy = set->revokePolicy;
	const std::vector<VansActionSpecHandle> specs = set->specs;
	std::vector<VansActionSetInitializerCleanup> initializerCleanup =
		std::move(set->initializerCleanup);
	bool succeeded = true;
	for (VansActionSpecHandle spec : specs)
	{
		std::string revokeError;
		if (!Revoke(spec, policy, revokeError))
		{
			succeeded = false;
			error = std::move(revokeError);
		}
	}
	for (auto iterator = initializerCleanup.rbegin();
		iterator != initializerCleanup.rend(); ++iterator)
	{
		std::string cleanupError;
		if (!(*iterator)(*this, cleanupError))
		{
			succeeded = false;
			if (error.empty()) error = std::move(cleanupError);
		}
	}
	m_ActionSets.Release(handle.value);
	return succeeded;
}

std::size_t VansActionHost::RevokeSource(
	std::uint64_t source,
	VansActionRevokePolicy policy)
{
	std::vector<VansActionSpecHandle> specs;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		if (spec.source == source) specs.push_back({ handle });
	});
	for (VansActionSpecHandle spec : specs)
	{
		std::string ignored;
		Revoke(spec, policy, ignored);
	}
	return specs.size();
}

VansActionResult VansActionHost::Activate(const VansActionActivationRequest& request)
{
	VansActionResult result;
	if (!m_Enabled)
	{
		result.error = VansActionError::Internal;
		result.message = "Action Host is disabled";
		return result;
	}
	ProcessTransitions();
	RecycleEnded();
	GrantedSpec* spec = m_Specs.Resolve(request.spec.value);
	if (!spec)
	{
		result.error = VansActionError::Rejected;
		result.message = "Action Spec handle is stale";
		return result;
	}
	if (m_Instances.ActiveCount() >= m_Dependencies.limits.maximumActiveActions)
	{
		result.error = VansActionError::Budget;
		result.message = "Action Host active Action budget exceeded";
		return result;
	}
	const auto& definition = *spec->definition;
	const bool concurrencyFull = definition.concurrencyGroup &&
		ConcurrencyOccupancy(definition.concurrencyGroup) >= definition.concurrencyLimit;
	if (concurrencyFull && definition.concurrencyPolicy == VansActionConcurrencyPolicy::QueueNew)
	{
		if (!ValidateActivation(request, *spec, result, true)) return result;
		return QueueActivation(request, *spec);
	}
	if (!ValidateActivation(request, *spec, result)) return result;
	if (concurrencyFull && definition.concurrencyPolicy == VansActionConcurrencyPolicy::CancelExisting)
	{
		const auto occupied = m_Concurrency.find(definition.concurrencyGroup);
		if (occupied != m_Concurrency.end())
		{
			const std::vector<VansActionHandle> cancellations = occupied->second;
			for (VansActionHandle action : cancellations)
			{
				std::string cancelError;
				if (!Cancel(action, VansActionCancelReason::Concurrency, cancelError))
				{
					result.error = VansActionError::Rejected;
					result.message = "Could not cancel existing Action: " + cancelError;
					return result;
				}
			}
		}
	}
	const VansActionHandle handle{ m_Instances.Emplace(ActionInstance{}) };
	return StartActivation(handle, request, *spec);
}

VansActionResult VansActionHost::ActivateAction(
	VansActionId action,
	VansActionContext context)
{
	VansActionResult result;
	const VansActionSpecHandle spec = FindSpecForAction(action);
	if (!spec)
	{
		result.error = VansActionError::Rejected;
		result.message = "Action is not granted to this Host";
		return result;
	}
	VansActionActivationRequest request;
	request.spec = spec;
	request.context = std::move(context);
	return Activate(request);
}

VansActionResult VansActionHost::CanActivate(
	VansActionSpecHandle specHandle,
	const VansActionContext& context) const
{
	VansActionResult result;
	const GrantedSpec* spec = m_Specs.Resolve(specHandle.value);
	if (!spec)
	{
		result.error = VansActionError::Internal;
		result.message = "Action Spec handle is invalid";
		return result;
	}
	VansActionActivationRequest request;
	request.spec = specHandle;
	request.context = context;
	ValidateActivation(request, *spec, result);
	return result;
}

VansActionResult VansActionHost::CanActivateAction(
	VansActionId action,
	const VansActionContext& context) const
{
	const VansActionSpecHandle spec = FindSpecForAction(action);
	if (!spec)
	{
		VansActionResult result;
		result.error = VansActionError::Rejected;
		result.message = "Action is not granted";
		return result;
	}
	return CanActivate(spec, context);
}

VansActionResult VansActionHost::RequestTransition(
	VansActionHandle source,
	VansActionId targetAction,
	VansActionContext context,
	VansSerializedValue contextPatch,
	bool cancelSource)
{
	VansActionResult result;
	if (!m_Initialized || m_ShuttingDown || !source || !targetAction)
	{
		result.error = VansActionError::Internal;
		result.message = "Action transition request is invalid or Host is unavailable";
		return result;
	}
	const ActionInstance* sourceInstance = m_Instances.Resolve(source.value);
	if (!sourceInstance || !IsExecutingState(sourceInstance->state))
	{
		result.error = VansActionError::Internal;
		result.message = "Action transition source is stale or no longer active";
		return result;
	}
	if (!FindSpecForAction(targetAction))
	{
		result.error = VansActionError::Rejected;
		result.message = "Action transition target is not granted";
		return result;
	}
	const VansEntityHandle contextOwner = context.Entity(VansActionContextSlots::Owner);
	if (contextOwner.IsValid() && contextOwner != m_Owner)
	{
		result.error = VansActionError::Rejected;
		result.message = "Action transition context belongs to a different Host";
		return result;
	}
	if (contextPatch.kind != VansSerializedValue::Kind::Object)
	{
		result.error = VansActionError::InvalidDefinition;
		result.message = "Action transition context patch must be an object";
		return result;
	}
	if (!contextOwner.IsValid()) context.SetEntity(VansActionContextSlots::Owner, m_Owner);
	PendingTransition transition;
	transition.source = source;
	transition.targetAction = targetAction;
	transition.context = std::move(context);
	transition.contextPatch = std::move(contextPatch);
	transition.name = cancelSource ? "GraphTransition" : "GraphSubAction";
	transition.sequence = m_NextTransitionSequence++;
	transition.cancelSource = cancelSource;
	transition.allowEndedSource = true;
	m_PendingTransitions.push_back(std::move(transition));
	result.action = source;
	return result;
}

VansActionResult VansActionHost::ActivateInput(
	std::string_view inputBinding,
	VansActionContext context)
{
	VansActionResult result;
	if (!m_Initialized || !m_Enabled || inputBinding.empty())
	{
		result.error = VansActionError::Internal;
		result.message = "Action input request is invalid or Host is disabled";
		return result;
	}
	struct Candidate
	{
		VansActionHandle source;
		const ActionInstance* instance = nullptr;
		const VansRuntimeTransitionRule* rule = nullptr;
	};
	std::vector<Candidate> candidates;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (!IsExecutingState(instance.state) || !instance.runtime) return;
		for (const VansRuntimeTransitionRule& rule : instance.runtime->transitionRules)
		{
			if (rule.trigger != VansRuntimeTransitionTrigger::Input ||
				rule.inputBinding != inputBinding ||
				(rule.maximumTimeSeconds >= 0.0 &&
					instance.elapsedSeconds > rule.maximumTimeSeconds) ||
				(HasQueryTerms(rule.requirements) && !m_Tags.Matches(rule.requirements))) continue;
			candidates.push_back({ { handle }, &instance, &rule });
		}
	});
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
	{
		if (left.rule->priority != right.rule->priority)
			return left.rule->priority > right.rule->priority;
		if (left.instance->definition->program.metadata.priority !=
			right.instance->definition->program.metadata.priority)
			return left.instance->definition->program.metadata.priority >
				right.instance->definition->program.metadata.priority;
		return left.source.value.index < right.source.value.index;
	});
	if (!candidates.empty())
	{
		const Candidate& candidate = candidates.front();
		PendingTransition transition;
		transition.source = candidate.source;
		transition.targetAction = candidate.rule->targetAction;
		transition.context = MergeTransitionInputContext(candidate.instance->context, context);
		transition.contextPatch = candidate.rule->contextPatch;
		transition.name = candidate.rule->name;
		transition.minimumSourceTime = candidate.rule->minimumTimeSeconds;
		transition.maximumSourceTime = candidate.rule->maximumTimeSeconds;
		transition.priority = candidate.rule->priority;
		transition.sequence = m_NextTransitionSequence++;
		transition.cancelSource = candidate.rule->cancelSource;
		if (candidate.instance->elapsedSeconds + 1e-12 >= candidate.rule->minimumTimeSeconds)
			return ExecuteTransition(transition);
		const VansRuntimeInputBufferPolicy& buffer = candidate.instance->runtime->inputBuffer;
		const std::size_t bufferedForSource = static_cast<std::size_t>(std::count_if(
			m_PendingTransitions.begin(), m_PendingTransitions.end(), [&](const PendingTransition& pending)
			{ return pending.source == candidate.source && !pending.allowEndedSource; }));
		if (!buffer.enabled || bufferedForSource >= buffer.maximumEntries)
		{
			result.error = VansActionError::Rejected;
			result.message = "Action transition window is not open";
			return result;
		}
		transition.expiresAt = m_ElapsedSeconds + buffer.durationSeconds;
		m_PendingTransitions.push_back(std::move(transition));
		result.action = candidate.source;
		result.disposition = VansActionActivationDisposition::Queued;
		result.message = "Action input buffered for a transition window";
		return result;
	}
	VansActionSpecHandle directSpec;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		if (!directSpec && !spec.pendingRemoval && spec.inputBinding == inputBinding)
			directSpec = { handle };
	});
	if (!directSpec)
	{
		result.error = VansActionError::Rejected;
		result.message = "No granted Action is bound to the requested input";
		return result;
	}
	VansActionActivationRequest request;
	request.spec = directSpec;
	request.context = std::move(context);
	return Activate(request);
}

VansActionResult VansActionHost::QueueActivation(
	const VansActionActivationRequest& request,
	const GrantedSpec& spec)
{
	const VansActionHandle handle{ m_Instances.Emplace(ActionInstance{}) };
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	instance->sourceSpec = request.spec;
	instance->definition = spec.definition;
	instance->runtime = spec.runtime;
	instance->context = request.context;
	instance->context.SetEntity(VansActionContextSlots::Owner, m_Owner);
	instance->source = SourceForHandle(handle);
	Transition(*instance, VansActionInstanceState::Created, "instance allocated");
	Transition(*instance, VansActionInstanceState::Queued, "waiting for concurrency slot");
	auto& queue = m_ConcurrencyQueues[spec.definition->concurrencyGroup];
	const auto position = std::find_if(queue.begin(), queue.end(), [&](VansActionHandle queued)
	{
		const ActionInstance* other = m_Instances.Resolve(queued.value);
		return other && other->definition &&
			other->definition->program.metadata.priority <
				spec.definition->program.metadata.priority;
	});
	queue.insert(position, handle);
	VansEventBus::Get().Enqueue(VansActionQueuedEvent{
		m_Owner, handle, spec.definition->id, spec.definition->concurrencyGroup,
		instance->context.correlationId }, VansEventLane::GameLogic);
	VansActionResult result;
	result.action = handle;
	result.message = "Action queued for its concurrency group";
	result.disposition = VansActionActivationDisposition::Queued;
	return result;
}

VansActionResult VansActionHost::StartActivation(
	VansActionHandle handle,
	const VansActionActivationRequest& request,
	GrantedSpec& spec)
{
	VansActionResult result;
	std::string error;
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	if (!instance)
	{
		result.error = VansActionError::Internal;
		result.message = "Action instance handle is stale";
		return result;
	}
	const bool wasQueued = instance->state == VansActionInstanceState::Queued;
	if (!wasQueued)
	{
		instance->sourceSpec = request.spec;
		instance->definition = spec.definition;
		instance->runtime = spec.runtime;
		instance->context = request.context;
		instance->context.SetEntity(VansActionContextSlots::Owner, m_Owner);
		instance->source = SourceForHandle(handle);
		Transition(*instance, VansActionInstanceState::Created, "instance allocated");
	}
	instance->tasks.SetMaximumTasks(m_Dependencies.limits.maximumTasksPerAction);
	RemoveFromConcurrencyQueue(handle, instance->definition->concurrencyGroup);
	std::vector<VansActionHandle> cancelActions;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& active)
	{
		if (active.state == VansActionInstanceState::Queued ||
			active.state == VansActionInstanceState::Ending || active.state == VansActionInstanceState::Ended) return;
		if (std::find(spec.definition->cancelActions.begin(), spec.definition->cancelActions.end(),
			active.definition->id) != spec.definition->cancelActions.end()) cancelActions.push_back({ handle });
	});
	for (VansActionHandle action : cancelActions)
	{
		std::string ignored;
		Cancel(action, VansActionCancelReason::Interrupted, ignored);
	}

	Transition(*instance, VansActionInstanceState::Resolving, "definition resolved");
	if (instance->runtime->targetingPolicy)
	{
		const VansTargetingPolicy* policy = m_Dependencies.targetingPolicies
			? m_Dependencies.targetingPolicies->Resolve(instance->runtime->targetingPolicy) : nullptr;
		if (!policy || !m_Dependencies.targetingHandlers)
		{
			error = "Action TargetingPolicy is unavailable";
			End(handle, *instance, VansActionEndReason::Failed, VansActionError::Dependency, error);
			return { VansActionError::Dependency, handle, error };
		}
		VansTargetData initial;
		VansTargetDataHandle contextTargetData =
			instance->context.TargetData(VansActionContextSlots::TargetData);
		if (contextTargetData)
		{
			const VansTargetData* supplied = m_TargetData.Resolve(contextTargetData);
			if (!supplied)
			{
				error = "Action Context TargetData handle is stale";
				End(handle, *instance, VansActionEndReason::Failed, VansActionError::Rejected, error);
				return { VansActionError::Rejected, handle, error };
			}
			initial = *supplied;
			m_TargetData.Release(contextTargetData);
			instance->context.Remove(VansActionContextSlots::TargetData);
		}
		else if (const VansEntityHandle primaryTarget =
			instance->context.Entity(VansActionContextSlots::PrimaryTarget);
			primaryTarget.IsValid())
			initial.values.push_back(primaryTarget);
		VansTargetingResult targeting = VansTargetingPipeline::Execute(
			*policy, instance->context, *m_Dependencies.targetingHandlers, std::move(initial));
		for (const VansTargetingTraceEntry& trace : targeting.trace)
			instance->trace.push_back({ instance->elapsedSeconds, instance->state,
				"Targeting " + trace.step + ": " + (trace.succeeded ? "ok" : trace.message) });
		if (!targeting)
		{
			error = targeting.message;
			End(handle, *instance, VansActionEndReason::Failed, targeting.error, error);
			return { targeting.error, handle, error };
		}
		contextTargetData = m_TargetData.Store(std::move(targeting.data));
		instance->context.SetTargetData(VansActionContextSlots::TargetData, contextTargetData);
		if (const VansTargetData* resolved = m_TargetData.Resolve(contextTargetData))
			for (const VansTargetDataValue& value : resolved->values)
			{
				if (const auto* entity = std::get_if<VansEntityHandle>(&value))
					{ instance->context.SetEntity(VansActionContextSlots::PrimaryTarget, *entity); break; }
				if (const auto* hit = std::get_if<VansTargetHitResult>(&value); hit && hit->entity.IsValid())
					{ instance->context.SetEntity(VansActionContextSlots::PrimaryTarget, hit->entity); break; }
			}
	}
	else if (const VansTargetDataHandle contextTargetData =
		instance->context.TargetData(VansActionContextSlots::TargetData);
		contextTargetData && !m_TargetData.Resolve(contextTargetData))
	{
		error = "Action Context TargetData handle is stale";
		End(handle, *instance, VansActionEndReason::Failed, VansActionError::Rejected, error);
		return { VansActionError::Rejected, handle, error };
	}
	Transition(*instance, VansActionInstanceState::BuildingContext, "activation context frozen");
	Transition(*instance, VansActionInstanceState::Validating, "activation validated");
	if (!instance->variables.Initialize(instance->definition->variables, error))
	{
		End(handle, *instance, VansActionEndReason::Failed,
			VansActionError::InvalidDefinition, error);
		return { VansActionError::InvalidDefinition, handle, error };
	}
	Transition(*instance, VansActionInstanceState::Preparing, "Executor and variables prepared");
	instance->executor = m_Dependencies.executors->Create(
		instance->definition->executor, *instance->definition, error);
	if (!instance->executor)
	{
		End(handle, *instance, VansActionEndReason::Failed, VansActionError::Execution, error);
		return { VansActionError::Execution, handle, error };
	}
	for (const VansCompiledActionRecord& driverRecord :
		instance->definition->program.execute.drivers)
	{
		std::string driverError;
		auto driver = m_Dependencies.drivers->Create(driverRecord, driverError);
		if (!driverError.empty())
		{
			End(handle, *instance, VansActionEndReason::Failed,
				VansActionError::Dependency, driverError);
			return { VansActionError::Dependency, handle, driverError };
		}
		if (driver) instance->drivers.push_back(std::move(driver));
	}
	Transition(*instance, VansActionInstanceState::Committing, "Commit started");
	if (!CommitActivation(handle, spec, *instance, error))
	{
		End(handle, *instance, VansActionEndReason::CommitFailed, VansActionError::Execution, error);
		return { VansActionError::Execution, handle, error };
	}
	Transition(*instance, VansActionInstanceState::Committed, "Commit completed");
	Transition(*instance, VansActionInstanceState::Running, "Executor started");
	for (VansCueId cue : instance->runtime->cues)
	{
		VansGameplayCueParameters parameters;
		parameters.context = instance->context;
		const VansEntityHandle primaryTarget =
			instance->context.Entity(VansActionContextSlots::PrimaryTarget);
		parameters.target = primaryTarget.IsValid() ? primaryTarget : m_Owner;
		if (const VansSerializedValue* payload =
			instance->context.Serialized(VansActionContextSlots::Payload))
			parameters.payload = *payload;
		const VansGameplayCueKey key{ instance->context.correlationId, cue, m_NextCueSequence++ };
		if (!m_Cues.Execute(key, m_Cues.DefaultScope(cue), parameters, error))
		{
			End(handle, *instance, VansActionEndReason::Failed,
				VansActionError::Execution, error);
			return { VansActionError::Execution, handle, error };
		}
	}
	VansEventBus::Get().Enqueue(VansActionStartedEvent{
		m_Owner, handle, instance->definition->id, instance->context.correlationId },
		VansEventLane::GameLogic);
	VansActionExecutionContext execution = BuildExecutionContext(handle, *instance, 0.0);
	for (const std::unique_ptr<IVansActionSidecarDriver>& driver : instance->drivers)
		if (!driver->Start(execution, error))
		{
			if (error.empty()) error = "Action sidecar Driver failed to start";
			End(handle, *instance, VansActionEndReason::Failed,
				VansActionError::Execution, error);
			return { VansActionError::Execution, handle, error };
		}
	const VansActionExecutorResult started = instance->executor->Start(execution);
	if (started.status == VansActionExecutorStatus::Succeeded)
		End(handle, *instance, VansActionEndReason::Completed, VansActionError::None, started.message);
	else if (started.status == VansActionExecutorStatus::Failed)
		End(handle, *instance, VansActionEndReason::Failed,
			started.error == VansActionError::None ? VansActionError::Execution : started.error,
			started.message);
	else if (started.status == VansActionExecutorStatus::Waiting)
		Transition(*instance, VansActionInstanceState::Waiting, "Executor is waiting");
	result.action = handle;
	return result;
}

bool VansActionHost::Cancel(
	VansActionHandle handle,
	VansActionCancelReason reason,
	std::string& error)
{
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	if (!instance || instance->state == VansActionInstanceState::Ending ||
		instance->state == VansActionInstanceState::Ended)
	{
		error = "Action handle is stale or already ending";
		return false;
	}
	const bool forced = reason == VansActionCancelReason::OwnerDestroyed ||
		reason == VansActionCancelReason::GrantRevoked || reason == VansActionCancelReason::System;
	if (!forced && reason != VansActionCancelReason::Interrupted && !instance->definition->cancellable)
	{
		error = "Action does not allow cancellation";
		return false;
	}
	VansActionExecutionContext execution = BuildExecutionContext(handle, *instance, 0.0);
	if (!forced && instance->executor && !instance->executor->RequestCancel(execution, reason))
	{
		error = "Action Executor rejected cancellation";
		return false;
	}
	const bool interrupted = reason == VansActionCancelReason::Interrupted ||
		reason == VansActionCancelReason::Concurrency;
	End(handle, *instance,
		interrupted ? VansActionEndReason::Interrupted : VansActionEndReason::Cancelled,
		VansActionError::Cancelled, interrupted ? "Action interrupted" : "Action cancelled");
	return true;
}

bool VansActionHost::Interrupt(VansActionHandle handle, std::string& error)
{
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	if (!instance)
	{
		error = "Action handle is stale";
		return false;
	}
	if (!instance->definition->interruptible)
	{
		error = "Action does not allow interruption";
		return false;
	}
	return Cancel(handle, VansActionCancelReason::Interrupted, error);
}

bool VansActionHost::EnqueueEvent(
	VansActionHandle handle,
	VansActionEvent event,
	std::string& error)
{
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	if (!instance || (instance->state != VansActionInstanceState::Running &&
		instance->state != VansActionInstanceState::Waiting &&
		instance->state != VansActionInstanceState::Transitioning) || !event.type)
	{
		error = "Action event target or type is invalid";
		return false;
	}
	instance->recentEvents.push_back({ instance->nextEventSequence++, event.type, event.stableName });
	constexpr std::size_t MaximumDebugEvents = 64;
	if (instance->recentEvents.size() > MaximumDebugEvents)
		instance->recentEvents.erase(instance->recentEvents.begin(),
			instance->recentEvents.begin() + (instance->recentEvents.size() - MaximumDebugEvents));
	bool consumed = false;
	for (const VansRuntimeTransitionRule& rule : instance->runtime->transitionRules)
	{
		if (rule.trigger != VansRuntimeTransitionTrigger::Event || rule.event != event.type ||
			instance->elapsedSeconds + 1e-12 < rule.minimumTimeSeconds ||
			(rule.maximumTimeSeconds >= 0.0 && instance->elapsedSeconds > rule.maximumTimeSeconds) ||
			(HasQueryTerms(rule.requirements) && !m_Tags.Matches(rule.requirements))) continue;
		PendingTransition transition;
		transition.source = handle;
		transition.targetAction = rule.targetAction;
		transition.context = instance->context;
		transition.contextPatch = rule.contextPatch;
		transition.name = rule.name;
		transition.minimumSourceTime = rule.minimumTimeSeconds;
		transition.maximumSourceTime = rule.maximumTimeSeconds;
		transition.expiresAt = m_ElapsedSeconds;
		transition.priority = rule.priority;
		transition.sequence = m_NextTransitionSequence++;
		transition.cancelSource = rule.cancelSource;
		transition.allowEndedSource = true;
		m_PendingTransitions.push_back(std::move(transition));
		consumed = rule.consumeTrigger;
		break;
	}
	if (!consumed) instance->inbox.push_back(std::move(event));
	m_LateContinuationRequested = true;
	return true;
}

bool VansActionHost::ReadVariable(
	VansActionHandle action,
	VansActionFieldId variable,
	VansSerializedValue& value,
	std::string& error) const
{
	const ActionInstance* instance = m_Instances.Resolve(action.value);
	const VansSerializedValue* found = instance ? instance->variables.Get(variable) : nullptr;
	if (!found)
	{
		error = instance ? "Action variable is not declared" : "Action handle is stale";
		return false;
	}
	value = *found;
	return true;
}

bool VansActionHost::WriteVariable(
	VansActionHandle action,
	VansActionFieldId variable,
	VansSerializedValue value,
	std::string& error)
{
	ActionInstance* instance = m_Instances.Resolve(action.value);
	if (!instance)
	{
		error = "Action handle is stale";
		return false;
	}
	if (!instance->variables.Set(variable, std::move(value)))
	{
		error = "Action variable is not declared";
		return false;
	}
	return true;
}

bool VansActionHost::CapturePersistentState(
	VansActionHostPersistentState& state,
	std::string& error) const
{
	if (!m_Initialized || m_ShuttingDown)
	{
		error = "Action Host is not in a stable state for persistence capture";
		return false;
	}
	state = {};
	m_Specs.ForEach([&](VansGenerationHandle, const GrantedSpec& spec)
	{
		if (spec.persistence != VansActionGrantPersistence::Persistent || spec.pendingRemoval) return;
		std::vector<VansCompiledActionRecord> extensions = spec.extensions;
		bool wroteCharges = false;
		for (VansCompiledActionRecord& extension : extensions)
			if (extension.type == "Gameplay.Charges")
			{
				SetSerializedObjectField(extension.inputs, "count",
					VansSerializedValue::Int(spec.charges));
				wroteCharges = true;
			}
		if (!wroteCharges && spec.charges != -1)
			extensions.push_back({ "Gameplay.Charges", VansSerializedValue::Object({
				{ "count", VansSerializedValue::Int(spec.charges) }
			}) });
		state.grants.push_back({ spec.definition->id, std::move(extensions), spec.source });
	});
	std::sort(state.grants.begin(), state.grants.end(), [](const auto& left, const auto& right)
	{
		if (left.action != right.action) return left.action < right.action;
		return left.source < right.source;
	});
	state.attributes = m_Attributes.Capture();
	for (const auto& [action, cooldowns] : m_Cooldowns)
		for (const CooldownState& cooldown : cooldowns)
			if (cooldown.remainingSeconds > 0.0)
				state.cooldowns.push_back({ action, cooldown.remainingSeconds, cooldown.tag });
	std::sort(state.cooldowns.begin(), state.cooldowns.end(), [](const auto& left, const auto& right)
	{
		if (left.action != right.action) return left.action < right.action;
		if (left.tag != right.tag) return left.tag < right.tag;
		return left.remainingSeconds < right.remainingSeconds;
	});
	return true;
}

bool VansActionHost::RestorePersistentState(
	const VansActionHostPersistentState& state,
	std::string& error)
{
	if (!m_Initialized || m_ShuttingDown ||
		!ActiveActions().empty())
	{
		error = "Action Host cannot restore persistence while unavailable or active";
		return false;
	}
	for (const VansPersistentActionGrantState& grant : state.grants)
	{
		const auto definition = m_Dependencies.definitions->Resolve(grant.action);
		std::shared_ptr<const VansActionRuntimeProjection> runtime;
		VansActionGrantDesc descriptor;
		descriptor.action = grant.action;
		descriptor.extensions = grant.extensions;
		descriptor.source = grant.source;
		ResolvedGrantConfiguration configuration;
		if (!definition || grant.source == 0 ||
			!ResolveGrantConfiguration(descriptor, m_Dependencies.tagDictionary,
				configuration, error) ||
			configuration.persistence != VansActionGrantPersistence::Persistent ||
			!BuildActionRuntimeProjection(*definition, runtime, error) ||
			!m_Dependencies.services->ValidateRequired(runtime->requiredServices, error))
		{
			if (error.empty()) error = "Persistent Action grant is invalid or unresolved";
			return false;
		}
	}
	for (const VansAttributeSnapshot& attribute : state.attributes)
		if (!m_Dependencies.attributeRegistry->Resolve(attribute.attribute) ||
			!std::isfinite(attribute.baseValue) || !std::isfinite(attribute.currentValue))
		{
			error = "Persistent Attribute snapshot is invalid or unresolved";
			return false;
		}
	for (const VansPersistentActionCooldownState& cooldown : state.cooldowns)
		if (!m_Dependencies.definitions->Resolve(cooldown.action) ||
			!m_Dependencies.tagDictionary->Resolve(cooldown.tag) ||
			!std::isfinite(cooldown.remainingSeconds) || cooldown.remainingSeconds <= 0.0)
		{
			error = "Persistent cooldown is invalid or unresolved";
			return false;
		}

	std::vector<VansGenerationHandle> persistentSpecs;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		if (spec.persistence == VansActionGrantPersistence::Persistent)
			persistentSpecs.push_back(handle);
	});
	for (VansGenerationHandle handle : persistentSpecs) m_Specs.Release(handle);
	for (const auto& [action, cooldowns] : m_Cooldowns)
		for (const CooldownState& cooldown : cooldowns) m_Tags.RemoveSource(cooldown.tagSource);
	m_Cooldowns.clear();
	m_Attributes.Restore(state.attributes);
	for (const VansPersistentActionGrantState& saved : state.grants)
	{
		VansActionGrantDesc grant;
		grant.action = saved.action;
		grant.extensions = saved.extensions;
		grant.source = saved.source;
		if (!Grant(grant, error)) return false;
	}
	for (const VansPersistentActionCooldownState& saved : state.cooldowns)
	{
		auto& cooldowns = m_Cooldowns[saved.action];
		const std::size_t index = cooldowns.size();
		CooldownState cooldown;
		cooldown.remainingSeconds = saved.remainingSeconds;
		cooldown.tag = saved.tag;
		cooldown.tagSource = SourceForCooldown(saved.action, index);
		if (!m_Tags.Add(cooldown.tag, cooldown.tagSource))
		{
			error = "Persistent cooldown Tag could not be restored";
			return false;
		}
		cooldowns.push_back(cooldown);
	}
	return true;
}

void VansActionHost::Tick(double deltaSeconds)
{
	if (!m_Enabled) return;
	if (!m_Initialized || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) return;
	m_ElapsedSeconds += deltaSeconds;
	ProcessTransitions();
	RecycleEnded();
	TickCooldowns(deltaSeconds);
	m_Effects.Tick(deltaSeconds);
	std::vector<VansActionHandle> handles;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.state == VansActionInstanceState::Running ||
			instance.state == VansActionInstanceState::Waiting ||
			instance.state == VansActionInstanceState::Transitioning) handles.push_back({ handle });
	});
	for (VansActionHandle handle : handles)
	{
		ActionInstance* instance = m_Instances.Resolve(handle.value);
		if (!instance) continue;
		instance->elapsedSeconds += deltaSeconds;
		instance->tasks.Tick(deltaSeconds);
		VansActionExecutionContext execution = BuildExecutionContext(handle, *instance, deltaSeconds);
		bool driversReady = true;
		for (const std::unique_ptr<IVansActionSidecarDriver>& driver : instance->drivers)
		{
			std::string driverError;
			if (driver->Tick(execution, driverError)) continue;
			if (driverError.empty()) driverError = "Action sidecar Driver failed";
			End(handle, *instance, VansActionEndReason::Failed,
				VansActionError::Execution, std::move(driverError));
			driversReady = false;
			break;
		}
		if (!driversReady) continue;
		std::vector<VansActionEvent> inbox = std::move(instance->inbox);
		instance->inbox.clear();
		for (const VansActionEvent& event : inbox)
		{
			if (instance->state == VansActionInstanceState::Ending ||
				instance->state == VansActionInstanceState::Ended) break;
			instance->executor->OnEvent(execution, event);
		}
		if (instance->state == VansActionInstanceState::Ending ||
			instance->state == VansActionInstanceState::Ended) continue;
		const VansActionExecutorResult ticked = instance->executor->Tick(execution);
		if (ticked.status == VansActionExecutorStatus::Succeeded)
			End(handle, *instance, VansActionEndReason::Completed, VansActionError::None, ticked.message);
		else if (ticked.status == VansActionExecutorStatus::Failed)
			End(handle, *instance, VansActionEndReason::Failed,
				ticked.error == VansActionError::None ? VansActionError::Execution : ticked.error,
				ticked.message);
		else if (ticked.status == VansActionExecutorStatus::Waiting &&
			instance->state != VansActionInstanceState::Waiting)
			Transition(*instance, VansActionInstanceState::Waiting, "Executor is waiting");
		else if (ticked.status == VansActionExecutorStatus::Running &&
			instance->state != VansActionInstanceState::Running)
			Transition(*instance, VansActionInstanceState::Running, "Executor resumed");
	}
	ProcessTransitions();
	ProcessConcurrencyQueues(deltaSeconds);
	m_LateContinuationRequested = false;
	ReleaseDeferredSpecs();
}

bool VansActionHost::RunLateContinuation()
{
	if (!m_Enabled) return false;
	if (!m_Initialized || !m_LateContinuationRequested) return false;
	m_LateContinuationRequested = false;
	std::vector<VansActionHandle> handles;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (!instance.inbox.empty() && (instance.state == VansActionInstanceState::Running ||
			instance.state == VansActionInstanceState::Waiting ||
			instance.state == VansActionInstanceState::Transitioning)) handles.push_back({ handle });
	});
	bool ran = false;
	for (VansActionHandle handle : handles)
	{
		ActionInstance* instance = m_Instances.Resolve(handle.value);
		if (!instance) continue;
		ran = true;
		VansActionExecutionContext execution = BuildExecutionContext(handle, *instance, 0.0);
		std::vector<VansActionEvent> inbox = std::move(instance->inbox);
		instance->inbox.clear();
		for (const VansActionEvent& event : inbox) instance->executor->OnEvent(execution, event);
		if (instance->state == VansActionInstanceState::Ending ||
			instance->state == VansActionInstanceState::Ended) continue;
		const VansActionExecutorResult result = instance->executor->Tick(execution);
		if (result.status == VansActionExecutorStatus::Succeeded)
			End(handle, *instance, VansActionEndReason::Completed, VansActionError::None, result.message);
		else if (result.status == VansActionExecutorStatus::Failed)
			End(handle, *instance, VansActionEndReason::Failed,
				result.error == VansActionError::None ? VansActionError::Execution : result.error,
				result.message);
		else if (result.status == VansActionExecutorStatus::Waiting)
			Transition(*instance, VansActionInstanceState::Waiting, "late continuation is waiting");
		else
			Transition(*instance, VansActionInstanceState::Running, "late continuation resumed");
	}
	ProcessTransitions();
	ProcessConcurrencyQueues(0.0);
	ReleaseDeferredSpecs();
	return ran;
}

VansActionSpecHandle VansActionHost::FindSpecForAction(VansActionId action) const
{
	VansActionSpecHandle result;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		if (!result && !spec.pendingRemoval && spec.definition && spec.definition->id == action)
			result = { handle };
	});
	return result;
}

VansActionResult VansActionHost::ExecuteTransition(const PendingTransition& transition)
{
	VansActionResult result;
	ActionInstance* source = m_Instances.Resolve(transition.source.value);
	if (!transition.allowEndedSource && (!source || !IsExecutingState(source->state)))
	{
		result.error = VansActionError::Internal;
		result.message = "Action transition source is no longer active";
		return result;
	}
	if (transition.cancelSource && source && IsExecutingState(source->state) &&
		!source->definition->interruptible)
	{
		result.error = VansActionError::Rejected;
		result.message = "Action transition source does not allow interruption";
		return result;
	}
	const VansActionSpecHandle targetSpec = FindSpecForAction(transition.targetAction);
	if (!targetSpec)
	{
		result.error = VansActionError::Rejected;
		result.message = "Action transition target is not granted";
		return result;
	}
	VansActionActivationRequest request;
	request.spec = targetSpec;
	request.context = transition.context;
	ApplyTransitionContextPatch(request.context, transition.contextPatch);
	result = Activate(request);
	if (!result) return result;
	if (transition.cancelSource && source && IsExecutingState(source->state) &&
		transition.source != result.action)
	{
		std::string cancelError;
		if (!Cancel(transition.source, VansActionCancelReason::Interrupted, cancelError))
		{
			std::string ignored;
			Cancel(result.action, VansActionCancelReason::System, ignored);
			result.error = VansActionError::Execution;
			result.action = {};
			result.message = "Action transition could not end its source: " + cancelError;
			return result;
		}
	}
	if (source)
		source->trace.push_back({ source->elapsedSeconds, source->state,
			"Transition executed: " + transition.name });
	return result;
}

void VansActionHost::ProcessTransitions()
{
	if (m_ProcessingTransitions || m_PendingTransitions.empty() || m_ShuttingDown) return;
	m_ProcessingTransitions = true;
	std::vector<PendingTransition> pending = std::move(m_PendingTransitions);
	m_PendingTransitions.clear();
	std::stable_sort(pending.begin(), pending.end(), [](const PendingTransition& left,
		const PendingTransition& right)
	{
		if (left.priority != right.priority) return left.priority > right.priority;
		return left.sequence < right.sequence;
	});
	std::vector<PendingTransition> retry;
	for (PendingTransition& transition : pending)
	{
		ActionInstance* source = m_Instances.Resolve(transition.source.value);
		if (!source || (!transition.allowEndedSource && !IsExecutingState(source->state))) continue;
		const double sourceTime = source->elapsedSeconds;
		if (transition.maximumSourceTime >= 0.0 &&
			sourceTime > transition.maximumSourceTime + 1e-12) continue;
		if (sourceTime + 1e-12 < transition.minimumSourceTime)
		{
			if (transition.expiresAt > m_ElapsedSeconds + 1e-12)
				retry.push_back(std::move(transition));
			continue;
		}
		const VansActionResult result = ExecuteTransition(transition);
		const bool retryable = result.error == VansActionError::Rejected ||
			result.error == VansActionError::Rejected ||
			result.error == VansActionError::Rejected ||
			result.error == VansActionError::Rejected ||
			result.error == VansActionError::Budget;
		if (!result && retryable && transition.expiresAt > m_ElapsedSeconds + 1e-12)
			retry.push_back(std::move(transition));
	}
	for (PendingTransition& transition : retry)
		m_PendingTransitions.push_back(std::move(transition));
	m_ProcessingTransitions = false;
}

void VansActionHost::QueueTerminalTransitions(
	VansActionHandle handle,
	const ActionInstance& instance,
	VansActionEndReason reason,
	VansActionError error)
{
	if (m_ShuttingDown || !instance.definition) return;
	const bool completed = reason == VansActionEndReason::Completed;
	const bool failed = reason == VansActionEndReason::Failed ||
		reason == VansActionEndReason::CommitFailed || reason == VansActionEndReason::TimedOut;
	if (!completed && !failed) return;
	bool queuedRule = false;
	for (const VansRuntimeTransitionRule& rule : instance.runtime->transitionRules)
	{
		if ((completed && rule.trigger != VansRuntimeTransitionTrigger::Completed) ||
			(failed && rule.trigger != VansRuntimeTransitionTrigger::Failed) ||
			instance.elapsedSeconds + 1e-12 < rule.minimumTimeSeconds ||
			(rule.maximumTimeSeconds >= 0.0 &&
				instance.elapsedSeconds > rule.maximumTimeSeconds) ||
			(HasQueryTerms(rule.requirements) && !m_Tags.Matches(rule.requirements))) continue;
		PendingTransition transition;
		transition.source = handle;
		transition.targetAction = rule.targetAction;
		transition.context = instance.context;
		transition.contextPatch = rule.contextPatch;
		transition.name = rule.name;
		transition.minimumSourceTime = rule.minimumTimeSeconds;
		transition.maximumSourceTime = rule.maximumTimeSeconds;
		transition.priority = rule.priority;
		transition.sequence = m_NextTransitionSequence++;
		transition.cancelSource = false;
		transition.allowEndedSource = true;
		m_PendingTransitions.push_back(std::move(transition));
		queuedRule = true;
		break;
	}
	const VansRuntimeFailureFallback& fallback = instance.runtime->failureFallback;
	const bool handlesError = fallback.errors.empty() ||
		std::find(fallback.errors.begin(), fallback.errors.end(), error) != fallback.errors.end();
	if (!queuedRule && failed && fallback.action && handlesError)
	{
		PendingTransition transition;
		transition.source = handle;
		transition.targetAction = fallback.action;
		transition.context = instance.context;
		transition.contextPatch = fallback.contextPatch;
		transition.name = "FailureFallback";
		transition.sequence = m_NextTransitionSequence++;
		transition.cancelSource = false;
		transition.allowEndedSource = true;
		m_PendingTransitions.push_back(std::move(transition));
	}
}

std::size_t VansActionHost::ConcurrencyOccupancy(VansActionConcurrencyGroupId group) const
{
	const auto found = m_Concurrency.find(group);
	return found == m_Concurrency.end() ? 0 : found->second.size();
}

void VansActionHost::RemoveFromConcurrencyQueue(
	VansActionHandle handle,
	VansActionConcurrencyGroupId group)
{
	if (!group) return;
	const auto found = m_ConcurrencyQueues.find(group);
	if (found == m_ConcurrencyQueues.end()) return;
	auto& queue = found->second;
	queue.erase(std::remove(queue.begin(), queue.end(), handle), queue.end());
}

void VansActionHost::ProcessConcurrencyQueues(double deltaSeconds)
{
	if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 || m_ConcurrencyQueues.empty()) return;
	std::vector<VansActionConcurrencyGroupId> groups;
	groups.reserve(m_ConcurrencyQueues.size());
	for (const auto& entry : m_ConcurrencyQueues) groups.push_back(entry.first);
	for (VansActionConcurrencyGroupId group : groups)
	{
		const auto queued = m_ConcurrencyQueues.find(group);
		if (queued == m_ConcurrencyQueues.end()) continue;
		const std::vector<VansActionHandle> timeoutCandidates(
			queued->second.begin(), queued->second.end());
		for (VansActionHandle handle : timeoutCandidates)
		{
			ActionInstance* instance = m_Instances.Resolve(handle.value);
			if (!instance || instance->state != VansActionInstanceState::Queued)
			{
				RemoveFromConcurrencyQueue(handle, group);
				continue;
			}
			instance->elapsedSeconds += deltaSeconds;
			const double timeout = instance->definition->concurrencyQueueTimeoutSeconds;
			if (timeout > 0.0 && instance->elapsedSeconds >= timeout)
				End(handle, *instance, VansActionEndReason::TimedOut,
					VansActionError::Timeout,
					"Action concurrency queue entry expired");
		}

		std::size_t transitionGuard = 0;
		while (++transitionGuard <= m_Dependencies.limits.maximumActiveActions)
		{
			const auto currentQueue = m_ConcurrencyQueues.find(group);
			if (currentQueue == m_ConcurrencyQueues.end() || currentQueue->second.empty()) break;
			const VansActionHandle handle = currentQueue->second.front();
			ActionInstance* instance = m_Instances.Resolve(handle.value);
			if (!instance || instance->state != VansActionInstanceState::Queued || !instance->definition)
			{
				currentQueue->second.pop_front();
				continue;
			}
			if (ConcurrencyOccupancy(group) >= instance->definition->concurrencyLimit) break;
			GrantedSpec* spec = m_Specs.Resolve(instance->sourceSpec.value);
			if (!spec)
			{
				End(handle, *instance, VansActionEndReason::Failed,
					VansActionError::Rejected, "Queued Action Spec is no longer granted");
				continue;
			}
			VansActionActivationRequest request;
			request.spec = instance->sourceSpec;
			request.context = instance->context;
			VansActionResult validation;
			if (!ValidateActivation(request, *spec, validation))
			{
				End(handle, *instance, VansActionEndReason::Failed, validation.error,
					"Queued Action failed revalidation: " + validation.message);
				continue;
			}
			StartActivation(handle, request, *spec);
		}
		const auto currentQueue = m_ConcurrencyQueues.find(group);
		if (currentQueue != m_ConcurrencyQueues.end() && currentQueue->second.empty())
			m_ConcurrencyQueues.erase(currentQueue);
	}
}

std::optional<VansActionInstanceSnapshot> VansActionHost::Query(VansActionHandle handle) const
{
	if (const ActionInstance* instance = m_Instances.Resolve(handle.value))
		return BuildSnapshot(handle, *instance);
	for (auto it = m_History.rbegin(); it != m_History.rend(); ++it)
		if (it->handle == handle) return *it;
	return std::nullopt;
}

std::vector<VansActionInstanceSnapshot> VansActionHost::ActiveActions() const
{
	std::vector<VansActionInstanceSnapshot> result;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.state == VansActionInstanceState::Ended) return;
		result.push_back(BuildSnapshot({ handle }, instance));
	});
	return result;
}

VansActionInstanceSnapshot VansActionHost::BuildSnapshot(
	VansActionHandle handle,
	const ActionInstance& instance) const
{
	VansActionInstanceSnapshot snapshot;
	snapshot.handle = handle;
	snapshot.action = instance.definition ? instance.definition->id : VansActionId{};
	snapshot.sourceSpec = instance.sourceSpec;
	snapshot.state = instance.state;
	snapshot.endReason = instance.endReason;
	snapshot.elapsedSeconds = instance.elapsedSeconds;
	snapshot.taskCount = instance.tasks.ActiveCount();
	snapshot.resourceCount = instance.resources.ActiveCount();
	snapshot.correlationId = instance.context.correlationId;
	snapshot.trace = instance.trace;
	snapshot.error = instance.error;
	snapshot.context = instance.context;
	if (const VansTargetData* targetData = m_TargetData.Resolve(
		instance.context.TargetData(VansActionContextSlots::TargetData)))
	{
		snapshot.targetData = *targetData;
		snapshot.hasTargetData = true;
	}
	for (const auto& [field, value] : instance.variables.Values())
		snapshot.variables.push_back({ field, value });
	std::sort(snapshot.variables.begin(), snapshot.variables.end(),
		[](const auto& left, const auto& right) { return left.field < right.field; });
	snapshot.tasks = instance.tasks.Snapshot();
	snapshot.resources = instance.resources.Snapshot();
	if (instance.executor) snapshot.executor = instance.executor->DebugView();
	snapshot.recentEvents = instance.recentEvents;
	return snapshot;
}

std::vector<VansGrantedActionSpecSnapshot> VansActionHost::GrantedActions() const
{
	std::vector<VansGrantedActionSpecSnapshot> result;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		std::vector<VansCompiledActionRecord> extensions = spec.extensions;
		bool wroteCharges = false;
		for (VansCompiledActionRecord& extension : extensions)
			if (extension.type == "Gameplay.Charges")
			{
				SetSerializedObjectField(extension.inputs, "count",
					VansSerializedValue::Int(spec.charges));
				wroteCharges = true;
			}
		if (!wroteCharges && spec.charges != -1)
			extensions.push_back({ "Gameplay.Charges", VansSerializedValue::Object({
				{ "count", VansSerializedValue::Int(spec.charges) }
			}) });
		result.push_back({ { handle }, spec.definition->id, std::move(extensions),
			spec.source, spec.pendingRemoval });
	});
	return result;
}

bool VansActionHost::IsCooldownActive(VansActionId action) const
{
	const auto found = m_Cooldowns.find(action);
	if (found == m_Cooldowns.end()) return false;
	return std::any_of(found->second.begin(), found->second.end(),
		[](const CooldownState& state) { return state.remainingSeconds > 0.0; });
}

VansActionExecutionContext VansActionHost::BuildExecutionContext(
	VansActionHandle handle,
	ActionInstance& instance,
	double deltaSeconds)
{
	VansActionExecutionContext context;
	context.action = handle;
	context.owner = m_Owner;
	context.definition = instance.definition.get();
	context.context = &instance.context;
	context.variables = &instance.variables;
	context.tasks = &instance.tasks;
	context.resources = &instance.resources;
	context.hostResources = &m_HostResources;
	context.worldResources = m_Dependencies.worldResources;
	context.services = m_Dependencies.services;
	context.emitSignal = [this, handle](VansActionEvent event, std::string& error)
	{
		return EnqueueEvent(handle, std::move(event), error);
	};
	context.deltaSeconds = deltaSeconds;
	return context;
}

bool VansActionHost::ValidateCommitRequirements(
	const VansActionRuntimeProjection& runtime,
	const VansActionContext& context,
	const VansTargetData* targetData,
	VansActionResult& result) const
{
	const auto compare = [](double left, double right, VansRuntimeRequirementComparison operation)
	{
		const double tolerance = 1e-9 * (std::max)(1.0,
			(std::max)(std::abs(left), std::abs(right)));
		switch (operation)
		{
		case VansRuntimeRequirementComparison::Less: return left < right;
		case VansRuntimeRequirementComparison::LessOrEqual: return left <= right;
		case VansRuntimeRequirementComparison::Equal: return std::abs(left - right) <= tolerance;
		case VansRuntimeRequirementComparison::NotEqual: return std::abs(left - right) > tolerance;
		case VansRuntimeRequirementComparison::Greater: return left > right;
		case VansRuntimeRequirementComparison::GreaterOrEqual: return left >= right;
		}
		return false;
	};
	for (const VansRuntimeRequirement& requirement : runtime.commitRequirements)
	{
		bool satisfied = false;
		switch (requirement.kind)
		{
		case VansRuntimeRequirementKind::Attribute:
			satisfied = compare(m_Attributes.Current(requirement.attribute),
				requirement.value, requirement.comparison);
			break;
		case VansRuntimeRequirementKind::PrimaryTarget:
		{
			std::size_t count = context.Entity(
				VansActionContextSlots::PrimaryTarget).IsValid() ? 1u : 0u;
			if (count == 0 && targetData)
				for (const VansTargetDataValue& value : targetData->values)
					if ((std::holds_alternative<VansEntityHandle>(value) &&
						std::get<VansEntityHandle>(value).IsValid()) ||
						(std::holds_alternative<VansTargetHitResult>(value) &&
							std::get<VansTargetHitResult>(value).entity.IsValid())) ++count;
			satisfied = count >= requirement.minimumTargets;
			break;
		}
		case VansRuntimeRequirementKind::TargetData:
			satisfied = targetData && targetData->values.size() >= requirement.minimumTargets;
			break;
		case VansRuntimeRequirementKind::Service:
			satisfied = m_Dependencies.services &&
				static_cast<bool>(m_Dependencies.services->Resolve(requirement.service));
			break;
		}
		if (!satisfied)
		{
			result.error = requirement.kind == VansRuntimeRequirementKind::PrimaryTarget ||
				requirement.kind == VansRuntimeRequirementKind::TargetData
				? VansActionError::Rejected : VansActionError::Rejected;
			result.message = "Action commit requirement failed";
			return false;
		}
	}
	return true;
}

bool VansActionHost::ValidateActivation(
	const VansActionActivationRequest& request,
	const GrantedSpec& spec,
	VansActionResult& result,
	bool ignoreConcurrencyOccupancy) const
{
	if (!m_Initialized || m_ShuttingDown)
	{
		result.error = VansActionError::Internal;
		result.message = "Action Host is not available";
		return false;
	}
	const VansSerializedValue* activationPayload =
		request.context.Serialized(VansActionContextSlots::Payload);
	if (activationPayload && !SerializedValueFitsBudget(
		*activationPayload, m_Dependencies.limits.maximumPayloadBytes))
	{
		result.error = VansActionError::Budget;
		result.message = "Action Context payload exceeds the Host byte or nesting budget";
		return false;
	}
	if (spec.pendingRemoval || spec.charges == 0)
	{
		result.error = VansActionError::Rejected;
		result.message = "Action Spec is pending removal or has no charges";
		return false;
	}
	if (!m_Tags.Matches(spec.runtime->activationRequirements))
	{
		result.error = VansActionError::Rejected;
		result.message = "Action activation Tag requirements failed";
		return false;
	}
	if (HasQueryTerms(spec.runtime->blockedByTags) && m_Tags.Matches(spec.runtime->blockedByTags))
	{
		result.error = VansActionError::Rejected;
		result.message = "Action is blocked by owned Tags";
		return false;
	}
	if (IsCooldownActive(spec.definition->id))
	{
		result.error = VansActionError::Rejected;
		result.message = "Action cooldown is active";
		return false;
	}
	VansTargetData resolvedRequirementData;
	const VansTargetData* requirementTargetData = nullptr;
	if (spec.runtime->targetingPolicy)
	{
		const VansTargetingPolicy* policy = m_Dependencies.targetingPolicies
			? m_Dependencies.targetingPolicies->Resolve(spec.runtime->targetingPolicy) : nullptr;
		if (!policy || !m_Dependencies.targetingHandlers)
		{
			result.error = VansActionError::Dependency;
			result.message = "Action TargetingPolicy or handler registry is unavailable";
			return false;
		}
		VansTargetData initial;
		const VansTargetDataHandle contextTargetData =
			request.context.TargetData(VansActionContextSlots::TargetData);
		if (contextTargetData)
		{
			const VansTargetData* supplied = m_TargetData.Resolve(contextTargetData);
			if (!supplied)
			{
				result.error = VansActionError::Rejected;
				result.message = "Action Context TargetData handle is stale";
				return false;
			}
			initial = *supplied;
		}
		else if (const VansEntityHandle primaryTarget =
			request.context.Entity(VansActionContextSlots::PrimaryTarget);
			primaryTarget.IsValid())
			initial.values.push_back(primaryTarget);
		const VansTargetingResult targeting = VansTargetingPipeline::Execute(
			*policy, request.context, *m_Dependencies.targetingHandlers, std::move(initial));
		if (!targeting)
		{
			result.error = targeting.error;
			result.message = targeting.message;
			return false;
		}
		resolvedRequirementData = targeting.data;
		requirementTargetData = &resolvedRequirementData;
	}
	else if (const VansTargetDataHandle contextTargetData =
		request.context.TargetData(VansActionContextSlots::TargetData))
	{
		requirementTargetData = m_TargetData.Resolve(contextTargetData);
		if (!requirementTargetData)
		{
			result.error = VansActionError::Rejected;
			result.message = "Action Context TargetData handle is stale";
			return false;
		}
	}
	if (!ValidateCommitRequirements(*spec.runtime, request.context,
		requirementTargetData, result)) return false;
	if (!m_Dependencies.services->ValidateRequired(spec.runtime->requiredServices, result.message))
	{
		result.error = VansActionError::Dependency;
		return false;
	}
	for (const VansRuntimeCost& cost : spec.runtime->costs)
	{
		const double amount = cost.amount * spec.level;
		if (cost.attributeCost &&
			m_Attributes.Current(cost.attribute) < amount)
		{
			result.error = VansActionError::Resource;
			result.message = "Action cost is unavailable";
			return false;
		}
		if (!cost.attributeCost)
		{
			if (!m_Dependencies.externalCosts)
			{
				result.error = VansActionError::Dependency;
				result.message = "Action external cost provider is unavailable";
				return false;
			}
			VansActionExternalCostRequest external;
			external.operation = cost.externalOperation;
			external.resource = cost.resource;
			external.amount = amount;
			external.context = request.context;
			external.payload = cost.payload;
			if (!m_Dependencies.externalCosts->CanCommit(external, result.message))
			{
				result.error = VansActionError::Resource;
				return false;
			}
		}
	}
	if (spec.definition->concurrencyGroup)
	{
		const bool full = ConcurrencyOccupancy(spec.definition->concurrencyGroup) >=
			spec.definition->concurrencyLimit;
		if (!ignoreConcurrencyOccupancy && full &&
			spec.definition->concurrencyPolicy != VansActionConcurrencyPolicy::Allow &&
			spec.definition->concurrencyPolicy != VansActionConcurrencyPolicy::CancelExisting)
		{
			result.error = spec.definition->concurrencyPolicy == VansActionConcurrencyPolicy::RejectNew ?
				VansActionError::Rejected : VansActionError::Rejected;
			result.message = "Action concurrency group is occupied";
			return false;
		}
	}
	bool blockedByAction = false;
	m_Instances.ForEach([&](VansGenerationHandle, const ActionInstance& active)
	{
		if (active.state == VansActionInstanceState::Queued ||
			active.state == VansActionInstanceState::Ending || active.state == VansActionInstanceState::Ended) return;
		blockedByAction = blockedByAction || std::find(spec.definition->blockedActions.begin(),
			spec.definition->blockedActions.end(), active.definition->id) != spec.definition->blockedActions.end();
	});
	if (blockedByAction)
	{
		result.error = VansActionError::Rejected;
		result.message = "Action is blocked by another active Action";
		return false;
	}
	return true;
}

bool VansActionHost::CommitActivation(
	VansActionHandle handle,
	GrantedSpec& spec,
	ActionInstance& instance,
	std::string& error)
{
	const std::uint64_t source = instance.source;

	// 所有可预见条件必须在第一个不可逆副作用前完成校验。Commit 一旦开始，
	// 已成功的 Operation 不会因为后续 Operation 失败或 Action 取消而反向执行。
	for (const VansRuntimeCost& cost : instance.runtime->costs)
	{
		const double amount = cost.amount * spec.level;
		if (cost.attributeCost)
		{
			if (m_Attributes.Current(cost.attribute) < amount)
			{
				error = "insufficient Attribute";
				return false;
			}
		}
		else
		{
			if (!m_Dependencies.externalCosts)
			{
				error = "external cost provider is unavailable";
				return false;
			}
			VansActionExternalCostRequest request;
			request.operation = cost.externalOperation;
			request.resource = cost.resource;
			request.amount = amount;
			request.context = instance.context;
			request.payload = cost.payload;
			if (!m_Dependencies.externalCosts->CanCommit(request, error)) return false;
		}
	}
	if (spec.charges == 0)
	{
		error = "no Action charges";
		return false;
	}
	for (VansGameplayTagId tag : instance.runtime->runningTags)
	{
		if (!m_Dependencies.tagDictionary->Resolve(tag))
		{
			error = "running Tag is missing";
			return false;
		}
	}
	if (instance.definition->concurrencyGroup &&
		instance.definition->concurrencyPolicy != VansActionConcurrencyPolicy::Allow)
	{
		const VansActionConcurrencyGroupId group = instance.definition->concurrencyGroup;
		if (ConcurrencyOccupancy(group) >= instance.definition->concurrencyLimit)
		{
			error = "concurrency group reached its limit";
			return false;
		}
	}
	if (!instance.runtime->cooldowns.empty())
	{
		const VansActionId actionId = instance.definition->id;
		if (IsCooldownActive(actionId))
		{
			error = "cooldown is active";
			return false;
		}
		for (const VansRuntimeCooldown& cooldown : instance.runtime->cooldowns)
		{
			if (!std::isfinite(cooldown.durationSeconds) || cooldown.durationSeconds <= 0.0)
			{
				error = "cooldown duration is invalid";
				return false;
			}
			if (cooldown.tag &&
				!m_Dependencies.tagDictionary->Resolve(cooldown.tag))
			{
				error = "cooldown Tag is missing";
				return false;
			}
		}
	}
	for (const VansRuntimeEffect& reference : instance.runtime->effects)
	{
		if (!m_Dependencies.effectRegistry ||
			!m_Dependencies.effectRegistry->Resolve(reference.effect))
		{
			error = "commit Effect is missing";
			return false;
		}
	}

	for (const VansRuntimeCost& cost : instance.runtime->costs)
	{
		const double amount = cost.amount * spec.level;
		if (cost.attributeCost)
		{
			if (!m_Attributes.AddBase(cost.attribute, -amount))
			{
				error = "failed to commit Attribute cost";
				return false;
			}
			continue;
		}
		VansActionExternalCostRequest request;
		request.operation = cost.externalOperation;
		request.resource = cost.resource;
		request.amount = amount;
		request.context = instance.context;
		request.payload = cost.payload;
		if (!m_Dependencies.externalCosts->Commit(request, error)) return false;
	}
	if (spec.charges >= 0) --spec.charges;

	if (!instance.runtime->runningTags.empty())
	{
		m_Tags.BeginBatch();
		for (VansGameplayTagId tag : instance.runtime->runningTags)
		{
			if (m_Tags.Add(tag, source)) continue;
			m_Tags.EndBatch();
			m_Tags.RemoveSource(source);
			error = "failed to add running Tag";
			return false;
		}
		m_Tags.EndBatch();
		VansActionResourceEntry resource;
		resource.type = "GameplayTags";
		resource.debugName = "Action running Tags";
		resource.release = [this, source] { m_Tags.RemoveSource(source); return true; };
		if (!instance.resources.Register(std::move(resource), error))
		{
			m_Tags.RemoveSource(source);
			return false;
		}
	}

	if (instance.definition->concurrencyGroup &&
		instance.definition->concurrencyPolicy != VansActionConcurrencyPolicy::Allow)
	{
		const VansActionConcurrencyGroupId group = instance.definition->concurrencyGroup;
		m_Concurrency[group].push_back(handle);
		VansActionResourceEntry resource;
		resource.type = "Concurrency";
		resource.debugName = "Action concurrency slot";
		resource.release = [this, group, handle]
		{
			const auto found = m_Concurrency.find(group);
			if (found == m_Concurrency.end()) return true;
			auto& occupants = found->second;
			occupants.erase(std::remove(occupants.begin(), occupants.end(), handle), occupants.end());
			if (occupants.empty()) m_Concurrency.erase(found);
			return true;
		};
		if (!instance.resources.Register(std::move(resource), error)) return false;
	}

	if (!instance.runtime->cooldowns.empty())
	{
		const VansActionId actionId = instance.definition->id;
		std::vector<CooldownState> states;
		states.reserve(instance.runtime->cooldowns.size());
		m_Tags.BeginBatch();
		for (std::size_t index = 0; index < instance.runtime->cooldowns.size(); ++index)
		{
			const VansRuntimeCooldown& cooldown = instance.runtime->cooldowns[index];
			const std::uint64_t cooldownSource = SourceForCooldown(actionId, index);
			if (cooldown.tag && !m_Tags.Add(cooldown.tag, cooldownSource))
			{
				m_Tags.EndBatch();
				for (const CooldownState& applied : states) m_Tags.RemoveSource(applied.tagSource);
				error = "failed to add cooldown Tag";
				return false;
			}
			states.push_back({ cooldown.durationSeconds, cooldown.tag, cooldownSource });
		}
		m_Tags.EndBatch();
		m_Cooldowns[actionId] = std::move(states);
	}

	auto removeOnEnd = std::make_shared<std::vector<VansActiveEffectHandle>>();
	for (const VansRuntimeEffect& reference : instance.runtime->effects)
	{
		VansEffectSpec effectSpec;
		effectSpec.definition = m_Dependencies.effectRegistry->Resolve(reference.effect);
		effectSpec.context = instance.context;
		effectSpec.targetData = instance.context.TargetData(VansActionContextSlots::TargetData);
		effectSpec.source = source;
		const VansEffectApplicationResult applied = m_Effects.Apply(effectSpec);
		if (!applied)
		{
			error = applied.message;
			return false;
		}
		if (reference.removeOnEnd && applied.active) removeOnEnd->push_back(applied.active);
	}
	if (!removeOnEnd->empty())
	{
		VansActionResourceEntry resource;
		resource.type = "GameplayEffects";
		resource.debugName = "Action remove-on-end Effects";
		resource.release = [this, removeOnEnd]
		{
			for (auto it = removeOnEnd->rbegin(); it != removeOnEnd->rend(); ++it)
			{
				std::string ignored;
				m_Effects.Remove(*it, ignored);
			}
			removeOnEnd->clear();
			return true;
		};
		if (!instance.resources.Register(std::move(resource), error)) return false;
	}
	return true;
}

void VansActionHost::End(
	VansActionHandle handle,
	ActionInstance& instance,
	VansActionEndReason reason,
	VansActionError error,
	std::string message)
{
	if (instance.state == VansActionInstanceState::Ending ||
		instance.state == VansActionInstanceState::Ended) return;
	instance.endReason = reason;
	instance.error = error;
	if (instance.definition)
		RemoveFromConcurrencyQueue(handle, instance.definition->concurrencyGroup);
	Transition(instance, VansActionInstanceState::Ending, std::move(message));
	instance.tasks.StopAcceptingTasks();
	instance.tasks.CancelAll();
	if (instance.executor)
	{
		VansActionExecutionContext execution = BuildExecutionContext(handle, instance, 0.0);
		instance.executor->Finish(execution, reason);
		for (const std::unique_ptr<IVansActionSidecarDriver>& driver : instance.drivers)
			driver->Finish(execution, reason);
	}
	std::vector<std::string> releaseErrors;
	if (!instance.resources.ReleaseAll(releaseErrors))
	{
		for (const std::string& releaseError : releaseErrors)
			instance.trace.push_back({ instance.elapsedSeconds, VansActionInstanceState::Ending, releaseError });
	}
	Transition(instance, VansActionInstanceState::Ended, "Action ended");
	QueueTerminalTransitions(handle, instance, reason, error);
	VansEventBus::Get().Enqueue(VansActionEndedEvent{
		m_Owner, handle, instance.definition->id, reason, error, instance.context.correlationId },
		VansEventLane::GameLogic);
	m_History.push_back(*Query(handle));
	while (m_History.size() > 256) m_History.pop_front();
	if (const VansTargetDataHandle contextTargetData =
		instance.context.TargetData(VansActionContextSlots::TargetData))
	{
		m_TargetData.Release(contextTargetData);
		instance.context.Remove(VansActionContextSlots::TargetData);
	}
	m_DeferredRecycle.push_back(handle);
}

void VansActionHost::Transition(
	ActionInstance& instance,
	VansActionInstanceState state,
	std::string message)
{
	instance.state = state;
	instance.trace.push_back({ instance.elapsedSeconds, state, std::move(message) });
}

bool VansActionHost::HasRunningActionForSpec(VansActionSpecHandle spec) const
{
	bool running = false;
	m_Instances.ForEach([&](VansGenerationHandle, const ActionInstance& instance)
	{
		running = running || (instance.sourceSpec == spec &&
			instance.state != VansActionInstanceState::Ending &&
			instance.state != VansActionInstanceState::Ended);
	});
	return running;
}

void VansActionHost::ReleaseDeferredSpecs()
{
	std::vector<VansActionSpecHandle> releases;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		const VansActionSpecHandle typed{ handle };
		if (spec.pendingRemoval && !HasRunningActionForSpec(typed)) releases.push_back(typed);
	});
	for (VansActionSpecHandle handle : releases) m_Specs.Release(handle.value);
}

void VansActionHost::RecycleEnded()
{
	const std::vector<VansActionHandle> recycle = std::move(m_DeferredRecycle);
	m_DeferredRecycle.clear();
	for (VansActionHandle handle : recycle) m_Instances.Release(handle.value);
}

void VansActionHost::TickCooldowns(double deltaSeconds)
{
	std::vector<VansActionId> expired;
	for (auto& entry : m_Cooldowns)
	{
		auto& states = entry.second;
		for (CooldownState& state : states)
		{
			state.remainingSeconds -= deltaSeconds;
			if (state.remainingSeconds <= 0.0) m_Tags.RemoveSource(state.tagSource);
		}
		states.erase(std::remove_if(states.begin(), states.end(), [](const CooldownState& state)
		{
			return state.remainingSeconds <= 0.0;
		}), states.end());
		if (states.empty()) expired.push_back(entry.first);
	}
	for (VansActionId action : expired)
		m_Cooldowns.erase(action);
}

std::uint64_t VansActionHost::SourceForHandle(VansActionHandle handle)
{
	return (static_cast<std::uint64_t>(handle.value.generation) << 32) |
		(static_cast<std::uint64_t>(handle.value.index) + 1ull);
}

std::uint64_t VansActionHost::SourceForActionSet(VansActionSetHandle handle)
{
	return 0x8000000000000000ull |
		(static_cast<std::uint64_t>(handle.value.generation) << 32) |
		(static_cast<std::uint64_t>(handle.value.index) + 1ull);
}

std::uint64_t VansActionHost::SourceForCooldown(VansActionId action, std::size_t index)
{
	std::uint64_t value = action.value ^ 0x434f4f4c444f574eull;
	value ^= static_cast<std::uint64_t>(index + 1) + 0x9e3779b97f4a7c15ull +
		(value << 6) + (value >> 2);
	return value == 0 ? 1 : value;
}
}
