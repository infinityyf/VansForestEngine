#include "VansActionHost.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EventCore/VansEventBus.h"
#include "../EventCore/VansEventLane.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
namespace
{
bool HasQueryTerms(const VansGameplayTagQuery& query)
{
	return !query.all.empty() || !query.any.empty() || !query.none.empty();
}

bool IsCancellationEnd(VansActionEndReason reason)
{
		return reason == VansActionEndReason::Cancelled ||
		reason == VansActionEndReason::Interrupted ||
		reason == VansActionEndReason::OwnerDestroyed;
}

bool IsExecutingState(VansActionInstanceState state)
{
	return state == VansActionInstanceState::Running ||
		state == VansActionInstanceState::Waiting ||
		state == VansActionInstanceState::Transitioning;
}

void ApplyTransitionContextPatch(
	VansActionContext& context,
	const VansSerializedValue& patch,
	bool inheritPrimaryTarget)
{
	if (!inheritPrimaryTarget) context.primaryTarget = {};
	if (patch.kind != VansSerializedValue::Kind::Object) return;
	if (ReadSerializedBoolField(patch, "clearPrimaryTarget", false)) context.primaryTarget = {};
	if (const VansSerializedValue* seed = FindObjectField(patch, "randomSeed");
		seed && seed->kind == VansSerializedValue::Kind::Int && seed->intValue >= 0)
		context.randomSeed = static_cast<std::uint64_t>(seed->intValue);
	if (context.payload.kind != VansSerializedValue::Kind::Object)
		context.payload = VansSerializedValue::Object({});
	if (const VansSerializedValue* payload = FindObjectField(patch, "payload");
		payload && payload->kind == VansSerializedValue::Kind::Object)
		for (const auto& [name, value] : payload->objectFields)
			SetSerializedObjectField(context.payload, name, value);
	for (const auto& [name, value] : patch.objectFields)
		if (name != "payload" && name != "randomSeed" && name != "clearPrimaryTarget")
			SetSerializedObjectField(context.payload, name, value);
}

VansActionContext MergeTransitionInputContext(
	const VansActionContext& inherited,
	const VansActionContext& request)
{
	VansActionContext result = inherited;
	if (request.instigator.IsValid()) result.instigator = request.instigator;
	if (request.source.IsValid()) result.source = request.source;
	if (request.primaryTarget.IsValid()) result.primaryTarget = request.primaryTarget;
	if (request.predictionKey.IsValid()) result.predictionKey = request.predictionKey;
	if (request.randomSeed != 0) result.randomSeed = request.randomSeed;
	if (request.payload.kind == VansSerializedValue::Kind::Object)
	{
		if (result.payload.kind != VansSerializedValue::Kind::Object)
			result.payload = VansSerializedValue::Object({});
		for (const auto& [name, value] : request.payload.objectFields)
			SetSerializedObjectField(result.payload, name, value);
	}
	return result;
}
}

VansActionHost::VansActionHost(
	VansEntityHandle owner,
	VansActionHostDependencies dependencies)
	: m_Owner(owner)
	, m_Dependencies(dependencies)
	, m_Tags(dependencies.tagDictionary)
	, m_Attributes(dependencies.attributeRegistry)
	, m_Cues(dependencies.cueRegistry)
	, m_Effects(&m_Attributes, &m_Tags, &m_Cues,
		dependencies.limits.maximumActiveEffects, &m_TargetData)
{
}

VansActionHost::~VansActionHost()
{
	Shutdown();
}

void VansActionHost::SetEnabled(bool enabled)
{
	if (m_Enabled == enabled) return;
	m_Enabled = enabled;
	if (enabled || !m_Initialized) return;
	std::vector<VansActionHandle> active;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.state != VansActionInstanceState::Ended) active.push_back({ handle });
	});
	for (VansActionHandle handle : active)
	{
		std::string ignored;
		Cancel(handle, VansActionCancelReason::System, ignored);
	}
}

bool VansActionHost::Initialize(std::string& error)
{
	if (m_Initialized) return true;
	if (!m_Owner.IsValid() || !m_Dependencies.definitions || !m_Dependencies.executors ||
		!m_Dependencies.executors->IsSealed() || !m_Dependencies.tagDictionary ||
		!m_Dependencies.tagDictionary->IsSealed() || !m_Dependencies.attributeRegistry ||
		!m_Dependencies.attributeRegistry->IsSealed() || !m_Dependencies.services ||
		!m_Dependencies.services->IsSealed() ||
		m_Dependencies.limits.maximumActiveActions == 0 ||
		m_Dependencies.limits.maximumTasksPerAction == 0 ||
		m_Dependencies.limits.maximumActiveEffects == 0 ||
		m_Dependencies.limits.maximumPayloadBytes == 0)
	{
		error = "Action Host dependencies are incomplete or not sealed";
		return false;
	}
	if (m_Dependencies.cueRegistry && !m_Dependencies.cueRegistry->IsSealed())
	{
		error = "Action Host Cue registry is not sealed";
		return false;
	}
	if (m_Dependencies.targetingPolicies &&
		(!m_Dependencies.targetingPolicies->IsSealed() ||
			!m_Dependencies.targetingHandlers || !m_Dependencies.targetingHandlers->IsSealed()))
	{
		error = "Action Host Targeting registries are not sealed";
		return false;
	}
	if (m_Dependencies.effectRegistry && !m_Dependencies.effectRegistry->IsSealed())
	{
		error = "Action Host Effect registry is not sealed";
		return false;
	}
	if (!m_Attributes.InitializeDefaults(error)) return false;
	m_Initialized = true;
	return true;
}

void VansActionHost::Shutdown()
{
	if (m_ShuttingDown) return;
	m_ShuttingDown = true;
	std::vector<VansActionHandle> actions;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.state != VansActionInstanceState::Ended) actions.push_back({ handle });
	});
	for (VansActionHandle handle : actions)
	{
		ActionInstance* instance = m_Instances.Resolve(handle.value);
		if (instance) End(handle, *instance, VansActionEndReason::OwnerDestroyed,
			VansActionError::Cancelled, "Action Host shutdown");
	}
	m_Instances.Clear();
	m_DeferredRecycle.clear();
	m_ActionSets.Clear();
	m_Specs.Clear();
	m_Effects.Clear();
	m_Cues.Clear();
	m_TargetData.Clear();
	m_Tags.Clear();
	m_Concurrency.clear();
	m_ConcurrencyQueues.clear();
	m_Cooldowns.clear();
	m_PendingTransitions.clear();
	m_History.clear();
	m_ElapsedSeconds = 0.0;
	m_NextTransitionSequence = 1;
	m_Initialized = false;
	m_CommitFrozen = false;
	m_ShuttingDown = false;
}

VansActionSpecHandle VansActionHost::Grant(const VansActionGrantDesc& desc, std::string& error)
{
	if (!m_Initialized || !desc.action || !std::isfinite(desc.level) || desc.level <= 0.0 ||
		desc.charges < -1 || desc.source == 0)
	{
		error = "Action grant descriptor is invalid or Host is not initialized";
		return {};
	}
	const auto definition = m_Dependencies.definitions->ResolveLatest(desc.action);
	if (!definition)
	{
		error = "Action Definition is not registered";
		return {};
	}
	if (!m_Dependencies.services->ValidateRequired(definition->requiredServices, error)) return {};
	GrantedSpec spec;
	spec.definition = definition;
	spec.level = desc.level;
	spec.inputBinding = desc.inputBinding;
	spec.dynamicTags = desc.dynamicTags;
	spec.charges = desc.charges;
	spec.source = desc.source;
	spec.persistence = desc.persistence;
	return { m_Specs.Emplace(std::move(spec)) };
}

bool VansActionHost::Revoke(
	VansActionSpecHandle specHandle,
	VansActionRevokePolicy policy,
	std::string& error)
{
	GrantedSpec* spec = m_Specs.Resolve(specHandle.value);
	if (!spec)
	{
		error = "Action Spec handle is stale";
		return false;
	}
	std::vector<VansActionHandle> running;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.sourceSpec == specHandle && instance.state != VansActionInstanceState::Ending &&
			instance.state != VansActionInstanceState::Ended) running.push_back({ handle });
	});
	if (policy == VansActionRevokePolicy::DeferUntilIdle && !running.empty())
	{
		spec->pendingRemoval = true;
		return true;
	}
	if (policy == VansActionRevokePolicy::CancelRunning)
	{
		for (VansActionHandle action : running)
		{
			std::string ignored;
			if (!Cancel(action, VansActionCancelReason::GrantRevoked, ignored))
			{
				ActionInstance* instance = m_Instances.Resolve(action.value);
				if (instance) End(action, *instance, VansActionEndReason::Cancelled,
					VansActionError::Cancelled, "Action grant revoked");
			}
		}
	}
	return m_Specs.Release(specHandle.value);
}

VansActionSetHandle VansActionHost::ApplyActionSet(
	const VansActionSetDefinition& set,
	std::string& error)
{
	if (!m_Initialized || !set.id || set.name.empty() || set.grants.empty())
	{
		error = "ActionSet is invalid or Host is not initialized";
		return {};
	}
	ActionSetState state;
	state.definition = set;
	const VansActionSetHandle handle{ m_ActionSets.Emplace(std::move(state)) };
	ActionSetState* stored = m_ActionSets.Resolve(handle.value);
	stored->source = SourceForActionSet(handle);
	for (const VansActionGrantDesc& grant : set.grants)
	{
		VansActionGrantDesc fromSet = grant;
		fromSet.source = stored->source;
		const VansActionSpecHandle spec = Grant(fromSet, error);
		if (!spec)
		{
			for (VansActionSpecHandle created : stored->specs)
			{
				std::string ignored;
				Revoke(created, VansActionRevokePolicy::KeepRunning, ignored);
			}
			m_ActionSets.Release(handle.value);
			return {};
		}
		stored->specs.push_back(spec);
	}
	m_Attributes.BeginBatch();
	for (std::size_t index = 0; index < set.attributeOverrides.size(); ++index)
	{
		const VansActionSetDefinition::AttributeOverride& overrideValue =
			set.attributeOverrides[index];
		VansAttributeModifierDesc modifier;
		modifier.attribute = overrideValue.attribute;
		modifier.operation = VansAttributeModifierOperation::Additive;
		modifier.magnitude = overrideValue.value -
			m_Attributes.Current(overrideValue.attribute);
		modifier.sourceOrder = index;
		modifier.source = stored->source;
		const VansAttributeModifierHandle applied = m_Attributes.AddModifier(modifier);
		if (!applied)
		{
			m_Attributes.EndBatch();
			error = "ActionSet Attribute override is invalid";
			std::string ignored;
			RevokeActionSet(handle, ignored);
			return {};
		}
		stored->attributeOverrides.push_back(applied);
	}
	m_Attributes.EndBatch();
	for (VansEffectId effectId : set.initialEffects)
	{
		if (!m_Dependencies.effectRegistry)
		{
			error = "ActionSet requires an Effect registry";
			RevokeActionSet(handle, error);
			return {};
		}
		const auto definition = m_Dependencies.effectRegistry->Resolve(effectId);
		if (!definition)
		{
			error = "ActionSet initial Effect is missing";
			std::string ignored;
			RevokeActionSet(handle, ignored);
			return {};
		}
		VansEffectSpec spec;
		spec.definition = definition;
		spec.source = stored->source;
		spec.context.owner = m_Owner;
		spec.context.instigator = m_Owner;
		spec.context.primaryTarget = m_Owner;
		const VansEffectApplicationResult applied = m_Effects.Apply(spec);
		if (!applied)
		{
			error = applied.message;
			std::string ignored;
			RevokeActionSet(handle, ignored);
			return {};
		}
		if (applied.active) stored->effects.push_back(applied.active);
	}
	return handle;
}

