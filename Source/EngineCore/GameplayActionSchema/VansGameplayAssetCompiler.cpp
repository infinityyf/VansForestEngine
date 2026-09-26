#include "VansGameplayAssetCompiler.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

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

template <typename Enum, std::size_t Count>
bool ReadEnumValue(
	std::string_view value,
	const std::pair<std::string_view, Enum> (&entries)[Count],
	Enum& output)
{
	for (const auto& entry : entries)
		if (entry.first == value)
		{
			output = entry.second;
			return true;
		}
	return false;
}

void AddInvalidEnumDiagnostic(
	VansGameplayDiagnostics& diagnostics,
	std::string code,
	std::string_view fieldName,
	std::string_view value,
	std::string fieldPath)
{
	AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
		std::move(code), "Unknown " + std::string(fieldName) + ": " + std::string(value),
		std::move(fieldPath));
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

bool ReadConcurrencyPolicy(
	std::string_view value,
	VansActionConcurrencyPolicy& output)
{
	static constexpr std::pair<std::string_view, VansActionConcurrencyPolicy> kValues[] = {
		{ "Allow", VansActionConcurrencyPolicy::Allow },
		{ "Reject", VansActionConcurrencyPolicy::RejectNew },
		{ "RejectNew", VansActionConcurrencyPolicy::RejectNew },
		{ "CancelExisting", VansActionConcurrencyPolicy::CancelExisting },
		{ "Queue", VansActionConcurrencyPolicy::QueueNew },
		{ "QueueNew", VansActionConcurrencyPolicy::QueueNew }
	};
	return ReadEnumValue(value, kValues, output);
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
	action->contentHash = cooked.contentHash;

	action->program.metadata.displayName = StringAt(root, "/metadata/displayName");
	action->program.metadata.category = StringAt(root, "/metadata/category", "Gameplay");
	action->program.metadata.labels = StringArray(At(root, "/metadata/labels"));
	action->program.metadata.priority =
		static_cast<std::int32_t>(IntAt(root, "/metadata/priority", 0));
	if (const VansSerializedValue* schema = At(root, "/context/schema"))
		action->program.contextSchema = *schema;
	if (const VansSerializedValue* defaults = At(root, "/context/defaults"))
		action->program.contextDefaults = *defaults;

	const auto appendRecords = [&](const char* path,
		std::vector<VansCompiledActionRecord>& destination)
	{
		const VansSerializedValue* values = At(root, path);
		if (!values || values->kind != VansSerializedValue::Kind::Array) return;
		for (std::size_t index = 0; index < values->arrayItems.size(); ++index)
		{
			const VansSerializedValue& item = values->arrayItems[index];
			const VansSerializedValue* inputs = item.kind == VansSerializedValue::Kind::Object
				? FindObjectField(item, "inputs") : nullptr;
			const std::string type = item.kind == VansSerializedValue::Kind::Object
				? ReadSerializedStringField(item, "type") : std::string{};
			if (type.empty() || !inputs || inputs->kind != VansSerializedValue::Kind::Object)
			{
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-ACTION-REGISTERED-TYPE", "Registered Action record is invalid",
					std::string(path) + "/" + std::to_string(index));
				continue;
			}
			destination.push_back({ type, *inputs });
		}
	};
	appendRecords("/policies", action->program.policies);
	appendRecords("/phases/activate/guards", action->program.activate.guards);
	appendRecords("/phases/activate/operations", action->program.activate.operations);
	appendRecords("/phases/commit/guards", action->program.commit.guards);
	appendRecords("/phases/commit/operations", action->program.commit.operations);
	appendRecords("/phases/execute/drivers", action->program.execute.drivers);
	appendRecords("/phases/execute/operations", action->program.execute.operations);
	appendRecords("/phases/finish/operations", action->program.finish.operations);
	appendRecords("/phases/cancel/operations", action->program.cancel.operations);
	appendRecords("/transitions", action->program.transitions);
	appendRecords("/extensions", action->program.extensions);

	const auto appendActionReferences = [&](const VansSerializedValue* values,
		std::vector<std::string>& references, std::vector<VansActionId>& actions)
	{
		if (!values || values->kind != VansSerializedValue::Kind::Array) return;
		for (const VansSerializedValue& item : values->arrayItems)
		{
			const std::string reference = ReferenceString(&item);
			if (reference.empty()) continue;
			references.push_back(reference);
			actions.push_back(StableId<VansActionIdTag>(reference));
		}
	};

	for (std::size_t policyIndex = 0;
		policyIndex < action->program.policies.size(); ++policyIndex)
	{
		const VansCompiledActionRecord& policy = action->program.policies[policyIndex];
		const VansSerializedValue& inputs = policy.inputs;
		if (policy.type == "Core.Policy.Concurrency")
		{
			action->concurrencyGroup = StableId<VansActionConcurrencyGroupIdTag>(
				ReadSerializedStringField(inputs, "group"));
			const std::string mode = ReadSerializedStringField(inputs, "mode", "Allow");
			if (!ReadConcurrencyPolicy(mode, action->concurrencyPolicy))
			{
				AddInvalidEnumDiagnostic(diagnostics, "GAF-ACTION-CONCURRENCY-MODE",
					"Action concurrency mode", mode,
					"/policies/" + std::to_string(policyIndex) + "/inputs/mode");
				continue;
			}
			action->concurrencyLimit = static_cast<std::uint32_t>((std::max<std::int64_t>)(1,
				ReadSerializedIntField(inputs, "limit", 1)));
			action->concurrencyQueueTimeoutSeconds =
				NumberField(inputs, "queueTimeout", 0.0);
		}
		else if (policy.type == "Core.Policy.Cancellation")
			action->cancellable = ReadSerializedBoolField(inputs, "cancellable", true);
		else if (policy.type == "Core.Policy.Interruption")
		{
			action->interruptible = ReadSerializedBoolField(inputs, "interruptible", true);
			appendActionReferences(FindObjectField(inputs, "blockedActions"),
				action->blockedActionReferences, action->blockedActions);
			appendActionReferences(FindObjectField(inputs, "cancelActions"),
				action->cancelActionReferences, action->cancelActions);
		}
		else if (policy.type == "Core.Policy.Completion" ||
			policy.type == "Core.Policy.Clock" || policy.type == "Core.Policy.Trigger")
		{
			// Generic policy records remain in the compiled program for their contributors.
		}
		else if (policy.type != "Core.Policy.Budget" &&
			policy.type != "Core.Policy.InputBuffer" &&
			policy.type != "Core.Policy.Failure")
			AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
				"GAF-ACTION-POLICY", "Action policy type is not registered", policy.type);
	}

	action->executor = StableId<VansActionExecutorIdTag>("Action.Executor.Immediate");
	for (const VansCompiledActionRecord& driver : action->program.execute.drivers)
	{
		if (driver.type == "Core.Driver.Graph")
		{
			action->executor = StableId<VansActionExecutorIdTag>("Action.Executor.Graph");
			action->executionGraphAsset = ReferenceString(
				FindObjectField(driver.inputs, "graph"));
		}
		else if (driver.type == "Core.Driver.Immediate")
			action->executor = StableId<VansActionExecutorIdTag>("Action.Executor.Immediate");
	}

	if (const VansSerializedValue* variables = At(root, "/variables");
		variables && variables->kind == VansSerializedValue::Kind::Array)
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

	action->program.capabilities = StringArray(At(root, "/dependencies/capabilities"));
	action->program.modules = StringArray(At(root, "/dependencies/modules"));

	const VansGameplayDiagnostics runtimeDiagnostics =
		VansActionDefinitionRegistry::Validate(*action);
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
			if (const VansSerializedValue* extensions = FindObjectField(item, "extensions");
				extensions && extensions->kind == VansSerializedValue::Kind::Array)
				for (const VansSerializedValue& extension : extensions->arrayItems)
				{
					if (extension.kind != VansSerializedValue::Kind::Object) continue;
					const std::string type = ReadSerializedStringField(extension, "type");
					const VansSerializedValue* inputs = FindObjectField(extension, "inputs");
					if (!type.empty() && inputs && inputs->kind == VansSerializedValue::Kind::Object)
						grant.extensions.push_back({ type, *inputs });
				}
			set.grants.push_back(std::move(grant));
		}
	}
	const auto appendRecords = [&root](const char* path,
		std::vector<VansCompiledActionRecord>& destination)
	{
		const VansSerializedValue* records = At(root, path);
		if (!records || records->kind != VansSerializedValue::Kind::Array) return;
		for (const VansSerializedValue& record : records->arrayItems)
		{
			if (record.kind != VansSerializedValue::Kind::Object) continue;
			const std::string type = ReadSerializedStringField(record, "type");
			const VansSerializedValue* inputs = FindObjectField(record, "inputs");
			if (!type.empty() && inputs && inputs->kind == VansSerializedValue::Kind::Object)
				destination.push_back({ type, *inputs });
		}
	};
	appendRecords("/initializers", set.initializers);
	appendRecords("/policies", set.policies);
	output = std::move(set);
	return true;
}

