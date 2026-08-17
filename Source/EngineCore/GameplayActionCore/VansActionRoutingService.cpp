#include "VansActionRoutingService.h"

#include "VansActionHost.h"
#include "VansActionScheduler.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

namespace Vans
{
namespace
{
VansActionCommandFieldSchema ActionField(
	std::string name,
	VansActionCommandValueKind kind,
	bool required,
	VansSerializedValue defaultValue = {})
{
	VansActionCommandFieldSchema result;
	result.name = std::move(name);
	result.kind = kind;
	result.required = required;
	result.defaultValue = std::move(defaultValue);
	return result;
}

VansActionCommandSchema ActionCommand(
	std::string name,
	std::vector<VansActionCommandFieldSchema> fields)
{
	VansActionCommandSchema result;
	result.command = VansMakeStableId<VansActionFieldIdTag>(name);
	result.stableName = std::move(name);
	result.resourcePolicy = VansActionCommandResourcePolicy::None;
	result.prediction = VansActionServicePredictionSupport::AuthorityOnly;
	result.fields = std::move(fields);
	return result;
}
}

VansActionRoutingService::VansActionRoutingService(VansActionScheduler& scheduler)
	: m_Scheduler(&scheduler)
{
	m_Capability.service = VansMakeStableId<VansActionServiceIdTag>("Service.Action");
	m_Capability.stableName = "Service.Action";
	m_Capability.version = 1;
	m_Capability.prediction = VansActionServicePredictionSupport::AuthorityOnly;
	m_Capability.commandSchemas.push_back(ActionCommand("ActivateSubAction",
	{
		ActionField("action", VansActionCommandValueKind::String, true),
		ActionField("contextPatch", VansActionCommandValueKind::Object, false,
			VansSerializedValue::Object({})),
		ActionField("inheritPrimaryTarget", VansActionCommandValueKind::Bool, false,
			VansSerializedValue::Bool(true))
	}));
	m_Capability.commandSchemas.push_back(ActionCommand("Transition",
	{
		ActionField("action", VansActionCommandValueKind::String, true),
		ActionField("contextPatch", VansActionCommandValueKind::Object, false,
			VansSerializedValue::Object({})),
		ActionField("cancelSource", VansActionCommandValueKind::Bool, false,
			VansSerializedValue::Bool(true)),
		ActionField("inheritPrimaryTarget", VansActionCommandValueKind::Bool, false,
			VansSerializedValue::Bool(true))
	}));
	for (const VansActionCommandSchema& command : m_Capability.commandSchemas)
		m_Capability.commands.push_back(command.stableName);
}

VansActionCommandResult VansActionRoutingService::Execute(const VansActionCommand& command)
{
	if (!m_Scheduler)
		return { VansActionError::InvalidState, {}, VansSerializedValue::Object({}),
			"Action routing service is detached" };
	const std::shared_ptr<VansActionHost> host = m_Scheduler->FindByOwner(command.context.owner);
	if (!host)
		return { VansActionError::InvalidHandle, {}, VansSerializedValue::Object({}),
			"Action routing service could not resolve the source Host" };
	const std::string actionName = ReadSerializedStringField(command.payload, "action");
	const VansActionId target = VansMakeStableId<VansActionIdTag>(actionName);
	const VansSerializedValue* patch = FindObjectField(command.payload, "contextPatch");
	const bool transition = command.stableName == "Transition";
	const bool cancelSource = transition &&
		ReadSerializedBoolField(command.payload, "cancelSource", true);
	const bool inheritPrimaryTarget =
		ReadSerializedBoolField(command.payload, "inheritPrimaryTarget", true);
	const VansActionResult queued = host->RequestTransition(
		command.action,
		target,
		command.context,
		patch ? *patch : VansSerializedValue::Object({}),
		cancelSource,
		inheritPrimaryTarget);
	if (!queued)
		return { queued.error, {}, VansSerializedValue::Object({}), queued.message };
	VansSerializedValue payload = VansSerializedValue::Object({});
	SetSerializedObjectField(payload, "queued", VansSerializedValue::Bool(true));
	SetSerializedObjectField(payload, "targetAction", VansSerializedValue::String(actionName));
	return { VansActionError::None, {}, std::move(payload), {} };
}

bool VansActionRoutingService::Release(VansGenerationHandle, std::string& error)
{
	error = "Action routing service does not create resources";
	return false;
}
}