bool VansActionHost::RevokeActionSet(VansActionSetHandle handle, std::string& error)
{
	ActionSetState* set = m_ActionSets.Resolve(handle.value);
	if (!set)
	{
		error = "ActionSet handle is stale";
		return false;
	}
	const VansActionRevokePolicy policy = set->definition.revokePolicy;
	const bool removeEffects = set->definition.removeInitialEffectsOnRevoke;
	const std::vector<VansActionSpecHandle> specs = set->specs;
	const std::vector<VansActiveEffectHandle> effects = set->effects;
	const std::vector<VansAttributeModifierHandle> attributeOverrides = set->attributeOverrides;
	bool succeeded = true;
	for (VansActionSpecHandle spec : specs)
	{
		std::string revokeError;
		if (!Revoke(spec, policy, revokeError))
		{
			succeeded = false;
			error = std::move(revokeError);
		}
	}
	if (removeEffects)
	{
		for (VansActiveEffectHandle effect : effects)
		{
			std::string removeError;
			if (!m_Effects.Remove(effect, removeError) && removeError != "Active Effect handle is stale")
			{
				succeeded = false;
				error = std::move(removeError);
			}
		}
	}
	m_Attributes.BeginBatch();
	for (VansAttributeModifierHandle attributeOverride : attributeOverrides)
	{
		if (!m_Attributes.RemoveModifier(attributeOverride))
		{
			succeeded = false;
			if (error.empty()) error = "ActionSet Attribute override handle is stale";
		}
	}
	m_Attributes.EndBatch();
	m_ActionSets.Release(handle.value);
	return succeeded;
}

std::size_t VansActionHost::RevokeSource(
	std::uint64_t source,
	VansActionRevokePolicy policy)
{
	std::vector<VansActionSpecHandle> specs;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		if (spec.source == source) specs.push_back({ handle });
	});
	for (VansActionSpecHandle spec : specs)
	{
		std::string ignored;
		Revoke(spec, policy, ignored);
	}
	return specs.size();
}

VansActionResult VansActionHost::Activate(const VansActionActivationRequest& request)
{
	VansActionResult result;
	if (!m_Enabled)
	{
		result.error = VansActionError::InvalidState;
		result.message = "Action Host is disabled";
		return result;
	}
	ProcessTransitions();
	RecycleEnded();
	GrantedSpec* spec = m_Specs.Resolve(request.spec.value);
	if (!spec)
	{
		result.error = VansActionError::NotGranted;
		result.message = "Action Spec handle is stale";
		return result;
	}
	if (m_Instances.ActiveCount() >= m_Dependencies.limits.maximumActiveActions)
	{
		result.error = VansActionError::BudgetExceeded;
		result.message = "Action Host active Action budget exceeded";
		return result;
	}
	const auto& definition = *spec->definition;
	const bool concurrencyFull = definition.concurrencyGroup &&
		ConcurrencyOccupancy(definition.concurrencyGroup) >= definition.concurrencyLimit;
	if (concurrencyFull && definition.concurrencyPolicy == VansActionConcurrencyPolicy::QueueNew)
	{
		if (!ValidateActivation(request, *spec, result, true)) return result;
		return QueueActivation(request, *spec);
	}
	if (!ValidateActivation(request, *spec, result)) return result;
	if (concurrencyFull && definition.concurrencyPolicy == VansActionConcurrencyPolicy::CancelExisting)
	{
		const auto occupied = m_Concurrency.find(definition.concurrencyGroup);
		if (occupied != m_Concurrency.end())
		{
			const std::vector<VansActionHandle> cancellations = occupied->second;
			for (VansActionHandle action : cancellations)
			{
				std::string cancelError;
				if (!Cancel(action, VansActionCancelReason::Concurrency, cancelError))
				{
					result.error = VansActionError::ConcurrencyRejected;
					result.message = "Could not cancel existing Action: " + cancelError;
					return result;
				}
			}
		}
	}
	const VansActionHandle handle{ m_Instances.Emplace(ActionInstance{}) };
	return StartActivation(handle, request, *spec);
}

VansActionResult VansActionHost::ActivateAction(
	VansActionId action,
	VansActionContext context)
{
	VansActionResult result;
	const VansActionSpecHandle spec = FindSpecForAction(action);
	if (!spec)
	{
		result.error = VansActionError::NotGranted;
		result.message = "Action is not granted to this Host";
		return result;
	}
	VansActionActivationRequest request;
	request.spec = spec;
	request.context = std::move(context);
	return Activate(request);
}

VansActionResult VansActionHost::CanActivate(
	VansActionSpecHandle specHandle,
	const VansActionContext& context,
	bool hasAuthority,
	bool predicted,
	bool locallyControlled) const
{
	VansActionResult result;
	const GrantedSpec* spec = m_Specs.Resolve(specHandle.value);
	if (!spec)
	{
		result.error = VansActionError::InvalidHandle;
		result.message = "Action Spec handle is invalid";
		return result;
	}
	VansActionActivationRequest request;
	request.spec = specHandle;
	request.context = context;
	request.hasAuthority = hasAuthority;
	request.locallyControlled = locallyControlled;
	request.predicted = predicted;
	ValidateActivation(request, *spec, result);
	return result;
}

VansActionResult VansActionHost::CanActivateAction(
	VansActionId action,
	const VansActionContext& context,
	bool hasAuthority,
	bool predicted,
	bool locallyControlled) const
{
	const VansActionSpecHandle spec = FindSpecForAction(action);
	if (!spec)
	{
		VansActionResult result;
		result.error = VansActionError::NotGranted;
		result.message = "Action is not granted";
		return result;
	}
	return CanActivate(spec, context, hasAuthority, predicted, locallyControlled);
}

VansActionResult VansActionHost::RequestTransition(
	VansActionHandle source,
	VansActionId targetAction,
	VansActionContext context,
	VansSerializedValue contextPatch,
	bool cancelSource,
	bool inheritPrimaryTarget)
{
	VansActionResult result;
	if (!m_Initialized || m_ShuttingDown || !source || !targetAction)
	{
		result.error = VansActionError::InvalidState;
		result.message = "Action transition request is invalid or Host is unavailable";
		return result;
	}
	const ActionInstance* sourceInstance = m_Instances.Resolve(source.value);
	if (!sourceInstance || !IsExecutingState(sourceInstance->state))
	{
		result.error = VansActionError::InvalidHandle;
		result.message = "Action transition source is stale or no longer active";
		return result;
	}
	if (!FindSpecForAction(targetAction))
	{
		result.error = VansActionError::NotGranted;
		result.message = "Action transition target is not granted";
		return result;
	}
	if (context.owner.IsValid() && context.owner != m_Owner)
	{
		result.error = VansActionError::TargetInvalid;
		result.message = "Action transition context belongs to a different Host";
		return result;
	}
	if (contextPatch.kind != VansSerializedValue::Kind::Object)
	{
		result.error = VansActionError::DefinitionInvalid;
		result.message = "Action transition context patch must be an object";
		return result;
	}
	if (!context.owner.IsValid()) context.owner = m_Owner;
	PendingTransition transition;
	transition.source = source;
	transition.targetAction = targetAction;
	transition.context = std::move(context);
	transition.contextPatch = std::move(contextPatch);
	transition.name = cancelSource ? "GraphTransition" : "GraphSubAction";
	transition.sequence = m_NextTransitionSequence++;
	transition.cancelSource = cancelSource;
	transition.inheritPrimaryTarget = inheritPrimaryTarget;
	transition.allowEndedSource = true;
	m_PendingTransitions.push_back(std::move(transition));
	result.action = source;
	return result;
}

VansActionResult VansActionHost::ActivateInput(
	std::string_view inputBinding,
	VansActionContext context)
{
	VansActionResult result;
	if (!m_Initialized || !m_Enabled || inputBinding.empty())
	{
		result.error = VansActionError::InvalidState;
		result.message = "Action input request is invalid or Host is disabled";
		return result;
	}
	struct Candidate
	{
		VansActionHandle source;
		const ActionInstance* instance = nullptr;
		const VansActionTransitionRule* rule = nullptr;
	};
	std::vector<Candidate> candidates;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (!IsExecutingState(instance.state) || !instance.definition) return;
		for (const VansActionTransitionRule& rule : instance.definition->transitionRules)
		{
			if (rule.trigger != VansActionTransitionTrigger::Input ||
				rule.inputBinding != inputBinding ||
				(rule.maximumTimeSeconds >= 0.0 &&
					instance.elapsedSeconds > rule.maximumTimeSeconds) ||
				(HasQueryTerms(rule.requirements) && !m_Tags.Matches(rule.requirements))) continue;
			candidates.push_back({ { handle }, &instance, &rule });
		}
	});
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right)
	{
		if (left.rule->priority != right.rule->priority)
			return left.rule->priority > right.rule->priority;
		if (left.instance->definition->priority != right.instance->definition->priority)
			return left.instance->definition->priority > right.instance->definition->priority;
		return left.source.value.index < right.source.value.index;
	});
	if (!candidates.empty())
	{
		const Candidate& candidate = candidates.front();
		PendingTransition transition;
		transition.source = candidate.source;
		transition.targetAction = candidate.rule->targetAction;
		transition.context = MergeTransitionInputContext(candidate.instance->context, context);
		transition.contextPatch = candidate.rule->contextPatch;
		transition.name = candidate.rule->name;
		transition.minimumSourceTime = candidate.rule->minimumTimeSeconds;
		transition.maximumSourceTime = candidate.rule->maximumTimeSeconds;
		transition.priority = candidate.rule->priority;
		transition.sequence = m_NextTransitionSequence++;
		transition.cancelSource = candidate.rule->cancelSource;
		transition.inheritPrimaryTarget = candidate.rule->inheritPrimaryTarget;
		if (candidate.instance->elapsedSeconds + 1e-12 >= candidate.rule->minimumTimeSeconds)
			return ExecuteTransition(transition);
		const VansActionInputBufferPolicy& buffer = candidate.instance->definition->inputBuffer;
		const std::size_t bufferedForSource = static_cast<std::size_t>(std::count_if(
			m_PendingTransitions.begin(), m_PendingTransitions.end(), [&](const PendingTransition& pending)
			{ return pending.source == candidate.source && !pending.allowEndedSource; }));
		if (!buffer.enabled || bufferedForSource >= buffer.maximumEntries)
		{
			result.error = VansActionError::RequirementsFailed;
			result.message = "Action transition window is not open";
			return result;
		}
		transition.expiresAt = m_ElapsedSeconds + buffer.durationSeconds;
		m_PendingTransitions.push_back(std::move(transition));
		result.action = candidate.source;
		result.disposition = VansActionActivationDisposition::Queued;
		result.message = "Action input buffered for a transition window";
		return result;
	}
	VansActionSpecHandle directSpec;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		if (!directSpec && !spec.pendingRemoval && spec.inputBinding == inputBinding)
			directSpec = { handle };
	});
	if (!directSpec)
	{
		result.error = VansActionError::NotGranted;
		result.message = "No granted Action is bound to the requested input";
		return result;
	}
	VansActionActivationRequest request;
	request.spec = directSpec;
	request.context = std::move(context);
	return Activate(request);
}

