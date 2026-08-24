#include "GAFContractTests.h"

#include "../EngineCore/GameplayActionCore/VansActionDefinition.h"
#include "../EngineCore/GameplayActionCore/VansActionHost.h"
#include "../EngineCore/GameplayActionCore/VansActionRoutingService.h"
#include "../EngineCore/GameplayActionCore/VansActionScheduler.h"
#include "../EngineCore/GameplayActionCore/VansGameplayRuntime.h"
#include "../EngineCore/GameplayActionCore/VansActionResourceLedger.h"
#include "../EngineCore/GameplayActionCore/VansActionServices.h"
#include "../EngineCore/GameplayActionCore/VansActionSystem.h"
#include "../EngineCore/GameplayActionDebug/VansGameplayActionDebug.h"
#include "../EngineCore/GameplayActionNetwork/VansGameplayActionNetwork.h"
#include "../EngineCore/GameplayActionAdapters/VansStandardActionServices.h"
#include "../EngineCore/GameplayActionAdapters/Camera/VansCameraActionService.h"
#include "../EngineCore/CameraGameplayAction/VansCameraActionGraphNodes.h"
#include "../EngineCore/GameplayActionExecution/VansActionTask.h"
#include "../EngineCore/GameplayActionExecution/VansActionExecutionGraph.h"
#include "../EngineCore/GameplayActionTimeline/VansGameplayActionTimelineIntegration.h"
#include "../EngineCore/GameplayAttributes/VansGameplayAttributes.h"
#include "../EngineCore/GameplayCues/VansGameplayCues.h"
#include "../EngineCore/GameplayEffects/VansGameplayEffects.h"
#include "../EngineCore/GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayAssetCompiler.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayAssetLibrary.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayAssetMigration.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayAssetSchema.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayAssetStorage.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayActionHostAuthoring.h"
#include "../EngineCore/GameplayTags/VansGameplayTags.h"
#include "../EngineCore/GameplayTargeting/VansGameplayTargeting.h"
#include "../EngineCore/PackagingCore/VansGameplayAssetPackageCooker.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include "../EngineCore/AssetCore/Storage/VansJsonFileStorage.h"
#include "../EngineCore/EditorCore/VansAssetDocumentTypeRegistry.h"
#include "../EngineCore/EditorCore/GameplayAction/VansGameplayAssetEditorModel.h"
#include "../EngineCore/EngineAPILayer/Private/GameplayActionAuthoringBridge.h"
#include "../EngineCore/EventCore/VansEventBus.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/ScriptCore/VansLuaGameplayActionBridge.h"
#include "../EngineCore/TimelineCore/VansTimelineCompiler.h"
#include "../EngineCore/TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineApplierRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineClockRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineSessionService.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineTrackDescriptorRegistry.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

extern "C"
{
#include <lauxlib.h>
#include <lualib.h>
}

namespace
{
bool ExpectGAF(bool value, const char* message)
{
	if (!value) std::cerr << "[GAF] " << message << '\n';
	return value;
}

class ProbeCueAdapter final : public Vans::IVansGameplayCueAdapter
{
public:
	explicit ProbeCueAdapter(std::string stableName)
		: m_Name(std::move(stableName))
		, m_Id(Vans::VansMakeStableId<Vans::VansCueIdTag>(m_Name)) {}
	Vans::VansCueId CueId() const override { return m_Id; }
	std::string_view StableName() const override { return m_Name; }
	bool Execute(const Vans::VansGameplayCueKey&, Vans::VansGameplayCueScope,
		const Vans::VansGameplayCueParameters&, std::string&) override
	{
		++executeCount;
		return true;
	}
	Vans::VansGenerationHandle Add(const Vans::VansGameplayCueKey&, Vans::VansGameplayCueScope,
		const Vans::VansGameplayCueParameters& parameters, std::string&) override
	{
		++addCount;
		lastIntensity = parameters.intensity;
		return { nextResource++, 1 };
	}
	bool Update(Vans::VansGenerationHandle resource,
		const Vans::VansGameplayCueParameters& parameters, std::string&) override
	{
		if (!resource) return false;
		++updateCount;
		lastIntensity = parameters.intensity;
		return true;
	}
	bool Remove(Vans::VansGenerationHandle resource, std::string&) override
	{
		if (!resource) return false;
		++removeCount;
		return true;
	}

	std::string m_Name;
	Vans::VansCueId m_Id;
	std::uint32_t nextResource = 0;
	int executeCount = 0;
	int addCount = 0;
	int updateCount = 0;
	int removeCount = 0;
	double lastIntensity = 0.0;
};

class ProbeAcquireTargets final : public Vans::IVansTargetingStepHandler
{
public:
	Vans::VansActionGraphNodeTypeId TypeId() const override
	{
		return Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Targeting.TestAcquire");
	}
	std::string_view StableName() const override { return "Targeting.TestAcquire"; }
	bool Execute(const Vans::VansTargetingStep&, const Vans::VansActionContext&,
		std::vector<Vans::VansTargetDataValue>& values, std::string&) const override
	{
		values.push_back(Vans::VansEntityHandle{ 1, 1 });
		values.push_back(Vans::VansEntityHandle{ 2, 1 });
		values.push_back(Vans::VansEntityHandle{ 3, 1 });
		return true;
	}
};

class ProbeLimitTargets final : public Vans::IVansTargetingStepHandler
{
public:
	Vans::VansActionGraphNodeTypeId TypeId() const override
	{
		return Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Targeting.TestLimit");
	}
	std::string_view StableName() const override { return "Targeting.TestLimit"; }
	bool Execute(const Vans::VansTargetingStep&, const Vans::VansActionContext&,
		std::vector<Vans::VansTargetDataValue>& values, std::string&) const override
	{
		if (values.size() > 1) values.resize(1);
		return true;
	}
};

class ProbeActionService final : public Vans::IVansActionService
{
public:
	ProbeActionService()
	{
		capability.service = Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Probe");
		capability.stableName = "Service.Probe";
		capability.prediction = Vans::VansActionServicePredictionSupport::PredictableWithRollback;
		capability.commands.push_back("Probe.Run");
	}
	const Vans::VansActionServiceCapability& Capability() const override { return capability; }
	Vans::VansActionCommandResult Execute(const Vans::VansActionCommand& command) override
	{
		++executeCount;
		lastCommand = command.stableName;
		return { Vans::VansActionError::None, { 4, 1 },
			Vans::VansSerializedValue::Object({}), {} };
	}
	bool Release(Vans::VansGenerationHandle resource, std::string&) override
	{
		if (!resource) return false;
		++releaseCount;
		return true;
	}

	Vans::VansActionServiceCapability capability;
	int executeCount = 0;
	int releaseCount = 0;
	std::string lastCommand;
};

struct ProbeExecutorState
{
	int tickCount = 0;
	int eventCount = 0;
	int finishCount = 0;
};

class ProbeRunningExecutor final : public Vans::IVansActionExecutor
{
public:
	explicit ProbeRunningExecutor(std::shared_ptr<ProbeExecutorState> state)
		: m_State(std::move(state)) {}
	Vans::VansActionExecutorResult Start(Vans::VansActionExecutionContext&) override
	{
		return { Vans::VansActionExecutorStatus::Waiting };
	}
	Vans::VansActionExecutorResult Tick(Vans::VansActionExecutionContext&) override
	{
		return { ++m_State->tickCount >= 2 ? Vans::VansActionExecutorStatus::Succeeded :
			Vans::VansActionExecutorStatus::Running };
	}
	bool RequestCancel(Vans::VansActionExecutionContext&, Vans::VansActionCancelReason) override
	{
		return true;
	}
	void OnEvent(Vans::VansActionExecutionContext&, const Vans::VansActionEvent&) override
	{
		++m_State->eventCount;
	}
	void Finish(Vans::VansActionExecutionContext&, Vans::VansActionEndReason) override
	{
		++m_State->finishCount;
	}
private:
	std::shared_ptr<ProbeExecutorState> m_State;
};

class ProbeFailExecutor final : public Vans::IVansActionExecutor
{
public:
	Vans::VansActionExecutorResult Start(Vans::VansActionExecutionContext&) override
	{
		return { Vans::VansActionExecutorStatus::Failed,
			Vans::VansActionError::ExecutionFailed, "requested failure" };
	}
	Vans::VansActionExecutorResult Tick(Vans::VansActionExecutionContext&) override
	{
		return { Vans::VansActionExecutorStatus::Failed,
			Vans::VansActionError::ExecutionFailed, "requested failure" };
	}
	bool RequestCancel(Vans::VansActionExecutionContext&, Vans::VansActionCancelReason) override
	{
		return true;
	}
	void OnEvent(Vans::VansActionExecutionContext&, const Vans::VansActionEvent&) override {}
	void Finish(Vans::VansActionExecutionContext&, Vans::VansActionEndReason) override {}
};

class ProbeGraphImmediateNode final : public Vans::IVansActionGraphNodeHandler
{
public:
	Vans::VansActionGraphNodeTypeId TypeId() const override
	{
		return Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Graph.Probe.Immediate");
	}
	std::string_view StableName() const override { return "Graph.Probe.Immediate"; }
	bool SupportsPrediction() const override { return true; }
	Vans::VansActionGraphNodeResult Start(Vans::VansActionExecutionContext&,
		const Vans::VansCompiledActionGraphNode&, Vans::VansSerializedValue&) const override
	{
		return { Vans::VansActionGraphNodeStatus::Succeeded, "Success" };
	}
	Vans::VansActionGraphNodeResult Tick(Vans::VansActionExecutionContext&,
		const Vans::VansCompiledActionGraphNode&, Vans::VansSerializedValue&) const override
	{
		return { Vans::VansActionGraphNodeStatus::Succeeded, "Success" };
	}
	void Cancel(Vans::VansActionExecutionContext&, const Vans::VansCompiledActionGraphNode&,
		Vans::VansSerializedValue&) const override {}
};

class ProbeGraphWaitNode final : public Vans::IVansActionGraphNodeHandler
{
public:
	Vans::VansActionGraphNodeTypeId TypeId() const override
	{
		return Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Graph.Probe.Wait");
	}
	std::string_view StableName() const override { return "Graph.Probe.Wait"; }
	bool SupportsPrediction() const override { return true; }
	Vans::VansActionGraphNodeResult Start(Vans::VansActionExecutionContext&,
		const Vans::VansCompiledActionGraphNode&, Vans::VansSerializedValue& state) const override
	{
		state = Vans::VansSerializedValue::Int(0);
		return { Vans::VansActionGraphNodeStatus::Waiting };
	}
	Vans::VansActionGraphNodeResult Tick(Vans::VansActionExecutionContext&,
		const Vans::VansCompiledActionGraphNode&, Vans::VansSerializedValue& state) const override
	{
		++state.intValue;
		return { Vans::VansActionGraphNodeStatus::Succeeded, "Success" };
	}
	void Cancel(Vans::VansActionExecutionContext&, const Vans::VansCompiledActionGraphNode&,
		Vans::VansSerializedValue&) const override {}
};
}

bool TestGAFGameplayTagsContract()
{
	Vans::VansGameplayTagDictionary dictionary;
	std::string error;
	if (!dictionary.Register("State", "运行时状态", false, {}, error) ||
		!dictionary.Register("State.Combat", "战斗状态", false, {}, error) ||
		!dictionary.Register("State.Combat.Aiming", "瞄准状态", false, {}, error) ||
		!dictionary.Register("State.Combat.LegacyAim", "旧瞄准状态", true,
			"State.Combat.Aiming", error) || !dictionary.Seal(error))
	{
		return ExpectGAF(false, error.c_str());
	}
	const auto* state = dictionary.Find("State");
	const auto* aiming = dictionary.Find("State.Combat.Aiming");
	if (!ExpectGAF(state && aiming && dictionary.IsDescendantOrEqual(aiming->id, state->id),
		"层级 Tag 关系错误")) return false;
	if (!ExpectGAF(dictionary.ExpandWildcard("State.Combat.*").size() == 2,
		"Tag wildcard 展开错误")) return false;

	Vans::VansGameplayTagContainer container(&dictionary);
	int notificationCount = 0;
	std::size_t lastChangedCount = 0;
	container.SetChangedCallback([&](const auto& changed)
	{
		++notificationCount;
		lastChangedCount = changed.size();
	});
	container.BeginBatch();
	if (!container.Add(aiming->id, 11) || !container.Add(aiming->id, 12, 2)) return false;
	container.EndBatch();
	if (!ExpectGAF(notificationCount == 1 && lastChangedCount == 1 &&
		container.CountExact(aiming->id) == 3 && container.Has(state->id),
		"Tag 来源计数或批量通知错误")) return false;
	Vans::VansGameplayTagQuery query;
	query.all.push_back(state->id);
	query.none.push_back(dictionary.Find("State.Combat.LegacyAim")->id);
	if (!ExpectGAF(container.Matches(query), "TagQuery 匹配错误")) return false;
	if (!ExpectGAF(container.RemoveSource(11) == 1 && container.CountExact(aiming->id) == 2,
		"按来源移除 Tag 错误")) return false;
	return ExpectGAF(!container.Remove(aiming->id, 12, 3) &&
		container.Remove(aiming->id, 12, 2) && !container.Has(aiming->id),
		"Tag 计数下溢保护错误");
}

bool TestGAFAttributesContract()
{
	Vans::VansAttributeRegistry registry;
	Vans::VansAttributeDefinition health;
	health.name = "Character.Health";
	health.defaultValue = 100.0;
	health.minimum = 0.0;
	health.maximum = 200.0;
	health.hasMinimum = true;
	health.hasMaximum = true;
	std::string error;
	if (!registry.Register(health, error) || !registry.Seal(error))
		return ExpectGAF(false, error.c_str());
	const Vans::VansAttributeId healthId = registry.Definitions().front().id;
	Vans::VansAttributeService attributes(&registry);
	if (!attributes.InitializeDefaults(error)) return ExpectGAF(false, error.c_str());
	if (!attributes.SetBase(healthId, 150.0)) return false;
	Vans::VansAttributeModifierDesc additive;
	additive.attribute = healthId;
	additive.magnitude = 10.0;
	additive.source = 1;
	const auto additiveHandle = attributes.AddModifier(additive);
	Vans::VansAttributeModifierDesc multiplier;
	multiplier.attribute = healthId;
	multiplier.operation = Vans::VansAttributeModifierOperation::Multiplicative;
	multiplier.magnitude = 2.0;
	multiplier.source = 2;
	const auto multiplierHandle = attributes.AddModifier(multiplier);
	if (!ExpectGAF(additiveHandle && multiplierHandle &&
		std::abs(attributes.Current(healthId) - 200.0) < 0.0001,
		"Attribute 聚合顺序或 Clamp 错误")) return false;
	Vans::VansAttributeModifierDesc overrideValue;
	overrideValue.attribute = healthId;
	overrideValue.operation = Vans::VansAttributeModifierOperation::Override;
	overrideValue.magnitude = 50.0;
	overrideValue.priority = 100;
	overrideValue.source = 3;
	const auto overrideHandle = attributes.AddModifier(overrideValue);
	if (!ExpectGAF(std::abs(attributes.Current(healthId) - 50.0) < 0.0001,
		"Attribute Override 阶段错误")) return false;
	if (!attributes.RemoveModifier(overrideHandle) || attributes.RemoveModifier(overrideHandle))
		return ExpectGAF(false, "Attribute modifier generation handle 未阻止重复释放");
	if (!attributes.RemoveModifier(multiplierHandle)) return false;
	if (!ExpectGAF(std::abs(attributes.Current(healthId) - 160.0) < 0.0001,
		"Attribute modifier 移除后未重算")) return false;
	const auto snapshot = attributes.Capture();
	attributes.SetBase(healthId, 20.0);
	attributes.Restore(snapshot);
	return ExpectGAF(std::abs(attributes.Current(healthId) - 160.0) < 0.0001 &&
		attributes.RemoveModifier(additiveHandle), "Attribute snapshot 恢复错误");
}

