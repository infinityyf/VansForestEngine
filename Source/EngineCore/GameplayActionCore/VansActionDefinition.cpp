#include "VansActionDefinition.h"

#include <cmath>
#include <unordered_set>

namespace Vans
{
bool VansActionDefinitionRegistry::RegisterRevision(
	std::shared_ptr<const VansCompiledActionDefinition> definition,
	std::string& error)
{
	if (!definition)
	{
		error = "Action Definition is null";
		return false;
	}
	const VansGameplayDiagnostics diagnostics = Validate(*definition);
	for (const VansGameplayDiagnostic& diagnostic : diagnostics)
	{
		if (diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
			diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal)
		{
			error = diagnostic.code + ": " + diagnostic.message;
			return false;
		}
	}
	RevisionMap& revisions = m_Definitions[definition->id];
	if (!revisions.emplace(definition->definitionVersion, std::move(definition)).second)
	{
		error = "Action Definition revision already exists";
		return false;
	}
	return true;
}

std::shared_ptr<const VansCompiledActionDefinition> VansActionDefinitionRegistry::ResolveLatest(
	VansActionId id) const
{
	const auto found = m_Definitions.find(id);
	if (found == m_Definitions.end() || found->second.empty()) return nullptr;
	auto latest = found->second.begin();
	for (auto revision = found->second.begin(); revision != found->second.end(); ++revision)
		if (revision->first > latest->first) latest = revision;
	return latest->second;
}

std::shared_ptr<const VansCompiledActionDefinition> VansActionDefinitionRegistry::ResolveRevision(
	VansActionId id,
	std::uint32_t definitionVersion) const
{
	const auto found = m_Definitions.find(id);
	if (found == m_Definitions.end()) return nullptr;
	const auto revision = found->second.find(definitionVersion);
	return revision == found->second.end() ? nullptr : revision->second;
}

std::uint32_t VansActionDefinitionRegistry::LatestRevision(VansActionId id) const
{
	const auto latest = ResolveLatest(id);
	return latest ? latest->definitionVersion : 0;
}

VansGameplayDiagnostics VansActionDefinitionRegistry::Validate(
	const VansCompiledActionDefinition& definition)
{
	VansGameplayDiagnostics diagnostics;
	auto error = [&](std::string code, std::string message, std::string field)
	{
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, std::move(code),
			std::move(message), {}, std::move(field) });
	};
	if (!definition.id || definition.name.empty())
		error("GAF-ACTION-IDENTITY", "Action identity is invalid", "identity");
	if (definition.definitionVersion == 0 || definition.schemaVersion == 0 || definition.contentHash == 0)
		error("GAF-ACTION-VERSION", "Action version and content hash must be non-zero", "identity.version");
	if (!definition.executor && definition.executionGraphAsset.empty())
		error("GAF-ACTION-EXECUTION", "Action needs an Executor or ExecutionGraph", "execution");
	if (definition.executor == VansMakeStableId<VansActionExecutorIdTag>("Action.Executor.Graph") &&
		definition.executionGraphAsset.empty())
		error("GAF-ACTION-GRAPH", "Graph Executor requires an Action Graph asset",
			"execution.graph");
	if (definition.concurrencyPolicy != VansActionConcurrencyPolicy::Allow && !definition.concurrencyGroup)
		error("GAF-ACTION-CONCURRENCY-GROUP", "Restricted concurrency requires a stable group",
			"commit.concurrency.group");
	if (definition.concurrencyLimit == 0)
		error("GAF-ACTION-CONCURRENCY-LIMIT", "Concurrency limit must be positive",
			"commit.concurrency.limit");
	if (!std::isfinite(definition.concurrencyQueueTimeoutSeconds) ||
		definition.concurrencyQueueTimeoutSeconds < 0.0)
		error("GAF-ACTION-CONCURRENCY-TIMEOUT", "Concurrency queue timeout is invalid",
			"commit.concurrency.queueTimeout");
	std::unordered_set<VansGameplayTagId> cooldownTags;
	for (std::size_t index = 0; index < definition.cooldowns.size(); ++index)
	{
		const VansActionCooldownDefinition& cooldown = definition.cooldowns[index];
		if (!std::isfinite(cooldown.durationSeconds) || cooldown.durationSeconds <= 0.0)
			error("GAF-ACTION-COOLDOWN", "Action cooldown duration must be positive",
				"commit.cooldowns[" + std::to_string(index) + "]");
		if (cooldown.cooldownTag && !cooldownTags.insert(cooldown.cooldownTag).second)
			error("GAF-ACTION-COOLDOWN-DUPLICATE", "Action contains duplicate cooldown Tags",
				"commit.cooldowns[" + std::to_string(index) + "]");
	}
	std::unordered_set<VansAttributeId> costAttributes;
	for (std::size_t index = 0; index < definition.commitRequirements.size(); ++index)
	{
		const VansActionRequirementDefinition& requirement = definition.commitRequirements[index];
		const std::string field = "commit.requirements[" + std::to_string(index) + "]";
		if (requirement.kind == VansActionRequirementKind::Attribute &&
			(!requirement.attribute || !std::isfinite(requirement.value)))
			error("GAF-ACTION-REQUIREMENT", "Attribute requirement is invalid", field);
		if ((requirement.kind == VansActionRequirementKind::PrimaryTarget ||
			requirement.kind == VansActionRequirementKind::TargetData) &&
			requirement.minimumTargets == 0)
			error("GAF-ACTION-REQUIREMENT", "Target requirement count must be positive", field);
		if (requirement.kind == VansActionRequirementKind::Service && !requirement.service)
			error("GAF-ACTION-REQUIREMENT", "Service requirement is invalid", field);
	}
	for (const VansActionCostDefinition& cost : definition.costs)
	{
		if (!std::isfinite(cost.amount) || cost.amount < 0.0 ||
			(cost.kind == VansActionCostKind::Attribute && !cost.attribute) ||
			(cost.kind != VansActionCostKind::Attribute && cost.resource.empty()) ||
			cost.payload.kind != VansSerializedValue::Kind::Object)
			error("GAF-ACTION-COST", "Action contains an invalid cost", "commit.costs");
		if (cost.kind == VansActionCostKind::Attribute &&
			!costAttributes.insert(cost.attribute).second)
			error("GAF-ACTION-COST-DUPLICATE", "Action contains duplicate Attribute costs", "commit.costs");
	}
	std::unordered_set<VansActionFieldId> variables;
	for (const VansActionVariableDefinition& variable : definition.variables)
	{
		if (!variable.id || variable.name.empty() || !variables.insert(variable.id).second)
			error("GAF-ACTION-VARIABLE", "Action variable identity is invalid or duplicated", "execution.variables");
	}
	std::unordered_set<VansActionServiceId> services;
	for (VansActionServiceId service : definition.requiredServices)
		if (!service || !services.insert(service).second)
			error("GAF-ACTION-SERVICE", "Action service dependency is invalid or duplicated", "dependencies.services");
	std::unordered_set<std::string> transitionNames;
	for (std::size_t index = 0; index < definition.transitionRules.size(); ++index)
	{
		const VansActionTransitionRule& rule = definition.transitionRules[index];
		const std::string field = "transitions.rules[" + std::to_string(index) + "]";
		if (rule.name.empty() || !transitionNames.insert(rule.name).second || !rule.targetAction)
			error("GAF-ACTION-TRANSITION-ID", "Transition identity or target is invalid or duplicated", field);
		if ((rule.trigger == VansActionTransitionTrigger::Event && !rule.event) ||
			(rule.trigger == VansActionTransitionTrigger::Input && rule.inputBinding.empty()))
			error("GAF-ACTION-TRANSITION-TRIGGER", "Transition trigger is incomplete", field);
		if (!std::isfinite(rule.minimumTimeSeconds) || !std::isfinite(rule.maximumTimeSeconds) ||
			rule.minimumTimeSeconds < 0.0 ||
			(rule.maximumTimeSeconds >= 0.0 &&
				rule.maximumTimeSeconds < rule.minimumTimeSeconds))
			error("GAF-ACTION-TRANSITION-WINDOW", "Transition time window is invalid", field);
		if (rule.contextPatch.kind != VansSerializedValue::Kind::Object)
			error("GAF-ACTION-TRANSITION-PATCH", "Transition context patch must be an object", field);
	}
	if (!std::isfinite(definition.inputBuffer.durationSeconds) ||
		definition.inputBuffer.durationSeconds < 0.0 || definition.inputBuffer.maximumEntries == 0 ||
		(definition.inputBuffer.enabled && definition.inputBuffer.durationSeconds <= 0.0))
		error("GAF-ACTION-INPUT-BUFFER", "Action input buffer policy is invalid",
			"transitions.inputBuffer");
	if (definition.failureFallback.action &&
		definition.failureFallback.contextPatch.kind != VansSerializedValue::Kind::Object)
		error("GAF-ACTION-FAILURE-FALLBACK", "Failure fallback context patch must be an object",
			"transitions.failureFallback");
	return diagnostics;
}
}
