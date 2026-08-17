#include "VansStandardActionServices.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <utility>

namespace Vans
{
namespace
{
using ValueKind = VansActionCommandValueKind;
using ResourcePolicy = VansActionCommandResourcePolicy;
using Prediction = VansActionServicePredictionSupport;

VansActionCommandFieldSchema Field(
	std::string name,
	ValueKind kind,
	bool required = false,
	VansSerializedValue defaultValue = {})
{
	VansActionCommandFieldSchema result;
	result.name = std::move(name);
	result.kind = kind;
	result.required = required;
	result.defaultValue = std::move(defaultValue);
	return result;
}

VansActionCommandFieldSchema NumberField(
	std::string name,
	ValueKind kind,
	bool required,
	VansSerializedValue defaultValue,
	double minimum,
	double maximum)
{
	VansActionCommandFieldSchema result =
		Field(std::move(name), kind, required, std::move(defaultValue));
	result.hasMinimum = true;
	result.hasMaximum = true;
	result.minimum = minimum;
	result.maximum = maximum;
	return result;
}

VansActionCommandFieldSchema ResourceField()
{
	return Field("resource", ValueKind::Object, true, VansSerializedValue::Object({}));
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
	if (index < 0 || index > UINT32_MAX || generation <= 0 || generation > UINT32_MAX) return false;
	resource = { static_cast<std::uint32_t>(index), static_cast<std::uint32_t>(generation) };
	return true;
}

VansActionCommandSchema Command(
	std::string name,
	ResourcePolicy resource,
	Prediction prediction,
	std::vector<VansActionCommandFieldSchema> fields = {})
{
	VansActionCommandSchema result;
	result.command = VansMakeStableId<VansActionFieldIdTag>(name);
	result.stableName = std::move(name);
	result.resourcePolicy = resource;
	result.prediction = prediction;
	result.fields = std::move(fields);
	return result;
}

VansActionServiceCapability Service(
	std::string name,
	Prediction prediction,
	std::vector<VansActionCommandSchema> commands)
{
	VansActionServiceCapability result;
	result.service = VansMakeStableId<VansActionServiceIdTag>(name);
	result.stableName = std::move(name);
	result.version = 1;
	result.prediction = prediction;
	result.commandSchemas = std::move(commands);
	for (const VansActionCommandSchema& command : result.commandSchemas)
		result.commands.push_back(command.stableName);
	return result;
}

std::vector<VansActionServiceCapability> BuildCapabilities()
{
	const auto asset = [](std::string name)
	{
		return Field(std::move(name), ValueKind::String, true);
	};
	const auto optionalString = [](std::string name)
	{
		return Field(std::move(name), ValueKind::String, false,
			VansSerializedValue::String({}));
	};
	const auto normalized = [](std::string name, double value = 1.0)
	{
		return NumberField(std::move(name), ValueKind::Float, false,
			VansSerializedValue::Float(value), 0.0, 1.0);
	};
	const auto rate = []
	{
		return NumberField("rate", ValueKind::Float, false,
			VansSerializedValue::Float(1.0), 0.01, 100.0);
	};

	std::vector<VansActionServiceCapability> result;
	result.push_back(Service("Service.Animation", Prediction::PredictableWithRollback,
	{
		Command("Animation.Play", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ asset("clip"), optionalString("slot"),
				NumberField("layer", ValueKind::Int, false, VansSerializedValue::Int(0), 0.0, 255.0),
				rate(), Field("loop", ValueKind::Bool, false, VansSerializedValue::Bool(false)) }),
		Command("Animation.Stop", ResourcePolicy::Release, Prediction::PredictableWithRollback,
			{ ResourceField(), normalized("blendOut", 0.0) }),
		Command("Animation.SetRate", ResourcePolicy::Update, Prediction::PredictableWithRollback,
			{ ResourceField(), rate() }),
		Command("Animation.JumpMarker", ResourcePolicy::Update, Prediction::PredictableWithRollback,
			{ ResourceField(), Field("marker", ValueKind::String, true) }),
		Command("Animation.WaitMarker", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ ResourceField(), Field("marker", ValueKind::String, true),
				NumberField("timeout", ValueKind::Float, false, VansSerializedValue::Float(0.0), 0.0, 3600.0) })
	}));
	result.push_back(Service("Service.Audio", Prediction::PredictableWithRollback,
	{
		Command("Audio.OneShot", ResourcePolicy::None, Prediction::Predictable,
			{ asset("sound"), normalized("volume"),
				NumberField("pitch", ValueKind::Float, false, VansSerializedValue::Float(1.0), 0.01, 4.0),
				Field("spatial", ValueKind::Bool, false, VansSerializedValue::Bool(true)) }),
		Command("Audio.Loop", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ asset("sound"), normalized("volume"),
				NumberField("pitch", ValueKind::Float, false, VansSerializedValue::Float(1.0), 0.01, 4.0) }),
		Command("Audio.Update", ResourcePolicy::Update, Prediction::PredictableWithRollback,
			{ ResourceField(), normalized("volume"),
				NumberField("pitch", ValueKind::Float, false, VansSerializedValue::Float(1.0), 0.01, 4.0) }),
		Command("Audio.Stop", ResourcePolicy::Release, Prediction::PredictableWithRollback,
			{ ResourceField(), normalized("fadeOut", 0.0) })
	}));
	result.push_back(Service("Service.VFX", Prediction::PredictableWithRollback,
	{
		Command("VFX.Spawn", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ asset("effect"), optionalString("socket"), Field("parameters", ValueKind::Object, false,
				VansSerializedValue::Object({})) }),
		Command("VFX.Attach", ResourcePolicy::Update, Prediction::PredictableWithRollback,
			{ ResourceField(), optionalString("socket") }),
		Command("VFX.Update", ResourcePolicy::Update, Prediction::PredictableWithRollback,
			{ ResourceField(), Field("parameters", ValueKind::Object, true,
				VansSerializedValue::Object({})) }),
		Command("VFX.Stop", ResourcePolicy::Release, Prediction::PredictableWithRollback,
			{ ResourceField(), Field("immediate", ValueKind::Bool, false, VansSerializedValue::Bool(false)) })
	}));
	result.push_back(Service("Service.Combat", Prediction::AuthorityOnly,
	{
		Command("Combat.ResolveHit", ResourcePolicy::None, Prediction::AuthorityOnly,
			{ Field("targets", ValueKind::Array, true, VansSerializedValue::Array({})),
				asset("damageProfile"), optionalString("policy") }),
		Command("Combat.ApplyDamageProfile", ResourcePolicy::None, Prediction::AuthorityOnly,
			{ Field("target", ValueKind::Object, true, VansSerializedValue::Object({})),
				asset("damageProfile"), NumberField("scale", ValueKind::Float, false,
					VansSerializedValue::Float(1.0), 0.0, 1000000.0) })
	}));
	result.push_back(Service("Service.PhysicsQuery", Prediction::Predictable,
	{
		Command("PhysicsQuery.Ray", ResourcePolicy::None, Prediction::Predictable,
			{ Field("origin", ValueKind::Object, true, VansSerializedValue::Object({})),
				Field("direction", ValueKind::Object, true, VansSerializedValue::Object({})),
				NumberField("distance", ValueKind::Float, true, VansSerializedValue::Float(1.0), 0.0, 1000000.0),
				optionalString("layerMask") }),
		Command("PhysicsQuery.Shape", ResourcePolicy::None, Prediction::Predictable,
			{ Field("shape", ValueKind::Object, true, VansSerializedValue::Object({})),
				Field("transform", ValueKind::Object, true, VansSerializedValue::Object({})), optionalString("layerMask") }),
		Command("PhysicsQuery.Sweep", ResourcePolicy::None, Prediction::Predictable,
			{ Field("shape", ValueKind::Object, true, VansSerializedValue::Object({})),
				Field("from", ValueKind::Object, true, VansSerializedValue::Object({})),
				Field("to", ValueKind::Object, true, VansSerializedValue::Object({})), optionalString("layerMask") })
	}));
	result.push_back(Service("Service.Projectile", Prediction::PredictableWithRollback,
	{
		Command("Projectile.Spawn", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ asset("projectile"), Field("transform", ValueKind::Object, true,
				VansSerializedValue::Object({})), Field("velocity", ValueKind::Object, false,
				VansSerializedValue::Object({})) }),
		Command("Projectile.Correct", ResourcePolicy::Update, Prediction::AuthorityOnly,
			{ ResourceField(), Field("state", ValueKind::Object, true, VansSerializedValue::Object({})) }),
		Command("Projectile.Destroy", ResourcePolicy::Release, Prediction::PredictableWithRollback,
			{ ResourceField(), optionalString("reason") })
	}));
	result.push_back(Service("Service.Camera", Prediction::PredictableWithRollback,
	{
		Command("Camera.Shot", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ asset("rig"), optionalString("view"),
				NumberField("priority", ValueKind::Int, false, VansSerializedValue::Int(0), -32768.0, 32767.0),
				normalized("blendIn", 0.0) }),
		Command("Camera.Lens", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ optionalString("view"), Field("lens", ValueKind::Object, true,
				VansSerializedValue::Object({})), NumberField("priority", ValueKind::Int, false,
				VansSerializedValue::Int(0), -32768.0, 32767.0) }),
		Command("Camera.Shake", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ asset("shake"), normalized("scale"), optionalString("view") }),
		Command("Camera.Impulse", ResourcePolicy::None, Prediction::Predictable,
			{ Field("translation", ValueKind::Object, false, VansSerializedValue::Object({})),
				Field("rotation", ValueKind::Object, false, VansSerializedValue::Object({})), optionalString("view") }),
		Command("Camera.LockOn", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ Field("target", ValueKind::Object, false, VansSerializedValue::Object({})),
				optionalString("view"), NumberField("priority", ValueKind::Int, false,
				VansSerializedValue::Int(0), -32768.0, 32767.0) }),
		Command("Camera.UpdateLockOn", ResourcePolicy::Update, Prediction::PredictableWithRollback,
			{ ResourceField(), Field("target", ValueKind::Object, false,
				VansSerializedValue::Object({})) }),
		Command("Camera.Release", ResourcePolicy::Release, Prediction::PredictableWithRollback,
			{ ResourceField(), normalized("blendOut", 0.0) })
	}));
	result.push_back(Service("Service.Navigation", Prediction::AuthorityOnly,
	{
		Command("Navigation.RequestPath", ResourcePolicy::Create, Prediction::AuthorityOnly,
			{ Field("destination", ValueKind::Object, true, VansSerializedValue::Object({})),
				optionalString("agentProfile") }),
		Command("Navigation.Move", ResourcePolicy::Create, Prediction::AuthorityOnly,
			{ Field("destination", ValueKind::Object, true, VansSerializedValue::Object({})),
				NumberField("acceptanceRadius", ValueKind::Float, false,
					VansSerializedValue::Float(0.1), 0.0, 1000000.0) }),
		Command("Navigation.Cancel", ResourcePolicy::Release, Prediction::AuthorityOnly,
			{ ResourceField() })
	}));
	result.push_back(Service("Service.UI", Prediction::PredictableWithRollback,
	{
		Command("UI.Prompt", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ Field("messageKey", ValueKind::String, true), optionalString("binding"),
				Field("parameters", ValueKind::Object, false, VansSerializedValue::Object({})) }),
		Command("UI.Indicator", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ asset("indicator"), Field("target", ValueKind::Object, false,
				VansSerializedValue::Object({})), Field("parameters", ValueKind::Object, false,
				VansSerializedValue::Object({})) }),
		Command("UI.ActionStateView", ResourcePolicy::Create, Prediction::PredictableWithRollback,
			{ optionalString("viewModel"), Field("parameters", ValueKind::Object, false,
				VansSerializedValue::Object({})) }),
		Command("UI.Remove", ResourcePolicy::Release, Prediction::PredictableWithRollback,
			{ ResourceField() })
	}));
	return result;
}

