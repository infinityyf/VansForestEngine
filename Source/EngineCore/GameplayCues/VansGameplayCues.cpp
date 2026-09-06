#include "VansGameplayCues.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>

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

bool IsAssetField(std::string_view name)
{
	return name == "clip" || name == "sound" || name == "effect" ||
		name == "damageProfile" || name == "projectile" || name == "rig" ||
		name == "shake" || name == "indicator";
}

bool CanSupplyDynamicField(std::string_view name, bool hasAsset, bool hasResource)
{
	return (hasAsset && IsAssetField(name)) || (hasResource && name == "resource") ||
		name == "target" || name == "position" || name == "origin" ||
		name == "direction" || name == "intensity" || name == "scale" ||
		name == "parameters" || name == "payload";
}
}

VansActionServiceGameplayCueAdapter::VansActionServiceGameplayCueAdapter(
	VansCueId cue,
	std::string stableName,
	VansGameplayCueScope scope,
	std::vector<VansGameplayCueAdapterMapping> mappings,
	const VansActionServiceRegistry* services)
	: m_Cue(cue)
	, m_StableName(std::move(stableName))
	, m_Scope(scope)
	, m_Mappings(std::move(mappings))
	, m_Services(services)
{
}

bool VansActionServiceGameplayCueAdapter::Validate(std::string& error) const
{
	if (!m_Cue || m_StableName.empty() || !m_Services || !m_Services->IsSealed())
	{
		error = "Gameplay Cue Action Service adapter is not ready";
		return false;
	}
	for (const VansGameplayCueAdapterMapping& mapping : m_Mappings)
	{
		if (!mapping.service || !mapping.command || mapping.serviceName.empty() ||
			mapping.commandName.empty() ||
			mapping.service != VansMakeStableId<VansActionServiceIdTag>(mapping.serviceName) ||
			mapping.command != VansMakeStableId<VansActionFieldIdTag>(mapping.commandName))
		{
			error = "Gameplay Cue contains an invalid Service/Command mapping: " + m_StableName;
			return false;
		}
		const VansActionCommandSchema* command =
			m_Services->ResolveCommandSchema(mapping.service, mapping.command);
		if (!command || (command->resourcePolicy != VansActionCommandResourcePolicy::None &&
			command->resourcePolicy != VansActionCommandResourcePolicy::Create))
		{
			error = "Gameplay Cue primary command is missing or has an invalid resource policy: " +
				mapping.commandName;
			return false;
		}
		if (mapping.parameters.kind != VansSerializedValue::Kind::Object)
		{
			error = "Gameplay Cue adapter parameters must be an object: " + m_StableName;
			return false;
		}
		for (const VansActionCommandFieldSchema& field : command->fields)
			if (field.required && field.defaultValue.IsNull() &&
				!FindObjectField(mapping.parameters, field.name) &&
				!CanSupplyDynamicField(field.name, !mapping.asset.empty(), false))
			{
				error = "Gameplay Cue cannot supply required command field: " + field.name;
				return false;
			}
		if (mapping.updateCommand)
		{
			if (mapping.updateCommandName.empty() || mapping.updateCommand !=
				VansMakeStableId<VansActionFieldIdTag>(mapping.updateCommandName))
			{
				error = "Gameplay Cue update command stable identity is invalid";
				return false;
			}
			const auto* update = m_Services->ResolveCommandSchema(mapping.service, mapping.updateCommand);
			if (!update || update->resourcePolicy != VansActionCommandResourcePolicy::Update)
			{
				error = "Gameplay Cue update command is missing or is not an Update command: " +
					mapping.updateCommandName;
				return false;
			}
		}
		if (mapping.removeCommand)
		{
			if (mapping.removeCommandName.empty() || mapping.removeCommand !=
				VansMakeStableId<VansActionFieldIdTag>(mapping.removeCommandName))
			{
				error = "Gameplay Cue remove command stable identity is invalid";
				return false;
			}
			const auto* remove = m_Services->ResolveCommandSchema(mapping.service, mapping.removeCommand);
			if (!remove || remove->resourcePolicy != VansActionCommandResourcePolicy::Release)
			{
				error = "Gameplay Cue remove command is missing or is not a Release command: " +
					mapping.removeCommandName;
				return false;
			}
		}
	}
	return true;
}