bool TestGAFCuesAndEffectsContract()
{
	Vans::VansGameplayTagDictionary tagDictionary;
	std::string error;
	if (!tagDictionary.Register("Effect", {}, false, {}, error) ||
		!tagDictionary.Register("Effect.Active", {}, false, {}, error) ||
		!tagDictionary.Seal(error)) return ExpectGAF(false, error.c_str());
	Vans::VansGameplayTagContainer tags(&tagDictionary);

	Vans::VansAttributeRegistry attributeRegistry;
	Vans::VansAttributeDefinition health;
	health.name = "Character.Health";
	health.defaultValue = 100.0;
	health.minimum = 0.0;
	health.hasMinimum = true;
	Vans::VansAttributeDefinition power;
	power.name = "Character.Power";
	power.defaultValue = 4.0;
	if (!attributeRegistry.Register(health, error) ||
		!attributeRegistry.Register(power, error) || !attributeRegistry.Seal(error)) return false;
	Vans::VansAttributeService attributes(&attributeRegistry);
	if (!attributes.InitializeDefaults(error)) return false;
	const Vans::VansAttributeId healthId = attributeRegistry.Definitions().front().id;
	const Vans::VansAttributeId powerId = attributeRegistry.Definitions()[1].id;

	auto adapter = std::make_shared<ProbeCueAdapter>("Cue.Effect.Active");
	Vans::VansGameplayCueRegistry cueRegistry;
	if (!cueRegistry.Register(adapter, error) || !cueRegistry.Seal(error)) return false;
	Vans::VansGameplayCueService cues(&cueRegistry);
	Vans::VansTargetDataStore targetData;
	Vans::VansGameplayEffectService effects(&attributes, &tags, &cues, 256, &targetData);
	auto definition = std::make_shared<Vans::VansEffectDefinition>();
	definition->id = Vans::VansMakeStableId<Vans::VansEffectIdTag>("Effect.PeriodicDamage");
	definition->name = "Effect.PeriodicDamage";
	definition->durationPolicy = Vans::VansEffectDurationPolicy::Duration;
	definition->durationSeconds = 1.0;
	definition->periodSeconds = 0.25;
	definition->stackingPolicy = Vans::VansEffectStackingPolicy::AggregateBySource;
	definition->maximumStacks = 2;
	definition->grantedTags.push_back(tagDictionary.Find("Effect.Active")->id);
	definition->modifiers.push_back({ healthId,
		Vans::VansAttributeModifierOperation::Additive, -10.0, 0 });
	definition->persistentCues.push_back(adapter->CueId());
	definition->periodicCues.push_back(adapter->CueId());
	Vans::VansEffectSpec spec;
	spec.definition = definition;
	spec.source = 77;
	spec.context.predictionKey = { 1, 9 };
	const auto first = effects.Apply(spec);
	const auto second = effects.Apply(spec);
	if (!ExpectGAF(first && first.active && second && second.stacked && second.active == first.active &&
		effects.ActiveCount() == 1 && std::abs(attributes.Current(healthId) - 100.0) < 0.0001 &&
		tags.CountExact(tagDictionary.Find("Effect.Active")->id) == 2 &&
		adapter->addCount == 1 && adapter->updateCount == 1 && adapter->lastIntensity == 2.0,
		"Periodic Effect incorrectly installed a persistent Attribute modifier")) return false;
	const auto overflow = effects.Apply(spec);
	if (!ExpectGAF(!overflow && overflow.error == Vans::VansActionError::ConcurrencyBlocked,
		"Effect overflow policy 未阻止超限堆叠")) return false;
	effects.Tick(0.25);
	if (!ExpectGAF(std::abs(attributes.Base(healthId) - 80.0) < 0.0001 && adapter->executeCount == 1,
		"Periodic Effect 未按堆叠数执行")) return false;
	effects.Tick(0.8);
	if (!ExpectGAF(effects.ActiveCount() == 0 && std::abs(attributes.Current(healthId) - 20.0) < 0.0001 &&
		!tags.Has(tagDictionary.Find("Effect.Active")->id) && cues.ActiveCount() == 0 &&
		adapter->executeCount == 4 && adapter->removeCount == 1,
		"Periodic Effect did not catch up every pulse through its exact expiration boundary")) return false;
	if (!ExpectGAF(!effects.Remove(first.active, error), "过期 Effect handle 仍然可用")) return false;
	auto replaceDefinition = std::make_shared<Vans::VansEffectDefinition>();
	replaceDefinition->id = Vans::VansMakeStableId<Vans::VansEffectIdTag>("Effect.ReplaceOldest");
	replaceDefinition->name = "Effect.ReplaceOldest";
	replaceDefinition->durationPolicy = Vans::VansEffectDurationPolicy::Duration;
	replaceDefinition->durationSeconds = 10.0;
	replaceDefinition->stackingPolicy = Vans::VansEffectStackingPolicy::AggregateByTarget;
	replaceDefinition->overflowPolicy = Vans::VansEffectOverflowPolicy::ReplaceOldest;
	replaceDefinition->maximumStacks = 2;
	replaceDefinition->modifiers.push_back({ healthId,
		Vans::VansAttributeModifierOperation::Additive, 10.0, 0 });
	Vans::VansEffectSpec replaceSpec;
	replaceSpec.definition = replaceDefinition;
	replaceSpec.source = 1;
	replaceSpec.level = 1.0;
	const auto replacement = effects.Apply(replaceSpec);
	replaceSpec.source = 2;
	replaceSpec.level = 2.0;
	const auto stackedReplacement = effects.Apply(replaceSpec);
	replaceSpec.source = 3;
	replaceSpec.level = 3.0;
	const auto overflowReplacement = effects.Apply(replaceSpec);
	if (!ExpectGAF(replacement && stackedReplacement && overflowReplacement &&
		replacement.active == stackedReplacement.active &&
		replacement.active == overflowReplacement.active &&
		std::abs(attributes.Current(healthId) - 70.0) < 0.0001 &&
		effects.Snapshot().front().stacks == 2 && effects.Snapshot().front().source == 3,
		"ReplaceOldest did not preserve per-source and per-level stack contributions")) return false;
	if (!ExpectGAF(effects.RemoveBySource(2) == 1 &&
		std::abs(attributes.Current(healthId) - 50.0) < 0.0001 &&
		effects.Snapshot().front().stacks == 1,
		"Effect source removal did not remove only that source's stack contribution")) return false;
	if (!effects.Remove(replacement.active, error)) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(std::abs(attributes.Current(healthId) - 20.0) < 0.0001,
		"Stacked Effect cleanup did not restore the aggregated Attribute state")) return false;
	auto budgetDefinition = std::make_shared<Vans::VansEffectDefinition>();
	budgetDefinition->id = Vans::VansMakeStableId<Vans::VansEffectIdTag>("Effect.BudgetProbe");
	budgetDefinition->name = "Effect.BudgetProbe";
	budgetDefinition->durationPolicy = Vans::VansEffectDurationPolicy::Duration;
	budgetDefinition->durationSeconds = 10.0;
	Vans::VansEffectSpec budgetSpec;
	budgetSpec.definition = budgetDefinition;
	budgetSpec.source = 91;
	Vans::VansGameplayEffectService budgetEffects(&attributes, &tags, &cues, 1);
	const Vans::VansEffectApplicationResult budgetFirst = budgetEffects.Apply(budgetSpec);
	const Vans::VansEffectApplicationResult budgetBlocked = budgetEffects.Apply(budgetSpec);
	if (!ExpectGAF(budgetFirst && !budgetBlocked &&
		budgetBlocked.error == Vans::VansActionError::BudgetExceeded,
		"Active Effect budget did not reject a distinct duration Effect")) return false;
	if (!budgetEffects.Remove(budgetFirst.active, error)) return false;
	if (!ExpectGAF(static_cast<bool>(budgetEffects.Apply(budgetSpec)),
		"Active Effect budget capacity was not restored after removal")) return false;

	const auto makeInstantEffect = [](const char* name, Vans::VansEffectModifier modifier)
	{
		auto value = std::make_shared<Vans::VansEffectDefinition>();
		value->id = Vans::VansMakeStableId<Vans::VansEffectIdTag>(name);
		value->name = name;
		value->modifiers.push_back(std::move(modifier));
		return value;
	};
	Vans::VansEffectModifier callerModifier;
	callerModifier.attribute = healthId;
	callerModifier.magnitudeSource = Vans::VansEffectMagnitudeSource::SetByCaller;
	callerModifier.setByCallerField =
		Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Damage");
	callerModifier.coefficient = 2.0;
	Vans::VansEffectSpec callerSpec;
	callerSpec.definition = makeInstantEffect("Effect.SetByCaller", callerModifier);
	callerSpec.source = 101;
	if (!ExpectGAF(!effects.Apply(callerSpec),
		"SetByCaller Effect accepted a missing caller value")) return false;
	callerSpec.setByCaller.emplace(callerModifier.setByCallerField, 3.0);
	if (!ExpectGAF(effects.Apply(callerSpec) &&
		std::abs(attributes.Base(healthId) - 26.0) < 0.0001,
		"SetByCaller Effect did not resolve and scale its caller value")) return false;

	Vans::VansEffectModifier contextModifier;
	contextModifier.attribute = healthId;
	contextModifier.magnitudeSource = Vans::VansEffectMagnitudeSource::ContextPayload;
	contextModifier.contextPayloadPath = "/bonus";
	Vans::VansEffectSpec contextSpec;
	contextSpec.definition = makeInstantEffect("Effect.ContextPayload", contextModifier);
	contextSpec.source = 102;
	contextSpec.context.payload = Vans::VansSerializedValue::Object({
		{ "bonus", Vans::VansSerializedValue::Float(4.0) }
	});
	if (!ExpectGAF(effects.Apply(contextSpec) &&
		std::abs(attributes.Base(healthId) - 30.0) < 0.0001,
		"Context payload Effect did not resolve its numeric JSON pointer")) return false;

	Vans::VansTargetData targetValues;
	targetValues.values.push_back(Vans::VansEntityHandle{ 1, 1 });
	targetValues.values.push_back(Vans::VansTargetLocation{ { 1.0, 2.0, 3.0 } });
	const Vans::VansTargetDataHandle targetHandle = targetData.Store(std::move(targetValues));
	Vans::VansEffectModifier targetModifier;
	targetModifier.attribute = healthId;
	targetModifier.magnitudeSource = Vans::VansEffectMagnitudeSource::TargetData;
	targetModifier.targetDataMetric = Vans::VansEffectTargetDataMetric::Count;
	Vans::VansEffectSpec targetSpec;
	targetSpec.definition = makeInstantEffect("Effect.TargetCount", targetModifier);
	targetSpec.source = 103;
	targetSpec.targetData = targetHandle;
	if (!ExpectGAF(effects.Apply(targetSpec) &&
		std::abs(attributes.Base(healthId) - 32.0) < 0.0001,
		"TargetData Effect did not resolve the requested metric")) return false;

	Vans::VansEffectModifier randomModifier;
	randomModifier.attribute = healthId;
	randomModifier.magnitudeSource = Vans::VansEffectMagnitudeSource::RandomRange;
	randomModifier.randomMinimum = 1.0;
	randomModifier.randomMaximum = 2.0;
	Vans::VansEffectSpec randomSpec;
	randomSpec.definition = makeInstantEffect("Effect.RandomRange", randomModifier);
	randomSpec.source = 104;
	randomSpec.context.randomSeed = 0x12345678ull;
	const double randomStart = attributes.Base(healthId);
	if (!effects.Apply(randomSpec)) return false;
	const double firstRandomDelta = attributes.Base(healthId) - randomStart;
	if (!effects.Apply(randomSpec)) return false;
	const double secondRandomDelta = attributes.Base(healthId) - randomStart - firstRandomDelta;
	if (!ExpectGAF(firstRandomDelta >= 1.0 && firstRandomDelta <= 2.0 &&
		std::abs(firstRandomDelta - secondRandomDelta) < 0.0000001,
		"RandomRange Effect was not deterministic for the same random seed")) return false;

	auto capturedDefinition = std::make_shared<Vans::VansEffectDefinition>();
	capturedDefinition->id = Vans::VansMakeStableId<Vans::VansEffectIdTag>("Effect.SnapshotCapture");
	capturedDefinition->name = "Effect.SnapshotCapture";
	capturedDefinition->durationPolicy = Vans::VansEffectDurationPolicy::Infinite;
	Vans::VansEffectModifier capturedModifier;
	capturedModifier.attribute = healthId;
	capturedModifier.magnitudeSource = Vans::VansEffectMagnitudeSource::CapturedAttribute;
	capturedModifier.capturedAttribute = powerId;
	capturedModifier.capturePolicy = Vans::VansEffectCapturePolicy::Snapshot;
	capturedDefinition->modifiers.push_back(capturedModifier);
	Vans::VansEffectSpec capturedSpec;
	capturedSpec.definition = capturedDefinition;
	capturedSpec.source = 105;
	const double captureBase = attributes.Base(healthId);
	const auto captured = effects.Apply(capturedSpec);
	if (!ExpectGAF(captured && std::abs(attributes.Current(healthId) - captureBase - 4.0) < 0.0001,
		"Snapshot Effect did not freeze the captured Attribute")) return false;
	attributes.SetBase(powerId, 7.0);
	if (!ExpectGAF(std::abs(attributes.Current(healthId) - captureBase - 4.0) < 0.0001,
		"Snapshot Effect changed after its captured Attribute changed")) return false;
	if (!effects.Remove(captured.active, error)) return false;

	auto dynamicDefinition = std::make_shared<Vans::VansEffectDefinition>(*capturedDefinition);
	dynamicDefinition->id = Vans::VansMakeStableId<Vans::VansEffectIdTag>("Effect.DynamicCapture");
	dynamicDefinition->name = "Effect.DynamicCapture";
	dynamicDefinition->modifiers.front().capturePolicy = Vans::VansEffectCapturePolicy::Dynamic;
	Vans::VansEffectSpec dynamicSpec;
	dynamicSpec.definition = dynamicDefinition;
	dynamicSpec.source = 106;
	const auto dynamic = effects.Apply(dynamicSpec);
	if (!ExpectGAF(dynamic && std::abs(attributes.Current(healthId) - captureBase - 7.0) < 0.0001,
		"Dynamic Effect did not use the current captured Attribute")) return false;
	attributes.SetBase(powerId, 9.0);
	effects.Tick(0.01);
	if (!ExpectGAF(std::abs(attributes.Current(healthId) - captureBase - 9.0) < 0.0001,
		"Dynamic Effect did not refresh after its captured Attribute changed")) return false;
	if (!effects.Remove(dynamic.active, error) || !targetData.Release(targetHandle)) return false;

	const auto* audioCapability = Vans::VansFindStandardActionServiceCapability(
		Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Audio"));
	if (!ExpectGAF(audioCapability != nullptr, "Standard Audio Action Service capability is missing"))
		return false;
	auto audio = std::make_shared<Vans::VansFakeActionService>(*audioCapability);
	Vans::VansActionServiceRegistry services;
	if (!services.Register(audio, error) || !services.Seal(error)) return ExpectGAF(false, error.c_str());
	Vans::VansGameplayCueAdapterMapping mapping;
	mapping.serviceName = "Service.Audio";
	mapping.service = Vans::VansMakeStableId<Vans::VansActionServiceIdTag>(mapping.serviceName);
	mapping.commandName = "Audio.Loop";
	mapping.command = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>(mapping.commandName);
	mapping.updateCommandName = "Audio.Update";
	mapping.updateCommand = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>(mapping.updateCommandName);
	mapping.removeCommandName = "Audio.Stop";
	mapping.removeCommand = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>(mapping.removeCommandName);
	mapping.asset = "Audio/ChargeLoop.wav";
	const auto serviceCueId = Vans::VansMakeStableId<Vans::VansCueIdTag>("Cue.Audio.Charge");
	auto serviceCue = std::make_shared<Vans::VansActionServiceGameplayCueAdapter>(
		serviceCueId, "Cue.Audio.Charge", Vans::VansGameplayCueScope::Owner,
		std::vector<Vans::VansGameplayCueAdapterMapping>{ mapping }, &services);
	if (!serviceCue->Validate(error)) return ExpectGAF(false, error.c_str());
	Vans::VansGameplayCueRegistry serviceCueRegistry;
	if (!serviceCueRegistry.Register(serviceCue, error) || !serviceCueRegistry.Seal(error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGameplayCueService serviceCues(&serviceCueRegistry);
	Vans::VansGameplayCueParameters serviceParameters;
	serviceParameters.context.owner = { 8, 1 };
	serviceParameters.context.predictionKey = { 2, 4 };
	serviceParameters.intensity = 0.75;
	const Vans::VansGameplayCueKey serviceKey{ { 2, 4 }, serviceCueId, 1 };
	const auto serviceCueHandle = serviceCues.Add(serviceKey,
		serviceCues.DefaultScope(serviceCueId), serviceParameters, 11, error);
	if (!ExpectGAF(serviceCueHandle && audio->ActiveResourceCount() == 1 &&
		serviceCues.DefaultScope(serviceCueId) == Vans::VansGameplayCueScope::Owner,
		"Service-backed Gameplay Cue did not create its declared resource or scope")) return false;
	serviceParameters.intensity = 0.25;
	if (!serviceCues.Update(serviceCueHandle, serviceParameters, error) ||
		!serviceCues.Remove(serviceCueHandle, error)) return ExpectGAF(false, error.c_str());
	return ExpectGAF(audio->ActiveResourceCount() == 0 && serviceCues.ActiveCount() == 0,
		"Service-backed Gameplay Cue leaked its persistent resource");
}

bool TestGAFTargetingContract()
{
	Vans::VansTargetingHandlerRegistry handlers;
	std::string error;
	auto acquire = std::make_shared<ProbeAcquireTargets>();
	auto limit = std::make_shared<ProbeLimitTargets>();
	if (!handlers.Register(acquire, error) || !handlers.Register(limit, error) || !handlers.Seal(error))
		return ExpectGAF(false, error.c_str());
	Vans::VansTargetingPolicy policy;
	policy.id = Vans::VansMakeStableId<Vans::VansTargetingPolicyIdTag>("Targeting.FirstEntity");
	policy.name = "Targeting.FirstEntity";
	policy.steps.push_back({ Vans::VansTargetingStepKind::Acquire, acquire->TypeId(),
		std::string(acquire->StableName()), Vans::VansSerializedValue::Object({}) });
	policy.steps.push_back({ Vans::VansTargetingStepKind::Limit, limit->TypeId(),
		std::string(limit->StableName()), Vans::VansSerializedValue::Object({}) });
	const auto result = Vans::VansTargetingPipeline::Execute(policy, {}, handlers);
	if (!ExpectGAF(result && result.data.values.size() == 1 && result.trace.size() == 2 &&
		result.trace[0].outputCount == 3 && result.trace[1].outputCount == 1,
		"Targeting pipeline 或 trace 错误")) return false;
	Vans::VansTargetData supplied;
	supplied.values.push_back(Vans::VansEntityHandle{ 44, 1 });
	const auto retained = Vans::VansTargetingPipeline::Execute(
		policy, {}, handlers, std::move(supplied));
	const auto* retainedEntity = retained && !retained.data.values.empty()
		? std::get_if<Vans::VansEntityHandle>(&retained.data.values.front()) : nullptr;
	if (!ExpectGAF(retained && retainedEntity && retainedEntity->index == 44 &&
		retained.trace.front().message == "supplied TargetData retained",
		"Targeting pipeline replaced caller-supplied TargetData")) return false;
	Vans::VansTargetDataStore store;
	const auto handle = store.Store(result.data);
	if (!ExpectGAF(handle && store.Resolve(handle) && store.Resolve(handle)->values.size() == 1,
		"TargetData store 未保存结果")) return false;
	return ExpectGAF(store.Release(handle) && !store.Resolve(handle) && !store.Release(handle),
		"TargetData generation handle 未阻止陈旧访问");
}

bool TestGAFDefinitionAndServiceContract()
{
	Vans::VansActionDefinitionRegistry definitions;
	auto first = std::make_shared<Vans::VansCompiledActionDefinition>();
	first->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test");
	first->name = "Action.Test";
	first->definitionVersion = 1;
	first->schemaVersion = 1;
	first->contentHash = 11;
	first->executor = Vans::VansMakeStableId<Vans::VansActionExecutorIdTag>("Executor.Test");
	std::string error;
	if (!definitions.RegisterRevision(first, error)) return ExpectGAF(false, error.c_str());
	auto second = std::make_shared<Vans::VansCompiledActionDefinition>(*first);
	second->definitionVersion = 2;
	second->contentHash = 22;
	if (!definitions.RegisterRevision(second, error)) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(definitions.ResolveLatest(first->id) == second &&
		definitions.ResolveRevision(first->id, 1) == first && definitions.LatestRevision(first->id) == 2,
		"Action Definition revision pinning 错误")) return false;
	auto invalid = std::make_shared<Vans::VansCompiledActionDefinition>();
	if (!ExpectGAF(!definitions.RegisterRevision(invalid, error),
		"无效 Action Definition 被注册")) return false;

	auto service = std::make_shared<ProbeActionService>();
	Vans::VansActionServiceRegistry services;
	if (!services.Register(service, error) || !services.Seal(error) ||
		!services.ValidateRequired({ service->capability.service }, error)) return false;
	Vans::VansActionCommand command;
	command.service = service->capability.service;
	command.command = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Probe.Run");
	command.stableName = "Probe.Run";
	const auto result = services.Execute(command);
	if (!ExpectGAF(result && result.resource && service->executeCount == 1 &&
		service->lastCommand == "Probe.Run", "Action Service 命令路由错误")) return false;
	if (!ExpectGAF(service->Release(result.resource, error) && service->releaseCount == 1,
		"Action Service 资源释放错误")) return false;

	const auto& capabilities = Vans::VansStandardActionServiceCapabilities();
	std::size_t commandCount = 0;
	for (const auto& capability : capabilities) commandCount += capability.commandSchemas.size();
	if (!ExpectGAF(capabilities.size() == 9 && commandCount == 35,
		"GAF 九类标准 Service 或命令目录不完整")) return false;
	auto fakeServices = Vans::VansCreateFakeStandardActionServices();
	Vans::VansActionServiceRegistry standardRegistry;
	for (const auto& fake : fakeServices)
		if (!standardRegistry.Register(fake, error)) return ExpectGAF(false, error.c_str());
	if (!standardRegistry.Seal(error) ||
		!Vans::VansRunActionServiceConformance(standardRegistry, fakeServices, error))
		return ExpectGAF(false, error.c_str());

	const auto cameraServiceId =
		Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Camera");
	const auto cameraShotId =
		Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Camera.Shot");
	const Vans::VansActionCommandSchema* cameraShot =
		standardRegistry.ResolveCommandSchema(cameraServiceId, cameraShotId);
	if (!ExpectGAF(cameraShot && cameraShot->resourcePolicy ==
		Vans::VansActionCommandResourcePolicy::Create,
		"GAF 标准 Service 命令 Schema 无法解析")) return false;
	Vans::VansActionCommand invalidCommand;
	invalidCommand.service = cameraServiceId;
	invalidCommand.command = cameraShotId;
	invalidCommand.stableName = "Camera.Shot";
	invalidCommand.payload = Vans::VansSerializedValue::Object({});
	if (!ExpectGAF(standardRegistry.Execute(invalidCommand).error ==
		Vans::VansActionError::DefinitionInvalid,
		"GAF Service 未拒绝缺少必填字段的命令")) return false;
	invalidCommand.payload = Vans::VansBuildActionCommandSamplePayload(*cameraShot);
	Vans::SetSerializedObjectField(invalidCommand.payload, "typo",
		Vans::VansSerializedValue::Bool(true));
	if (!ExpectGAF(standardRegistry.Execute(invalidCommand).error ==
		Vans::VansActionError::DefinitionInvalid,
		"GAF Service 未拒绝未知负载字段")) return false;
	const auto combatServiceId =
		Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Combat");
	const auto resolveHitId =
		Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Combat.ResolveHit");
	const Vans::VansActionCommandSchema* resolveHit =
		standardRegistry.ResolveCommandSchema(combatServiceId, resolveHitId);
	if (!ExpectGAF(resolveHit != nullptr, "GAF Combat Service 命令 Schema 无法解析")) return false;
	invalidCommand = {};
	invalidCommand.service = combatServiceId;
	invalidCommand.command = resolveHitId;
	invalidCommand.stableName = "Combat.ResolveHit";
	invalidCommand.predicted = true;
	invalidCommand.payload = Vans::VansBuildActionCommandSamplePayload(*resolveHit);
	return ExpectGAF(standardRegistry.Execute(invalidCommand).error ==
		Vans::VansActionError::AuthorityDenied,
		"GAF Service 未拒绝 AuthorityOnly 命令的预测执行");
}

bool TestGAFResourceLedgerAndTaskContract()
{
	Vans::VansActionResourceLedger ledger;
	std::vector<int> order;
	std::string error;
	Vans::VansActionResourceEntry first;
	first.type = "Probe";
	first.debugName = "first";
	first.prediction = Vans::VansActionPredictionResourcePolicy::UndoRedo;
	first.release = [&] { order.push_back(10); return true; };
	first.undo = [&] { order.push_back(11); return true; };
	first.redo = [&] { order.push_back(12); return true; };
	const auto firstHandle = ledger.Register(std::move(first), error);
	Vans::VansActionResourceEntry second;
	second.type = "Probe";
	second.debugName = "second";
	second.dependsOn = firstHandle;
	second.prediction = Vans::VansActionPredictionResourcePolicy::UndoRedo;
	second.release = [&] { order.push_back(20); return true; };
	second.undo = [&] { order.push_back(21); return true; };
	second.redo = [&] { order.push_back(22); return true; };
	const auto secondHandle = ledger.Register(std::move(second), error);
	if (!firstHandle || !secondHandle) return ExpectGAF(false, error.c_str());
	std::vector<std::string> errors;
	if (!ledger.RollbackPredicted(errors) || !ledger.ReplayPredicted(errors) ||
		!ledger.ReleaseAll(errors)) return false;
	const std::vector<int> expected{ 21, 11, 12, 22, 20, 10 };
	if (!ExpectGAF(order == expected && ledger.ActiveCount() == 0 && ledger.IsReleased(),
		"ResourceLedger Undo/Redo/Release 顺序错误")) return false;
	if (!ExpectGAF(!ledger.Register({}, error), "已释放 ResourceLedger 仍接受资源")) return false;

	double balance = 100.0;
	Vans::VansActionCommitTransaction transaction;
	Vans::VansActionCommitStep cost;
	cost.name = "cost";
	cost.preflight = [&](std::string&) { return balance >= 30.0; };
	cost.apply = [&](std::string&) { balance -= 30.0; return true; };
	cost.compensate = [&](std::string&) { balance += 30.0; return true; };
	if (!transaction.AddStep(std::move(cost), error)) return false;
	Vans::VansActionCommitStep failure;
	failure.name = "forced failure";
	failure.preflight = [](std::string&) { return true; };
	failure.apply = [](std::string& message) { message = "requested"; return false; };
	failure.compensate = [](std::string&) { return true; };
	if (!transaction.AddStep(std::move(failure), error)) return false;
	if (!ExpectGAF(!transaction.Commit(error) && balance == 100.0 &&
		!transaction.CompensationFailed(), "CommitTransaction 未原子补偿")) return false;

	Vans::VansActionTaskSet tasks;
	int cancelCount = 0;
	int terminalCount = 0;
	Vans::VansActionTaskDesc task;
	task.type = Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Task.Timer");
	task.debugName = "timeout probe";
	task.timeoutSeconds = 0.5;
	task.cancel = [&] { ++cancelCount; };
	task.terminal = [&](Vans::VansActionTaskState state)
	{
		if (state == Vans::VansActionTaskState::TimedOut) ++terminalCount;
	};
	const auto taskHandle = tasks.Create(std::move(task), error);
	tasks.Tick(0.5);
	if (!ExpectGAF(taskHandle && tasks.ActiveCount() == 0 && cancelCount == 1 && terminalCount == 1,
		"Action Task timeout 未单次终结")) return false;
	if (!ExpectGAF(!tasks.Complete(taskHandle, error), "终结后的 Action Task 仍可完成")) return false;
	Vans::VansActionTaskSet budgetTasks(1);
	Vans::VansActionTaskDesc budgetTask;
	budgetTask.type = Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Task.BudgetProbe");
	budgetTask.debugName = "budget probe";
	const Vans::VansActionTaskHandle budgetFirst = budgetTasks.Create(budgetTask, error);
	error.clear();
	const Vans::VansActionTaskHandle budgetBlocked = budgetTasks.Create(budgetTask, error);
	if (!ExpectGAF(budgetFirst && !budgetBlocked && error == "Action Task budget exceeded",
		"Action Task budget did not reject excess tasks with a stable diagnostic")) return false;
	if (!budgetTasks.Complete(budgetFirst, error)) return false;
	return ExpectGAF(static_cast<bool>(budgetTasks.Create(std::move(budgetTask), error)),
		"Action Task budget capacity was not restored after completion");
}

bool TestGAFExecutionGraphContract()
{
	Vans::VansActionGraphNodeRegistry handlers;
	auto immediate = std::make_shared<ProbeGraphImmediateNode>();
	auto wait = std::make_shared<ProbeGraphWaitNode>();
	std::string error;
	if (!handlers.Register(immediate, error) || !handlers.Register(wait, error) || !handlers.Seal(error))
		return ExpectGAF(false, error.c_str());
	auto graph = std::make_shared<Vans::VansCompiledActionGraph>();
	graph->name = "Graph.Probe";
	graph->version = 1;
	graph->contentHash = 123;
	graph->entryNode = 0;
	graph->nodes.push_back({ "node-a", immediate->TypeId(),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}), true });
	graph->nodes.push_back({ "node-b", wait->TypeId(),
		Vans::VansActionGraphNodeKind::Latent, Vans::VansSerializedValue::Object({}), true });
	graph->edges.push_back({ 0, "Success", 1, 0 });
	Vans::VansActionGraphRuntime runtime;
	const auto diagnostics = runtime.Initialize(graph, &handlers);
	for (const auto& diagnostic : diagnostics)
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	Vans::VansActionExecutionContext context;
	const auto started = runtime.Start(context);
	if (!ExpectGAF(started.status == Vans::VansActionExecutorStatus::Waiting && runtime.IsRunning() &&
		runtime.NodeState(0) == Vans::VansActionGraphNodeStatus::Succeeded &&
		runtime.NodeState(1) == Vans::VansActionGraphNodeStatus::Waiting,
		"ExecutionGraph 未在 latent node 等待")) return false;
	const auto completed = runtime.Tick(context);
	if (!ExpectGAF(completed.status == Vans::VansActionExecutorStatus::Succeeded && !runtime.IsRunning() &&
		runtime.NodeState(1) == Vans::VansActionGraphNodeStatus::Succeeded,
		"ExecutionGraph 未从 latent node 完成")) return false;
	Vans::VansActionGraphRuntime budgetRuntime;
	for (const auto& diagnostic : budgetRuntime.Initialize(graph, &handlers, 1))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	const Vans::VansActionExecutorResult budgetResult = budgetRuntime.Start(context);
	if (!ExpectGAF(budgetResult.status == Vans::VansActionExecutorStatus::Failed &&
		budgetResult.error == Vans::VansActionError::BudgetExceeded && !budgetRuntime.IsRunning(),
		"ExecutionGraph transition budget did not terminate runaway same-tick work")) return false;

	Vans::VansActionGraphNodeRegistry builtIns;
	if (!Vans::VansRegisterBuiltInActionGraphNodes(builtIns, error) || !builtIns.Seal(error))
		return ExpectGAF(false, error.c_str());
	const std::vector<std::string> builtInNames{
		"Action.Graph.Sequence", "Action.Graph.Parallel", "Action.Graph.Race",
		"Action.Graph.Branch", "Action.Graph.Switch", "Action.Graph.Loop",
		"Action.Graph.Repeat", "Action.Graph.Channel", "Action.Graph.Gate",
		"Action.Graph.Wait", "Action.Graph.Timeout", "Action.Graph.Command",
		"Action.Graph.Complete", "Action.Graph.Fail", "Action.Graph.SubAction",
		"Action.Graph.Transition", "Action.Graph.Try", "Action.Graph.Compensate"
	};
	for (const std::string& name : builtInNames)
	{
		const auto handler = builtIns.Resolve(
			Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>(name));
		if (!ExpectGAF(handler && handler->StableName() == name,
			"Built-in Action Graph node registry is incomplete")) return false;
	}

	const auto builtInType = [](const char* name)
	{
		return Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>(name);
	};
	auto repeatGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	repeatGraph->name = "Graph.Repeat";
	repeatGraph->version = 1;
	repeatGraph->contentHash = 201;
	repeatGraph->entryNode = 0;
	repeatGraph->nodes.push_back({ "repeat", builtInType("Action.Graph.Repeat"),
		Vans::VansActionGraphNodeKind::Flow,
		Vans::VansSerializedValue::Object({ { "count", Vans::VansSerializedValue::Int(2) } }), true });
	repeatGraph->nodes.push_back({ "body", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}), true });
	repeatGraph->nodes.push_back({ "end", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}), true });
	repeatGraph->edges.push_back({ 0, "Body", 1, 0 });
	repeatGraph->edges.push_back({ 1, "Success", 0, 0 });
	repeatGraph->edges.push_back({ 0, "Success", 2, 1 });
	Vans::VansActionGraphRuntime repeatRuntime;
	for (const auto& diagnostic : repeatRuntime.Initialize(repeatGraph, &builtIns, 32))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	const Vans::VansActionExecutorResult repeated = repeatRuntime.Start(context);
	if (!ExpectGAF(repeated.status == Vans::VansActionExecutorStatus::Succeeded &&
		repeatRuntime.NodeState(2) == Vans::VansActionGraphNodeStatus::Succeeded,
		"Repeat node did not preserve its iteration state across graph re-entry")) return false;

	auto parallelGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	parallelGraph->name = "Graph.Parallel";
	parallelGraph->version = 1;
	parallelGraph->contentHash = 202;
	parallelGraph->entryNode = 0;
	parallelGraph->nodes.push_back({ "parallel", builtInType("Action.Graph.Parallel"),
		Vans::VansActionGraphNodeKind::Flow,
		Vans::VansSerializedValue::Object({ { "branches", Vans::VansSerializedValue::Int(2) } }), true });
	parallelGraph->nodes.push_back({ "left", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}), true });
	parallelGraph->nodes.push_back({ "right", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}), true });
	parallelGraph->nodes.push_back({ "joined", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}), true });
	parallelGraph->edges.push_back({ 0, "Branch", 1, 0 });
	parallelGraph->edges.push_back({ 0, "Branch", 2, 1 });
	parallelGraph->edges.push_back({ 1, "Success", 0, 0 });
	parallelGraph->edges.push_back({ 2, "Success", 0, 0 });
	parallelGraph->edges.push_back({ 0, "Success", 3, 0 });
	Vans::VansActionGraphRuntime parallelRuntime;
	for (const auto& diagnostic : parallelRuntime.Initialize(parallelGraph, &builtIns, 32))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	const Vans::VansActionExecutorResult parallel = parallelRuntime.Start(context);
	if (!ExpectGAF(parallel.status == Vans::VansActionExecutorStatus::Succeeded &&
		parallelRuntime.NodeState(1) == Vans::VansActionGraphNodeStatus::Succeeded &&
		parallelRuntime.NodeState(2) == Vans::VansActionGraphNodeStatus::Succeeded &&
		parallelRuntime.NodeState(3) == Vans::VansActionGraphNodeStatus::Succeeded,
		"Parallel node did not wait for every configured branch")) return false;

	auto raceGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	raceGraph->name = "Graph.Race";
	raceGraph->version = 1;
	raceGraph->contentHash = 203;
	raceGraph->entryNode = 0;
	raceGraph->nodes.push_back({ "race", builtInType("Action.Graph.Race"),
		Vans::VansActionGraphNodeKind::Flow,
		Vans::VansSerializedValue::Object({ { "cancelNodes", Vans::VansSerializedValue::Array({
			Vans::VansSerializedValue::String("wait") }) } }), true });
	raceGraph->nodes.push_back({ "wait", builtInType("Action.Graph.Wait"),
		Vans::VansActionGraphNodeKind::Latent,
		Vans::VansSerializedValue::Object({ { "seconds", Vans::VansSerializedValue::Float(10.0) } }), true });
	raceGraph->nodes.push_back({ "winner", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}), true });
	raceGraph->nodes.push_back({ "end", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}), true });
	raceGraph->edges.push_back({ 0, "Branch", 1, 0 });
	raceGraph->edges.push_back({ 0, "Branch", 2, 1 });
	raceGraph->edges.push_back({ 1, "Success", 0, 0 });
	raceGraph->edges.push_back({ 2, "Success", 0, 0 });
	raceGraph->edges.push_back({ 0, "Success", 3, 0 });
	Vans::VansActionGraphRuntime raceRuntime;
	for (const auto& diagnostic : raceRuntime.Initialize(raceGraph, &builtIns, 32))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	const Vans::VansActionExecutorResult raced = raceRuntime.Start(context);
	if (!ExpectGAF(raced.status == Vans::VansActionExecutorStatus::Succeeded &&
		raceRuntime.NodeState(1) == Vans::VansActionGraphNodeStatus::Cancelled &&
		raceRuntime.NodeState(3) == Vans::VansActionGraphNodeStatus::Succeeded,
		"Race node did not cancel configured losing latent branches")) return false;

	auto loopGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	loopGraph->name = "Graph.LoopBudget";
	loopGraph->version = 1;
	loopGraph->contentHash = 204;
	loopGraph->entryNode = 0;
	loopGraph->nodes.push_back({ "loop", builtInType("Action.Graph.Loop"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({
			{ "condition", Vans::VansSerializedValue::Bool(true) },
			{ "maximumIterations", Vans::VansSerializedValue::Int(2) } }), true });
	loopGraph->nodes.push_back({ "body", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}), true });
	loopGraph->edges.push_back({ 0, "Body", 1, 0 });
	loopGraph->edges.push_back({ 1, "Success", 0, 0 });
	Vans::VansActionGraphRuntime loopRuntime;
	for (const auto& diagnostic : loopRuntime.Initialize(loopGraph, &builtIns, 32))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	const Vans::VansActionExecutorResult looped = loopRuntime.Start(context);
	if (!ExpectGAF(looped.status == Vans::VansActionExecutorStatus::Failed &&
		looped.error == Vans::VansActionError::BudgetExceeded,
		"Loop node did not preserve its specific bounded-loop failure")) return false;

	auto timeoutGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	timeoutGraph->name = "Graph.Timeout";
	timeoutGraph->version = 1;
	timeoutGraph->contentHash = 205;
	timeoutGraph->entryNode = 0;
	timeoutGraph->nodes.push_back({ "timeout", builtInType("Action.Graph.Timeout"),
		Vans::VansActionGraphNodeKind::Latent, Vans::VansSerializedValue::Object({
			{ "seconds", Vans::VansSerializedValue::Float(0.1) },
			{ "fail", Vans::VansSerializedValue::Bool(true) } }), true });
	Vans::VansActionGraphRuntime timeoutRuntime;
	for (const auto& diagnostic : timeoutRuntime.Initialize(timeoutGraph, &builtIns, 32))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	context.deltaSeconds = 0.0;
	if (timeoutRuntime.Start(context).status != Vans::VansActionExecutorStatus::Waiting) return false;
	context.deltaSeconds = 0.1;
	const Vans::VansActionExecutorResult timedOut = timeoutRuntime.Tick(context);
	if (!ExpectGAF(timedOut.status == Vans::VansActionExecutorStatus::Failed &&
		timedOut.error == Vans::VansActionError::TimedOut,
		"Timeout node did not expose a stable timeout error")) return false;

	Vans::VansActionResourceLedger compensationLedger;
	int compensationCount = 0;
	Vans::VansActionResourceEntry compensatable;
	compensatable.type = "GraphProbe";
	compensatable.debugName = "compensatable";
	compensatable.prediction = Vans::VansActionPredictionResourcePolicy::UndoOnly;
	compensatable.release = [] { return true; };
	compensatable.undo = [&] { ++compensationCount; return true; };
	if (!compensationLedger.Register(std::move(compensatable), error)) return false;
	auto compensateGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	compensateGraph->name = "Graph.Compensate";
	compensateGraph->version = 1;
	compensateGraph->contentHash = 206;
	compensateGraph->entryNode = 0;
	compensateGraph->nodes.push_back({ "compensate", builtInType("Action.Graph.Compensate"),
		Vans::VansActionGraphNodeKind::Transaction, Vans::VansSerializedValue::Object({}), true });
	Vans::VansActionGraphRuntime compensateRuntime;
	for (const auto& diagnostic : compensateRuntime.Initialize(compensateGraph, &builtIns, 32))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	context.resources = &compensationLedger;
	const Vans::VansActionExecutorResult compensated = compensateRuntime.Start(context);
	if (!ExpectGAF(compensated.status == Vans::VansActionExecutorStatus::Succeeded &&
		compensationCount == 1,
		"Compensate node did not roll back predicted Action resources")) return false;

	if (!ExpectGAF(Vans::VansCameraActionGraphNodeDescriptors().size() == 8,
		"Camera Action Graph descriptor catalog is incomplete")) return false;
	Vans::VansActionGraphNodeRegistry cameraHandlers;
	if (!Vans::VansRegisterBuiltInActionGraphNodes(cameraHandlers, error) ||
		!Vans::VansRegisterCameraActionGraphNodes(cameraHandlers, error) ||
		!cameraHandlers.Seal(error)) return ExpectGAF(false, error.c_str());
	Vans::VansGameplayAssetLibrary emptyCameraAssets;
	Vans::VansCameraRuntime cameraRuntime;
	Vans::VansCameraViewSnapshot baseCamera;
	if (!cameraRuntime.SetBaseView(Vans::VansCameraRuntime::MainView(), baseCamera, error))
		return ExpectGAF(false, error.c_str());
	auto cameraService = Vans::VansCameraActionService::Create(
		cameraRuntime, emptyCameraAssets, error);
	Vans::VansActionServiceRegistry cameraServices;
	if (!cameraService || !cameraServices.Register(cameraService, error) ||
		!cameraServices.Seal(error)) return ExpectGAF(false, error.c_str());
	const Vans::VansActionFieldId lockVariable =
		Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("camera.lock");
	const Vans::VansActionFieldId eventVariable =
		Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("camera.event");
	Vans::VansActionVariableStore cameraVariables;
	if (!cameraVariables.Initialize({
		{ lockVariable, "camera.lock", Vans::VansSerializedValue::Object({}) },
		{ eventVariable, "camera.event", Vans::VansSerializedValue::Object({}) }
	}, error)) return ExpectGAF(false, error.c_str());
	Vans::VansActionContext actionContext;
	Vans::VansActionResourceLedger cameraResources;
	Vans::VansActionExecutionContext cameraContext;
	cameraContext.context = &actionContext;
	cameraContext.variables = &cameraVariables;
	cameraContext.resources = &cameraResources;
	cameraContext.services = &cameraServices;
	const auto cameraType = [](const char* name)
	{
		return Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>(name);
	};
	const auto position = [](double x, double y, double z)
	{
		return Vans::VansSerializedValue::Object({
			{ "x", Vans::VansSerializedValue::Float(x) },
			{ "y", Vans::VansSerializedValue::Float(y) },
			{ "z", Vans::VansSerializedValue::Float(z) }
		});
	};
	auto lockGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	lockGraph->name = "Graph.CameraLock";
	lockGraph->contentHash = 301;
	lockGraph->entryNode = 0;
	lockGraph->nodes.push_back({ "lock", cameraType("Camera.StartLockOn"),
		Vans::VansActionGraphNodeKind::Command, Vans::VansSerializedValue::Object({
			{ "target", position(1.0, 0.0, 0.0) },
			{ "resultVariable", Vans::VansSerializedValue::String("camera.lock") }
		}), true });
	lockGraph->nodes.push_back({ "update", cameraType("Camera.UpdateLockOn"),
		Vans::VansActionGraphNodeKind::Command, Vans::VansSerializedValue::Object({
			{ "resourceVariable", Vans::VansSerializedValue::String("camera.lock") },
			{ "target", position(0.0, 0.0, 1.0) }
		}), true });
	lockGraph->edges.push_back({ 0, "Success", 1, 0 });
	Vans::VansActionGraphRuntime lockRuntime;
	for (const auto& diagnostic : lockRuntime.Initialize(lockGraph, &cameraHandlers, 16))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error)
			return ExpectGAF(false, diagnostic.message.c_str());
	const Vans::VansActionExecutorResult locked = lockRuntime.Start(cameraContext);
	if (!ExpectGAF(locked.status == Vans::VansActionExecutorStatus::Succeeded &&
		cameraResources.ActiveCount() == 1 && cameraRuntime.ContributionCount() == 1 &&
		std::abs(cameraRuntime.ResolveView(Vans::VansCameraRuntime::MainView())
			.snapshot.pose.rotationDegrees.y - 90.0f) < 0.001f,
		"Camera Graph did not create and update a tracked LockOn contribution")) return false;

	auto releaseGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	releaseGraph->name = "Graph.CameraRelease";
	releaseGraph->contentHash = 302;
	releaseGraph->entryNode = 0;
	releaseGraph->nodes.push_back({ "release", cameraType("Camera.Release"),
		Vans::VansActionGraphNodeKind::Command, Vans::VansSerializedValue::Object({
			{ "resourceVariable", Vans::VansSerializedValue::String("camera.lock") }
		}), true });
	Vans::VansActionGraphRuntime releaseRuntime;
	for (const auto& diagnostic : releaseRuntime.Initialize(releaseGraph, &cameraHandlers, 8))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error)
			return ExpectGAF(false, diagnostic.message.c_str());
	if (!ExpectGAF(releaseRuntime.Start(cameraContext).status ==
			Vans::VansActionExecutorStatus::Succeeded &&
		cameraResources.ActiveCount() == 0 && cameraRuntime.ContributionCount() == 0,
		"Camera Graph explicit Release did not reconcile the Action resource ledger")) return false;

	auto eventGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	eventGraph->name = "Graph.CameraEvent";
	eventGraph->contentHash = 303;
	eventGraph->entryNode = 0;
	eventGraph->nodes.push_back({ "wait-event", cameraType("Camera.WaitEvent"),
		Vans::VansActionGraphNodeKind::Latent, Vans::VansSerializedValue::Object({
			{ "event", Vans::VansSerializedValue::String("Camera.BlendComplete") },
			{ "resultVariable", Vans::VansSerializedValue::String("camera.event") }
		}), true });
	Vans::VansActionGraphRuntime eventRuntime;
	for (const auto& diagnostic : eventRuntime.Initialize(eventGraph, &cameraHandlers, 8))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error)
			return ExpectGAF(false, diagnostic.message.c_str());
	if (eventRuntime.Start(cameraContext).status != Vans::VansActionExecutorStatus::Waiting)
		return ExpectGAF(false, "Camera WaitEvent node did not enter the waiting state");
	std::vector<Vans::VansActionEvent> cameraEvents{
		{ Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Camera.BlendComplete"),
			"Camera.BlendComplete", {}, {}, Vans::VansSerializedValue::Object({
				{ "view", Vans::VansSerializedValue::String("Main") }
			}) }
	};
	cameraContext.events = &cameraEvents;
	const Vans::VansActionExecutorResult eventCompleted = eventRuntime.Tick(cameraContext);
	return ExpectGAF(eventCompleted.status == Vans::VansActionExecutorStatus::Succeeded &&
		cameraVariables.Get(eventVariable) != nullptr,
		"Camera WaitEvent node did not resume and store its event payload");
}

