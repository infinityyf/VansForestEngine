#pragma once

#include "../AssetCore/VansAssetDatabase.h"
#include "VansActionExecution.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansActionGraphNodeKind : std::uint8_t
{
	Pure,
	Command,
	Latent,
	State,
	Flow,
	Transaction,
	Bridge,
	SubAction
};

enum class VansActionGraphNodeStatus : std::uint8_t
{
	NotStarted,
	Running,
	Waiting,
	Succeeded,
	Failed,
	Cancelled
};

enum class VansActionGraphPropertyKind : std::uint8_t
{
	Bool,
	Int,
	Float,
	String,
	AssetReference,
	StringArray,
	Payload
};

struct VansActionGraphPinDescriptor
{
	std::string name;
	std::string dataType = "Flow";
	bool input = false;
	bool multiple = false;
};

struct VansActionGraphPropertyDescriptor
{
	std::string name;
	std::string displayName;
	VansActionGraphPropertyKind kind = VansActionGraphPropertyKind::String;
	VansSerializedValue defaultValue;
	bool required = false;
	bool hasMinimum = false;
	bool hasMaximum = false;
	double minimum = 0.0;
	double maximum = 0.0;
	std::vector<VansAssetType> allowedAssetTypes;
};

struct VansActionGraphNodeDescriptor
{
	VansActionGraphNodeTypeId type;
	std::string stableName;
	std::string displayName;
	std::string category;
	VansActionGraphNodeKind kind = VansActionGraphNodeKind::Pure;
	bool predictable = false;
	bool authorityOnly = false;
	std::vector<VansActionGraphPinDescriptor> pins;
	std::vector<VansActionGraphPropertyDescriptor> properties;
};

const std::vector<VansActionGraphNodeDescriptor>& VansBuiltInActionGraphNodeDescriptors();

struct VansActionGraphNodeResult
{
	VansActionGraphNodeStatus status = VansActionGraphNodeStatus::Succeeded;
	std::string output = "Success";
	VansActionError error = VansActionError::None;
	std::string message;
};

enum class VansActionGraphRollbackPlan : std::uint8_t
{
	None,
	Automatic,
	Compensate
};

struct VansCompiledActionGraphNode
{
	std::string guid;
	VansActionGraphNodeTypeId type;
	VansActionGraphNodeKind kind = VansActionGraphNodeKind::Pure;
	VansSerializedValue properties = VansSerializedValue::Object({});
	bool predictable = false;
	VansActionGraphRollbackPlan rollbackPlan = VansActionGraphRollbackPlan::None;
};

struct VansCompiledActionGraphEdge
{
	std::uint32_t from = 0;
	std::string output = "Success";
	std::uint32_t to = 0;
	std::int32_t order = 0;
};

struct VansCompiledActionGraph
{
	std::string name;
	std::uint32_t version = 1;
	std::uint64_t contentHash = 0;
	std::uint32_t entryNode = 0;
	std::vector<VansCompiledActionGraphNode> nodes;
	std::vector<VansCompiledActionGraphEdge> edges;
};

class IVansActionGraphNodeHandler
{
public:
	virtual ~IVansActionGraphNodeHandler() = default;
	virtual VansActionGraphNodeTypeId TypeId() const = 0;
	virtual std::string_view StableName() const = 0;
	virtual bool SupportsPrediction() const = 0;
	virtual bool PreservesStateAcrossActivations() const { return false; }
	virtual VansActionGraphNodeResult Start(
		VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node,
		VansSerializedValue& state) const = 0;
	virtual VansActionGraphNodeResult Tick(
		VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node,
		VansSerializedValue& state) const = 0;
	virtual void Cancel(
		VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node,
		VansSerializedValue& state) const = 0;
};

VansActionGraphNodeResult VansExecuteActionServiceGraphCommand(
	VansActionExecutionContext& context,
	const VansCompiledActionGraphNode& node,
	std::string defaultService,
	std::string defaultCommand,
	const VansSerializedValue* payloadOverride = nullptr);

class VansActionGraphNodeRegistry
{
public:
	bool Register(std::shared_ptr<const IVansActionGraphNodeHandler> handler, std::string& error);
	bool Seal(std::string& error);
	std::shared_ptr<const IVansActionGraphNodeHandler> Resolve(VansActionGraphNodeTypeId type) const;
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::unordered_map<VansActionGraphNodeTypeId, std::shared_ptr<const IVansActionGraphNodeHandler>> m_Handlers;
};

class VansActionGraphRuntime
{
public:
	VansGameplayDiagnostics Initialize(
		std::shared_ptr<const VansCompiledActionGraph> graph,
		const VansActionGraphNodeRegistry* handlers,
		std::size_t maximumTransitionsPerTick = 1024);
	VansActionExecutorResult Start(VansActionExecutionContext& context);
	VansActionExecutorResult Tick(VansActionExecutionContext& context);
	void Cancel(VansActionExecutionContext& context);
	VansActionGraphNodeStatus NodeState(std::uint32_t node) const;
	VansActionExecutorDebugView DebugView() const;
	bool IsRunning() const { return m_Started && !m_Terminal; }

private:
	struct RuntimeNode
	{
		VansActionGraphNodeStatus status = VansActionGraphNodeStatus::NotStarted;
		VansSerializedValue state = VansSerializedValue::Object({});
	};

	VansActionExecutorResult Advance(VansActionExecutionContext& context);
	bool QueueOutgoing(std::uint32_t node, std::string_view output);
	void CancelConfiguredNodes(VansActionExecutionContext& context, std::uint32_t node);
	bool HasPendingWork() const;

	std::shared_ptr<const VansCompiledActionGraph> m_Graph;
	const VansActionGraphNodeRegistry* m_Handlers = nullptr;
	std::vector<RuntimeNode> m_Nodes;
	std::vector<std::vector<std::uint32_t>> m_CancelNodes;
	std::vector<std::uint32_t> m_Ready;
	bool m_Started = false;
	bool m_Terminal = false;
	bool m_Failed = false;
	VansActionError m_Error = VansActionError::None;
	std::string m_Message;
	std::size_t m_MaximumTransitionsPerTick = 1024;
};

bool VansRegisterBuiltInActionGraphNodes(
	VansActionGraphNodeRegistry& registry,
	std::string& error);
}
