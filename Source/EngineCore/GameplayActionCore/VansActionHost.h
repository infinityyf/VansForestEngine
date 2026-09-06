#pragma once

#include "VansActionDefinition.h"
#include "VansActionResourceLedger.h"
#include "VansActionServices.h"
#include "../GameplayActionExecution/VansActionExecution.h"
#include "../GameplayAttributes/VansGameplayAttributes.h"
#include "../GameplayCues/VansGameplayCues.h"
#include "../GameplayEffects/VansGameplayEffects.h"
#include "../GameplayTags/VansGameplayTags.h"
#include "../GameplayTargeting/VansGameplayTargeting.h"
#include "../RuntimeCore/VansGenerationPool.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansActionRuntimeProjection;
class VansActionHost;

using VansActionSetInitializerCleanup =
	std::function<bool(VansActionHost&, std::string&)>;
using VansActionSetInitializerHandler = std::function<bool(
	VansActionHost&,
	std::uint64_t,
	const VansSerializedValue&,
	VansActionSetInitializerCleanup&,
	std::string&)>;

enum class VansActionGrantPersistence : std::uint8_t
{
	Transient,
	OwnerLifetime,
	Persistent
};

enum class VansActionRevokePolicy : std::uint8_t
{
	KeepRunning,
	CancelRunning,
	DeferUntilIdle
};

struct VansActionGrantDesc
{
	VansActionId action;
	std::vector<VansCompiledActionRecord> extensions;
	std::uint64_t source = 0;
	std::string actionReference;
};

struct VansActionSetDefinition
{
	VansActionSetId id;
	std::string name;
	std::vector<VansActionGrantDesc> grants;
	std::vector<VansCompiledActionRecord> initializers;
	std::vector<VansCompiledActionRecord> policies;
};

struct VansGrantedActionSpecSnapshot
{
	VansActionSpecHandle handle;
	VansActionId action;
	std::vector<VansCompiledActionRecord> extensions;
	std::uint64_t source = 0;
	bool pendingRemoval = false;
};

enum class VansActionInstanceState : std::uint8_t
{
	Created,
	Queued,
	Resolving,
	BuildingContext,
	Validating,
	Preparing,
	Committing,
	Committed,
	Running,
	Waiting,
	Transitioning,
	Ending,
	Ended
};

struct VansActionTraceEntry
{
	double elapsedSeconds = 0.0;
	VansActionInstanceState state = VansActionInstanceState::Created;
	std::string message;
};

struct VansActionVariableSnapshot
{
	VansActionFieldId field;
	VansSerializedValue value;
};

struct VansActionDebugEventSnapshot
{
	std::uint64_t sequence = 0;
	VansActionFieldId type;
	std::string stableName;
};

struct VansActionInstanceSnapshot
{
	VansActionHandle handle;
	VansActionId action;
	VansActionSpecHandle sourceSpec;
	VansActionInstanceState state = VansActionInstanceState::Created;
	VansActionEndReason endReason = VansActionEndReason::Completed;
	double elapsedSeconds = 0.0;
	std::size_t taskCount = 0;
	std::size_t resourceCount = 0;
	std::uint64_t correlationId = 0;
	std::vector<VansActionTraceEntry> trace;
	VansActionError error = VansActionError::None;
	VansActionContext context;
	VansTargetData targetData;
	bool hasTargetData = false;
	std::vector<VansActionVariableSnapshot> variables;
	std::vector<VansActionTaskSnapshot> tasks;
	std::vector<VansActionResourceSnapshot> resources;
	VansActionExecutorDebugView executor;
	std::vector<VansActionDebugEventSnapshot> recentEvents;
};

struct VansActionActivationRequest
{
	VansActionSpecHandle spec;
	VansActionContext context;
};

struct VansActionStartedEvent
{
	VansEntityHandle owner;
	VansActionHandle action;
	VansActionId definition;
	std::uint64_t correlationId = 0;
};

struct VansActionQueuedEvent
{
	VansEntityHandle owner;
	VansActionHandle action;
	VansActionId definition;
	VansActionConcurrencyGroupId group;
	std::uint64_t correlationId = 0;
};

