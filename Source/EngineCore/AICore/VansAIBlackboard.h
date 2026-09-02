#pragma once

#include "VansAITypes.h"

#include <string>
#include <unordered_map>

namespace Vans
{
class VansAIBlackboard
{
public:
	bool Configure(const std::vector<VansAIBlackboardEntryDefinition>& definitions,
		std::string& error);
	bool Set(const std::string& name, VansAIValue value, std::string* error = nullptr);
	const VansAIValue* Find(const std::string& name) const;

	bool SetBool(const std::string& name, bool value, std::string* error = nullptr);
	bool GetBool(const std::string& name, bool fallback = false) const;
	bool SetEntity(const std::string& name, VansEntityHandle value,
		std::string* error = nullptr);
	VansEntityHandle GetEntity(const std::string& name) const;

private:
	struct Entry
	{
		VansAIValueType type = VansAIValueType::Bool;
		VansAIValue value = false;
	};
	std::unordered_map<std::string, Entry> m_Entries;
};
}
