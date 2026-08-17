#pragma once

#include "../GameplayActionCore/VansActionDefinition.h"
#include "../GameplayActionCore/VansActionResourceLedger.h"
#include "../GameplayActionCore/VansActionServices.h"
#include "VansActionTask.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansActionExecutorStatus : std::uint8_t
{
	Running,
	Waiting,
	Succeeded,
	Failed
};

struct VansActionExecutorResult
{
	VansActionExecutorStatus status = VansActionExecutorStatus::Running;
	VansActionError error = VansActionError::None;
	std::string message;
};

enum class VansActionCancelReason : std::uint8_t
{
	User,
	InputReleased,
	Interrupted,
	Concurrency,
	GrantRevoked,
	OwnerDestroyed,
	PredictionRejected,
	System
};

enum class VansActionEndReason : std::uint8_t
{
	Completed,
	Failed,
	Cancelled,
	Interrupted,
	TimedOut,
	CommitFailed,
	OwnerDestroyed
};

struct VansActionEvent
{
	VansActionFieldId type;
	std::string stableName;
	VansEntityHandle source;
	VansEntityHandle target;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

class VansActionVariableStore
{
public:
	bool Initialize(const std::vector<VansActionVariableDefinition>& definitions, std::string& error);
	bool Set(VansActionFieldId field, VansSerializedValue value);
	const VansSerializedValue* Get(VansActionFieldId field) const;
	VansSerializedValue* GetMutable(VansActionFieldId field);
	const std::unordered_map<VansActionFieldId, VansSerializedValue>& Values() const { return m_Values; }

private:
	std::unordered_map<VansActionFieldId, VansSerializedValue> m_Values;
};

struct VansActionExecutionContext
{
	VansActionHandle action;
	const VansCompiledActionDefinition* definition = nullptr;
	const VansActionContext* context = nullptr;
	VansActionVariableStore* variables = nullptr;
	VansActionTaskSet* tasks = nullptr;
	VansActionResourceLedger* resources = nullptr;
	const VansActionServiceRegistry* services = nullptr;
	const std::vector<VansActionEvent>* events = nullptr;
	double deltaSeconds = 0.0;
};

struct VansActionExecutorDebugView
{
	std::string executor;
	std::vector<std::string> activeNodes;
	std::vector<std::string> waitingNodes;
};

class IVansActionExecutor
{
public:
	virtual ~IVansActionExecutor() = default;
	virtual VansActionExecutorResult Start(VansActionExecutionContext& context) = 0;
	virtual VansActionExecutorResult Tick(VansActionExecutionContext& context) = 0;
	virtual bool RequestCancel(VansActionExecutionContext& context, VansActionCancelReason reason) = 0;
	virtual void OnEvent(VansActionExecutionContext& context, const VansActionEvent& event) = 0;
	virtual void Finish(VansActionExecutionContext& context, VansActionEndReason reason) = 0;
	virtual VansActionExecutorDebugView DebugView() const { return {}; }
};

class VansActionExecutorRegistry
{
public:
	using Factory = std::function<std::unique_ptr<IVansActionExecutor>(const VansCompiledActionDefinition&)>;

	bool Register(VansActionExecutorId id, std::string stableName, Factory factory, std::string& error);
	bool Seal(std::string& error);
	std::unique_ptr<IVansActionExecutor> Create(
		VansActionExecutorId id,
		const VansCompiledActionDefinition& definition,
		std::string& error) const;
	bool Contains(VansActionExecutorId id) const;
	bool IsSealed() const { return m_Sealed; }

private:
	struct Entry
	{
		std::string stableName;
		Factory factory;
	};
	bool m_Sealed = false;
	std::unordered_map<VansActionExecutorId, Entry> m_Factories;
};

namespace ActionExecutorNames
{
	inline constexpr std::string_view Immediate = "Action.Executor.Immediate";
	inline constexpr std::string_view Graph = "Action.Executor.Graph";
}

class VansActionGraphNodeRegistry;
bool VansRegisterBuiltInActionExecutors(
	VansActionExecutorRegistry& registry,
	const VansActionGraphNodeRegistry* graphNodes,
	std::string& error,
	std::size_t maximumGraphTransitionsPerTick = 1024);
}
