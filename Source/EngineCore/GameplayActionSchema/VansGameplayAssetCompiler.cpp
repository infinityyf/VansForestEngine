#include "VansGameplayAssetCompiler.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace Vans
{
namespace
{
void AddDiagnostic(
	VansGameplayDiagnostics& diagnostics,
	VansGameplayDiagnosticSeverity severity,
	std::string code,
	std::string message,
	std::string fieldPath = {})
{
	diagnostics.push_back({ severity, std::move(code), std::move(message), {}, std::move(fieldPath) });
}

bool HasErrors(const VansGameplayDiagnostics& diagnostics)
{
	return std::any_of(diagnostics.begin(), diagnostics.end(), [](const VansGameplayDiagnostic& diagnostic)
	{
		return diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
			diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal;
	});
}

const VansSerializedValue* At(const VansSerializedValue& root, const char* path)
{
	return FindSerializedPointer(root, path);
}

std::string StringAt(const VansSerializedValue& root, const char* path, std::string fallback = {})
{
	const VansSerializedValue* value = At(root, path);
	return value ? ReadSerializedString(*value, std::move(fallback)) : std::move(fallback);
}

std::int64_t IntAt(const VansSerializedValue& root, const char* path, std::int64_t fallback = 0)
{
	const VansSerializedValue* value = At(root, path);
	return value ? ReadSerializedInt(*value, fallback) : fallback;
}

double NumberAt(const VansSerializedValue& root, const char* path, double fallback = 0.0)
{
	const VansSerializedValue* value = At(root, path);
	return value ? ReadSerializedNumber(*value, fallback) : fallback;
}

bool BoolAt(const VansSerializedValue& root, const char* path, bool fallback = false)
{
	const VansSerializedValue* value = At(root, path);
	return value ? ReadSerializedBool(*value, fallback) : fallback;
}

double NumberField(const VansSerializedValue& object, const char* name, double fallback = 0.0)
{
	const VansSerializedValue* value = FindObjectField(object, name);
	return value ? ReadSerializedNumber(*value, fallback) : fallback;
}

std::string ReferenceString(const VansSerializedValue* value)
{
	if (!value) return {};
	if (value->kind == VansSerializedValue::Kind::String) return value->stringValue;
	if (value->kind != VansSerializedValue::Kind::Object) return {};
	for (const char* field : { "stableId", "id", "guid", "path", "assetGuid", "assetPath" })
	{
		const std::string result = ReadSerializedStringField(*value, field);
		if (!result.empty()) return result;
	}
	return {};
}

template <typename Tag>
VansStableId<Tag> StableId(std::string_view name)
{
	return name.empty() ? VansStableId<Tag>{} : VansMakeStableId<Tag>(name);
}

std::vector<std::string> StringArray(const VansSerializedValue* value)
{
	std::vector<std::string> result;
	if (!value || value->kind != VansSerializedValue::Kind::Array) return result;
	result.reserve(value->arrayItems.size());
	for (const VansSerializedValue& item : value->arrayItems)
	{
		const std::string text = ReferenceString(&item);
		if (!text.empty()) result.push_back(text);
	}
	return result;
}

VansGameplayTagQuery TagQuery(const VansSerializedValue* value)
{
	VansGameplayTagQuery result;
	if (!value || value->kind != VansSerializedValue::Kind::Object) return result;
	for (const std::string& tag : StringArray(FindObjectField(*value, "all")))
		result.all.push_back(StableId<VansGameplayTagIdTag>(tag));
	for (const std::string& tag : StringArray(FindObjectField(*value, "any")))
		result.any.push_back(StableId<VansGameplayTagIdTag>(tag));
	for (const std::string& tag : StringArray(FindObjectField(*value, "none")))
		result.none.push_back(StableId<VansGameplayTagIdTag>(tag));
	result.exact = ReadSerializedBoolField(*value, "exact", false);
	return result;
}

VansActionAuthorityPolicy AuthorityPolicy(std::string_view value)
{
	if (value == "LocalOwner") return VansActionAuthorityPolicy::LocalOwner;
	if (value == "AuthorityOnly") return VansActionAuthorityPolicy::AuthorityOnly;
	return VansActionAuthorityPolicy::Any;
}

VansActionReplicationPolicy ReplicationPolicy(std::string_view value)
{
	if (value == "OwnerPredicted") return VansActionReplicationPolicy::OwnerPredicted;
	if (value == "ServerAuthoritative") return VansActionReplicationPolicy::ServerAuthoritative;
	if (value == "Replicated") return VansActionReplicationPolicy::Replicated;
	return VansActionReplicationPolicy::LocalOnly;
}

VansActionConcurrencyPolicy ConcurrencyPolicy(std::string_view value)
{
	if (value == "Reject" || value == "RejectNew") return VansActionConcurrencyPolicy::RejectNew;
	if (value == "CancelExisting") return VansActionConcurrencyPolicy::CancelExisting;
	if (value == "Queue" || value == "QueueNew") return VansActionConcurrencyPolicy::QueueNew;
	return VansActionConcurrencyPolicy::Allow;
}

VansActionCostRefundPolicy RefundPolicy(std::string_view value)
{
	if (value == "OnCommitFailure") return VansActionCostRefundPolicy::OnCommitFailure;
	if (value == "OnCancel") return VansActionCostRefundPolicy::OnCancel;
	if (value == "Always") return VansActionCostRefundPolicy::Always;
	return VansActionCostRefundPolicy::Never;
}

VansActionCostKind CostKind(std::string_view value)
{
	if (value == "Inventory") return VansActionCostKind::Inventory;
	if (value == "Reservation") return VansActionCostKind::Reservation;
	return VansActionCostKind::Attribute;
}

VansActionRequirementKind RequirementKind(std::string_view value)
{
	if (value == "PrimaryTarget") return VansActionRequirementKind::PrimaryTarget;
	if (value == "TargetData") return VansActionRequirementKind::TargetData;
	if (value == "Service") return VansActionRequirementKind::Service;
	return VansActionRequirementKind::Attribute;
}

VansActionRequirementComparison RequirementComparison(std::string_view value)
{
	if (value == "Less") return VansActionRequirementComparison::Less;
	if (value == "LessOrEqual") return VansActionRequirementComparison::LessOrEqual;
	if (value == "Equal") return VansActionRequirementComparison::Equal;
	if (value == "NotEqual") return VansActionRequirementComparison::NotEqual;
	if (value == "Greater") return VansActionRequirementComparison::Greater;
	return VansActionRequirementComparison::GreaterOrEqual;
}

VansActionEndPolicy EndPolicy(std::string_view value)
{
	if (value == "Explicit") return VansActionEndPolicy::Explicit;
	if (value == "TimelineEnd") return VansActionEndPolicy::TimelineEnd;
	if (value == "FirstTerminal") return VansActionEndPolicy::FirstTerminal;
	return VansActionEndPolicy::ExecutorResult;
}

VansActionTransitionTrigger TransitionTrigger(std::string_view value)
{
	if (value == "Input") return VansActionTransitionTrigger::Input;
	if (value == "Completed") return VansActionTransitionTrigger::Completed;
	if (value == "Failed") return VansActionTransitionTrigger::Failed;
	return VansActionTransitionTrigger::Event;
}

VansActionError ActionErrorByName(std::string_view value)
{
	if (value == "DefinitionMissing") return VansActionError::DefinitionMissing;
	if (value == "DefinitionInvalid") return VansActionError::DefinitionInvalid;
	if (value == "RequirementsFailed") return VansActionError::RequirementsFailed;
	if (value == "TargetInvalid") return VansActionError::TargetInvalid;
	if (value == "CostUnavailable") return VansActionError::CostUnavailable;
	if (value == "CooldownActive") return VansActionError::CooldownActive;
	if (value == "ConcurrencyBlocked") return VansActionError::ConcurrencyBlocked;
	if (value == "AuthorityDenied") return VansActionError::AuthorityDenied;
	if (value == "ServiceMissing") return VansActionError::ServiceMissing;
	if (value == "CommitFailed") return VansActionError::CommitFailed;
	if (value == "TimedOut") return VansActionError::TimedOut;
	if (value == "BudgetExceeded") return VansActionError::BudgetExceeded;
	return VansActionError::ExecutionFailed;
}

bool CompileAction(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	auto action = std::make_shared<VansCompiledActionDefinition>();
	action->name = StringAt(root, "/actionId");
	action->id = StableId<VansActionIdTag>(action->name);
	action->nameSpace = StringAt(root, "/namespace");
	action->definitionVersion = static_cast<std::uint32_t>(std::max<std::int64_t>(1,
		IntAt(root, "/definitionVersion", 1)));
	action->schemaVersion = cooked.schemaVersion;
	action->contentHash = cooked.contentHash;
	action->authoringGuid = StringAt(root, "/authoringGuid");
	for (const std::string& tag : StringArray(At(root, "/tags")))
		action->abilityTags.push_back(StableId<VansGameplayTagIdTag>(tag));
	action->category = StringAt(root, "/category", "Gameplay");
	action->priority = static_cast<std::int32_t>(IntAt(root, "/priority", 0));
	action->replicationPolicy = ReplicationPolicy(StringAt(root, "/replication", "LocalOnly"));
	action->authorityPolicy = AuthorityPolicy(StringAt(root, "/activation/authority", "Any"));
	action->activationRequirements = TagQuery(At(root, "/activation/requirements"));
	action->blockedByTags = TagQuery(At(root, "/activation/blockedTags"));
	const std::string targeting = ReferenceString(At(root, "/activation/targeting/asset"));
	action->targetingPolicyReference = targeting;
	action->targetingPolicy = StableId<VansTargetingPolicyIdTag>(targeting);
	action->triggers = StringArray(At(root, "/activation/triggers"));
	action->concurrencyGroup = StableId<VansActionConcurrencyGroupIdTag>(
		StringAt(root, "/commit/concurrency/group"));
	action->concurrencyPolicy = ConcurrencyPolicy(StringAt(root, "/commit/concurrency/mode"));
	action->concurrencyLimit = static_cast<std::uint32_t>(std::max<std::int64_t>(0,
		IntAt(root, "/commit/concurrency/limit", 1)));
	action->concurrencyQueueTimeoutSeconds =
		NumberAt(root, "/commit/concurrency/queueTimeout", 0.0);
	if (const VansSerializedValue* requirements = At(root, "/commit/requirements");
		requirements && requirements->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : requirements->arrayItems)
		{
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansActionRequirementDefinition requirement;
			requirement.kind = RequirementKind(
				ReadSerializedStringField(item, "kind", "Attribute"));
			requirement.attribute = StableId<VansAttributeIdTag>(
				ReadSerializedStringField(item, "attribute"));
			requirement.comparison = RequirementComparison(
				ReadSerializedStringField(item, "comparison", "GreaterOrEqual"));
			requirement.value = NumberField(item, "value");
			requirement.minimumTargets = static_cast<std::uint32_t>((std::max<std::int64_t>)(0,
				ReadSerializedIntField(item, "minimumTargets", 1)));
			requirement.service = StableId<VansActionServiceIdTag>(
				ReadSerializedStringField(item, "service"));
			action->commitRequirements.push_back(requirement);
		}
	}
	if (const VansSerializedValue* costs = At(root, "/commit/costs");
		costs && costs->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : costs->arrayItems)
		{
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansActionCostDefinition cost;
			cost.kind = CostKind(ReadSerializedStringField(item, "kind", "Attribute"));
			cost.attribute = StableId<VansAttributeIdTag>(ReadSerializedStringField(item, "attribute"));
			cost.amount = NumberField(item, "amount");
			cost.refundPolicy = RefundPolicy(ReadSerializedStringField(item, "refund", "Never"));
			cost.resource = ReadSerializedStringField(item, "resource");
			if (const VansSerializedValue* payload = FindObjectField(item, "payload"))
				cost.payload = *payload;
			action->costs.push_back(cost);
		}
	}
	if (const VansSerializedValue* cooldowns = At(root, "/commit/cooldowns");
		cooldowns && cooldowns->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : cooldowns->arrayItems)
		{
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansActionCooldownDefinition cooldown;
			cooldown.durationSeconds = NumberField(item, "duration");
			cooldown.cooldownTag = StableId<VansGameplayTagIdTag>(
				ReadSerializedStringField(item, "tag"));
			action->cooldowns.push_back(cooldown);
		}
	}
	for (const std::string& tag : StringArray(At(root, "/commit/grantedWhileRunning")))
		action->grantedWhileRunning.push_back(StableId<VansGameplayTagIdTag>(tag));
	if (const VansSerializedValue* effects = At(root, "/commit/effects");
		effects && effects->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : effects->arrayItems)
		{
			const std::string name = ReferenceString(&item);
			if (name.empty()) continue;
			action->commitEffects.push_back({ StableId<VansEffectIdTag>(name),
				item.kind == VansSerializedValue::Kind::Object &&
				ReadSerializedBoolField(item, "removeOnEnd", false), name });
		}
	}
	action->executor = StableId<VansActionExecutorIdTag>(
		StringAt(root, "/execution/executor", "Action.Executor.Immediate"));
	action->executionGraphAsset = ReferenceString(At(root, "/execution/graph"));
	const std::string timeline = ReferenceString(At(root, "/execution/timeline"));
	if (!timeline.empty()) action->timelineAssets.push_back(timeline);
	for (const std::string& extra : StringArray(At(root, "/execution/timelines")))
		action->timelineAssets.push_back(extra);
	if (const VansSerializedValue* variables = At(root, "/execution/variables");
		variables && variables->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : variables->arrayItems)
		{
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansActionVariableDefinition variable;
			variable.name = ReadSerializedStringField(item, "name");
			variable.id = StableId<VansActionFieldIdTag>(variable.name);
			if (const VansSerializedValue* defaultValue = FindObjectField(item, "default"))
				variable.defaultValue = *defaultValue;
			action->variables.push_back(std::move(variable));
		}
	}
	action->timeDomain = StringAt(root, "/execution/timeDomain", "Game");
	action->endPolicy = EndPolicy(StringAt(root, "/execution/endPolicy", "ExecutorResult"));
	action->cancellable = BoolAt(root, "/transitions/cancellable", true);
	action->interruptible = BoolAt(root, "/transitions/interruptible", true);
	const auto appendActionReferences = [&](const char* path,
		std::vector<std::string>& references, std::vector<VansActionId>& actions)
	{
		const VansSerializedValue* values = At(root, path);
		if (!values || values->kind != VansSerializedValue::Kind::Array) return;
		for (const VansSerializedValue& item : values->arrayItems)
		{
			const std::string reference = ReferenceString(&item);
			if (reference.empty()) continue;
			references.push_back(reference);
			actions.push_back(StableId<VansActionIdTag>(reference));
		}
	};
	appendActionReferences("/transitions/blockedActions",
		action->blockedActionReferences, action->blockedActions);
	appendActionReferences("/transitions/cancelActions",
		action->cancelActionReferences, action->cancelActions);
	const auto appendTransitionRule = [&](const VansSerializedValue& item,
		std::string path, bool comboWindow)
	{
		if (item.kind != VansSerializedValue::Kind::Object) return;
		VansActionTransitionRule rule;
		rule.name = ReadSerializedStringField(item, "name", comboWindow ? "Combo" : "Transition");
		rule.trigger = comboWindow ? VansActionTransitionTrigger::Input :
			TransitionTrigger(ReadSerializedStringField(item, "trigger", "Event"));
		const std::string eventName = ReadSerializedStringField(item, "event");
		rule.event = StableId<VansActionFieldIdTag>(eventName);
		rule.inputBinding = ReadSerializedStringField(item, "input");
		const std::string target = ReferenceString(FindObjectField(item, "target"));
		rule.targetActionReference = target;
		rule.targetAction = StableId<VansActionIdTag>(target);
		rule.minimumTimeSeconds = NumberField(item, comboWindow ? "openTime" : "minimumTime", 0.0);
		rule.maximumTimeSeconds = NumberField(item, comboWindow ? "closeTime" : "maximumTime", -1.0);
		rule.priority = static_cast<std::int32_t>(ReadSerializedIntField(item, "priority", 0));
		rule.consumeTrigger = ReadSerializedBoolField(item, "consume", false);
		rule.cancelSource = ReadSerializedBoolField(item, "cancelSource", true);
		rule.inheritPrimaryTarget = ReadSerializedBoolField(item, "inheritPrimaryTarget", true);
		rule.requirements = TagQuery(FindObjectField(item, "requirements"));
		if (const VansSerializedValue* patch = FindObjectField(item, "contextPatch"))
			rule.contextPatch = *patch;
		const bool triggerValid =
			(rule.trigger != VansActionTransitionTrigger::Event || rule.event) &&
			(rule.trigger != VansActionTransitionTrigger::Input || !rule.inputBinding.empty());
		if (rule.name.empty() || !rule.targetAction || !triggerValid ||
			!std::isfinite(rule.minimumTimeSeconds) || !std::isfinite(rule.maximumTimeSeconds) ||
			rule.minimumTimeSeconds < 0.0 ||
			(rule.maximumTimeSeconds >= 0.0 &&
				rule.maximumTimeSeconds < rule.minimumTimeSeconds))
		{
			AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
				"GAF-ACTION-TRANSITION", "Action transition rule is invalid", std::move(path));
			return;
		}
		action->transitionRules.push_back(std::move(rule));
	};
	if (const VansSerializedValue* rules = At(root, "/transitions/rules");
		rules && rules->kind == VansSerializedValue::Kind::Array)
		for (std::size_t index = 0; index < rules->arrayItems.size(); ++index)
			appendTransitionRule(rules->arrayItems[index],
				"/transitions/rules/" + std::to_string(index), false);
	if (const VansSerializedValue* combos = At(root, "/transitions/comboWindows");
		combos && combos->kind == VansSerializedValue::Kind::Array)
		for (std::size_t index = 0; index < combos->arrayItems.size(); ++index)
			appendTransitionRule(combos->arrayItems[index],
				"/transitions/comboWindows/" + std::to_string(index), true);
	std::stable_sort(action->transitionRules.begin(), action->transitionRules.end(),
		[](const VansActionTransitionRule& left, const VansActionTransitionRule& right)
		{ return left.priority > right.priority; });
	action->inputBuffer.enabled = BoolAt(root, "/transitions/inputBuffer/enabled", false);
	action->inputBuffer.durationSeconds =
		NumberAt(root, "/transitions/inputBuffer/duration", 0.0);
	action->inputBuffer.maximumEntries = static_cast<std::uint32_t>((std::max<std::int64_t>)(1,
		IntAt(root, "/transitions/inputBuffer/maximumEntries", 1)));
	const std::string fallbackAction = ReferenceString(At(root, "/transitions/failureFallback/action"));
	action->failureFallback.actionReference = fallbackAction;
	action->failureFallback.action = StableId<VansActionIdTag>(fallbackAction);
	for (const std::string& fallbackError : StringArray(
		At(root, "/transitions/failureFallback/errors")))
		action->failureFallback.errors.push_back(ActionErrorByName(fallbackError));
	action->failureFallback.inheritPrimaryTarget =
		BoolAt(root, "/transitions/failureFallback/inheritPrimaryTarget", true);
	if (const VansSerializedValue* patch = At(root, "/transitions/failureFallback/contextPatch"))
		action->failureFallback.contextPatch = *patch;
	if (const VansSerializedValue* cues = At(root, "/presentation/cues");
		cues && cues->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : cues->arrayItems)
		{
			const std::string reference = ReferenceString(&item);
			if (reference.empty()) continue;
			action->presentationCueReferences.push_back(reference);
			action->presentationCues.push_back(StableId<VansCueIdTag>(reference));
		}
	}
	for (const std::string& name : StringArray(At(root, "/dependencies/services")))
		action->requiredServices.push_back(StableId<VansActionServiceIdTag>(name));
	action->assetDependencies = cooked.dependencies;
	if (const VansSerializedValue* extensions = At(root, "/extensions"))
		action->extensionData = *extensions;
	const VansGameplayDiagnostics runtimeDiagnostics = VansActionDefinitionRegistry::Validate(*action);
	diagnostics.insert(diagnostics.end(), runtimeDiagnostics.begin(), runtimeDiagnostics.end());
	output = std::shared_ptr<const VansCompiledActionDefinition>(std::move(action));
	return !HasErrors(diagnostics);
}

