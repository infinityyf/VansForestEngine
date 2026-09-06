#include "VansActionServiceAdapter.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <utility>

namespace Vans
{
namespace
{
const VansActionCommandSchema* FindCommand(
	const VansActionServiceCapability& capability,
	VansActionFieldId command)
{
	for (const VansActionCommandSchema& schema : capability.commandSchemas)
		if (schema.command == command) return &schema;
	return nullptr;
}

VansSerializedValue ResourceValue(VansGenerationHandle resource)
{
	return VansSerializedValue::Object({
		{ "index", VansSerializedValue::Int(resource.index) },
		{ "generation", VansSerializedValue::Int(resource.generation) }
	});
}

bool ReadResource(const VansSerializedValue& payload, VansGenerationHandle& resource)
{
	const VansSerializedValue* value = FindObjectField(payload, "resource");
	if (!value || value->kind != VansSerializedValue::Kind::Object) return false;
	const std::int64_t index = ReadSerializedIntField(*value, "index", -1);
	const std::int64_t generation = ReadSerializedIntField(*value, "generation", -1);
	if (index < 0 || index > UINT32_MAX || generation <= 0 || generation > UINT32_MAX)
		return false;
	resource = { static_cast<std::uint32_t>(index),
		static_cast<std::uint32_t>(generation) };
	return true;
}

VansSerializedValue SampleValue(const VansActionCommandFieldSchema& field)
{
	if (!field.defaultValue.IsNull()) return field.defaultValue;
	switch (field.kind)
	{
	case VansActionCommandValueKind::Bool: return VansSerializedValue::Bool(false);
	case VansActionCommandValueKind::Int:
		return VansSerializedValue::Int(
			static_cast<std::int64_t>(field.hasMinimum ? field.minimum : 0.0));
	case VansActionCommandValueKind::Float:
		return VansSerializedValue::Float(field.hasMinimum ? field.minimum : 0.0);
	case VansActionCommandValueKind::String:
		return VansSerializedValue::String("Conformance.Sample");
	case VansActionCommandValueKind::Object: return VansSerializedValue::Object({});
	case VansActionCommandValueKind::Array: return VansSerializedValue::Array({});
	}
	return {};
}
}

VansActionCommandFieldSchema VansActionCommandField(
	std::string name,
	VansActionCommandValueKind kind,
	bool required,
	VansSerializedValue defaultValue)
{
	VansActionCommandFieldSchema result;
	result.name = std::move(name);
	result.kind = kind;
	result.required = required;
	result.defaultValue = std::move(defaultValue);
	return result;
}

VansActionCommandFieldSchema VansActionCommandNumberField(
	std::string name,
	VansActionCommandValueKind kind,
	bool required,
	VansSerializedValue defaultValue,
	double minimum,
	double maximum)
{
	VansActionCommandFieldSchema result = VansActionCommandField(
		std::move(name), kind, required, std::move(defaultValue));
	result.hasMinimum = true;
	result.hasMaximum = true;
	result.minimum = minimum;
	result.maximum = maximum;
	return result;
}

VansActionCommandFieldSchema VansActionCommandResourceField()
{
	return VansActionCommandField("resource", VansActionCommandValueKind::Object,
		true, VansSerializedValue::Object({}));
}

VansActionCommandSchema VansActionCommandCapability(
	std::string stableName,
	VansActionCommandResourcePolicy resourcePolicy,
	std::vector<VansActionCommandFieldSchema> fields)
{
	VansActionCommandSchema result;
	result.command = VansMakeStableId<VansActionFieldIdTag>(stableName);
	result.stableName = std::move(stableName);
	result.resourcePolicy = resourcePolicy;
	result.fields = std::move(fields);
	return result;
}

VansActionServiceCapability VansActionServiceCapabilityDescriptor(
	std::string stableName,
	std::vector<VansActionCommandSchema> commands)
{
	VansActionServiceCapability result;
	result.service = VansMakeStableId<VansActionServiceIdTag>(stableName);
	result.stableName = std::move(stableName);
	result.commandSchemas = std::move(commands);
	return result;
}

VansSerializedValue VansBuildActionCommandSamplePayload(const VansActionCommandSchema& schema)
{
	VansSerializedValue payload = VansSerializedValue::Object({});
	for (const VansActionCommandFieldSchema& field : schema.fields)
		if (field.required || !field.defaultValue.IsNull())
			SetSerializedObjectField(payload, field.name, SampleValue(field));
	return payload;
}

VansActionServiceAdapter::VansActionServiceAdapter(VansActionServiceCapability capability)
	: m_Capability(std::move(capability))
{
}

bool VansActionServiceAdapter::Bind(
	std::string_view command,
	VansActionServiceCommandHandler handler,
	std::string& error)
{
	const VansActionFieldId id = VansMakeStableId<VansActionFieldIdTag>(command);
	if (!handler || !FindCommand(m_Capability, id))
	{
		error = "Action Service adapter command is invalid or undeclared";
		return false;
	}
	if (!m_Handlers.emplace(id, std::move(handler)).second)
	{
		error = "Action Service adapter command is already bound";
		return false;
	}
	return true;
}

void VansActionServiceAdapter::SetReleaseHandler(VansActionServiceReleaseHandler handler)
{
	m_Release = std::move(handler);
}

bool VansActionServiceAdapter::ValidateBindings(std::string& error) const
{
	for (const VansActionCommandSchema& command : m_Capability.commandSchemas)
		if (m_Handlers.find(command.command) == m_Handlers.end())
		{
			error = "Action Service adapter is missing command binding: " + command.stableName;
			return false;
		}
	const bool createsResources = std::any_of(
		m_Capability.commandSchemas.begin(), m_Capability.commandSchemas.end(),
		[](const VansActionCommandSchema& command)
		{
			return command.resourcePolicy == VansActionCommandResourcePolicy::Create;
		});
	if (createsResources && !m_Release)
	{
		error = "Action Service adapter creates resources but has no release handler";
		return false;
	}
	return true;
}

VansActionCommandResult VansActionServiceAdapter::Execute(const VansActionCommand& command)
{
	const auto found = m_Handlers.find(command.command);
	if (found == m_Handlers.end())
		return { VansActionError::Dependency, {}, VansSerializedValue::Object({}),
			"Action Service adapter command is not bound" };
	return found->second(command);
}

bool VansActionServiceAdapter::Release(VansGenerationHandle resource, std::string& error)
{
	if (!m_Release)
	{
		error = "Action Service adapter has no resource release handler";
		return false;
	}
	return m_Release(resource, error);
}

VansFakeActionService::VansFakeActionService(VansActionServiceCapability capability)
	: m_Capability(std::move(capability))
{
}

VansActionCommandResult VansFakeActionService::Execute(const VansActionCommand& command)
{
	const VansActionCommandSchema* schema = FindCommand(m_Capability, command.command);
	if (!schema)
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			"Fake Action Service received an undeclared command" };
	++m_ExecutedCommandCount;
	VansGenerationHandle resource;
	if (schema->resourcePolicy == VansActionCommandResourcePolicy::Create)
		resource = m_Resources.Emplace(ResourceState{ command.command, m_NextSequence++ });
	else if (schema->resourcePolicy == VansActionCommandResourcePolicy::Update ||
		schema->resourcePolicy == VansActionCommandResourcePolicy::Release)
	{
		if (!ReadResource(command.payload, resource) || !m_Resources.Resolve(resource))
			return { VansActionError::Internal, {}, VansSerializedValue::Object({}),
				"Fake Action Service received a stale resource handle" };
		if (schema->resourcePolicy == VansActionCommandResourcePolicy::Release &&
			!m_Resources.Release(resource))
			return { VansActionError::Internal, {}, VansSerializedValue::Object({}),
				"Fake Action Service could not release its resource" };
		resource = {};
	}
	VansSerializedValue payload = VansSerializedValue::Object({});
	SetSerializedObjectField(payload, "service",
		VansSerializedValue::String(m_Capability.stableName));
	SetSerializedObjectField(payload, "command",
		VansSerializedValue::String(schema->stableName));
	return { VansActionError::None, resource, std::move(payload), {} };
}