bool ReadEffectModifierApplication(
	std::string_view value,
	VansEffectModifierApplication& application)
{
	static constexpr std::pair<std::string_view, VansEffectModifierApplication> kValues[] = {
		{ "Base", VansEffectModifierApplication::Base },
		{ "Persistent", VansEffectModifierApplication::Persistent }
	};
	return ReadEnumValue(value, kValues, application);
}

bool ReadEffectModifierOperation(
	std::string_view value,
	VansEffectModifierOperation& operation)
{
	static constexpr std::pair<std::string_view, VansEffectModifierOperation> kValues[] = {
		{ "Add", VansEffectModifierOperation::Add },
		{ "Multiply", VansEffectModifierOperation::Multiply },
		{ "Set", VansEffectModifierOperation::Set }
	};
	return ReadEnumValue(value, kValues, operation);
}

bool ReadEffectMagnitudeSource(
	std::string_view value,
	VansEffectMagnitudeSource& output)
{
	static constexpr std::pair<std::string_view, VansEffectMagnitudeSource> kValues[] = {
		{ "Fixed", VansEffectMagnitudeSource::Fixed },
		{ "SetByCaller", VansEffectMagnitudeSource::SetByCaller },
		{ "CapturedAttribute", VansEffectMagnitudeSource::CapturedAttribute },
		{ "ContextPayload", VansEffectMagnitudeSource::ContextPayload },
		{ "TargetData", VansEffectMagnitudeSource::TargetData },
		{ "RandomRange", VansEffectMagnitudeSource::RandomRange }
	};
	return ReadEnumValue(value, kValues, output);
}

