#include "VansGameplayTags.h"

#include <algorithm>

namespace Vans
{
namespace
{
bool IsValidTagName(std::string_view name)
{
	if (name.empty() || name.front() == '.' || name.back() == '.') return false;
	bool previousDot = false;
	for (const char character : name)
	{
		const bool dot = character == '.';
		if (dot && previousDot) return false;
		const bool valid = dot || character == '_' || character == '-' ||
			(character >= 'a' && character <= 'z') ||
			(character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9');
		if (!valid) return false;
		previousDot = dot;
	}
	return true;
}

std::string ParentName(std::string_view name)
{
	const std::size_t separator = name.rfind('.');
	return separator == std::string_view::npos ? std::string() : std::string(name.substr(0, separator));
}
}

bool VansGameplayTagDictionary::Register(
	std::string name,
	std::string description,
	bool deprecated,
	std::string replacement,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "Gameplay Tag dictionary is sealed";
		return false;
	}
	if (!IsValidTagName(name))
	{
		error = "invalid Gameplay Tag name: " + name;
		return false;
	}
	if (m_ByName.find(name) != m_ByName.end())
	{
		error = "duplicate Gameplay Tag: " + name;
		return false;
	}
	VansGameplayTagDefinition definition;
	definition.id = VansMakeStableId<VansGameplayTagIdTag>(name);
	definition.name = std::move(name);
	definition.description = std::move(description);
	definition.deprecated = deprecated;
	if (!replacement.empty())
		definition.replacement = VansMakeStableId<VansGameplayTagIdTag>(replacement);
	const std::string parentName = ParentName(definition.name);
	if (!parentName.empty())
		definition.parent = VansMakeStableId<VansGameplayTagIdTag>(parentName);
	const std::size_t index = m_Definitions.size();
	m_ById.emplace(definition.id, index);
	m_ByName.emplace(definition.name, index);
	m_Definitions.push_back(std::move(definition));
	return true;
}

bool VansGameplayTagDictionary::Seal(std::string& error)
{
	if (m_Sealed) return true;
	for (const VansGameplayTagDefinition& definition : m_Definitions)
	{
		if (definition.parent && !Resolve(definition.parent))
		{
			error = "Gameplay Tag parent is missing for " + definition.name;
			return false;
		}
		if (definition.replacement && !Resolve(definition.replacement))
		{
			error = "deprecated Gameplay Tag replacement is missing for " + definition.name;
			return false;
		}
		m_Version ^= definition.id.value + 0x9e3779b97f4a7c15ull +
			(m_Version << 6) + (m_Version >> 2);
	}
	if (m_Version == 0) m_Version = 1;
	m_Sealed = true;
	return true;
}

const VansGameplayTagDefinition* VansGameplayTagDictionary::Resolve(VansGameplayTagId id) const
{
	const auto found = m_ById.find(id);
	return found == m_ById.end() ? nullptr : &m_Definitions[found->second];
}

const VansGameplayTagDefinition* VansGameplayTagDictionary::Find(std::string_view name) const
{
	const auto found = m_ByName.find(std::string(name));
	return found == m_ByName.end() ? nullptr : &m_Definitions[found->second];
}

bool VansGameplayTagDictionary::IsDescendantOrEqual(
	VansGameplayTagId candidate,
	VansGameplayTagId ancestor) const
{
	if (!candidate || !ancestor) return false;
	VansGameplayTagId current = candidate;
	for (std::size_t depth = 0; depth <= m_Definitions.size(); ++depth)
	{
		if (current == ancestor) return true;
		const VansGameplayTagDefinition* definition = Resolve(current);
		if (!definition || !definition->parent) return false;
		current = definition->parent;
	}
	return false;
}

std::vector<VansGameplayTagId> VansGameplayTagDictionary::ExpandWildcard(std::string_view pattern) const
{
	std::vector<VansGameplayTagId> result;
	const bool wildcard = pattern.size() >= 2 && pattern.substr(pattern.size() - 2) == ".*";
	const std::string_view prefix = wildcard ? pattern.substr(0, pattern.size() - 1) : pattern;
	for (const VansGameplayTagDefinition& definition : m_Definitions)
	{
		if ((!wildcard && definition.name == pattern) ||
			(wildcard && definition.name.size() > prefix.size() &&
				definition.name.compare(0, prefix.size(), prefix) == 0))
			result.push_back(definition.id);
	}
	return result;
}

bool VansGameplayTagContainer::Add(VansGameplayTagId tag, SourceId source, std::uint32_t count)
{
	if (!tag || source == 0 || count == 0) return false;
	if (m_Dictionary && !m_Dictionary->Resolve(tag)) return false;
	m_Counts[tag][source] += count;
	MarkChanged(tag);
	return true;
}

bool VansGameplayTagContainer::Remove(VansGameplayTagId tag, SourceId source, std::uint32_t count)
{
	if (!tag || source == 0 || count == 0) return false;
	const auto tagIt = m_Counts.find(tag);
	if (tagIt == m_Counts.end()) return false;
	const auto sourceIt = tagIt->second.find(source);
	if (sourceIt == tagIt->second.end() || sourceIt->second < count) return false;
	sourceIt->second -= count;
	if (sourceIt->second == 0) tagIt->second.erase(sourceIt);
	if (tagIt->second.empty()) m_Counts.erase(tagIt);
	MarkChanged(tag);
	return true;
}

std::size_t VansGameplayTagContainer::RemoveSource(SourceId source)
{
	if (source == 0) return 0;
	BeginBatch();
	std::size_t removed = 0;
	for (auto tagIt = m_Counts.begin(); tagIt != m_Counts.end();)
	{
		const auto sourceIt = tagIt->second.find(source);
		if (sourceIt != tagIt->second.end())
		{
			tagIt->second.erase(sourceIt);
			MarkChanged(tagIt->first);
			++removed;
		}
		if (tagIt->second.empty()) tagIt = m_Counts.erase(tagIt);
		else ++tagIt;
	}
	EndBatch();
	return removed;
}

std::uint32_t VansGameplayTagContainer::CountExact(VansGameplayTagId tag) const
{
	const auto found = m_Counts.find(tag);
	if (found == m_Counts.end()) return 0;
	std::uint32_t total = 0;
	for (const auto& sourceCount : found->second) total += sourceCount.second;
	return total;
}

bool VansGameplayTagContainer::Has(VansGameplayTagId tag, bool exact) const
{
	if (CountExact(tag) > 0) return true;
	if (exact || !m_Dictionary) return false;
	for (const auto& entry : m_Counts)
		if (!entry.second.empty() && m_Dictionary->IsDescendantOrEqual(entry.first, tag)) return true;
	return false;
}

bool VansGameplayTagContainer::Matches(const VansGameplayTagQuery& query) const
{
	for (VansGameplayTagId tag : query.all)
		if (!Has(tag, query.exact)) return false;
	if (!query.any.empty())
	{
		bool matched = false;
		for (VansGameplayTagId tag : query.any) matched = matched || Has(tag, query.exact);
		if (!matched) return false;
	}
	for (VansGameplayTagId tag : query.none)
		if (Has(tag, query.exact)) return false;
	return true;
}

std::vector<std::pair<VansGameplayTagId, std::uint32_t>>
VansGameplayTagContainer::Snapshot() const
{
	std::vector<std::pair<VansGameplayTagId, std::uint32_t>> result;
	result.reserve(m_Counts.size());
	for (const auto& [tag, sources] : m_Counts)
	{
		std::uint32_t count = 0;
		for (const auto& [source, value] : sources) { (void)source; count += value; }
		if (count != 0) result.emplace_back(tag, count);
	}
	std::sort(result.begin(), result.end(),
		[](const auto& left, const auto& right) { return left.first < right.first; });
	return result;
}

void VansGameplayTagContainer::BeginBatch()
{
	++m_BatchDepth;
}

void VansGameplayTagContainer::EndBatch()
{
	if (m_BatchDepth == 0) return;
	--m_BatchDepth;
	if (m_BatchDepth == 0) FlushChanged();
}

void VansGameplayTagContainer::Clear()
{
	BeginBatch();
	for (const auto& entry : m_Counts) MarkChanged(entry.first);
	m_Counts.clear();
	EndBatch();
}

void VansGameplayTagContainer::MarkChanged(VansGameplayTagId tag)
{
	m_PendingChanges.insert(tag);
	if (m_BatchDepth == 0) FlushChanged();
}

void VansGameplayTagContainer::FlushChanged()
{
	if (m_PendingChanges.empty()) return;
	std::vector<VansGameplayTagId> changed(m_PendingChanges.begin(), m_PendingChanges.end());
	std::sort(changed.begin(), changed.end());
	m_PendingChanges.clear();
	if (m_Changed) m_Changed(changed);
}
}
