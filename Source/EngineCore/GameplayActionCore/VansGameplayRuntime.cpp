#include "VansGameplayRuntime.h"

#include "VansActionRoutingService.h"

#include "../SceneRuntime/VansComponentStorage.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../SceneRuntime/VansRuntimeWorld.h"

#include <algorithm>
#include <unordered_set>

namespace Vans
{
bool VansGameplayRuntime::Initialize(
	const std::vector<VansAssetRecord>& records,
	const VansAssetObjectRepository& assetObjects,
	std::string& error)

{
	return Initialize(records, assetObjects, VansGAFSettings{},
		VansGameplayRuntimeDependencies{}, error);
}

bool VansGameplayRuntime::Initialize(
	const std::vector<VansAssetRecord>& records,
	const VansAssetObjectRepository& assetObjects,
	const VansGAFSettings& settings,
	std::string& error)
{
	return Initialize(records, assetObjects, settings,
		VansGameplayRuntimeDependencies{}, error);
}

bool VansGameplayRuntime::Initialize(
	const std::vector<VansAssetRecord>& records,
	const VansAssetObjectRepository& assetObjects,
	const VansGAFSettings& settings,
	const VansGameplayRuntimeDependencies& dependencies,
	std::string& error)
{
	Shutdown();
	if (settings.performance.maximumActiveActionsPerHost == 0 ||
		settings.performance.maximumTasksPerAction == 0 ||
		settings.performance.maximumGraphTransitionsPerTick == 0 ||
		settings.performance.maximumEffectsPerHost == 0 ||
		settings.performance.maximumPayloadBytes == 0)
	{
		error = "Gameplay Runtime GAF performance budget is invalid";
		return false;
	}
	m_Settings = settings;
	m_ExternalCosts.reset();
	m_HostInitializers.clear();
	m_ActionSetInitializers.clear();
	m_GraphNodes = {};
	m_Drivers = {};
	m_Executors = {};
	m_Services = {};
	m_Cues = {};
	m_TargetingHandlers = {};
	m_Types = {};
	m_Schemas = VansGAFSchemaRegistry{};
	m_AssetSchemas = {};
	m_AssetCompilers = {};
	std::vector<std::shared_ptr<const IVansGameplayModuleContributor>> contributors;
	contributors.reserve(dependencies.contributors.size() + 2);
	contributors.push_back(VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor("Core", "GAF Core"),
		VansRegisterCoreGAFTypes,
		VansRegisterCoreGAFSchemas,
		[this](VansGAFRuntimeRegistry& context, std::string& contributionError)
		{
			return context.RegisterExecutorOwnedDriver(
				"Core.Driver.Immediate", contributionError) &&
				context.RegisterService(
				std::make_shared<VansActionRoutingService>(m_Scheduler), contributionError);
		}, VansRegisterCoreGameplayAssetCompilers,
		VansRegisterCoreGameplayAssetSchemas));
	contributors.push_back(VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor("Core.Graph", "GAF Core Graph", { "Core" }),
		{}, {},
		[](VansGAFRuntimeRegistry& context, std::string& contributionError)
		{
			return context.RegisterExecutorOwnedDriver(
				"Core.Driver.Graph", contributionError) &&
				context.RegisterGraphNodes(
				VansRegisterBuiltInActionGraphNodes, contributionError);
		}, {}));
	contributors.insert(contributors.end(), dependencies.contributors.begin(),
		dependencies.contributors.end());
	std::vector<std::shared_ptr<const IVansGameplayModuleContributor>> orderedContributors;
	if (!VansOrderGameplayModuleContributors(contributors, orderedContributors, error))
	{
		m_Assets.Clear();
		return false;
	}
	for (const auto& contributor : orderedContributors)
		if (!contributor->Descriptor().environment.runtime ||
			!contributor->Descriptor().environment.cook)
		{
			error = "GAF Runtime contributor does not support Runtime and Cook: " +
				contributor->Descriptor().moduleId;
			return false;
		}
		else if (!contributor->RegisterTypes(m_Types, error))
		{
			if (error.empty()) error = "GAF module Type registration failed: " +
				contributor->Descriptor().moduleId;
			return false;
		}
	if (!m_Types.Seal(error)) return false;
	m_Schemas.BindTypes(m_Types);
	for (const auto& contributor : orderedContributors)
		if (!contributor->RegisterSchemas(m_Schemas, error))
		{
			if (error.empty()) error = "GAF module Schema registration failed: " +
				contributor->Descriptor().moduleId;
			return false;
		}
	if (!m_Schemas.Seal(error)) return false;
	for (const auto& contributor : orderedContributors)
		if (!contributor->RegisterAssetSchemas(m_AssetSchemas, error))
		{
			if (error.empty()) error = "GAF module Asset Schema registration failed: " +
				contributor->Descriptor().moduleId;
			return false;
		}
	if (!m_AssetSchemas.Seal(error)) return false;
	for (const auto& contributor : orderedContributors)
		if (!contributor->RegisterAssetCompilers(m_AssetCompilers, error))
		{
			if (error.empty()) error = "GAF module asset Compiler registration failed: " +
				contributor->Descriptor().moduleId;
			return false;
		}
	if (!m_AssetCompilers.Seal(error)) return false;
	if (!m_Assets.Load(records, assetObjects, dependencies.sourceOverrides,
		m_AssetSchemas, m_Schemas, m_AssetCompilers, error))
		return false;
	std::unordered_set<std::string> activeModules;
	for (const auto& contributor : orderedContributors)
		activeModules.insert(contributor->Descriptor().moduleId);
	for (const auto& definition : m_Assets.Actions().Definitions())
		for (const std::string& moduleId : definition->program.modules)
			if (activeModules.find(moduleId) == activeModules.end())
			{
				error = "Action requires an inactive GAF module: " +
					definition->name + " -> " + moduleId;
				m_Assets.Clear();
				return false;
			}
	VansGAFRuntimeRegistry contributionContext(
		m_Assets, m_Services, m_GraphNodes, m_Drivers, m_TargetingHandlers, m_ExternalCosts,
		m_HostInitializers, m_ActionSetInitializers);
	for (const auto& contributor : orderedContributors)
		if (!contributor->RegisterRuntime(contributionContext, error))
		{
			if (error.empty()) error = "Gameplay module contribution failed: " +
				contributor->Descriptor().moduleId;
			m_Assets.Clear();
			return false;
		}
	if (!m_GraphNodes.Seal(error) || !m_Drivers.Seal(error) ||
		!m_TargetingHandlers.Seal(error) ||
		!VansRegisterBuiltInActionExecutors(m_Executors, &m_GraphNodes, error,
			m_Settings.performance.maximumGraphTransitionsPerTick) ||
		!m_Executors.Seal(error) || !m_Services.Seal(error))
	{
		m_Assets.Clear();
		return false;
	}
	for (const auto& definition : m_Assets.Actions().Definitions())
	{
		for (const VansCompiledActionRecord& driver : definition->program.execute.drivers)
			if (!m_Drivers.Contains(driver.type))
			{
				error = "Action Driver implementation is missing: " +
					definition->name + " -> " + driver.type;
				m_Assets.Clear();
				return false;
			}
		for (const std::string& capability : definition->program.capabilities)
			if (!m_Services.Resolve(
				VansMakeStableId<VansActionServiceIdTag>(capability)))
			{
				error = "Action capability is missing: " +
					definition->name + " -> " + capability;
				m_Assets.Clear();
				return false;
			}
	}
	for (const VansCompiledGameplayCueDefinition& cue : m_Assets.Cues())
	{
		auto adapter = std::make_shared<VansActionServiceGameplayCueAdapter>(
			cue.id, cue.name, cue.scope, cue.adapterMappings, &m_Services);
		if (!adapter->Validate(error) || !m_Cues.Register(std::move(adapter), error))
		{
			m_Assets.Clear();
			return false;
		}
	}
	if (!m_Assets.Cues().empty() && !m_Cues.Seal(error))
	{
		m_Assets.Clear();
		return false;
	}
	m_Initialized = true;
	return true;
}