bool TestGAFActionHostLifecycleContract()
{
	class ProbeExternalCostProvider final : public Vans::IVansActionExternalCostProvider
	{
	public:
		bool CanCommit(const Vans::VansActionExternalCostRequest& request,
			std::string& message) const override
		{
			if (request.kind != Vans::VansActionCostKind::Inventory ||
				request.resource != "Inventory.Test.Ammo" || request.amount <= 0.0)
			{
				message = "unsupported external cost";
				return false;
			}
			if (balance + 1e-9 < request.amount)
			{
				message = "insufficient inventory";
				return false;
			}
			return true;
		}
		Vans::VansGenerationHandle Commit(const Vans::VansActionExternalCostRequest& request,
			std::string& message) override
		{
			if (!CanCommit(request, message)) return {};
			balance -= request.amount;
			return receipts.Emplace(request.amount);
		}
		bool Settle(Vans::VansGenerationHandle receipt, bool refund,
			std::string& message) override
		{
			const double* amount = receipts.Resolve(receipt);
			if (!amount) { message = "stale external cost receipt"; return false; }
			if (refund) balance += *amount;
			return receipts.Release(receipt);
		}

		double balance = 5.0;
		Vans::VansGenerationPool<double> receipts;
	};
	ProbeExternalCostProvider externalCosts;
	std::string error;
	Vans::VansGameplayTagDictionary tags;
	if (!tags.Register("Action", {}, false, {}, error) ||
		!tags.Register("Action.Running", {}, false, {}, error) ||
		!tags.Register("Cooldown", {}, false, {}, error) ||
		!tags.Register("Cooldown.Test", {}, false, {}, error) ||
		!tags.Register("Cooldown.Shared", {}, false, {}, error) || !tags.Seal(error)) return false;
	Vans::VansAttributeRegistry attributes;
	Vans::VansAttributeDefinition energy;
	energy.name = "Character.Energy";
	energy.defaultValue = 100.0;
	energy.minimum = 0.0;
	energy.hasMinimum = true;
	if (!attributes.Register(energy, error) || !attributes.Seal(error)) return false;
	const auto energyId = attributes.Definitions().front().id;
	Vans::VansActionScheduler scheduler;
	Vans::VansActionServiceRegistry services;
	if (!services.Register(std::make_shared<Vans::VansActionRoutingService>(scheduler), error) ||
		!services.Seal(error)) return false;
	Vans::VansActionExecutorRegistry executors;
	auto executorState = std::make_shared<ProbeExecutorState>();
	const auto executorId = Vans::VansMakeStableId<Vans::VansActionExecutorIdTag>("Executor.ProbeRunning");
	const auto failExecutorId = Vans::VansMakeStableId<Vans::VansActionExecutorIdTag>("Executor.ProbeFail");
	if (!executors.Register(executorId, "Executor.ProbeRunning",
		[executorState](const Vans::VansCompiledActionDefinition&)
		{ return std::make_unique<ProbeRunningExecutor>(executorState); }, error) ||
		!executors.Register(failExecutorId, "Executor.ProbeFail",
			[](const Vans::VansCompiledActionDefinition&)
			{ return std::make_unique<ProbeFailExecutor>(); }, error) ||
		!executors.Seal(error)) return false;
	Vans::VansActionDefinitionRegistry definitions;
	auto action = std::make_shared<Vans::VansCompiledActionDefinition>();
	action->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.Host");
	action->name = "Action.Test.Host";
	action->definitionVersion = 1;
	action->schemaVersion = 1;
	action->contentHash = 901;
	action->executor = executorId;
	action->concurrencyGroup = Vans::VansMakeStableId<Vans::VansActionConcurrencyGroupIdTag>("Group.Test");
	action->concurrencyPolicy = Vans::VansActionConcurrencyPolicy::RejectNew;
	action->costs.push_back({ energyId, 30.0, Vans::VansActionCostRefundPolicy::OnCancel });
	Vans::VansActionCostDefinition inventoryCost;
	inventoryCost.kind = Vans::VansActionCostKind::Inventory;
	inventoryCost.resource = "Inventory.Test.Ammo";
	inventoryCost.amount = 2.0;
	inventoryCost.refundPolicy = Vans::VansActionCostRefundPolicy::OnCancel;
	action->costs.push_back(std::move(inventoryCost));
	action->cooldowns.push_back({ 0.5, tags.Find("Cooldown.Test")->id });
	action->cooldowns.push_back({ 1.0, tags.Find("Cooldown.Shared")->id });
	action->grantedWhileRunning.push_back(tags.Find("Action.Running")->id);
	auto queuedAction = std::make_shared<Vans::VansCompiledActionDefinition>(*action);
	queuedAction->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.Queued");
	queuedAction->name = "Action.Test.Queued";
	queuedAction->contentHash = 902;
	queuedAction->concurrencyGroup =
		Vans::VansMakeStableId<Vans::VansActionConcurrencyGroupIdTag>("Group.Queue");
	queuedAction->concurrencyPolicy = Vans::VansActionConcurrencyPolicy::QueueNew;
	queuedAction->concurrencyLimit = 1;
	queuedAction->concurrencyQueueTimeoutSeconds = 1.0;
	queuedAction->costs.clear();
	queuedAction->cooldowns.clear();
	queuedAction->grantedWhileRunning.clear();
	auto timeoutAction = std::make_shared<Vans::VansCompiledActionDefinition>(*queuedAction);
	timeoutAction->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.QueueTimeout");
	timeoutAction->name = "Action.Test.QueueTimeout";
	timeoutAction->contentHash = 903;
	timeoutAction->concurrencyQueueTimeoutSeconds = 0.05;
	auto transitionTarget = std::make_shared<Vans::VansCompiledActionDefinition>(*queuedAction);
	transitionTarget->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.TransitionTarget");
	transitionTarget->name = "Action.Test.TransitionTarget";
	transitionTarget->contentHash = 904;
	transitionTarget->concurrencyGroup = {};
	transitionTarget->concurrencyPolicy = Vans::VansActionConcurrencyPolicy::Allow;
	auto transitionSource = std::make_shared<Vans::VansCompiledActionDefinition>(*transitionTarget);
	transitionSource->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.TransitionSource");
	transitionSource->name = "Action.Test.TransitionSource";
	transitionSource->contentHash = 905;
	Vans::VansActionTransitionRule comboRule;
	comboRule.name = "BufferedCombo";
	comboRule.trigger = Vans::VansActionTransitionTrigger::Input;
	comboRule.inputBinding = "Combo";
	comboRule.targetAction = transitionTarget->id;
	comboRule.minimumTimeSeconds = 0.3;
	comboRule.maximumTimeSeconds = 1.0;
	comboRule.priority = 10;
	comboRule.cancelSource = true;
	comboRule.contextPatch = Vans::VansSerializedValue::Object({
		{ "comboStage", Vans::VansSerializedValue::Int(2) }
	});
	transitionSource->transitionRules.push_back(std::move(comboRule));
	transitionSource->inputBuffer.enabled = true;
	transitionSource->inputBuffer.durationSeconds = 0.5;
	transitionSource->inputBuffer.maximumEntries = 1;
	auto failureSource = std::make_shared<Vans::VansCompiledActionDefinition>(*transitionTarget);
	failureSource->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.FailureSource");
	failureSource->name = "Action.Test.FailureSource";
	failureSource->contentHash = 906;
	failureSource->executor = failExecutorId;
	failureSource->failureFallback.action = transitionTarget->id;
	failureSource->failureFallback.errors.push_back(Vans::VansActionError::ExecutionFailed);
	auto targetingAction = std::make_shared<Vans::VansCompiledActionDefinition>(*transitionTarget);
	targetingAction->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.Targeting");
	targetingAction->name = "Action.Test.Targeting";
	targetingAction->contentHash = 907;
	targetingAction->targetingPolicy =
		Vans::VansMakeStableId<Vans::VansTargetingPolicyIdTag>("Targeting.Test.Primary");
	if (!definitions.RegisterRevision(action, error) ||
		!definitions.RegisterRevision(queuedAction, error) ||
		!definitions.RegisterRevision(timeoutAction, error) ||
		!definitions.RegisterRevision(transitionTarget, error) ||
		!definitions.RegisterRevision(transitionSource, error) ||
		!definitions.RegisterRevision(failureSource, error) ||
		!definitions.RegisterRevision(targetingAction, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansTargetingPolicyRegistry targetingPolicies;
	Vans::VansTargetingPolicy targetingPolicy;
	targetingPolicy.id = targetingAction->targetingPolicy;
	targetingPolicy.name = "Targeting.Test.Primary";
	targetingPolicy.steps.push_back({ Vans::VansTargetingStepKind::Acquire,
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Targeting.Acquire.PrimaryTarget"),
		"Targeting.Acquire.PrimaryTarget", Vans::VansSerializedValue::Object({}) });
	targetingPolicy.steps.push_back({ Vans::VansTargetingStepKind::Lock,
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Targeting.Lock.Entity"),
		"Targeting.Lock.Entity", Vans::VansSerializedValue::Object({}) });
	if (!targetingPolicies.Register(std::move(targetingPolicy), error) ||
		!targetingPolicies.Seal(error)) return ExpectGAF(false, error.c_str());
	Vans::VansTargetingHandlerRegistry targetingHandlers;
	if (!Vans::VansRegisterBuiltInTargetingHandlers(targetingHandlers, error) ||
		!targetingHandlers.Seal(error)) return ExpectGAF(false, error.c_str());
	Vans::VansActionHostDependencies dependencies;
	dependencies.definitions = &definitions;
	dependencies.executors = &executors;
	dependencies.tagDictionary = &tags;
	dependencies.attributeRegistry = &attributes;
	dependencies.targetingPolicies = &targetingPolicies;
	dependencies.targetingHandlers = &targetingHandlers;
	dependencies.services = &services;
	dependencies.externalCosts = &externalCosts;
	Vans::VansActionHost host({ 7, 1 }, dependencies);
	if (!host.Initialize(error)) return ExpectGAF(false, error.c_str());
	Vans::VansActionGrantDesc targetingGrant;
	targetingGrant.action = targetingAction->id;
	targetingGrant.source = 43;
	const Vans::VansActionSpecHandle targetingSpec = host.Grant(targetingGrant, error);
	Vans::VansActionActivationRequest targetingRequest;
	targetingRequest.spec = targetingSpec;
	targetingRequest.context.primaryTarget = { 71, 1 };
	const Vans::VansActionResult targetingDryRun = host.CanActivate(
		targetingRequest.spec, targetingRequest.context,
		targetingRequest.hasAuthority, targetingRequest.predicted);
	const Vans::VansActionResult targeted = host.Activate(targetingRequest);
	const auto targetedSnapshot = targeted ? host.Query(targeted.action) : std::nullopt;
	const Vans::VansTargetDataHandle activeTargetData = targetedSnapshot
		? targetedSnapshot->context.targetData : Vans::VansTargetDataHandle{};
	const Vans::VansTargetData* activeTargets = host.ResolveTargetData(activeTargetData);
	const auto* activeTarget = activeTargets && !activeTargets->values.empty()
		? std::get_if<Vans::VansEntityHandle>(&activeTargets->values.front()) : nullptr;
	if (!ExpectGAF(targetingSpec && targetingDryRun && targeted && activeTarget &&
		activeTarget->index == 71,
		"Action Host did not execute TargetingPolicy or retain TargetData")) return false;
	if (!host.Cancel(targeted.action, Vans::VansActionCancelReason::System, error) ||
		!ExpectGAF(host.ResolveTargetData(activeTargetData) == nullptr,
			"Action Host leaked TargetData after the Action ended") ||
		!host.Revoke(targetingSpec, Vans::VansActionRevokePolicy::CancelRunning, error)) return false;
	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::GameLogic);
	executorState->tickCount = 0;
	executorState->eventCount = 0;
	executorState->finishCount = 0;
	Vans::VansActionGrantDesc grant;
	grant.action = action->id;
	grant.source = 44;
	grant.charges = 2;
	const auto spec = host.Grant(grant, error);
	if (!spec) return ExpectGAF(false, error.c_str());
	int startedEvents = 0;
	int endedEvents = 0;
	int queuedEvents = 0;
	auto startedConnection = Vans::VansEventBus::Get().Subscribe<Vans::VansActionStartedEvent>(
		[&](const auto&) { ++startedEvents; }, Vans::VansEventLane::GameLogic);
	auto endedConnection = Vans::VansEventBus::Get().Subscribe<Vans::VansActionEndedEvent>(
		[&](const auto&) { ++endedEvents; }, Vans::VansEventLane::GameLogic);
	auto queuedConnection = Vans::VansEventBus::Get().Subscribe<Vans::VansActionQueuedEvent>(
		[&](const auto&) { ++queuedEvents; }, Vans::VansEventLane::GameLogic);
	Vans::VansActionActivationRequest request;
	request.spec = spec;
	request.context.instigator = { 7, 1 };
	request.context.predictionKey = { 2, 5 };
	const auto first = host.Activate(request);
	if (!ExpectGAF(first && first.action && host.Query(first.action)->state == Vans::VansActionInstanceState::Waiting &&
		std::abs(host.Attributes().Current(energyId) - 70.0) < 0.0001 &&
		std::abs(externalCosts.balance - 3.0) < 0.0001 &&
		externalCosts.receipts.ActiveCount() == 1 &&
		host.Tags().Has(tags.Find("Action.Running")->id) &&
		host.Tags().Has(tags.Find("Cooldown.Test")->id) &&
		host.Tags().Has(tags.Find("Cooldown.Shared")->id) && host.IsCooldownActive(action->id),
		"Action Host 未完成激活 Commit")) return false;
	const auto blocked = host.Activate(request);
	if (!ExpectGAF(!blocked && blocked.error == Vans::VansActionError::CooldownActive,
		"Action Host 未执行 Cooldown 门禁")) return false;
	if (!host.Cancel(first.action, Vans::VansActionCancelReason::User, error)) return false;
	if (!ExpectGAF(host.Query(first.action)->state == Vans::VansActionInstanceState::Ended &&
		std::abs(host.Attributes().Current(energyId) - 100.0) < 0.0001 &&
		std::abs(externalCosts.balance - 5.0) < 0.0001 &&
		externalCosts.receipts.ActiveCount() == 0 &&
		!host.Tags().Has(tags.Find("Action.Running")->id),
		"Action 取消未退款或未释放运行资源")) return false;
	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::GameLogic);
	if (!ExpectGAF(startedEvents == 1 && endedEvents == 1,
		"Action lifecycle 事实事件未按 lane 发布")) return false;
	host.Tick(0.5);
	if (!ExpectGAF(host.IsCooldownActive(action->id) &&
		!host.Tags().Has(tags.Find("Cooldown.Test")->id) &&
		host.Tags().Has(tags.Find("Cooldown.Shared")->id),
		"Action Host did not expire independent cooldown entries deterministically")) return false;
	host.Tick(0.5);
	if (!ExpectGAF(!host.IsCooldownActive(action->id) && host.Query(first.action).has_value(),
		"Action cooldown collection or history snapshot is invalid")) return false;
	executorState->tickCount = 0;
	const auto second = host.Activate(request);
	if (!second) return false;
	host.Tick(0.1);
	host.Tick(0.1);
	if (!ExpectGAF(host.Query(second.action)->state == Vans::VansActionInstanceState::Ended &&
		host.Query(second.action)->endReason == Vans::VansActionEndReason::Completed &&
		std::abs(host.Attributes().Current(energyId) - 70.0) < 0.0001 &&
		std::abs(externalCosts.balance - 3.0) < 0.0001 &&
		externalCosts.receipts.ActiveCount() == 0 &&
		executorState->finishCount == 2, "Action Executor 完成路径或 Finish 次数错误")) return false;
	host.Tick(1.0);
	std::shared_ptr<Vans::VansActionHost> hostView(&host, [](Vans::VansActionHost*) {});
	const auto schedulerHandle = scheduler.Register(hostView, error);
	if (!schedulerHandle) return false;
	executorState->tickCount = 0;
	const auto lateAction = host.Activate(request);
	Vans::VansActionEvent event;
	event.type = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Action.Event.Probe");
	event.stableName = "Action.Event.Probe";
	if (!lateAction || !host.EnqueueEvent(lateAction.action, std::move(event), error)) return false;
	if (!ExpectGAF(scheduler.RunLateContinuation() && !scheduler.RunLateContinuation() &&
		executorState->eventCount == 1 && executorState->tickCount == 1,
		"ActionScheduler 未限制 SameFrame late continuation 为一次")) return false;
	if (!host.Cancel(lateAction.action, Vans::VansActionCancelReason::System, error) ||
		!scheduler.Unregister(schedulerHandle)) return false;
	if (!host.Revoke(spec, Vans::VansActionRevokePolicy::KeepRunning, error)) return false;
	Vans::VansActionGrantDesc queuedGrant;
	queuedGrant.action = queuedAction->id;
	queuedGrant.source = 45;
	const Vans::VansActionSpecHandle queuedSpec = host.Grant(queuedGrant, error);
	queuedGrant.action = timeoutAction->id;
	queuedGrant.source = 46;
	const Vans::VansActionSpecHandle timeoutSpec = host.Grant(queuedGrant, error);
	if (!queuedSpec || !timeoutSpec) return ExpectGAF(false, error.c_str());
	executorState->tickCount = 0;
	Vans::VansActionActivationRequest queuedRequest;
	queuedRequest.spec = queuedSpec;
	queuedRequest.context.instigator = { 7, 1 };
	const Vans::VansActionResult queueOwner = host.Activate(queuedRequest);
	const Vans::VansActionResult queued = host.Activate(queuedRequest);
	if (!ExpectGAF(queueOwner && queued &&
		queued.disposition == Vans::VansActionActivationDisposition::Queued &&
		host.Query(queued.action)->state == Vans::VansActionInstanceState::Queued,
		"QueueNew did not return a queryable queued ActionHandle")) return false;
	host.Tick(0.1);
	host.Tick(0.1);
	if (!ExpectGAF(host.Query(queueOwner.action)->state == Vans::VansActionInstanceState::Ended &&
		host.Query(queued.action)->state == Vans::VansActionInstanceState::Waiting,
		"Queued Action did not preserve its Handle while acquiring the released slot")) return false;
	host.Tick(0.1);
	if (!ExpectGAF(host.Query(queued.action)->state == Vans::VansActionInstanceState::Ended,
		"Dequeued Action did not complete through the regular Executor lifecycle")) return false;
	executorState->tickCount = 0;
	const Vans::VansActionResult timeoutOwner = host.Activate(queuedRequest);
	Vans::VansActionActivationRequest timeoutRequest = queuedRequest;
	timeoutRequest.spec = timeoutSpec;
	const Vans::VansActionResult timedQueue = host.Activate(timeoutRequest);
	host.Tick(0.1);
	const auto timedSnapshot = host.Query(timedQueue.action);
	if (!ExpectGAF(timeoutOwner && timedQueue &&
		timedQueue.disposition == Vans::VansActionActivationDisposition::Queued &&
		timedSnapshot && timedSnapshot->state == Vans::VansActionInstanceState::Ended &&
		timedSnapshot->endReason == Vans::VansActionEndReason::TimedOut &&
		timedSnapshot->error == Vans::VansActionError::ConcurrencyQueueExpired,
		"Concurrency queue timeout was not machine-queryable")) return false;
	if (!host.Cancel(timeoutOwner.action, Vans::VansActionCancelReason::System, error)) return false;
	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::GameLogic);
	if (!ExpectGAF(queuedEvents == 2, "Action queued facts were not published exactly once")) return false;
	if (!host.Revoke(queuedSpec, Vans::VansActionRevokePolicy::CancelRunning, error) ||
		!host.Revoke(timeoutSpec, Vans::VansActionRevokePolicy::CancelRunning, error)) return false;
	Vans::VansActionSetDefinition set;
	set.id = Vans::VansMakeStableId<Vans::VansActionSetIdTag>("ActionSet.Test");
	set.name = "ActionSet.Test";
	grant.charges = 1;
	set.grants.push_back(grant);
	const auto setHandle = host.ApplyActionSet(set, error);
	if (!ExpectGAF(setHandle && host.GrantedActions().size() == 1,
		"ActionSet 未批量授予 Action")) return false;
	if (!ExpectGAF(host.RevokeActionSet(setHandle, error) && host.GrantedActions().empty(),
		"ActionSet 未成组撤销 Action")) return false;
	Vans::VansActionHost transitionHost({ 9, 1 }, dependencies);
	if (!transitionHost.Initialize(error)) return ExpectGAF(false, error.c_str());
	Vans::VansActionGrantDesc transitionGrant;
	transitionGrant.source = 90;
	transitionGrant.action = transitionSource->id;
	const Vans::VansActionSpecHandle transitionSourceSpec =
		transitionHost.Grant(transitionGrant, error);
	transitionGrant.source = 91;
	transitionGrant.action = transitionTarget->id;
	const Vans::VansActionSpecHandle transitionTargetSpec =
		transitionHost.Grant(transitionGrant, error);
	transitionGrant.source = 92;
	transitionGrant.action = failureSource->id;
	const Vans::VansActionSpecHandle failureSourceSpec =
		transitionHost.Grant(transitionGrant, error);
	if (!transitionSourceSpec || !transitionTargetSpec || !failureSourceSpec)
		return ExpectGAF(false, error.c_str());
	std::shared_ptr<Vans::VansActionHost> transitionHostView(
		&transitionHost, [](Vans::VansActionHost*) {});
	const Vans::VansActionSchedulerHandle transitionSchedulerHandle =
		scheduler.Register(transitionHostView, error);
	if (!transitionSchedulerHandle) return ExpectGAF(false, error.c_str());
	executorState->tickCount = 0;
	Vans::VansActionActivationRequest routedActivation;
	routedActivation.spec = transitionSourceSpec;
	routedActivation.context.owner = { 9, 1 };
	routedActivation.context.instigator = { 9, 1 };
	const Vans::VansActionResult routedSource = transitionHost.Activate(routedActivation);
	Vans::VansActionCommand routeCommand;
	routeCommand.service = Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Action");
	routeCommand.command = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Transition");
	routeCommand.stableName = "Transition";
	routeCommand.action = routedSource.action;
	routeCommand.context = routedActivation.context;
	routeCommand.payload = Vans::VansSerializedValue::Object({
		{ "action", Vans::VansSerializedValue::String("Action.Test.TransitionTarget") }
	});
	const Vans::VansActionCommandResult routed = services.Execute(routeCommand);
	transitionHost.Tick(0.0);
	const auto routedActions = transitionHost.ActiveActions();
	const auto routedTarget = std::find_if(routedActions.begin(), routedActions.end(),
		[&](const auto& snapshot) { return snapshot.action == transitionTarget->id; });
	if (!ExpectGAF(routed && routedSource &&
		transitionHost.Query(routedSource.action)->endReason == Vans::VansActionEndReason::Interrupted &&
		routedTarget != routedActions.end(),
		"Service.Action did not route a Graph transition through the Host lifecycle")) return false;
	if (!transitionHost.Cancel(
		routedTarget->handle, Vans::VansActionCancelReason::System, error)) return false;
	executorState->tickCount = 0;
	Vans::VansActionActivationRequest transitionActivation;
	transitionActivation.spec = transitionSourceSpec;
	transitionActivation.context.instigator = { 9, 1 };
	transitionActivation.context.primaryTarget = { 99, 1 };
	const Vans::VansActionResult transitionOwner = transitionHost.Activate(transitionActivation);
	Vans::VansActionContext inputContext;
	inputContext.primaryTarget = { 100, 1 };
	const Vans::VansActionResult buffered = transitionHost.ActivateInput("Combo", inputContext);
	if (!ExpectGAF(transitionOwner && buffered &&
		buffered.disposition == Vans::VansActionActivationDisposition::Queued &&
		buffered.action == transitionOwner.action,
		"Action transition input did not enter the configured buffer")) return false;
	transitionHost.Tick(0.3);
	const auto transitionedSource = transitionHost.Query(transitionOwner.action);
	const auto transitionedActions = transitionHost.ActiveActions();
	const auto transitionedTarget = std::find_if(transitionedActions.begin(), transitionedActions.end(),
		[&](const auto& snapshot) { return snapshot.action == transitionTarget->id; });
	if (!ExpectGAF(transitionedSource &&
		transitionedSource->state == Vans::VansActionInstanceState::Ended &&
		transitionedSource->endReason == Vans::VansActionEndReason::Interrupted &&
		transitionedTarget != transitionedActions.end(),
		"Buffered Action transition did not activate its target and interrupt its source")) return false;
	if (!transitionHost.Cancel(
		transitionedTarget->handle, Vans::VansActionCancelReason::System, error)) return false;
	executorState->tickCount = 0;
	Vans::VansActionActivationRequest failureActivation;
	failureActivation.spec = failureSourceSpec;
	failureActivation.context.instigator = { 9, 1 };
	const Vans::VansActionResult failedSource = transitionHost.Activate(failureActivation);
	if (!ExpectGAF(failedSource && transitionHost.Query(failedSource.action)->state ==
		Vans::VansActionInstanceState::Ended,
		"Failure fallback source did not reach a terminal state")) return false;
	transitionHost.Tick(0.0);
	const auto fallbackActions = transitionHost.ActiveActions();
	const auto fallbackTarget = std::find_if(fallbackActions.begin(), fallbackActions.end(),
		[&](const auto& snapshot) { return snapshot.action == transitionTarget->id; });
	if (!ExpectGAF(fallbackTarget != fallbackActions.end(),
		"Action failure fallback was lost before deferred source recycling")) return false;
	if (!transitionHost.Cancel(
		fallbackTarget->handle, Vans::VansActionCancelReason::System, error)) return false;
	if (!scheduler.Unregister(transitionSchedulerHandle)) return false;
	Vans::VansActionHostDependencies limitedDependencies = dependencies;
	limitedDependencies.limits.maximumActiveActions = 1;
	limitedDependencies.limits.maximumPayloadBytes = 32;
	Vans::VansActionHost limitedHost({ 8, 1 }, limitedDependencies);
	if (!limitedHost.Initialize(error)) return ExpectGAF(false, error.c_str());
	Vans::VansActionGrantDesc limitedGrant;
	limitedGrant.action = queuedAction->id;
	limitedGrant.source = 88;
	const Vans::VansActionSpecHandle limitedSpec = limitedHost.Grant(limitedGrant, error);
	Vans::VansActionActivationRequest limitedRequest;
	limitedRequest.spec = limitedSpec;
	limitedRequest.context.instigator = { 8, 1 };
	Vans::VansActionActivationRequest oversizedPayloadRequest = limitedRequest;
	oversizedPayloadRequest.context.payload = Vans::VansSerializedValue::Object({
		{ "oversized", Vans::VansSerializedValue::String(std::string(64, 'x')) }
	});
	if (!ExpectGAF(limitedHost.CanActivate(oversizedPayloadRequest.spec,
		oversizedPayloadRequest.context).error == Vans::VansActionError::BudgetExceeded,
		"Action Host accepted a Context payload above the project budget")) return false;
	const Vans::VansActionResult limitedFirst = limitedHost.Activate(limitedRequest);
	const Vans::VansActionResult limitedBlocked = limitedHost.Activate(limitedRequest);
	if (!ExpectGAF(limitedFirst && !limitedBlocked &&
		limitedBlocked.error == Vans::VansActionError::BudgetExceeded,
		"Action Host budget did not reject an excess active Action")) return false;
	if (!limitedHost.Cancel(limitedFirst.action, Vans::VansActionCancelReason::System, error)) return false;
	const Vans::VansActionResult limitedAfterRelease = limitedHost.Activate(limitedRequest);
	if (!limitedAfterRelease) return ExpectGAF(false,
		"Action Host budget capacity was not restored after an Action ended");
	if (!limitedHost.Cancel(
		limitedAfterRelease.action, Vans::VansActionCancelReason::System, error)) return false;
	Vans::VansActionHost persistenceSource({ 10, 1 }, dependencies);
	if (!persistenceSource.Initialize(error) ||
		!persistenceSource.Attributes().SetBase(energyId, 77.0)) return false;
	Vans::VansActionGrantDesc persistentGrant;
	persistentGrant.action = action->id;
	persistentGrant.source = 1001;
	persistentGrant.persistence = Vans::VansActionGrantPersistence::Persistent;
	const auto persistentSpec = persistenceSource.Grant(persistentGrant, error);
	Vans::VansActionActivationRequest persistentActivation;
	persistentActivation.spec = persistentSpec;
	persistentActivation.context.instigator = { 10, 1 };
	const auto persistentAction = persistenceSource.Activate(persistentActivation);
	if (!persistentAction || !persistenceSource.Cancel(
		persistentAction.action, Vans::VansActionCancelReason::User, error)) return false;
	Vans::VansActionHostPersistentState persistentState;
	if (!persistenceSource.CapturePersistentState(persistentState, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansActionHost persistenceTarget({ 11, 1 }, dependencies);
	if (!persistenceTarget.Initialize(error) ||
		!persistenceTarget.RestorePersistentState(persistentState, error))
		return ExpectGAF(false, error.c_str());
	const auto restoredGrants = persistenceTarget.GrantedActions();
	if (!ExpectGAF(restoredGrants.size() == 1 &&
		restoredGrants.front().persistence == Vans::VansActionGrantPersistence::Persistent &&
		std::abs(persistenceTarget.Attributes().Base(energyId) - 77.0) < 0.0001 &&
		persistenceTarget.IsCooldownActive(action->id),
		"Action Host persistent Grant, Attribute, or cooldown did not round-trip")) return false;
	auto incompatibleState = persistentState;
	incompatibleState.version = 99;
	return ExpectGAF(!persistenceTarget.RestorePersistentState(incompatibleState, error),
		"Action Host accepted an incompatible persistence state version");
}

bool TestGAFPackagingContract()
{
	const char* failureStage = "load engine GAF configuration";
	bool completed = false;
	struct FailureStageReporter
	{
		const char*& stage;
		bool& completed;
		~FailureStageReporter()
		{
			if (!completed)
				std::cerr << "[GAF] Packaging contract failed during: " << stage << '\n';
		}
	} failureStageReporter{ failureStage, completed };
	const std::filesystem::path sourceRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	Vans::VansGAFProjectConfiguration configuration;
	std::string error;
	if (!Vans::VansGAFProjectConfiguration::Load(
		sourceRoot / "EngineAssets/GAF/ProjectSettings", configuration, error))
		return ExpectGAF(false, error.c_str());
	failureStage = "initialize editable project configuration";

	const std::filesystem::path projectRoot =
		std::filesystem::temp_directory_path() / "ForestGAFPackagingContract";
	std::error_code cleanupError;
	std::filesystem::remove_all(projectRoot, cleanupError);
	struct Cleanup
	{
		std::filesystem::path path;
		~Cleanup()
		{
			std::error_code errorCode;
			std::filesystem::remove_all(path, errorCode);
		}
	} cleanup{ projectRoot };
	const std::filesystem::path assetsRoot = projectRoot / "Assets";
	std::filesystem::create_directories(assetsRoot);
	if (!Vans::VansGAFProjectConfiguration::EnsureProjectFiles(
		projectRoot / "ProjectSettings",
		sourceRoot / "EngineAssets/GAF/ProjectSettings", error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGAFProjectConfiguration projectConfiguration;
	if (!Vans::VansGAFProjectConfiguration::LoadForProject(
		projectRoot, sourceRoot, projectConfiguration, error))
	{
		return ExpectGAF(false, error.c_str());
	}
	if (!ExpectGAF(projectConfiguration.templates.size() == configuration.templates.size() &&
			std::filesystem::is_regular_file(projectRoot / "ProjectSettings/GAFSettings.json") &&
			std::filesystem::is_regular_file(projectRoot / "ProjectSettings/GAFSchemaRegistry.json") &&
			std::filesystem::is_regular_file(projectRoot / "ProjectSettings/GAFValidationRules.json") &&
			std::filesystem::is_regular_file(projectRoot / "ProjectSettings/GAFTemplates.json"),
			"GAF project settings were not initialized as a complete editable set")) return false;
	failureStage = "round-trip project configuration";
	projectConfiguration.settings.performance.maximumActiveActionsPerHost = 65;
	if (!Vans::VansGAFProjectConfiguration::Save(
		projectRoot / "ProjectSettings", projectConfiguration, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGAFProjectConfiguration savedConfiguration;
	if (!Vans::VansGAFProjectConfiguration::Load(
		projectRoot / "ProjectSettings", savedConfiguration, error) ||
		!ExpectGAF(savedConfiguration.settings.performance.maximumActiveActionsPerHost == 65 &&
			savedConfiguration.templates.size() == projectConfiguration.templates.size() &&
			savedConfiguration.allowlist.nodeTypes == projectConfiguration.allowlist.nodeTypes,
			"GAF project configuration did not round-trip all four files")) return false;
	Vans::VansGAFProjectConfiguration invalidConfiguration = savedConfiguration;
	invalidConfiguration.settings.performance.maximumActiveActionsPerHost = 0;
	std::string invalidConfigurationError;
	if (!ExpectGAF(!Vans::VansGAFProjectConfiguration::Save(
		projectRoot / "ProjectSettings", invalidConfiguration, invalidConfigurationError) &&
		!invalidConfigurationError.empty(),
		"GAF project configuration accepted a zero runtime budget")) return false;
	failureStage = "index and author gameplay assets";
	Vans::VansAssetDatabase database(assetsRoot, projectRoot / "Library/Artifacts");
	const std::filesystem::path graphPath = assetsRoot / "RootActionGraph.vactiongraph";
	Vans::VansSerializedValue graph = configuration.templates.at("ActionGraph");
	if (!Vans::SetSerializedPointer(graph, "/nodes/0/type",
		Vans::VansSerializedValue::String("Action.Graph.Wait"), &error) ||
		!Vans::SetSerializedPointer(graph, "/nodes/0/properties",
			Vans::VansSerializedValue::Object({
				{ "seconds", Vans::VansSerializedValue::Float(10.0) }
			}), &error)) return ExpectGAF(false, error.c_str());
	if (!Vans::VansGameplayAssetStorage::SaveSourceAtomic(
		graphPath, graph, error))
		return ExpectGAF(false, error.c_str());
	const Vans::VansAssetScanResult graphScan =
		database.Scan(Vans::VansAssetOperationPolicy::Authoring());
	const auto graphRecord = database.Find(graphPath);
	if (!ExpectGAF(graphScan && graphRecord.has_value(),
		"GAF package contract could not register the Action Graph")) return false;

	const std::filesystem::path effectPath = assetsRoot / "ReferencedEffect.veffect";
	if (!Vans::VansGameplayAssetStorage::SaveSourceAtomic(
		effectPath, configuration.templates.at("GameplayEffect"), error))
		return ExpectGAF(false, error.c_str());
	const Vans::VansAssetScanResult effectScan =
		database.Scan(Vans::VansAssetOperationPolicy::Authoring());
	const auto effectRecord = database.Find(effectPath);
	if (!ExpectGAF(effectScan && effectRecord.has_value(),
		"GAF package contract could not register referenced effect")) return false;

	Vans::VansSerializedValue action = configuration.templates.at("ActionDefinition");
	if (!Vans::SetSerializedPointer(action, "/actionId",
		Vans::VansSerializedValue::String("Gameplay.Contract.Root"), &error) ||
		!Vans::SetSerializedPointer(action, "/execution/variables",
			Vans::VansSerializedValue::Array({
				Vans::VansSerializedValue::Object({
					{ "name", Vans::VansSerializedValue::String("TimelineValue") },
					{ "default", Vans::VansSerializedValue::Float(0.25) }
				})
			}), &error) ||
		!Vans::SetSerializedPointer(action, "/execution/executor",
		Vans::VansSerializedValue::String("Action.Executor.Graph"), &error) ||
		!Vans::SetSerializedPointer(action, "/execution/graph",
			Vans::VansSerializedValue::Object({
				{ "assetGuid", Vans::VansSerializedValue::String(graphRecord->guid.ToString()) }
			}), &error)) return ExpectGAF(false, error.c_str());
	Vans::SetSerializedObjectField(action, "contractReference",
		Vans::VansSerializedValue::Object({
			{ "assetGuid", Vans::VansSerializedValue::String(effectRecord->guid.ToString()) }
		}));
	const std::filesystem::path actionPath = assetsRoot / "RootAction.vaction";
	if (!Vans::VansGameplayAssetStorage::SaveSourceAtomic(actionPath, action, error))
		return ExpectGAF(false, error.c_str());
	const Vans::VansAssetScanResult actionScan =
		database.Scan(Vans::VansAssetOperationPolicy::Authoring());
	const auto actionRecord = database.Find(actionPath);
	if (!ExpectGAF(actionScan && actionRecord.has_value(),
		"GAF package contract could not register root action")) return false;
	failureStage = "cook recursive gameplay asset closure";

	const Vans::VansGameplayPackageCookResult packaged =
		Vans::VansGameplayAssetPackageCooker::CookClosure(
			projectRoot, database, nullptr, { actionRecord->guid.ToString() });
	if (!ExpectGAF(packaged && packaged.assets.size() == 3 &&
		packaged.requiredAssetGuids.size() == 3,
		"GAF package cooker did not produce the recursive dependency closure")) return false;
	for (const Vans::VansGameplayPackagedAssetRecord& record : packaged.assets)
	{
		Vans::VansGameplayCookedAsset cooked;
		if (!ExpectGAF(std::filesystem::is_regular_file(record.artifactPath) &&
			Vans::VansGameplayAssetStorage::LoadCooked(record.artifactPath, cooked, error) &&
			cooked.contentHash == record.contentHash,
			"GAF packaged artifact could not be verified")) return false;
	}
	Vans::VansGameplayAssetLibrary sourceLibrary;
	if (!sourceLibrary.Load(database.All(), error) ||
		!ExpectGAF(sourceLibrary.AssetCount() == 3 &&
			sourceLibrary.ResolveAction(actionRecord->guid.ToString()) != nullptr &&
			sourceLibrary.ResolveAction(actionRecord->guid.ToString())->executionGraph != nullptr,
			"GAF source asset library did not compile the indexed Action")) return false;
	failureStage = "initialize gameplay runtime and host";
	Vans::VansGAFSettings runtimeSettings = projectConfiguration.settings;
	runtimeSettings.performance.maximumActiveActionsPerHost = 2;
	runtimeSettings.performance.maximumTasksPerAction = 3;
	runtimeSettings.performance.maximumGraphTransitionsPerTick = 4;
	runtimeSettings.performance.maximumEffectsPerHost = 5;
	Vans::VansGameplayRuntime gameplayRuntime;
	Vans::VansGameplayRuntimeDependencies runtimeDependencies;
	const auto runtimeFakeServices = Vans::VansCreateFakeStandardActionServices();
	for (const auto& fake : runtimeFakeServices) runtimeDependencies.services.push_back(fake);
	if (!gameplayRuntime.Initialize(database.All(), runtimeSettings, runtimeDependencies, error))
		return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(gameplayRuntime.Settings().performance.maximumActiveActionsPerHost == 2 &&
		gameplayRuntime.Settings().performance.maximumTasksPerAction == 3 &&
		gameplayRuntime.Settings().performance.maximumGraphTransitionsPerTick == 4 &&
		gameplayRuntime.Settings().performance.maximumEffectsPerHost == 5 &&
		gameplayRuntime.Services().Resolve(
			Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Action")) &&
		gameplayRuntime.Services().Resolve(
			Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Camera")),
		"Gameplay Runtime did not retain the project GAF performance settings")) return false;
	Vans::VansRuntimeWorld world;
	world.Commands().CreateEntity({ "gaf-runtime-owner", "GAF Runtime Owner", {}, true });
	world.FlushCommands();
	const Vans::VansEntityHandle owner = world.Entities().FindByGuid("gaf-runtime-owner");
	Vans::VansGameplayActionHostSetup hostSetup;
	hostSetup.grants.push_back({ actionRecord->guid.ToString() });
	std::shared_ptr<Vans::VansActionHost> runtimeHost =
		gameplayRuntime.CreateHost(owner, hostSetup, error);
	if (!ExpectGAF(runtimeHost && runtimeHost->GrantedActions().size() == 1,
		"GAF Runtime did not create a Host with its configured direct grant")) return false;
	world.Commands().AddActionHostComponent(
		owner, "gaf-runtime-host", runtimeHost, hostSetup.enabled);
	world.FlushCommands();
	const Vans::VansComponentHandle hostComponent = world.FindComponentByGuid(
		"gaf-runtime-host", Vans::VansRuntimeComponentType_ActionHost);
	if (!ExpectGAF(hostComponent.IsValid() && runtimeHost->IsEnabled(),
		"RuntimeWorld did not retain the configured ActionHost component")) return false;
	Vans::VansActionActivationRequest activation;
	activation.spec = runtimeHost->GrantedActions().front().handle;
	activation.context.owner = owner;
	activation.context.instigator = owner;
	activation.context.primaryTarget = owner;
	const Vans::VansActionResult activationResult = runtimeHost->Activate(activation);
	if (!ExpectGAF(activationResult && runtimeHost->Query(activationResult.action).has_value(),
		"Scene ActionHost could not activate its configured Action")) return false;
	Vans::VansActionSystem actionSystem(gameplayRuntime);
	const Vans::VansActionHostRef actionHostRef{ owner };
	Vans::VansActionContext apiContext;
	apiContext.owner = owner;
	apiContext.instigator = owner;
	const auto apiReport = actionSystem.CanActivate(actionHostRef,
		runtimeHost->GrantedActions().front().handle, apiContext, true, false);
	const auto apiViews = actionSystem.QueryActive({ actionHostRef });
	const auto apiInspection = actionSystem.Inspect({ actionHostRef, activationResult.action });
	if (!ExpectGAF(apiReport.allowed && apiViews.size() == 1 && apiInspection &&
		apiInspection->instance.handle == activationResult.action,
		"Public ActionSystem API did not validate, query, and inspect the live Host")) return false;
	failureStage = "compile and execute gameplay Timeline";

	const std::array<const char*, 6> gafTimelineTracks{
		"Action.Event", "Action.Window", "Action.Cue", "Action.Parameter",
		"Action.SubAction", "Action.Marker"
	};
	const auto& timelineExtensions = Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	const auto timelineDescriptors =
		Vans::VansTimelineTrackDescriptorRegistry::Build(timelineExtensions);
	for (const char* stableName : gafTimelineTracks)
	{
		const auto descriptor = std::find_if(timelineDescriptors.begin(), timelineDescriptors.end(),
			[stableName](const auto& item) { return item.stableName == stableName; });
		if (!ExpectGAF(timelineExtensions.Resolve(stableName) != nullptr &&
			descriptor != timelineDescriptors.end(),
			"GAF Timeline track is missing from runtime or editor registries")) return false;
	}

	Vans::VansTimelineAsset timelineAsset;
	timelineAsset.durationTicks = 12;
	timelineAsset.playbackRange = { 0, 12 };
	timelineAsset.workRange = timelineAsset.playbackRange;
	Vans::VansTimelineBinding timelineBinding;
	timelineBinding.id = "gaf-owner";
	timelineBinding.stableId = Vans::VansMakeStableId<Vans::VansTimelineBindingTag>(timelineBinding.id);
	timelineBinding.displayName = "GAF Owner";
	timelineBinding.kind = Vans::VansTimelineBindingKind::RuntimeObject;
	timelineAsset.bindings.push_back(timelineBinding);
	const auto addTimelineTrack = [&](const std::string& type,
		Vans::VansTimelineTick start, Vans::VansTimelineTick duration,
		Vans::VansSerializedValue fields, std::vector<Vans::VansTimelineChannel> channels = {})
	{
		Vans::VansTimelineTrack track;
		track.id = "gaf-track-" + type;
		track.type = Vans::VansTimelineTrackTypeRef::FromName(type);
		track.bindingId = timelineBinding.id;
		track.extensionData = std::move(fields);
		Vans::VansTimelineSection section;
		section.id = "gaf-section-" + type;
		section.startTick = start;
		section.durationTicks = duration;
		section.sourceOutTick = duration;
		section.channels = std::move(channels);
		track.sections.push_back(std::move(section));
		timelineAsset.tracks.push_back(std::move(track));
	};
	const auto emptyPayload = []
	{
		return Vans::VansSerializedValue::Object({});
	};
	addTimelineTrack("Action.Parameter", 0, 10,
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String("Gameplay.Contract.Root") },
			{ "variable", Vans::VansSerializedValue::String("TimelineValue") },
			{ "valueType", Vans::VansSerializedValue::String("Float") }
		}), { Vans::VansTimelineChannel{
			"gaf-parameter-channel", "value", Vans::VansTimelineValueType::Float,
			Vans::VansTimelineExtrapolation::None, Vans::VansTimelineExtrapolation::None,
			{ { "gaf-parameter-key", 0, 0.75f,
				Vans::VansTimelineInterpolation::Constant } } } });
	addTimelineTrack("Action.Window", 1, 1,
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String("Gameplay.Contract.Root") },
			{ "window", Vans::VansSerializedValue::String("Attack") },
			{ "payload", emptyPayload() }
		}));
	addTimelineTrack("Action.Event", 2, 1,
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String("Gameplay.Contract.Root") },
			{ "event", Vans::VansSerializedValue::String("Timeline.Contract.Event") },
			{ "payload", emptyPayload() }
		}));
	addTimelineTrack("Action.Marker", 2, 1,
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String("Gameplay.Contract.Root") },
			{ "marker", Vans::VansSerializedValue::String("Contract") },
			{ "payload", emptyPayload() }
		}));
	addTimelineTrack("Action.SubAction", 2, 1,
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String("Gameplay.Contract.Missing") },
			{ "payload", emptyPayload() },
			{ "failurePolicy", Vans::VansSerializedValue::String("Ignore") }
		}));
	addTimelineTrack("Action.Cue", 8, 1,
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String("Gameplay.Contract.Root") },
			{ "cue", Vans::VansSerializedValue::String("Cue.Contract") },
			{ "mode", Vans::VansSerializedValue::String("Execute") },
			{ "scope", Vans::VansSerializedValue::String("Owner") },
			{ "payload", emptyPayload() },
			{ "intensity", Vans::VansSerializedValue::Float(1.0) }
		}));

	Vans::VansTimelineCompileOptions timelineOptions;
	timelineOptions.extensions = &timelineExtensions;
	const auto compiledTimeline = Vans::VansTimelineCompiler::Compile(timelineAsset, timelineOptions);
	if (!compiledTimeline)
	{
		for (const auto& diagnostic : compiledTimeline.diagnostics)
			std::cerr << "[GAF Timeline] " << diagnostic.code << ": " << diagnostic.message << '\n';
		return ExpectGAF(false,
			"GAF Timeline tracks did not compile through the shared Timeline compiler");
	}
	Vans::VansTimelineApplierRegistry timelineAppliers;
	if (!Vans::VansRegisterGameplayActionTimelineIntegration(
		gameplayRuntime, timelineAppliers, error)) return ExpectGAF(false, error.c_str());
	for (const char* stableName : gafTimelineTracks)
	{
		const auto output = Vans::VansMakeStableId<Vans::VansTimelineOutputTypeTag>(
			std::string(stableName) + ".Output");
		if (!ExpectGAF(timelineAppliers.SlotOf(output) != Vans::VansInvalidTimelineApplierSlot,
			"GAF Timeline output applier is not registered")) return false;
	}
	if (!timelineAppliers.Seal(error)) return ExpectGAF(false, error.c_str());
	Vans::VansTimelineSessionService timelineSessions(
		Vans::VansTimelineClockRegistry::BuiltIns(), timelineAppliers);
	const auto runTimelineSession = [&](Vans::VansTimelineSessionKind kind)
	{
		Vans::VansTimelineSessionDesc desc;
		desc.kind = kind;
		desc.timeline = compiledTimeline.timeline;
		desc.owner = owner;
		desc.clockType = std::string(Vans::TimelineClockNames::Manual);
		desc.runtimeBindings.push_back({ timelineBinding.stableId,
			Vans::VansMakeStableId<Vans::VansRuntimeObjectTypeTag>("Gameplay.ActionHostOwner"),
			Vans::VansGenerationHandle{ owner.index, owner.generation }, 1 });
		const auto created = timelineSessions.Create(desc);
		if (!created || !timelineSessions.Play(created.handle))
			return Vans::VansTimelineSessionHandle{};
		timelineSessions.Advance(created.handle, 4.0 / 60000.0);
		timelineSessions.Evaluate(
			created.handle, Vans::VansTimelineEvaluationPhase::PostScript);
		return created.handle;
	};
	const Vans::VansTimelineSessionHandle actionTimeline =
		runTimelineSession(Vans::VansTimelineSessionKind::Action);
	Vans::VansSerializedValue timelineValue;
	const Vans::VansActionFieldId timelineVariable =
		Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("TimelineValue");
	if (!ExpectGAF(actionTimeline &&
		runtimeHost->ReadVariable(activationResult.action, timelineVariable, timelineValue, error) &&
		timelineValue.kind == Vans::VansSerializedValue::Kind::Float &&
		std::abs(timelineValue.floatValue - 0.75) < 0.0001,
		"Action Timeline did not apply its sampled parameter")) return false;
	const auto actionTimelineState = timelineSessions.Query(actionTimeline);
	if (!ExpectGAF(actionTimelineState &&
		actionTimelineState->state != Vans::VansTimelinePlayerState::Error,
		"Action Timeline event/window/marker/sub-action outputs failed")) return false;
	if (!timelineSessions.Release(actionTimeline) ||
		!runtimeHost->ReadVariable(activationResult.action, timelineVariable, timelineValue, error) ||
		!ExpectGAF(timelineValue.kind == Vans::VansSerializedValue::Kind::Float &&
			std::abs(timelineValue.floatValue - 0.25) < 0.0001,
			"Action Timeline parameter did not restore on session release")) return false;
	const Vans::VansTimelineSessionHandle previewTimeline =
		runTimelineSession(Vans::VansTimelineSessionKind::Preview);
	if (!ExpectGAF(previewTimeline &&
		runtimeHost->ReadVariable(activationResult.action, timelineVariable, timelineValue, error) &&
		std::abs(timelineValue.floatValue - 0.25) < 0.0001,
		"GAF Timeline appliers executed destructive gameplay behavior in Preview")) return false;
	if (!timelineSessions.Release(previewTimeline))
		return ExpectGAF(false, "GAF preview Timeline session could not be released");
	world.SetComponentEnabled(hostComponent, false);
	gameplayRuntime.SynchronizeHostEnablement(world);
	if (!ExpectGAF(!runtimeHost->IsEnabled(),
		"ActionHost did not follow RuntimeWorld component disablement")) return false;
	world.SetComponentEnabled(hostComponent, true);
	world.SetEntityActive(owner, false);
	gameplayRuntime.SynchronizeHostEnablement(world);
	if (!ExpectGAF(!runtimeHost->IsEnabled(),
		"ActionHost ignored owner hierarchy disablement")) return false;
	world.SetEntityActive(owner, true);
	gameplayRuntime.SynchronizeHostEnablement(world);
	gameplayRuntime.TickEarly(0.016);
	gameplayRuntime.RunLateContinuation();
	if (!ExpectGAF(runtimeHost->IsEnabled(),
		"ActionHost did not recover after owner hierarchy reactivation")) return false;
	failureStage = "load cooked gameplay asset library";
	std::vector<Vans::VansAssetRecord> packagedRecords;
	for (const Vans::VansGameplayPackagedAssetRecord& packagedAsset : packaged.assets)
	{
		Vans::VansAssetGuid guid;
		if (!Vans::VansAssetGuid::TryParse(packagedAsset.guid, guid))
			return ExpectGAF(false, "GAF package cooker emitted an invalid asset guid");
		Vans::VansAssetRecord record;
		record.guid = guid;
		record.type = packagedAsset.assetType;
		record.state = Vans::VansAssetState::CpuReady;
		record.sourcePath = packagedAsset.sourcePath;
		record.artifactPath = packagedAsset.artifactPath;
		record.artifactFormat = Vans::VansAssetArtifactFormat::Cooked;
		packagedRecords.push_back(std::move(record));
	}
	Vans::VansGameplayAssetLibrary cookedLibrary;
	if (!cookedLibrary.Load(packagedRecords, error) ||
		!ExpectGAF(cookedLibrary.AssetCount() == sourceLibrary.AssetCount() &&
			cookedLibrary.ResolveAction(actionRecord->guid.ToString()) != nullptr,
			"GAF cooked asset library does not match source-mode resolution")) return false;
	gameplayRuntime.Shutdown();
	world.Clear();
	failureStage = "reject editor-only assets during cooking";

	const std::filesystem::path layoutPath = assetsRoot / "EditorOnly.gafeditorlayout";
	if (!Vans::VansGameplayAssetStorage::SaveSourceAtomic(layoutPath,
		configuration.templates.at("GAFEditorLayout"), error))
		return ExpectGAF(false, error.c_str());
	const Vans::VansAssetScanResult layoutScan =
		database.Scan(Vans::VansAssetOperationPolicy::Authoring());
	const auto layoutRecord = database.Find(layoutPath);
	if (!ExpectGAF(layoutScan && layoutRecord.has_value(),
		"GAF package contract could not register editor layout")) return false;
	const Vans::VansGameplayPackageCookResult editorOnly =
		Vans::VansGameplayAssetPackageCooker::CookClosure(
			projectRoot, database, nullptr, { layoutRecord->guid.ToString() });
	completed = ExpectGAF(!editorOnly && !editorOnly.errors.empty() && editorOnly.assets.empty(),
		"GAF package cooker accepted an editor-only asset");
	return completed;
}

