#include "VansGameplayModuleContributor.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>

namespace Vans
{
namespace
{
void HashDescriptorText(std::uint64_t& hash, std::string_view value)
{
	for (unsigned char character : value)
	{
		hash ^= character;
		hash *= 1099511628211ull;
	}
	hash ^= 0xffu;
	hash *= 1099511628211ull;
}
}

VansGAFModuleDescriptor VansMakeGAFModuleDescriptor(
	std::string moduleId,
	std::string displayName,
	std::vector<std::string> requiredModules,
	std::vector<std::string> optionalModules,
	VansGAFModuleSource source,
	VansGAFModuleEnvironment environment)
{
	VansGAFModuleDescriptor descriptor;
	descriptor.moduleId = std::move(moduleId);
	descriptor.displayName = std::move(displayName);
	descriptor.requiredModules = std::move(requiredModules);
	descriptor.optionalModules = std::move(optionalModules);
	descriptor.source = source;
	descriptor.environment = environment;
	std::uint64_t fingerprint = 1469598103934665603ull;
	HashDescriptorText(fingerprint, descriptor.moduleId);
	HashDescriptorText(fingerprint, descriptor.displayName);
	for (const std::string& dependency : descriptor.requiredModules)
		HashDescriptorText(fingerprint, dependency);
	for (const std::string& dependency : descriptor.optionalModules)
		HashDescriptorText(fingerprint, dependency);
	fingerprint ^= static_cast<std::uint64_t>(descriptor.source);
	fingerprint *= 1099511628211ull;
	for (bool flag : { descriptor.environment.runtime, descriptor.environment.cook,
		descriptor.environment.editor, descriptor.deterministicRegistration,
		descriptor.threadSafeRuntime })
	{
		fingerprint ^= flag ? 1u : 0u;
		fingerprint *= 1099511628211ull;
	}
	descriptor.registrationFingerprint = fingerprint;
	return descriptor;
}

bool VansGAFRuntimeRegistry::RegisterService(
	std::shared_ptr<IVansActionService> service,
	std::string& error)
{
	return m_Services.Register(std::move(service), error);
}

bool VansGAFRuntimeRegistry::InstantiateService(
	const ServiceFactory& factory,
	std::string& error)
{
	if (!factory)
	{
		error = "Gameplay module contains an invalid Action capability factory";
		return false;
	}
	std::shared_ptr<IVansActionService> service = factory(m_Assets, error);
	if (!service)
	{
		if (error.empty()) error = "Gameplay module Action capability factory failed";
		return false;
	}
	return RegisterService(std::move(service), error);
}

bool VansGAFRuntimeRegistry::RegisterGraphNodes(
	const GraphNodeRegistrar& registrar,
	std::string& error)
{
	if (!registrar)
	{
		error = "Gameplay module contains an invalid Graph node contributor";
		return false;
	}
	return registrar(m_GraphNodes, error);
}

bool VansGAFRuntimeRegistry::RegisterExecutorOwnedDriver(
	std::string typeId,
	std::string& error)
{
	return m_Drivers.RegisterExecutorOwned(std::move(typeId), error);
}

bool VansGAFRuntimeRegistry::RegisterSidecarDriver(
	std::string typeId,
	VansActionDriverRegistry::Factory factory,
	std::string& error)
{
	return m_Drivers.RegisterSidecar(std::move(typeId), std::move(factory), error);
}

bool VansGAFRuntimeRegistry::RegisterTargetingHandlers(
	const TargetingHandlerRegistrar& registrar,
	std::string& error)
{
	if (!registrar)
	{
		error = "Gameplay module contains an invalid Targeting contributor";
		return false;
	}
	return registrar(m_TargetingHandlers, error);
}

bool VansGAFRuntimeRegistry::ProvideExternalCosts(
	std::shared_ptr<IVansActionExternalCostProvider> provider,
	std::string& error)
{
	if (!provider)
	{
		error = "Gameplay module external cost provider is invalid";
		return false;
	}
	if (m_ExternalCosts)
	{
		error = "Gameplay modules registered more than one external cost provider";
		return false;
	}
	m_ExternalCosts = std::move(provider);
	return true;
}

bool VansGAFRuntimeRegistry::RegisterHostInitializer(
	std::string typeId,
	HostInitializer initializer,
	std::string& error)
{
	if (typeId.empty() || !initializer)
	{
		error = "Gameplay module Host initializer is invalid";
		return false;
	}
	if (!m_HostInitializers.emplace(std::move(typeId), std::move(initializer)).second)
	{
		error = "duplicate Gameplay module Host initializer";
		return false;
	}
	return true;
}

bool VansGAFRuntimeRegistry::RegisterActionSetInitializer(
	std::string typeId,
	VansActionSetInitializerHandler initializer,
	std::string& error)
{
	if (typeId.empty() || !initializer)
	{
		error = "Gameplay module ActionSet initializer is invalid";
		return false;
	}
	if (!m_ActionSetInitializers.emplace(std::move(typeId), std::move(initializer)).second)
	{
		error = "duplicate Gameplay module ActionSet initializer";
		return false;
	}
	return true;
}

namespace
{
class VansGameplayModuleContributor final : public IVansGameplayModuleContributor
{
public:
	VansGameplayModuleContributor(
		VansGAFModuleDescriptor descriptor,
		VansGameplayModuleTypeContribution typeContribution,
		VansGameplayModuleSchemaContribution schemaContribution,
		VansGameplayModuleRuntimeContribution runtimeContribution,
		VansGameplayModuleAssetCompilerContribution assetCompilerContribution,
		VansGameplayModuleAssetSchemaContribution assetSchemaContribution)
		: m_Descriptor(std::move(descriptor))
		, m_TypeContribution(std::move(typeContribution))
		, m_SchemaContribution(std::move(schemaContribution))
		, m_RuntimeContribution(std::move(runtimeContribution))
		, m_AssetCompilerContribution(std::move(assetCompilerContribution))
		, m_AssetSchemaContribution(std::move(assetSchemaContribution))
	{
	}