void VansGameplayRuntime::Shutdown()
{
	m_Scheduler.Clear();
	std::vector<std::string> resourceErrors;
	m_WorldResources.ReleaseAll(resourceErrors);
	m_WorldResources = {};
	m_Cues = {};
	m_ExternalCosts.reset();
	m_HostInitializers.clear();
	m_ActionSetInitializers.clear();
	m_Initialized = false;
}

std::uint64_t VansGameplayRuntime::SourceFor(VansEntityHandle owner, std::uint32_t slot)
{
	std::uint64_t source = (static_cast<std::uint64_t>(owner.generation) << 48) ^
		(static_cast<std::uint64_t>(owner.index + 1) << 16) ^ static_cast<std::uint64_t>(slot + 1);
	return source == 0 ? 1 : source;
}

std::shared_ptr<VansActionHost> VansGameplayRuntime::CreateHost(
	VansEntityHandle owner,
	const VansGameplayActionHostSetup& setup,
	std::string& error)
{
	if (!m_Initialized || !owner.IsValid())
	{
		error = "Gameplay Runtime is not initialized or owner is invalid";
		return {};
	}
	VansActionHostDependencies dependencies;
	dependencies.definitions = &m_Assets.Actions();
	dependencies.executors = &m_Executors;
	dependencies.drivers = &m_Drivers;
	dependencies.tagDictionary = &m_Assets.Tags();
	dependencies.attributeRegistry = &m_Assets.Attributes();
	dependencies.effectRegistry = m_Assets.Effects();
	dependencies.cueRegistry = m_Cues.IsSealed() ? &m_Cues : nullptr;
	dependencies.targetingPolicies = m_Assets.TargetingPolicies();
	dependencies.targetingHandlers = &m_TargetingHandlers;
	dependencies.services = &m_Services;
	dependencies.actionSetInitializers = &m_ActionSetInitializers;
	dependencies.externalCosts = m_ExternalCosts.get();
	dependencies.worldResources = &m_WorldResources;
	dependencies.limits.maximumActiveActions = m_Settings.performance.maximumActiveActionsPerHost;
	dependencies.limits.maximumTasksPerAction = m_Settings.performance.maximumTasksPerAction;
	dependencies.limits.maximumActiveEffects = m_Settings.performance.maximumEffectsPerHost;
	dependencies.limits.maximumPayloadBytes = m_Settings.performance.maximumPayloadBytes;
	auto host = std::make_shared<VansActionHost>(owner, dependencies);
	if (!host->Initialize(error)) return {};

	std::uint32_t sourceSlot = 0;
	for (const VansGameplayHostInitializer& initializer : setup.initializers)
	{
		const auto found = m_HostInitializers.find(initializer.type);
		if (found == m_HostInitializers.end())
		{
			error = "ActionHost initializer type is not registered: " + initializer.type;
			return {};
		}
		if (initializer.inputs.kind != VansSerializedValue::Kind::Object ||
			!found->second(*host, m_Assets, owner, SourceFor(owner, sourceSlot++),
				initializer.inputs, error)) return {};
	}
	for (const std::string& reference : setup.actionSets)
	{
		const VansActionSetDefinition* set = m_Assets.ResolveActionSet(reference);
		if (!set || !host->ApplyActionSet(*set, error))
		{
			if (error.empty()) error = "ActionHost ActionSet is unresolved: " + reference;
			return {};
		}
	}
	for (const VansGameplayDirectGrant& initial : setup.grants)
	{
		const auto action = m_Assets.ResolveAction(initial.action);
		if (!action)
		{
			error = "ActionHost direct grant is unresolved: " + initial.action;
			return {};
		}
		VansActionGrantDesc grant;
		grant.action = action->id;
		grant.actionReference = initial.action;
		grant.extensions = initial.extensions;
		grant.source = SourceFor(owner, sourceSlot++);
		if (!host->Grant(grant, error)) return {};
	}
	for (const std::string& reference : setup.autoActivate)
	{
		const auto action = m_Assets.ResolveAction(reference);
		if (!action)
		{
			error = "ActionHost auto activation is unresolved: " + reference;
			return {};
		}
		const std::vector<VansGrantedActionSpecSnapshot> granted = host->GrantedActions();
		const auto spec = std::find_if(granted.begin(), granted.end(), [&](const auto& candidate)
		{
			return candidate.action == action->id;
		});
		if (spec == granted.end())
		{
			error = "ActionHost auto activation requires a granted Action: " + reference;
			return {};
		}
		VansActionActivationRequest request;
		request.spec = spec->handle;
		request.context.SetEntity(VansActionContextSlots::Owner, owner);
		request.context.SetEntity(VansActionContextSlots::Instigator, owner);
		request.context.SetEntity(VansActionContextSlots::PrimaryTarget, owner);
		const VansActionResult result = host->Activate(request);
		if (!result)
		{
			error = "ActionHost auto activation failed: " + result.message;
			return {};
		}
	}
	host->SetEnabled(setup.enabled);
	if (!m_Scheduler.Register(host, error)) return {};
	return host;
}

void VansGameplayRuntime::SynchronizeHostEnablement(VansRuntimeWorld& world)
{
	auto* storage = static_cast<VansComponentStorage<VansRuntimeActionHostComponent>*>(
		world.FindStorage(VansRuntimeComponentType_ActionHost));
	if (!storage) return;
	const auto& headers = storage->Headers();
	auto& components = storage->DenseData();
	const std::size_t count = std::min(headers.size(), components.size());
	for (std::size_t index = 0; index < count; ++index)
		if (components[index].host) components[index].host->SetEnabled(headers[index].effectiveEnabled);
}
}
