#include "VansActionExecutionGraph.h"
#include "VansActionBinding.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vans
{
const std::vector<VansActionGraphNodeDescriptor>& VansBuiltInActionGraphNodeDescriptors()
{
	static const std::vector<VansActionGraphNodeDescriptor> descriptors = []
	{
		std::vector<VansActionGraphNodeDescriptor> result;
		const auto pin = [](std::string name, bool input, bool multiple = false,
			std::string dataType = "Flow")
		{
			return VansActionGraphPinDescriptor{
				std::move(name), std::move(dataType), input, multiple };
		};
		const auto property = [](std::string name, std::string displayName,
			VansActionGraphPropertyKind kind, VansSerializedValue value,
			double minimum = 0.0, double maximum = 0.0, bool hasRange = false)
		{
			VansActionGraphPropertyDescriptor descriptor;
			descriptor.name = std::move(name);
			descriptor.displayName = std::move(displayName);
			descriptor.kind = kind;
			descriptor.defaultValue = std::move(value);
			descriptor.hasMinimum = hasRange;
			descriptor.hasMaximum = hasRange;
			descriptor.minimum = minimum;
			descriptor.maximum = maximum;
			return descriptor;
		};
		const auto add = [&](const char* stableName, const char* displayName,
			const char* category, VansActionGraphNodeKind kind, std::vector<VansActionGraphPinDescriptor> pins,
			std::vector<VansActionGraphPropertyDescriptor> properties = {})
		{
			result.push_back({
				VansMakeStableId<VansActionGraphNodeTypeIdTag>(stableName), stableName,
				displayName, category, kind, std::move(pins), std::move(properties) });
		};

		add("Action.Graph.Sequence", "Sequence", "Flow", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("Return", true, true),
				pin("Step.*", false), pin("Success", false) },
			{ property("steps", "Step Outputs", VansActionGraphPropertyKind::StringArray,
				VansSerializedValue::Array({})),
			  property("count", "Step Count", VansActionGraphPropertyKind::Int,
				VansSerializedValue::Int(0), 0.0, 65535.0, true) });
		add("Action.Graph.Parallel", "Parallel", "Flow", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("BranchCompleted", true, true),
				pin("Branch", false, true), pin("Success", false) },
			{ property("branches", "Branch Count", VansActionGraphPropertyKind::Int,
				VansSerializedValue::Int(1), 1.0, 65535.0, true) });
		add("Action.Graph.Race", "Race", "Flow", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("BranchCompleted", true, true),
				pin("Branch", false, true), pin("Success", false) },
			{ property("cancelNodes", "Losing Nodes", VansActionGraphPropertyKind::StringArray,
				VansSerializedValue::Array({})) });
		add("Action.Graph.Branch", "Branch", "Flow", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("True", false), pin("False", false) },
			{ property("condition", "Condition", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(false)),
			  property("variable", "Condition Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")) });
		add("Action.Graph.Switch", "Switch", "Flow", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("*", false, true), pin("Default", false) },
			{ property("value", "Value", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Null()),
			  property("variable", "Value Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("defaultOutput", "Default Output", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("Default")) });
		add("Action.Graph.Loop", "Loop", "Flow", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("Return", true), pin("Body", false),
				pin("Completed", false), pin("Failure", false) },
			{ property("condition", "Condition", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(true)),
			  property("variable", "Condition Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("maximumIterations", "Maximum Iterations", VansActionGraphPropertyKind::Int,
				VansSerializedValue::Int(1), 1.0, 65535.0, true) });
		add("Action.Graph.Repeat", "Repeat", "Flow", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("Return", true), pin("Body", false),
				pin("Success", false), pin("Failure", false) },
			{ property("count", "Count", VansActionGraphPropertyKind::Int,
				VansSerializedValue::Int(1), 0.0, 65535.0, true) });
		add("Action.Graph.Channel", "Channel", "Flow", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("*", false, true), pin("Default", false) },
			{ property("channel", "Channel", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("Default")),
			  property("variable", "Channel Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")) });
		add("Action.Graph.Gate", "Gate", "Flow", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("Open", false), pin("Closed", false),
				pin("Failure", false) },
			{ property("condition", "Open", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(false)),
			  property("variable", "Open Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("waitUntilOpen", "Wait Until Open", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(false)) });
		add("Action.Graph.Wait", "Wait", "Latent", VansActionGraphNodeKind::Latent,  { pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("seconds", "Seconds", VansActionGraphPropertyKind::Float,
				VansSerializedValue::Float(0.0), 0.0, 86400.0, true) });
		add("Action.Graph.Timeout", "Timeout", "Latent", VansActionGraphNodeKind::Latent,  { pin("In", true), pin("Timeout", false), pin("Failure", false) },
			{ property("seconds", "Seconds", VansActionGraphPropertyKind::Float,
				VansSerializedValue::Float(1.0), 0.0, 86400.0, true),
			  property("fail", "Fail On Timeout", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(false)) });
		add("Core.Graph.Invoke", "Invoke", "Operation", VansActionGraphNodeKind::Command,  { pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("capability", "Capability", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("operation", "Operation", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("inputs", "Inputs", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})),
			  property("outputs", "Outputs", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})) });
		add("Core.Graph.ReadBinding", "Read Binding", "Data", VansActionGraphNodeKind::Pure,
			{ pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("input", "Input", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})),
			  property("output", "Output", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})) });
		add("Core.Graph.WaitSignal", "Wait Signal", "Latent", VansActionGraphNodeKind::Latent,
			{ pin("In", true), pin("Success", false), pin("Timeout", false), pin("Failure", false) },
			{ property("signal", "Signal", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("timeoutSeconds", "Timeout", VansActionGraphPropertyKind::Float,
				VansSerializedValue::Float(0.0), 0.0, 86400.0, true),
			  property("payloadOutput", "Payload Output", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Null()) });
		add("Core.Graph.AwaitTask", "Await Task", "Latent", VansActionGraphNodeKind::Latent,
			{ pin("In", true), pin("Success", false), pin("Failure", false), pin("Timeout", false) },
			{ property("task", "Task", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})) });
		add("Core.Graph.ReleaseResource", "Release Resource", "Resource",
			VansActionGraphNodeKind::Command,
			{ pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("resource", "Resource", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})) });
		add("Core.Graph.TransferResource", "Transfer Resource", "Resource",
			VansActionGraphNodeKind::Command,
			{ pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("resource", "Resource", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})),
			  property("destination", "Destination", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("Host")),
			  property("output", "Output", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Null()) });
		add("Core.Graph.EmitSignal", "Emit Signal", "Signal", VansActionGraphNodeKind::Command,
			{ pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("signal", "Signal", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("payload", "Payload", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})) });
		add("Action.Graph.Complete", "Complete", "Terminal", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("Success", false) });
		add("Action.Graph.Fail", "Fail", "Terminal", VansActionGraphNodeKind::Flow,  { pin("In", true), pin("Failure", false) },
			{ property("message", "Message", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("Action Graph failed")) });
		add("Action.Graph.SubAction", "Sub Action", "SubAction", VansActionGraphNodeKind::SubAction,  { pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("payload", "Activation", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})) });
		add("Action.Graph.Transition", "Transition", "SubAction", VansActionGraphNodeKind::SubAction,  { pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("payload", "Transition", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})) });
		add("Action.Graph.Try", "Try", "Flow", VansActionGraphNodeKind::Flow,
			{ pin("In", true), pin("Try", false), pin("Failure", false) });
		return result;
	}();
	return descriptors;
}