bool TestGAFNetworkContract()
{
	Vans::VansActionActivationNetworkMessage activation;
	activation.action = Vans::VansMakeStableId<Vans::VansActionIdTag>("Gameplay.Network.Contract");
	activation.context.owner = { 4, 2 };
	activation.context.instigator = { 5, 3 };
	activation.context.source = { 6, 4 };
	activation.context.primaryTarget = { 7, 5 };
	activation.context.predictionKey = { 17, 99 };
	activation.context.randomSeed = UINT64_MAX - 3;
	activation.context.payload = Vans::VansSerializedValue::Object({
		{ "damage", Vans::VansSerializedValue::Float(42.5) },
		{ "tags", Vans::VansSerializedValue::Array({
			Vans::VansSerializedValue::String("Damage.Fire") }) }
	});
	activation.hasTargetData = true;
	activation.targetData.values.push_back(Vans::VansEntityHandle{ 8, 1 });
	activation.targetData.values.push_back(Vans::VansTargetLocation{ { 1.0, 2.0, 3.0 } });
	activation.targetData.values.push_back(Vans::VansTargetDirection{ { 0.0, 0.0, 1.0 } });
	activation.targetData.values.push_back(Vans::VansTargetTransform{
		{ 4.0, 5.0, 6.0 }, { 0.0, 0.0, 0.0, 1.0 }, { 1.0, 1.0, 1.0 } });
	activation.targetData.values.push_back(Vans::VansTargetArea{
		{ 10.0, 20.0, 30.0 }, 12.0 });
	activation.targetData.values.push_back(Vans::VansTargetShape{
		Vans::VansTargetShapeKind::Capsule,
		{ { 2.0, 3.0, 4.0 }, { 0.0, 0.0, 0.0, 1.0 }, { 1.0, 1.0, 1.0 } },
		{ 0.5, 1.5, 0.5 }, 0.5, 1.5 });
	activation.targetData.values.push_back(Vans::VansTargetRay{
		{ 1.0, 2.0, 3.0 }, { 0.0, 0.0, 1.0 }, 100.0 });
	activation.targetData.values.push_back(Vans::VansTargetHitResult{
		{ 9, 1 }, { 3.0, 2.0, 1.0 }, { 0.0, 1.0, 0.0 }, 12.0,
		Vans::VansMakeStableId<Vans::VansGameplayTagIdTag>("Surface.Metal") });
	activation.targetData.values.push_back(Vans::VansDeferredTargetQuery{
		Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.PhysicsQuery"),
		Vans::VansSerializedValue::Object({
			{ "shape", Vans::VansSerializedValue::String("Capsule") },
			{ "radius", Vans::VansSerializedValue::Float(0.5) } }) });
	activation.definitionContentHash = UINT64_MAX - 7;
	Vans::VansActionNetworkPacket packet;
	packet.header.type = Vans::VansActionNetworkMessageType::ActivationRequest;
	packet.header.connection = 17;
	packet.header.sequence = 1;
	packet.header.dictionaryVersion = 7;
	packet.header.contentManifestHash = 9;
	packet.payload = Vans::VansEncodeActionActivationMessage(activation);
	std::vector<std::uint8_t> bytes;
	const auto encoded = Vans::VansActionNetworkCodec::Encode(packet, bytes);
	if (!ExpectGAF(bytes.size() > 4 && bytes[0] == 0x47 && bytes[1] == 0x41 &&
		bytes[2] == 0x46 && bytes[3] == 0x31,
		"Gameplay Action network packet does not use the canonical little-endian wire format"))
		return false;
	Vans::VansActionNetworkPacket decodedPacket;
	const auto decoded = Vans::VansActionNetworkCodec::Decode(bytes, decodedPacket);
	Vans::VansActionActivationNetworkMessage decodedActivation;
	std::string error;
	Vans::VansTargetDataNetworkPolicy targetPolicy;
	targetPolicy.entityAllowed = [](Vans::VansEntityHandle entity) { return entity.index < 100; };
	targetPolicy.deferredServiceAllowed = [](Vans::VansActionServiceId service)
	{
		return service == Vans::VansMakeStableId<Vans::VansActionServiceIdTag>(
			"Service.PhysicsQuery");
	};
	if (!ExpectGAF(encoded && decoded &&
		Vans::VansDecodeActionActivationMessage(
			decodedPacket.payload, decodedActivation, error, targetPolicy) &&
		decodedActivation.action == activation.action &&
		decodedActivation.context.owner == activation.context.owner &&
		decodedActivation.context.randomSeed == activation.context.randomSeed &&
		decodedActivation.definitionContentHash == activation.definitionContentHash &&
		decodedActivation.hasTargetData && decodedActivation.targetData.values.size() == 9 &&
		std::holds_alternative<Vans::VansEntityHandle>(decodedActivation.targetData.values[0]) &&
		std::holds_alternative<Vans::VansTargetLocation>(decodedActivation.targetData.values[1]) &&
		std::holds_alternative<Vans::VansTargetDirection>(decodedActivation.targetData.values[2]) &&
		std::holds_alternative<Vans::VansTargetTransform>(decodedActivation.targetData.values[3]) &&
		std::holds_alternative<Vans::VansTargetArea>(decodedActivation.targetData.values[4]) &&
		std::holds_alternative<Vans::VansTargetShape>(decodedActivation.targetData.values[5]) &&
		std::holds_alternative<Vans::VansTargetRay>(decodedActivation.targetData.values[6]) &&
		std::holds_alternative<Vans::VansTargetHitResult>(decodedActivation.targetData.values[7]) &&
		std::holds_alternative<Vans::VansDeferredTargetQuery>(decodedActivation.targetData.values[8]),
		"Gameplay Action network packet did not round-trip without identity loss")) return false;
	Vans::VansActionActivationNetworkMessage rejectedTargetData;
	Vans::VansTargetDataNetworkPolicy restrictiveTargetPolicy = targetPolicy;
	restrictiveTargetPolicy.maximumTargets = 8;
	if (!ExpectGAF(!Vans::VansDecodeActionActivationMessage(
		decodedPacket.payload, rejectedTargetData, error, restrictiveTargetPolicy),
		"Gameplay Action network accepted TargetData above the count budget")) return false;
	restrictiveTargetPolicy = targetPolicy;
	restrictiveTargetPolicy.maximumDistance = 50.0;
	if (!ExpectGAF(!Vans::VansDecodeActionActivationMessage(
		decodedPacket.payload, rejectedTargetData, error, restrictiveTargetPolicy),
		"Gameplay Action network accepted TargetData above the distance budget")) return false;
	restrictiveTargetPolicy = targetPolicy;
	restrictiveTargetPolicy.entityAllowed = [](Vans::VansEntityHandle entity)
		{ return entity.index != 9; };
	if (!ExpectGAF(!Vans::VansDecodeActionActivationMessage(
		decodedPacket.payload, rejectedTargetData, error, restrictiveTargetPolicy),
		"Gameplay Action network accepted unauthorized TargetData entities")) return false;
	restrictiveTargetPolicy = targetPolicy;
	restrictiveTargetPolicy.maximumDeferredDescriptorBytes = 4;
	if (!ExpectGAF(!Vans::VansDecodeActionActivationMessage(
		decodedPacket.payload, rejectedTargetData, error, restrictiveTargetPolicy),
		"Gameplay Action network accepted an oversized Deferred TargetData descriptor")) return false;

	Vans::VansActionNetworkPeerPolicy peerPolicy;
	peerPolicy.dictionaryVersion = 7;
	peerPolicy.contentManifestHash = 9;
	peerPolicy.maximumPacketsPerSecond = 1000.0;
	peerPolicy.burstPackets = 64.0;
	Vans::VansActionNetworkGate gate(peerPolicy);
	if (!ExpectGAF(static_cast<bool>(gate.Accept(packet, 0.0)),
		"Gameplay Action network gate rejected a valid packet")) return false;
	if (!ExpectGAF(gate.Accept(packet, 0.001).error == Vans::VansActionNetworkError::Duplicate,
		"Gameplay Action network replay window accepted a duplicate")) return false;
	packet.header.sequence = 3;
	if (!gate.Accept(packet, 0.002)) return false;
	packet.header.sequence = 2;
	if (!ExpectGAF(static_cast<bool>(gate.Accept(packet, 0.003)),
		"Gameplay Action network replay window rejected valid reordering")) return false;
	packet.header.sequence = 4;
	packet.header.contentManifestHash = 10;
	if (!ExpectGAF(gate.Accept(packet, 0.004).error == Vans::VansActionNetworkError::ContentMismatch,
		"Gameplay Action network gate accepted a content mismatch")) return false;

	Vans::VansActionLoopbackConfig loopbackConfig;
	loopbackConfig.latencyTicks = 2;
	loopbackConfig.dropEvery = 3;
	loopbackConfig.duplicateEvery = 2;
	Vans::VansActionLoopbackTransport loopback(loopbackConfig);
	if (!loopback.Send(17, 23, bytes, error)) return ExpectGAF(false, error.c_str());
	loopback.Advance();
	std::vector<std::uint8_t> delivered;
	if (!ExpectGAF(!loopback.Receive(23, delivered),
		"Loopback transport ignored configured latency")) return false;
	loopback.Advance();
	if (!ExpectGAF(loopback.Receive(23, delivered) && delivered == bytes,
		"Loopback transport did not deliver the packet")) return false;
	if (!loopback.Send(17, 23, bytes, error)) return false;
	loopback.Advance(2);
	const bool duplicateFirst = loopback.Receive(23, delivered);
	const bool duplicateSecond = loopback.Receive(23, delivered);
	if (!ExpectGAF(duplicateFirst && duplicateSecond,
		"Loopback transport did not reproduce duplicate delivery")) return false;
	if (!loopback.Send(17, 23, bytes, error)) return false;
	loopback.Advance(2);
	if (!ExpectGAF(!loopback.Receive(23, delivered),
		"Loopback transport did not reproduce packet loss")) return false;

	for (std::size_t size = 0; size < 48 && size < bytes.size(); ++size)
	{
		std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin() + size);
		Vans::VansActionNetworkPacket ignored;
		if (!ExpectGAF(!Vans::VansActionNetworkCodec::Decode(truncated, ignored),
			"Gameplay Action network decoder accepted a truncated packet")) return false;
	}
	std::vector<std::uint8_t> corrupted = bytes;
	corrupted.back() ^= 0x5au;
	Vans::VansActionNetworkPacket ignored;
	if (!ExpectGAF(Vans::VansActionNetworkCodec::Decode(corrupted, ignored).error ==
		Vans::VansActionNetworkError::HashMismatch,
		"Gameplay Action network decoder accepted corrupted payload bytes")) return false;
	std::uint32_t fuzzState = 0x51f15e77u;
	for (std::size_t sample = 0; sample < 512; ++sample)
	{
		fuzzState = fuzzState * 1664525u + 1013904223u;
		std::vector<std::uint8_t> fuzzBytes(fuzzState % 257u);
		for (std::uint8_t& byte : fuzzBytes)
		{
			fuzzState = fuzzState * 1664525u + 1013904223u;
			byte = static_cast<std::uint8_t>(fuzzState >> 24u);
		}
		Vans::VansActionNetworkPacket fuzzPacket;
		const Vans::VansActionNetworkResult fuzzDecoded =
			Vans::VansActionNetworkCodec::Decode(fuzzBytes, fuzzPacket);
		if (fuzzDecoded)
		{
			std::vector<std::uint8_t> canonical;
			if (!Vans::VansActionNetworkCodec::Encode(fuzzPacket, canonical))
				return ExpectGAF(false,
					"Gameplay Action network fuzz decode produced a non-encodable packet");
		}
	}
	for (std::uint32_t iteration = 0; iteration < 1000; ++iteration)
	{
		packet.header.sequence = iteration + 100;
		packet.payload = Vans::VansEncodeActionActivationMessage(activation);
		std::vector<std::uint8_t> repeated;
		Vans::VansActionNetworkPacket repeatedPacket;
		if (!Vans::VansActionNetworkCodec::Encode(packet, repeated) ||
			!Vans::VansActionNetworkCodec::Decode(repeated, repeatedPacket))
			return ExpectGAF(false,
				"Gameplay Action network repeated codec stability contract failed");
	}
	Vans::VansActionNetworkLimits strictLimits;
	strictLimits.maximumDepth = 1;
	packet.payload = Vans::VansSerializedValue::Array({
		Vans::VansSerializedValue::Array({
			Vans::VansSerializedValue::Array({ Vans::VansSerializedValue::Int(1) }) }) });
	return ExpectGAF(Vans::VansActionNetworkCodec::Encode(packet, delivered, strictLimits).error ==
		Vans::VansActionNetworkError::BudgetExceeded,
		"Gameplay Action network encoder ignored recursive payload budgets");
}