bool CompileActionSet(const VansGameplayCookedAsset& cooked, VansCompiledGameplayAssetData& output)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansActionSetDefinition set;
	set.name = StringAt(root, "/actionSetId");
	set.id = StableId<VansActionSetIdTag>(set.name);
	if (const VansSerializedValue* grants = At(root, "/grants");
		grants && grants->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : grants->arrayItems)
		{
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansActionGrantDesc grant;
			grant.actionReference = ReferenceString(FindObjectField(item, "action"));
			grant.action = StableId<VansActionIdTag>(grant.actionReference);
			grant.level = FindObjectField(item, "level")
				? ReadSerializedNumber(*FindObjectField(item, "level"), 1.0) : 1.0;
			grant.inputBinding = ReadSerializedStringField(item, "inputBinding");
			for (const std::string& tag : StringArray(FindObjectField(item, "dynamicTags")))
				grant.dynamicTags.push_back(StableId<VansGameplayTagIdTag>(tag));
			grant.charges = static_cast<std::int32_t>(ReadSerializedIntField(item, "charges", -1));
			const std::string persistence = ReadSerializedStringField(item, "persistence", "OwnerLifetime");
			if (persistence == "Transient") grant.persistence = VansActionGrantPersistence::Transient;
			else if (persistence == "Persistent") grant.persistence = VansActionGrantPersistence::Persistent;
			set.grants.push_back(std::move(grant));
		}
	}
	if (const VansSerializedValue* effects = At(root, "/initialEffects");
		effects && effects->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : effects->arrayItems)
		{
			const std::string effect = ReferenceString(&item);
			if (effect.empty()) continue;
			set.initialEffectReferences.push_back(effect);
			set.initialEffects.push_back(StableId<VansEffectIdTag>(effect));
		}
	}
	if (const VansSerializedValue* overrides = At(root, "/attributeOverrides");
		overrides && overrides->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : overrides->arrayItems)
		{
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			set.attributeOverrides.push_back({
				StableId<VansAttributeIdTag>(ReadSerializedStringField(item, "attribute")),
				NumberField(item, "value") });
		}
	}
	const std::string revoke = StringAt(root, "/revokePolicy", "CancelRunning");
	if (revoke == "KeepRunning") set.revokePolicy = VansActionRevokePolicy::KeepRunning;
	else if (revoke == "DeferUntilIdle") set.revokePolicy = VansActionRevokePolicy::DeferUntilIdle;
	set.removeInitialEffectsOnRevoke = BoolAt(root, "/removeInitialEffectsOnRevoke", true);
	output = std::move(set);
	return true;
}