bool ReadEffectTargetDataMetric(
	std::string_view value,
	VansEffectTargetDataMetric& output)
{
	static constexpr std::pair<std::string_view, VansEffectTargetDataMetric> kValues[] = {
		{ "Count", VansEffectTargetDataMetric::Count },
		{ "HitDistance", VansEffectTargetDataMetric::HitDistance },
		{ "RayLength", VansEffectTargetDataMetric::RayLength }
	};
	return ReadEnumValue(value, kValues, output);
}

bool ReadEffectCapturePolicy(
	std::string_view value,
	VansEffectCapturePolicy& output)
{
	static constexpr std::pair<std::string_view, VansEffectCapturePolicy> kValues[] = {
		{ "Snapshot", VansEffectCapturePolicy::Snapshot },
		{ "Dynamic", VansEffectCapturePolicy::Dynamic }
	};
	return ReadEnumValue(value, kValues, output);
}

bool ReadEffectDurationPolicy(
	std::string_view value,
	VansEffectDurationPolicy& output)
{
	static constexpr std::pair<std::string_view, VansEffectDurationPolicy> kValues[] = {
		{ "Instant", VansEffectDurationPolicy::Instant },
		{ "Duration", VansEffectDurationPolicy::Duration },
		{ "Infinite", VansEffectDurationPolicy::Infinite }
	};
	return ReadEnumValue(value, kValues, output);
}

bool ReadEffectStackingPolicy(
	std::string_view value,
	VansEffectStackingPolicy& output)
{
	static constexpr std::pair<std::string_view, VansEffectStackingPolicy> kValues[] = {
		{ "None", VansEffectStackingPolicy::None },
		{ "AggregateBySource", VansEffectStackingPolicy::AggregateBySource },
		{ "AggregateByTarget", VansEffectStackingPolicy::AggregateByTarget }
	};
	return ReadEnumValue(value, kValues, output);
}

bool ReadEffectOverflowPolicy(
	std::string_view value,
	VansEffectOverflowPolicy& output)
{
	static constexpr std::pair<std::string_view, VansEffectOverflowPolicy> kValues[] = {
		{ "Reject", VansEffectOverflowPolicy::Reject },
		{ "RefreshOnly", VansEffectOverflowPolicy::RefreshOnly },
		{ "ReplaceOldest", VansEffectOverflowPolicy::ReplaceOldest }
	};
	return ReadEnumValue(value, kValues, output);
}

enum class VansEffectCuePhase : std::uint8_t
{
	Execute,
	Persistent,
	Periodic,
	Remove
};

bool ReadEffectCuePhase(std::string_view value, VansEffectCuePhase& output)
{
	static constexpr std::pair<std::string_view, VansEffectCuePhase> kValues[] = {
		{ "Execute", VansEffectCuePhase::Execute },
		{ "Persistent", VansEffectCuePhase::Persistent },
		{ "Periodic", VansEffectCuePhase::Periodic },
		{ "Remove", VansEffectCuePhase::Remove }
	};
	return ReadEnumValue(value, kValues, output);
}

