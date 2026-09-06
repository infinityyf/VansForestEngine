#include "VansActionExecution.h"
#include "VansActionExecutionGraph.h"

#include <unordered_set>

namespace Vans
{
namespace
{
class VansImmediateActionExecutor final : public IVansActionExecutor
{
public:
	VansActionExecutorResult Start(VansActionExecutionContext&) override
	{
		return { VansActionExecutorStatus::Succeeded };
	}
	VansActionExecutorResult Tick(VansActionExecutionContext&) override
	{
		return { VansActionExecutorStatus::Succeeded };
	}
	bool RequestCancel(VansActionExecutionContext&, VansActionCancelReason) override { return true; }
	void OnEvent(VansActionExecutionContext&, const VansActionEvent&) override {}
	void Finish(VansActionExecutionContext&, VansActionEndReason) override {}
};

class VansGraphActionExecutor final : public IVansActionExecutor
{
public:
	VansGraphActionExecutor(
		std::shared_ptr<const VansCompiledActionGraph> graph,
		const VansActionGraphNodeRegistry* handlers,
		std::size_t maximumTransitionsPerTick)
		: m_Graph(std::move(graph)), m_Handlers(handlers),
		  m_MaximumTransitionsPerTick(maximumTransitionsPerTick) {}

	VansActionExecutorResult Start(VansActionExecutionContext& context) override
	{
		const VansGameplayDiagnostics diagnostics = m_Runtime.Initialize(
			m_Graph, m_Handlers, m_MaximumTransitionsPerTick);
		for (const VansGameplayDiagnostic& diagnostic : diagnostics)
		{
			if (diagnostic.severity == VansGameplayDiagnosticSeverity::Error ||
				diagnostic.severity == VansGameplayDiagnosticSeverity::Fatal)
				return { VansActionExecutorStatus::Failed, VansActionError::Execution,
					diagnostic.code + ": " + diagnostic.message };
		}
		return m_Runtime.Start(context);
	}

	VansActionExecutorResult Tick(VansActionExecutionContext& context) override
	{
		const std::vector<VansActionEvent>* previousEvents = context.events;
		context.events = &m_Events;
		const VansActionExecutorResult result = m_Runtime.Tick(context);
		context.events = previousEvents;
		m_Events.clear();
		return result;
	}

	bool RequestCancel(VansActionExecutionContext& context, VansActionCancelReason) override
	{
		m_Runtime.Cancel(context);
		return true;
	}

	void OnEvent(VansActionExecutionContext&, const VansActionEvent& event) override
	{
		m_Events.push_back(event);
	}

	void Finish(VansActionExecutionContext& context, VansActionEndReason reason) override
	{
		if (reason != VansActionEndReason::Completed && m_Runtime.IsRunning())
			m_Runtime.Cancel(context);
	}

	VansActionExecutorDebugView DebugView() const override { return m_Runtime.DebugView(); }

private:
	std::shared_ptr<const VansCompiledActionGraph> m_Graph;
	const VansActionGraphNodeRegistry* m_Handlers = nullptr;
	std::size_t m_MaximumTransitionsPerTick = 1024;
	VansActionGraphRuntime m_Runtime;
	std::vector<VansActionEvent> m_Events;
};
}

bool VansActionDriverRegistry::RegisterExecutorOwned(
	std::string typeId,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Action Driver registry is sealed";
		return false;
	}
	if (typeId.empty() || !m_Entries.emplace(std::move(typeId), Entry{}).second)
	{
		error = "duplicate or invalid Action Driver TypeId";
		return false;
	}
	return true;
}

bool VansActionDriverRegistry::RegisterSidecar(
	std::string typeId,
	Factory factory,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Action Driver registry is sealed";
		return false;
	}
	if (typeId.empty() || !factory ||
		!m_Entries.emplace(std::move(typeId), Entry{ std::move(factory) }).second)
	{
		error = "duplicate or invalid Action Driver TypeId";
		return false;
	}
	return true;
}

bool VansActionDriverRegistry::Seal(std::string& error)
{
	if (m_Entries.empty())
	{
		error = "Action Driver registry is empty";
		return false;
	}
	m_Sealed = true;
	return true;
}

std::unique_ptr<IVansActionSidecarDriver> VansActionDriverRegistry::Create(
	const VansCompiledActionRecord& record,
	std::string& error) const
{
	if (!m_Sealed)
	{
		error = "Action Driver registry is not sealed";
		return {};
	}
	const auto found = m_Entries.find(record.type);
	if (found == m_Entries.end())
	{
		error = "Action Driver implementation is not registered: " + record.type;
		return {};
	}
	if (!found->second.factory) return {};
	auto driver = found->second.factory(record);
	if (!driver) error = "Action Driver factory returned null: " + record.type;
	return driver;
}