VansActionResult VansActionHost::QueueActivation(
	const VansActionActivationRequest& request,
	const GrantedSpec& spec)
{
	const VansActionHandle handle{ m_Instances.Emplace(ActionInstance{}) };
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	instance->sourceSpec = request.spec;
	instance->definition = spec.definition;
	instance->context = request.context;
	instance->context.owner = m_Owner;
	instance->source = SourceForHandle(handle);
	instance->hasAuthority = request.hasAuthority;
	instance->locallyControlled = request.locallyControlled;
	instance->predicted = request.predicted;
	Transition(*instance, VansActionInstanceState::Created, "instance allocated");
	Transition(*instance, VansActionInstanceState::Queued, "waiting for concurrency slot");
	auto& queue = m_ConcurrencyQueues[spec.definition->concurrencyGroup];
	const auto position = std::find_if(queue.begin(), queue.end(), [&](VansActionHandle queued)
	{
		const ActionInstance* other = m_Instances.Resolve(queued.value);
		return other && other->definition && other->definition->priority < spec.definition->priority;
	});
	queue.insert(position, handle);
	VansEventBus::Get().Enqueue(VansActionQueuedEvent{
		m_Owner, handle, spec.definition->id, spec.definition->concurrencyGroup,
		instance->context.predictionKey }, VansEventLane::GameLogic);
	VansActionResult result;
	result.action = handle;
	result.message = "Action queued for its concurrency group";
	result.disposition = VansActionActivationDisposition::Queued;
	return result;
}

VansActionResult VansActionHost::StartActivation(
	VansActionHandle handle,
	const VansActionActivationRequest& request,
	GrantedSpec& spec)
{
	VansActionResult result;
	std::string error;
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	if (!instance)
	{
		result.error = VansActionError::InvalidHandle;
		result.message = "Action instance handle is stale";
		return result;
	}
	const bool wasQueued = instance->state == VansActionInstanceState::Queued;
	if (!wasQueued)
	{
		instance->sourceSpec = request.spec;
		instance->definition = spec.definition;
		instance->context = request.context;
		instance->context.owner = m_Owner;
		instance->source = SourceForHandle(handle);
		instance->hasAuthority = request.hasAuthority;
		instance->locallyControlled = request.locallyControlled;
		instance->predicted = request.predicted;
		Transition(*instance, VansActionInstanceState::Created, "instance allocated");
	}
	instance->tasks.SetMaximumTasks(m_Dependencies.limits.maximumTasksPerAction);
	RemoveFromConcurrencyQueue(handle, instance->definition->concurrencyGroup);
	std::vector<VansActionHandle> cancelActions;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& active)
	{
		if (active.state == VansActionInstanceState::Queued ||
			active.state == VansActionInstanceState::Ending || active.state == VansActionInstanceState::Ended) return;
		if (std::find(spec.definition->cancelActions.begin(), spec.definition->cancelActions.end(),
			active.definition->id) != spec.definition->cancelActions.end()) cancelActions.push_back({ handle });
	});
	for (VansActionHandle action : cancelActions)
	{
		std::string ignored;
		Cancel(action, VansActionCancelReason::Interrupted, ignored);
	}

	Transition(*instance, VansActionInstanceState::Resolving, "definition revision pinned");
	if (instance->definition->targetingPolicy)
	{
		const VansTargetingPolicy* policy = m_Dependencies.targetingPolicies
			? m_Dependencies.targetingPolicies->Resolve(instance->definition->targetingPolicy) : nullptr;
		if (!policy || !m_Dependencies.targetingHandlers)
		{
			error = "Action TargetingPolicy is unavailable";
			End(handle, *instance, VansActionEndReason::Failed, VansActionError::ServiceMissing, error);
			return { VansActionError::ServiceMissing, handle, error };
		}
		VansTargetData initial;
		if (instance->context.targetData)
		{
			const VansTargetData* supplied = m_TargetData.Resolve(instance->context.targetData);
			if (!supplied)
			{
				error = "Action Context TargetData handle is stale";
				End(handle, *instance, VansActionEndReason::Failed, VansActionError::TargetInvalid, error);
				return { VansActionError::TargetInvalid, handle, error };
			}
			initial = *supplied;
			m_TargetData.Release(instance->context.targetData);
			instance->context.targetData = {};
		}
		else if (instance->context.primaryTarget.IsValid())
			initial.values.push_back(instance->context.primaryTarget);
		VansTargetingResult targeting = VansTargetingPipeline::Execute(
			*policy, instance->context, *m_Dependencies.targetingHandlers, std::move(initial));
		for (const VansTargetingTraceEntry& trace : targeting.trace)
			instance->trace.push_back({ instance->elapsedSeconds, instance->state,
				"Targeting " + trace.step + ": " + (trace.succeeded ? "ok" : trace.message) });
		if (!targeting)
		{
			error = targeting.message;
			End(handle, *instance, VansActionEndReason::Failed, targeting.error, error);
			return { targeting.error, handle, error };
		}
		instance->context.targetData = m_TargetData.Store(std::move(targeting.data));
		if (const VansTargetData* resolved = m_TargetData.Resolve(instance->context.targetData))
			for (const VansTargetDataValue& value : resolved->values)
			{
				if (const auto* entity = std::get_if<VansEntityHandle>(&value))
					{ instance->context.primaryTarget = *entity; break; }
				if (const auto* hit = std::get_if<VansTargetHitResult>(&value); hit && hit->entity.IsValid())
					{ instance->context.primaryTarget = hit->entity; break; }
			}
	}
	else if (instance->context.targetData && !m_TargetData.Resolve(instance->context.targetData))
	{
		error = "Action Context TargetData handle is stale";
		End(handle, *instance, VansActionEndReason::Failed, VansActionError::TargetInvalid, error);
		return { VansActionError::TargetInvalid, handle, error };
	}
	Transition(*instance, VansActionInstanceState::BuildingContext, "activation context frozen");
	Transition(*instance, VansActionInstanceState::Validating, "activation validated");
	if (!instance->variables.Initialize(instance->definition->variables, error))
	{
		End(handle, *instance, VansActionEndReason::Failed,
			VansActionError::DefinitionInvalid, error);
		return { VansActionError::DefinitionInvalid, handle, error };
	}
	Transition(*instance, VansActionInstanceState::Preparing, "Executor and variables prepared");
	instance->executor = m_Dependencies.executors->Create(
		instance->definition->executor, *instance->definition, error);
	if (!instance->executor)
	{
		End(handle, *instance, VansActionEndReason::Failed, VansActionError::ExecutionFailed, error);
		return { VansActionError::ExecutionFailed, handle, error };
	}
	Transition(*instance, VansActionInstanceState::Committing, "CommitTransaction started");
	if (!CommitActivation(handle, spec, *instance, error))
	{
		End(handle, *instance, VansActionEndReason::CommitFailed, VansActionError::CommitFailed, error);
		return { VansActionError::CommitFailed, handle, error };
	}
	Transition(*instance, VansActionInstanceState::Committed, "CommitTransaction completed");
	Transition(*instance, VansActionInstanceState::Running, "Executor started");
	for (VansCueId cue : instance->definition->presentationCues)
	{
		VansGameplayCueParameters parameters;
		parameters.context = instance->context;
		parameters.target = instance->context.primaryTarget.IsValid()
			? instance->context.primaryTarget : m_Owner;
		parameters.payload = instance->context.payload;
		const VansGameplayCueKey key{ instance->context.predictionKey, cue, m_NextCueSequence++ };
		if (!m_Cues.Execute(key, m_Cues.DefaultScope(cue), parameters, error))
		{
			End(handle, *instance, VansActionEndReason::Failed,
				VansActionError::ExecutionFailed, error);
			return { VansActionError::ExecutionFailed, handle, error };
		}
	}
	VansEventBus::Get().Enqueue(VansActionStartedEvent{
		m_Owner, handle, instance->definition->id, instance->context.predictionKey },
		VansEventLane::GameLogic);
	VansActionExecutionContext execution = BuildExecutionContext(handle, *instance, 0.0);
	const VansActionExecutorResult started = instance->executor->Start(execution);
	if (started.status == VansActionExecutorStatus::Succeeded)
		End(handle, *instance, VansActionEndReason::Completed, VansActionError::None, started.message);
	else if (started.status == VansActionExecutorStatus::Failed)
		End(handle, *instance, VansActionEndReason::Failed,
			started.error == VansActionError::None ? VansActionError::ExecutionFailed : started.error,
			started.message);
	else if (started.status == VansActionExecutorStatus::Waiting)
		Transition(*instance, VansActionInstanceState::Waiting, "Executor is waiting");
	result.action = handle;
	return result;
}

