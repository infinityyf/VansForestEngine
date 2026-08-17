#pragma once

#include "../GameplayActionSchema/VansGameplaySchemaTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Vans
{
struct VansGameplayTagDefinition
{
	VansGameplayTagId id;
	VansGameplayTagId parent;
	std::string name;
	std::string description;
	bool deprecated = false;
	VansGameplayTagId replacement;
};

class VansGameplayTagDictionary
{
public:
	bool Register(
		std::string name,
		std::string description,
		bool deprecated,
		std::string replacement,
		std::string& error);
	bool Seal(std::string& error);
	const VansGameplayTagDefinition* Resolve(VansGameplayTagId id) const;
	const VansGameplayTagDefinition* Find(std::string_view name) const;
	bool IsDescendantOrEqual(VansGameplayTagId candidate, VansGameplayTagId ancestor) const;
	std::vector<VansGameplayTagId> ExpandWildcard(std::string_view pattern) const;
	const std::vector<VansGameplayTagDefinition>& Definitions() const { return m_Definitions; }
	std::uint64_t Version() const { return m_Version; }
	bool IsSealed() const { return m_Sealed; }

private:
	bool m_Sealed = false;
	std::uint64_t m_Version = 0;
	std::vector<VansGameplayTagDefinition> m_Definitions;
	std::unordered_map<VansGameplayTagId, std::size_t> m_ById;
	std::unordered_map<std::string, std::size_t> m_ByName;
};

struct VansGameplayTagQuery
{
	std::vector<VansGameplayTagId> all;
	std::vector<VansGameplayTagId> any;
	std::vector<VansGameplayTagId> none;
	bool exact = false;
};

class VansGameplayTagContainer
{
public:
	using SourceId = std::uint64_t;
	using ChangedCallback = std::function<void(const std::vector<VansGameplayTagId>&)>;

	explicit VansGameplayTagContainer(const VansGameplayTagDictionary* dictionary = nullptr)
		: m_Dictionary(dictionary) {}

	void SetDictionary(const VansGameplayTagDictionary* dictionary) { m_Dictionary = dictionary; }
	bool Add(VansGameplayTagId tag, SourceId source, std::uint32_t count = 1);
	bool Remove(VansGameplayTagId tag, SourceId source, std::uint32_t count = 1);
	std::size_t RemoveSource(SourceId source);
	std::uint32_t CountExact(VansGameplayTagId tag) const;
	bool Has(VansGameplayTagId tag, bool exact = false) const;
	bool Matches(const VansGameplayTagQuery& query) const;
	std::vector<std::pair<VansGameplayTagId, std::uint32_t>> Snapshot() const;
	void BeginBatch();
	void EndBatch();
	void Clear();
	void SetChangedCallback(ChangedCallback callback) { m_Changed = std::move(callback); }

private:
	void MarkChanged(VansGameplayTagId tag);
	void FlushChanged();

	const VansGameplayTagDictionary* m_Dictionary = nullptr;
	std::unordered_map<VansGameplayTagId, std::unordered_map<SourceId, std::uint32_t>> m_Counts;
	std::unordered_set<VansGameplayTagId> m_PendingChanges;
	ChangedCallback m_Changed;
	std::uint32_t m_BatchDepth = 0;
};
}