VansAttributeModifierOperation ModifierOperation(std::string_view value)
{
	if (value == "Multiplicative" || value == "Multiply")
		return VansAttributeModifierOperation::Multiplicative;
	if (value == "Override") return VansAttributeModifierOperation::Override;
	return VansAttributeModifierOperation::Additive;
}

VansEffectMagnitudeSource EffectMagnitudeSource(std::string_view value)
{
	if (value == "SetByCaller") return VansEffectMagnitudeSource::SetByCaller;
	if (value == "CapturedAttribute") return VansEffectMagnitudeSource::CapturedAttribute;
	if (value == "ContextPayload") return VansEffectMagnitudeSource::ContextPayload;
	if (value == "TargetData") return VansEffectMagnitudeSource::TargetData;
	if (value == "RandomRange") return VansEffectMagnitudeSource::RandomRange;
	return VansEffectMagnitudeSource::Fixed;
}

VansEffectTargetDataMetric EffectTargetDataMetric(std::string_view value)
{
	if (value == "HitDistance") return VansEffectTargetDataMetric::HitDistance;
	if (value == "AreaRadius") return VansEffectTargetDataMetric::AreaRadius;
	if (value == "RayLength") return VansEffectTargetDataMetric::RayLength;
	return VansEffectTargetDataMetric::Count;
}