namespace
{
double NumberProperty(
	const VansSerializedValue& properties,
	const char* name,
	double fallback = 0.0)
{
	const VansSerializedValue* value = FindObjectField(properties, name);
	return value ? ReadSerializedNumber(*value, fallback) : fallback;
}

std::int64_t IntegerProperty(
	const VansSerializedValue& properties,
	const char* name,
	std::int64_t fallback = 0)
{
	const VansSerializedValue* value = FindObjectField(properties, name);
	return value ? ReadSerializedInt(*value, fallback) : fallback;
}

std::int64_t StateInteger(
	const VansSerializedValue& state,
	const char* name,
	std::int64_t fallback = 0)
{
	const VansSerializedValue* value = FindObjectField(state, name);
	return value ? ReadSerializedInt(*value, fallback) : fallback;
}

void SetStateInteger(VansSerializedValue& state, const char* name, std::int64_t value)
{
	SetSerializedObjectField(state, name, VansSerializedValue::Int(value));
}

const VansSerializedValue* ResolveNodeValue(
	VansActionExecutionContext& context,
	const VansCompiledActionGraphNode& node,
	const char* literalName,
	const char* variableName)
{
	const std::string variable = ReadSerializedStringField(node.properties, variableName);
	if (!variable.empty())
	{
		return context.variables
			? context.variables->Get(VansMakeStableId<VansActionFieldIdTag>(variable)) : nullptr;
	}
	return FindObjectField(node.properties, literalName);
}

bool ResolveNodeCondition(
	VansActionExecutionContext& context,
	const VansCompiledActionGraphNode& node,
	bool fallback,
	bool& available)
{
	const VansSerializedValue* value = ResolveNodeValue(
		context, node, "condition", "variable");
	available = value != nullptr || ReadSerializedStringField(node.properties, "variable").empty();
	return value ? ReadSerializedBool(*value, fallback) : fallback;
}

std::string NodeOutputValue(const VansSerializedValue* value, std::string fallback)
{
	if (!value) return fallback;
	switch (value->kind)
	{
	case VansSerializedValue::Kind::String: return value->stringValue;
	case VansSerializedValue::Kind::Bool: return value->boolValue ? "True" : "False";
	case VansSerializedValue::Kind::Int: return std::to_string(value->intValue);
	case VansSerializedValue::Kind::Float: return std::to_string(value->floatValue);
	default: return fallback;
	}
}

class VansSequenceGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Sequence");
	}
	std::string_view StableName() const override { return "Action.Graph.Sequence"; }
	bool PreservesStateAcrossActivations() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const VansSerializedValue* steps = FindObjectField(node.properties, "steps");
		const std::int64_t count = steps && steps->kind == VansSerializedValue::Kind::Array
			? static_cast<std::int64_t>(steps->arrayItems.size())
			: IntegerProperty(node.properties, "count", 0);
		if (count < 0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Sequence step count is invalid" };
		const std::int64_t index = StateInteger(state, "index");
		if (index >= count)
			return { VansActionGraphNodeStatus::Succeeded, "Success" };
		SetStateInteger(state, "index", index + 1);
		if (steps && steps->kind == VansSerializedValue::Kind::Array &&
			static_cast<std::size_t>(index) < steps->arrayItems.size())
		{
			const std::string output = ReadSerializedString(
				steps->arrayItems[static_cast<std::size_t>(index)]);
			if (!output.empty()) return { VansActionGraphNodeStatus::Succeeded, output };
		}
		return { VansActionGraphNodeStatus::Succeeded,
			"Step." + std::to_string(index) };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansRepeatGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Repeat");
	}
	std::string_view StableName() const override { return "Action.Graph.Repeat"; }
	bool PreservesStateAcrossActivations() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const std::int64_t count = IntegerProperty(node.properties, "count", 1);
		if (count < 0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Repeat count is invalid" };
		const std::int64_t iteration = StateInteger(state, "iteration");
		if (iteration >= count)
			return { VansActionGraphNodeStatus::Succeeded, "Success" };
		SetStateInteger(state, "iteration", iteration + 1);
		return { VansActionGraphNodeStatus::Succeeded, "Body" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansLoopGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Loop");
	}
	std::string_view StableName() const override { return "Action.Graph.Loop"; }
	bool PreservesStateAcrossActivations() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		bool available = false;
		const bool condition = ResolveNodeCondition(context, node, true, available);
		if (!available)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Loop variable is unavailable" };
		if (!condition) return { VansActionGraphNodeStatus::Succeeded, "Completed" };
		const std::int64_t maximum = IntegerProperty(node.properties, "maximumIterations", 1);
		const std::int64_t iteration = StateInteger(state, "iteration");
		if (maximum <= 0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Loop maximumIterations must be positive" };
		if (iteration >= maximum)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::Budget, "Loop exceeded maximumIterations" };
		SetStateInteger(state, "iteration", iteration + 1);
		return { VansActionGraphNodeStatus::Succeeded, "Body" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansParallelGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Parallel");
	}
	std::string_view StableName() const override { return "Action.Graph.Parallel"; }
	bool PreservesStateAcrossActivations() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const std::int64_t branches = IntegerProperty(node.properties, "branches", 1);
		if (branches <= 0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Parallel branch count must be positive" };
		if (StateInteger(state, "started") == 0)
		{
			SetStateInteger(state, "started", 1);
			SetStateInteger(state, "completed", 0);
			return { VansActionGraphNodeStatus::Succeeded, "Branch" };
		}
		const std::int64_t completed = StateInteger(state, "completed") + 1;
		SetStateInteger(state, "completed", completed);
		return { completed >= branches ? VansActionGraphNodeStatus::Succeeded :
			VansActionGraphNodeStatus::Waiting, completed >= branches ? "Success" : "Pending" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext&,
		const VansCompiledActionGraphNode&, VansSerializedValue&) const override
	{
		return { VansActionGraphNodeStatus::Waiting, "Pending" };
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansRaceGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Race");
	}
	std::string_view StableName() const override { return "Action.Graph.Race"; }
	bool PreservesStateAcrossActivations() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode&, VansSerializedValue& state) const override
	{
		const std::int64_t phase = StateInteger(state, "phase");
		if (phase == 0)
		{
			SetStateInteger(state, "phase", 1);
			return { VansActionGraphNodeStatus::Succeeded, "Branch" };
		}
		if (phase == 1)
		{
			SetStateInteger(state, "phase", 2);
			return { VansActionGraphNodeStatus::Succeeded, "Success" };
		}
		return { VansActionGraphNodeStatus::Succeeded, "Ignored" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext&,
		const VansCompiledActionGraphNode&, VansSerializedValue&) const override
	{
		return { VansActionGraphNodeStatus::Waiting, "Pending" };
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansCompleteGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Complete");
	}
	std::string_view StableName() const override { return "Action.Graph.Complete"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode&, VansSerializedValue&) const override
	{
		return { VansActionGraphNodeStatus::Succeeded, "Success" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansFailGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Fail");
	}
	std::string_view StableName() const override { return "Action.Graph.Fail"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		return { VansActionGraphNodeStatus::Failed, "Failure", VansActionError::Execution,
			ReadSerializedStringField(node.properties, "message", "Action Graph failed") };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansWaitGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Wait");
	}
	std::string_view StableName() const override { return "Action.Graph.Wait"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const double seconds = NumberProperty(node.properties, "seconds", 0.0);
		if (!std::isfinite(seconds) || seconds < 0.0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Wait node duration is invalid" };
		state = VansSerializedValue::Float(0.0);
		return { seconds <= 0.0 ? VansActionGraphNodeStatus::Succeeded :
			VansActionGraphNodeStatus::Waiting, "Success" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const double seconds = NumberProperty(node.properties, "seconds", 0.0);
		state.kind = VansSerializedValue::Kind::Float;
		state.floatValue += std::max(0.0, context.deltaSeconds);
		return { state.floatValue + 1e-12 >= seconds ? VansActionGraphNodeStatus::Succeeded :
			VansActionGraphNodeStatus::Waiting, "Success" };
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansBranchGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Branch");
	}
	std::string_view StableName() const override { return "Action.Graph.Branch"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		bool condition = ReadSerializedBoolField(node.properties, "condition", false);
		const std::string variable = ReadSerializedStringField(node.properties, "variable");
		if (!variable.empty())
		{
			const VansSerializedValue* value = context.variables ? context.variables->Get(
				VansMakeStableId<VansActionFieldIdTag>(variable)) : nullptr;
			if (!value)
				return { VansActionGraphNodeStatus::Failed, "Failure",
					VansActionError::InvalidDefinition, "Branch variable is unavailable" };
			condition = ReadSerializedBool(*value, false);
		}
		return { VansActionGraphNodeStatus::Succeeded, condition ? "True" : "False" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansSwitchGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Switch");
	}
	std::string_view StableName() const override { return "Action.Graph.Switch"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		const VansSerializedValue* value = ResolveNodeValue(context, node, "value", "variable");
		if (!value && !ReadSerializedStringField(node.properties, "variable").empty())
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Switch variable is unavailable" };
		return { VansActionGraphNodeStatus::Succeeded,
			NodeOutputValue(value, ReadSerializedStringField(node.properties, "defaultOutput", "Default")) };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansChannelGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Channel");
	}
	std::string_view StableName() const override { return "Action.Graph.Channel"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		const VansSerializedValue* value = ResolveNodeValue(context, node, "channel", "variable");
		if (!value && !ReadSerializedStringField(node.properties, "variable").empty())
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Channel variable is unavailable" };
		return { VansActionGraphNodeStatus::Succeeded,
			NodeOutputValue(value, "Default") };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansGateGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Gate");
	}
	std::string_view StableName() const override { return "Action.Graph.Gate"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		bool available = false;
		const bool open = ResolveNodeCondition(context, node, false, available);
		if (!available)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Gate variable is unavailable" };
		if (!open && ReadSerializedBoolField(node.properties, "waitUntilOpen", false))
			return { VansActionGraphNodeStatus::Waiting, "Closed" };
		return { VansActionGraphNodeStatus::Succeeded, open ? "Open" : "Closed" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansTimeoutGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Timeout");
	}
	std::string_view StableName() const override { return "Action.Graph.Timeout"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const double seconds = NumberProperty(node.properties, "seconds", 0.0);
		if (!std::isfinite(seconds) || seconds < 0.0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Timeout duration is invalid" };
		state = VansSerializedValue::Float(0.0);
		if (seconds > 0.0) return { VansActionGraphNodeStatus::Waiting, "Pending" };
		return Finish(node);
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		state.kind = VansSerializedValue::Kind::Float;
		state.floatValue += (std::max)(0.0, context.deltaSeconds);
		if (state.floatValue + 1e-12 < NumberProperty(node.properties, "seconds", 0.0))
			return { VansActionGraphNodeStatus::Waiting, "Pending" };
		return Finish(node);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
private:
	static VansActionGraphNodeResult Finish(const VansCompiledActionGraphNode& node)
	{
		if (ReadSerializedBoolField(node.properties, "fail", false))
			return { VansActionGraphNodeStatus::Failed, "Timeout",
				VansActionError::Timeout, "Action Graph Timeout elapsed" };
		return { VansActionGraphNodeStatus::Succeeded, "Timeout" };
	}
};

class VansTryGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Try");
	}
	std::string_view StableName() const override { return "Action.Graph.Try"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode&, VansSerializedValue&) const override
	{
		return { VansActionGraphNodeStatus::Succeeded, "Try" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

bool DecodeGenerationHandle(const VansSerializedValue& value, VansGenerationHandle& handle)
{
	if (value.kind != VansSerializedValue::Kind::Object) return false;
	const std::int64_t index = ReadSerializedIntField(value, "index", -1);
	const std::int64_t generation = ReadSerializedIntField(value, "generation", 0);
	if (index < 0 || generation <= 0) return false;
	handle = { static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(generation) };
	return true;
}

VansSerializedValue EncodeGenerationHandle(VansGenerationHandle handle)
{
	return VansSerializedValue::Object({
		{ "index", VansSerializedValue::Int(handle.index) },
		{ "generation", VansSerializedValue::Int(handle.generation) }
	});
}

bool ResolveBindingProperty(
	VansActionExecutionContext& context,
	const VansCompiledActionGraphNode& node,
	const char* property,
	VansSerializedValue& value,
	std::string& error)
{
	const VansSerializedValue* encoded = FindObjectField(node.properties, property);
	VansActionInputBinding binding;
	return encoded && VansDecodeActionInputBinding(*encoded, binding, error) &&
		VansResolveActionInputBinding(binding, context, value, error);
}

bool WriteBindingProperty(
	VansActionExecutionContext& context,
	const VansCompiledActionGraphNode& node,
	const char* property,
	VansSerializedValue value,
	bool required,
	std::string& error)
{
	const VansSerializedValue* encoded = FindObjectField(node.properties, property);
	if (!encoded || encoded->IsNull())
	{
		if (!required) return true;
		error = "Action Graph output binding is missing";
		return false;
	}
	VansActionOutputBinding binding;
	return VansDecodeActionOutputBinding(*encoded, binding, error) &&
		VansWriteActionOutputBinding(binding, std::move(value), context, error);
}

class VansReadBindingGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Core.Graph.ReadBinding");
	}
	std::string_view StableName() const override { return "Core.Graph.ReadBinding"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		VansSerializedValue value;
		std::string error;
		if (!ResolveBindingProperty(context, node, "input", value, error) ||
			!WriteBindingProperty(context, node, "output", std::move(value), true, error))
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, std::move(error) };
		return { VansActionGraphNodeStatus::Succeeded, "Success" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
		{ return Start(context, node, state); }
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansWaitSignalGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Core.Graph.WaitSignal");
	}
	std::string_view StableName() const override { return "Core.Graph.WaitSignal"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		state = VansSerializedValue::Float(0.0);
		return Tick(context, node, state);
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const std::string signal = ReadSerializedStringField(node.properties, "signal");
		if (signal.empty())
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "WaitSignal signal is empty" };
		if (context.events)
			for (const VansActionEvent& event : *context.events)
				if (event.stableName == signal || event.type ==
					VansMakeStableId<VansActionFieldIdTag>(signal))
				{
					std::string error;
					if (!WriteBindingProperty(context, node, "payloadOutput",
						event.payload, false, error))
						return { VansActionGraphNodeStatus::Failed, "Failure",
							VansActionError::InvalidDefinition, std::move(error) };
					return { VansActionGraphNodeStatus::Succeeded, "Success" };
				}
		state.kind = VansSerializedValue::Kind::Float;
		state.floatValue += (std::max)(0.0, context.deltaSeconds);
		const double timeout = NumberProperty(node.properties, "timeoutSeconds", 0.0);
		if (!std::isfinite(timeout) || timeout < 0.0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "WaitSignal timeout is invalid" };
		if (timeout > 0.0 && state.floatValue >= timeout)
			return { VansActionGraphNodeStatus::Succeeded, "Timeout" };
		return { VansActionGraphNodeStatus::Waiting, "Pending" };
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansAwaitTaskGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Core.Graph.AwaitTask");
	}
	std::string_view StableName() const override { return "Core.Graph.AwaitTask"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
		{ return Tick(context, node, state); }
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		if (!context.tasks)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::Internal, "AwaitTask has no Action TaskSet" };
		VansSerializedValue value;
		std::string error;
		VansGenerationHandle encodedHandle;
		if (!ResolveBindingProperty(context, node, "task", value, error) ||
			!DecodeGenerationHandle(value, encodedHandle))
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, error.empty()
					? "AwaitTask binding is not a Task handle" : std::move(error) };
		const VansActionTaskHandle task{ encodedHandle };
		const VansActionTaskState taskState = context.tasks->State(task);
		if (taskState == VansActionTaskState::Waiting)
			return { VansActionGraphNodeStatus::Waiting, "Pending" };
		VansActionTaskState consumed{};
		if (!context.tasks->Consume(task, consumed, error))
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::Internal, std::move(error) };
		if (consumed == VansActionTaskState::Completed)
			return { VansActionGraphNodeStatus::Succeeded, "Success" };
		if (consumed == VansActionTaskState::TimedOut)
			return { VansActionGraphNodeStatus::Succeeded, "Timeout" };
		return { VansActionGraphNodeStatus::Failed, "Failure",
			consumed == VansActionTaskState::Cancelled
				? VansActionError::Cancelled : VansActionError::Execution,
			"Awaited Action Task did not complete successfully" };
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansReleaseResourceGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Core.Graph.ReleaseResource");
	}
	std::string_view StableName() const override { return "Core.Graph.ReleaseResource"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		VansSerializedValue value;
		VansGenerationHandle handle;
		std::string error;
		if (!context.resources || !ResolveBindingProperty(context, node, "resource", value, error) ||
			!DecodeGenerationHandle(value, handle) ||
			!context.resources->Release({ handle }, error))
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::Internal, error.empty()
					? "ReleaseResource binding is invalid" : std::move(error) };
		return { VansActionGraphNodeStatus::Succeeded, "Success" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
		{ return Start(context, node, state); }
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansTransferResourceGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Core.Graph.TransferResource");
	}
	std::string_view StableName() const override { return "Core.Graph.TransferResource"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		VansSerializedValue value;
		VansGenerationHandle handle;
		std::string error;
		const std::string destinationName =
			ReadSerializedStringField(node.properties, "destination", "Host");
		VansActionResourceLedger* destination = destinationName == "Host"
			? context.hostResources : destinationName == "World"
				? context.worldResources : nullptr;
		VansActionResourceHandle destinationHandle;
		if (!context.resources || !destination ||
			!ResolveBindingProperty(context, node, "resource", value, error) ||
			!DecodeGenerationHandle(value, handle) ||
			!context.resources->Transfer({ handle }, *destination, destinationHandle, error) ||
			!WriteBindingProperty(context, node, "output",
				EncodeGenerationHandle(destinationHandle.value), false, error))
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::Internal, error.empty()
					? "TransferResource destination or binding is invalid" : std::move(error) };
		return { VansActionGraphNodeStatus::Succeeded, "Success" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
		{ return Start(context, node, state); }
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansEmitSignalGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Core.Graph.EmitSignal");
	}
	std::string_view StableName() const override { return "Core.Graph.EmitSignal"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		const std::string signal = ReadSerializedStringField(node.properties, "signal");
		VansSerializedValue payload;
		std::string error;
		if (signal.empty() || !context.context || !context.emitSignal ||
			!ResolveBindingProperty(context, node, "payload", payload, error))
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, error.empty()
					? "EmitSignal configuration is incomplete" : std::move(error) };
		VansActionEvent event;
		event.type = VansMakeStableId<VansActionFieldIdTag>(signal);
		event.stableName = signal;
		event.source = context.context->Entity(VansActionContextSlots::Owner);
		event.target = context.context->Entity(VansActionContextSlots::PrimaryTarget);
		event.payload = std::move(payload);
		if (!context.emitSignal(std::move(event), error))
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::Execution, std::move(error) };
		return { VansActionGraphNodeStatus::Succeeded, "Success" };
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
		{ return Start(context, node, state); }
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

VansActionGraphNodeResult ExecuteGraphCommand(
	VansActionExecutionContext& context,
	const VansCompiledActionGraphNode& node,
	std::string defaultService,
	std::string defaultCommand,
	const VansSerializedValue* payloadOverride = nullptr)
{
	const bool genericInvocation = defaultService.empty() && defaultCommand.empty();
	const std::string serviceName = genericInvocation
		? ReadSerializedStringField(node.properties, "capability") : std::move(defaultService);
	const std::string commandName = genericInvocation
		? ReadSerializedStringField(node.properties, "operation") : std::move(defaultCommand);
	if (!context.services || !context.context || serviceName.empty() || commandName.empty())
		return { VansActionGraphNodeStatus::Failed, "Failure", VansActionError::Dependency,
			"Invoke node capability, operation or Action context is missing" };
	VansActionCommand command;
	command.service = VansMakeStableId<VansActionServiceIdTag>(serviceName);
	command.command = VansMakeStableId<VansActionFieldIdTag>(commandName);
	command.stableName = commandName;
	command.action = context.action;
	command.context = *context.context;
	if (genericInvocation)
	{
		const VansSerializedValue* inputs = FindObjectField(node.properties, "inputs");
		if (!inputs || inputs->kind != VansSerializedValue::Kind::Object)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Invoke inputs must be an object" };
		command.payload = VansSerializedValue::Object({});
		for (const auto& [name, encodedBinding] : inputs->objectFields)
		{
			VansActionInputBinding binding;
			std::string bindingError;
			VansSerializedValue value;
			if (!VansDecodeActionInputBinding(encodedBinding, binding, bindingError) ||
				!VansResolveActionInputBinding(binding, context, value, bindingError))
				return { VansActionGraphNodeStatus::Failed, "Failure",
					VansActionError::InvalidDefinition, std::move(bindingError) };
			SetSerializedObjectField(command.payload, name, std::move(value));
		}
	}
	else if (payloadOverride)
		command.payload = *payloadOverride;
	else command.payload = node.properties;
	const VansActionCommandSchema* commandSchema =
		context.services->ResolveCommandSchema(command.service, command.command);
	VansGenerationHandle releasedResource;
	if (commandSchema && (commandSchema->resourcePolicy ==
		VansActionCommandResourcePolicy::Update || commandSchema->resourcePolicy ==
		VansActionCommandResourcePolicy::Release))
	{
		if (const VansSerializedValue* encoded = FindObjectField(command.payload, "resource"))
		{
			VansGenerationHandle ledgerHandle;
			VansActionServiceId resourceService;
			VansGenerationHandle externalResource;
			if (!context.resources || !DecodeGenerationHandle(*encoded, ledgerHandle) ||
				!context.resources->ResolveExternal(
					{ ledgerHandle }, resourceService, externalResource) ||
				resourceService != command.service)
				return { VansActionGraphNodeStatus::Failed, "Failure",
					VansActionError::Internal,
					"Invoke resource binding is stale or belongs to another capability" };
			SetSerializedObjectField(command.payload, "resource",
				EncodeGenerationHandle(externalResource));
			if (commandSchema->resourcePolicy == VansActionCommandResourcePolicy::Release)
				releasedResource = externalResource;
		}
	}
	const VansActionCommandResult executed = context.services->Execute(command);
	if (!executed)
		return { VansActionGraphNodeStatus::Failed, "Failure", executed.error, executed.message };
	if (releasedResource && context.resources)
		context.resources->ForgetExternalResource(command.service, releasedResource);
	VansActionResourceHandle registeredResource;
	if (executed.resource)
	{
		const std::shared_ptr<IVansActionService> service = context.services->Resolve(command.service);
		if (!service || !context.resources)
			return { VansActionGraphNodeStatus::Failed, "Failure", VansActionError::Internal,
				"Command resource cannot be tracked" };
		VansActionResourceEntry resource;
		resource.type = "ActionService";
		resource.debugName = serviceName + "." + commandName;
		resource.service = command.service;
		resource.externalResource = executed.resource;
		resource.release = [service, handle = executed.resource]
		{
			std::string ignored;
			return service->Release(handle, ignored);
		};
		std::string resourceError;
		registeredResource = context.resources->Register(std::move(resource), resourceError);
		if (!registeredResource)
		{
			std::string ignored;
			service->Release(executed.resource, ignored);
			return { VansActionGraphNodeStatus::Failed, "Failure", VansActionError::Execution,
				resourceError };
		}
	}
	if (genericInvocation)
	{
		const VansSerializedValue* outputs = FindObjectField(node.properties, "outputs");
		if (!outputs || outputs->kind != VansSerializedValue::Kind::Object)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InvalidDefinition, "Invoke outputs must be an object" };
		for (const auto& [name, encodedBinding] : outputs->objectFields)
		{
			VansActionOutputBinding binding;
			std::string bindingError;
			VansSerializedValue value;
			if (name == "result") value = executed.payload;
			else if (name == "resource")
				value = EncodeGenerationHandle(registeredResource.value);
			else
			{
				const VansSerializedValue* field = FindObjectField(executed.payload, name);
				if (!field)
					return { VansActionGraphNodeStatus::Failed, "Failure",
						VansActionError::InvalidDefinition,
						"Invoke output is unavailable: " + name };
				value = *field;
			}
			if (!VansDecodeActionOutputBinding(encodedBinding, binding, bindingError) ||
				!VansWriteActionOutputBinding(binding, std::move(value), context, bindingError))
				return { VansActionGraphNodeStatus::Failed, "Failure",
					VansActionError::InvalidDefinition, std::move(bindingError) };
		}
	}
	return { VansActionGraphNodeStatus::Succeeded, "Success" };
}

class VansCommandGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Core.Graph.Invoke");
	}
	std::string_view StableName() const override { return "Core.Graph.Invoke"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		return ExecuteGraphCommand(context, node, {}, {});
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansSubActionGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.SubAction");
	}
	std::string_view StableName() const override { return "Action.Graph.SubAction"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		return ExecuteGraphCommand(context, node, "Service.Action", "ActivateSubAction");
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};

class VansTransitionGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Transition");
	}
	std::string_view StableName() const override { return "Action.Graph.Transition"; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		return ExecuteGraphCommand(context, node, "Service.Action", "Transition");
	}
	VansActionGraphNodeResult Tick(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};
}