bool TestGAFDebugAndReplayContract()
{
	using namespace Vans;
	const VansEntityHandle owner{ 17, 3 };
	const VansActionHandle actionHandle{ { 5, 2 } };
	const VansActionId actionId = VansMakeStableId<VansActionIdTag>("Gameplay.Debug.Contract");
	const VansAttributeId health = VansMakeStableId<VansAttributeIdTag>("Attribute.Health");
	VansActionInstanceSnapshot previousAction;
	previousAction.handle = actionHandle;
	previousAction.action = actionId;
	previousAction.sourceSpec = { { 2, 1 } };
	previousAction.state = VansActionInstanceState::Running;
	previousAction.context.owner = owner;
	previousAction.context.randomSeed = 7;
	previousAction.context.payload = VansSerializedValue::Object({
		{ "mode", VansSerializedValue::String("debug") }
	});
	previousAction.hasTargetData = true;
	previousAction.targetData.values.push_back(VansTargetLocation{ { 4.0, 5.0, 6.0 } });
	previousAction.targetData.values.push_back(
		VansTargetRay{ { 1.0, 2.0, 3.0 }, { 0.0, 0.0, 1.0 }, 250.0 });
	previousAction.variables.push_back({ VansMakeStableId<VansActionFieldIdTag>("Damage"),
		VansSerializedValue::Float(25.0) });
	previousAction.tasks.push_back({ { { 1, 1 } },
		VansMakeStableId<VansActionGraphNodeTypeIdTag>("Action.Graph.Wait"),
		"WaitForMarker", VansActionTaskState::Waiting, 0.1, 1.0 });
	previousAction.taskCount = previousAction.tasks.size();
	previousAction.resources.push_back({ { { 3, 1 } }, "Cue", "ChargeLoop", {},
		VansActionPredictionResourcePolicy::UndoRedo, false });
	previousAction.resourceCount = previousAction.resources.size();
	previousAction.executor.executor = "ExecutionGraph";
	previousAction.executor.activeNodes = { "Acquire" };

	VansActionHostDebugSnapshot previousHost;
	previousHost.owner = owner;
	previousHost.enabled = true;
	previousHost.tags.push_back({ VansMakeStableId<VansGameplayTagIdTag>("State.Ready"), 1 });
	previousHost.attributes.push_back({ health, 100.0, 100.0 });
	previousHost.effects.push_back({ { { 4, 1 } },
		VansMakeStableId<VansEffectIdTag>("Effect.Debug"), 9, 2.0, 0.5, 2, { 1, 3 } });
	VansGrantedActionSpecSnapshot grant;
	grant.handle = previousAction.sourceSpec;
	grant.action = actionId;
	grant.definitionVersion = 4;
	grant.level = 2.0;
	grant.inputBinding = "Primary";
	grant.dynamicTags.push_back(VansMakeStableId<VansGameplayTagIdTag>("Grant.Debug"));
	grant.charges = 3;
	grant.source = 91;
	grant.persistence = VansActionGrantPersistence::Persistent;
	previousHost.grants.push_back(grant);
	previousHost.actions.push_back(previousAction);
	VansGameplayDebugSnapshot previous;
	previous.frame = 10;
	previous.timeSeconds = 1.0;
	previous.contentManifestHash = 0x1234;
	previous.hosts.push_back(previousHost);

	VansGameplayDebugSnapshot current = previous;
	current.frame = 11;
	current.timeSeconds = 1.016;
	auto& currentHost = current.hosts.front();
	currentHost.attributes.front().currentValue = 75.0;
	auto& currentAction = currentHost.actions.front();
	currentAction.state = VansActionInstanceState::Waiting;
	currentAction.error = VansActionError::ExecutionFailed;
	currentAction.prediction = { 8, 12 };
	currentAction.executor.activeNodes = { "ResolveHit" };
	currentAction.recentEvents.push_back({ 1,
		VansMakeStableId<VansActionFieldIdTag>("Gameplay.Hit"), "Gameplay.Hit" });
	currentAction.recentEvents.push_back({ 2,
		VansMakeStableId<VansActionFieldIdTag>("Action.Window.Melee.Open"),
		"Action.Window.Melee.Open" });

	VansGameplayActionBreakpointSet breakpoints;
	auto add = [&](VansActionBreakpoint breakpoint) { breakpoints.Add(std::move(breakpoint)); };
	VansActionBreakpoint state;
	state.kind = VansActionBreakpointKind::State;
	state.state = VansActionInstanceState::Waiting;
	add(state);
	VansActionBreakpoint node;
	node.kind = VansActionBreakpointKind::Node;
	node.node = "ResolveHit";
	add(node);
	VansActionBreakpoint event;
	event.kind = VansActionBreakpointKind::Event;
	event.event = "Gameplay.Hit";
	add(event);
	VansActionBreakpoint window;
	window.kind = VansActionBreakpointKind::Window;
	window.window = "Melee";
	add(window);
	VansActionBreakpoint errorBreakpoint;
	errorBreakpoint.kind = VansActionBreakpointKind::Error;
	errorBreakpoint.error = VansActionError::ExecutionFailed;
	add(errorBreakpoint);
	VansActionBreakpoint prediction;
	prediction.kind = VansActionBreakpointKind::Prediction;
	prediction.prediction = { 8, 12 };
	add(prediction);
	VansActionBreakpoint attribute;
	attribute.kind = VansActionBreakpointKind::Attribute;
	attribute.attribute = health;
	attribute.comparison = VansActionBreakpointComparison::Less;
	attribute.value = 90.0;
	add(attribute);
	const auto hits = breakpoints.Evaluate(previous, current);
	if (!ExpectGAF(hits.size() == 7,
		"GAF debugger did not edge-trigger state/node/event/window/error/prediction/attribute breakpoints"))
		return false;
	if (!ExpectGAF(breakpoints.Evaluate(current, current).empty(),
		"GAF debugger repeated edge-triggered breakpoints without a state change")) return false;

	VansGameplayTraceRecorder recorder;
	std::string errorText;
	if (!recorder.Begin(current.contentManifestHash, 4, 1024 * 1024, errorText) ||
		!recorder.Record(previous, errorText) || !recorder.Record(current, errorText))
		return ExpectGAF(false, errorText.c_str());
	VansGameplayTraceArchive archive = recorder.End();
	const std::filesystem::path tracePath =
		std::filesystem::temp_directory_path() / "ForestGAFDebugContract.gaftrace";
	std::filesystem::remove(tracePath);
	if (!VansGameplayTraceRecorder::Save(tracePath, archive, errorText))
		return ExpectGAF(false, errorText.c_str());
	VansGameplayTraceArchive loaded;
	if (!VansGameplayTraceRecorder::Load(tracePath, loaded, errorText))
		return ExpectGAF(false, errorText.c_str());
	std::filesystem::remove(tracePath);
	if (!ExpectGAF(loaded.frames.size() == 2 && loaded.frames.back().hosts.size() == 1 &&
		loaded.frames.back().hosts.front().tags.size() == 1 &&
		loaded.frames.back().hosts.front().attributes.front().currentValue == 75.0 &&
		loaded.frames.back().hosts.front().effects.size() == 1 &&
		loaded.frames.back().hosts.front().grants.size() == 1 &&
		loaded.frames.back().hosts.front().actions.front().tasks.size() == 1 &&
		loaded.frames.back().hosts.front().actions.front().resources.size() == 1 &&
		loaded.frames.back().hosts.front().actions.front().recentEvents.size() == 2 &&
		loaded.frames.back().hosts.front().actions.front().hasTargetData &&
		loaded.frames.back().hosts.front().actions.front().targetData.values.size() == 2,
		"GAF trace round-trip discarded debugger runtime state")) return false;
	const auto& loadedTargets =
		loaded.frames.back().hosts.front().actions.front().targetData.values;
	if (!ExpectGAF(std::holds_alternative<VansTargetLocation>(loadedTargets[0]) &&
		std::get<VansTargetLocation>(loadedTargets[0]).value[2] == 6.0 &&
		std::holds_alternative<VansTargetRay>(loadedTargets[1]) &&
		std::get<VansTargetRay>(loadedTargets[1]).length == 250.0,
		"GAF trace did not preserve TargetData values")) return false;
	VansGameplayReplaySession replay;
	if (!replay.Load(std::move(loaded), errorText) || !replay.Step(1) ||
		!replay.Current() || replay.Current()->frame != 11 || !replay.Step(-1) ||
		replay.Current()->frame != 10)
		return ExpectGAF(false, "GAF replay session seek/step contract failed");
	return true;
}