bool VansActionHost::Cancel(
	VansActionHandle handle,
	VansActionCancelReason reason,
	std::string& error)
{
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	if (!instance || instance->state == VansActionInstanceState::Ending ||
		instance->state == VansActionInstanceState::Ended)
	{
		error = "Action handle is stale or already ending";
		return false;
	}
	const bool forced = reason == VansActionCancelReason::OwnerDestroyed ||
		reason == VansActionCancelReason::GrantRevoked || reason == VansActionCancelReason::System;
	if (!forced && reason != VansActionCancelReason::Interrupted && !instance->definition->cancellable)
	{
		error = "Action does not allow cancellation";
		return false;
	}
	VansActionExecutionContext execution = BuildExecutionContext(handle, *instance, 0.0);
	if (!forced && instance->executor && !instance->executor->RequestCancel(execution, reason))
	{
		error = "Action Executor rejected cancellation";
		return false;
	}
	const bool interrupted = reason == VansActionCancelReason::Interrupted ||
		reason == VansActionCancelReason::Concurrency;
	End(handle, *instance,
		interrupted ? VansActionEndReason::Interrupted : VansActionEndReason::Cancelled,
		VansActionError::Cancelled, interrupted ? "Action interrupted" : "Action cancelled");
	return true;
}

bool VansActionHost::Interrupt(VansActionHandle handle, std::string& error)
{
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	if (!instance)
	{
		error = "Action handle is stale";
		return false;
	}
	if (!instance->definition->interruptible)
	{
		error = "Action does not allow interruption";
		return false;
	}
	return Cancel(handle, VansActionCancelReason::Interrupted, error);
}

bool VansActionHost::EnqueueEvent(
	VansActionHandle handle,
	VansActionEvent event,
	std::string& error)
{
	ActionInstance* instance = m_Instances.Resolve(handle.value);
	if (!instance || (instance->state != VansActionInstanceState::Running &&
		instance->state != VansActionInstanceState::Waiting &&
		instance->state != VansActionInstanceState::Transitioning) || !event.type)
	{
		error = "Action event target or type is invalid";
		return false;
	}
	instance->recentEvents.push_back({ instance->nextEventSequence++, event.type, event.stableName });
	constexpr std::size_t MaximumDebugEvents = 64;
	if (instance->recentEvents.size() > MaximumDebugEvents)
		instance->recentEvents.erase(instance->recentEvents.begin(),
			instance->recentEvents.begin() + (instance->recentEvents.size() - MaximumDebugEvents));
	bool consumed = false;
	for (const VansActionTransitionRule& rule : instance->definition->transitionRules)
	{
		if (rule.trigger != VansActionTransitionTrigger::Event || rule.event != event.type ||
			instance->elapsedSeconds + 1e-12 < rule.minimumTimeSeconds ||
			(rule.maximumTimeSeconds >= 0.0 && instance->elapsedSeconds > rule.maximumTimeSeconds) ||
			(HasQueryTerms(rule.requirements) && !m_Tags.Matches(rule.requirements))) continue;
		PendingTransition transition;
		transition.source = handle;
		transition.targetAction = rule.targetAction;
		transition.context = instance->context;
		transition.contextPatch = rule.contextPatch;
		transition.name = rule.name;
		transition.minimumSourceTime = rule.minimumTimeSeconds;
		transition.maximumSourceTime = rule.maximumTimeSeconds;
		transition.expiresAt = m_ElapsedSeconds;
		transition.priority = rule.priority;
		transition.sequence = m_NextTransitionSequence++;
		transition.cancelSource = rule.cancelSource;
		transition.inheritPrimaryTarget = rule.inheritPrimaryTarget;
		transition.allowEndedSource = true;
		m_PendingTransitions.push_back(std::move(transition));
		consumed = rule.consumeTrigger;
		break;
	}
	if (!consumed) instance->inbox.push_back(std::move(event));
	m_LateContinuationRequested = true;
	return true;
}

bool VansActionHost::ReadVariable(
	VansActionHandle action,
	VansActionFieldId variable,
	VansSerializedValue& value,
	std::string& error) const
{
	const ActionInstance* instance = m_Instances.Resolve(action.value);
	const VansSerializedValue* found = instance ? instance->variables.Get(variable) : nullptr;
	if (!found)
	{
		error = instance ? "Action variable is not declared" : "Action handle is stale";
		return false;
	}
	value = *found;
	return true;
}

bool VansActionHost::WriteVariable(
	VansActionHandle action,
	VansActionFieldId variable,
	VansSerializedValue value,
	std::string& error)
{
	ActionInstance* instance = m_Instances.Resolve(action.value);
	if (!instance)
	{
		error = "Action handle is stale";
		return false;
	}
	if (!instance->variables.Set(variable, std::move(value)))
	{
		error = "Action variable is not declared";
		return false;
	}
	return true;
}

bool VansActionHost::RollbackPrediction(
	VansActionHandle action,
	std::vector<std::string>& errors)
{
	ActionInstance* instance = m_Instances.Resolve(action.value);
	if (!instance || instance->state == VansActionInstanceState::Ended) return false;
	return instance->resources.RollbackPredicted(errors);
}

bool VansActionHost::ReplayPrediction(
	VansActionHandle action,
	std::vector<std::string>& errors)
{
	ActionInstance* instance = m_Instances.Resolve(action.value);
	if (!instance || instance->state == VansActionInstanceState::Ended) return false;
	return instance->resources.ReplayPredicted(errors);
}

bool VansActionHost::CapturePersistentState(
	VansActionHostPersistentState& state,
	std::string& error) const
{
	if (!m_Initialized || m_ShuttingDown || m_CommitFrozen)
	{
		error = "Action Host is not in a stable state for persistence capture";
		return false;
	}
	state = {};
	m_Specs.ForEach([&](VansGenerationHandle, const GrantedSpec& spec)
	{
		if (spec.persistence != VansActionGrantPersistence::Persistent || spec.pendingRemoval) return;
		state.grants.push_back({ spec.definition->id, spec.level, spec.inputBinding,
			spec.dynamicTags, spec.charges, spec.source });
	});
	std::sort(state.grants.begin(), state.grants.end(), [](const auto& left, const auto& right)
	{
		if (left.action != right.action) return left.action < right.action;
		return left.source < right.source;
	});
	state.attributes = m_Attributes.Capture();
	for (const auto& [action, cooldowns] : m_Cooldowns)
		for (const CooldownState& cooldown : cooldowns)
			if (cooldown.remainingSeconds > 0.0)
				state.cooldowns.push_back({ action, cooldown.remainingSeconds, cooldown.tag });
	std::sort(state.cooldowns.begin(), state.cooldowns.end(), [](const auto& left, const auto& right)
	{
		if (left.action != right.action) return left.action < right.action;
		if (left.tag != right.tag) return left.tag < right.tag;
		return left.remainingSeconds < right.remainingSeconds;
	});
	return true;
}

bool VansActionHost::RestorePersistentState(
	const VansActionHostPersistentState& state,
	std::string& error)
{
	if (!m_Initialized || m_ShuttingDown || m_CommitFrozen || state.version != 1 ||
		!ActiveActions().empty())
	{
		error = "Action Host cannot restore persistence while unavailable, active, or version-incompatible";
		return false;
	}
	for (const VansPersistentActionGrantState& grant : state.grants)
	{
		const auto definition = m_Dependencies.definitions->ResolveLatest(grant.action);
		if (!definition || !std::isfinite(grant.level) || grant.level <= 0.0 ||
			grant.charges < -1 || grant.source == 0 ||
			!m_Dependencies.services->ValidateRequired(definition->requiredServices, error))
		{
			if (error.empty()) error = "Persistent Action grant is invalid or unresolved";
			return false;
		}
		for (VansGameplayTagId tag : grant.dynamicTags)
			if (!m_Dependencies.tagDictionary->Resolve(tag))
			{
				error = "Persistent Action grant contains an unresolved dynamic Tag";
				return false;
			}
	}
	for (const VansAttributeSnapshot& attribute : state.attributes)
		if (!m_Dependencies.attributeRegistry->Resolve(attribute.attribute) ||
			!std::isfinite(attribute.baseValue) || !std::isfinite(attribute.currentValue))
		{
			error = "Persistent Attribute snapshot is invalid or unresolved";
			return false;
		}
	for (const VansPersistentActionCooldownState& cooldown : state.cooldowns)
		if (!m_Dependencies.definitions->ResolveLatest(cooldown.action) ||
			!m_Dependencies.tagDictionary->Resolve(cooldown.tag) ||
			!std::isfinite(cooldown.remainingSeconds) || cooldown.remainingSeconds <= 0.0)
		{
			error = "Persistent cooldown is invalid or unresolved";
			return false;
		}

	std::vector<VansGenerationHandle> persistentSpecs;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		if (spec.persistence == VansActionGrantPersistence::Persistent)
			persistentSpecs.push_back(handle);
	});
	for (VansGenerationHandle handle : persistentSpecs) m_Specs.Release(handle);
	for (const auto& [action, cooldowns] : m_Cooldowns)
		for (const CooldownState& cooldown : cooldowns) m_Tags.RemoveSource(cooldown.tagSource);
	m_Cooldowns.clear();
	m_Attributes.Restore(state.attributes);
	for (const VansPersistentActionGrantState& saved : state.grants)
	{
		VansActionGrantDesc grant;
		grant.action = saved.action;
		grant.level = saved.level;
		grant.inputBinding = saved.inputBinding;
		grant.dynamicTags = saved.dynamicTags;
		grant.charges = saved.charges;
		grant.source = saved.source;
		grant.persistence = VansActionGrantPersistence::Persistent;
		if (!Grant(grant, error)) return false;
	}
	for (const VansPersistentActionCooldownState& saved : state.cooldowns)
	{
		auto& cooldowns = m_Cooldowns[saved.action];
		const std::size_t index = cooldowns.size();
		CooldownState cooldown;
		cooldown.remainingSeconds = saved.remainingSeconds;
		cooldown.tag = saved.tag;
		cooldown.tagSource = SourceForCooldown(saved.action, index);
		if (!m_Tags.Add(cooldown.tag, cooldown.tagSource))
		{
			error = "Persistent cooldown Tag could not be restored";
			return false;
		}
		cooldowns.push_back(cooldown);
	}
	return true;
}