bool CompileEffect(const VansGameplayCookedAsset& cooked, VansCompiledGameplayAssetData& output)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	auto effect = std::make_shared<VansEffectDefinition>();
	effect->name = StringAt(root, "/effectId");
	effect->id = StableId<VansEffectIdTag>(effect->name);
	effect->definitionVersion = static_cast<std::uint32_t>(std::max<std::int64_t>(1,
		IntAt(root, "/definitionVersion", 1)));
	const std::string durationPolicy = StringAt(root, "/duration/policy", "Instant");
	if (durationPolicy == "Duration") effect->durationPolicy = VansEffectDurationPolicy::Duration;
	else if (durationPolicy == "Infinite") effect->durationPolicy = VansEffectDurationPolicy::Infinite;
	effect->durationSeconds = NumberAt(root, "/duration/seconds");
	effect->periodSeconds = NumberAt(root, "/duration/period");
	effect->executePeriodicOnApply = BoolAt(root, "/duration/executePeriodicOnApply");
	const std::string stacking = StringAt(root, "/stacking/policy", "None");
	if (stacking == "AggregateBySource") effect->stackingPolicy = VansEffectStackingPolicy::AggregateBySource;
	else if (stacking == "AggregateByTarget") effect->stackingPolicy = VansEffectStackingPolicy::AggregateByTarget;
	const std::string overflow = StringAt(root, "/stacking/overflow", "Reject");
	if (overflow == "RefreshOnly") effect->overflowPolicy = VansEffectOverflowPolicy::RefreshOnly;
	else if (overflow == "ReplaceOldest") effect->overflowPolicy = VansEffectOverflowPolicy::ReplaceOldest;
	effect->maximumStacks = static_cast<std::uint32_t>(std::max<std::int64_t>(1,
		IntAt(root, "/stacking/maximumStacks", 1)));
	effect->refreshDurationOnStack = BoolAt(root, "/stacking/refreshDuration", true);
	effect->resetPeriodOnStack = BoolAt(root, "/stacking/resetPeriod", false);
	effect->requirements = TagQuery(At(root, "/requirements"));
	effect->immunity = TagQuery(At(root, "/immunity"));
	for (const std::string& tag : StringArray(At(root, "/effectTags")))
		effect->effectTags.push_back(StableId<VansGameplayTagIdTag>(tag));
	for (const std::string& tag : StringArray(At(root, "/grantedTags")))
		effect->grantedTags.push_back(StableId<VansGameplayTagIdTag>(tag));
	if (const VansSerializedValue* modifiers = At(root, "/modifiers");
		modifiers && modifiers->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : modifiers->arrayItems)
		{
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansEffectModifier modifier;
			modifier.attribute = StableId<VansAttributeIdTag>(ReadSerializedStringField(item, "attribute"));
			modifier.operation = ModifierOperation(ReadSerializedStringField(item, "operation", "Additive"));
			modifier.magnitude = FindObjectField(item, "magnitude")
				? ReadSerializedNumber(*FindObjectField(item, "magnitude")) : 0.0;
			modifier.priority = static_cast<std::int32_t>(ReadSerializedIntField(item, "priority", 0));
			modifier.magnitudeSource = EffectMagnitudeSource(
				ReadSerializedStringField(item, "magnitudeSource", "Fixed"));
			modifier.setByCallerField = StableId<VansActionFieldIdTag>(
				ReadSerializedStringField(item, "setByCaller"));
			modifier.capturedAttribute = StableId<VansAttributeIdTag>(
				ReadSerializedStringField(item, "capturedAttribute"));
			modifier.capturePolicy = ReadSerializedStringField(item, "capture", "Snapshot") == "Dynamic"
				? VansEffectCapturePolicy::Dynamic : VansEffectCapturePolicy::Snapshot;
			modifier.contextPayloadPath = ReadSerializedStringField(item, "contextPath");
			modifier.targetDataMetric = EffectTargetDataMetric(
				ReadSerializedStringField(item, "targetMetric", "Count"));
			modifier.randomMinimum = NumberField(item, "randomMinimum", 0.0);
			modifier.randomMaximum = NumberField(item, "randomMaximum", 1.0);
			modifier.coefficient = NumberField(item, "coefficient", 1.0);
			modifier.preAdd = NumberField(item, "preAdd", 0.0);
			modifier.postAdd = NumberField(item, "postAdd", 0.0);
			effect->modifiers.push_back(modifier);
		}
	}
	const auto appendCues = [&](const char* path, std::vector<VansCueId>& destination,
		std::vector<std::string>& references)
	{
		const VansSerializedValue* cues = At(root, path);
		if (!cues || cues->kind != VansSerializedValue::Kind::Array) return;
		for (const VansSerializedValue& item : cues->arrayItems)
		{
			const std::string reference = ReferenceString(&item);
			if (reference.empty()) continue;
			references.push_back(reference);
			destination.push_back(StableId<VansCueIdTag>(reference));
		}
	};
	appendCues("/cues/execute", effect->executeCues, effect->executeCueReferences);
	appendCues("/cues/persistent", effect->persistentCues, effect->persistentCueReferences);
	appendCues("/cues/periodic", effect->periodicCues, effect->periodicCueReferences);
	appendCues("/cues/remove", effect->removeCues, effect->removeCueReferences);
	output = std::shared_ptr<const VansEffectDefinition>(std::move(effect));
	return true;
}