VansActionCommandResult VansActionServiceGameplayCueAdapter::Run(
	const VansGameplayCueAdapterMapping& mapping,
	std::string_view commandName,
	VansActionFieldId commandId,
	const VansGameplayCueParameters& parameters,
	VansGenerationHandle resource) const
{
	if (!m_Services || commandName.empty() || !commandId)
		return { VansActionError::InvalidDefinition, {}, VansSerializedValue::Object({}),
			"Gameplay Cue command is invalid" };
	const VansActionCommandSchema* schema =
		m_Services->ResolveCommandSchema(mapping.service, commandId);
	if (!schema)
		return { VansActionError::Dependency, {}, VansSerializedValue::Object({}),
			"Gameplay Cue command schema is unavailable" };
	VansSerializedValue payload = mapping.parameters;
	const auto setIfDeclared = [&](std::string_view name, VansSerializedValue value)
	{
		const std::string fieldName(name);
		if (FindField(*schema, name) && !FindObjectField(payload, fieldName))
			SetSerializedObjectField(payload, fieldName, std::move(value));
	};
	if (!mapping.asset.empty())
	{
		const auto assetField = std::find_if(schema->fields.begin(), schema->fields.end(),
			[&](const auto& field)
			{
				return field.kind == VansActionCommandValueKind::String &&
					IsAssetField(field.name) && !FindObjectField(payload, field.name);
			});
		if (assetField != schema->fields.end())
			SetSerializedObjectField(payload, assetField->name,
				VansSerializedValue::String(mapping.asset));
	}
	if (resource) setIfDeclared("resource", ResourceValue(resource));
	const VansEntityHandle target = parameters.target.IsValid()
		? parameters.target
		: parameters.context.Entity(VansActionContextSlots::PrimaryTarget);
	setIfDeclared("target", EntityValue(target));
	setIfDeclared("position", VectorValue(parameters.position));
	setIfDeclared("origin", VectorValue(parameters.position));
	setIfDeclared("direction", VectorValue(parameters.direction));
	setIfDeclared("intensity", VansSerializedValue::Float(parameters.intensity));
	setIfDeclared("scale", VansSerializedValue::Float(parameters.intensity));
	setIfDeclared("parameters", parameters.payload);
	setIfDeclared("payload", parameters.payload);
	VansActionCommand command;
	command.service = mapping.service;
	command.command = commandId;
	command.stableName = std::string(commandName);
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
	for (const VansGameplayCueAdapterMapping& mapping : m_Mappings)
	{
		VansActionCommandResult result = Run(mapping, mapping.commandName,
			mapping.command, parameters, {});
		if (!result)
		{
			error = result.message;
			return false;
		}
		if (result.resource)
		{
			auto service = m_Services->Resolve(mapping.service);
			if (!service || !service->Release(result.resource, error)) return false;
		}
	}
	return true;
}

