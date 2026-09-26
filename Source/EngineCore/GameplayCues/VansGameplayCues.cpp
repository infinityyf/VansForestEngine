#include "VansGameplayCues.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../GameplayTargeting/VansGameplayTargeting.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Vans
{
namespace
{
VansSerializedValue EntityValue(VansEntityHandle entity)
{
	return VansSerializedValue::Object({
		{ "index", VansSerializedValue::Int(entity.index) },
		{ "generation", VansSerializedValue::Int(entity.generation) }
	});
}

VansSerializedValue VectorValue(const std::array<double, 3>& value)
{
	return VansSerializedValue::Object({
		{ "x", VansSerializedValue::Float(value[0]) },
		{ "y", VansSerializedValue::Float(value[1]) },
		{ "z", VansSerializedValue::Float(value[2]) }
	});
}

VansSerializedValue ResourceValue(VansGenerationHandle resource)
{
	return VansSerializedValue::Object({
		{ "index", VansSerializedValue::Int(resource.index) },
		{ "generation", VansSerializedValue::Int(resource.generation) }
	});
}

const VansActionCommandFieldSchema* FindField(
	const VansActionCommandSchema& schema,
	std::string_view name)
{
	const auto found = std::find_if(schema.fields.begin(), schema.fields.end(),
		[name](const auto& field) { return field.name == name; });
	return found == schema.fields.end() ? nullptr : &*found;
}

bool ReadSourceValue(
	VansGameplayCueSource source,
	const VansGameplayCueBinding& binding,
	const VansGameplayCueParameters& parameters,
	VansSerializedValue& value)
{
	switch (source)
	{
	case VansGameplayCueSource::Asset:
		if (binding.asset.empty()) return false;
		value = VansSerializedValue::String(binding.asset);
		return true;
	case VansGameplayCueSource::Target:
	{
		const VansEntityHandle target = parameters.target.IsValid() ? parameters.target :
			parameters.context.Entity(VansActionContextSlots::PrimaryTarget);
		if (!target.IsValid()) return false;
		value = EntityValue(target);
		return true;
	}
	case VansGameplayCueSource::Position:
		if (!parameters.position) return false;
		value = VectorValue(*parameters.position);
		return true;
	case VansGameplayCueSource::Origin:
		if (!parameters.origin) return false;
		value = VectorValue(*parameters.origin);
		return true;
	case VansGameplayCueSource::Direction:
		if (!parameters.direction) return false;
		value = VectorValue(*parameters.direction);
		return true;
	case VansGameplayCueSource::Normal:
		if (!parameters.normal) return false;
		value = VectorValue(*parameters.normal);
		return true;
	case VansGameplayCueSource::Surface:
		if (!parameters.surface) return false;
		value = VansSerializedValue::String(std::to_string(parameters.surface.value));
		return true;
	case VansGameplayCueSource::Intensity:
		if (!std::isfinite(parameters.intensity)) return false;
		value = VansSerializedValue::Float(parameters.intensity);
		return true;
	case VansGameplayCueSource::Payload:
		if (parameters.payload.kind != VansSerializedValue::Kind::Object) return false;
		value = parameters.payload;
		return true;
	}
	return false;
}

bool BuildPayload(
	const VansGameplayCueBinding& binding,
	const VansGameplayCueCommandBinding& command,
	const VansGameplayCueParameters& parameters,
	VansGenerationHandle resource,
	const VansActionCommandSchema& schema,
	VansSerializedValue& payload,
	std::string& error)
{
	payload = command.values;
	if (payload.kind != VansSerializedValue::Kind::Object)
	{
		error = "Gameplay Cue command values must be an object";
		return false;
	}
	if (resource)
	{
		const VansActionCommandFieldSchema* resourceField = FindField(schema, "resource");
		if (!resourceField || !resourceField->required ||
			resourceField->kind != VansActionCommandValueKind::Object)
		{
			error = "Gameplay Cue lifecycle command must declare a required object resource field";
			return false;
		}
		SetSerializedObjectField(payload, "resource", ResourceValue(resource));
	}
	for (const VansGameplayCueFieldBinding& field : command.fields)
	{
		if (FindObjectField(payload, field.field))
		{
			error = "Gameplay Cue command field has both a static value and a source: " + field.field;
			return false;
		}
		VansSerializedValue value;
		if (!ReadSourceValue(field.source, binding, parameters, value))
		{
			error = "Gameplay Cue source is unavailable: " +
				std::string(VansGameplayCueSourceName(field.source));
			return false;
		}
		SetSerializedObjectField(payload, field.field, std::move(value));
	}
	return true;
}
}

bool VansReadGameplayCueSource(std::string_view name, VansGameplayCueSource& source)
{
	static constexpr std::pair<std::string_view, VansGameplayCueSource> kSources[] = {
		{ "Asset", VansGameplayCueSource::Asset },
		{ "Target", VansGameplayCueSource::Target },
		{ "Position", VansGameplayCueSource::Position },
		{ "Origin", VansGameplayCueSource::Origin },
		{ "Direction", VansGameplayCueSource::Direction },
		{ "Normal", VansGameplayCueSource::Normal },
		{ "Surface", VansGameplayCueSource::Surface },
		{ "Intensity", VansGameplayCueSource::Intensity },
		{ "Payload", VansGameplayCueSource::Payload }
	};
	const auto found = std::find_if(std::begin(kSources), std::end(kSources),
		[name](const auto& item) { return item.first == name; });
	if (found == std::end(kSources)) return false;
	source = found->second;
	return true;
}

std::string_view VansGameplayCueSourceName(VansGameplayCueSource source)
{
	switch (source)
	{
	case VansGameplayCueSource::Asset: return "Asset";
	case VansGameplayCueSource::Target: return "Target";
	case VansGameplayCueSource::Position: return "Position";
	case VansGameplayCueSource::Origin: return "Origin";
	case VansGameplayCueSource::Direction: return "Direction";
	case VansGameplayCueSource::Normal: return "Normal";
	case VansGameplayCueSource::Surface: return "Surface";
	case VansGameplayCueSource::Intensity: return "Intensity";
	case VansGameplayCueSource::Payload: return "Payload";
	}
	return {};
}

void VansApplyGameplayCueTargetData(
	VansGameplayCueParameters& parameters,
	const VansTargetData& targetData)
{
	for (const VansTargetDataValue& value : targetData.values)
	{
		if (const auto* entity = std::get_if<VansEntityHandle>(&value))
		{
			if (!parameters.target.IsValid()) parameters.target = *entity;
		}
		else if (const auto* location = std::get_if<VansTargetLocation>(&value))
		{
			if (!parameters.position) parameters.position = location->value;
		}
		else if (const auto* ray = std::get_if<VansTargetRay>(&value))
		{
			if (!parameters.origin) parameters.origin = ray->origin;
			if (!parameters.direction) parameters.direction = ray->direction;
		}
		else if (const auto* hit = std::get_if<VansTargetHitResult>(&value))
		{
			if (!parameters.target.IsValid())
				parameters.target = hit->hitEntity.IsValid() ? hit->hitEntity : hit->entity;
			if (!parameters.position) parameters.position = hit->position;
			if (!parameters.normal) parameters.normal = hit->normal;
			if (!parameters.surface) parameters.surface = hit->surface;
		}
	}
}

VansActionServiceGameplayCueAdapter::VansActionServiceGameplayCueAdapter(
	VansCueId cue,
	std::string stableName,
	VansGameplayCueScope scope,
	VansGameplayCueBinding binding,
	const VansActionServiceRegistry* services)
	: m_Cue(cue)
	, m_StableName(std::move(stableName))
	, m_Scope(scope)
	, m_Binding(std::move(binding))
	, m_Services(services)
{
}

bool VansActionServiceGameplayCueAdapter::Validate(std::string& error) const
{
	if (!m_Cue || m_StableName.empty() || !m_Binding.service ||
		!m_Binding.invoke.command || !m_Services || !m_Services->IsSealed())
	{
		error = "Gameplay Cue Action Service adapter is not ready";
		return false;
	}
	const VansActionCommandSchema* invoke =
		m_Services->ResolveCommandSchema(m_Binding.service, m_Binding.invoke.command);
	if (!invoke || (invoke->resourcePolicy != VansActionCommandResourcePolicy::None &&
		invoke->resourcePolicy != VansActionCommandResourcePolicy::Create))
	{
		error = "Gameplay Cue invoke command is missing or has an invalid resource policy: " +
			m_StableName;
		return false;
	}
	const auto validateCommand = [&](const VansGameplayCueCommandBinding& command,
		VansActionCommandResourcePolicy policy, bool lifecycle)
	{
		if (!command.command)
		{
			if (command.values.kind != VansSerializedValue::Kind::Object ||
				!command.values.objectFields.empty() || !command.fields.empty())
			{
				error = "Gameplay Cue command data has no command: " + m_StableName;
				return false;
			}
			return true;
		}
		const VansActionCommandSchema* schema =
			m_Services->ResolveCommandSchema(m_Binding.service, command.command);
		if (!schema || schema->resourcePolicy != policy)
		{
			error = "Gameplay Cue command has an invalid resource policy: " + m_StableName;
			return false;
		}
		if (command.values.kind != VansSerializedValue::Kind::Object)
		{
			error = "Gameplay Cue command values must be an object: " + m_StableName;
			return false;
		}
		std::unordered_set<std::string> fields;
		for (const VansGameplayCueFieldBinding& field : command.fields)
		{
			if (field.field.empty() || field.field == "resource" ||
				!fields.insert(field.field).second || !FindField(*schema, field.field) ||
				FindObjectField(command.values, field.field))
			{
				error = "Gameplay Cue contains an invalid or duplicate field binding: " + field.field;
				return false;
			}
		}
		if (FindObjectField(command.values, "resource"))
		{
			error = "Gameplay Cue resource is owned by the adapter lifecycle";
			return false;
		}
		VansGameplayCueParameters sample;
		sample.target = { 1, 1 };
		sample.position = std::array<double, 3>{ 1.0, 2.0, 3.0 };
		sample.origin = sample.position;
		sample.direction = sample.position;
		sample.normal = sample.position;
		sample.surface = VansGameplayTagId{ 1 };
		VansSerializedValue payload;
		if (!BuildPayload(m_Binding, command, sample,
			lifecycle ? VansGenerationHandle{ 1, 1 } : VansGenerationHandle{},
			*schema, payload, error)) return false;
		return m_Services->ValidatePayload(m_Binding.service, command.command, payload, error);
	};
	if (!validateCommand(m_Binding.invoke, invoke->resourcePolicy, false)) return false;
	if ((m_Binding.update.command || m_Binding.release.command) &&
		invoke->resourcePolicy != VansActionCommandResourcePolicy::Create)
	{
		error = "Gameplay Cue lifecycle commands require a resource-creating invoke command";
		return false;
	}
	if (!validateCommand(m_Binding.update, VansActionCommandResourcePolicy::Update, true) ||
		!validateCommand(m_Binding.release, VansActionCommandResourcePolicy::Release, true))
		return false;
	bool usesAsset = false;
	for (const VansGameplayCueCommandBinding* command :
		{ &m_Binding.invoke, &m_Binding.update, &m_Binding.release })
		for (const VansGameplayCueFieldBinding& field : command->fields)
			usesAsset = usesAsset || field.source == VansGameplayCueSource::Asset;
	if (usesAsset != !m_Binding.asset.empty())
	{
		error = usesAsset ? "Gameplay Cue Asset source has no asset" :
			"Gameplay Cue asset is not bound to a command field";
		return false;
	}
	return true;
}

VansActionCommandResult VansActionServiceGameplayCueAdapter::Run(
	const VansGameplayCueCommandBinding& binding,
	const VansGameplayCueParameters& parameters,
	VansGenerationHandle resource) const
{
	if (!m_Services || !binding.command)
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			"Gameplay Cue command is invalid" };
	const VansActionCommandSchema* schema =
		m_Services->ResolveCommandSchema(m_Binding.service, binding.command);
	if (!schema)
		return { VansActionError::Dependency, {}, VansSerializedValue::Object({}),
			"Gameplay Cue command schema is unavailable" };
	VansSerializedValue payload;
	std::string error;
	if (!BuildPayload(m_Binding, binding, parameters, resource, *schema, payload, error) ||
		!m_Services->ValidatePayload(m_Binding.service, binding.command, payload, error))
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			std::move(error), "Core.GameplayCue.InvalidBinding" };
	VansActionCommand command;
	command.service = m_Binding.service;
	command.command = binding.command;
	command.stableName = schema->stableName;
	command.context = parameters.context;
	command.payload = std::move(payload);
	return m_Services->Execute(command);
}

