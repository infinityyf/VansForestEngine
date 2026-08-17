#include "VansCameraActionGraphNodes.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <memory>
#include <utility>

namespace Vans
{
namespace
{
VansActionGraphPinDescriptor Pin(std::string name, bool input)
{
	return { std::move(name), "Flow", input, false };
}

VansActionGraphPropertyDescriptor Property(
	std::string name,
	std::string displayName,
	VansActionGraphPropertyKind kind,
	VansSerializedValue defaultValue,
	bool required = false,
	double minimum = 0.0,
	double maximum = 0.0,
	bool ranged = false)
{
	VansActionGraphPropertyDescriptor result;
	result.name = std::move(name);
	result.displayName = std::move(displayName);
	result.kind = kind;
	result.defaultValue = std::move(defaultValue);
	result.required = required;
	result.hasMinimum = ranged;
	result.hasMaximum = ranged;
	result.minimum = minimum;
	result.maximum = maximum;
	return result;
}

class CameraServiceNode final : public IVansActionGraphNodeHandler
{
public:
	CameraServiceNode(std::string stableName, std::string command)
		: m_StableName(std::move(stableName)), m_Command(std::move(command)),
		  m_Type(VansMakeStableId<VansActionGraphNodeTypeIdTag>(m_StableName)) {}

