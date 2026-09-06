#include "VansActionServices.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vans
{
namespace
{
bool MatchesValueKind(const VansSerializedValue& value, VansActionCommandValueKind kind)
{
	switch (kind)
	{
	case VansActionCommandValueKind::Bool:
		return value.kind == VansSerializedValue::Kind::Bool;
	case VansActionCommandValueKind::Int:
		return value.kind == VansSerializedValue::Kind::Int;
	case VansActionCommandValueKind::Float:
		return value.kind == VansSerializedValue::Kind::Float ||
			value.kind == VansSerializedValue::Kind::Int;
	case VansActionCommandValueKind::String:
		return value.kind == VansSerializedValue::Kind::String;
	case VansActionCommandValueKind::Object:
		return value.kind == VansSerializedValue::Kind::Object;
	case VansActionCommandValueKind::Array:
		return value.kind == VansSerializedValue::Kind::Array;
	}
	return false;
}

bool ReadNumericValue(const VansSerializedValue& value, double& result)
{
	if (value.kind == VansSerializedValue::Kind::Int)
	{
		result = static_cast<double>(value.intValue);
		return true;
	}
	if (value.kind == VansSerializedValue::Kind::Float)
	{
		result = value.floatValue;
		return true;
	}
	return false;
}

bool ValidateFieldSchema(const VansActionCommandFieldSchema& field, std::string& error)
{
	if (field.name.empty())
	{
		error = "Action Service command contains an unnamed payload field";
		return false;
	}
	if ((field.hasMinimum || field.hasMaximum) &&
		field.kind != VansActionCommandValueKind::Int &&
		field.kind != VansActionCommandValueKind::Float)
	{
		error = "Action Service command field range is only valid for numeric fields: " + field.name;
		return false;
	}
	if ((field.hasMinimum && !std::isfinite(field.minimum)) ||
		(field.hasMaximum && !std::isfinite(field.maximum)) ||
		(field.hasMinimum && field.hasMaximum && field.minimum > field.maximum))
	{
		error = "Action Service command field range is invalid: " + field.name;
		return false;
	}
	if (!field.defaultValue.IsNull())
	{
		if (!MatchesValueKind(field.defaultValue, field.kind))
		{
			error = "Action Service command field default has the wrong type: " + field.name;
			return false;
		}
		double number = 0.0;
		if (ReadNumericValue(field.defaultValue, number) &&
			((field.hasMinimum && number < field.minimum) ||
				(field.hasMaximum && number > field.maximum)))
		{
			error = "Action Service command field default is outside its range: " + field.name;
			return false;
		}
	}
	return true;
}

bool ValidateCommandPayload(
	const VansActionCommandSchema& schema,
	const VansSerializedValue& payload,
	std::string& error)
{
	if (payload.kind != VansSerializedValue::Kind::Object)
	{
		error = "Action Service command payload must be an object";
		return false;
	}

	for (const VansActionCommandFieldSchema& field : schema.fields)
	{
		const VansSerializedValue* value = FindObjectField(payload, field.name);
		if (!value)
		{
			if (field.required && field.defaultValue.IsNull())
			{
				error = "Action Service command is missing required field: " + field.name;
				return false;
			}
			continue;
		}
		if (!MatchesValueKind(*value, field.kind))
		{
			error = "Action Service command field has the wrong type: " + field.name;
			return false;
		}
		double number = 0.0;
		if (ReadNumericValue(*value, number) &&
			((field.hasMinimum && number < field.minimum) ||
				(field.hasMaximum && number > field.maximum)))
		{
			error = "Action Service command field is outside its range: " + field.name;
			return false;
		}
	}

	if (!schema.allowUnknownFields)
	{
		for (const auto& [name, value] : payload.objectFields)
		{
			(void)value;
			bool known = false;
			for (const VansActionCommandFieldSchema& field : schema.fields)
			{
				if (field.name == name)
				{
					known = true;
					break;
				}
			}
			if (!known)
			{
				error = "Action Service command contains an unknown field: " + name;
				return false;
			}
		}
	}
	return true;
}

}

bool VansActionServiceRegistry::Register(
	std::shared_ptr<IVansActionService> service,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Action Service registry is sealed";
		return false;
	}
	if (!service || !service->Capability().service ||
		service->Capability().stableName.empty())
	{
		error = "Action Service capability is invalid";
		return false;
	}
	if (service->Capability().service !=
		VansMakeStableId<VansActionServiceIdTag>(service->Capability().stableName))
	{
		error = "Action Service stable ID does not match its name";
		return false;
	}
	if (!m_Services.emplace(service->Capability().service, service).second)
	{
		error = "duplicate Action Service";
		return false;
	}
	m_TickOrder.push_back(std::move(service));
	return true;
}