bool VansActionServiceGameplayCueAdapter::Execute(
	const VansGameplayCueKey&,
	VansGameplayCueScope,
	const VansGameplayCueParameters& parameters,
	std::string& error)
{
	VansActionCommandResult result = Run(m_Binding.invoke, parameters, {});
	if (!result)
	{
		error = result.message;
		return false;
	}
	if (result.resource)
	{
		auto service = m_Services->Resolve(m_Binding.service);
		if (!service || !service->Release(result.resource, error)) return false;
	}
	return true;
}

VansGenerationHandle VansActionServiceGameplayCueAdapter::Add(
	const VansGameplayCueKey&,
	VansGameplayCueScope,
	const VansGameplayCueParameters& parameters,
	std::string& error)
{
	VansActionCommandResult result = Run(m_Binding.invoke, parameters, {});
	if (!result)
	{
		error = result.message;
		return {};
	}
	if (!result.resource)
	{
		error = "Persistent Gameplay Cue invoke command did not create a resource";
		return {};
	}
	return m_Active.Emplace(ActiveCue{ parameters, result.resource });
}

bool VansActionServiceGameplayCueAdapter::Update(
	VansGenerationHandle resource,
	const VansGameplayCueParameters& parameters,
	std::string& error)
{
	ActiveCue* cue = m_Active.Resolve(resource);
	if (!cue)
	{
		error = "Gameplay Cue adapter resource is stale";
		return false;
	}
	if (m_Binding.update.command)
	{
		const VansActionCommandResult result = Run(m_Binding.update, parameters, cue->resource);
		if (!result)
		{
			error = result.message;
			return false;
		}
	}
	cue->parameters = parameters;
	return true;
}