void VansActionHost::Tick(double deltaSeconds)
{
	if (!m_Enabled) return;
	if (!m_Initialized || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0) return;
	m_ElapsedSeconds += deltaSeconds;
	ProcessTransitions();
	RecycleEnded();
	TickCooldowns(deltaSeconds);
	m_Effects.Tick(deltaSeconds);
	std::vector<VansActionHandle> handles;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.state == VansActionInstanceState::Running ||
			instance.state == VansActionInstanceState::Waiting ||
			instance.state == VansActionInstanceState::Transitioning) handles.push_back({ handle });
	});
	for (VansActionHandle handle : handles)
	{
		ActionInstance* instance = m_Instances.Resolve(handle.value);
		if (!instance) continue;
		instance->elapsedSeconds += deltaSeconds;
		instance->tasks.Tick(deltaSeconds);
		VansActionExecutionContext execution = BuildExecutionContext(handle, *instance, deltaSeconds);
		std::vector<VansActionEvent> inbox = std::move(instance->inbox);
		instance->inbox.clear();
		for (const VansActionEvent& event : inbox)
		{
			if (instance->state == VansActionInstanceState::Ending ||
				instance->state == VansActionInstanceState::Ended) break;
			instance->executor->OnEvent(execution, event);
		}
		if (instance->state == VansActionInstanceState::Ending ||
			instance->state == VansActionInstanceState::Ended) continue;
		const VansActionExecutorResult ticked = instance->executor->Tick(execution);
		if (ticked.status == VansActionExecutorStatus::Succeeded)
			End(handle, *instance, VansActionEndReason::Completed, VansActionError::None, ticked.message);
		else if (ticked.status == VansActionExecutorStatus::Failed)
			End(handle, *instance, VansActionEndReason::Failed,
				ticked.error == VansActionError::None ? VansActionError::ExecutionFailed : ticked.error,
				ticked.message);
		else if (ticked.status == VansActionExecutorStatus::Waiting &&
			instance->state != VansActionInstanceState::Waiting)
			Transition(*instance, VansActionInstanceState::Waiting, "Executor is waiting");
		else if (ticked.status == VansActionExecutorStatus::Running &&
			instance->state != VansActionInstanceState::Running)
			Transition(*instance, VansActionInstanceState::Running, "Executor resumed");
	}
	ProcessTransitions();
	ProcessConcurrencyQueues(deltaSeconds);
	m_LateContinuationRequested = false;
	ReleaseDeferredSpecs();
}

bool VansActionHost::RunLateContinuation()
{
	if (!m_Enabled) return false;
	if (!m_Initialized || !m_LateContinuationRequested) return false;
	m_LateContinuationRequested = false;
	std::vector<VansActionHandle> handles;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (!instance.inbox.empty() && (instance.state == VansActionInstanceState::Running ||
			instance.state == VansActionInstanceState::Waiting ||
			instance.state == VansActionInstanceState::Transitioning)) handles.push_back({ handle });
	});
	bool ran = false;
	for (VansActionHandle handle : handles)
	{
		ActionInstance* instance = m_Instances.Resolve(handle.value);
		if (!instance) continue;
		ran = true;
		VansActionExecutionContext execution = BuildExecutionContext(handle, *instance, 0.0);
		std::vector<VansActionEvent> inbox = std::move(instance->inbox);
		instance->inbox.clear();
		for (const VansActionEvent& event : inbox) instance->executor->OnEvent(execution, event);
		if (instance->state == VansActionInstanceState::Ending ||
			instance->state == VansActionInstanceState::Ended) continue;
		const VansActionExecutorResult result = instance->executor->Tick(execution);
		if (result.status == VansActionExecutorStatus::Succeeded)
			End(handle, *instance, VansActionEndReason::Completed, VansActionError::None, result.message);
		else if (result.status == VansActionExecutorStatus::Failed)
			End(handle, *instance, VansActionEndReason::Failed,
				result.error == VansActionError::None ? VansActionError::ExecutionFailed : result.error,
				result.message);
		else if (result.status == VansActionExecutorStatus::Waiting)
			Transition(*instance, VansActionInstanceState::Waiting, "late continuation is waiting");
		else
			Transition(*instance, VansActionInstanceState::Running, "late continuation resumed");
	}
	ProcessTransitions();
	ProcessConcurrencyQueues(0.0);
	ReleaseDeferredSpecs();
	return ran;
}

VansActionSpecHandle VansActionHost::FindSpecForAction(VansActionId action) const
{
	VansActionSpecHandle result;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		if (!result && !spec.pendingRemoval && spec.definition && spec.definition->id == action)
			result = { handle };
	});
	return result;
}

VansActionResult VansActionHost::ExecuteTransition(const PendingTransition& transition)
{
	VansActionResult result;
	ActionInstance* source = m_Instances.Resolve(transition.source.value);
	if (!transition.allowEndedSource && (!source || !IsExecutingState(source->state)))
	{
		result.error = VansActionError::InvalidState;
		result.message = "Action transition source is no longer active";
		return result;
	}
	if (transition.cancelSource && source && IsExecutingState(source->state) &&
		!source->definition->interruptible)
	{
		result.error = VansActionError::RequirementsFailed;
		result.message = "Action transition source does not allow interruption";
		return result;
	}
	const VansActionSpecHandle targetSpec = FindSpecForAction(transition.targetAction);
	if (!targetSpec)
	{
		result.error = VansActionError::NotGranted;
		result.message = "Action transition target is not granted";
		return result;
	}
	VansActionActivationRequest request;
	request.spec = targetSpec;
	request.context = transition.context;
	ApplyTransitionContextPatch(
		request.context, transition.contextPatch, transition.inheritPrimaryTarget);
	result = Activate(request);
	if (!result) return result;
	if (transition.cancelSource && source && IsExecutingState(source->state) &&
		transition.source != result.action)
	{
		std::string cancelError;
		if (!Cancel(transition.source, VansActionCancelReason::Interrupted, cancelError))
		{
			std::string ignored;
			Cancel(result.action, VansActionCancelReason::System, ignored);
			result.error = VansActionError::CommitFailed;
			result.action = {};
			result.message = "Action transition could not end its source: " + cancelError;
			return result;
		}
	}
	if (source)
		source->trace.push_back({ source->elapsedSeconds, source->state,
			"Transition executed: " + transition.name });
	return result;
}

void VansActionHost::ProcessTransitions()
{
	if (m_ProcessingTransitions || m_PendingTransitions.empty() || m_ShuttingDown) return;
	m_ProcessingTransitions = true;
	std::vector<PendingTransition> pending = std::move(m_PendingTransitions);
	m_PendingTransitions.clear();
	std::stable_sort(pending.begin(), pending.end(), [](const PendingTransition& left,
		const PendingTransition& right)
	{
		if (left.priority != right.priority) return left.priority > right.priority;
		return left.sequence < right.sequence;
	});
	std::vector<PendingTransition> retry;
	for (PendingTransition& transition : pending)
	{
		ActionInstance* source = m_Instances.Resolve(transition.source.value);
		if (!source || (!transition.allowEndedSource && !IsExecutingState(source->state))) continue;
		const double sourceTime = source->elapsedSeconds;
		if (transition.maximumSourceTime >= 0.0 &&
			sourceTime > transition.maximumSourceTime + 1e-12) continue;
		if (sourceTime + 1e-12 < transition.minimumSourceTime)
		{
			if (transition.expiresAt > m_ElapsedSeconds + 1e-12)
				retry.push_back(std::move(transition));
			continue;
		}
		const VansActionResult result = ExecuteTransition(transition);
		const bool retryable = result.error == VansActionError::RequirementsFailed ||
			result.error == VansActionError::CooldownActive ||
			result.error == VansActionError::ConcurrencyBlocked ||
			result.error == VansActionError::ConcurrencyRejected ||
			result.error == VansActionError::BudgetExceeded;
		if (!result && retryable && transition.expiresAt > m_ElapsedSeconds + 1e-12)
			retry.push_back(std::move(transition));
	}
	for (PendingTransition& transition : retry)
		m_PendingTransitions.push_back(std::move(transition));
	m_ProcessingTransitions = false;
}

void VansActionHost::QueueTerminalTransitions(
	VansActionHandle handle,
	const ActionInstance& instance,
	VansActionEndReason reason,
	VansActionError error)
{
	if (m_ShuttingDown || !instance.definition) return;
	const bool completed = reason == VansActionEndReason::Completed;
	const bool failed = reason == VansActionEndReason::Failed ||
		reason == VansActionEndReason::CommitFailed || reason == VansActionEndReason::TimedOut;
	if (!completed && !failed) return;
	bool queuedRule = false;
	for (const VansActionTransitionRule& rule : instance.definition->transitionRules)
	{
		if ((completed && rule.trigger != VansActionTransitionTrigger::Completed) ||
			(failed && rule.trigger != VansActionTransitionTrigger::Failed) ||
			instance.elapsedSeconds + 1e-12 < rule.minimumTimeSeconds ||
			(rule.maximumTimeSeconds >= 0.0 &&
				instance.elapsedSeconds > rule.maximumTimeSeconds) ||
			(HasQueryTerms(rule.requirements) && !m_Tags.Matches(rule.requirements))) continue;
		PendingTransition transition;
		transition.source = handle;
		transition.targetAction = rule.targetAction;
		transition.context = instance.context;
		transition.contextPatch = rule.contextPatch;
		transition.name = rule.name;
		transition.minimumSourceTime = rule.minimumTimeSeconds;
		transition.maximumSourceTime = rule.maximumTimeSeconds;
		transition.priority = rule.priority;
		transition.sequence = m_NextTransitionSequence++;
		transition.cancelSource = false;
		transition.inheritPrimaryTarget = rule.inheritPrimaryTarget;
		transition.allowEndedSource = true;
		m_PendingTransitions.push_back(std::move(transition));
		queuedRule = true;
		break;
	}
	const VansActionFailureFallback& fallback = instance.definition->failureFallback;
	const bool handlesError = fallback.errors.empty() ||
		std::find(fallback.errors.begin(), fallback.errors.end(), error) != fallback.errors.end();
	if (!queuedRule && failed && fallback.action && handlesError)
	{
		PendingTransition transition;
		transition.source = handle;
		transition.targetAction = fallback.action;
		transition.context = instance.context;
		transition.contextPatch = fallback.contextPatch;
		transition.name = "FailureFallback";
		transition.sequence = m_NextTransitionSequence++;
		transition.cancelSource = false;
		transition.inheritPrimaryTarget = fallback.inheritPrimaryTarget;
		transition.allowEndedSource = true;
		m_PendingTransitions.push_back(std::move(transition));
	}
}

std::size_t VansActionHost::ConcurrencyOccupancy(VansActionConcurrencyGroupId group) const
{
	const auto found = m_Concurrency.find(group);
	return found == m_Concurrency.end() ? 0 : found->second.size();
}

void VansActionHost::RemoveFromConcurrencyQueue(
	VansActionHandle handle,
	VansActionConcurrencyGroupId group)
{
	if (!group) return;
	const auto found = m_ConcurrencyQueues.find(group);
	if (found == m_ConcurrencyQueues.end()) return;
	auto& queue = found->second;
	queue.erase(std::remove(queue.begin(), queue.end(), handle), queue.end());
}

