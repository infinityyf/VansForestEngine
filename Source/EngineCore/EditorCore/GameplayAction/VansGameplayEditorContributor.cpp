#include "VansGameplayEditorContributor.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace Vans
{
bool VansGAFEditorRegistry::Register(
	VansGAFEditorDescriptor descriptor,
	std::string& error)
{
	if (m_Sealed)
	{
		error = "GAF Editor registry is sealed";
		return false;
	}
	if (descriptor.typeId.empty() || descriptor.displayName.empty() ||
		descriptor.category.empty())
	{
		error = "GAF Editor descriptor is invalid";
		return false;
	}
	if (!m_Descriptors.emplace(descriptor.typeId, std::move(descriptor)).second)
	{
		error = "duplicate GAF Editor TypeId";
		return false;
	}
	return true;
}

bool VansGAFEditorRegistry::Seal(std::string& error)
{
	if (m_Descriptors.empty())
	{
		error = "GAF Editor registry is empty";
		return false;
	}
	m_Sealed = true;
	return true;
}

const VansGAFEditorDescriptor* VansGAFEditorRegistry::Resolve(std::string_view typeId) const
{
	const auto found = m_Descriptors.find(std::string(typeId));
	return found == m_Descriptors.end() ? nullptr : &found->second;
}

std::vector<VansGAFEditorDescriptor> VansGAFEditorRegistry::Descriptors() const
{
	std::vector<VansGAFEditorDescriptor> result;
	result.reserve(m_Descriptors.size());
	for (const auto& entry : m_Descriptors) result.push_back(entry.second);
	std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
	{
		return left.typeId < right.typeId;
	});
	return result;
}

namespace
{
class VansGameplayEditorContributor final : public IVansGameplayEditorContributor
{
public:
	VansGameplayEditorContributor(
		VansGAFModuleDescriptor descriptor,
		VansGameplayEditorContribution contribution)
		: m_Descriptor(std::move(descriptor))
		, m_Contribution(std::move(contribution))
	{
	}

	const VansGAFModuleDescriptor& Descriptor() const override { return m_Descriptor; }
	bool RegisterEditor(VansGAFEditorRegistry& registry, std::string& error) const override
	{
		return !m_Contribution || m_Contribution(registry, error);
	}

private:
	VansGAFModuleDescriptor m_Descriptor;
	VansGameplayEditorContribution m_Contribution;
};
}

std::shared_ptr<const IVansGameplayEditorContributor> VansMakeGAFEditorContributor(
	VansGAFModuleDescriptor descriptor,
	VansGameplayEditorContribution contribution)
{
	return std::make_shared<VansGameplayEditorContributor>(
		std::move(descriptor), std::move(contribution));
}

bool VansOrderGameplayEditorContributors(
	const std::vector<std::shared_ptr<const IVansGameplayEditorContributor>>& contributors,
	std::vector<std::shared_ptr<const IVansGameplayEditorContributor>>& ordered,
	std::string& error)
{
	ordered.clear();
	std::map<std::string, std::shared_ptr<const IVansGameplayEditorContributor>> modules;
	for (const auto& contributor : contributors)
	{
		if (!contributor || contributor->Descriptor().moduleId.empty() ||
			contributor->Descriptor().displayName.empty() ||
			contributor->Descriptor().registrationFingerprint == 0)
		{
			error = "GAF Editor contributor identity is invalid";
			return false;
		}
		const std::string moduleId = contributor->Descriptor().moduleId;
		if (!modules.emplace(moduleId, contributor).second)
		{
			error = "duplicate GAF Editor contributor: " + moduleId;
			return false;
		}
	}

	std::map<std::string, std::size_t> indegree;
	std::map<std::string, std::vector<std::string>> dependents;
	for (const auto& entry : modules)
	{
		const std::string& moduleId = entry.first;
		indegree.emplace(moduleId, 0);
		std::set<std::string> unique;
		for (const std::string& dependency : entry.second->Descriptor().requiredModules)
		{
			if (dependency.empty() || dependency == moduleId || !unique.insert(dependency).second ||
				modules.find(dependency) == modules.end())
			{
				error = "GAF Editor module dependency is invalid: " + moduleId;
				return false;
			}
			++indegree[moduleId];
			dependents[dependency].push_back(moduleId);
		}
		for (const std::string& dependency : entry.second->Descriptor().optionalModules)
		{
			if (dependency.empty() || dependency == moduleId || !unique.insert(dependency).second)
			{
				error = "GAF Editor optional dependency is invalid: " + moduleId;
				return false;
			}
			if (modules.find(dependency) == modules.end()) continue;
			++indegree[moduleId];
			dependents[dependency].push_back(moduleId);
		}
	}

	std::set<std::string> ready;
	for (const auto& entry : indegree)
		if (entry.second == 0) ready.insert(entry.first);
	while (!ready.empty())
	{
		const std::string moduleId = *ready.begin();
		ready.erase(ready.begin());
		ordered.push_back(modules.at(moduleId));
		auto found = dependents.find(moduleId);
		if (found == dependents.end()) continue;
		std::sort(found->second.begin(), found->second.end());
		for (const std::string& dependent : found->second)
			if (--indegree[dependent] == 0) ready.insert(dependent);
	}
	if (ordered.size() != modules.size())
	{
		error = "GAF Editor module dependency graph contains a cycle";
		ordered.clear();
		return false;
	}
	return true;
}
}