VansGameplayCueScope CueScope(std::string_view value)
{
	if (value == "Owner") return VansGameplayCueScope::Owner;
	if (value == "Observers") return VansGameplayCueScope::Observers;
	if (value == "World") return VansGameplayCueScope::World;
	if (value == "LocalOnly") return VansGameplayCueScope::LocalOnly;
	return VansGameplayCueScope::Target;
}

bool CompileCue(const VansGameplayCookedAsset& cooked, VansCompiledGameplayAssetData& output)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansCompiledGameplayCueDefinition cue;
	cue.name = StringAt(root, "/cueId");
	cue.id = StableId<VansCueIdTag>(cue.name);
	cue.scope = CueScope(StringAt(root, "/scope", "Target"));
	cue.payloadSchemaAsset = ReferenceString(At(root, "/payloadSchema"));
	if (const VansSerializedValue* adapters = At(root, "/adapters");
		adapters && adapters->kind == VansSerializedValue::Kind::Array)
		for (const VansSerializedValue& source : adapters->arrayItems)
		{
			if (source.kind != VansSerializedValue::Kind::Object) continue;
			VansGameplayCueAdapterMapping mapping;
			mapping.serviceName = ReadSerializedStringField(source, "service");
			mapping.service = StableId<VansActionServiceIdTag>(mapping.serviceName);
			mapping.commandName = ReadSerializedStringField(source, "command");
			mapping.command = StableId<VansActionFieldIdTag>(mapping.commandName);
			mapping.updateCommandName = ReadSerializedStringField(source, "updateCommand");
			mapping.updateCommand = StableId<VansActionFieldIdTag>(mapping.updateCommandName);
			mapping.removeCommandName = ReadSerializedStringField(source, "removeCommand");
			mapping.removeCommand = StableId<VansActionFieldIdTag>(mapping.removeCommandName);
			mapping.asset = ReferenceString(FindObjectField(source, "asset"));
			if (const VansSerializedValue* parameters = FindObjectField(source, "parameters"))
				mapping.parameters = *parameters;
			cue.adapterMappings.push_back(std::move(mapping));
		}
	output = std::move(cue);
	return true;
}