bool VansActionDriverRegistry::Contains(std::string_view typeId) const
{
	return m_Entries.find(std::string(typeId)) != m_Entries.end();
}

bool VansActionVariableStore::Initialize(
	const std::vector<VansActionVariableDefinition>& definitions,
	std::string& error)
{
	m_Values.clear();
	for (const VansActionVariableDefinition& definition : definitions)
	{
		if (!definition.id || !m_Values.emplace(definition.id, definition.defaultValue).second)
		{
			error = "Action variable definition is invalid or duplicated";
			m_Values.clear();
			return false;
		}
	}
	return true;
}

bool VansActionVariableStore::Set(VansActionFieldId field, VansSerializedValue value)
{
	const auto found = m_Values.find(field);
	if (found == m_Values.end()) return false;
	found->second = std::move(value);
	return true;
}

const VansSerializedValue* VansActionVariableStore::Get(VansActionFieldId field) const
{
	const auto found = m_Values.find(field);
	return found == m_Values.end() ? nullptr : &found->second;
}

VansSerializedValue* VansActionVariableStore::GetMutable(VansActionFieldId field)
{
	const auto found = m_Values.find(field);
	return found == m_Values.end() ? nullptr : &found->second;
}

bool VansActionExecutorRegistry::Register(
	VansActionExecutorId id,
	std::string stableName,
	Factory factory,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Action Executor registry is sealed";
		return false;
	}
	if (!id || stableName.empty() || !factory)
	{
		error = "Action Executor descriptor is invalid";
		return false;
	}
	if (!m_Factories.emplace(id, Entry{ std::move(stableName), std::move(factory) }).second)
	{
		error = "duplicate Action Executor";
		return false;
	}
	return true;
}

bool VansActionExecutorRegistry::Seal(std::string& error)
{
	if (m_Factories.empty())
	{
		error = "Action Executor registry is empty";
		return false;
	}
	std::unordered_set<std::string> names;
	for (const auto& entry : m_Factories)
	{
		if (!names.insert(entry.second.stableName).second)
		{
			error = "duplicate Action Executor stable name";
			return false;
		}
	}
	m_Sealed = true;
	return true;
}

std::unique_ptr<IVansActionExecutor> VansActionExecutorRegistry::Create(
	VansActionExecutorId id,
	const VansCompiledActionDefinition& definition,
	std::string& error) const
{
	if (!m_Sealed)
	{
		error = "Action Executor registry is not sealed";
		return nullptr;
	}
	const auto found = m_Factories.find(id);
	if (found == m_Factories.end())
	{
		error = "Action Executor is not registered";
		return nullptr;
	}
	std::unique_ptr<IVansActionExecutor> executor = found->second.factory(definition);
	if (!executor) error = "Action Executor factory returned null";
	return executor;
}

bool VansActionExecutorRegistry::Contains(VansActionExecutorId id) const
{
	return m_Factories.find(id) != m_Factories.end();
}

bool VansRegisterBuiltInActionExecutors(
	VansActionExecutorRegistry& registry,
	const VansActionGraphNodeRegistry* graphNodes,
	std::string& error,
	std::size_t maximumGraphTransitionsPerTick)
{
	if (!graphNodes || !graphNodes->IsSealed() || maximumGraphTransitionsPerTick == 0)
	{
		error = "Built-in Action Executors require a sealed Graph node registry";
		return false;
	}
	const auto immediateId = VansMakeStableId<VansActionExecutorIdTag>(ActionExecutorNames::Immediate);
	const auto graphId = VansMakeStableId<VansActionExecutorIdTag>(ActionExecutorNames::Graph);
	return registry.Register(immediateId, std::string(ActionExecutorNames::Immediate),
		[](const VansCompiledActionDefinition&) { return std::make_unique<VansImmediateActionExecutor>(); }, error) &&
		registry.Register(graphId, std::string(ActionExecutorNames::Graph),
			[graphNodes, maximumGraphTransitionsPerTick](
				const VansCompiledActionDefinition& definition) -> std::unique_ptr<IVansActionExecutor>
			{
				if (!definition.executionGraph) return nullptr;
				return std::make_unique<VansGraphActionExecutor>(definition.executionGraph,
					graphNodes, maximumGraphTransitionsPerTick);
			}, error);
}
}