void VansActionHost::ProcessConcurrencyQueues(double deltaSeconds)
{
	if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0 || m_ConcurrencyQueues.empty()) return;
	std::vector<VansActionConcurrencyGroupId> groups;
	groups.reserve(m_ConcurrencyQueues.size());
	for (const auto& entry : m_ConcurrencyQueues) groups.push_back(entry.first);
	for (VansActionConcurrencyGroupId group : groups)
	{
		const auto queued = m_ConcurrencyQueues.find(group);
		if (queued == m_ConcurrencyQueues.end()) continue;
		const std::vector<VansActionHandle> timeoutCandidates(
			queued->second.begin(), queued->second.end());
		for (VansActionHandle handle : timeoutCandidates)
		{
			ActionInstance* instance = m_Instances.Resolve(handle.value);
			if (!instance || instance->state != VansActionInstanceState::Queued)
			{
				RemoveFromConcurrencyQueue(handle, group);
				continue;
			}
			instance->elapsedSeconds += deltaSeconds;
			const double timeout = instance->definition->concurrencyQueueTimeoutSeconds;
			if (timeout > 0.0 && instance->elapsedSeconds >= timeout)
				End(handle, *instance, VansActionEndReason::TimedOut,
					VansActionError::ConcurrencyQueueExpired,
					"Action concurrency queue entry expired");
		}

		std::size_t transitionGuard = 0;
		while (++transitionGuard <= m_Dependencies.limits.maximumActiveActions)
		{
			const auto currentQueue = m_ConcurrencyQueues.find(group);
			if (currentQueue == m_ConcurrencyQueues.end() || currentQueue->second.empty()) break;
			const VansActionHandle handle = currentQueue->second.front();
			ActionInstance* instance = m_Instances.Resolve(handle.value);
			if (!instance || instance->state != VansActionInstanceState::Queued || !instance->definition)
			{
				currentQueue->second.pop_front();
				continue;
			}
			if (ConcurrencyOccupancy(group) >= instance->definition->concurrencyLimit) break;
			GrantedSpec* spec = m_Specs.Resolve(instance->sourceSpec.value);
			if (!spec)
			{
				End(handle, *instance, VansActionEndReason::Failed,
					VansActionError::NotGranted, "Queued Action Spec is no longer granted");
				continue;
			}
			VansActionActivationRequest request;
			request.spec = instance->sourceSpec;
			request.context = instance->context;
			request.hasAuthority = instance->hasAuthority;
			request.locallyControlled = instance->locallyControlled;
			request.predicted = instance->predicted;
			VansActionResult validation;
			if (!ValidateActivation(request, *spec, validation))
			{
				End(handle, *instance, VansActionEndReason::Failed, validation.error,
					"Queued Action failed revalidation: " + validation.message);
				continue;
			}
			StartActivation(handle, request, *spec);
		}
		const auto currentQueue = m_ConcurrencyQueues.find(group);
		if (currentQueue != m_ConcurrencyQueues.end() && currentQueue->second.empty())
			m_ConcurrencyQueues.erase(currentQueue);
	}
}

std::optional<VansActionInstanceSnapshot> VansActionHost::Query(VansActionHandle handle) const
{
	if (const ActionInstance* instance = m_Instances.Resolve(handle.value))
		return BuildSnapshot(handle, *instance);
	for (auto it = m_History.rbegin(); it != m_History.rend(); ++it)
		if (it->handle == handle) return *it;
	return std::nullopt;
}

std::vector<VansActionInstanceSnapshot> VansActionHost::ActiveActions() const
{
	std::vector<VansActionInstanceSnapshot> result;
	m_Instances.ForEach([&](VansGenerationHandle handle, const ActionInstance& instance)
	{
		if (instance.state == VansActionInstanceState::Ended) return;
		result.push_back(BuildSnapshot({ handle }, instance));
	});
	return result;
}

VansActionInstanceSnapshot VansActionHost::BuildSnapshot(
	VansActionHandle handle,
	const ActionInstance& instance) const
{
	VansActionInstanceSnapshot snapshot;
	snapshot.handle = handle;
	snapshot.action = instance.definition ? instance.definition->id : VansActionId{};
	snapshot.definitionVersion = instance.definition ? instance.definition->definitionVersion : 0;
	snapshot.sourceSpec = instance.sourceSpec;
	snapshot.state = instance.state;
	snapshot.endReason = instance.endReason;
	snapshot.elapsedSeconds = instance.elapsedSeconds;
	snapshot.taskCount = instance.tasks.ActiveCount();
	snapshot.resourceCount = instance.resources.ActiveCount();
	snapshot.prediction = instance.context.predictionKey;
	snapshot.trace = instance.trace;
	snapshot.error = instance.error;
	snapshot.context = instance.context;
	if (const VansTargetData* targetData = m_TargetData.Resolve(instance.context.targetData))
	{
		snapshot.targetData = *targetData;
		snapshot.hasTargetData = true;
	}
	for (const auto& [field, value] : instance.variables.Values())
		snapshot.variables.push_back({ field, value });
	std::sort(snapshot.variables.begin(), snapshot.variables.end(),
		[](const auto& left, const auto& right) { return left.field < right.field; });
	snapshot.tasks = instance.tasks.Snapshot();
	snapshot.resources = instance.resources.Snapshot();
	if (instance.executor) snapshot.executor = instance.executor->DebugView();
	snapshot.recentEvents = instance.recentEvents;
	return snapshot;
}

std::vector<VansGrantedActionSpecSnapshot> VansActionHost::GrantedActions() const
{
	std::vector<VansGrantedActionSpecSnapshot> result;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		result.push_back({ { handle }, spec.definition->id, spec.definition->definitionVersion,
			spec.level, spec.inputBinding, spec.dynamicTags, spec.charges, spec.source,
			spec.persistence, spec.pendingRemoval });
	});
	return result;
}

bool VansActionHost::IsCooldownActive(VansActionId action) const
{
	const auto found = m_Cooldowns.find(action);
	if (found == m_Cooldowns.end()) return false;
	return std::any_of(found->second.begin(), found->second.end(),
		[](const CooldownState& state) { return state.remainingSeconds > 0.0; });
}

VansActionExecutionContext VansActionHost::BuildExecutionContext(
	VansActionHandle handle,
	ActionInstance& instance,
	double deltaSeconds)
{
	return { handle, instance.definition.get(), &instance.context, &instance.variables,
		&instance.tasks, &instance.resources, m_Dependencies.services, nullptr, deltaSeconds };
}

bool VansActionHost::ValidateCommitRequirements(
	const VansCompiledActionDefinition& definition,
	const VansActionContext& context,
	const VansTargetData* targetData,
	VansActionResult& result) const
{
	const auto compare = [](double left, double right, VansActionRequirementComparison operation)
	{
		const double tolerance = 1e-9 * (std::max)(1.0,
			(std::max)(std::abs(left), std::abs(right)));
		switch (operation)
		{
		case VansActionRequirementComparison::Less: return left < right;
		case VansActionRequirementComparison::LessOrEqual: return left <= right;
		case VansActionRequirementComparison::Equal: return std::abs(left - right) <= tolerance;
		case VansActionRequirementComparison::NotEqual: return std::abs(left - right) > tolerance;
		case VansActionRequirementComparison::Greater: return left > right;
		case VansActionRequirementComparison::GreaterOrEqual: return left >= right;
		}
		return false;
	};
	for (const VansActionRequirementDefinition& requirement : definition.commitRequirements)
	{
		bool satisfied = false;
		switch (requirement.kind)
		{
		case VansActionRequirementKind::Attribute:
			satisfied = compare(m_Attributes.Current(requirement.attribute),
				requirement.value, requirement.comparison);
			break;
		case VansActionRequirementKind::PrimaryTarget:
		{
			std::size_t count = context.primaryTarget.IsValid() ? 1u : 0u;
			if (count == 0 && targetData)
				for (const VansTargetDataValue& value : targetData->values)
					if ((std::holds_alternative<VansEntityHandle>(value) &&
						std::get<VansEntityHandle>(value).IsValid()) ||
						(std::holds_alternative<VansTargetHitResult>(value) &&
							std::get<VansTargetHitResult>(value).entity.IsValid())) ++count;
			satisfied = count >= requirement.minimumTargets;
			break;
		}
		case VansActionRequirementKind::TargetData:
			satisfied = targetData && targetData->values.size() >= requirement.minimumTargets;
			break;
		case VansActionRequirementKind::Service:
			satisfied = m_Dependencies.services &&
				static_cast<bool>(m_Dependencies.services->Resolve(requirement.service));
			break;
		}
		if (!satisfied)
		{
			result.error = requirement.kind == VansActionRequirementKind::PrimaryTarget ||
				requirement.kind == VansActionRequirementKind::TargetData
				? VansActionError::TargetInvalid : VansActionError::RequirementsFailed;
			result.message = "Action commit requirement failed";
			return false;
		}
	}
	return true;
}