bool CompileEffect(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansCompiledEffectAsset compiled;
	auto effect = std::make_shared<VansEffectDefinition>();
	effect->name = StringAt(root, "/effectId");
	effect->id = StableId<VansEffectIdTag>(effect->name);
	const std::string durationPolicy = StringAt(root, "/duration/policy", "Instant");
	if (!ReadEffectDurationPolicy(durationPolicy, effect->durationPolicy))
	{
		AddInvalidEnumDiagnostic(diagnostics, "GAF-EFFECT-DURATION-POLICY",
			"Gameplay Effect duration policy", durationPolicy, "/duration/policy");
		return false;
	}
	effect->durationSeconds = NumberAt(root, "/duration/seconds");
	effect->periodSeconds = NumberAt(root, "/duration/period");
	effect->executePeriodicOnApply = BoolAt(root, "/duration/executePeriodicOnApply");
	const std::string stacking = StringAt(root, "/stacking/policy", "None");
	if (!ReadEffectStackingPolicy(stacking, effect->stackingPolicy))
	{
		AddInvalidEnumDiagnostic(diagnostics, "GAF-EFFECT-STACKING-POLICY",
			"Gameplay Effect stacking policy", stacking, "/stacking/policy");
		return false;
	}
	const std::string overflow = StringAt(root, "/stacking/overflow", "Reject");
	if (!ReadEffectOverflowPolicy(overflow, effect->overflowPolicy))
	{
		AddInvalidEnumDiagnostic(diagnostics, "GAF-EFFECT-OVERFLOW-POLICY",
			"Gameplay Effect overflow policy", overflow, "/stacking/overflow");
		return false;
	}
	const std::int64_t maximumStacks = IntAt(root, "/stacking/maximumStacks", 1);
	if (maximumStacks <= 0)
	{
		AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
			"GAF-EFFECT-STACK", "maximumStacks must be positive",
			"/stacking/maximumStacks");
		return false;
	}
	effect->maximumStacks = static_cast<std::uint32_t>(maximumStacks);
	effect->refreshDurationOnStack = BoolAt(root, "/stacking/refreshDuration", false);
	effect->resetPeriodOnStack = BoolAt(root, "/stacking/resetPeriod", false);
	effect->requirements = TagQuery(At(root, "/requirements"));
	effect->immunity = TagQuery(At(root, "/immunity"));
	for (const std::string& tag : StringArray(At(root, "/grantedTags")))
		effect->grantedTags.push_back(StableId<VansGameplayTagIdTag>(tag));
	if (const VansSerializedValue* extensions = At(root, "/extensions");
		extensions && extensions->kind == VansSerializedValue::Kind::Array)
	{
		for (std::size_t extensionIndex = 0;
			extensionIndex < extensions->arrayItems.size(); ++extensionIndex)
		{
			const VansSerializedValue& extension = extensions->arrayItems[extensionIndex];
			const std::string inputPath = "/extensions/" +
				std::to_string(extensionIndex) + "/inputs/";
			if (extension.kind != VansSerializedValue::Kind::Object) continue;
			const std::string type = ReadSerializedStringField(extension, "type");
			const VansSerializedValue* inputs = FindObjectField(extension, "inputs");
			if (!inputs || inputs->kind != VansSerializedValue::Kind::Object) continue;
			if (type == "Gameplay.Effect.AttributeModifier")
			{
				VansEffectModifier modifier;
				modifier.attribute = StableId<VansAttributeIdTag>(
					ReadSerializedStringField(*inputs, "attribute"));
				const std::string application =
					ReadSerializedStringField(*inputs, "application");
				if (!ReadEffectModifierApplication(application, modifier.application))
				{
					AddInvalidEnumDiagnostic(diagnostics, "GAF-EFFECT-MODIFIER-APPLICATION",
						"Gameplay Effect modifier application", application,
						inputPath + "application");
					return false;
				}
				const std::string operation =
					ReadSerializedStringField(*inputs, "operation");
				if (!ReadEffectModifierOperation(operation, modifier.operation))
				{
					AddInvalidEnumDiagnostic(diagnostics, "GAF-EFFECT-MODIFIER-OPERATION",
						"Gameplay Effect modifier operation", operation, inputPath + "operation");
					return false;
				}
				modifier.magnitude = FindObjectField(*inputs, "magnitude")
					? ReadSerializedNumber(*FindObjectField(*inputs, "magnitude")) : 0.0;
				modifier.priority = static_cast<std::int32_t>(
					ReadSerializedIntField(*inputs, "priority", 0));
				const std::string magnitudeSource =
					ReadSerializedStringField(*inputs, "magnitudeSource", "Fixed");
				if (!ReadEffectMagnitudeSource(magnitudeSource, modifier.magnitudeSource))
				{
					AddInvalidEnumDiagnostic(diagnostics, "GAF-EFFECT-MAGNITUDE-SOURCE",
						"Gameplay Effect magnitude source", magnitudeSource,
						inputPath + "magnitudeSource");
					return false;
				}
				modifier.setByCallerField = StableId<VansActionFieldIdTag>(
					ReadSerializedStringField(*inputs, "setByCaller"));
				modifier.capturedAttribute = StableId<VansAttributeIdTag>(
					ReadSerializedStringField(*inputs, "capturedAttribute"));
				const std::string capture =
					ReadSerializedStringField(*inputs, "capture", "Snapshot");
				if (!ReadEffectCapturePolicy(capture, modifier.capturePolicy))
				{
					AddInvalidEnumDiagnostic(diagnostics, "GAF-EFFECT-CAPTURE-POLICY",
						"Gameplay Effect capture policy", capture, inputPath + "capture");
					return false;
				}
				modifier.contextPayloadPath = ReadSerializedStringField(*inputs, "contextPath");
				const std::string targetMetric =
					ReadSerializedStringField(*inputs, "targetMetric", "Count");
				if (!ReadEffectTargetDataMetric(targetMetric, modifier.targetDataMetric))
				{
					AddInvalidEnumDiagnostic(diagnostics, "GAF-EFFECT-TARGET-METRIC",
						"Gameplay Effect target metric", targetMetric, inputPath + "targetMetric");
					return false;
				}
				modifier.randomMinimum = NumberField(*inputs, "randomMinimum", 0.0);
				modifier.randomMaximum = NumberField(*inputs, "randomMaximum", 1.0);
				modifier.coefficient = NumberField(*inputs, "coefficient", 1.0);
				modifier.preAdd = NumberField(*inputs, "preAdd", 0.0);
				modifier.postAdd = NumberField(*inputs, "postAdd", 0.0);
				effect->modifiers.push_back(modifier);
				continue;
			}
			if (type != "Gameplay.Effect.CueBinding") continue;
			std::vector<VansCueId>* cueIds = nullptr;
			std::vector<std::string>* cueAssets = nullptr;
			const std::string phase = ReadSerializedStringField(*inputs, "phase", "Execute");
			VansEffectCuePhase cuePhase{};
			if (!ReadEffectCuePhase(phase, cuePhase))
			{
				AddInvalidEnumDiagnostic(diagnostics, "GAF-EFFECT-CUE-PHASE",
					"Gameplay Effect Cue phase", phase, inputPath + "phase");
				return false;
			}
			if (cuePhase == VansEffectCuePhase::Execute)
			{
				cueIds = &effect->executeCues;
				cueAssets = &compiled.executeCueAssets;
			}
			else if (cuePhase == VansEffectCuePhase::Persistent)
			{
				cueIds = &effect->persistentCues;
				cueAssets = &compiled.persistentCueAssets;
			}
			else if (cuePhase == VansEffectCuePhase::Periodic)
			{
				cueIds = &effect->periodicCues;
				cueAssets = &compiled.periodicCueAssets;
			}
			else if (cuePhase == VansEffectCuePhase::Remove)
			{
				cueIds = &effect->removeCues;
				cueAssets = &compiled.removeCueAssets;
			}
			const VansSerializedValue* assets = FindObjectField(*inputs, "assets");
			if (!assets || assets->kind != VansSerializedValue::Kind::Array)
			{
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-EFFECT-CUE-ASSETS", "Gameplay Effect Cue assets must be an array",
					inputPath + "assets");
				return false;
			}
			for (const VansSerializedValue& item : assets->arrayItems)
			{
				const std::string reference = ReferenceString(&item);
				if (reference.empty()) continue;
				cueAssets->push_back(reference);
				cueIds->push_back(StableId<VansCueIdTag>(reference));
			}
		}
	}
	const VansEffectPolicyValidation policyIssues =
		VansValidateEffectPolicy(*effect);
	for (const VansEffectPolicyIssue& issue : policyIssues)
		AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
			"GAF-EFFECT-FIELD-INAPPLICABLE", std::string(issue.message),
			std::string(VansEffectPolicyFieldPath(issue.field)));
	if (!policyIssues.IsValid()) return false;
	compiled.definition = std::shared_ptr<const VansEffectDefinition>(std::move(effect));
	output = std::move(compiled);
	return true;
}