bool CompileAttributeSet(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansCompiledAttributeSetDefinition set;
	set.name = StringAt(root, "/attributeSetId");
	set.id = StableId<VansAttributeSetIdTag>(set.name);
	VansAttributeRegistry validator;
	if (const VansSerializedValue* attributes = At(root, "/attributes");
		attributes && attributes->kind == VansSerializedValue::Kind::Array)
	{
		for (std::size_t index = 0; index < attributes->arrayItems.size(); ++index)
		{
			const VansSerializedValue& item = attributes->arrayItems[index];
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansAttributeDefinition definition;
			definition.name = ReadSerializedStringField(item, "name");
			definition.id = StableId<VansAttributeIdTag>(definition.name);
			const std::string fieldName = ReadSerializedStringField(item, "fieldId", definition.name);
			definition.fieldId = VansStableHash64(fieldName);
			definition.defaultValue = FindObjectField(item, "default")
				? ReadSerializedNumber(*FindObjectField(item, "default")) : 0.0;
			if (const VansSerializedValue* minimum = FindObjectField(item, "minimum"))
			{
				definition.minimum = ReadSerializedNumber(*minimum);
				definition.hasMinimum = true;
			}
			if (const VansSerializedValue* maximum = FindObjectField(item, "maximum"))
			{
				definition.maximum = ReadSerializedNumber(*maximum);
				definition.hasMaximum = true;
			}
			definition.replicated = ReadSerializedBoolField(item, "replicated", false);
			std::string error;
			if (!validator.Register(definition, error))
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-ATTRIBUTE-DEFINITION", error,
					"/attributes/" + std::to_string(index));
			else set.attributes.push_back(std::move(definition));
		}
	}
	if (!set.attributes.empty())
	{
		std::string error;
		if (!validator.Seal(error))
			AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
				"GAF-ATTRIBUTE-SET", error, "/attributes");
	}
	else AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
		"GAF-ATTRIBUTE-EMPTY", "AttributeSet must contain at least one Attribute", "/attributes");
	output = std::move(set);
	return !HasErrors(diagnostics);
}

VansTargetingStepKind TargetingStepKind(std::string_view value)
{
	if (value == "Filter") return VansTargetingStepKind::Filter;
	if (value == "Sort") return VansTargetingStepKind::Sort;
	if (value == "Limit") return VansTargetingStepKind::Limit;
	if (value == "Lock") return VansTargetingStepKind::Lock;
	return VansTargetingStepKind::Acquire;
}

bool CompileTargeting(const VansGameplayCookedAsset& cooked, VansCompiledGameplayAssetData& output)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansTargetingPolicy policy;
	policy.name = StringAt(root, "/targetingId");
	policy.id = StableId<VansTargetingPolicyIdTag>(policy.name);
	if (const VansSerializedValue* steps = At(root, "/steps");
		steps && steps->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : steps->arrayItems)
		{
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansTargetingStep step;
			step.kind = TargetingStepKind(ReadSerializedStringField(item, "kind", "Acquire"));
			step.stableName = ReadSerializedStringField(item, "handler");
			step.handler = StableId<VansActionGraphNodeTypeIdTag>(step.stableName);
			if (const VansSerializedValue* parameters = FindObjectField(item, "parameters"))
				step.parameters = *parameters;
			policy.steps.push_back(std::move(step));
		}
	}
	output = std::move(policy);
	return true;
}

bool CompileTagTree(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansCompiledGameplayTagTreeDefinition tree;
	tree.name = StringAt(root, "/tagTreeId");
	tree.id = StableId<VansGameplayTagTreeIdTag>(tree.name);
	VansGameplayTagDictionary dictionary;
	if (const VansSerializedValue* tags = At(root, "/tags");
		tags && tags->kind == VansSerializedValue::Kind::Array)
	{
		for (std::size_t index = 0; index < tags->arrayItems.size(); ++index)
		{
			const VansSerializedValue& item = tags->arrayItems[index];
			const std::string name = item.kind == VansSerializedValue::Kind::String
				? item.stringValue : ReadSerializedStringField(item, "name");
			const std::string description = item.kind == VansSerializedValue::Kind::Object
				? ReadSerializedStringField(item, "description") : std::string();
			const bool deprecated = item.kind == VansSerializedValue::Kind::Object &&
				ReadSerializedBoolField(item, "deprecated", false);
			const std::string replacement = item.kind == VansSerializedValue::Kind::Object
				? ReadSerializedStringField(item, "replacement") : std::string();
			std::string error;
			if (!dictionary.Register(name, description, deprecated, replacement, error))
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-TAG-DEFINITION", error, "/tags/" + std::to_string(index));
		}
	}
	std::string error;
	if (!HasErrors(diagnostics) && !dictionary.Seal(error))
		AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
			"GAF-TAG-TREE", error, "/tags");
	tree.tags = dictionary.Definitions();
	output = std::move(tree);
	return !HasErrors(diagnostics);
}

VansTimelineValueType PayloadFieldType(std::string_view value)
{
	if (value == "Bool") return VansTimelineValueType::Bool;
	if (value == "Int32") return VansTimelineValueType::Int32;
	if (value == "Int64") return VansTimelineValueType::Int64;
	if (value == "Float") return VansTimelineValueType::Float;
	if (value == "Double") return VansTimelineValueType::Double;
	if (value == "Enum") return VansTimelineValueType::Enum;
	if (value == "String") return VansTimelineValueType::String;
	if (value == "Vec2") return VansTimelineValueType::Vec2;
	if (value == "Vec3") return VansTimelineValueType::Vec3;
	if (value == "Vec4") return VansTimelineValueType::Vec4;
	if (value == "Quaternion") return VansTimelineValueType::Quaternion;
	if (value == "ColorLinear") return VansTimelineValueType::ColorLinear;
	if (value == "ColorSrgb") return VansTimelineValueType::ColorSrgb;
	if (value == "ObjectReference") return VansTimelineValueType::ObjectReference;
	if (value == "Struct") return VansTimelineValueType::Struct;
	return VansTimelineValueType::Null;
}