	const VansGAFModuleDescriptor& Descriptor() const override { return m_Descriptor; }
	bool RegisterTypes(VansGAFTypeRegistry& registry, std::string& error) const override
	{
		return !m_TypeContribution || m_TypeContribution(registry, error);
	}
	bool RegisterSchemas(VansGAFSchemaRegistry& registry, std::string& error) const override
	{
		return !m_SchemaContribution || m_SchemaContribution(registry, error);
	}
	bool RegisterRuntime(VansGAFRuntimeRegistry& registry, std::string& error) const override
	{
		return !m_RuntimeContribution || m_RuntimeContribution(registry, error);
	}
	bool RegisterAssetCompilers(
		VansGameplayAssetCompilerRegistry& registry, std::string& error) const override
	{
		return !m_AssetCompilerContribution || m_AssetCompilerContribution(registry, error);
	}
	bool RegisterAssetSchemas(
		VansGameplayAssetSchemaRegistry& registry, std::string& error) const override
	{
		return !m_AssetSchemaContribution || m_AssetSchemaContribution(registry, error);
	}

private:
	VansGAFModuleDescriptor m_Descriptor;
	VansGameplayModuleTypeContribution m_TypeContribution;
	VansGameplayModuleSchemaContribution m_SchemaContribution;
	VansGameplayModuleRuntimeContribution m_RuntimeContribution;
	VansGameplayModuleAssetCompilerContribution m_AssetCompilerContribution;
	VansGameplayModuleAssetSchemaContribution m_AssetSchemaContribution;
};
}

std::shared_ptr<const IVansGameplayModuleContributor> VansMakeGAFModuleContributor(
	VansGAFModuleDescriptor descriptor,
	VansGameplayModuleTypeContribution typeContribution,
	VansGameplayModuleSchemaContribution schemaContribution,
	VansGameplayModuleRuntimeContribution runtimeContribution,
	VansGameplayModuleAssetCompilerContribution assetCompilerContribution,
	VansGameplayModuleAssetSchemaContribution assetSchemaContribution)
{
	return std::make_shared<VansGameplayModuleContributor>(
		std::move(descriptor), std::move(typeContribution),
		std::move(schemaContribution), std::move(runtimeContribution),
		std::move(assetCompilerContribution), std::move(assetSchemaContribution));
}

bool VansOrderGameplayModuleContributors(
	const std::vector<std::shared_ptr<const IVansGameplayModuleContributor>>& contributors,
	std::vector<std::shared_ptr<const IVansGameplayModuleContributor>>& ordered,
	std::string& error)
{
	ordered.clear();
	std::map<std::string, std::shared_ptr<const IVansGameplayModuleContributor>> modules;
	for (const auto& contributor : contributors)
	{
		if (!contributor || contributor->Descriptor().moduleId.empty() ||
			contributor->Descriptor().displayName.empty() ||
			contributor->Descriptor().registrationFingerprint == 0)
		{
			error = "Gameplay module contributor identity is invalid";
			return false;
		}
		const std::string name(contributor->Descriptor().moduleId);
		if (!modules.emplace(name, contributor).second)
		{
			error = "duplicate Gameplay module contributor: " + name;
			return false;
		}
	}
	std::unordered_map<std::string, std::size_t> indegree;
	std::unordered_map<std::string, std::vector<std::string>> dependents;
	for (const auto& [name, contributor] : modules)
	{
		indegree.emplace(name, 0);
		std::set<std::string> uniqueDependencies;
		for (const std::string& dependency : contributor->Descriptor().requiredModules)
		{
			if (dependency.empty() || dependency == name || !uniqueDependencies.insert(dependency).second)
			{
				error = "Gameplay module dependency is invalid: " + name;
				return false;
			}
			if (modules.find(dependency) == modules.end())
			{
				error = "Gameplay module dependency is missing: " + name + " -> " + dependency;
				return false;
			}
			++indegree[name];
			dependents[dependency].push_back(name);
		}
		for (const std::string& dependency : contributor->Descriptor().optionalModules)
		{
			if (dependency.empty() || dependency == name ||
				!uniqueDependencies.insert(dependency).second)
			{
				error = "Gameplay module optional dependency is invalid: " + name;
				return false;
			}
			if (modules.find(dependency) == modules.end()) continue;
			++indegree[name];
			dependents[dependency].push_back(name);
		}
	}
	std::set<std::string> ready;
	for (const auto& [name, count] : indegree)
		if (count == 0) ready.insert(name);
	while (!ready.empty())
	{
		const std::string name = *ready.begin();
		ready.erase(ready.begin());
		ordered.push_back(modules.at(name));
		auto found = dependents.find(name);
		if (found == dependents.end()) continue;
		std::sort(found->second.begin(), found->second.end());
		for (const std::string& dependent : found->second)
			if (--indegree[dependent] == 0) ready.insert(dependent);
	}
	if (ordered.size() != modules.size())
	{
		error = "Gameplay module dependency graph contains a cycle";
		ordered.clear();
		return false;
	}
	return true;
}
}