bool ReadCueScope(std::string_view value, VansGameplayCueScope& output)
{
	static constexpr std::pair<std::string_view, VansGameplayCueScope> kValues[] = {
		{ "Owner", VansGameplayCueScope::Owner },
		{ "Target", VansGameplayCueScope::Target },
		{ "Observers", VansGameplayCueScope::Observers },
		{ "World", VansGameplayCueScope::World },
		{ "LocalOnly", VansGameplayCueScope::LocalOnly }
	};
	return ReadEnumValue(value, kValues, output);
}

bool CompileCueCommand(
	const VansSerializedValue* source,
	bool required,
	std::string_view path,
	VansGameplayCueCommandBinding& command,
	VansGameplayDiagnostics& diagnostics)
{
	if (!source)
	{
		if (!required) return true;
		AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
			"GAF-CUE-COMMAND", "Gameplay Cue invoke command is missing", std::string(path));
		return false;
	}
	if (source->kind != VansSerializedValue::Kind::Object)
	{
		AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
			"GAF-CUE-COMMAND", "Gameplay Cue command must be an object", std::string(path));
		return false;
	}
	const std::string name = ReadSerializedStringField(*source, "command");
	if (name.empty())
	{
		if (!required && source->objectFields.empty()) return true;
		AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
			"GAF-CUE-COMMAND", "Gameplay Cue command name is missing",
			std::string(path) + "/command");
		return false;
	}
	command.command = StableId<VansActionFieldIdTag>(name);
	if (const VansSerializedValue* values = FindObjectField(*source, "values"))
	{
		if (values->kind != VansSerializedValue::Kind::Object)
		{
			AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
				"GAF-CUE-VALUES", "Gameplay Cue command values must be an object",
				std::string(path) + "/values");
			return false;
		}
		command.values = *values;
	}
	if (const VansSerializedValue* bindings = FindObjectField(*source, "bindings"))
	{
		if (bindings->kind != VansSerializedValue::Kind::Object)
		{
			AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
				"GAF-CUE-FIELD-BINDINGS", "Gameplay Cue field bindings must be an object",
				std::string(path) + "/bindings");
			return false;
		}
		for (const auto& [field, encodedSource] : bindings->objectFields)
		{
			VansGameplayCueSource cueSource;
			if (field.empty() || encodedSource.kind != VansSerializedValue::Kind::String ||
				!VansReadGameplayCueSource(encodedSource.stringValue, cueSource))
			{
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-CUE-FIELD-SOURCE", "Gameplay Cue field source is invalid",
					std::string(path) + "/bindings/" + field);
				return false;
			}
			command.fields.push_back({ field, cueSource });
		}
		std::sort(command.fields.begin(), command.fields.end(),
			[](const auto& left, const auto& right) { return left.field < right.field; });
	}
	return true;
}