bool TestGAFAssetSchemaAndCookContract()
{
	struct AssetCase
	{
		const char* extension;
		Vans::VansAssetType type;
		const char* importer;
	};
	const AssetCase cases[] = {
		{ ".vaction", Vans::VansAssetType::ActionDefinition, "GameplayActionImporter" },
		{ ".vactionset", Vans::VansAssetType::ActionSet, "GameplayActionSetImporter" },
		{ ".veffect", Vans::VansAssetType::GameplayEffect, "GameplayEffectImporter" },
		{ ".vcue", Vans::VansAssetType::GameplayCue, "GameplayCueImporter" },
		{ ".vattributeset", Vans::VansAssetType::AttributeSet, "GameplayAttributeSetImporter" },
		{ ".vtargeting", Vans::VansAssetType::TargetingPolicy, "GameplayTargetingImporter" },
		{ ".vtagtree", Vans::VansAssetType::GameplayTagTree, "GameplayTagTreeImporter" },
		{ ".vpayloadschema", Vans::VansAssetType::PayloadSchema, "GameplayPayloadSchemaImporter" },
		{ ".vactiongraph", Vans::VansAssetType::ActionGraph, "GameplayActionGraphImporter" },
		{ ".vcamerarig", Vans::VansAssetType::CameraRigProfile, "CameraRigProfileImporter" },
		{ ".vcamerashake", Vans::VansAssetType::CameraShakeProfile, "CameraShakeProfileImporter" },
		{ ".gafeditorlayout", Vans::VansAssetType::GAFEditorLayout, "GAFEditorLayoutImporter" }
	};
	const Vans::VansGameplayAssetSchemaRegistry& schemas =
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns();
	if (!ExpectGAF(schemas.IsSealed(), "内置 GAF Schema Registry 未封存")) return false;
	for (const AssetCase& assetCase : cases)
	{
		const std::filesystem::path path(std::string("asset") + assetCase.extension);
		if (!ExpectGAF(Vans::VansAssetDatabase::Classify(path) == assetCase.type,
			"GAF 扩展名分类错误")) return false;
		if (!ExpectGAF(Vans::VansAssetDatabase::ImporterFor(assetCase.type) == assetCase.importer,
			"GAF importer 映射错误")) return false;
		if (!ExpectGAF(schemas.Resolve(assetCase.type) != nullptr &&
			Vans::VansAssetDocumentTypeRegistry::Get().Find(assetCase.type) != nullptr,
			"GAF Schema 或编辑器文档类型未注册")) return false;
	}

	const std::filesystem::path sourceRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	Vans::VansGAFProjectConfiguration configuration;
	std::string error;
	if (!Vans::VansGAFProjectConfiguration::Load(
		sourceRoot / "EngineAssets/GAF/ProjectSettings", configuration, error))
		return ExpectGAF(false, error.c_str());
	const auto actionTemplate = configuration.templates.find("ActionDefinition");
	if (!ExpectGAF(actionTemplate != configuration.templates.end() &&
		configuration.templates.size() == 12 &&
		configuration.allowlist.nodeTypes.count("Action.Graph.Complete") == 1 &&
		configuration.allowlist.handlers.size() == 5 &&
		configuration.allowlist.handlers.count("Targeting.Filter.TagQuery") == 0 &&
		configuration.settings.performance.maximumGraphTransitionsPerTick == 1024,
		"GAF 项目配置没有完整加载")) return false;
	Vans::VansGameplayDiagnostics policyDiagnostics = {
		{ Vans::VansGameplayDiagnosticSeverity::Error, "GAF-FIELD-DEPRECATED",
			"deprecated contract field" },
		{ Vans::VansGameplayDiagnosticSeverity::Warning, "GAF-ACTION-TRANSITION",
			"transition contract warning" }
	};
	configuration.ApplyValidationPolicy(policyDiagnostics);
	Vans::VansGAFProjectConfiguration strictCookConfiguration = configuration;
	strictCookConfiguration.settings.treatCookWarningsAsErrors = true;
	if (!ExpectGAF(
		policyDiagnostics.front().severity == Vans::VansGameplayDiagnosticSeverity::Warning &&
		!configuration.HasBlockingDiagnostics(
			policyDiagnostics, Vans::VansGAFValidationStage::Save) &&
		configuration.HasBlockingDiagnostics(
			policyDiagnostics, Vans::VansGAFValidationStage::CI) &&
		strictCookConfiguration.HasBlockingDiagnostics(
			policyDiagnostics, Vans::VansGAFValidationStage::Cook),
		"GAF validation severity and stage blocking policy is not enforced")) return false;
	const std::filesystem::path templateProject =
		std::filesystem::temp_directory_path() / "ForestGAFTemplateDirectoryContract";
	std::error_code templateCleanupError;
	std::filesystem::remove_all(templateProject, templateCleanupError);
	struct TemplateCleanup
	{
		std::filesystem::path path;
		~TemplateCleanup()
		{
			std::error_code cleanupError;
			std::filesystem::remove_all(path, cleanupError);
		}
	} templateCleanup{ templateProject };
	if (!Vans::VansGAFProjectConfiguration::EnsureProjectFiles(
		templateProject / "ProjectSettings",
		sourceRoot / "EngineAssets/GAF/ProjectSettings", error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGAFProjectConfiguration externalTemplateConfiguration = configuration;
	externalTemplateConfiguration.settings.templateDirectory = "GAFTemplates";
	if (!Vans::VansGAFProjectConfiguration::Save(
		templateProject / "ProjectSettings", externalTemplateConfiguration, error))
		return ExpectGAF(false, error.c_str());
	std::filesystem::create_directories(templateProject / "GAFTemplates");
	Vans::VansSerializedValue externalActionTemplate = actionTemplate->second;
	if (!Vans::SetSerializedPointer(externalActionTemplate, "/displayName",
		Vans::VansSerializedValue::String("External Action Template"), &error) ||
		!Vans::VansGameplayAssetStorage::SaveSourceAtomic(
			templateProject / "GAFTemplates/Action.vaction",
			externalActionTemplate, error, &externalTemplateConfiguration))
		return ExpectGAF(false, error.c_str());
	Vans::VansGAFProjectConfiguration loadedExternalTemplates;
	if (!Vans::VansGAFProjectConfiguration::LoadForProject(templateProject, sourceRoot,
		loadedExternalTemplates, error)) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(Vans::ReadSerializedStringField(
		loadedExternalTemplates.templates.at("ActionDefinition"), "displayName") ==
		"External Action Template",
		"GAF templateDirectory did not override the project Action template")) return false;

	const std::filesystem::path cameraDirectory =
		std::filesystem::temp_directory_path() / "ForestGAFCameraContract";
	std::error_code cameraCleanupError;
	std::filesystem::remove_all(cameraDirectory, cameraCleanupError);
	std::filesystem::create_directories(cameraDirectory);
	struct CameraCleanup
	{
		std::filesystem::path path;
		~CameraCleanup()
		{
			std::error_code cleanupError;
			std::filesystem::remove_all(path, cleanupError);
		}
	} cameraCleanup{ cameraDirectory };
	const std::filesystem::path rigPath = cameraDirectory / "ContractRig.vcamerarig";
	const std::filesystem::path shakePath = cameraDirectory / "ContractShake.vcamerashake";
	Vans::VansSerializedValue rigSource = configuration.templates.at("CameraRigProfile");
	Vans::VansSerializedValue shakeSource = configuration.templates.at("CameraShakeProfile");
	Vans::SetSerializedPointer(rigSource, "/cameraRigId",
		Vans::VansSerializedValue::String("Camera.Rig.ContractAsset"), &error);
	Vans::SetSerializedPointer(shakeSource, "/cameraShakeId",
		Vans::VansSerializedValue::String("Camera.Shake.ContractAsset"), &error);
	if (!Vans::VansGameplayAssetStorage::SaveSourceAtomic(rigPath, rigSource, error) ||
		!Vans::VansGameplayAssetStorage::SaveSourceAtomic(shakePath, shakeSource, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansAssetDatabase cameraDatabase(
		cameraDirectory, cameraDirectory / "Library/Artifacts");
	if (!cameraDatabase.Scan(Vans::VansAssetOperationPolicy::Authoring()))
		return ExpectGAF(false, "Camera GAF contract asset scan failed");
	const auto rigRecord = cameraDatabase.Find(rigPath);
	const auto shakeRecord = cameraDatabase.Find(shakePath);
	Vans::VansGameplayAssetLibrary cameraAssets;
	if (!rigRecord || !shakeRecord || !cameraAssets.Load(cameraDatabase.All(), error) ||
		!ExpectGAF(cameraAssets.CameraRigs().size() == 1 &&
			cameraAssets.CameraShakes().size() == 1 &&
			cameraAssets.ResolveCameraRig(rigRecord->guid.ToString()) &&
			cameraAssets.ResolveCameraShake(shakeRecord->guid.ToString()),
			"Camera profiles did not reach the typed GAF asset library")) return false;
	Vans::VansCameraRuntime cameraRuntime;
	Vans::VansCameraViewSnapshot cameraBase;
	cameraBase.lens.fieldOfView = 45.0f;
	if (!cameraRuntime.SetBaseView(Vans::VansCameraRuntime::MainView(), cameraBase, error)) return false;
	auto cameraService = Vans::VansCameraActionService::Create(
		cameraRuntime, cameraAssets, error);
	Vans::VansActionServiceRegistry cameraServices;
	if (!cameraService || !cameraServices.Register(cameraService, error) ||
		!cameraServices.Seal(error)) return ExpectGAF(false, error.c_str());
	Vans::VansActionCommand cameraCommand;
	cameraCommand.service = cameraService->Capability().service;
	cameraCommand.command = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Camera.Shot");
	cameraCommand.stableName = "Camera.Shot";
	cameraCommand.payload = Vans::VansSerializedValue::Object({
		{ "rig", Vans::VansSerializedValue::String(rigRecord->guid.ToString()) }
	});
	const Vans::VansActionCommandResult shotResult = cameraServices.Execute(cameraCommand);
	if (!ExpectGAF(shotResult && shotResult.resource &&
		std::abs(cameraRuntime.ResolveView(Vans::VansCameraRuntime::MainView()).snapshot.lens.fieldOfView -
			60.0f) < 0.001f,
		"Camera GAF Shot did not resolve a cooked Rig profile")) return false;
	cameraCommand.command = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Camera.Shake");
	cameraCommand.stableName = "Camera.Shake";
	cameraCommand.payload = Vans::VansSerializedValue::Object({
		{ "shake", Vans::VansSerializedValue::String(shakeRecord->guid.ToString()) },
		{ "scale", Vans::VansSerializedValue::Float(1.0) }
	});
	const Vans::VansActionCommandResult shakeResult = cameraServices.Execute(cameraCommand);
	cameraRuntime.Advance(0.05);
	const Vans::VansResolvedCameraView shaken =
		cameraRuntime.ResolveView(Vans::VansCameraRuntime::MainView());
	if (!ExpectGAF(shakeResult && shakeResult.resource &&
		(glm::length(shaken.snapshot.pose.position) > 0.00001f ||
			glm::length(shaken.snapshot.pose.rotationDegrees) > 0.00001f),
		"Camera GAF Shake profile did not produce a deterministic sampled contribution")) return false;
	const std::size_t beforeImpulse = cameraRuntime.ContributionCount();
	cameraCommand.command = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Camera.Impulse");
	cameraCommand.stableName = "Camera.Impulse";
	cameraCommand.payload = Vans::VansSerializedValue::Object({
		{ "translation", Vans::VansSerializedValue::Object({
			{ "x", Vans::VansSerializedValue::Float(0.0) },
			{ "y", Vans::VansSerializedValue::Float(0.0) },
			{ "z", Vans::VansSerializedValue::Float(1.0) } }) }
	});
	if (!cameraServices.Execute(cameraCommand) ||
		cameraRuntime.ContributionCount() != beforeImpulse + 1) return false;
	cameraRuntime.ResolveAndConsumeView(Vans::VansCameraRuntime::MainView());
	if (!ExpectGAF(cameraRuntime.ContributionCount() == beforeImpulse,
		"Camera GAF Impulse was not consumed after one resolve")) return false;
	if (!cameraService->Release(shotResult.resource, error) ||
		!cameraService->Release(shakeResult.resource, error) ||
		cameraService->Release(shakeResult.resource, error))
		return ExpectGAF(false, "Camera GAF resource release or stale-handle rejection failed");

	Vans::VansSerializedValue action = actionTemplate->second;
	Vans::SetSerializedObjectField(action, "unknownFutureField",
		Vans::VansSerializedValue::String("preserve-me"));
	Vans::SetSerializedObjectField(action, "editorMetadata",
		Vans::VansSerializedValue::Object({ { "folded", Vans::VansSerializedValue::Bool(true) } }));
	if (!Vans::SetSerializedPointer(action, "/commit/cooldowns",
		Vans::VansSerializedValue::Array({
			Vans::VansSerializedValue::Object({
				{ "duration", Vans::VansSerializedValue::Float(0.5) },
				{ "tag", Vans::VansSerializedValue::String("Cooldown.Primary") }
			}),
			Vans::VansSerializedValue::Object({
				{ "duration", Vans::VansSerializedValue::Float(1.0) },
				{ "tag", Vans::VansSerializedValue::String("Cooldown.Shared") }
			})
		}), &error)) return ExpectGAF(false, error.c_str());
	const auto diagnostics = Vans::VansAssetDocumentTypeRegistry::Get().ValidateBeforeSave(
		Vans::VansAssetType::ActionDefinition, "probe.vaction", action);
	for (const auto& diagnostic : diagnostics)
		if (diagnostic.severity == Vans::VansAssetDocumentDiagnosticSeverity::Error)
			return ExpectGAF(false, diagnostic.message.c_str());
	const Vans::VansGameplayCookResult first = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionDefinition, action);
	if (!ExpectGAF(first && first.asset.contentHash != 0 &&
		Vans::FindObjectField(first.asset.runtimeDocument, "unknownFutureField") != nullptr &&
		Vans::FindObjectField(first.asset.runtimeDocument, "editorMetadata") == nullptr,
		"GAF Cook 未保留未知字段或未剥离编辑器字段")) return false;
	Vans::VansSerializedValue reordered = action;
	std::reverse(reordered.objectFields.begin(), reordered.objectFields.end());
	const Vans::VansGameplayCookResult second = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionDefinition, reordered);
	if (!ExpectGAF(second && second.asset.contentHash == first.asset.contentHash,
		"GAF Cook ContentHash 不具备确定性")) return false;
	const Vans::VansGameplayCookResult configuredCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionDefinition, action,
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &configuration);
	Vans::VansGAFProjectConfiguration changedCookPolicy = configuration;
	changedCookPolicy.allowlist.handlers.insert("Targeting.Custom.Contract");
	const Vans::VansGameplayCookResult changedPolicyCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionDefinition, action,
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &changedCookPolicy);
	Vans::VansGAFProjectConfiguration blockedGraphPolicy = configuration;
	blockedGraphPolicy.allowlist.nodeTypes.erase("Action.Graph.Complete");
	const Vans::VansGameplayCookResult blockedGraphCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionGraph, configuration.templates.at("ActionGraph"),
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &blockedGraphPolicy);
	Vans::VansSerializedValue cameraGraph = configuration.templates.at("ActionGraph");
	if (!Vans::SetSerializedPointer(cameraGraph, "/nodes/0/type",
		Vans::VansSerializedValue::String("Camera.PushShot"), &error)) return false;
	Vans::VansGAFProjectConfiguration blockedCameraBridge = configuration;
	blockedCameraBridge.allowlist.bridges.erase("Camera.Action");
	const Vans::VansGameplayCookResult blockedCameraCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionGraph, cameraGraph,
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &blockedCameraBridge);
	Vans::VansSerializedValue timelineAction = action;
	if (!Vans::SetSerializedPointer(timelineAction, "/execution/timeline",
		Vans::VansSerializedValue::Object({
			{ "assetGuid", Vans::VansSerializedValue::String("timeline-contract-guid") }
		}), &error)) return false;
	Vans::VansGAFProjectConfiguration blockedTimelineBridge = configuration;
	blockedTimelineBridge.allowlist.bridges.erase("Timeline.Action");
	const Vans::VansGameplayCookResult blockedTimelineCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionDefinition, timelineAction,
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &blockedTimelineBridge);
	Vans::VansSerializedValue predictedGraph = configuration.templates.at("ActionGraph");
	if (!Vans::SetSerializedPointer(predictedGraph, "/nodes/0/type",
		Vans::VansSerializedValue::String("Action.Graph.Command"), &error) ||
		!Vans::SetSerializedPointer(predictedGraph, "/nodes/0/kind",
			Vans::VansSerializedValue::String("Command"), &error) ||
		!Vans::SetSerializedPointer(predictedGraph, "/nodes/0/predictable",
			Vans::VansSerializedValue::Bool(true), &error)) return false;
	Vans::VansGAFProjectConfiguration rollbackPolicy = configuration;
	rollbackPolicy.settings.networkMode = Vans::VansGAFNetworkMode::Loopback;
	rollbackPolicy.settings.predictionEnabled = true;
	rollbackPolicy.settings.requireRollbackPlan = true;
	const Vans::VansGameplayCookResult blockedRollbackCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionGraph, predictedGraph,
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &rollbackPolicy);
	if (!Vans::SetSerializedPointer(predictedGraph, "/nodes/0/rollbackPlan",
		Vans::VansSerializedValue::String("Automatic"), &error)) return false;
	const Vans::VansGameplayCookResult allowedRollbackCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionGraph, predictedGraph,
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &rollbackPolicy);
	if (!ExpectGAF(configuredCook && changedPolicyCook &&
		configuredCook.asset.contentHash != changedPolicyCook.asset.contentHash &&
		!blockedGraphCook && std::any_of(blockedGraphCook.diagnostics.begin(),
			blockedGraphCook.diagnostics.end(), [](const auto& diagnostic)
			{ return diagnostic.code == "GAF-PROJECT-NODE-ALLOWLIST"; }) &&
		!blockedCameraCook && std::any_of(blockedCameraCook.diagnostics.begin(),
			blockedCameraCook.diagnostics.end(), [](const auto& diagnostic)
			{ return diagnostic.code == "GAF-PROJECT-BRIDGE-ALLOWLIST"; }) &&
		!blockedTimelineCook && std::any_of(blockedTimelineCook.diagnostics.begin(),
			blockedTimelineCook.diagnostics.end(), [](const auto& diagnostic)
			{ return diagnostic.code == "GAF-PROJECT-BRIDGE-ALLOWLIST"; }) &&
		!blockedRollbackCook && allowedRollbackCook &&
		std::any_of(blockedRollbackCook.diagnostics.begin(),
			blockedRollbackCook.diagnostics.end(), [](const auto& diagnostic)
			{ return diagnostic.code == "GAF-PROJECT-ROLLBACK-PLAN"; }),
		"GAF Cook did not fingerprint or enforce project policy")) return false;

	const std::filesystem::path tempDirectory =
		std::filesystem::temp_directory_path() / "ForestGAFAssetContract";
	std::filesystem::create_directories(tempDirectory);
	const std::filesystem::path cookedPath = tempDirectory / "probe.gafcooked";
	if (!Vans::VansGameplayAssetStorage::SaveCookedAtomic(cookedPath, first.asset, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGameplayCookedAsset loaded;
	if (!Vans::VansGameplayAssetStorage::LoadCooked(cookedPath, loaded, error))
		return ExpectGAF(false, error.c_str());
	const std::filesystem::path configuredCookedPath =
		tempDirectory / "configured-probe.gafcooked";
	Vans::VansGameplayCookedAsset configuredLoaded;
	if (!Vans::VansGameplayAssetStorage::SaveCookedAtomic(
		configuredCookedPath, configuredCook.asset, error) ||
		!Vans::VansGameplayAssetStorage::LoadCooked(
			configuredCookedPath, configuredLoaded, error) ||
		!ExpectGAF(configuredLoaded.contentHash == configuredCook.asset.contentHash &&
			configuredLoaded.cookPolicyFingerprint == configuredCook.asset.cookPolicyFingerprint,
			"Configured GAF cooked asset did not preserve its policy fingerprint"))
		return false;
	std::string cookedBytes;
	if (!Vans::VansFileStorage::ReadAllBytes(cookedPath, cookedBytes, error) ||
		!ExpectGAF(cookedBytes.size() > 28 && cookedBytes.front() != '{',
			"GAF cooked output is not a binary container")) return false;
	const std::filesystem::path corruptedPath = tempDirectory / "corrupted.gafcooked";
	cookedBytes.back() ^= 0x01;
	if (!Vans::VansFileStorage::WriteAtomicBytes(corruptedPath, cookedBytes, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGameplayCookedAsset corrupted;
	std::string corruptionError;
	if (!ExpectGAF(!Vans::VansGameplayAssetStorage::LoadCooked(
		corruptedPath, corrupted, corruptionError) && !corruptionError.empty(),
		"GAF cooked corruption was not rejected")) return false;
	std::filesystem::remove(corruptedPath);
	std::filesystem::remove(configuredCookedPath);
	std::filesystem::remove(cookedPath);
	std::filesystem::remove(tempDirectory);
	if (!ExpectGAF(loaded.assetType == Vans::VansAssetType::ActionDefinition &&
		loaded.contentHash == first.asset.contentHash,
		"GAF Cooked 资产往返错误")) return false;

	for (const AssetCase& assetCase : cases)
	{
		if (assetCase.type == Vans::VansAssetType::GAFEditorLayout) continue;
		const auto* schema = schemas.Resolve(assetCase.type);
		const auto source = schema ? configuration.templates.find(schema->assetKind) : configuration.templates.end();
		if (!ExpectGAF(schema && source != configuration.templates.end(),
			"GAF 运行时资产缺少新建模板")) return false;
		const Vans::VansGameplayCookResult cooked =
			Vans::VansGameplayAssetStorage::Cook(assetCase.type, source->second);
		if (!ExpectGAF(static_cast<bool>(cooked), "GAF 模板无法 Cook")) return false;
		const Vans::VansGameplayCompileResult compiled =
			Vans::VansGameplayAssetCompiler::Compile(cooked.asset);
		if (!ExpectGAF(static_cast<bool>(compiled) && compiled.asset.assetType == assetCase.type &&
			compiled.asset.contentHash == cooked.asset.contentHash,
			"GAF 模板无法编译为强类型运行时资产")) return false;
	}
	const auto compiledAction = Vans::VansGameplayAssetCompiler::Compile(first.asset);
	const auto* actionDefinition = std::get_if<std::shared_ptr<const Vans::VansCompiledActionDefinition>>(
		&compiledAction.asset.data);
	if (!ExpectGAF(compiledAction && actionDefinition && *actionDefinition &&
		(*actionDefinition)->id == Vans::VansMakeStableId<Vans::VansActionIdTag>("Gameplay.NewAction") &&
		(*actionDefinition)->cooldowns.size() == 2,
		"ActionDefinition 强类型编译结果错误")) return false;

	Vans::VansGameplayAssetMigrationRegistry migrations;
	if (!migrations.Register(Vans::VansAssetType::ActionDefinition, 1, "Contract v1 to v2",
		[](Vans::VansSerializedValue& document, std::string&)
		{
			Vans::SetSerializedObjectField(document, "migrated",
				Vans::VansSerializedValue::Bool(true));
			return true;
		}, error) || !migrations.Seal(error)) return ExpectGAF(false, error.c_str());
	Vans::VansSerializedValue migrationDocument = actionTemplate->second;
	std::vector<Vans::VansGameplayMigrationRecord> migrationReport;
	if (!migrations.Migrate(Vans::VansAssetType::ActionDefinition, 2,
		migrationDocument, migrationReport, error)) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(migrationReport.size() == 1 &&
		Vans::ReadSerializedIntField(migrationDocument, "schemaVersion") == 2 &&
		Vans::ReadSerializedBoolField(migrationDocument, "migrated"),
		"GAF 资产迁移没有连续推进版本")) return false;
	if (!migrations.Migrate(Vans::VansAssetType::ActionDefinition, 2,
		migrationDocument, migrationReport, error)) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(migrationReport.empty(), "GAF 资产迁移不满足幂等性")) return false;

	const std::filesystem::path editorDirectory =
		std::filesystem::temp_directory_path() / "ForestGAFEditorModelContract";
	const std::filesystem::path editorAsset = editorDirectory / "editor-probe.vaction";
	std::error_code editorCleanupError;
	std::filesystem::remove_all(editorDirectory, editorCleanupError);
	Vans::VansAssetDocumentRegistry::Get().Clear();
	Vans::VansGameplayAssetEditorModel editor;
	if (!editor.CreateFromTemplate(Vans::VansAssetType::ActionDefinition,
		editorAsset, configuration, error)) return ExpectGAF(false, error.c_str());
	const Vans::VansSerializedValue editorBaseline = editor.Snapshot();
	if (!ExpectGAF(editor.IsOpen() && !editor.Fields().empty() &&
		!editor.Document()->sourceDocument.IsDirty(),
		"GAF 编辑器模型未打开共享资产文档")) return false;
	if (!editor.SetValue("/category", Vans::VansSerializedValue::String("Combat")) ||
		!editor.AppendArrayItem("/tags", Vans::VansSerializedValue::String("Action.Combat")) ||
		!editor.DuplicateArrayItem("/tags", 0))
		return ExpectGAF(false, "GAF 编辑器字段或数组命令执行失败");
	if (!ExpectGAF(editor.Document()->sourceDocument.IsDirty() &&
		Vans::FindSerializedPointer(editor.Snapshot(), "/tags")->arrayItems.size() == 2 &&
		!editor.DiffAgainst(editorBaseline).empty() && editor.PreviewCook(),
		"GAF 编辑器修改、Diff 或 Cook 预览错误")) return false;
	if (!editor.Undo() ||
		!ExpectGAF(Vans::FindSerializedPointer(editor.Snapshot(), "/tags")->arrayItems.size() == 1,
			"GAF 编辑器 Undo 未恢复数组命令") || !editor.Redo() ||
		!editor.RemoveArrayItem("/tags", 1) || !editor.ResetField("/category")) return false;
	if (!ExpectGAF(Vans::ReadSerializedStringField(editor.Snapshot(), "category") == "Gameplay",
		"GAF 编辑器字段默认值重置错误")) return false;
	const auto* actionSchema = schemas.Resolve(Vans::VansAssetType::ActionDefinition);
	const auto costsSchema = actionSchema ? std::find_if(actionSchema->fields.begin(),
		actionSchema->fields.end(), [](const Vans::VansGameplayPropertySchema& field)
		{
			return field.path == "/commit/costs";
		}) : std::vector<Vans::VansGameplayPropertySchema>::const_iterator{};
	if (!ExpectGAF(actionSchema && costsSchema != actionSchema->fields.end() &&
		costsSchema->children.size() == 6 && costsSchema->hasArrayElement,
		"GAF Action Cost 缺少结构化数组 Schema")) return false;
	const auto* effectSchema = schemas.Resolve(Vans::VansAssetType::GameplayEffect);
	const auto modifiersSchema = effectSchema ? std::find_if(effectSchema->fields.begin(),
		effectSchema->fields.end(), [](const Vans::VansGameplayPropertySchema& field)
		{
			return field.path == "/modifiers";
		}) : std::vector<Vans::VansGameplayPropertySchema>::const_iterator{};
	if (!ExpectGAF(effectSchema && modifiersSchema != effectSchema->fields.end() &&
		modifiersSchema->children.size() == 15 && modifiersSchema->hasArrayElement,
		"GAF Effect modifier schema does not expose magnitude sources and capture settings"))
		return false;
	if (!editor.AppendArrayItem("/commit/costs", costsSchema->arrayElementDefault) ||
		!editor.SetValue("/commit/costs/0/attribute",
			Vans::VansSerializedValue::String("Resource.Mana")) ||
		!editor.SetValue("/commit/costs/0/amount", Vans::VansSerializedValue::Float(10.0)) ||
		!editor.SetValue("/commit/costs/0/refund", Vans::VansSerializedValue::String("Always")) ||
		!editor.ResetField("/commit/costs/0/refund"))
		return ExpectGAF(false, "GAF 递归字段编辑或默认值重置失败");
	if (!ExpectGAF(Vans::ReadSerializedStringField(
		*Vans::FindSerializedPointer(editor.Snapshot(), "/commit/costs/0"), "refund") == "Never" &&
		!editor.SetValue("/commit/costs/0/refund", Vans::VansSerializedValue::String("Invalid")),
		"GAF 嵌套 Enum 校验未阻断非法配置")) return false;
	editor.Close();
	Vans::VansAssetDocumentRegistry::Get().Clear();
	Vans::VansGAFProjectConfiguration customRoots = configuration;
	customRoots.settings.defaultTagRoots = { "Ability", "Status" };
	const std::filesystem::path tagTreeAsset = editorDirectory / "tag-roots.vtagtree";
	Vans::VansGameplayAssetEditorModel tagTreeEditor;
	if (!tagTreeEditor.CreateFromTemplate(Vans::VansAssetType::GameplayTagTree,
		tagTreeAsset, customRoots, error)) return ExpectGAF(false, error.c_str());
	const Vans::VansSerializedValue tagTreeSnapshot = tagTreeEditor.Snapshot();
	const Vans::VansSerializedValue* generatedRoots =
		Vans::FindSerializedPointer(tagTreeSnapshot, "/tags");
	if (!ExpectGAF(generatedRoots && generatedRoots->arrayItems.size() == 2 &&
		Vans::ReadSerializedStringField(generatedRoots->arrayItems[0], "name") == "Ability" &&
		Vans::ReadSerializedStringField(generatedRoots->arrayItems[1], "name") == "Status",
		"GAF defaultTagRoots did not drive the GameplayTagTree template")) return false;
	tagTreeEditor.Close();
	Vans::VansAssetDocumentRegistry::Get().Clear();
	const auto graphNodeCatalog =
		Vans::EditorAPI::GameplayActionAuthoringBridge::GetGraphNodeCatalog();
	if (!ExpectGAF(graphNodeCatalog.size() == 26 &&
		std::all_of(graphNodeCatalog.begin(), graphNodeCatalog.end(),
			[](const auto& node) { return node.allowed && !node.pins.empty(); }),
		"GAF Graph editor node catalog is incomplete")) return false;
	const std::filesystem::path graphEditorAsset = editorDirectory / "graph-editor-probe.vactiongraph";
	if (!Vans::VansGameplayAssetStorage::SaveSourceAtomic(
		graphEditorAsset, configuration.templates.at("ActionGraph"), error)) return false;
	auto graphDocument = Vans::EditorAPI::GameplayActionAuthoringBridge::Open(
		graphEditorAsset.string());
	if (!ExpectGAF(graphDocument.success && graphDocument.graph.available &&
		graphDocument.graph.nodes.size() == 1,
		"GAF Graph editor bridge did not expose the graph snapshot")) return false;
	Vans::EditorAPI::GAFGraphEditRequest graphEdit;
	graphEdit.sourcePath = graphEditorAsset.string();
	graphEdit.operation = Vans::EditorAPI::GAFGraphEditOperation::AddNode;
	graphEdit.nodeGuid = "wait";
	graphEdit.nodeType = "Action.Graph.Wait";
	graphEdit.x = 220.0;
	graphEdit.y = 40.0;
	auto graphOperation = Vans::EditorAPI::GameplayActionAuthoringBridge::EditGraph(graphEdit);
	if (!ExpectGAF(graphOperation.success && graphOperation.document.graph.nodes.size() == 2,
		"GAF Graph editor could not add a typed node")) return false;
	graphEdit = {};
	graphEdit.sourcePath = graphEditorAsset.string();
	graphEdit.operation = Vans::EditorAPI::GAFGraphEditOperation::SetNodeProperty;
	graphEdit.nodeGuid = "wait";
	graphEdit.propertyName = "seconds";
	graphEdit.value.kind = Vans::EditorAPI::GAFEditorValueKind::Float;
	graphEdit.value.floatValue = 0.25;
	if (!Vans::EditorAPI::GameplayActionAuthoringBridge::EditGraph(graphEdit).success)
		return ExpectGAF(false, "GAF Graph editor could not edit a typed node property");
	graphEdit = {};
	graphEdit.sourcePath = graphEditorAsset.string();
	graphEdit.operation = Vans::EditorAPI::GAFGraphEditOperation::MoveNode;
	graphEdit.nodeGuid = "wait";
	graphEdit.x = 300.0;
	graphEdit.y = 120.0;
	if (!Vans::EditorAPI::GameplayActionAuthoringBridge::EditGraph(graphEdit).success) return false;
	graphEdit = {};
	graphEdit.sourcePath = graphEditorAsset.string();
	graphEdit.operation = Vans::EditorAPI::GAFGraphEditOperation::Connect;
	graphEdit.fromNode = "complete";
	graphEdit.outputPin = "Success";
	graphEdit.toNode = "wait";
	graphOperation = Vans::EditorAPI::GameplayActionAuthoringBridge::EditGraph(graphEdit);
	if (!ExpectGAF(graphOperation.success && graphOperation.document.graph.edges.size() == 1 &&
		std::abs(graphOperation.document.graph.nodes.back().x - 300.0) < 0.0001,
		"GAF Graph editor did not retain node position or create a typed connection")) return false;
	if (!ExpectGAF(!Vans::EditorAPI::GameplayActionAuthoringBridge::EditGraph(graphEdit).success,
		"GAF Graph editor accepted a duplicate connection")) return false;
	graphEdit = {};
	graphEdit.sourcePath = graphEditorAsset.string();
	graphEdit.operation = Vans::EditorAPI::GAFGraphEditOperation::RemoveNode;
	graphEdit.nodeGuid = "wait";
	graphOperation = Vans::EditorAPI::GameplayActionAuthoringBridge::EditGraph(graphEdit);
	if (!ExpectGAF(graphOperation.success && graphOperation.document.graph.nodes.size() == 1 &&
		graphOperation.document.graph.edges.empty(),
		"GAF Graph node removal did not atomically clean connected edges")) return false;
	graphOperation = Vans::EditorAPI::GameplayActionAuthoringBridge::Undo(
		graphEditorAsset.string());
	if (!ExpectGAF(graphOperation.success && graphOperation.document.graph.nodes.size() == 2 &&
		graphOperation.document.graph.edges.size() == 1,
		"GAF Graph atomic edit did not restore through shared Undo")) return false;
	Vans::VansAssetDocumentRegistry::Get().Clear();
	std::filesystem::remove(editorAsset);
	std::filesystem::remove(tagTreeAsset);
	std::filesystem::remove(graphEditorAsset);
	std::filesystem::remove(editorDirectory);
	const Vans::VansGameplayCookResult editorOnly = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::GAFEditorLayout,
		schemas.CreateDefault(Vans::VansAssetType::GAFEditorLayout));
	if (!ExpectGAF(!editorOnly, "编辑器布局被错误写入 runtime Cook")) return false;
	const Vans::VansGameplayCookResult graphCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionGraph, configuration.templates.at("ActionGraph"));
	const Vans::VansSerializedValue* cookedNodes = graphCook
		? Vans::FindSerializedPointer(graphCook.asset.runtimeDocument, "/nodes") : nullptr;
	if (!ExpectGAF(graphCook && cookedNodes && !cookedNodes->arrayItems.empty() &&
		Vans::FindObjectField(cookedNodes->arrayItems.front(), "editor") == nullptr,
		"Graph 节点编辑器布局进入了 runtime Cook")) return false;
	Vans::VansSerializedValue hostData = Vans::VansGameplayActionHostAuthoring::CreateDefaultData();
	if (!ExpectGAF(Vans::VansGameplayActionHostAuthoring::Validate(hostData).empty(),
		"ActionHost 默认场景配置无效")) return false;
	auto hostGrant = Vans::VansGameplayActionHostAuthoring::CreateDefaultArrayElement("grants");
	if (!ExpectGAF(hostGrant.has_value(), "ActionHost 缺少直接授予默认结构")) return false;
	Vans::FindObjectField(hostData, "grants")->arrayItems.push_back(*hostGrant);
	if (!ExpectGAF(!Vans::VansGameplayActionHostAuthoring::Validate(hostData).empty(),
		"ActionHost 未诊断空 Action 授予")) return false;
	Vans::SetSerializedPointer(hostData, "/grants/0/action",
		Vans::VansSerializedValue::String("Gameplay.Contract.Action"), &error);
	return ExpectGAF(Vans::VansGameplayActionHostAuthoring::Validate(hostData).empty(),
		"ActionHost 合法直接授予配置未通过校验");
}

