#include "VansActionExecutionGraph.h"

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
			const char* category, VansActionGraphNodeKind kind, bool predictable,
			bool authorityOnly, std::vector<VansActionGraphPinDescriptor> pins,
			std::vector<VansActionGraphPropertyDescriptor> properties = {})
		{
			result.push_back({
				VansMakeStableId<VansActionGraphNodeTypeIdTag>(stableName), stableName,
				displayName, category, kind, predictable, authorityOnly,
				std::move(pins), std::move(properties) });
		};

		add("Action.Graph.Sequence", "Sequence", "Flow", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("Return", true, true),
				pin("Step.*", false), pin("Success", false) },
			{ property("steps", "Step Outputs", VansActionGraphPropertyKind::StringArray,
				VansSerializedValue::Array({})),
			  property("count", "Step Count", VansActionGraphPropertyKind::Int,
				VansSerializedValue::Int(0), 0.0, 65535.0, true) });
		add("Action.Graph.Parallel", "Parallel", "Flow", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("BranchCompleted", true, true),
				pin("Branch", false, true), pin("Success", false) },
			{ property("branches", "Branch Count", VansActionGraphPropertyKind::Int,
				VansSerializedValue::Int(1), 1.0, 65535.0, true) });
		add("Action.Graph.Race", "Race", "Flow", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("BranchCompleted", true, true),
				pin("Branch", false, true), pin("Success", false) },
			{ property("cancelNodes", "Losing Nodes", VansActionGraphPropertyKind::StringArray,
				VansSerializedValue::Array({})) });
		add("Action.Graph.Branch", "Branch", "Flow", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("True", false), pin("False", false) },
			{ property("condition", "Condition", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(false)),
			  property("variable", "Condition Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")) });
		add("Action.Graph.Switch", "Switch", "Flow", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("*", false, true), pin("Default", false) },
			{ property("value", "Value", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Null()),
			  property("variable", "Value Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("defaultOutput", "Default Output", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("Default")) });
		add("Action.Graph.Loop", "Loop", "Flow", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("Return", true), pin("Body", false),
				pin("Completed", false), pin("Failure", false) },
			{ property("condition", "Condition", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(true)),
			  property("variable", "Condition Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("maximumIterations", "Maximum Iterations", VansActionGraphPropertyKind::Int,
				VansSerializedValue::Int(1), 1.0, 65535.0, true) });
		add("Action.Graph.Repeat", "Repeat", "Flow", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("Return", true), pin("Body", false),
				pin("Success", false), pin("Failure", false) },
			{ property("count", "Count", VansActionGraphPropertyKind::Int,
				VansSerializedValue::Int(1), 0.0, 65535.0, true) });
		add("Action.Graph.Channel", "Channel", "Flow", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("*", false, true), pin("Default", false) },
			{ property("channel", "Channel", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("Default")),
			  property("variable", "Channel Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")) });
		add("Action.Graph.Gate", "Gate", "Flow", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("Open", false), pin("Closed", false),
				pin("Failure", false) },
			{ property("condition", "Open", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(false)),
			  property("variable", "Open Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("waitUntilOpen", "Wait Until Open", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(false)) });
		add("Action.Graph.Wait", "Wait", "Latent", VansActionGraphNodeKind::Latent,
			true, false, { pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("seconds", "Seconds", VansActionGraphPropertyKind::Float,
				VansSerializedValue::Float(0.0), 0.0, 86400.0, true) });
		add("Action.Graph.Timeout", "Timeout", "Latent", VansActionGraphNodeKind::Latent,
			true, false, { pin("In", true), pin("Timeout", false), pin("Failure", false) },
			{ property("seconds", "Seconds", VansActionGraphPropertyKind::Float,
				VansSerializedValue::Float(1.0), 0.0, 86400.0, true),
			  property("fail", "Fail On Timeout", VansActionGraphPropertyKind::Bool,
				VansSerializedValue::Bool(false)) });
		add("Action.Graph.Command", "Command", "Command", VansActionGraphNodeKind::Command,
			false, true, { pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("service", "Service", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("command", "Command", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")),
			  property("payload", "Payload", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})),
			  property("resultVariable", "Result Variable", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("")) });
		add("Action.Graph.Complete", "Complete", "Terminal", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("Success", false) });
		add("Action.Graph.Fail", "Fail", "Terminal", VansActionGraphNodeKind::Flow,
			true, false, { pin("In", true), pin("Failure", false) },
			{ property("message", "Message", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("Action Graph failed")) });
		add("Action.Graph.SubAction", "Sub Action", "SubAction", VansActionGraphNodeKind::SubAction,
			false, true, { pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("payload", "Activation", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})) });
		add("Action.Graph.Transition", "Transition", "SubAction", VansActionGraphNodeKind::SubAction,
			false, true, { pin("In", true), pin("Success", false), pin("Failure", false) },
			{ property("payload", "Transition", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})) });
		add("Action.Graph.Try", "Try", "Transaction", VansActionGraphNodeKind::Transaction,
			true, false, { pin("In", true), pin("Try", false), pin("Failure", false) });
		add("Action.Graph.Compensate", "Compensate", "Transaction",
			VansActionGraphNodeKind::Transaction, true, false,
			{ pin("In", true), pin("Success", false), pin("Failure", false) });
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
	bool SupportsPrediction() const override { return true; }
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
				VansActionError::DefinitionInvalid, "Sequence step count is invalid" };
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
	bool SupportsPrediction() const override { return true; }
	bool PreservesStateAcrossActivations() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const std::int64_t count = IntegerProperty(node.properties, "count", 1);
		if (count < 0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::DefinitionInvalid, "Repeat count is invalid" };
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
	bool SupportsPrediction() const override { return true; }
	bool PreservesStateAcrossActivations() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		bool available = false;
		const bool condition = ResolveNodeCondition(context, node, true, available);
		if (!available)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::DefinitionInvalid, "Loop variable is unavailable" };
		if (!condition) return { VansActionGraphNodeStatus::Succeeded, "Completed" };
		const std::int64_t maximum = IntegerProperty(node.properties, "maximumIterations", 1);
		const std::int64_t iteration = StateInteger(state, "iteration");
		if (maximum <= 0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::DefinitionInvalid, "Loop maximumIterations must be positive" };
		if (iteration >= maximum)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::BudgetExceeded, "Loop exceeded maximumIterations" };
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
	bool SupportsPrediction() const override { return true; }
	bool PreservesStateAcrossActivations() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const std::int64_t branches = IntegerProperty(node.properties, "branches", 1);
		if (branches <= 0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::DefinitionInvalid, "Parallel branch count must be positive" };
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
	bool SupportsPrediction() const override { return true; }
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
	bool SupportsPrediction() const override { return true; }
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
	bool SupportsPrediction() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		return { VansActionGraphNodeStatus::Failed, "Failure", VansActionError::ExecutionFailed,
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
	bool SupportsPrediction() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const double seconds = NumberProperty(node.properties, "seconds", 0.0);
		if (!std::isfinite(seconds) || seconds < 0.0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::DefinitionInvalid, "Wait node duration is invalid" };
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
	bool SupportsPrediction() const override { return true; }
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
					VansActionError::DefinitionInvalid, "Branch variable is unavailable" };
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
	bool SupportsPrediction() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		const VansSerializedValue* value = ResolveNodeValue(context, node, "value", "variable");
		if (!value && !ReadSerializedStringField(node.properties, "variable").empty())
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::DefinitionInvalid, "Switch variable is unavailable" };
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
	bool SupportsPrediction() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		const VansSerializedValue* value = ResolveNodeValue(context, node, "channel", "variable");
		if (!value && !ReadSerializedStringField(node.properties, "variable").empty())
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::DefinitionInvalid, "Channel variable is unavailable" };
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
	bool SupportsPrediction() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node, VansSerializedValue&) const override
	{
		bool available = false;
		const bool open = ResolveNodeCondition(context, node, false, available);
		if (!available)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::DefinitionInvalid, "Gate variable is unavailable" };
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
	bool SupportsPrediction() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode& node, VansSerializedValue& state) const override
	{
		const double seconds = NumberProperty(node.properties, "seconds", 0.0);
		if (!std::isfinite(seconds) || seconds < 0.0)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::DefinitionInvalid, "Timeout duration is invalid" };
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
				VansActionError::TimedOut, "Action Graph Timeout elapsed" };
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
	bool SupportsPrediction() const override { return true; }
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

class VansCompensateGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Compensate");
	}
	std::string_view StableName() const override { return "Action.Graph.Compensate"; }
	bool SupportsPrediction() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext& context,
		const VansCompiledActionGraphNode&, VansSerializedValue&) const override
	{
		if (!context.resources)
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::InternalInvariant, "Compensate node has no ResourceLedger" };
		std::vector<std::string> errors;
		if (!context.resources->RollbackPredicted(errors))
		{
			std::string message = "Action Graph compensation failed";
			for (const std::string& item : errors) message += "; " + item;
			return { VansActionGraphNodeStatus::Failed, "Failure",
				VansActionError::CommitFailed, std::move(message) };
		}
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

VansActionGraphNodeResult ExecuteGraphCommand(
	VansActionExecutionContext& context,
	const VansCompiledActionGraphNode& node,
	std::string defaultService,
	std::string defaultCommand,
	const VansSerializedValue* payloadOverride = nullptr)
{
	const std::string serviceName = ReadSerializedStringField(
		node.properties, "service", std::move(defaultService));
	const std::string commandName = ReadSerializedStringField(
		node.properties, "command", std::move(defaultCommand));
	if (!context.services || !context.context || serviceName.empty() || commandName.empty())
		return { VansActionGraphNodeStatus::Failed, "Failure", VansActionError::ServiceMissing,
			"Command node service, command or Action context is missing" };
	VansActionCommand command;
	command.service = VansMakeStableId<VansActionServiceIdTag>(serviceName);
	command.command = VansMakeStableId<VansActionFieldIdTag>(commandName);
	command.stableName = commandName;
	command.action = context.action;
	command.context = *context.context;
	command.predicted = context.context->predictionKey.IsValid();
	if (payloadOverride)
		command.payload = *payloadOverride;
	else if (const VansSerializedValue* payload = FindObjectField(node.properties, "payload"))
		command.payload = *payload;
	const VansActionCommandSchema* commandSchema =
		context.services->ResolveCommandSchema(command.service, command.command);
	VansGenerationHandle releasedResource;
	if (commandSchema &&
		commandSchema->resourcePolicy == VansActionCommandResourcePolicy::Release)
	{
		if (const VansSerializedValue* encoded = FindObjectField(command.payload, "resource"))
		{
			const std::int64_t index = ReadSerializedIntField(*encoded, "index", -1);
			const std::int64_t generation = ReadSerializedIntField(*encoded, "generation", 0);
			if (index >= 0 && generation > 0)
				releasedResource = { static_cast<std::uint32_t>(index),
					static_cast<std::uint32_t>(generation) };
		}
	}
	const VansActionCommandResult executed = context.services->Execute(command);
	if (!executed)
		return { VansActionGraphNodeStatus::Failed, "Failure", executed.error, executed.message };
	if (releasedResource && context.resources)
		context.resources->ForgetExternalResource(command.service, releasedResource);
	if (executed.resource)
	{
		const std::shared_ptr<IVansActionService> service = context.services->Resolve(command.service);
		if (!service || !context.resources)
			return { VansActionGraphNodeStatus::Failed, "Failure", VansActionError::InternalInvariant,
				"Command resource cannot be tracked" };
		VansActionResourceEntry resource;
		resource.type = "ActionService";
		resource.debugName = serviceName + "." + commandName;
		resource.service = command.service;
		resource.externalResource = executed.resource;
		resource.prediction = command.predicted
			? VansActionPredictionResourcePolicy::UndoOnly
			: VansActionPredictionResourcePolicy::NotPredictable;
		resource.release = [service, handle = executed.resource]
		{
			std::string ignored;
			return service->Release(handle, ignored);
		};
		if (command.predicted) resource.undo = resource.release;
		std::string resourceError;
		if (!context.resources->Register(std::move(resource), resourceError))
		{
			std::string ignored;
			service->Release(executed.resource, ignored);
			return { VansActionGraphNodeStatus::Failed, "Failure", VansActionError::CommitFailed,
				resourceError };
		}
	}
	const std::string resultVariable = ReadSerializedStringField(node.properties, "resultVariable");
	if (!resultVariable.empty() && (!context.variables || !context.variables->Set(
		VansMakeStableId<VansActionFieldIdTag>(resultVariable), executed.payload)))
		return { VansActionGraphNodeStatus::Failed, "Failure", VansActionError::DefinitionInvalid,
			"Command result variable is unavailable" };
	return { VansActionGraphNodeStatus::Succeeded, "Success" };
}

class VansCommandGraphNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Command");
	}
	std::string_view StableName() const override { return "Action.Graph.Command"; }
	bool SupportsPrediction() const override { return false; }
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
	bool SupportsPrediction() const override { return false; }
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
	bool SupportsPrediction() const override { return false; }
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
	if (graph->version == 0 || graph->contentHash == 0 || graph->nodes.empty() ||
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
		else if (node.predictable && !handler->SupportsPrediction())
			addError("GAF-GRAPH-PREDICTION", "Predictable path contains a non-predictable node",
				"nodes[" + std::to_string(index) + "]");
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
		return { VansActionExecutorStatus::Failed, VansActionError::ExecutionFailed,
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
	m_Error = VansActionError::ExecutionFailed;
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
			m_Error = VansActionError::ExecutionFailed;
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
				? VansActionError::ExecutionFailed : result.error;
			m_Message = result.message;
		}
	}
	while (!m_Ready.empty() && !m_Failed)
	{
		if (++iterations > guardLimit)
		{
			m_Failed = true;
			m_Error = VansActionError::BudgetExceeded;
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
			m_Error = VansActionError::ExecutionFailed;
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
				? VansActionError::ExecutionFailed : result.error;
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
		registry.Register(std::make_shared<VansSubActionGraphNode>(), error) &&
		registry.Register(std::make_shared<VansTransitionGraphNode>(), error) &&
		registry.Register(std::make_shared<VansTryGraphNode>(), error) &&
		registry.Register(std::make_shared<VansCompensateGraphNode>(), error);
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