struct VansActionEndedEvent
{
	VansEntityHandle owner;
	VansActionHandle action;
	VansActionId definition;
	VansActionEndReason reason = VansActionEndReason::Completed;
	VansActionError error = VansActionError::None;
	std::uint64_t correlationId = 0;
};

struct VansActionHostLimits
{
	std::size_t maximumActiveActions = 64;
	std::size_t maximumTasksPerAction = 64;
	std::size_t maximumActiveEffects = 256;
	std::size_t maximumPayloadBytes = 4096;
};

struct VansActionExternalCostRequest
{
	std::string operation;
	std::string resource;
	double amount = 0.0;
	VansActionContext context;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

class IVansActionExternalCostProvider
{
public:
	virtual ~IVansActionExternalCostProvider() = default;
	virtual bool CanCommit(const VansActionExternalCostRequest& request,
		std::string& error) const = 0;
	virtual bool Commit(const VansActionExternalCostRequest& request,
		std::string& error) = 0;
};

struct VansPersistentActionGrantState
{
	VansActionId action;
	std::vector<VansCompiledActionRecord> extensions;
	std::uint64_t source = 0;
};

struct VansPersistentActionCooldownState
{
	VansActionId action;
	double remainingSeconds = 0.0;
	VansGameplayTagId tag;
};

struct VansActionHostPersistentState
{
	std::vector<VansPersistentActionGrantState> grants;
	std::vector<VansAttributeSnapshot> attributes;
	std::vector<VansPersistentActionCooldownState> cooldowns;
};

struct VansActionHostDependencies
{
	const VansActionDefinitionRegistry* definitions = nullptr;
	const VansActionExecutorRegistry* executors = nullptr;
	const VansActionDriverRegistry* drivers = nullptr;
	const VansGameplayTagDictionary* tagDictionary = nullptr;
	const VansAttributeRegistry* attributeRegistry = nullptr;
	const VansEffectRegistry* effectRegistry = nullptr;
	const VansGameplayCueRegistry* cueRegistry = nullptr;
	const VansTargetingPolicyRegistry* targetingPolicies = nullptr;
	const VansTargetingHandlerRegistry* targetingHandlers = nullptr;
	const VansActionServiceRegistry* services = nullptr;
	const std::unordered_map<std::string, VansActionSetInitializerHandler>*
		actionSetInitializers = nullptr;
	IVansActionExternalCostProvider* externalCosts = nullptr;
	VansActionResourceLedger* worldResources = nullptr;
	VansActionHostLimits limits;
};

class VansActionHost
{
public:
	VansActionHost(VansEntityHandle owner, VansActionHostDependencies dependencies);
	~VansActionHost();

	bool Initialize(std::string& error);
	void Shutdown();
	VansActionSpecHandle Grant(const VansActionGrantDesc& desc, std::string& error);
	bool Revoke(VansActionSpecHandle spec, VansActionRevokePolicy policy, std::string& error);
	VansActionSetHandle ApplyActionSet(const VansActionSetDefinition& set, std::string& error);
	bool RevokeActionSet(VansActionSetHandle set, std::string& error);
	std::size_t RevokeSource(std::uint64_t source, VansActionRevokePolicy policy);