bool VansActionHost::ValidateActivation(
	const VansActionActivationRequest& request,
	const GrantedSpec& spec,
	VansActionResult& result,
	bool ignoreConcurrencyOccupancy) const
{
	if (!m_Initialized || m_ShuttingDown)
	{
		result.error = VansActionError::InternalInvariant;
		result.message = "Action Host is not available";
		return false;
	}
	if (m_CommitFrozen)
	{
		result.error = VansActionError::CommitFailed;
		result.message = "Action Host commits are frozen after compensation failure";
		return false;
	}
	if (!SerializedValueFitsBudget(
		request.context.payload, m_Dependencies.limits.maximumPayloadBytes))
	{
		result.error = VansActionError::BudgetExceeded;
		result.message = "Action Context payload exceeds the Host byte or nesting budget";
		return false;
	}
	if (spec.pendingRemoval || spec.charges == 0)
	{
		result.error = VansActionError::NotGranted;
		result.message = "Action Spec is pending removal or has no charges";
		return false;
	}
	if (!m_Tags.Matches(spec.definition->activationRequirements))
	{
		result.error = VansActionError::RequirementsFailed;
		result.message = "Action activation Tag requirements failed";
		return false;
	}
	if (HasQueryTerms(spec.definition->blockedByTags) && m_Tags.Matches(spec.definition->blockedByTags))
	{
		result.error = VansActionError::RequirementsFailed;
		result.message = "Action is blocked by owned Tags";
		return false;
	}
	if (IsCooldownActive(spec.definition->id))
	{
		result.error = VansActionError::CooldownActive;
		result.message = "Action cooldown is active";
		return false;
	}
	if (spec.definition->authorityPolicy == VansActionAuthorityPolicy::AuthorityOnly && !request.hasAuthority)
	{
		result.error = VansActionError::AuthorityDenied;
		result.message = "Action requires authority";
		return false;
	}
	if (spec.definition->authorityPolicy == VansActionAuthorityPolicy::LocalOwner &&
		!request.locallyControlled)
	{
		result.error = VansActionError::AuthorityDenied;
		result.message = "Action requires local ownership";
		return false;
	}
	if (request.predicted && (!m_Dependencies.predictionEnabled ||
		spec.definition->replicationPolicy != VansActionReplicationPolicy::OwnerPredicted ||
		!request.locallyControlled || !request.context.predictionKey.IsValid()))
	{
		result.error = VansActionError::AuthorityDenied;
		result.message = !m_Dependencies.predictionEnabled
			? "Action prediction is disabled by project settings"
			: spec.definition->replicationPolicy != VansActionReplicationPolicy::OwnerPredicted
			? "Action does not allow owner prediction"
			: !request.locallyControlled
			? "Predicted Action requires local ownership"
			: "Predicted Action requires a valid PredictionKey";
		return false;
	}
	VansTargetData resolvedRequirementData;
	const VansTargetData* requirementTargetData = nullptr;
	if (spec.definition->targetingPolicy)
	{
		const VansTargetingPolicy* policy = m_Dependencies.targetingPolicies
			? m_Dependencies.targetingPolicies->Resolve(spec.definition->targetingPolicy) : nullptr;
		if (!policy || !m_Dependencies.targetingHandlers)
		{
			result.error = VansActionError::ServiceMissing;
			result.message = "Action TargetingPolicy or handler registry is unavailable";
			return false;
		}
		VansTargetData initial;
		if (request.context.targetData)
		{
			const VansTargetData* supplied = m_TargetData.Resolve(request.context.targetData);
			if (!supplied)
			{
				result.error = VansActionError::TargetInvalid;
				result.message = "Action Context TargetData handle is stale";
				return false;
			}
			initial = *supplied;
		}
		else if (request.context.primaryTarget.IsValid())
			initial.values.push_back(request.context.primaryTarget);
		const VansTargetingResult targeting = VansTargetingPipeline::Execute(
			*policy, request.context, *m_Dependencies.targetingHandlers, std::move(initial));
		if (!targeting)
		{
			result.error = targeting.error;
			result.message = targeting.message;
			return false;
		}
		resolvedRequirementData = targeting.data;
		requirementTargetData = &resolvedRequirementData;
	}
	else if (request.context.targetData)
	{
		requirementTargetData = m_TargetData.Resolve(request.context.targetData);
		if (!requirementTargetData)
		{
			result.error = VansActionError::TargetInvalid;
			result.message = "Action Context TargetData handle is stale";
			return false;
		}
	}
	if (!ValidateCommitRequirements(*spec.definition, request.context,
		requirementTargetData, result)) return false;
	if (!m_Dependencies.services->ValidateRequired(spec.definition->requiredServices, result.message))
	{
		result.error = VansActionError::ServiceMissing;
		return false;
	}
	for (const VansActionCostDefinition& cost : spec.definition->costs)
	{
		const double amount = cost.amount * spec.level;
		if (cost.kind == VansActionCostKind::Attribute &&
			m_Attributes.Current(cost.attribute) < amount)
		{
			result.error = VansActionError::CostUnavailable;
			result.message = "Action cost is unavailable";
			return false;
		}
		if (cost.kind != VansActionCostKind::Attribute)
		{
			if (!m_Dependencies.externalCosts)
			{
				result.error = VansActionError::ServiceMissing;
				result.message = "Action external cost provider is unavailable";
				return false;
			}
			VansActionExternalCostRequest external;
			external.kind = cost.kind;
			external.resource = cost.resource;
			external.amount = amount;
			external.context = request.context;
			external.payload = cost.payload;
			if (!m_Dependencies.externalCosts->CanCommit(external, result.message))
			{
				result.error = VansActionError::CostUnavailable;
				return false;
			}
		}
	}
	if (spec.definition->concurrencyGroup)
	{
		const bool full = ConcurrencyOccupancy(spec.definition->concurrencyGroup) >=
			spec.definition->concurrencyLimit;
		if (!ignoreConcurrencyOccupancy && full &&
			spec.definition->concurrencyPolicy != VansActionConcurrencyPolicy::Allow &&
			spec.definition->concurrencyPolicy != VansActionConcurrencyPolicy::CancelExisting)
		{
			result.error = spec.definition->concurrencyPolicy == VansActionConcurrencyPolicy::RejectNew ?
				VansActionError::ConcurrencyRejected : VansActionError::ConcurrencyBlocked;
			result.message = "Action concurrency group is occupied";
			return false;
		}
	}
	bool blockedByAction = false;
	m_Instances.ForEach([&](VansGenerationHandle, const ActionInstance& active)
	{
		if (active.state == VansActionInstanceState::Queued ||
			active.state == VansActionInstanceState::Ending || active.state == VansActionInstanceState::Ended) return;
		blockedByAction = blockedByAction || std::find(spec.definition->blockedActions.begin(),
			spec.definition->blockedActions.end(), active.definition->id) != spec.definition->blockedActions.end();
	});
	if (blockedByAction)
	{
		result.error = VansActionError::ConcurrencyBlocked;
		result.message = "Action is blocked by another active Action";
		return false;
	}
	return true;
}

bool VansActionHost::CommitActivation(
	VansActionHandle handle,
	GrantedSpec& spec,
	ActionInstance& instance,
	std::string& error)
{
	VansActionCommitTransaction transaction;
	const std::uint64_t source = instance.source;
	struct ExternalCommittedCost
	{
		VansActionCostDefinition definition;
		double amount = 0.0;
		std::shared_ptr<VansGenerationHandle> receipt;
	};
	std::vector<ExternalCommittedCost> externalCommittedCosts;
	for (const VansActionCostDefinition& cost : instance.definition->costs)
	{
		const double amount = cost.amount * spec.level;
		VansActionCommitStep step;
		if (cost.kind == VansActionCostKind::Attribute)
		{
			step.name = "Attribute cost";
			step.preflight = [this, cost, amount](std::string& message)
			{
				if (m_Attributes.Current(cost.attribute) < amount)
				{
					message = "insufficient Attribute";
					return false;
				}
				return true;
			};
			step.apply = [this, cost, amount](std::string&)
				{ return m_Attributes.AddBase(cost.attribute, -amount); };
			step.compensate = [this, cost, amount](std::string&)
				{ return m_Attributes.AddBase(cost.attribute, amount); };
		}
		else
		{
			if (!m_Dependencies.externalCosts)
			{
				error = "external cost provider is unavailable";
				return false;
			}
			VansActionExternalCostRequest request;
			request.kind = cost.kind;
			request.resource = cost.resource;
			request.amount = amount;
			request.context = instance.context;
			request.payload = cost.payload;
			auto receipt = std::make_shared<VansGenerationHandle>();
			IVansActionExternalCostProvider* provider = m_Dependencies.externalCosts;
			step.name = cost.kind == VansActionCostKind::Inventory
				? "Inventory cost" : "Reservation cost";
			step.preflight = [provider, request](std::string& message)
				{ return provider->CanCommit(request, message); };
			step.apply = [provider, request, receipt](std::string& message)
			{
				*receipt = provider->Commit(request, message);
				return receipt->IsValid();
			};
			step.compensate = [provider, receipt](std::string& message)
				{ return !receipt->IsValid() || provider->Settle(*receipt, true, message); };
			externalCommittedCosts.push_back({ cost, amount, std::move(receipt) });
		}
		if (!transaction.AddStep(std::move(step), error)) return false;
	}
	if (spec.charges >= 0)
	{
		VansActionCommitStep step;
		step.name = "Action charge";
		step.preflight = [&spec](std::string& message)
		{
			if (spec.charges <= 0) { message = "no Action charges"; return false; }
			return true;
		};
		step.apply = [&spec](std::string&) { --spec.charges; return true; };
		step.compensate = [&spec](std::string&) { ++spec.charges; return true; };
		if (!transaction.AddStep(std::move(step), error)) return false;
	}
	if (!instance.definition->grantedWhileRunning.empty())
	{
		VansActionCommitStep step;
		step.name = "running Tags";
		step.preflight = [this, &instance](std::string& message)
		{
			for (VansGameplayTagId tag : instance.definition->grantedWhileRunning)
				if (!m_Dependencies.tagDictionary->Resolve(tag)) { message = "running Tag is missing"; return false; }
			return true;
		};
		step.apply = [this, &instance, source](std::string& message)
		{
			m_Tags.BeginBatch();
			for (VansGameplayTagId tag : instance.definition->grantedWhileRunning)
			{
				if (!m_Tags.Add(tag, source))
				{
					m_Tags.EndBatch();
					m_Tags.RemoveSource(source);
					message = "failed to add running Tag";
					return false;
				}
			}
			m_Tags.EndBatch();
			return true;
		};
		step.compensate = [this, source](std::string&) { m_Tags.RemoveSource(source); return true; };
		if (!transaction.AddStep(std::move(step), error)) return false;
		VansActionResourceEntry resource;
		resource.type = "GameplayTags";
		resource.debugName = "Action running Tags";
		resource.release = [this, source] { m_Tags.RemoveSource(source); return true; };
		if (!instance.resources.Register(std::move(resource), error)) return false;
	}
	if (instance.definition->concurrencyGroup &&
		instance.definition->concurrencyPolicy != VansActionConcurrencyPolicy::Allow)
	{
		const VansActionConcurrencyGroupId group = instance.definition->concurrencyGroup;
		const std::uint32_t limit = instance.definition->concurrencyLimit;
		VansActionCommitStep step;
		step.name = "concurrency slot";
		step.preflight = [this, group, limit](std::string& message)
		{
			if (ConcurrencyOccupancy(group) >= limit)
			{
				message = "concurrency group reached its limit";
				return false;
			}
			return true;
		};
		step.apply = [this, group, handle](std::string&)
		{
			m_Concurrency[group].push_back(handle);
			return true;
		};
		step.compensate = [this, group, handle](std::string&)
		{
			const auto found = m_Concurrency.find(group);
			if (found != m_Concurrency.end())
			{
				auto& occupants = found->second;
				occupants.erase(std::remove(occupants.begin(), occupants.end(), handle), occupants.end());
				if (occupants.empty()) m_Concurrency.erase(found);
			}
			return true;
		};
		if (!transaction.AddStep(std::move(step), error)) return false;
		VansActionResourceEntry resource;
		resource.type = "Concurrency";
		resource.debugName = "Action concurrency slot";
		resource.release = [this, group, handle]
		{
			const auto found = m_Concurrency.find(group);
			if (found != m_Concurrency.end())
			{
				auto& occupants = found->second;
				occupants.erase(std::remove(occupants.begin(), occupants.end(), handle), occupants.end());
				if (occupants.empty()) m_Concurrency.erase(found);
			}
			return true;
		};
		if (!instance.resources.Register(std::move(resource), error)) return false;
	}
	if (!instance.definition->cooldowns.empty())
	{
		const VansActionId actionId = instance.definition->id;
		const std::vector<VansActionCooldownDefinition> cooldowns = instance.definition->cooldowns;
		VansActionCommitStep step;
		step.name = "cooldowns";
		step.preflight = [this, actionId, cooldowns](std::string& message)
		{
			if (IsCooldownActive(actionId)) { message = "cooldown is active"; return false; }
			for (const VansActionCooldownDefinition& cooldown : cooldowns)
			{
				if (!std::isfinite(cooldown.durationSeconds) || cooldown.durationSeconds <= 0.0)
				{
					message = "cooldown duration is invalid";
					return false;
				}
				if (cooldown.cooldownTag && !m_Dependencies.tagDictionary->Resolve(cooldown.cooldownTag))
				{
					message = "cooldown Tag is missing";
					return false;
				}
			}
			return true;
		};
		step.apply = [this, actionId, cooldowns](std::string& message)
		{
			std::vector<CooldownState> states;
			states.reserve(cooldowns.size());
			m_Tags.BeginBatch();
			for (std::size_t index = 0; index < cooldowns.size(); ++index)
			{
				const VansActionCooldownDefinition& cooldown = cooldowns[index];
				const std::uint64_t source = SourceForCooldown(actionId, index);
				if (cooldown.cooldownTag && !m_Tags.Add(cooldown.cooldownTag, source))
				{
					m_Tags.EndBatch();
					for (const CooldownState& applied : states) m_Tags.RemoveSource(applied.tagSource);
					message = "failed to add cooldown Tag";
					return false;
				}
				states.push_back({ cooldown.durationSeconds, cooldown.cooldownTag, source });
			}
			m_Tags.EndBatch();
			m_Cooldowns[actionId] = std::move(states);
			return true;
		};
		step.compensate = [this, actionId](std::string&)
		{
			const auto found = m_Cooldowns.find(actionId);
			if (found != m_Cooldowns.end())
				for (const CooldownState& state : found->second) m_Tags.RemoveSource(state.tagSource);
			m_Cooldowns.erase(actionId);
			return true;
		};
		if (!transaction.AddStep(std::move(step), error)) return false;
	}
	struct EffectCommitState
	{
		std::vector<VansAttributeSnapshot> attributes;
		VansActiveEffectHandle active;
		bool applied = false;
	};
	auto removeOnEnd = std::make_shared<std::vector<VansActiveEffectHandle>>();
	for (const VansActionEffectReference& reference : instance.definition->commitEffects)
	{
		auto state = std::make_shared<EffectCommitState>();
		VansActionCommitStep step;
		step.name = "commit Effect";
		step.preflight = [this, reference](std::string& message)
		{
			if (!m_Dependencies.effectRegistry || !m_Dependencies.effectRegistry->Resolve(reference.effect))
			{
				message = "commit Effect is missing";
				return false;
			}
			return true;
		};
		step.apply = [this, &instance, reference, source, state, removeOnEnd](std::string& message)
		{
			state->attributes = m_Attributes.Capture();
			VansEffectSpec effectSpec;
			effectSpec.definition = m_Dependencies.effectRegistry->Resolve(reference.effect);
			effectSpec.context = instance.context;
			effectSpec.targetData = instance.context.targetData;
			effectSpec.source = source;
			const VansEffectApplicationResult applied = m_Effects.Apply(effectSpec);
			if (!applied)
			{
				m_Attributes.Restore(state->attributes);
				message = applied.message;
				return false;
			}
			state->active = applied.active;
			state->applied = true;
			if (reference.removeOnEnd && state->active) removeOnEnd->push_back(state->active);
			return true;
		};
		step.compensate = [this, state, removeOnEnd](std::string&)
		{
			if (state->active)
			{
				std::string ignored;
				m_Effects.Remove(state->active, ignored);
				removeOnEnd->erase(std::remove(removeOnEnd->begin(), removeOnEnd->end(), state->active),
					removeOnEnd->end());
			}
			if (state->applied) m_Attributes.Restore(state->attributes);
			return true;
		};
		if (!transaction.AddStep(std::move(step), error)) return false;
	}
	if (!instance.definition->commitEffects.empty())
	{
		VansActionResourceEntry resource;
		resource.type = "GameplayEffects";
		resource.debugName = "Action remove-on-end Effects";
		resource.release = [this, removeOnEnd]
		{
			for (auto it = removeOnEnd->rbegin(); it != removeOnEnd->rend(); ++it)
			{
				std::string ignored;
				m_Effects.Remove(*it, ignored);
			}
			removeOnEnd->clear();
			return true;
		};
		if (!instance.resources.Register(std::move(resource), error)) return false;
	}
	if (!transaction.Commit(error))
	{
		if (transaction.CompensationFailed()) m_CommitFrozen = true;
		return false;
	}
	for (const VansActionCostDefinition& cost : instance.definition->costs)
		if (cost.kind == VansActionCostKind::Attribute)
			instance.committedCosts.push_back({ cost.attribute, cost.amount * spec.level,
				cost.refundPolicy, cost.kind, {} });
	for (const ExternalCommittedCost& cost : externalCommittedCosts)
		instance.committedCosts.push_back({ {}, cost.amount, cost.definition.refundPolicy,
			cost.definition.kind, *cost.receipt });
	if (spec.charges >= 0) instance.committedCharge = 1;
	return true;
}