bool VansActionServiceGameplayCueAdapter::ReleaseBound(
	VansGenerationHandle resource,
	const VansGameplayCueParameters& parameters,
	std::string& error) const
{
	if (m_Binding.release.command)
	{
		const VansActionCommandResult result = Run(m_Binding.release, parameters, resource);
		if (!result)
		{
			error = result.message;
			return false;
		}
	}
	else
	{
		auto service = m_Services ? m_Services->Resolve(m_Binding.service) : nullptr;
		if (!service || !service->Release(resource, error)) return false;
	}
	return true;
}

bool VansActionServiceGameplayCueAdapter::Remove(
	VansGenerationHandle resource,
	std::string& error)
{
	ActiveCue* cue = m_Active.Resolve(resource);
	if (!cue)
	{
		error = "Gameplay Cue adapter resource is stale";
		return false;
	}
	if (!ReleaseBound(cue->resource, cue->parameters, error)) return false;
	return m_Active.Release(resource);
}

bool VansGameplayCueRegistry::Register(
	std::shared_ptr<IVansGameplayCueAdapter> adapter,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Gameplay Cue registry is sealed";
		return false;
	}
	if (!adapter || !adapter->CueId() || adapter->StableName().empty())
	{
		error = "Gameplay Cue adapter is invalid";
		return false;
	}
	if (!m_Adapters.emplace(adapter->CueId(), std::move(adapter)).second)
	{
		error = "duplicate Gameplay Cue adapter";
		return false;
	}
	return true;
}

