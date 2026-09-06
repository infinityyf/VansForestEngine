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
	VansEntityHandle owner;
	const VansCompiledActionDefinition* definition = nullptr;
	const VansActionContext* context = nullptr;
	VansActionVariableStore* variables = nullptr;
	VansActionTaskSet* tasks = nullptr;
	VansActionResourceLedger* resources = nullptr;
	VansActionResourceLedger* hostResources = nullptr;
	VansActionResourceLedger* worldResources = nullptr;
	const VansActionServiceRegistry* services = nullptr;
	const std::vector<VansActionEvent>* events = nullptr;
	std::function<bool(VansActionEvent, std::string&)> emitSignal;
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

// A Driver may own the primary Action executor or run as a sidecar beside it.
// Sidecars are module-owned and receive the same lifecycle without introducing
// a dependency from GAF Core to the module that implements them.
class IVansActionSidecarDriver
{
public:
	virtual ~IVansActionSidecarDriver() = default;
	virtual bool Start(VansActionExecutionContext& context, std::string& error) = 0;
	virtual bool Tick(VansActionExecutionContext& context, std::string& error) = 0;
	virtual void Finish(VansActionExecutionContext& context, VansActionEndReason reason) = 0;
	virtual std::string_view StableName() const = 0;
};

class VansActionDriverRegistry
{
public:
	using Factory = std::function<std::unique_ptr<IVansActionSidecarDriver>(
		const VansCompiledActionRecord&)>;

	bool RegisterExecutorOwned(std::string typeId, std::string& error);
	bool RegisterSidecar(std::string typeId, Factory factory, std::string& error);
	bool Seal(std::string& error);
	std::unique_ptr<IVansActionSidecarDriver> Create(
		const VansCompiledActionRecord& record, std::string& error) const;
	bool Contains(std::string_view typeId) const;
	bool IsSealed() const { return m_Sealed; }

private:
	struct Entry { Factory factory; };
	bool m_Sealed = false;
	std::unordered_map<std::string, Entry> m_Entries;
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