bool CompileCue(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansCompiledGameplayCueDefinition cue;
	cue.name = StringAt(root, "/cueId");
	cue.id = StableId<VansCueIdTag>(cue.name);
	const std::string scope = StringAt(root, "/scope", "Target");
	if (!ReadCueScope(scope, cue.scope))
	{
		AddInvalidEnumDiagnostic(diagnostics, "GAF-CUE-SCOPE",
			"Gameplay Cue scope", scope, "/scope");
		return false;
	}
	cue.payloadSchemaAsset = ReferenceString(At(root, "/payloadSchema"));
	std::size_t bindingCount = 0;
	if (const VansSerializedValue* bindings = At(root, "/bindings");
		bindings && bindings->kind == VansSerializedValue::Kind::Array)
		for (std::size_t index = 0; index < bindings->arrayItems.size(); ++index)
		{
			const VansSerializedValue& binding = bindings->arrayItems[index];
			if (binding.kind != VansSerializedValue::Kind::Object ||
				ReadSerializedStringField(binding, "type") != "Gameplay.Cue.Invoke") continue;
			if (++bindingCount > 1)
			{
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-CUE-BINDING-COUNT", "Gameplay Cue supports exactly one Service binding",
					"/bindings/" + std::to_string(index));
				return false;
			}
			const VansSerializedValue* inputs = FindObjectField(binding, "inputs");
			if (!inputs || inputs->kind != VansSerializedValue::Kind::Object)
			{
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-CUE-BINDING", "Gameplay Cue Service binding inputs are invalid",
					"/bindings/" + std::to_string(index) + "/inputs");
				return false;
			}
			VansGameplayCueBinding compiled;
			const std::string capability = ReadSerializedStringField(*inputs, "capability");
			if (capability.empty())
			{
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-CUE-CAPABILITY", "Gameplay Cue capability is missing",
					"/bindings/" + std::to_string(index) + "/inputs/capability");
				return false;
			}
			compiled.service = StableId<VansActionServiceIdTag>(capability);
			compiled.asset = ReferenceString(FindObjectField(*inputs, "asset"));
			const std::string commandRoot = "/bindings/" + std::to_string(index) + "/inputs/";
			if (!CompileCueCommand(FindObjectField(*inputs, "invoke"), true,
					commandRoot + "invoke", compiled.invoke, diagnostics) ||
				!CompileCueCommand(FindObjectField(*inputs, "update"), false,
					commandRoot + "update", compiled.update, diagnostics) ||
				!CompileCueCommand(FindObjectField(*inputs, "release"), false,
					commandRoot + "release", compiled.release, diagnostics))
				return false;
			cue.binding = std::move(compiled);
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

bool CompileTargeting(
	const VansGameplayCookedAsset& cooked,
	const VansTargetingHandlerRegistry& handlers,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansTargetingPolicy policy;
	policy.name = StringAt(root, "/targetingId");
	policy.id = StableId<VansTargetingPolicyIdTag>(policy.name);
	const VansSerializedValue* steps = At(root, "/steps");
	if (!steps || steps->kind != VansSerializedValue::Kind::Array || steps->arrayItems.empty())
		AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
			"GAF-TARGETING-STEPS", "Targeting policy must contain at least one step", "/steps");
	else
	{
		for (std::size_t index = 0; index < steps->arrayItems.size(); ++index)
		{
			const VansSerializedValue& item = steps->arrayItems[index];
			const std::string path = "/steps/" + std::to_string(index);
			if (item.kind != VansSerializedValue::Kind::Object)
			{
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-TARGETING-STEP", "Targeting step must be an object", path);
				continue;
			}
			const std::string stableName = ReadSerializedStringField(item, "type");
			const std::shared_ptr<const IVansTargetingStepHandler> handler =
				handlers.Find(stableName);
			if (!handler)
			{
				AddDiagnostic(diagnostics, VansGameplayDiagnosticSeverity::Error,
					"GAF-TARGETING-HANDLER", "Unknown Targeting handler: " + stableName,
					path + "/type");
				continue;
			}
			VansTargetingStep step;
			step.handler = handler->TypeId();
			if (const VansSerializedValue* inputs = FindObjectField(item, "inputs"))
				step.inputs = *inputs;
			policy.steps.push_back(std::move(step));
		}
	}
	output = std::move(policy);
	return !HasErrors(diagnostics);
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
	tree.tags = dictionary.Snapshot();
	output = std::move(tree);
	return !HasErrors(diagnostics);
}