	VansActionResult Activate(const VansActionActivationRequest& request);
	VansActionResult ActivateAction(VansActionId action, VansActionContext context);
	VansActionResult ActivateInput(std::string_view inputBinding, VansActionContext context);
	VansActionResult CanActivate(VansActionSpecHandle spec,
		const VansActionContext& context) const;
	VansActionResult CanActivateAction(VansActionId action,
		const VansActionContext& context) const;
	VansActionResult RequestTransition(
		VansActionHandle source,
		VansActionId targetAction,
		VansActionContext context,
		VansSerializedValue contextPatch,
		bool cancelSource);
	bool Cancel(VansActionHandle action, VansActionCancelReason reason, std::string& error);
	bool Interrupt(VansActionHandle action, std::string& error);
	bool EnqueueEvent(VansActionHandle action, VansActionEvent event, std::string& error);
	VansTargetDataHandle StoreTargetData(VansTargetData data) { return m_TargetData.Store(std::move(data)); }
	const VansTargetData* ResolveTargetData(VansTargetDataHandle handle) const
		{ return m_TargetData.Resolve(handle); }
	bool ReleaseTargetData(VansTargetDataHandle handle) { return m_TargetData.Release(handle); }
	bool ReadVariable(VansActionHandle action, VansActionFieldId variable,
		VansSerializedValue& value, std::string& error) const;
	bool WriteVariable(VansActionHandle action, VansActionFieldId variable,
		VansSerializedValue value, std::string& error);
	bool CapturePersistentState(VansActionHostPersistentState& state, std::string& error) const;
	bool RestorePersistentState(const VansActionHostPersistentState& state, std::string& error);
	void Tick(double deltaSeconds);
	bool RunLateContinuation();
	void SetEnabled(bool enabled);

	std::optional<VansActionInstanceSnapshot> Query(VansActionHandle action) const;
	std::vector<VansActionInstanceSnapshot> ActiveActions() const;
	std::vector<VansGrantedActionSpecSnapshot> GrantedActions() const;
	bool IsCooldownActive(VansActionId action) const;
	VansEntityHandle Owner() const { return m_Owner; }
	bool IsInitialized() const { return m_Initialized; }
	bool IsEnabled() const { return m_Enabled; }

	VansGameplayTagContainer& Tags() { return m_Tags; }
	const VansGameplayTagContainer& Tags() const { return m_Tags; }
	VansAttributeService& Attributes() { return m_Attributes; }
	const VansAttributeService& Attributes() const { return m_Attributes; }
	VansGameplayEffectService& Effects() { return m_Effects; }
	const VansGameplayEffectService& Effects() const { return m_Effects; }
	VansGameplayCueService& Cues() { return m_Cues; }
	VansTargetDataStore& TargetData() { return m_TargetData; }

private:
	struct GrantedSpec
	{
		std::shared_ptr<const VansCompiledActionDefinition> definition;
		std::shared_ptr<const VansActionRuntimeProjection> runtime;
		std::vector<VansCompiledActionRecord> extensions;
		double level = 1.0;
		std::string inputBinding;
		std::vector<VansGameplayTagId> dynamicTags;
		std::int32_t charges = -1;
		std::uint64_t source = 0;
		VansActionGrantPersistence persistence = VansActionGrantPersistence::OwnerLifetime;
		bool pendingRemoval = false;
	};

	struct ActionSetState
	{
		VansActionSetDefinition definition;
		std::uint64_t source = 0;
		VansActionRevokePolicy revokePolicy = VansActionRevokePolicy::CancelRunning;
		std::vector<VansActionSpecHandle> specs;
		std::vector<VansActionSetInitializerCleanup> initializerCleanup;
	};

	struct ActionInstance
	{
		VansActionSpecHandle sourceSpec;
		std::shared_ptr<const VansCompiledActionDefinition> definition;
		std::shared_ptr<const VansActionRuntimeProjection> runtime;
		VansActionContext context;
		VansActionInstanceState state = VansActionInstanceState::Created;
		VansActionEndReason endReason = VansActionEndReason::Completed;
		VansActionError error = VansActionError::None;
		double elapsedSeconds = 0.0;
		VansActionVariableStore variables;
		VansActionTaskSet tasks;
		VansActionResourceLedger resources;
		std::unique_ptr<IVansActionExecutor> executor;
		std::vector<std::unique_ptr<IVansActionSidecarDriver>> drivers;
		std::vector<VansActionEvent> inbox;
		std::vector<VansActionDebugEventSnapshot> recentEvents;
		std::uint64_t nextEventSequence = 1;
		std::vector<VansActionTraceEntry> trace;
		std::uint64_t source = 0;
	};

	struct CooldownState
	{
		double remainingSeconds = 0.0;
		VansGameplayTagId tag;
		std::uint64_t tagSource = 0;
	};