bool VansFakeActionService::Release(VansGenerationHandle resource, std::string& error)
{
	if (!m_Resources.Release(resource))
	{
		error = "Fake Action Service resource handle is stale or invalid";
		return false;
	}
	return true;
}

bool VansRunActionServiceConformance(
	VansActionServiceRegistry& registry,
	const std::vector<std::shared_ptr<VansFakeActionService>>& services,
	std::string& error)
{
	if (!registry.IsSealed())
	{
		error = "Action Service conformance requires a sealed registry";
		return false;
	}
	for (const auto& service : services)
	{
		if (!service || registry.Resolve(service->Capability().service) != service)
		{
			error = "Action Service conformance registry does not contain the supplied service";
			return false;
		}
		for (const VansActionCommandSchema& schema : service->Capability().commandSchemas)
		{
			VansGenerationHandle prerequisite;
			VansActionCommand command;
			command.service = service->Capability().service;
			command.command = schema.command;
			command.stableName = schema.stableName;
			command.payload = VansBuildActionCommandSamplePayload(schema);
			if (schema.resourcePolicy == VansActionCommandResourcePolicy::Update ||
				schema.resourcePolicy == VansActionCommandResourcePolicy::Release)
			{
				const auto create = std::find_if(service->Capability().commandSchemas.begin(),
					service->Capability().commandSchemas.end(), [](const auto& candidate)
					{
						return candidate.resourcePolicy ==
							VansActionCommandResourcePolicy::Create;
					});
				if (create == service->Capability().commandSchemas.end())
				{
					error = "Action Service conformance cannot provision a resource for: " +
						schema.stableName;
					return false;
				}
				VansActionCommand createCommand;
				createCommand.service = service->Capability().service;
				createCommand.command = create->command;
				createCommand.stableName = create->stableName;
				createCommand.payload = VansBuildActionCommandSamplePayload(*create);
				const VansActionCommandResult created = registry.Execute(createCommand);
				if (!created || !created.resource)
				{
					error = "Action Service conformance resource setup failed: " + schema.stableName;
					return false;
				}
				prerequisite = created.resource;
				SetSerializedObjectField(command.payload, "resource", ResourceValue(prerequisite));
			}
			const VansActionCommandResult result = registry.Execute(command);
			if (!result)
			{
				error = "Action Service conformance command failed: " + schema.stableName +
					": " + result.message;
				return false;
			}
			if (schema.resourcePolicy == VansActionCommandResourcePolicy::Create)
			{
				if (!result.resource || !service->Release(result.resource, error))
				{
					if (error.empty()) error = "Action Service conformance resource release failed";
					return false;
				}
			}
			else if (schema.resourcePolicy == VansActionCommandResourcePolicy::Update)
			{
				if (!service->Release(prerequisite, error))
				{
					if (error.empty()) error = "Action Service conformance update cleanup failed";
					return false;
				}
			}
		}
		if (service->ActiveResourceCount() != 0)
		{
			error = "Action Service conformance leaked a resource";
			return false;
		}
	}
	return true;
}
}