bool CompilePayload(const VansGameplayCookedAsset& cooked, VansCompiledGameplayAssetData& output)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansPayloadSchema schema;
	schema.stableName = StringAt(root, "/payloadTypeId");
	schema.typeId = StableId<VansTimelinePayloadTypeTag>(schema.stableName);
	schema.maximumBytes = static_cast<std::uint32_t>(std::max<std::int64_t>(1,
		IntAt(root, "/maximumBytes", 4096)));
	schema.editorSafe = BoolAt(root, "/editorSafe", false);
	schema.allowAdditionalFields = BoolAt(root, "/allowAdditionalFields", false);
	if (const VansSerializedValue* fields = At(root, "/fields");
		fields && fields->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& item : fields->arrayItems)
		{
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansPayloadFieldSchema field;
			field.name = ReadSerializedStringField(item, "name");
			field.id = StableId<VansTimelineFieldTag>(field.name);
			field.type = PayloadFieldType(ReadSerializedStringField(item, "type"));
			field.required = ReadSerializedBoolField(item, "required", false);
			field.flags = ReadSerializedBoolField(item, "sensitive", false)
				? VansPayloadFieldFlags::Sensitive : VansPayloadFieldFlags::None;
			schema.fields.push_back(std::move(field));
		}
	}
	output = std::move(schema);
	return true;
}

VansActionGraphNodeKind GraphNodeKind(std::string_view value)
{
	if (value == "Command") return VansActionGraphNodeKind::Command;
	if (value == "Latent") return VansActionGraphNodeKind::Latent;
	if (value == "State") return VansActionGraphNodeKind::State;
	if (value == "Flow") return VansActionGraphNodeKind::Flow;
	if (value == "Transaction") return VansActionGraphNodeKind::Transaction;
	if (value == "Bridge") return VansActionGraphNodeKind::Bridge;
	if (value == "SubAction") return VansActionGraphNodeKind::SubAction;
	return VansActionGraphNodeKind::Pure;
}

VansActionGraphRollbackPlan GraphRollbackPlan(std::string_view value)
{
	if (value == "Automatic") return VansActionGraphRollbackPlan::Automatic;
	if (value == "Compensate") return VansActionGraphRollbackPlan::Compensate;
	return VansActionGraphRollbackPlan::None;
}

bool CompileGraph(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	auto graph = std::make_shared<VansCompiledActionGraph>();
	graph->name = StringAt(root, "/graphId");
	graph->version = static_cast<std::uint32_t>(std::max<std::int64_t>(1,
		IntAt(root, "/definitionVersion", 1)));
	graph->contentHash = cooked.contentHash;
	std::unordered_map<std::string, std::uint32_t> nodesByGuid;
	if (const VansSerializedValue* nodes = At(root, "/nodes");
		nodes && nodes->kind == VansSerializedValue::Kind::Array)
	{
		for (std::size_t index = 0; index < nodes->arrayItems.size(); ++index)
		{
			const VansSerializedValue& item = nodes->arrayItems[index];
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansCompiledActionGraphNode node;
			node.guid = ReadSerializedStringField(item, "guid");
			const std::string type = ReadSerializedStringField(item, "type");
			node.type = StableId<VansActionGraphNodeTypeIdTag>(type);
			node.kind = GraphNodeKind(ReadSerializedStringField(item, "kind", "Pure"));
			node.predictable = ReadSerializedBoolField(item, "predictable", false);
			node.rollbackPlan = GraphRollbackPlan(
				ReadSerializedStringField(item, "rollbackPlan", "None"));
			if (const VansSerializedValue* properties = FindObjectField(item, "properties"))
				node.properties = *properties;
			if (node.guid.empty() || !nodesByGuid.emplace(node.guid,
				static_cast<std::uint32_t>(graph->nodes.size())).second)
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-GRAPH-NODE-GUID", "Graph node GUID is empty or duplicated",
					"/nodes/" + std::to_string(index) + "/guid");
			graph->nodes.push_back(std::move(node));
		}
	}
	const std::string entry = StringAt(root, "/entryNode");
	const auto entryFound = nodesByGuid.find(entry);
	if (entryFound == nodesByGuid.end())
		AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
			"GAF-GRAPH-ENTRY", "Graph entryNode does not resolve to a node", "/entryNode");
	else graph->entryNode = entryFound->second;
	if (const VansSerializedValue* edges = At(root, "/edges");
		edges && edges->kind == VansSerializedValue::Kind::Array)
	{
		for (std::size_t index = 0; index < edges->arrayItems.size(); ++index)
		{
			const VansSerializedValue& item = edges->arrayItems[index];
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			const auto from = nodesByGuid.find(ReadSerializedStringField(item, "from"));
			const auto to = nodesByGuid.find(ReadSerializedStringField(item, "to"));
			if (from == nodesByGuid.end() || to == nodesByGuid.end())
			{
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-GRAPH-EDGE", "Graph edge endpoint is missing",
					"/edges/" + std::to_string(index));
				continue;
			}
			graph->edges.push_back({ from->second,
				ReadSerializedStringField(item, "output", "Success"), to->second,
				static_cast<std::int32_t>(ReadSerializedIntField(item, "order", 0)) });
		}
	}
	output = std::shared_ptr<const VansCompiledActionGraph>(std::move(graph));
	return !HasErrors(diagnostics);
}

