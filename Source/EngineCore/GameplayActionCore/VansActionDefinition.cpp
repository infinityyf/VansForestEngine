#include "VansActionDefinition.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vans
{
bool VansActionDefinitionRegistry::Register(
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
	if (!m_Definitions.emplace(definition->id, std::move(definition)).second)
	{
		error = "Action Definition stable ID already exists";
		return false;
	}
	return true;
}

bool VansActionDefinitionRegistry::Replace(
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
		if (diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
			diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal)
		{
			error = diagnostic.code + ": " + diagnostic.message;
			return false;
		}
	const auto found = m_Definitions.find(definition->id);
	if (found == m_Definitions.end())
	{
		error = "Action Definition stable ID is not registered";
		return false;
	}
	found->second = std::move(definition);
	return true;
}

std::shared_ptr<const VansCompiledActionDefinition> VansActionDefinitionRegistry::Resolve(
	VansActionId id) const
{
	const auto found = m_Definitions.find(id);
	return found == m_Definitions.end() ? nullptr : found->second;
}

std::vector<std::shared_ptr<const VansCompiledActionDefinition>>
VansActionDefinitionRegistry::Definitions() const
{
	std::vector<std::shared_ptr<const VansCompiledActionDefinition>> result;
	result.reserve(m_Definitions.size());
	for (const auto& [id, definition] : m_Definitions)
	{
		(void)id;
		result.push_back(definition);
	}
	std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
	{
		return left->name < right->name;
	});
	return result;
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
	if (definition.contentHash == 0)
		error("GAF-ACTION-CONTENT", "Action content hash must be non-zero", "identity.contentHash");
	if (!definition.executor && definition.executionGraphAsset.empty())
		error("GAF-ACTION-EXECUTION", "Action needs a registered execution Driver",
			"phases.execute.drivers");
	if (definition.executor == VansMakeStableId<VansActionExecutorIdTag>("Action.Executor.Graph") &&
		definition.executionGraphAsset.empty())
		error("GAF-ACTION-GRAPH", "Graph Executor requires an Action Graph asset",
			"phases.execute.drivers.Core.Driver.Graph.inputs.graph");
	if (definition.concurrencyPolicy != VansActionConcurrencyPolicy::Allow && !definition.concurrencyGroup)
		error("GAF-ACTION-CONCURRENCY-GROUP", "Restricted concurrency requires a stable group",
			"policies.Core.Policy.Concurrency.inputs.group");
	if (definition.concurrencyLimit == 0)
		error("GAF-ACTION-CONCURRENCY-LIMIT", "Concurrency limit must be positive",
			"policies.Core.Policy.Concurrency.inputs.limit");
	if (!std::isfinite(definition.concurrencyQueueTimeoutSeconds) ||
		definition.concurrencyQueueTimeoutSeconds < 0.0)
		error("GAF-ACTION-CONCURRENCY-TIMEOUT", "Concurrency queue timeout is invalid",
			"policies.Core.Policy.Concurrency.inputs.queueTimeout");
	const auto validateRecords = [&](const std::vector<VansCompiledActionRecord>& records,
		const std::string& field)
	{
		for (std::size_t index = 0; index < records.size(); ++index)
			if (records[index].type.empty() ||
				records[index].inputs.kind != VansSerializedValue::Kind::Object)
				error("GAF-ACTION-RECORD", "Compiled Action record is invalid",
					field + "[" + std::to_string(index) + "]");
	};
	validateRecords(definition.program.policies, "policies");
	validateRecords(definition.program.activate.guards, "activate.guards");
	validateRecords(definition.program.activate.operations, "activate.operations");
	validateRecords(definition.program.activate.drivers, "activate.drivers");
	validateRecords(definition.program.commit.guards, "commit.guards");
	validateRecords(definition.program.commit.operations, "commit.operations");
	validateRecords(definition.program.commit.drivers, "commit.drivers");
	validateRecords(definition.program.execute.guards, "execute.guards");
	validateRecords(definition.program.execute.operations, "execute.operations");
	validateRecords(definition.program.execute.drivers, "execute.drivers");
	validateRecords(definition.program.finish.guards, "finish.guards");
	validateRecords(definition.program.finish.operations, "finish.operations");
	validateRecords(definition.program.finish.drivers, "finish.drivers");
	validateRecords(definition.program.cancel.guards, "cancel.guards");
	validateRecords(definition.program.cancel.operations, "cancel.operations");
	validateRecords(definition.program.cancel.drivers, "cancel.drivers");
	validateRecords(definition.program.transitions, "transitions");
	validateRecords(definition.program.extensions, "extensions");
	std::unordered_set<VansActionFieldId> variables;
	for (const VansActionVariableDefinition& variable : definition.variables)
	{
		if (!variable.id || variable.name.empty() || !variables.insert(variable.id).second)
			error("GAF-ACTION-VARIABLE", "Action variable identity is invalid or duplicated", "variables");
	}
	std::unordered_set<std::string> capabilities;
	for (const std::string& capability : definition.program.capabilities)
		if (capability.empty() || !capabilities.insert(capability).second)
			error("GAF-ACTION-CAPABILITY", "Action capability is invalid or duplicated",
				"dependencies.capabilities");
	std::unordered_set<std::string> modules;
	for (const std::string& module : definition.program.modules)
		if (module.empty() || !modules.insert(module).second)
			error("GAF-ACTION-MODULE", "Action module is invalid or duplicated",
				"dependencies.modules");
	return diagnostics;
}
}