	struct PendingTransition
	{
		VansActionHandle source;
		VansActionId targetAction;
		VansActionContext context;
		VansSerializedValue contextPatch = VansSerializedValue::Object({});
		std::string name;
		double minimumSourceTime = 0.0;
		double maximumSourceTime = -1.0;
		double expiresAt = 0.0;
		std::int32_t priority = 0;
		std::uint64_t sequence = 0;
		bool cancelSource = true;
		bool allowEndedSource = false;
	};

	VansActionExecutionContext BuildExecutionContext(
		VansActionHandle handle,
		ActionInstance& instance,
		double deltaSeconds);
	bool ValidateActivation(
		const VansActionActivationRequest& request,
		const GrantedSpec& spec,
		VansActionResult& result,
		bool ignoreConcurrencyOccupancy = false) const;
	bool ValidateCommitRequirements(
		const VansActionRuntimeProjection& runtime,
		const VansActionContext& context,
		const VansTargetData* targetData,
		VansActionResult& result) const;
	VansActionResult StartActivation(
		VansActionHandle handle,
		const VansActionActivationRequest& request,
		GrantedSpec& spec);
	VansActionResult QueueActivation(
		const VansActionActivationRequest& request,
		const GrantedSpec& spec);
	void ProcessConcurrencyQueues(double deltaSeconds);
	void ProcessTransitions();
	VansActionResult ExecuteTransition(const PendingTransition& transition);
	VansActionSpecHandle FindSpecForAction(VansActionId action) const;
	void QueueTerminalTransitions(
		VansActionHandle handle,
		const ActionInstance& instance,
		VansActionEndReason reason,
		VansActionError error);
	std::size_t ConcurrencyOccupancy(VansActionConcurrencyGroupId group) const;
	void RemoveFromConcurrencyQueue(VansActionHandle handle, VansActionConcurrencyGroupId group);
	bool CommitActivation(
		VansActionHandle handle,
		GrantedSpec& spec,
		ActionInstance& instance,
		std::string& error);
	void End(
		VansActionHandle handle,
		ActionInstance& instance,
		VansActionEndReason reason,
		VansActionError error,
		std::string message);
	void Transition(ActionInstance& instance, VansActionInstanceState state, std::string message);
	VansActionInstanceSnapshot BuildSnapshot(VansActionHandle handle, const ActionInstance& instance) const;
	bool HasRunningActionForSpec(VansActionSpecHandle spec) const;
	void ReleaseDeferredSpecs();
	void RecycleEnded();
	void TickCooldowns(double deltaSeconds);
	static std::uint64_t SourceForHandle(VansActionHandle handle);
	static std::uint64_t SourceForActionSet(VansActionSetHandle handle);
	static std::uint64_t SourceForCooldown(VansActionId action, std::size_t index);

	VansEntityHandle m_Owner;
	VansActionHostDependencies m_Dependencies;
	VansGameplayTagContainer m_Tags;
	VansAttributeService m_Attributes;
	VansGameplayCueService m_Cues;
	VansTargetDataStore m_TargetData;
	VansGameplayEffectService m_Effects;
	VansActionResourceLedger m_HostResources;
	VansGenerationPool<GrantedSpec> m_Specs;
	VansGenerationPool<ActionSetState> m_ActionSets;
	VansGenerationPool<ActionInstance> m_Instances;
	std::unordered_map<VansActionConcurrencyGroupId, std::vector<VansActionHandle>> m_Concurrency;
	std::unordered_map<VansActionConcurrencyGroupId, std::deque<VansActionHandle>> m_ConcurrencyQueues;
	std::unordered_map<VansActionId, std::vector<CooldownState>> m_Cooldowns;
	std::vector<PendingTransition> m_PendingTransitions;
	std::vector<VansActionHandle> m_DeferredRecycle;
	std::deque<VansActionInstanceSnapshot> m_History;
	bool m_Initialized = false;
	bool m_ShuttingDown = false;
	bool m_LateContinuationRequested = false;
	bool m_Enabled = true;
	bool m_ProcessingTransitions = false;
	double m_ElapsedSeconds = 0.0;
	std::uint64_t m_NextTransitionSequence = 1;
	std::uint32_t m_NextCueSequence = 1;
};
}