	VansActionGraphNodeTypeId TypeId() const override { return m_Type; }
	std::string_view StableName() const override { return m_StableName; }
	bool SupportsPrediction() const override { return true; }
	VansActionGraphNodeResult Start(
		VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node,
		VansSerializedValue&) const override
	{
		VansSerializedValue payload = VansSerializedValue::Object({});
		for (const auto& [name, value] : node.properties.objectFields)
		{
			if (name == "resultVariable" || name == "resourceVariable") continue;
			SetSerializedObjectField(payload, name, value);
		}
		const std::string resourceVariable =
			ReadSerializedStringField(node.properties, "resourceVariable");
		if (!resourceVariable.empty())
		{
			const VansSerializedValue* variable = context.variables
				? context.variables->Get(VansMakeStableId<VansActionFieldIdTag>(resourceVariable))
				: nullptr;
			const VansSerializedValue* resource = variable
				? FindObjectField(*variable, "resource") : nullptr;
			if (!resource)
				return { VansActionGraphNodeStatus::Failed, "Failure",
					VansActionError::InvalidHandle,
					"Camera Graph node resource variable is unavailable" };
			SetSerializedObjectField(payload, "resource", *resource);
		}
		return VansExecuteActionServiceGraphCommand(
			context, node, "Service.Camera", m_Command, &payload);
	}
	VansActionGraphNodeResult Tick(
		VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node,
		VansSerializedValue& state) const override
	{
		return Start(context, node, state);
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}

private:
	std::string m_StableName;
	std::string m_Command;
	VansActionGraphNodeTypeId m_Type;
};

class CameraWaitEventNode final : public IVansActionGraphNodeHandler
{
public:
	VansActionGraphNodeTypeId TypeId() const override
	{
		return VansMakeStableId<VansActionGraphNodeTypeIdTag>("Camera.WaitEvent");
	}
	std::string_view StableName() const override { return "Camera.WaitEvent"; }
	bool SupportsPrediction() const override { return true; }
	VansActionGraphNodeResult Start(VansActionExecutionContext&,
		const VansCompiledActionGraphNode&, VansSerializedValue&) const override
	{
		return { VansActionGraphNodeStatus::Waiting };
	}
	VansActionGraphNodeResult Tick(
		VansActionExecutionContext& context,
		const VansCompiledActionGraphNode& node,
		VansSerializedValue&) const override
	{
		const std::string expected = ReadSerializedStringField(node.properties, "event");
		if (!context.events || expected.empty())
			return { VansActionGraphNodeStatus::Waiting };
		for (const VansActionEvent& event : *context.events)
		{
			if (event.stableName != expected) continue;
			const std::string resultVariable =
				ReadSerializedStringField(node.properties, "resultVariable");
			if (!resultVariable.empty() && (!context.variables || !context.variables->Set(
				VansMakeStableId<VansActionFieldIdTag>(resultVariable), event.payload)))
			{
				return { VansActionGraphNodeStatus::Failed, "Failure",
					VansActionError::DefinitionInvalid,
					"Camera event result variable is unavailable" };
			}
			return { VansActionGraphNodeStatus::Succeeded, "Success" };
		}
		return { VansActionGraphNodeStatus::Waiting };
	}
	void Cancel(VansActionExecutionContext&, const VansCompiledActionGraphNode&,
		VansSerializedValue&) const override {}
};
}

const std::vector<VansActionGraphNodeDescriptor>& VansCameraActionGraphNodeDescriptors()
{
	static const std::vector<VansActionGraphNodeDescriptor> descriptors = []
	{
		std::vector<VansActionGraphNodeDescriptor> result;
		const auto addCommand = [&](const char* type, const char* display,
			std::vector<VansActionGraphPropertyDescriptor> properties)
		{
			result.push_back({ VansMakeStableId<VansActionGraphNodeTypeIdTag>(type), type,
				display, "Camera", VansActionGraphNodeKind::Command, true, false,
				{ Pin("In", true), Pin("Success", false), Pin("Failure", false) },
				std::move(properties) });
		};
		const auto view = []
		{
			return Property("view", "View", VansActionGraphPropertyKind::String,
				VansSerializedValue::String("Main"));
		};
		const auto priority = []
		{
			return Property("priority", "Priority", VansActionGraphPropertyKind::Int,
				VansSerializedValue::Int(0), false, -32768.0, 32767.0, true);
		};
		const auto resultVariable = []
		{
			return Property("resultVariable", "Result Variable",
				VansActionGraphPropertyKind::String, VansSerializedValue::String(""));
		};
		const auto resourceVariable = []
		{
			return Property("resourceVariable", "Resource Variable",
				VansActionGraphPropertyKind::String, VansSerializedValue::String(""), true);
		};
		const auto asset = [](const char* name, const char* display, VansAssetType type)
		{
			auto result = Property(name, display, VansActionGraphPropertyKind::AssetReference,
				VansSerializedValue::String(""), true);
			result.allowedAssetTypes.push_back(type);
			return result;
		};
		addCommand("Camera.PushShot", "Push Shot",
		{
			asset("rig", "Camera Rig", VansAssetType::CameraRigProfile), view(), priority(),
			Property("blendIn", "Blend In", VansActionGraphPropertyKind::Float,
				VansSerializedValue::Float(0.0), false, 0.0, 1.0, true), resultVariable()
		});
		addCommand("Camera.PushLens", "Push Lens",
		{
			Property("lens", "Lens", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({}), true), view(), priority(), resultVariable()
		});
		addCommand("Camera.StartShake", "Start Shake",
		{
			asset("shake", "Camera Shake", VansAssetType::CameraShakeProfile),
			Property("scale", "Scale", VansActionGraphPropertyKind::Float,
				VansSerializedValue::Float(1.0), false, 0.0, 1.0, true),
			view(), resultVariable()
		});
		addCommand("Camera.AddImpulse", "Add Impulse",
		{
			Property("translation", "Translation", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})),
			Property("rotation", "Rotation", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})), view()
		});
		addCommand("Camera.StartLockOn", "Start Lock On",
		{
			Property("target", "Target", VansActionGraphPropertyKind::Payload,
				VansSerializedValue::Object({})), view(), priority(), resultVariable()
		});
		addCommand("Camera.UpdateLockOn", "Update Lock On",
		{
			resourceVariable(), Property("target", "Target",
				VansActionGraphPropertyKind::Payload, VansSerializedValue::Object({}))
		});
		addCommand("Camera.Release", "Release Camera",
			{ resourceVariable() });
		result.push_back({ VansMakeStableId<VansActionGraphNodeTypeIdTag>("Camera.WaitEvent"),
			"Camera.WaitEvent", "Wait Camera Event", "Camera",
			VansActionGraphNodeKind::Latent, true, false,
			{ Pin("In", true), Pin("Success", false), Pin("Failure", false) },
			{
				Property("event", "Event", VansActionGraphPropertyKind::String,
					VansSerializedValue::String(""), true),
				resultVariable()
			} });
		return result;
	}();
	return descriptors;
}

bool VansRegisterCameraActionGraphNodes(
	VansActionGraphNodeRegistry& registry,
	std::string& error)
{
	return registry.Register(std::make_shared<CameraServiceNode>(
		"Camera.PushShot", "Camera.Shot"), error) &&
		registry.Register(std::make_shared<CameraServiceNode>(
			"Camera.PushLens", "Camera.Lens"), error) &&
		registry.Register(std::make_shared<CameraServiceNode>(
			"Camera.StartShake", "Camera.Shake"), error) &&
		registry.Register(std::make_shared<CameraServiceNode>(
			"Camera.AddImpulse", "Camera.Impulse"), error) &&
		registry.Register(std::make_shared<CameraServiceNode>(
			"Camera.StartLockOn", "Camera.LockOn"), error) &&
		registry.Register(std::make_shared<CameraServiceNode>(
			"Camera.UpdateLockOn", "Camera.UpdateLockOn"), error) &&
		registry.Register(std::make_shared<CameraServiceNode>(
			"Camera.Release", "Camera.Release"), error) &&
		registry.Register(std::make_shared<CameraWaitEventNode>(), error);
}
}