bool ReadPayloadFieldType(std::string_view value, VansTimelineValueType& output)
{
	static constexpr std::pair<std::string_view, VansTimelineValueType> kValues[] = {
		{ "Bool", VansTimelineValueType::Bool },
		{ "Int32", VansTimelineValueType::Int32 },
		{ "Int64", VansTimelineValueType::Int64 },
		{ "Float", VansTimelineValueType::Float },
		{ "Double", VansTimelineValueType::Double },
		{ "Enum", VansTimelineValueType::Enum },
		{ "String", VansTimelineValueType::String },
		{ "Vec2", VansTimelineValueType::Vec2 },
		{ "Vec3", VansTimelineValueType::Vec3 },
		{ "Vec4", VansTimelineValueType::Vec4 },
		{ "Quaternion", VansTimelineValueType::Quaternion },
		{ "ColorLinear", VansTimelineValueType::ColorLinear },
		{ "ColorSrgb", VansTimelineValueType::ColorSrgb },
		{ "ObjectReference", VansTimelineValueType::ObjectReference },
		{ "Struct", VansTimelineValueType::Struct }
	};
	return ReadEnumValue(value, kValues, output);
}

bool CompilePayload(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	VansTimelinePayloadSchema schema;
	schema.stableName = StringAt(root, "/payloadTypeId");
	schema.typeId = StableId<VansTimelinePayloadTypeTag>(schema.stableName);
	schema.maximumBytes = static_cast<std::uint32_t>(std::max<std::int64_t>(1,
		IntAt(root, "/maximumBytes", 4096)));
	schema.allowAdditionalFields = BoolAt(root, "/allowAdditionalFields", false);
	if (const VansSerializedValue* fields = At(root, "/fields");
		fields && fields->kind == VansSerializedValue::Kind::Array)
	{
		for (std::size_t index = 0; index < fields->arrayItems.size(); ++index)
		{
			const VansSerializedValue& item = fields->arrayItems[index];
			if (item.kind != VansSerializedValue::Kind::Object) continue;
			VansTimelinePayloadFieldSchema field;
			field.name = ReadSerializedStringField(item, "name");
			field.id = StableId<VansTimelineFieldTag>(field.name);
			const std::string fieldType = ReadSerializedStringField(item, "type");
			if (!ReadPayloadFieldType(fieldType, field.type))
			{
				AddInvalidEnumDiagnostic(diagnostics, "GAF-PAYLOAD-FIELD-TYPE",
					"Payload field type", fieldType,
					"/fields/" + std::to_string(index) + "/type");
				continue;
			}
			field.required = ReadSerializedBoolField(item, "required", false);
			schema.fields.push_back(std::move(field));
		}
	}
	output = std::move(schema);
	return !HasErrors(diagnostics);
}

bool ReadGraphNodeKind(std::string_view value, VansActionGraphNodeKind& output)
{
	static constexpr std::pair<std::string_view, VansActionGraphNodeKind> kValues[] = {
		{ "Pure", VansActionGraphNodeKind::Pure },
		{ "Command", VansActionGraphNodeKind::Command },
		{ "Latent", VansActionGraphNodeKind::Latent },
		{ "State", VansActionGraphNodeKind::State },
		{ "Flow", VansActionGraphNodeKind::Flow },
		{ "Bridge", VansActionGraphNodeKind::Bridge },
		{ "SubAction", VansActionGraphNodeKind::SubAction }
	};
	return ReadEnumValue(value, kValues, output);
}

bool CompileGraph(
	const VansGameplayCookedAsset& cooked,
	VansCompiledGameplayAssetData& output,
	VansGameplayDiagnostics& diagnostics)
{
	const VansSerializedValue& root = cooked.runtimeDocument;
	auto graph = std::make_shared<VansCompiledActionGraph>();
	graph->name = StringAt(root, "/graphId");
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
			const std::string kind = ReadSerializedStringField(item, "kind", "Pure");
			if (!ReadGraphNodeKind(kind, node.kind))
				AddInvalidEnumDiagnostic(diagnostics, "GAF-GRAPH-NODE-KIND",
					"Action Graph node kind", kind,
					"/nodes/" + std::to_string(index) + "/kind");
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

}