bool TestGAFSampleLibraryContract()
{
	const std::filesystem::path sourceRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path sampleRoot = sourceRoot / "Samples/GAF";
	const std::filesystem::path projectRoot =
		std::filesystem::temp_directory_path() / "ForestGAFSampleLibraryContract";
	std::error_code filesystemError;
	std::filesystem::remove_all(projectRoot, filesystemError);
	struct Cleanup
	{
		std::filesystem::path path;
		~Cleanup()
		{
			std::error_code error;
			std::filesystem::remove_all(path, error);
		}
	} cleanup{ projectRoot };
	const std::filesystem::path assetsRoot = projectRoot / "Assets";
	std::filesystem::create_directories(projectRoot, filesystemError);
	std::filesystem::copy(sampleRoot, assetsRoot,
		std::filesystem::copy_options::recursive |
		std::filesystem::copy_options::overwrite_existing, filesystemError);
	if (!ExpectGAF(!filesystemError,
		"GAF sample library could not be copied into the temporary project")) return false;

	Vans::VansAssetDatabase database(assetsRoot, projectRoot / "Library/Artifacts");
	const Vans::VansAssetScanResult scan =
		database.Scan(Vans::VansAssetOperationPolicy::Authoring());
	if (!ExpectGAF(static_cast<bool>(scan),
		"GAF sample library Asset Database scan failed")) return false;

	Vans::VansGAFProjectConfiguration configuration;
	std::string error;
	if (!Vans::VansGAFProjectConfiguration::Load(
		sourceRoot / "EngineAssets/GAF/ProjectSettings", configuration, error))
		return ExpectGAF(false, error.c_str());
	const std::vector<std::string> packageRoots = {
		"00000000-0000-4000-8000-000000001001",
		"00000000-0000-4000-8000-000000001002",
		"00000000-0000-4000-8000-000000002005",
		"00000000-0000-4000-8000-000000003006",
		"00000000-0000-4000-8000-000000004007",
		"00000000-0000-4000-8000-000000005301"
	};
	const Vans::VansGameplayPackageCookResult package =
		Vans::VansGameplayAssetPackageCooker::CookClosure(
			projectRoot, database, nullptr, packageRoots, &configuration);
	if (!ExpectGAF(package && package.assets.size() == 37 &&
		package.requiredAssetGuids.size() == 37,
		"GAF sample package closure did not include all 37 linked assets"))
	{
		std::cerr << "[GAF sample package] cooked=" << package.assets.size()
			<< " required=" << package.requiredAssetGuids.size() << '\n';
		for (const std::string& packageError : package.errors)
			std::cerr << "[GAF sample package] " << packageError << '\n';
		return false;
	}
	Vans::VansSerializedValue unsafePredictedGraph;
	const std::filesystem::path unsafeGraphPath = assetsRoot / "Shooter/Fire.vactiongraph";
	if (!Vans::VansGameplayAssetStorage::LoadSource(
		unsafeGraphPath, unsafePredictedGraph, error) ||
		!Vans::SetSerializedPointer(unsafePredictedGraph, "/nodes/0/predictable",
			Vans::VansSerializedValue::Bool(true), &error)) return false;
	Vans::VansGameplayRuntime unsafeRuntime;
	Vans::VansGameplayRuntimeDependencies unsafeDependencies;
	unsafeDependencies.sourceOverrides.push_back({ unsafeGraphPath, unsafePredictedGraph });
	Vans::VansGAFSettings predictiveSettings = configuration.settings;
	predictiveSettings.networkMode = Vans::VansGAFNetworkMode::Loopback;
	predictiveSettings.predictionEnabled = true;
	predictiveSettings.requireRollbackPlan = true;
	std::string rollbackError;
	if (!ExpectGAF(!unsafeRuntime.Initialize(database.All(), predictiveSettings,
		unsafeDependencies, rollbackError) && rollbackError.find("rollback plan") != std::string::npos,
		"Runtime source override bypassed the prediction rollback policy")) return false;

	Vans::VansGameplayRuntime runtime;
	Vans::VansGameplayRuntimeDependencies dependencies;
	const auto fakeServices = Vans::VansCreateFakeStandardActionServices();
	for (const auto& service : fakeServices) dependencies.services.push_back(service);
	dependencies.graphNodeRegistrars.push_back(
		[](Vans::VansActionGraphNodeRegistry& registry, std::string& registrationError)
		{
			return Vans::VansRegisterCameraActionGraphNodes(registry, registrationError);
		});
	if (!runtime.Initialize(database.All(), configuration.settings, dependencies, error))
		return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(runtime.Assets().AssetCount() == 37 &&
		runtime.Assets().Actions().ActionCount() == 10 &&
		runtime.Assets().Cues().size() == 5 &&
		runtime.Assets().CameraRigs().size() == 2 &&
		runtime.Assets().CameraShakes().size() == 2,
		"GAF sample library typed asset counts are incomplete")) return false;

	const auto fire = runtime.Assets().ResolveAction("Gameplay.Sample.Shooter.Fire");
	const auto finisher = runtime.Assets().ResolveAction("Gameplay.Sample.Melee.Finisher");
	const auto lightAttack = runtime.Assets().ResolveAction("Gameplay.Sample.Melee.LightAttack");
	if (!ExpectGAF(fire && finisher && lightAttack &&
		fire->targetingPolicy == Vans::VansMakeStableId<Vans::VansTargetingPolicyIdTag>(
			"Targeting.Sample.PrimaryEntity") &&
		fire->presentationCues.size() == 1 &&
		fire->presentationCues.front() == Vans::VansMakeStableId<Vans::VansCueIdTag>(
			"Cue.Sample.Shooter.Fire") &&
		lightAttack->transitionRules.size() == 1 &&
		lightAttack->transitionRules.front().targetAction == finisher->id,
		"GAF sample GUID references did not link to runtime stable IDs")) return false;

	Vans::VansGameplayActionHostSetup setup;
	setup.actionSets = {
		"00000000-0000-4000-8000-000000002005",
		"00000000-0000-4000-8000-000000003006",
		"00000000-0000-4000-8000-000000004007",
		"00000000-0000-4000-8000-000000005301"
	};
	setup.initialTags.push_back({ "State.HasKey", 1 });
	setup.initialAttributes = {
		{ "Attribute.Ammo", 10.0 },
		{ "Attribute.Stamina", 20.0 },
		{ "Attribute.WeaponHeat", 0.0 }
	};
	const Vans::VansEntityHandle owner{ 401, 1 };
	const Vans::VansEntityHandle target{ 402, 1 };
	const auto host = runtime.CreateHost(owner, setup, error);
	if (!host) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(host->GrantedActions().size() == 10 &&
		std::abs(host->Attributes().Current(Vans::VansMakeStableId<Vans::VansAttributeIdTag>(
			"Attribute.Ammo")) - 30.0) < 0.0001 &&
		std::abs(host->Attributes().Current(Vans::VansMakeStableId<Vans::VansAttributeIdTag>(
			"Attribute.Stamina")) - 100.0) < 0.0001,
		"GAF sample ActionSets did not grant actions or apply Attribute overrides")) return false;

	Vans::VansActionContext context;
	context.owner = owner;
	context.instigator = owner;
	context.primaryTarget = target;
	context.randomSeed = 12345;
	const Vans::VansAttributeId requirementAmmo =
		Vans::VansMakeStableId<Vans::VansAttributeIdTag>("Attribute.Ammo");
	Vans::VansActionContext missingTargetContext = context;
	missingTargetContext.primaryTarget = {};
	if (!ExpectGAF(host->CanActivateAction(fire->id, missingTargetContext).error ==
		Vans::VansActionError::TargetInvalid,
		"GAF TargetData commit requirement accepted a missing target")) return false;
	if (!host->Attributes().AddBase(requirementAmmo, -30.0)) return false;
	if (!ExpectGAF(host->CanActivateAction(fire->id, context).error ==
		Vans::VansActionError::RequirementsFailed,
		"GAF Attribute commit requirement accepted an insufficient value")) return false;
	if (!host->Attributes().AddBase(requirementAmmo, 30.0)) return false;
	const auto activate = [&](std::string_view name)
	{
		const auto definition = runtime.Assets().ResolveAction(name);
		if (!definition) return false;
		const Vans::VansActionResult result = host->ActivateAction(definition->id, context);
		if (!result) std::cerr << "[GAF sample action] " << name << ": " << result.message << '\n';
		return static_cast<bool>(result);
	};
	for (const char* action : {
		"Gameplay.Sample.Shooter.Fire",
		"Gameplay.Sample.Melee.LightAttack",
		"Gameplay.Sample.Door.Open",
		"Gameplay.Sample.Camera.FocusShot",
		"Gameplay.Sample.Camera.Recoil",
		"Gameplay.Sample.Camera.HitReaction",
		"Gameplay.Sample.Camera.LockOn",
		"Gameplay.Sample.Camera.LensPulse" })
		if (!activate(action)) return false;
	for (int tick = 0; tick < 10; ++tick) runtime.TickEarly(0.1);
	if (!activate("Gameplay.Sample.Melee.Finisher")) return false;
	for (int tick = 0; tick < 4; ++tick) runtime.TickEarly(0.1);

	std::size_t executedCommands = 0;
	bool leakedResources = false;
	for (const auto& service : fakeServices)
	{
		executedCommands += service->ExecutedCommandCount();
		leakedResources = leakedResources || service->ActiveResourceCount() != 0;
	}
	const Vans::VansAttributeId ammo =
		Vans::VansMakeStableId<Vans::VansAttributeIdTag>("Attribute.Ammo");
	const Vans::VansAttributeId stamina =
		Vans::VansMakeStableId<Vans::VansAttributeIdTag>("Attribute.Stamina");
	const Vans::VansAttributeId heat =
		Vans::VansMakeStableId<Vans::VansAttributeIdTag>("Attribute.WeaponHeat");
	if (!ExpectGAF(host->ActiveActions().empty() && !leakedResources &&
		executedCommands >= 14 &&
		std::abs(host->Attributes().Current(ammo) - 29.0) < 0.0001 &&
		std::abs(host->Attributes().Current(stamina) - 75.0) < 0.0001 &&
		std::abs(host->Attributes().Current(heat) - 5.0) < 0.0001,
		"GAF sample actions did not execute, settle resources, or commit attributes"))
		return false;
	Vans::VansGameplayActionHostSetup revokeSetup;
	revokeSetup.initialAttributes = {
		{ "Attribute.Ammo", 10.0 },
		{ "Attribute.Stamina", 20.0 },
		{ "Attribute.WeaponHeat", 0.0 }
	};
	const auto revokeHost = runtime.CreateHost({ 403, 1 }, revokeSetup, error);
	const Vans::VansActionSetDefinition* shooterSet =
		runtime.Assets().ResolveActionSet("00000000-0000-4000-8000-000000002005");
	const Vans::VansActionSetHandle shooterSetHandle = revokeHost && shooterSet
		? revokeHost->ApplyActionSet(*shooterSet, error) : Vans::VansActionSetHandle{};
	if (!ExpectGAF(shooterSetHandle &&
		std::abs(revokeHost->Attributes().Current(ammo) - 30.0) < 0.0001 &&
		revokeHost->RevokeActionSet(shooterSetHandle, error) &&
		revokeHost->GrantedActions().empty() &&
		std::abs(revokeHost->Attributes().Current(ammo) - 10.0) < 0.0001,
		"GAF ActionSet Attribute override did not revoke by source")) return false;
	return true;
}