const VansActionCommandSchema* FindCommand(
	const VansActionServiceCapability& capability,
	VansActionFieldId command)
{
	for (const VansActionCommandSchema& schema : capability.commandSchemas)
		if (schema.command == command)
			return &schema;
	return nullptr;
}

VansSerializedValue SampleValue(const VansActionCommandFieldSchema& field)
{
	if (!field.defaultValue.IsNull())
		return field.defaultValue;
	switch (field.kind)
	{
	case ValueKind::Bool: return VansSerializedValue::Bool(false);
	case ValueKind::Int:
		return VansSerializedValue::Int(static_cast<std::int64_t>(field.hasMinimum ? field.minimum : 0.0));
	case ValueKind::Float:
		return VansSerializedValue::Float(field.hasMinimum ? field.minimum : 0.0);
	case ValueKind::String: return VansSerializedValue::String("Conformance.Sample");
	case ValueKind::Object: return VansSerializedValue::Object({});
	case ValueKind::Array: return VansSerializedValue::Array({});
	}
	return {};
}
}

const std::vector<VansActionServiceCapability>& VansStandardActionServiceCapabilities()
{
	static const std::vector<VansActionServiceCapability> capabilities = BuildCapabilities();
	return capabilities;
}

const VansActionServiceCapability* VansFindStandardActionServiceCapability(
	VansActionServiceId service)
{
	const auto& capabilities = VansStandardActionServiceCapabilities();
	const auto found = std::find_if(capabilities.begin(), capabilities.end(),
		[service](const VansActionServiceCapability& candidate)
		{
			return candidate.service == service;
		});
	return found == capabilities.end() ? nullptr : &*found;
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
	{
		if (m_Handlers.find(command.command) == m_Handlers.end())
		{
			error = "Action Service adapter is missing command binding: " + command.stableName;
			return false;
		}
	}
	const bool createsResources = std::any_of(
		m_Capability.commandSchemas.begin(), m_Capability.commandSchemas.end(),
		[](const VansActionCommandSchema& command)
		{
			return command.resourcePolicy == ResourcePolicy::Create;
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
		return { VansActionError::ServiceMissing, {}, VansSerializedValue::Object({}),
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
		return { VansActionError::DefinitionInvalid, {}, VansSerializedValue::Object({}),
			"Fake Action Service received an undeclared command" };
	++m_ExecutedCommandCount;
	VansGenerationHandle resource;
	if (schema->resourcePolicy == ResourcePolicy::Create)
		resource = m_Resources.Emplace(ResourceState{ command.command, m_NextSequence++ });
	else if (schema->resourcePolicy == ResourcePolicy::Update ||
		schema->resourcePolicy == ResourcePolicy::Release)
	{
		if (!ReadResource(command.payload, resource) || !m_Resources.Resolve(resource))
			return { VansActionError::InvalidHandle, {}, VansSerializedValue::Object({}),
				"Fake Action Service received a stale resource handle" };
		if (schema->resourcePolicy == ResourcePolicy::Release && !m_Resources.Release(resource))
			return { VansActionError::InternalInvariant, {}, VansSerializedValue::Object({}),
				"Fake Action Service could not release its resource" };
		resource = {};
	}
	VansSerializedValue payload = VansSerializedValue::Object({});
	SetSerializedObjectField(payload, "service", VansSerializedValue::String(m_Capability.stableName));
	SetSerializedObjectField(payload, "command", VansSerializedValue::String(schema->stableName));
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

std::vector<std::shared_ptr<VansFakeActionService>> VansCreateFakeStandardActionServices()
{
	std::vector<std::shared_ptr<VansFakeActionService>> result;
	for (const VansActionServiceCapability& capability : VansStandardActionServiceCapabilities())
		result.push_back(std::make_shared<VansFakeActionService>(capability));
	return result;
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
			if (schema.resourcePolicy == ResourcePolicy::Update ||
				schema.resourcePolicy == ResourcePolicy::Release)
			{
				const auto create = std::find_if(service->Capability().commandSchemas.begin(),
					service->Capability().commandSchemas.end(), [](const auto& candidate)
					{ return candidate.resourcePolicy == ResourcePolicy::Create; });
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
			if (schema.resourcePolicy == ResourcePolicy::Create)
			{
				if (!result.resource || !service->Release(result.resource, error))
				{
					if (error.empty()) error = "Action Service conformance resource release failed";
					return false;
				}
			}
			else if (schema.resourcePolicy == ResourcePolicy::Update)
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
