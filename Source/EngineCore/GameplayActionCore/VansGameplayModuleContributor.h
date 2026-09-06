#pragma once

#include "VansActionHost.h"
#include "VansActionServices.h"
#include "VansGAFExtensionRegistry.h"
#include "../GameplayActionExecution/VansActionExecutionGraph.h"
#include "../GameplayActionSchema/VansGameplayAssetLibrary.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansGAFModuleSource : std::uint8_t
{
	Engine,
	Plugin,
	Project
};

struct VansGAFModuleEnvironment
{
	bool runtime = true;
	bool cook = true;
	bool editor = false;
};

struct VansGAFModuleDescriptor
{
	std::string moduleId;
	std::string displayName;
	std::vector<std::string> requiredModules;
	std::vector<std::string> optionalModules;
	VansGAFModuleSource source = VansGAFModuleSource::Engine;
	VansGAFModuleEnvironment environment;
	bool deterministicRegistration = true;
	bool threadSafeRuntime = false;
	std::uint64_t registrationFingerprint = 0;
};

VansGAFModuleDescriptor VansMakeGAFModuleDescriptor(
	std::string moduleId,
	std::string displayName,
	std::vector<std::string> requiredModules = {},
	std::vector<std::string> optionalModules = {},
	VansGAFModuleSource source = VansGAFModuleSource::Engine,
	VansGAFModuleEnvironment environment = {});

class VansGAFRuntimeRegistry
{
public:
	using ServiceFactory = std::function<std::shared_ptr<IVansActionService>(
		const VansGameplayAssetLibrary&, std::string&)>;
	using GraphNodeRegistrar = std::function<bool(VansActionGraphNodeRegistry&, std::string&)>;
	using TargetingHandlerRegistrar =
		std::function<bool(VansTargetingHandlerRegistry&, std::string&)>;
	using HostInitializer = std::function<bool(
		VansActionHost&,
		const VansGameplayAssetLibrary&,
		VansEntityHandle,
		std::uint64_t,
		const VansSerializedValue&,
		std::string&)>;

	VansGAFRuntimeRegistry(
		const VansGameplayAssetLibrary& assets,
		VansActionServiceRegistry& services,
		VansActionGraphNodeRegistry& graphNodes,
		VansActionDriverRegistry& drivers,
		VansTargetingHandlerRegistry& targetingHandlers,
		std::shared_ptr<IVansActionExternalCostProvider>& externalCosts,
		std::unordered_map<std::string, HostInitializer>& hostInitializers,
		std::unordered_map<std::string, VansActionSetInitializerHandler>&
			actionSetInitializers)
		: m_Assets(assets)
		, m_Services(services)
		, m_GraphNodes(graphNodes)
		, m_Drivers(drivers)
		, m_TargetingHandlers(targetingHandlers)
		, m_ExternalCosts(externalCosts)
		, m_HostInitializers(hostInitializers)
		, m_ActionSetInitializers(actionSetInitializers)
	{
	}

	const VansGameplayAssetLibrary& Assets() const { return m_Assets; }
	bool RegisterService(std::shared_ptr<IVansActionService> service, std::string& error);
	bool InstantiateService(const ServiceFactory& factory, std::string& error);
	bool RegisterGraphNodes(const GraphNodeRegistrar& registrar, std::string& error);
	bool RegisterExecutorOwnedDriver(std::string typeId, std::string& error);
	bool RegisterSidecarDriver(
		std::string typeId, VansActionDriverRegistry::Factory factory, std::string& error);
	bool RegisterTargetingHandlers(
		const TargetingHandlerRegistrar& registrar, std::string& error);
	bool ProvideExternalCosts(
		std::shared_ptr<IVansActionExternalCostProvider> provider, std::string& error);
	bool RegisterHostInitializer(
		std::string typeId,
		HostInitializer initializer,
		std::string& error);
	bool RegisterActionSetInitializer(
		std::string typeId,
		VansActionSetInitializerHandler initializer,
		std::string& error);

private:
	const VansGameplayAssetLibrary& m_Assets;
	VansActionServiceRegistry& m_Services;
	VansActionGraphNodeRegistry& m_GraphNodes;
	VansActionDriverRegistry& m_Drivers;
	VansTargetingHandlerRegistry& m_TargetingHandlers;
	std::shared_ptr<IVansActionExternalCostProvider>& m_ExternalCosts;
	std::unordered_map<std::string, HostInitializer>& m_HostInitializers;
	std::unordered_map<std::string, VansActionSetInitializerHandler>&
		m_ActionSetInitializers;
};

class IVansGameplayModuleContributor
{
public:
	virtual ~IVansGameplayModuleContributor() = default;
	virtual const VansGAFModuleDescriptor& Descriptor() const = 0;
	virtual bool RegisterTypes(VansGAFTypeRegistry& registry, std::string& error) const = 0;
	virtual bool RegisterSchemas(VansGAFSchemaRegistry& registry, std::string& error) const = 0;
	virtual bool RegisterAssetCompilers(
		VansGameplayAssetCompilerRegistry& registry, std::string& error) const = 0;
	virtual bool RegisterAssetSchemas(
		VansGameplayAssetSchemaRegistry& registry, std::string& error) const = 0;
	virtual bool RegisterRuntime(VansGAFRuntimeRegistry& registry, std::string& error) const = 0;
};

using VansGameplayModuleTypeContribution =
	std::function<bool(VansGAFTypeRegistry&, std::string&)>;
using VansGameplayModuleSchemaContribution =
	std::function<bool(VansGAFSchemaRegistry&, std::string&)>;
using VansGameplayModuleRuntimeContribution =
	std::function<bool(VansGAFRuntimeRegistry&, std::string&)>;
using VansGameplayModuleAssetCompilerContribution =
	std::function<bool(VansGameplayAssetCompilerRegistry&, std::string&)>;
using VansGameplayModuleAssetSchemaContribution =
	std::function<bool(VansGameplayAssetSchemaRegistry&, std::string&)>;

std::shared_ptr<const IVansGameplayModuleContributor> VansMakeGAFModuleContributor(
	VansGAFModuleDescriptor descriptor,
	VansGameplayModuleTypeContribution typeContribution,
	VansGameplayModuleSchemaContribution schemaContribution,
	VansGameplayModuleRuntimeContribution runtimeContribution,
	VansGameplayModuleAssetCompilerContribution assetCompilerContribution = {},
	VansGameplayModuleAssetSchemaContribution assetSchemaContribution = {});

bool VansOrderGameplayModuleContributors(
	const std::vector<std::shared_ptr<const IVansGameplayModuleContributor>>& contributors,
	std::vector<std::shared_ptr<const IVansGameplayModuleContributor>>& ordered,
	std::string& error);
}