VansActionGraphNodeResult VansExecuteActionServiceGraphCommand(
	VansActionExecutionContext& context,
	const VansCompiledActionGraphNode& node,
	std::string defaultService,
	std::string defaultCommand,
	const VansSerializedValue* payloadOverride)
{
	return ExecuteGraphCommand(context, node, std::move(defaultService),
		std::move(defaultCommand), payloadOverride);
}

bool VansActionGraphNodeRegistry::Register(
	std::shared_ptr<const IVansActionGraphNodeHandler> handler,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Action Graph node registry is sealed";
		return false;
	}
	if (!handler || !handler->TypeId() || handler->StableName().empty())
	{
		error = "Action Graph node handler is invalid";
		return false;
	}
	if (!m_Handlers.emplace(handler->TypeId(), std::move(handler)).second)
	{
		error = "duplicate Action Graph node handler";
		return false;
	}
	return true;
}

bool VansActionGraphNodeRegistry::Seal(std::string& error)
{
	if (m_Handlers.empty())
	{
		error = "Action Graph node registry is empty";
		return false;
	}
	m_Sealed = true;
	return true;
}

std::shared_ptr<const IVansActionGraphNodeHandler> VansActionGraphNodeRegistry::Resolve(
	VansActionGraphNodeTypeId type) const
{
	const auto found = m_Handlers.find(type);
	return found == m_Handlers.end() ? nullptr : found->second;
}