bool VansGameplayCueRegistry::Seal(bool allowEmpty, std::string& error)
{
	if (m_Adapters.empty() && !allowEmpty)
	{
		error = "Gameplay Cue registry is empty";
		return false;
	}
	error.clear();
	m_Sealed = true;
	return true;
}

std::shared_ptr<IVansGameplayCueAdapter> VansGameplayCueRegistry::Resolve(VansCueId cue) const
{
	const auto found = m_Adapters.find(cue);
	return found == m_Adapters.end() ? nullptr : found->second;
}

VansGameplayCueExecuteStatus VansGameplayCueService::Execute(
	const VansGameplayCueKey& key,
	std::optional<VansGameplayCueScope> scopeOverride,
	const VansGameplayCueParameters& parameters,
	std::string& error)
{
	error.clear();
	if (!m_Registry || !m_Registry->IsSealed() || !key.IsValid() ||
		m_MaximumExecutionHistory == 0)
	{
		error = "Gameplay Cue service is not ready or key is invalid";
		return VansGameplayCueExecuteStatus::Failed;
	}
	if (m_Executed.find(key) != m_Executed.end())
		return VansGameplayCueExecuteStatus::Suppressed;
	std::shared_ptr<IVansGameplayCueAdapter> adapter = m_Registry->Resolve(key.cue);
	if (!adapter)
	{
		error = "Gameplay Cue adapter is missing";
		return VansGameplayCueExecuteStatus::Failed;
	}
	const VansGameplayCueScope scope = scopeOverride.value_or(adapter->DefaultScope());
	if (!adapter->Execute(key, scope, parameters, error))
		return VansGameplayCueExecuteStatus::Failed;
	m_Executed.insert(key);
	m_ExecutionOrder.push_back(key);
	while (m_ExecutionOrder.size() > m_MaximumExecutionHistory)
	{
		m_Executed.erase(m_ExecutionOrder.front());
		m_ExecutionOrder.pop_front();
	}
	return VansGameplayCueExecuteStatus::Executed;
}