bool CompileCameraProfile(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	auto vectorAt = [&](const char* path, glm::vec3 fallback = glm::vec3(0.0f))
	{
		const VansSerializedValue* object = At(cooked.runtimeDocument, path);
		if (!object) return fallback;
		return glm::vec3(
			static_cast<float>(NumberField(*object, "x", fallback.x)),
			static_cast<float>(NumberField(*object, "y", fallback.y)),
			static_cast<float>(NumberField(*object, "z", fallback.z)));
	};
	if (cooked.assetType == VansAssetType::CameraRigProfile)
	{
		VansCameraRigDefinition rig;
		rig.stableName = StringAt(cooked.runtimeDocument, "/cameraRigId");
		rig.id = StableId<VansCameraRigIdTag>(rig.stableName);
		rig.follow.enabled = true;
		rig.follow.mode = StringAt(cooked.runtimeDocument, "/follow/mode", "SpringArm");
		rig.follow.targetBinding = StringAt(cooked.runtimeDocument, "/follow/targetBinding", "Avatar");
		rig.follow.localOffset = vectorAt("/follow/offset", { 0.0f, 1.6f, -3.0f });
		rig.follow.positionDamping = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/follow/damping", 0.15));
		rig.lookAt.enabled = BoolAt(cooked.runtimeDocument, "/lookAt/enabled", true);
		rig.lookAt.targetBinding = StringAt(cooked.runtimeDocument, "/lookAt/targetBinding", "Avatar");
		rig.lookAt.worldOffset = vectorAt("/lookAt/offset", { 0.0f, 1.4f, 0.0f });
		rig.lookAt.rotationDamping = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lookAt/damping", 0.1));
		rig.collision.enabled = BoolAt(cooked.runtimeDocument, "/collision/enabled", true);
		rig.collision.radius = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/collision/probeRadius", 0.2));
		rig.collision.minimumDistance = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/collision/minimumDistance", 0.1));
		rig.collision.padding = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/collision/padding", 0.05));
		rig.collision.recoverySeconds = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/collision/recoverySeconds", 0.2));
		rig.collision.layers = StringArray(At(cooked.runtimeDocument, "/collision/layers"));
		rig.initialView.pose.position = rig.follow.localOffset;
		rig.initialView.lens.fieldOfView = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lens/fieldOfView", 60.0));
		rig.initialView.lens.nearClip = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lens/nearPlane", 0.1));
		rig.initialView.lens.farClip = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lens/farPlane", 1000.0));
		rig.focusDistance = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/lens/focusDistance", 10.0));
		rig.composition.screenX = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/composition/screenX", 0.5));
		rig.composition.screenY = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/composition/screenY", 0.5));
		rig.composition.deadZoneX = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/composition/deadZoneX", 0.1));
		rig.composition.deadZoneY = static_cast<float>(
			NumberAt(cooked.runtimeDocument, "/composition/deadZoneY", 0.1));
		if (rig.initialView.lens.nearClip >= rig.initialView.lens.farClip)
		{
			AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
				"GAF-CAMERA-LENS-RANGE", "Camera near plane must be smaller than far plane",
				"/lens/nearPlane");
			return false;
		}
		VansCameraRuntime validator;
		std::string error;
		if (!validator.RegisterRig(rig, error))
		{
			AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
				"GAF-CAMERA-RIG", error, "/cameraRigId");
			return false;
		}
		output = std::move(rig);
		return true;
	}

	VansCameraShakeDefinition shake;
	shake.stableName = StringAt(cooked.runtimeDocument, "/cameraShakeId");
	shake.id = StableId<VansCameraShakeIdTag>(shake.stableName);
	shake.translationAmplitude = vectorAt("/noise/translationAmplitude", glm::vec3(0.05f));
	shake.rotationAmplitude = vectorAt("/noise/rotationAmplitude", glm::vec3(0.5f));
	shake.frequency = static_cast<float>(NumberAt(cooked.runtimeDocument, "/noise/frequency", 12.0));
	shake.attackSeconds = static_cast<float>(NumberAt(cooked.runtimeDocument, "/envelope/attack", 0.05));
	shake.sustainSeconds = static_cast<float>(NumberAt(cooked.runtimeDocument, "/envelope/sustain", 0.1));
	shake.releaseSeconds = static_cast<float>(NumberAt(cooked.runtimeDocument, "/envelope/release", 0.15));
	shake.minimumDistance = static_cast<float>(NumberAt(
		cooked.runtimeDocument, "/falloff/minimumDistance", 0.0));
	shake.maximumDistance = static_cast<float>(NumberAt(
		cooked.runtimeDocument, "/falloff/maximumDistance", 25.0));
	shake.falloffExponent = static_cast<float>(NumberAt(
		cooked.runtimeDocument, "/falloff/exponent", 1.0));
	shake.seed = static_cast<std::uint64_t>(std::max<std::int64_t>(
		0, IntAt(cooked.runtimeDocument, "/seed", 0)));
	VansCameraRuntime validator;
	std::string error;
	if (!validator.RegisterShake(shake, error))
	{
		AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
			"GAF-CAMERA-SHAKE", error, "/cameraShakeId");
		return false;
	}
	output = std::move(shake);
	return true;
}
}

VansGameplayCompileResult VansGameplayAssetCompiler::Compile(const VansGameplayCookedAsset& cooked)
{
	VansGameplayCompileResult result;
	result.asset.assetType = cooked.assetType;
	result.asset.schemaVersion = cooked.schemaVersion;
	result.asset.contentHash = cooked.contentHash;
	result.asset.dependencies = cooked.dependencies;
	const VansGameplayAssetSchemaDescriptor* schema =
		VansGameplayAssetSchemaRegistry::BuiltIns().Resolve(cooked.assetType);
	if (!schema || schema->editorOnly || cooked.schemaVersion != schema->schemaVersion ||
		cooked.contentHash == 0)
	{
		result.error = "Gameplay compiled asset header is invalid";
		return result;
	}
	bool compiled = false;
	switch (cooked.assetType)
	{
	case VansAssetType::ActionDefinition:
		compiled = CompileAction(cooked, result.asset.data, result.diagnostics); break;
	case VansAssetType::ActionSet:
		compiled = CompileActionSet(cooked, result.asset.data); break;
	case VansAssetType::GameplayEffect:
		compiled = CompileEffect(cooked, result.asset.data); break;
	case VansAssetType::GameplayCue:
		compiled = CompileCue(cooked, result.asset.data); break;
	case VansAssetType::AttributeSet:
		compiled = CompileAttributeSet(cooked, result.asset.data, result.diagnostics); break;
	case VansAssetType::TargetingPolicy:
		compiled = CompileTargeting(cooked, result.asset.data); break;
	case VansAssetType::GameplayTagTree:
		compiled = CompileTagTree(cooked, result.asset.data, result.diagnostics); break;
	case VansAssetType::PayloadSchema:
		compiled = CompilePayload(cooked, result.asset.data); break;
	case VansAssetType::ActionGraph:
		compiled = CompileGraph(cooked, result.asset.data, result.diagnostics); break;
	case VansAssetType::CameraRigProfile:
	case VansAssetType::CameraShakeProfile:
		compiled = CompileCameraProfile(cooked, result.asset.data, result.diagnostics); break;
	default:
		break;
	}
	if (!compiled)
		result.error = HasErrors(result.diagnostics)
			? "Gameplay asset failed typed compilation"
			: "Gameplay asset type has no runtime compiler";
	return result;
}
}