void VansActionHost::End(
	VansActionHandle handle,
	ActionInstance& instance,
	VansActionEndReason reason,
	VansActionError error,
	std::string message)
{
	if (instance.state == VansActionInstanceState::Ending ||
		instance.state == VansActionInstanceState::Ended) return;
	instance.endReason = reason;
	instance.error = error;
	if (instance.definition)
		RemoveFromConcurrencyQueue(handle, instance.definition->concurrencyGroup);
	Transition(instance, VansActionInstanceState::Ending, std::move(message));
	instance.tasks.StopAcceptingTasks();
	instance.tasks.CancelAll();
	if (instance.executor)
	{
		VansActionExecutionContext execution = BuildExecutionContext(handle, instance, 0.0);
		instance.executor->Finish(execution, reason);
	}
	std::vector<std::string> releaseErrors;
	if (!instance.resources.ReleaseAll(releaseErrors))
	{
		for (const std::string& releaseError : releaseErrors)
			instance.trace.push_back({ instance.elapsedSeconds, VansActionInstanceState::Ending, releaseError });
	}
	RefundCosts(instance, reason);
	Transition(instance, VansActionInstanceState::Ended, "Action ended");
	QueueTerminalTransitions(handle, instance, reason, error);
	VansEventBus::Get().Enqueue(VansActionEndedEvent{
		m_Owner, handle, instance.definition->id, reason, error, instance.context.predictionKey },
		VansEventLane::GameLogic);
	m_History.push_back(*Query(handle));
	while (m_History.size() > 256) m_History.pop_front();
	if (instance.context.targetData)
	{
		m_TargetData.Release(instance.context.targetData);
		instance.context.targetData = {};
	}
	m_DeferredRecycle.push_back(handle);
}

void VansActionHost::Transition(
	ActionInstance& instance,
	VansActionInstanceState state,
	std::string message)
{
	instance.state = state;
	instance.trace.push_back({ instance.elapsedSeconds, state, std::move(message) });
}

void VansActionHost::RefundCosts(ActionInstance& instance, VansActionEndReason reason)
{
	for (const CommittedCost& cost : instance.committedCosts)
	{
		const bool refund = cost.policy == VansActionCostRefundPolicy::Always ||
			(cost.policy == VansActionCostRefundPolicy::OnCancel && IsCancellationEnd(reason));
		if (cost.kind == VansActionCostKind::Attribute)
		{
			if (refund) m_Attributes.AddBase(cost.attribute, cost.amount);
			continue;
		}
		std::string error;
		if (!m_Dependencies.externalCosts ||
			!m_Dependencies.externalCosts->Settle(cost.externalReceipt, refund, error))
			m_CommitFrozen = true;
	}
	if (instance.committedCharge > 0 && reason != VansActionEndReason::Completed)
	{
		if (GrantedSpec* spec = m_Specs.Resolve(instance.sourceSpec.value))
			if (spec->charges >= 0) spec->charges += instance.committedCharge;
	}
}

bool VansActionHost::HasRunningActionForSpec(VansActionSpecHandle spec) const
{
	bool running = false;
	m_Instances.ForEach([&](VansGenerationHandle, const ActionInstance& instance)
	{
		running = running || (instance.sourceSpec == spec &&
			instance.state != VansActionInstanceState::Ending &&
			instance.state != VansActionInstanceState::Ended);
	});
	return running;
}

void VansActionHost::ReleaseDeferredSpecs()
{
	std::vector<VansActionSpecHandle> releases;
	m_Specs.ForEach([&](VansGenerationHandle handle, const GrantedSpec& spec)
	{
		const VansActionSpecHandle typed{ handle };
		if (spec.pendingRemoval && !HasRunningActionForSpec(typed)) releases.push_back(typed);
	});
	for (VansActionSpecHandle handle : releases) m_Specs.Release(handle.value);
}

void VansActionHost::RecycleEnded()
{
	const std::vector<VansActionHandle> recycle = std::move(m_DeferredRecycle);
	m_DeferredRecycle.clear();
	for (VansActionHandle handle : recycle) m_Instances.Release(handle.value);
}

void VansActionHost::TickCooldowns(double deltaSeconds)
{
	std::vector<VansActionId> expired;
	for (auto& entry : m_Cooldowns)
	{
		auto& states = entry.second;
		for (CooldownState& state : states)
		{
			state.remainingSeconds -= deltaSeconds;
			if (state.remainingSeconds <= 0.0) m_Tags.RemoveSource(state.tagSource);
		}
		states.erase(std::remove_if(states.begin(), states.end(), [](const CooldownState& state)
		{
			return state.remainingSeconds <= 0.0;
		}), states.end());
		if (states.empty()) expired.push_back(entry.first);
	}
	for (VansActionId action : expired)
		m_Cooldowns.erase(action);
}

std::uint64_t VansActionHost::SourceForHandle(VansActionHandle handle)
{
	return (static_cast<std::uint64_t>(handle.value.generation) << 32) |
		(static_cast<std::uint64_t>(handle.value.index) + 1ull);
}

std::uint64_t VansActionHost::SourceForActionSet(VansActionSetHandle handle)
{
	return 0x8000000000000000ull |
		(static_cast<std::uint64_t>(handle.value.generation) << 32) |
		(static_cast<std::uint64_t>(handle.value.index) + 1ull);
}

std::uint64_t VansActionHost::SourceForCooldown(VansActionId action, std::size_t index)
{
	std::uint64_t value = action.value ^ 0x434f4f4c444f574eull;
	value ^= static_cast<std::uint64_t>(index + 1) + 0x9e3779b97f4a7c15ull +
		(value << 6) + (value >> 2);
	return value == 0 ? 1 : value;
}
}