bool VansGameplayAssetCompilerRegistry::Register(
	VansAssetType assetType,
	std::string stableName,
	Compiler compiler,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Gameplay asset Compiler registry is sealed";
		return false;
	}
	if (assetType == VansAssetType::Unknown || stableName.empty() || !compiler)
	{
		error = "Gameplay asset Compiler descriptor is invalid";
		return false;
	}
	if (!m_Compilers.emplace(assetType,
		Entry{ std::move(stableName), std::move(compiler) }).second)
	{
		error = "duplicate Gameplay asset Compiler";
		return false;
	}
	return true;
}

bool VansGameplayAssetCompilerRegistry::Seal(std::string& error)
{
	if (m_Compilers.empty())
	{
		error = "Gameplay asset Compiler registry is empty";
		return false;
	}
	m_Sealed = true;
	return true;
}

const VansGameplayAssetCompilerRegistry::Compiler* VansGameplayAssetCompilerRegistry::Resolve(
	VansAssetType assetType) const
{
	const auto found = m_Compilers.find(assetType);
	return found == m_Compilers.end() ? nullptr : &found->second.compiler;
}

bool VansRegisterCoreGameplayAssetCompilers(
	VansGameplayAssetCompilerRegistry& registry,
	std::string& error)
{
	return registry.Register(VansAssetType::ActionDefinition, "Core.Asset.Action",
		[](const auto& cooked, auto& output, auto& diagnostics)
		{ return CompileAction(cooked, output, diagnostics); }, error) &&
		registry.Register(VansAssetType::ActionSet, "Core.Asset.ActionSet",
			[](const auto& cooked, auto& output, auto&)
			{ return CompileActionSet(cooked, output); }, error) &&
		registry.Register(VansAssetType::PayloadSchema, "Core.Asset.PayloadSchema",
			[](const auto& cooked, auto& output, auto& diagnostics)
			{ return CompilePayload(cooked, output, diagnostics); }, error) &&
		registry.Register(VansAssetType::ActionGraph, "Core.Asset.ActionGraph",
			[](const auto& cooked, auto& output, auto& diagnostics)
			{ return CompileGraph(cooked, output, diagnostics); }, error);
}

bool VansRegisterGameplayPrimitiveAssetCompilers(
	VansGameplayAssetCompilerRegistry& registry,
	std::string& error)
{
	auto targetingHandlers = std::make_shared<VansTargetingHandlerRegistry>();
	if (!VansBuildBuiltInTargetingHandlerRegistry(*targetingHandlers, error)) return false;
	return registry.Register(VansAssetType::GameplayEffect, "Gameplay.Asset.Effect",
		[](const auto& cooked, auto& output, auto& diagnostics)
		{ return CompileEffect(cooked, output, diagnostics); }, error) &&
		registry.Register(VansAssetType::GameplayCue, "Gameplay.Asset.Cue",
			[](const auto& cooked, auto& output, auto& diagnostics)
			{ return CompileCue(cooked, output, diagnostics); }, error) &&
		registry.Register(VansAssetType::AttributeSet, "Gameplay.Asset.AttributeSet",
			[](const auto& cooked, auto& output, auto& diagnostics)
			{ return CompileAttributeSet(cooked, output, diagnostics); }, error) &&
		registry.Register(VansAssetType::TargetingPolicy, "Gameplay.Asset.TargetingPolicy",
			[targetingHandlers](const auto& cooked, auto& output, auto& diagnostics)
			{ return CompileTargeting(cooked, *targetingHandlers, output, diagnostics); }, error) &&
		registry.Register(VansAssetType::GameplayTagTree, "Gameplay.Asset.TagTree",
			[](const auto& cooked, auto& output, auto& diagnostics)
			{ return CompileTagTree(cooked, output, diagnostics); }, error);
}

VansGameplayCompileResult VansGameplayAssetCompiler::Compile(
	const VansGameplayCookedAsset& cooked)
{
	static const VansGameplayAssetCompilerRegistry compilers = []
	{
		VansGameplayAssetCompilerRegistry registry;
		std::string error;
		VansRegisterDefaultGameplayAssetCompilers(registry, error);
		registry.Seal(error);
		return registry;
	}();
	return Compile(cooked, compilers);
}

VansGameplayCompileResult VansGameplayAssetCompiler::Compile(
	const VansGameplayCookedAsset& cooked,
	const VansGameplayAssetCompilerRegistry& compilers)
{
	VansGameplayCompileResult result;
	result.asset.assetType = cooked.assetType;
	result.asset.contentHash = cooked.contentHash;
	result.asset.dependencies = cooked.dependencies;
	if (cooked.assetType == VansAssetType::Unknown || cooked.contentHash == 0 ||
		!compilers.IsSealed())
	{
		result.error = "Gameplay compiled asset header is invalid";
		return result;
	}
	const VansGameplayAssetCompilerRegistry::Compiler* compiler =
		compilers.Resolve(cooked.assetType);
	const bool compiled = compiler && (*compiler)(cooked, result.asset.data, result.diagnostics);
	if (!compiled)
		result.error = HasErrors(result.diagnostics)
			? "Gameplay asset failed typed compilation"
			: "Gameplay asset type has no runtime compiler";
	return result;
}
}