bool VansActionServiceRegistry::Seal(std::string& error)
{
	for (const auto& entry : m_Services)
	{
		std::unordered_set<std::string> schemaNames;
		std::unordered_set<VansActionFieldId> schemaIds;
		for (const VansActionCommandSchema& command : entry.second->Capability().commandSchemas)
		{
			if (!command.command || command.stableName.empty() ||
				command.command != VansMakeStableId<VansActionFieldIdTag>(command.stableName))
			{
				error = "Action Service command schema has an invalid stable identity";
				return false;
			}
			if (!schemaNames.insert(command.stableName).second || !schemaIds.insert(command.command).second)
			{
				error = "Action Service contains a duplicate command schema";
				return false;
			}
			std::unordered_set<std::string> fields;
			for (const VansActionCommandFieldSchema& field : command.fields)
			{
				if (!fields.insert(field.name).second)
				{
					error = "Action Service command contains a duplicate payload field";
					return false;
				}
				if (!ValidateFieldSchema(field, error))
					return false;
			}
		}
	}
	std::sort(m_TickOrder.begin(), m_TickOrder.end(), [](const auto& left, const auto& right)
	{
		return left->Capability().stableName < right->Capability().stableName;
	});
	m_Sealed = true;
	return true;
}

void VansActionServiceRegistry::Tick(double deltaSeconds) const
{
	if (!m_Sealed) return;
	for (const std::shared_ptr<IVansActionService>& service : m_TickOrder)
		service->Tick(deltaSeconds);
}

const VansActionCommandSchema* VansActionServiceRegistry::ResolveCommandSchema(
	VansActionServiceId service,
	VansActionFieldId command) const
{
	const auto resolved = Resolve(service);
	if (!resolved)
		return nullptr;
	for (const VansActionCommandSchema& schema : resolved->Capability().commandSchemas)
		if (schema.command == command)
			return &schema;
	return nullptr;
}

std::shared_ptr<IVansActionService> VansActionServiceRegistry::Resolve(VansActionServiceId service) const
{
	const auto found = m_Services.find(service);
	return found == m_Services.end() ? nullptr : found->second;
}

bool VansActionServiceRegistry::ValidateRequired(
	const std::vector<VansActionServiceId>& required,
	std::string& error) const
{
	if (!m_Sealed)
	{
		error = "Action Service registry is not sealed";
		return false;
	}
	for (VansActionServiceId service : required)
	{
		if (!Resolve(service))
		{
			error = "required Action Service is missing";
			return false;
		}
	}
	return true;
}

VansActionCommandResult VansActionServiceRegistry::Execute(const VansActionCommand& command) const
{
	const auto service = Resolve(command.service);
	if (!service)
		return { VansActionError::Dependency, {}, VansSerializedValue::Object({}),
			"Action Service is missing", "Core.ActionService.Missing" };
	if (!m_Sealed)
		return { VansActionError::Internal, {}, VansSerializedValue::Object({}),
			"Action Service registry is not sealed", "Core.ActionService.RegistryNotSealed" };
	if (!command.command || command.stableName.empty() ||
		command.command != VansMakeStableId<VansActionFieldIdTag>(command.stableName))
	{
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			"Action Service command stable identity is invalid",
			"Core.ActionService.InvalidCommandIdentity" };
	}

	const VansActionCommandSchema* schema = ResolveCommandSchema(command.service, command.command);
	if (!schema)
	{
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			"Action Service command is not declared by the service",
			"Core.ActionService.CommandNotDeclared" };
	}
	if (schema)
	{
		std::string payloadError;
		if (!ValidateCommandPayload(*schema, command.payload, payloadError))
		{
			return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
				std::move(payloadError), "Core.ActionService.InvalidPayload" };
		}
	}

	VansActionCommandResult result = service->Execute(command);
	if (result.error != VansActionError::None && result.reasonCode.empty())
		result.reasonCode = VansActionDefaultReasonCode(result.error);
	if (!result || !schema)
		return result;

	if (schema->resourcePolicy == VansActionCommandResourcePolicy::Create && !result.resource)
	{
		result.error = VansActionError::Internal;
		result.message = "Action Service create command did not return a resource handle";
		result.reasonCode = "Core.ActionService.ResourceMissing";
		return result;
	}
	if (schema->resourcePolicy != VansActionCommandResourcePolicy::Create && result.resource)
	{
		std::string releaseError;
		service->Release(result.resource, releaseError);
		result.resource = {};
		result.error = VansActionError::Internal;
		result.message = "Action Service command returned an unexpected resource handle";
		result.reasonCode = "Core.ActionService.UnexpectedResource";
		if (!releaseError.empty())
			result.message += ": " + releaseError;
	}
	return result;
}
}
