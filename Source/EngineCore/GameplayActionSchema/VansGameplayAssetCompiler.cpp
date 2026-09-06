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

VansActionConcurrencyPolicy ConcurrencyPolicy(std::string_view value)
{
	if (value == "Reject" || value == "RejectNew") return VansActionConcurrencyPolicy::RejectNew;
	if (value == "CancelExisting") return VansActionConcurrencyPolicy::CancelExisting;
	if (value == "Queue" || value == "QueueNew") return VansActionConcurrencyPolicy::QueueNew;
	return VansActionConcurrencyPolicy::Allow;
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

	for (const VansCompiledActionRecord& policy : action->program.policies)
	{
		const VansSerializedValue& inputs = policy.inputs;
		if (policy.type == "Core.Policy.Concurrency")
		{
			action->concurrencyGroup = StableId<VansActionConcurrencyGroupIdTag>(
				ReadSerializedStringField(inputs, "group"));
			action->concurrencyPolicy = ConcurrencyPolicy(
				ReadSerializedStringField(inputs, "mode", "Allow"));
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
	if (const VansSerializedValue* extensions = At(root, "/extensions");
		extensions && extensions->kind == VansSerializedValue::Kind::Array)
	{
		for (const VansSerializedValue& extension : extensions->arrayItems)
		{
			if (extension.kind != VansSerializedValue::Kind::Object) continue;
			const std::string type = ReadSerializedStringField(extension, "type");
			const VansSerializedValue* inputs = FindObjectField(extension, "inputs");
			if (!inputs || inputs->kind != VansSerializedValue::Kind::Object) continue;
			if (type == "Gameplay.Effect.AttributeModifier")
			{
				VansEffectModifier modifier;
				modifier.attribute = StableId<VansAttributeIdTag>(
					ReadSerializedStringField(*inputs, "attribute"));
				modifier.operation = ModifierOperation(
					ReadSerializedStringField(*inputs, "operation", "Additive"));
				modifier.magnitude = FindObjectField(*inputs, "magnitude")
					? ReadSerializedNumber(*FindObjectField(*inputs, "magnitude")) : 0.0;
				modifier.priority = static_cast<std::int32_t>(
					ReadSerializedIntField(*inputs, "priority", 0));
				modifier.magnitudeSource = EffectMagnitudeSource(
					ReadSerializedStringField(*inputs, "magnitudeSource", "Fixed"));
				modifier.setByCallerField = StableId<VansActionFieldIdTag>(
					ReadSerializedStringField(*inputs, "setByCaller"));
				modifier.capturedAttribute = StableId<VansAttributeIdTag>(
					ReadSerializedStringField(*inputs, "capturedAttribute"));
				modifier.capturePolicy = ReadSerializedStringField(*inputs, "capture", "Snapshot") == "Dynamic"
					? VansEffectCapturePolicy::Dynamic : VansEffectCapturePolicy::Snapshot;
				modifier.contextPayloadPath = ReadSerializedStringField(*inputs, "contextPath");
				modifier.targetDataMetric = EffectTargetDataMetric(
					ReadSerializedStringField(*inputs, "targetMetric", "Count"));
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
			std::vector<std::string>* cueReferences = nullptr;
			const std::string phase = ReadSerializedStringField(*inputs, "phase", "Execute");
			if (phase == "Execute")
			{
				cueIds = &effect->executeCues;
				cueReferences = &effect->executeCueReferences;
			}
			else if (phase == "Persistent")
			{
				cueIds = &effect->persistentCues;
				cueReferences = &effect->persistentCueReferences;
			}
			else if (phase == "Periodic")
			{
				cueIds = &effect->periodicCues;
				cueReferences = &effect->periodicCueReferences;
			}
			else if (phase == "Remove")
			{
				cueIds = &effect->removeCues;
				cueReferences = &effect->removeCueReferences;
			}
			const VansSerializedValue* assets = FindObjectField(*inputs, "assets");
			if (!cueIds || !cueReferences || !assets || assets->kind != VansSerializedValue::Kind::Array)
				continue;
			for (const VansSerializedValue& item : assets->arrayItems)
			{
				const std::string reference = ReferenceString(&item);
				if (reference.empty()) continue;
				cueReferences->push_back(reference);
				cueIds->push_back(StableId<VansCueIdTag>(reference));
			}
		}
	}
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
	if (const VansSerializedValue* bindings = At(root, "/bindings");
		bindings && bindings->kind == VansSerializedValue::Kind::Array)
		for (const VansSerializedValue& binding : bindings->arrayItems)
		{
			if (binding.kind != VansSerializedValue::Kind::Object ||
				ReadSerializedStringField(binding, "type") != "Gameplay.Cue.Invoke") continue;
			const VansSerializedValue* inputs = FindObjectField(binding, "inputs");
			if (!inputs || inputs->kind != VansSerializedValue::Kind::Object) continue;
			VansGameplayCueAdapterMapping mapping;
			mapping.serviceName = ReadSerializedStringField(*inputs, "capability");
			mapping.service = StableId<VansActionServiceIdTag>(mapping.serviceName);
			mapping.commandName = ReadSerializedStringField(*inputs, "invoke");
			mapping.command = StableId<VansActionFieldIdTag>(mapping.commandName);
			mapping.updateCommandName = ReadSerializedStringField(*inputs, "update");
			mapping.updateCommand = StableId<VansActionFieldIdTag>(mapping.updateCommandName);
			mapping.removeCommandName = ReadSerializedStringField(*inputs, "release");
			mapping.removeCommand = StableId<VansActionFieldIdTag>(mapping.removeCommandName);
			mapping.asset = ReferenceString(FindObjectField(*inputs, "asset"));
			if (const VansSerializedValue* parameters = FindObjectField(*inputs, "parameters"))
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
			step.stableName = ReadSerializedStringField(item, "type");
			step.handler = StableId<VansActionGraphNodeTypeIdTag>(step.stableName);
			if (const VansSerializedValue* inputs = FindObjectField(item, "inputs"))
				step.inputs = *inputs;
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
	if (value == "Bridge") return VansActionGraphNodeKind::Bridge;
	if (value == "SubAction") return VansActionGraphNodeKind::SubAction;
	return VansActionGraphNodeKind::Pure;
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
			node.kind = GraphNodeKind(ReadSerializedStringField(item, "kind", "Pure"));
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
			[](const auto& cooked, auto& output, auto&)
			{ return CompilePayload(cooked, output); }, error) &&
		registry.Register(VansAssetType::ActionGraph, "Core.Asset.ActionGraph",
			[](const auto& cooked, auto& output, auto& diagnostics)
			{ return CompileGraph(cooked, output, diagnostics); }, error);
}

bool VansRegisterGameplayPrimitiveAssetCompilers(
	VansGameplayAssetCompilerRegistry& registry,
	std::string& error)
{
	return registry.Register(VansAssetType::GameplayEffect, "Gameplay.Asset.Effect",
		[](const auto& cooked, auto& output, auto&)
		{ return CompileEffect(cooked, output); }, error) &&
		registry.Register(VansAssetType::GameplayCue, "Gameplay.Asset.Cue",
			[](const auto& cooked, auto& output, auto&)
			{ return CompileCue(cooked, output); }, error) &&
		registry.Register(VansAssetType::AttributeSet, "Gameplay.Asset.AttributeSet",
			[](const auto& cooked, auto& output, auto& diagnostics)
			{ return CompileAttributeSet(cooked, output, diagnostics); }, error) &&
		registry.Register(VansAssetType::TargetingPolicy, "Gameplay.Asset.TargetingPolicy",
			[](const auto& cooked, auto& output, auto&)
			{ return CompileTargeting(cooked, output); }, error) &&
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
