#include "VansGameplayTags.h"

#include <algorithm>
#include <limits>

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

std::uint64_t TotalTagCount(
	const std::unordered_map<VansGameplayTagContainer::SourceId, std::uint32_t>& counts)
{
	std::uint64_t total = 0;
	for (const auto& [source, count] : counts)
	{
		(void)source;
		total += count;
	}
	return total;
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
	if (m_ById.find(definition.id) != m_ById.end())
	{
		error = "Gameplay Tag stable ID collision: " + name;
		return false;
	}
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
	error.clear();
	std::uint64_t version = 0;
	for (const VansGameplayTagDefinition& definition : m_Definitions)
	{
		if (definition.parent && !Contains(definition.parent))
		{
			error = "Gameplay Tag parent is missing for " + definition.name;
			return false;
		}
		if (definition.replacement && !Contains(definition.replacement))
		{
			error = "deprecated Gameplay Tag replacement is missing for " + definition.name;
			return false;
		}
		version ^= definition.id.value + 0x9e3779b97f4a7c15ull +
			(version << 6) + (version >> 2);
	}
	m_Version = version == 0 ? 1 : version;
	m_Sealed = true;
	return true;
}

bool VansGameplayTagDictionary::Contains(VansGameplayTagId id) const
{
	return m_ById.find(id) != m_ById.end();
}

std::optional<VansGameplayTagId> VansGameplayTagDictionary::FindId(std::string_view name) const
{
	const auto found = m_ByName.find(std::string(name));
	return found == m_ByName.end()
		? std::nullopt
		: std::optional<VansGameplayTagId>{ m_Definitions[found->second].id };
}

std::optional<std::string> VansGameplayTagDictionary::FindName(VansGameplayTagId id) const
{
	const auto found = m_ById.find(id);
	return found == m_ById.end()
		? std::nullopt
		: std::optional<std::string>{ m_Definitions[found->second].name };
}

std::vector<VansGameplayTagDefinition> VansGameplayTagDictionary::Snapshot() const
{
	return m_Definitions;
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
		const auto found = m_ById.find(current);
		if (found == m_ById.end()) return false;
		const VansGameplayTagId parent = m_Definitions[found->second].parent;
		if (!parent) return false;
		current = parent;
	}
	return false;
}

bool VansGameplayTagContainer::Add(VansGameplayTagId tag, SourceId source, std::uint32_t count)
{
	if (!tag || source == 0 || count == 0) return false;
	if (m_Dictionary && !m_Dictionary->Contains(tag)) return false;
	const auto tagIt = m_Counts.find(tag);
	if (tagIt == m_Counts.end())
	{
		std::unordered_map<SourceId, std::uint32_t> sources;
		sources.emplace(source, count);
		m_Counts.emplace(tag, std::move(sources));
		return true;
	}
	const std::uint64_t maximumCount = (std::numeric_limits<std::uint32_t>::max)();
	if (TotalTagCount(tagIt->second) > maximumCount - count) return false;
	const auto [sourceIt, inserted] = tagIt->second.try_emplace(source, count);
	if (!inserted) sourceIt->second += count;
	return true;
}

std::size_t VansGameplayTagContainer::RemoveSource(SourceId source)
{
	if (source == 0) return 0;
	std::size_t removed = 0;
	for (auto tagIt = m_Counts.begin(); tagIt != m_Counts.end();)
	{
		const auto sourceIt = tagIt->second.find(source);
		if (sourceIt != tagIt->second.end())
		{
			tagIt->second.erase(sourceIt);
			++removed;
		}
		if (tagIt->second.empty()) tagIt = m_Counts.erase(tagIt);
		else ++tagIt;
	}
	return removed;
}

std::uint32_t VansGameplayTagContainer::CountExact(VansGameplayTagId tag) const
{
	const auto found = m_Counts.find(tag);
	if (found == m_Counts.end()) return 0;
	return static_cast<std::uint32_t>(TotalTagCount(found->second));
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

bool VansGameplayTagContainer::MatchesWithTags(
	const VansGameplayTagQuery& query,
	const std::vector<VansGameplayTagId>& added) const
{
	const auto has = [&](VansGameplayTagId tag)
	{
		if (Has(tag, query.exact)) return true;
		return std::any_of(added.begin(), added.end(), [&](VansGameplayTagId candidate)
		{
			return candidate == tag || (!query.exact && m_Dictionary &&
				m_Dictionary->IsDescendantOrEqual(candidate, tag));
		});
	};
	for (VansGameplayTagId tag : query.all)
		if (!has(tag)) return false;
	if (!query.any.empty() &&
		std::none_of(query.any.begin(), query.any.end(), has)) return false;
	for (VansGameplayTagId tag : query.none)
		if (has(tag)) return false;
	return true;
}

std::vector<std::pair<VansGameplayTagId, std::uint32_t>>
VansGameplayTagContainer::Snapshot() const
{
	std::vector<std::pair<VansGameplayTagId, std::uint32_t>> result;
	result.reserve(m_Counts.size());
	for (const auto& [tag, sources] : m_Counts)
	{
		const std::uint32_t count = static_cast<std::uint32_t>(TotalTagCount(sources));
		if (count != 0) result.emplace_back(tag, count);
	}
	std::sort(result.begin(), result.end(),
		[](const auto& left, const auto& right) { return left.first < right.first; });
	return result;
}

void VansGameplayTagContainer::Clear()
{
	m_Counts.clear();
}
}