VansGameplayDiagnostics VansActionGraphRuntime::Initialize(
	std::shared_ptr<const VansCompiledActionGraph> graph,
	const VansActionGraphNodeRegistry* handlers,
	std::size_t maximumTransitionsPerTick)
{
	VansGameplayDiagnostics diagnostics;
	auto addError = [&](std::string code, std::string message, std::string field)
	{
		diagnostics.push_back({ VansGameplayDiagnosticSeverity::Error, std::move(code),
			std::move(message), {}, std::move(field) });
	};
	if (!graph || !handlers || !handlers->IsSealed() || maximumTransitionsPerTick == 0)
	{
		addError("GAF-GRAPH-REGISTRY", "Graph, sealed node registry or transition budget is invalid", "graph");
		return diagnostics;
	}
	if (graph->contentHash == 0 || graph->nodes.empty() ||
		graph->entryNode >= graph->nodes.size())
	{
		addError("GAF-GRAPH-IDENTITY", "Graph identity, entry or node list is invalid", "graph");
		return diagnostics;
	}
	std::unordered_set<std::string> guids;
	std::unordered_map<std::string, std::uint32_t> nodesByGuid;
	for (std::size_t index = 0; index < graph->nodes.size(); ++index)
	{
		const VansCompiledActionGraphNode& node = graph->nodes[index];
		const auto handler = handlers->Resolve(node.type);
		if (node.guid.empty() || !node.type || !guids.insert(node.guid).second)
			addError("GAF-GRAPH-NODE-ID", "Graph node identity is invalid or duplicated",
				"nodes[" + std::to_string(index) + "]");
		else nodesByGuid.emplace(node.guid, static_cast<std::uint32_t>(index));
		if (!handler)
			addError("GAF-GRAPH-NODE-TYPE", "Graph node handler is not registered",
				"nodes[" + std::to_string(index) + "].type");
	}
	for (std::size_t index = 0; index < graph->edges.size(); ++index)
	{
		const VansCompiledActionGraphEdge& edge = graph->edges[index];
		if (edge.from >= graph->nodes.size() || edge.to >= graph->nodes.size() || edge.output.empty())
			addError("GAF-GRAPH-EDGE", "Graph edge is invalid",
				"edges[" + std::to_string(index) + "]");
	}
	m_CancelNodes.assign(graph->nodes.size(), {});
	for (std::size_t index = 0; index < graph->nodes.size(); ++index)
	{
		const VansSerializedValue* cancellationNodes =
			FindObjectField(graph->nodes[index].properties, "cancelNodes");
		if (!cancellationNodes) continue;
		if (cancellationNodes->kind != VansSerializedValue::Kind::Array)
		{
			addError("GAF-GRAPH-CANCEL-NODES", "cancelNodes must be an array",
				"nodes[" + std::to_string(index) + "].properties.cancelNodes");
			continue;
		}
		for (std::size_t target = 0; target < cancellationNodes->arrayItems.size(); ++target)
		{
			const std::string guid = ReadSerializedString(cancellationNodes->arrayItems[target]);
			const auto found = nodesByGuid.find(guid);
			if (found == nodesByGuid.end() || found->second == index)
				addError("GAF-GRAPH-CANCEL-NODE", "cancelNodes contains an invalid node GUID",
					"nodes[" + std::to_string(index) + "].properties.cancelNodes[" +
					std::to_string(target) + "]");
			else m_CancelNodes[index].push_back(found->second);
		}
		std::sort(m_CancelNodes[index].begin(), m_CancelNodes[index].end());
		m_CancelNodes[index].erase(std::unique(m_CancelNodes[index].begin(),
			m_CancelNodes[index].end()), m_CancelNodes[index].end());
	}
	if (!diagnostics.empty()) return diagnostics;
	std::vector<bool> reachable(graph->nodes.size(), false);
	std::vector<std::uint32_t> pending{ graph->entryNode };
	while (!pending.empty())
	{
		const std::uint32_t node = pending.back();
		pending.pop_back();
		if (reachable[node]) continue;
		reachable[node] = true;
		for (const VansCompiledActionGraphEdge& edge : graph->edges)
			if (edge.from == node && !reachable[edge.to]) pending.push_back(edge.to);
	}
	for (std::size_t index = 0; index < reachable.size(); ++index)
		if (!reachable[index]) diagnostics.push_back({ VansGameplayDiagnosticSeverity::Warning,
			"GAF-GRAPH-UNREACHABLE", "Graph node is unreachable", {},
			"nodes[" + std::to_string(index) + "]" });
	m_Graph = std::move(graph);
	m_Handlers = handlers;
	m_Nodes.assign(m_Graph->nodes.size(), RuntimeNode{});
	m_Ready.clear();
	m_Started = false;
	m_Terminal = false;
	m_Failed = false;
	m_Error = VansActionError::None;
	m_Message.clear();
	m_MaximumTransitionsPerTick = maximumTransitionsPerTick;
	return diagnostics;
}

