#pragma once

#include "VansAITypes.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansAIBlackboardDebugEntry
{
	std::string name;
	VansAIValueType type = VansAIValueType::Bool;
	VansAIValue value = false;
	std::string lastWriter;
};

struct VansAIBlackboardDebugSnapshot
{
	std::size_t totalEntries = 0;
	bool truncated = false;
	std::vector<VansAIBlackboardDebugEntry> entries;
};

class VansAIBlackboard
{
public:
	bool Configure(const std::vector<VansAIBlackboardEntryDefinition>& definitions,
		std::string& error);
	bool Set(const std::string& name, VansAIValue value,
		const std::string& writer, std::string* error = nullptr);
	const VansAIValue* Find(const std::string& name) const;
	bool Has(const std::string& name, VansAIValueType type) const;

	bool SetBool(const std::string& name, bool value,
		const std::string& writer, std::string* error = nullptr);
	bool GetBool(const std::string& name, bool fallback = false) const;
	bool SetEntity(const std::string& name, VansEntityHandle value,
		const std::string& writer, std::string* error = nullptr);
	VansEntityHandle GetEntity(const std::string& name) const;
	VansAIBlackboardDebugSnapshot CaptureDebugSnapshot(
		std::size_t maxEntries) const;

private:
	struct Entry
	{
		VansAIValueType type = VansAIValueType::Bool;
		VansAIValue value = false;
		std::string lastWriter;
	};
	std::unordered_map<std::string, Entry> m_Entries;
};
}