VansGenerationHandle VansActionServiceGameplayCueAdapter::Add(
	const VansGameplayCueKey& key,
	VansGameplayCueScope scope,
	const VansGameplayCueParameters& parameters,
	std::string& error)
{
	ActiveCue cue;
	cue.key = key;
	cue.scope = scope;
	cue.parameters = parameters;
	for (std::size_t index = 0; index < m_Mappings.size(); ++index)
	{
		const auto& mapping = m_Mappings[index];
		VansActionCommandResult result = Run(mapping, mapping.commandName,
			mapping.command, parameters, {});
		if (!result)
		{
			error = result.message;
			for (auto resource = cue.resources.rbegin(); resource != cue.resources.rend(); ++resource)
			{
				std::string ignored;
				ReleaseBound(*resource, &parameters, ignored);
			}
			return {};
		}
		if (result.resource) cue.resources.push_back({ index, result.resource, true });
	}
	return m_Active.Emplace(std::move(cue));
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
	for (const BoundResource& bound : cue->resources)
	{
		if (!bound.active) continue;
		const auto& mapping = m_Mappings[bound.mapping];
		if (!mapping.updateCommand) continue;
		const VansActionCommandResult result = Run(mapping, mapping.updateCommandName,
			mapping.updateCommand, parameters, bound.external);
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
	BoundResource& resource,
	const VansGameplayCueParameters* parameters,
	std::string& error) const
{
	if (!resource.active) return true;
	const auto& mapping = m_Mappings[resource.mapping];
	if (mapping.removeCommand && parameters)
	{
		const VansActionCommandResult result = Run(mapping, mapping.removeCommandName,
			mapping.removeCommand, *parameters, resource.external);
		if (!result)
		{
			error = result.message;
			return false;
		}
	}
	else
	{
		auto service = m_Services ? m_Services->Resolve(mapping.service) : nullptr;
		if (!service || !service->Release(resource.external, error)) return false;
	}
	resource.active = false;
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
	bool success = true;
	for (auto bound = cue->resources.rbegin(); bound != cue->resources.rend(); ++bound)
	{
		std::string releaseError;
		if (!ReleaseBound(*bound, &cue->parameters, releaseError))
		{
			if (error.empty()) error = releaseError;
			success = false;
		}
	}
	return success && m_Active.Release(resource);
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

bool VansGameplayCueRegistry::Seal(std::string& error)
{
	if (m_Adapters.empty())
	{
		error = "Gameplay Cue registry is empty";
		return false;
	}
	m_Sealed = true;
	return true;
}

std::shared_ptr<IVansGameplayCueAdapter> VansGameplayCueRegistry::Resolve(VansCueId cue) const
{
	const auto found = m_Adapters.find(cue);
	return found == m_Adapters.end() ? nullptr : found->second;
}

VansGameplayCueScope VansGameplayCueRegistry::DefaultScope(VansCueId cue) const
{
	const auto adapter = Resolve(cue);
	return adapter ? adapter->DefaultScope() : VansGameplayCueScope::Target;
}

bool VansGameplayCueService::Execute(
	const VansGameplayCueKey& key,
	VansGameplayCueScope scope,
	const VansGameplayCueParameters& parameters,
	std::string& error)
{
	if (!m_Registry || !m_Registry->IsSealed() || !key.IsValid())
	{
		error = "Gameplay Cue service is not ready or key is invalid";
		return false;
	}
	if (m_Executed.find(key) != m_Executed.end()) return true;
	std::shared_ptr<IVansGameplayCueAdapter> adapter = m_Registry->Resolve(key.cue);
	if (!adapter)
	{
		error = "Gameplay Cue adapter is missing";
		return false;
	}
	if (!adapter->Execute(key, scope, parameters, error)) return false;
	m_Executed.insert(key);
	return true;
}

VansCueHandle VansGameplayCueService::Add(
	const VansGameplayCueKey& key,
	VansGameplayCueScope scope,
	const VansGameplayCueParameters& parameters,
	std::uint64_t source,
	std::string& error)
{
	if (!m_Registry || !m_Registry->IsSealed() || !key.IsValid() || source == 0)
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
	const VansGenerationHandle resource = adapter->Add(key, scope, parameters, error);
	if (!resource) return {};
	return { m_Active.Emplace(ActiveCue{ std::move(adapter), resource, key, source }) };
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

VansGameplayCueScope VansGameplayCueService::DefaultScope(VansCueId cue) const
{
	return m_Registry ? m_Registry->DefaultScope(cue) : VansGameplayCueScope::Target;
}

std::size_t VansGameplayCueService::RemoveSource(std::uint64_t source)
{
	std::vector<VansCueHandle> removals;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveCue& cue)
	{
		if (cue.source == source) removals.push_back({ handle });
	});
	std::size_t removed = 0;
	for (VansCueHandle handle : removals)
	{
		std::string ignored;
		if (Remove(handle, ignored)) ++removed;
	}
	return removed;
}

void VansGameplayCueService::Clear()
{
	std::vector<VansCueHandle> removals;
	m_Active.ForEach([&](VansGenerationHandle handle, const ActiveCue&) { removals.push_back({ handle }); });
	for (VansCueHandle handle : removals)
	{
		std::string ignored;
		Remove(handle, ignored);
	}
	m_Executed.clear();
}
}