VansActionExecutorResult VansActionGraphRuntime::Start(VansActionExecutionContext& context)
{
	if (!m_Graph || !m_Handlers || m_Started)
		return { VansActionExecutorStatus::Failed, VansActionError::Execution,
			"Action Graph is not initialized or already started" };
	m_Started = true;
	m_Ready.push_back(m_Graph->entryNode);
	return Advance(context);
}

VansActionExecutorResult VansActionGraphRuntime::Tick(VansActionExecutionContext& context)
{
	if (!m_Started || m_Terminal)
		return { m_Failed ? VansActionExecutorStatus::Failed : VansActionExecutorStatus::Succeeded,
			m_Failed ? m_Error : VansActionError::None, m_Message };
	return Advance(context);
}

void VansActionGraphRuntime::Cancel(VansActionExecutionContext& context)
{
	if (!m_Graph || !m_Handlers || m_Terminal) return;
	for (std::size_t index = m_Nodes.size(); index > 0; --index)
	{
		RuntimeNode& runtime = m_Nodes[index - 1];
		if (runtime.status != VansActionGraphNodeStatus::Running &&
			runtime.status != VansActionGraphNodeStatus::Waiting) continue;
		if (const auto handler = m_Handlers->Resolve(m_Graph->nodes[index - 1].type))
			handler->Cancel(context, m_Graph->nodes[index - 1], runtime.state);
		runtime.status = VansActionGraphNodeStatus::Cancelled;
	}
	m_Ready.clear();
	m_Terminal = true;
	m_Failed = true;
	m_Error = VansActionError::Execution;
	m_Message = "Action Graph cancelled";
}

