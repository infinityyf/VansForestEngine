#pragma once

#include "VansActionHost.h"
#include "VansActionScheduler.h"
#include "../GameplayActionSchema/VansGameplayAssetLibrary.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../GameplayActionExecution/VansActionExecutionGraph.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Vans
{
class VansRuntimeWorld;

struct VansGameplayInitialTag
{
	std::string tag;
	std::uint32_t count = 1;
};

struct VansGameplayInitialAttribute
{
	std::string attribute;
	double value = 0.0;
};

struct VansGameplayDirectGrant
{
	std::string action;
	double level = 1.0;
	std::string inputBinding;
	std::vector<std::string> dynamicTags;
	std::int32_t charges = -1;
	VansActionGrantPersistence persistence = VansActionGrantPersistence::OwnerLifetime;
};

struct VansGameplayActionHostSetup
{
	bool enabled = true;
	std::vector<std::string> actionSets;
	std::vector<VansGameplayDirectGrant> grants;
	std::vector<VansGameplayInitialTag> initialTags;
	std::vector<VansGameplayInitialAttribute> initialAttributes;
	std::vector<std::string> autoActivate;
};

struct VansGameplayRuntimeDependencies
{
	bool externalNetworkTransportAvailable = false;
	std::vector<VansGameplayAssetSourceOverride> sourceOverrides;
	std::vector<std::shared_ptr<IVansActionService>> services;
	std::shared_ptr<IVansActionExternalCostProvider> externalCosts;
	using ServiceFactory = std::function<std::shared_ptr<IVansActionService>(
		const VansGameplayAssetLibrary&, std::string&)>;
	std::vector<ServiceFactory> serviceFactories;
	using GraphNodeRegistrar = std::function<bool(VansActionGraphNodeRegistry&, std::string&)>;
	std::vector<GraphNodeRegistrar> graphNodeRegistrars;
	using TargetingHandlerRegistrar = std::function<bool(VansTargetingHandlerRegistry&, std::string&)>;
	std::vector<TargetingHandlerRegistrar> targetingHandlerRegistrars;
};

class VansGameplayRuntime
{
public:
	bool Initialize(const std::vector<VansAssetRecord>& records, std::string& error);
	bool Initialize(const std::vector<VansAssetRecord>& records,
		const VansGAFSettings& settings, std::string& error);
	bool Initialize(const std::vector<VansAssetRecord>& records,
		const VansGAFSettings& settings,
		const VansGameplayRuntimeDependencies& dependencies,
		std::string& error);
	void Shutdown();
	std::shared_ptr<VansActionHost> CreateHost(
		VansEntityHandle owner,
		const VansGameplayActionHostSetup& setup,
		std::string& error);
	void SynchronizeHostEnablement(VansRuntimeWorld& world);
	std::shared_ptr<VansActionHost> FindHost(VansEntityHandle owner) const
	{
		return m_Scheduler.FindByOwner(owner);
	}
	std::vector<std::shared_ptr<VansActionHost>> Hosts() const { return m_Scheduler.Hosts(); }
	void TickEarly(double deltaSeconds) { m_Scheduler.TickEarly(deltaSeconds); }
	bool RunLateContinuation() { return m_Scheduler.RunLateContinuation(); }

	bool IsInitialized() const { return m_Initialized; }
	const VansGameplayAssetLibrary& Assets() const { return m_Assets; }
	const VansGAFSettings& Settings() const { return m_Settings; }
	const VansActionServiceRegistry& Services() const { return m_Services; }

private:
	static std::uint64_t SourceFor(VansEntityHandle owner, std::uint32_t slot);

	VansGameplayAssetLibrary m_Assets;
	VansActionGraphNodeRegistry m_GraphNodes;
	VansActionExecutorRegistry m_Executors;
	VansActionServiceRegistry m_Services;
	std::shared_ptr<IVansActionExternalCostProvider> m_ExternalCosts;
	VansGameplayCueRegistry m_Cues;
	VansTargetingHandlerRegistry m_TargetingHandlers;
	VansActionScheduler m_Scheduler;
	VansGAFSettings m_Settings;
	bool m_Initialized = false;
};
}