VansCueHandle VansGameplayCueService::Add(
	const VansGameplayCueKey& key,
	std::optional<VansGameplayCueScope> scopeOverride,
	const VansGameplayCueParameters& parameters,
	std::string& error)
{
	if (!m_Registry || !m_Registry->IsSealed() || !key.IsValid())
	{
		error = "Gameplay Cue add request is invalid";
		return {};
	}
	std::shared_ptr<IVansGameplayCueAdapter> adapter = m_Registry->Resolve(key.cue);
	if (!adapter)
	{
		error = "Gameplay Cue adapter is missing";
		return {};
	}
	const VansGameplayCueScope scope = scopeOverride.value_or(adapter->DefaultScope());
	const VansGenerationHandle resource = adapter->Add(key, scope, parameters, error);
	if (!resource) return {};
	return { m_Active.Emplace(ActiveCue{ std::move(adapter), resource }) };
}

bool VansGameplayCueService::Update(
	VansCueHandle handle,
	const VansGameplayCueParameters& parameters,
	std::string& error)
{
	ActiveCue* cue = m_Active.Resolve(handle.value);
	if (!cue)
	{
		error = "Gameplay Cue handle is stale";
		return false;
	}
	return cue->adapter->Update(cue->resource, parameters, error);
}

bool VansGameplayCueService::Remove(VansCueHandle handle, std::string& error)
{
	ActiveCue* cue = m_Active.Resolve(handle.value);
	if (!cue)
	{
		error = "Gameplay Cue handle is stale";
		return false;
	}
	if (!cue->adapter->Remove(cue->resource, error)) return false;
	return m_Active.Release(handle.value);
}

void VansGameplayCueService::Clear()
{
	std::vector<VansCueHandle> removals;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveCue&) { removals.push_back({ handle }); });
	for (VansCueHandle handle : removals)
	{
		std::string removeError;
		if (!Remove(handle, removeError))
			VANS_LOG_ERROR("[GAF] Gameplay Cue service clear failed: " << removeError);
	}
	m_Executed.clear();
	m_ExecutionOrder.clear();
}
}