VansActionGraphNodeStatus VansActionGraphRuntime::NodeState(std::uint32_t node) const
{
	return node < m_Nodes.size() ? m_Nodes[node].status : VansActionGraphNodeStatus::Failed;
}

VansActionExecutorDebugView VansActionGraphRuntime::DebugView() const
{
	VansActionExecutorDebugView result;
	result.executor = std::string(ActionExecutorNames::Graph);
	if (!m_Graph) return result;
	for (std::size_t index = 0; index < m_Nodes.size() && index < m_Graph->nodes.size(); ++index)
	{
		const auto state = m_Nodes[index].status;
		if (state == VansActionGraphNodeStatus::Running)
			result.activeNodes.push_back(m_Graph->nodes[index].guid);
		else if (state == VansActionGraphNodeStatus::Waiting)
			result.waitingNodes.push_back(m_Graph->nodes[index].guid);
	}
	return result;
}

VansActionExecutorResult VansActionGraphRuntime::Advance(VansActionExecutionContext& context)
{
	const std::size_t guardLimit = m_MaximumTransitionsPerTick;
	std::size_t iterations = 0;
	for (std::size_t index = 0; index < m_Nodes.size(); ++index)
	{
		RuntimeNode& runtime = m_Nodes[index];
		if (runtime.status != VansActionGraphNodeStatus::Running &&
			runtime.status != VansActionGraphNodeStatus::Waiting) continue;
		const auto handler = m_Handlers->Resolve(m_Graph->nodes[index].type);
		if (!handler)
		{
			m_Terminal = true;
			m_Failed = true;
			m_Error = VansActionError::Execution;
			m_Message = "Action Graph node handler disappeared";
			break;
		}
		const VansActionGraphNodeResult result = handler->Tick(context, m_Graph->nodes[index], runtime.state);
		runtime.status = result.status;
		if (result.status == VansActionGraphNodeStatus::Succeeded)
		{
			if (m_Graph->nodes[index].type == VansMakeStableId<VansActionGraphNodeTypeIdTag>(
				"Action.Graph.Race") && result.output == "Success")
				CancelConfiguredNodes(context, static_cast<std::uint32_t>(index));
			QueueOutgoing(static_cast<std::uint32_t>(index), result.output);
		}
		else if (result.status == VansActionGraphNodeStatus::Failed &&
			!QueueOutgoing(static_cast<std::uint32_t>(index), result.output.empty() ? "Failure" : result.output))
		{
			m_Failed = true;
			m_Error = result.error == VansActionError::None
				? VansActionError::Execution : result.error;
			m_Message = result.message;
		}
	}
	while (!m_Ready.empty() && !m_Failed)
	{
		if (++iterations > guardLimit)
		{
			m_Failed = true;
			m_Error = VansActionError::Budget;
			m_Message = "Action Graph exceeded the configured transition budget";
			break;
		}
		const std::uint32_t index = m_Ready.front();
		m_Ready.erase(m_Ready.begin());
		RuntimeNode& runtime = m_Nodes[index];
		runtime.status = VansActionGraphNodeStatus::NotStarted;
		const auto handler = m_Handlers->Resolve(m_Graph->nodes[index].type);
		if (!handler)
		{
			m_Failed = true;
			m_Error = VansActionError::Execution;
			m_Message = "Action Graph node handler disappeared";
			break;
		}
		if (!handler->PreservesStateAcrossActivations())
			runtime.state = VansSerializedValue::Object({});
		const VansActionGraphNodeResult result = handler->Start(context, m_Graph->nodes[index], runtime.state);
		runtime.status = result.status;
		if (result.status == VansActionGraphNodeStatus::Succeeded)
		{
			if (m_Graph->nodes[index].type == VansMakeStableId<VansActionGraphNodeTypeIdTag>(
				"Action.Graph.Race") && result.output == "Success")
				CancelConfiguredNodes(context, index);
			QueueOutgoing(index, result.output);
		}
		else if (result.status == VansActionGraphNodeStatus::Failed &&
			!QueueOutgoing(index, result.output.empty() ? "Failure" : result.output))
		{
			m_Failed = true;
			m_Error = result.error == VansActionError::None
				? VansActionError::Execution : result.error;
			m_Message = result.message;
		}
	}
	if (m_Failed)
	{
		m_Terminal = true;
		return { VansActionExecutorStatus::Failed, m_Error, m_Message };
	}
	if (!HasPendingWork())
	{
		m_Terminal = true;
		return { VansActionExecutorStatus::Succeeded };
	}
	return { VansActionExecutorStatus::Waiting };
}