bool TestGAFDemoHallWindowBreakContract()
{
	namespace fs = std::filesystem;
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 6 && !fs::exists(workspace / "DemoHallProject"); ++depth)
		workspace = workspace.parent_path();
	const fs::path projectRoot = workspace / "DemoHallProject";
	const fs::path sourceAssets = projectRoot / "Assets/GAF/WindowBreak";
	if (!ExpectGAF(fs::is_directory(sourceAssets),
		"DemoHall window-break GAF assets are missing")) return false;

	Vans::VansGAFProjectConfiguration configuration;
	std::string error;
	if (!Vans::VansGAFProjectConfiguration::LoadForProject(
		projectRoot, workspace / "ForestEngine/ForestEngine", configuration, error))
		return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(configuration.settings.networkMode == Vans::VansGAFNetworkMode::Disabled,
		"DemoHall window-break interaction unexpectedly enables GAF networking")) return false;

	const fs::path temporaryRoot =
		fs::temp_directory_path() / "ForestGAFDemoHallWindowBreakContract";
	std::error_code filesystemError;
	fs::remove_all(temporaryRoot, filesystemError);
	struct Cleanup
	{
		fs::path path;
		~Cleanup()
		{
			std::error_code ignored;
			fs::remove_all(path, ignored);
		}
	} cleanup{ temporaryRoot };
	const fs::path assetsRoot = temporaryRoot / "Assets/WindowBreak";
	fs::create_directories(assetsRoot, filesystemError);
	fs::copy(sourceAssets, assetsRoot,
		fs::copy_options::recursive | fs::copy_options::overwrite_existing, filesystemError);
	if (!ExpectGAF(!filesystemError,
		"DemoHall window-break assets could not be copied for validation")) return false;

	Vans::VansAssetDatabase database(temporaryRoot / "Assets", temporaryRoot / "Library/Artifacts");
	const Vans::VansAssetScanResult scan =
		database.Scan(Vans::VansAssetOperationPolicy::Authoring());
	if (!ExpectGAF(scan && database.All().size() == 6,
		"DemoHall window-break assets did not scan as six GAF assets")) return false;
	for (const Vans::VansAssetRecord& record : database.All())
	{
		Vans::VansSerializedValue source;
		const fs::path sourcePath = fs::is_regular_file(record.authoringPath)
			? record.authoringPath : record.sourcePath;
		if (!Vans::VansGameplayAssetStorage::LoadSource(sourcePath, source, error))
			return ExpectGAF(false, error.c_str());
		const Vans::VansGameplayCookResult cooked = Vans::VansGameplayAssetStorage::Cook(
			record.type, source, Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &configuration);
		if (!ExpectGAF(static_cast<bool>(cooked),
			"DemoHall window-break asset failed configured GAF Cook")) return false;
	}

	Vans::VansGameplayRuntime runtime;
	if (!runtime.Initialize(database.All(), configuration.settings, error))
		return ExpectGAF(false, error.c_str());
	const auto action = runtime.Assets().ResolveAction("Gameplay.DemoHall.Window.Break");
	const auto actionSet = runtime.Assets().ResolveActionSet(
		"4d408a1b-97bc-4453-b3b0-c8e1426b7e1b");
	if (!ExpectGAF(action && actionSet && action->executionGraph &&
		action->timelineAssets.size() == 1 &&
		action->timelineAssets.front() == "8d2df4b5-3c7e-4c69-9f76-9b6e63fac850",
		"DemoHall window-break Action links are incomplete")) return false;

	const Vans::VansEntityHandle owner{ 701, 1 };
	const Vans::VansEntityHandle player{ 702, 1 };
	Vans::VansGameplayActionHostSetup setup;
	setup.actionSets.push_back("4d408a1b-97bc-4453-b3b0-c8e1426b7e1b");
	setup.initialTags.push_back({ "Target.Interactable.Window", 1 });
	const auto host = runtime.CreateHost(owner, setup, error);
	if (!ExpectGAF(host && host->GrantedActions().size() == 1,
		"DemoHall window ActionHost did not receive its ActionSet")) return false;

	Vans::VansActionContext context;
	context.owner = owner;
	context.instigator = player;
	context.source = player;
	context.primaryTarget = owner;
	const Vans::VansActionResult first = host->ActivateAction(action->id, context);
	const Vans::VansGameplayTagId broken =
		Vans::VansMakeStableId<Vans::VansGameplayTagIdTag>("State.DemoHall.Window.Broken");
	if (!ExpectGAF(first && host->Tags().Has(broken),
		"DemoHall window-break Action did not commit its persistent Broken state")) return false;
	runtime.TickEarly(3.1);
	const Vans::VansActionResult second = host->ActivateAction(action->id, context);
	if (!ExpectGAF(host->ActiveActions().empty() && !second,
		"DemoHall window-break Action did not settle or reject a second activation")) return false;

	nlohmann::ordered_json scene;
	if (!Vans::VansJsonFileStorage::Read(projectRoot / "Scenes/DemoHall.json", scene, error))
		return ExpectGAF(false, error.c_str());
	bool foundHost = false;
	bool foundScriptBinding = false;
	for (const auto& entity : scene.value("entities", nlohmann::ordered_json::array()))
	{
		if (entity.value("id", std::string{}) != "f6bb9edd-c1e1-56f0-8079-d7442b568b46")
			continue;
		for (const auto& component : entity.value("components", nlohmann::ordered_json::array()))
		{
			const auto data = component.value("data", nlohmann::ordered_json::object());
			if (component.value("type", std::string{}) == "ActionHost")
			{
				const auto sets = data.value("actionSets", nlohmann::ordered_json::array());
				foundHost = !sets.empty() && sets.front().value("guid", std::string{}) ==
					"4d408a1b-97bc-4453-b3b0-c8e1426b7e1b";
			}
			if (component.value("type", std::string{}) == "Script")
			{
				const auto fields = data.value("fields", nlohmann::ordered_json::object());
				foundScriptBinding = fields.value("breakActionId", std::string{}) ==
					"Gameplay.DemoHall.Window.Break";
			}
		}
	}
	std::string script;
	if (!Vans::VansFileStorage::ReadAllBytes(
		projectRoot / "Scripts/forest_lua_behaviors.lua", script, error))
		return ExpectGAF(false, error.c_str());
	return ExpectGAF(foundHost && foundScriptBinding &&
		script.find("vans.action.try_activate") != std::string::npos &&
		script.find("play_break_presentation") != std::string::npos &&
		script.find("GlassBreakInteractable:update_interaction_session") != std::string::npos &&
		script.find("timeline.state(timelineGuid)") != std::string::npos &&
		script.find("timelineStallSeconds >= 0.35") != std::string::npos,
		"DemoHall scene or Lua Script.Action bridge is not wired to the window ActionHost");
}

bool TestGAFLuaBridgeContract()
{
	lua_State* state = luaL_newstate();
	if (!ExpectGAF(state != nullptr, "GAF Lua contract could not create a Lua state")) return false;
	struct CloseLua
	{
		lua_State* state = nullptr;
		~CloseLua()
		{
			if (!state) return;
			VansRuntime::VansLuaGameplayActionBridge::Shutdown(state);
			lua_close(state);
		}
	} close{ state };
	luaL_openlibs(state);
	lua_newtable(state);
	VansRuntime::VansLuaGameplayActionBridge::Register(state);
	lua_setglobal(state, "vans");
	static constexpr const char* Contract = R"(
		assert(type(vans.action.give_action) == "function")
		assert(type(vans.action.revoke_action) == "function")
		assert(type(vans.action.apply_action_set) == "function")
		assert(type(vans.action.can_activate) == "function")
		assert(type(vans.action.try_activate) == "function")
		assert(type(vans.action.request_cancel) == "function")
		assert(type(vans.action.interrupt) == "function")
		assert(type(vans.action.query_actions) == "function")
		assert(type(vans.action.inspect_action) == "function")
		assert(type(vans.action.subscribe_action_event) == "function")
		assert(type(vans.action.unsubscribe_action_event) == "function")
		local entity = vans.target.entity("entity-guid")
		local location = vans.target.location(1, 2, 3)
		local ray = vans.target.ray(1, 2, 3, 0, 0, 1, 250)
		local set = vans.target.set({entity, location, ray})
		assert(entity.kind == "Entity" and entity.guid == "entity-guid")
		assert(location.kind == "Location" and location.x == 1 and location.z == 3)
		assert(ray.kind == "Ray" and ray.oz == 3 and ray.dz == 1 and ray.length == 250)
		assert(set.kind == "Set" and #set.targets == 3)
		return true
	)";
	if (luaL_loadstring(state, Contract) != LUA_OK || lua_pcall(state, 0, 1, 0) != LUA_OK)
	{
		const char* message = lua_tostring(state, -1);
		return ExpectGAF(false, message ? message : "GAF Lua contract failed");
	}
	return ExpectGAF(lua_toboolean(state, -1) != 0,
		"GAF Lua API or TargetData builders are incomplete");
}
