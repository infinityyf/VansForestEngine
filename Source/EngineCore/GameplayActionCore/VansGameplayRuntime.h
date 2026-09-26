#pragma once

#include "VansActionHost.h"
#include "VansActionScheduler.h"
#include "VansGameplayServiceRuntime.h"
#include "VansGameplayModuleContributor.h"
#include "../GameplayActionSchema/VansGameplayAssetLibrary.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../GameplayActionExecution/VansActionExecutionGraph.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
class VansRuntimeWorld;
class VansAssetObjectRepository;

struct VansGameplayHostInitializer
{
	std::string type;
	VansSerializedValue inputs = VansSerializedValue::Object({});
};

struct VansGameplayDirectGrant
{
	std::string action;
	std::vector<VansCompiledActionRecord> extensions;
};

struct VansGameplayActionHostSetup
{
	bool enabled = true;
	std::vector<std::string> actionSets;
	std::vector<VansGameplayDirectGrant> grants;
	std::vector<VansGameplayHostInitializer> initializers;
	std::vector<std::string> autoActivate;
};

struct VansGameplayRuntimeDependencies
{
	std::vector<VansGameplayAssetSourceOverride> sourceOverrides;
	std::vector<std::shared_ptr<const IVansGameplayModuleContributor>> contributors;
	const VansGAFProjectConfiguration* projectConfiguration = nullptr;
};

class VansGameplayRuntime : public IVansGameplayServiceRuntime
{
public:
	bool Initialize(const std::vector<VansAssetRecord>& records,
		const VansAssetObjectRepository& assetObjects, std::string& error);
	bool Initialize(const std::vector<VansAssetRecord>& records,
		const VansAssetObjectRepository& assetObjects,
		const VansGAFSettings& settings, std::string& error);
	bool Initialize(const std::vector<VansAssetRecord>& records,
		const VansAssetObjectRepository& assetObjects,
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
	bool HasHost(VansEntityHandle owner) const override;
	bool IsActionActive(VansEntityHandle owner, VansActionHandle action) const override;
	bool HasTag(VansEntityHandle owner, VansGameplayTagId tag) const override;
	bool EnqueueActionEvent(VansEntityHandle owner, VansActionHandle action,
		VansActionEvent event, std::string& error) override;
	bool PublishGameplayEvent(VansEntityHandle owner, VansActionEvent event) override;
	bool ReadAttribute(VansEntityHandle owner, VansAttributeId attribute,
		double& value) const override;
	bool ApplyBaseAttribute(VansEntityHandle owner, VansAttributeId attribute,
		VansAttributeBaseOperation operation, double magnitude, double& value) override;
	VansActionResult ActivateAction(VansEntityHandle owner, VansActionId action,
		VansActionContext context, VansTargetData targetData) override;
	void TickEarly(double deltaSeconds)
	{
		m_Scheduler.TickEarly(deltaSeconds);
		m_Services.Tick(deltaSeconds);
	}
	bool RunLateContinuation() { return m_Scheduler.RunLateContinuation(); }

	bool IsInitialized() const { return m_Initialized; }
	const VansGameplayAssetLibrary& Assets() const { return m_Assets; }
	const VansGAFSettings& Settings() const { return m_Settings; }
	const VansActionServiceRegistry& Services() const { return m_Services; }
	// A service may retire an already completed World-owned resource without
	// invoking its destruction callback again. Action/Host ownership is untouched.
	bool ForgetCompletedWorldResource(VansActionServiceId service,
		VansGenerationHandle resource) override;

private:
	static std::uint64_t SourceFor(VansEntityHandle owner, std::uint32_t slot);

	VansGameplayAssetLibrary m_Assets;
	VansGameplayAssetSchemaRegistry m_AssetSchemas;
	VansGAFTypeRegistry m_Types;
	VansGAFSchemaRegistry m_Schemas;
	VansGameplayAssetCompilerRegistry m_AssetCompilers;
	VansActionGraphNodeRegistry m_GraphNodes;
	VansActionDriverRegistry m_Drivers;
	VansActionExecutorRegistry m_Executors;
	VansActionServiceRegistry m_Services;
	VansActionResourceLedger m_WorldResources;
	std::shared_ptr<IVansActionExternalCostProvider> m_ExternalCosts;
	std::unordered_map<std::string, VansGAFRuntimeRegistry::HostInitializer> m_HostInitializers;
	std::unordered_map<std::string, VansActionSetInitializerHandler> m_ActionSetInitializers;
	bool m_RuntimeRegistrySealed = false;
	VansGameplayCueRegistry m_Cues;
	VansTargetingHandlerRegistry m_TargetingHandlers;
	VansActionScheduler m_Scheduler;
	VansGAFSettings m_Settings;
	bool m_Initialized = false;
};
}