bool VansActionGraphRuntime::QueueOutgoing(std::uint32_t node, std::string_view output)
{
	std::vector<VansCompiledActionGraphEdge> matches;
	for (const VansCompiledActionGraphEdge& edge : m_Graph->edges)
		if (edge.from == node && edge.output == output) matches.push_back(edge);
	std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right)
	{
		if (left.order != right.order) return left.order < right.order;
		return left.to < right.to;
	});
	for (const VansCompiledActionGraphEdge& edge : matches) m_Ready.push_back(edge.to);
	return !matches.empty();
}

void VansActionGraphRuntime::CancelConfiguredNodes(
	VansActionExecutionContext& context,
	std::uint32_t node)
{
	if (node >= m_CancelNodes.size()) return;
	for (const std::uint32_t target : m_CancelNodes[node])
	{
		if (target >= m_Nodes.size()) continue;
		RuntimeNode& runtime = m_Nodes[target];
		if (runtime.status == VansActionGraphNodeStatus::Running ||
			runtime.status == VansActionGraphNodeStatus::Waiting)
		{
			if (const auto handler = m_Handlers->Resolve(m_Graph->nodes[target].type))
				handler->Cancel(context, m_Graph->nodes[target], runtime.state);
			runtime.status = VansActionGraphNodeStatus::Cancelled;
		}
		m_Ready.erase(std::remove(m_Ready.begin(), m_Ready.end(), target), m_Ready.end());
	}
}

