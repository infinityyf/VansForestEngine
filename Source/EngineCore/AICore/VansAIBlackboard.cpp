#include "VansAIBlackboard.h"

#include <utility>

namespace Vans
{
namespace
{
bool MatchesType(VansAIValueType type, const VansAIValue& value)
{
	switch (type)
	{
	case VansAIValueType::Bool: return std::holds_alternative<bool>(value);
	case VansAIValueType::Int: return std::holds_alternative<std::int64_t>(value);
	case VansAIValueType::Float: return std::holds_alternative<double>(value);
	case VansAIValueType::Vector3: return std::holds_alternative<glm::vec3>(value);
	case VansAIValueType::Entity: return std::holds_alternative<VansEntityHandle>(value);
	default: return false;
	}
}
}

bool VansAIBlackboard::Configure(
	const std::vector<VansAIBlackboardEntryDefinition>& definitions,
	std::string& error)
{
	m_Entries.clear();
	for (const VansAIBlackboardEntryDefinition& definition : definitions)
	{
		if (definition.name.empty() || !MatchesType(definition.type, definition.defaultValue))
		{
			error = "AI Blackboard definition is empty or has a mismatched default value";
			m_Entries.clear();
			return false;
		}
		if (!m_Entries.emplace(definition.name,
			Entry{ definition.type, definition.defaultValue }).second)
		{
			error = "Duplicate AI Blackboard entry: " + definition.name;
			m_Entries.clear();
			return false;
		}
	}
	error.clear();
	return true;
}

bool VansAIBlackboard::Set(const std::string& name, VansAIValue value, std::string* error)
{
	const auto found = m_Entries.find(name);
	if (found == m_Entries.end())
	{
		if (error) *error = "Unknown AI Blackboard entry: " + name;
		return false;
	}
	if (!MatchesType(found->second.type, value))
	{
		if (error) *error = "AI Blackboard type mismatch: " + name;
		return false;
	}
	found->second.value = std::move(value);
	if (error) error->clear();
	return true;
}

const VansAIValue* VansAIBlackboard::Find(const std::string& name) const
{
	const auto found = m_Entries.find(name);
	return found == m_Entries.end() ? nullptr : &found->second.value;
}

bool VansAIBlackboard::SetBool(const std::string& name, bool value, std::string* error)
{
	return Set(name, VansAIValue(value), error);
}

bool VansAIBlackboard::GetBool(const std::string& name, bool fallback) const
{
	const VansAIValue* value = Find(name);
	const bool* boolean = value ? std::get_if<bool>(value) : nullptr;
	return boolean ? *boolean : fallback;
}

bool VansAIBlackboard::SetEntity(
	const std::string& name, VansEntityHandle value, std::string* error)
{
	return Set(name, VansAIValue(value), error);
}

VansEntityHandle VansAIBlackboard::GetEntity(const std::string& name) const
{
	const VansAIValue* value = Find(name);
	const VansEntityHandle* entity = value ? std::get_if<VansEntityHandle>(value) : nullptr;
	return entity ? *entity : VansEntityHandle{};
}
}
