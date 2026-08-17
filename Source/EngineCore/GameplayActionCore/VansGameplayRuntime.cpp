#include "VansGameplayRuntime.h"

#include "VansActionRoutingService.h"

#include "../SceneRuntime/VansComponentStorage.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../SceneRuntime/VansRuntimeWorld.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
bool VansGameplayRuntime::Initialize(
	const std::vector<VansAssetRecord>& records,
	std::string& error)

{
	return Initialize(records, VansGAFSettings{}, VansGameplayRuntimeDependencies{}, error);
}

bool VansGameplayRuntime::Initialize(
	const std::vector<VansAssetRecord>& records,
	const VansGAFSettings& settings,
	std::string& error)
{
	return Initialize(records, settings, VansGameplayRuntimeDependencies{}, error);
}

bool VansGameplayRuntime::Initialize(
	const std::vector<VansAssetRecord>& records,
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
	if (settings.networkMode == VansGAFNetworkMode::ExternalTransport &&
		settings.failWithoutTransport && !dependencies.externalNetworkTransportAvailable)
	{
		error = "Gameplay Runtime requires an external Action transport, but none was provided";
		return false;
	}
	m_Settings = settings;
	m_ExternalCosts = dependencies.externalCosts;
	m_GraphNodes = {};
	m_Executors = {};
	m_Services = {};
	m_Cues = {};
	m_TargetingHandlers = {};
	if (!m_Assets.Load(records, dependencies.sourceOverrides, error)) return false;
	if (settings.predictionEnabled && settings.requireRollbackPlan &&
		!m_Assets.ValidatePredictionRollbackPolicy(error))
	{
		m_Assets.Clear();
		return false;
	}
	if (!m_Services.Register(std::make_shared<VansActionRoutingService>(m_Scheduler), error))
	{
		m_Assets.Clear();
		return false;
	}
	for (const std::shared_ptr<IVansActionService>& service : dependencies.services)
		if (!m_Services.Register(service, error))
		{
			m_Assets.Clear();
			return false;
		}
	for (const VansGameplayRuntimeDependencies::ServiceFactory& factory :
		dependencies.serviceFactories)
	{
		if (!factory)
		{
			error = "Gameplay Runtime contains an invalid Action Service factory";
			m_Assets.Clear();
			return false;
		}
		std::shared_ptr<IVansActionService> service = factory(m_Assets, error);
		if (!service || !m_Services.Register(std::move(service), error))
		{
			if (error.empty()) error = "Gameplay Runtime Action Service factory failed";
			m_Assets.Clear();
			return false;
		}
	}
	if (!VansRegisterBuiltInActionGraphNodes(m_GraphNodes, error))
	{
		m_Assets.Clear();
		return false;
	}
	if (!VansRegisterBuiltInTargetingHandlers(m_TargetingHandlers, error))
	{
		m_Assets.Clear();
		return false;
	}
	for (const VansGameplayRuntimeDependencies::TargetingHandlerRegistrar& registrar :
		dependencies.targetingHandlerRegistrars)
	{
		if (!registrar || !registrar(m_TargetingHandlers, error))
		{
			if (error.empty()) error = "Gameplay Runtime Targeting handler registrar failed";
			m_Assets.Clear();
			return false;
		}
	}
	for (const VansGameplayRuntimeDependencies::GraphNodeRegistrar& registrar :
		dependencies.graphNodeRegistrars)
	{
		if (!registrar || !registrar(m_GraphNodes, error))
		{
			if (error.empty()) error = "Gameplay Runtime Graph node registrar failed";
			m_Assets.Clear();
			return false;
		}
	}
	if (!m_GraphNodes.Seal(error) || !m_TargetingHandlers.Seal(error) ||
		!VansRegisterBuiltInActionExecutors(m_Executors, &m_GraphNodes, error,
			m_Settings.performance.maximumGraphTransitionsPerTick) ||
		!m_Executors.Seal(error) || !m_Services.Seal(error))
	{
		m_Assets.Clear();
		return false;
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
	m_Cues = {};
	m_ExternalCosts.reset();
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
	dependencies.tagDictionary = &m_Assets.Tags();
	dependencies.attributeRegistry = &m_Assets.Attributes();
	dependencies.effectRegistry = m_Assets.Effects();
	dependencies.cueRegistry = m_Cues.IsSealed() ? &m_Cues : nullptr;
	dependencies.targetingPolicies = m_Assets.TargetingPolicies();
	dependencies.targetingHandlers = &m_TargetingHandlers;
	dependencies.services = &m_Services;
	dependencies.externalCosts = m_ExternalCosts.get();
	dependencies.predictionEnabled = m_Settings.predictionEnabled &&
		m_Settings.networkMode != VansGAFNetworkMode::Disabled;
	dependencies.limits.maximumActiveActions = m_Settings.performance.maximumActiveActionsPerHost;
	dependencies.limits.maximumTasksPerAction = m_Settings.performance.maximumTasksPerAction;
	dependencies.limits.maximumActiveEffects = m_Settings.performance.maximumEffectsPerHost;
	dependencies.limits.maximumPayloadBytes = m_Settings.performance.maximumPayloadBytes;
	auto host = std::make_shared<VansActionHost>(owner, dependencies);
	if (!host->Initialize(error)) return {};

	std::uint32_t sourceSlot = 0;
	for (const VansGameplayInitialTag& initial : setup.initialTags)
	{
		const VansGameplayTagDefinition* tag = m_Assets.Tags().Find(initial.tag);
		if (!tag || initial.count == 0 ||
			!host->Tags().Add(tag->id, SourceFor(owner, sourceSlot++), initial.count))
		{
			error = "ActionHost initial Tag is invalid: " + initial.tag;
			return {};
		}
	}
	for (const VansGameplayInitialAttribute& initial : setup.initialAttributes)
	{
		const VansAttributeId attribute = VansMakeStableId<VansAttributeIdTag>(initial.attribute);
		if (!std::isfinite(initial.value) || !m_Assets.Attributes().Resolve(attribute) ||
			!host->Attributes().SetBase(attribute, initial.value))
		{
			error = "ActionHost initial Attribute is invalid: " + initial.attribute;
			return {};
		}
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
		grant.level = initial.level;
		grant.inputBinding = initial.inputBinding;
		grant.charges = initial.charges;
		grant.persistence = initial.persistence;
		grant.source = SourceFor(owner, sourceSlot++);
		for (const std::string& tagName : initial.dynamicTags)
		{
			const VansGameplayTagDefinition* tag = m_Assets.Tags().Find(tagName);
			if (!tag)
			{
				error = "ActionHost grant dynamic Tag is unresolved: " + tagName;
				return {};
			}
			grant.dynamicTags.push_back(tag->id);
		}
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
		request.context.owner = owner;
		request.context.instigator = owner;
		request.context.primaryTarget = owner;
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