bool VansActionGraphRuntime::HasPendingWork() const
{
	if (!m_Ready.empty()) return true;
	for (const RuntimeNode& node : m_Nodes)
		if (node.status == VansActionGraphNodeStatus::Running ||
			node.status == VansActionGraphNodeStatus::Waiting) return true;
	return false;
}

bool VansRegisterBuiltInActionGraphNodes(
	VansActionGraphNodeRegistry& registry,
	std::string& error)
{
	const bool registered = registry.Register(std::make_shared<VansCompleteGraphNode>(), error) &&
		registry.Register(std::make_shared<VansFailGraphNode>(), error) &&
		registry.Register(std::make_shared<VansSequenceGraphNode>(), error) &&
		registry.Register(std::make_shared<VansParallelGraphNode>(), error) &&
		registry.Register(std::make_shared<VansRaceGraphNode>(), error) &&
		registry.Register(std::make_shared<VansWaitGraphNode>(), error) &&
		registry.Register(std::make_shared<VansTimeoutGraphNode>(), error) &&
		registry.Register(std::make_shared<VansBranchGraphNode>(), error) &&
		registry.Register(std::make_shared<VansSwitchGraphNode>(), error) &&
		registry.Register(std::make_shared<VansLoopGraphNode>(), error) &&
		registry.Register(std::make_shared<VansRepeatGraphNode>(), error) &&
		registry.Register(std::make_shared<VansChannelGraphNode>(), error) &&
		registry.Register(std::make_shared<VansGateGraphNode>(), error) &&
		registry.Register(std::make_shared<VansCommandGraphNode>(), error) &&
		registry.Register(std::make_shared<VansReadBindingGraphNode>(), error) &&
		registry.Register(std::make_shared<VansWaitSignalGraphNode>(), error) &&
		registry.Register(std::make_shared<VansAwaitTaskGraphNode>(), error) &&
		registry.Register(std::make_shared<VansReleaseResourceGraphNode>(), error) &&
		registry.Register(std::make_shared<VansTransferResourceGraphNode>(), error) &&
		registry.Register(std::make_shared<VansEmitSignalGraphNode>(), error) &&
		registry.Register(std::make_shared<VansSubActionGraphNode>(), error) &&
		registry.Register(std::make_shared<VansTransitionGraphNode>(), error) &&
		registry.Register(std::make_shared<VansTryGraphNode>(), error);
	if (!registered) return false;
	for (const VansActionGraphNodeDescriptor& descriptor : VansBuiltInActionGraphNodeDescriptors())
	{
		const auto handler = registry.Resolve(descriptor.type);
		if (!handler || handler->StableName() != descriptor.stableName)
		{
			error = "Built-in Action Graph node catalog is inconsistent: " + descriptor.stableName;
			return false;
		}
	}
	return true;
}
}
