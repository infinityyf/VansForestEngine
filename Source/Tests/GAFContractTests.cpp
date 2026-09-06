#include "GAFContractTests.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <fstream>
#include "../EngineCore/SceneRuntime/Transform/VansTransformGraph.h"
#include "../EngineCore/AnimationCore/Runtime/VansSkeletonAnchorRegistry.h"
#include "../EngineCore/SceneCore/VansScenePhysicsComponentReader.h"

#include "../EngineCore/GameplayActionCore/VansActionDefinition.h"
#include "../EngineCore/GameplayActionCore/VansActionHost.h"
#include "../EngineCore/GameplayActionCore/VansActionRoutingService.h"
#include "../EngineCore/GameplayActionCore/VansActionScheduler.h"
#include "../EngineCore/GameplayActionCore/VansGameplayRuntime.h"
#include "../EngineCore/GameplayActionCore/VansGameplayModuleContributor.h"
#include "../EngineCore/GameplayActionCore/VansActionResourceLedger.h"
#include "../EngineCore/GameplayActionCore/VansActionServices.h"
#include "../EngineCore/GameplayActionCore/VansActionSystem.h"
#include "../EngineCore/GameplayActionDebug/VansGameplayActionDebug.h"
#include "../EngineCore/GameplayActionAdapters/VansActionServiceAdapter.h"
#include "../EngineCore/GameplayActionAdapters/VansGameplayPrimitivesContributor.h"
#include "../EngineCore/GameplayActionAdapters/Audio/VansAudioActionCapability.h"
#include "../EngineCore/GameplayActionAdapters/Camera/VansCameraActionService.h"
#include "../EngineCore/GameplayActionAdapters/Camera/VansCameraGameplayAssetCompiler.h"
#include "../EngineCore/GameplayActionAdapters/Character/VansCharacterActionServices.h"
#include "../EngineCore/GameplayActionAdapters/Character/VansAnimationEventActionService.h"
#include "../EngineCore/GameplayActionAdapters/Projectile/VansProjectileActionService.h"
#include "../EngineCore/GameplayActionAdapters/Combat/VansCombatActionService.h"
#include "../EngineCore/GameplayActionAdapters/Physics/VansPhysicsQueryActionCapability.h"
#include "../EngineCore/GameplayActionAdapters/Projectile/VansProjectileActionCapability.h"
#include "../EngineCore/GameplayActionAdapters/UI/VansUIActionCapability.h"
#include "../EngineCore/GameplayActionAdapters/VFX/VansVFXActionCapability.h"
#include "../EngineCore/GameplayActionExecution/VansActionTask.h"
#include "../EngineCore/GameplayActionExecution/VansActionExecutionGraph.h"
#include "../EngineCore/GameplayActionTimeline/VansGameplayActionTimelineIntegration.h"
#include "../EngineCore/GameplayAttributes/VansGameplayAttributes.h"
#include "../EngineCore/GameplayCues/VansGameplayCues.h"
#include "../EngineCore/GameplayEffects/VansGameplayEffects.h"
#include "../EngineCore/GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayAssetCompiler.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayAssetLibrary.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayAssetSchema.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayAssetStorage.h"
#include "../EngineCore/GameplayActionSchema/VansGameplayActionHostAuthoring.h"
#include "../EngineCore/GameplayTags/VansGameplayTags.h"
#include "../EngineCore/GameplayTargeting/VansGameplayTargeting.h"
#include "../EngineCore/PackagingCore/VansGameplayAssetPackageCooker.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/AssetCore/Storage/VansFileStorage.h"
#include "../EngineCore/AssetCore/Storage/VansAssetMetaStorage.h"
#include "../EngineCore/AssetCore/Storage/VansJsonFileStorage.h"
#include "../EngineCore/AssetCore/VansSkeletalMeshImportSettings.h"
#include "../EngineCore/AssetCore/VansAssetObjectRepository.h"
#include "../EngineCore/AICore/VansAIBehaviorAsset.h"
#include "../EngineCore/AnimationCore/VansAnimationClip.h"
#include "../EngineCore/AnimationCore/VansAnimationNode.h"
#include "../EngineCore/AnimationCore/VansAnimationSampler.h"
#include "../EngineCore/AnimationCore/VansAnimatorIO.h"
#include "../EngineCore/AnimationCore/VansAnimatorRuntimeCompiler.h"
#include "../EngineCore/AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../EngineCore/AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../EngineCore/AnimationCore/VansPoseMath.h"
#include "../EngineCore/AnimationCore/Retargeting/VansRetargetProcessor.h"
#include "../EngineCore/AnimationCore/Storage/VansRetargetProfileStorage.h"
#include "../EngineCore/AnimationCore/VansSkinnedMeshLoader.h"
#include "../EngineCore/AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../EngineCore/EditorCore/VansAssetDocumentTypeRegistry.h"
#include "../EngineCore/EditorCore/GameplayAction/VansGameplayAssetEditorModel.h"
#include "../EngineCore/EditorCore/GameplayAction/VansGameplayEditorContributor.h"
#include "../EngineCore/EngineAPILayer/Private/GameplayActionAuthoringBridge.h"
#include "../EngineCore/EventCore/VansEventBus.h"
#include "../EngineCore/RenderCore/VansAnimationPreviewRenderer.h"
#include "../EngineCore/NavigationCore/VansNavigationMesh.h"
#include "../EngineCore/PhysicsCore/VansCharacterControllerNode.h"
#include "../EngineCore/PhysicsCore/VansPhysics.h"
#include "../EngineCore/PhysicsCore/VansPhysicsNode.h"
#include "../EngineCore/SceneCore/VansSceneAnimationComponentReader.h"
#include "../EngineCore/SceneCore/VansAssetObjectBootstrapper.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/ScriptCore/VansLuaGameplayActionBridge.h"
#include "../EngineCore/ScriptCore/VansTransform.h"
#include "../EngineCore/TimelineCore/VansTimelineCompiler.h"
#include "../EngineCore/TimelineCore/VansTimelineSerialization.h"
#include "../EngineCore/TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineApplierRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineClockRegistry.h"
#include "../EngineCore/TimelineRuntime/VansTimelineSessionService.h"
#include "../EngineCore/EditorCore/Timeline/VansTimelineTrackDescriptorRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
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

const Vans::VansCompiledActionRecord* FindCompiledActionRecord(
	const std::vector<Vans::VansCompiledActionRecord>& records,
	std::string_view type)
{
	const auto found = std::find_if(records.begin(), records.end(), [&](const auto& record)
	{
		return record.type == type;
	});
	return found == records.end() ? nullptr : &*found;
}

std::string CompiledActionReference(
	const Vans::VansCompiledActionRecord* record,
	const char* field)
{
	if (!record) return {};
	const Vans::VansSerializedValue* value = Vans::FindObjectField(record->inputs, field);
	if (!value) return {};
	if (value->kind == Vans::VansSerializedValue::Kind::String) return value->stringValue;
	if (value->kind != Vans::VansSerializedValue::Kind::Object) return {};
	for (const char* key : { "stableId", "id", "guid", "path", "assetGuid", "assetPath" })
	{
		const std::string reference = Vans::ReadSerializedStringField(*value, key);
		if (!reference.empty()) return reference;
	}
	return {};
}

void SetGrantExtension(
	Vans::VansActionGrantDesc& grant,
	std::string type,
	Vans::VansSerializedValue inputs)
{
	const auto found = std::find_if(grant.extensions.begin(), grant.extensions.end(),
		[&](const auto& extension) { return extension.type == type; });
	if (found == grant.extensions.end())
		grant.extensions.push_back({ std::move(type), std::move(inputs) });
	else
		found->inputs = std::move(inputs);
}

void AddHostTagInitializer(
	Vans::VansGameplayActionHostSetup& setup,
	std::string tag,
	std::uint32_t count = 1)
{
	setup.initializers.push_back({ "Gameplay.Tags.Initialize",
		Vans::VansSerializedValue::Object({
			{ "tag", Vans::VansSerializedValue::String(std::move(tag)) },
			{ "count", Vans::VansSerializedValue::Int(count) }
		}) });
}

void AddHostAttributeInitializer(
	Vans::VansGameplayActionHostSetup& setup,
	std::string attribute,
	double value)
{
	setup.initializers.push_back({ "Gameplay.Attributes.Initialize",
		Vans::VansSerializedValue::Object({
			{ "attribute", Vans::VansSerializedValue::String(std::move(attribute)) },
			{ "value", Vans::VansSerializedValue::Float(value) }
		}) });
}

bool BootstrapGameplayMemory(
	const std::vector<Vans::VansAssetRecord>& records,
	Vans::VansAssetObjectRepository& repository,
	std::string& error)
{
	const Vans::VansAssetObjectBootstrapResult result =
		Vans::VansAssetObjectBootstrapper::Publish(records, repository);
	if (result) return true;
	error = result.errors.empty()
		? "GAF memory bootstrap failed" : result.errors.front();
	return false;
}

const std::vector<Vans::VansActionServiceCapability>& TestActionCapabilities()
{
	static const std::vector<Vans::VansActionServiceCapability> capabilities{
		Vans::VansAnimationActionCapability(),
		Vans::VansAudioActionCapability(),
		Vans::VansVFXActionCapability(),
		Vans::VansCombatActionCapability(),
		Vans::VansPhysicsQueryActionCapability(),
		Vans::VansProjectileActionCapability(),
		Vans::VansCameraActionCapability(),
		Vans::VansNavigationActionCapability(),
		Vans::VansUIActionCapability()
	};
	return capabilities;
}

const Vans::VansActionServiceCapability* FindTestActionCapability(
	Vans::VansActionServiceId service)
{
	const auto& capabilities = TestActionCapabilities();
	const auto found = std::find_if(capabilities.begin(), capabilities.end(),
		[service](const auto& capability) { return capability.service == service; });
	return found == capabilities.end() ? nullptr : &*found;
}

std::vector<std::shared_ptr<Vans::VansFakeActionService>> CreateTestFakeActionServices()
{
	std::vector<std::shared_ptr<Vans::VansFakeActionService>> services;
	for (const auto& capability : TestActionCapabilities())
		services.push_back(std::make_shared<Vans::VansFakeActionService>(capability));
	return services;
}

std::shared_ptr<const Vans::IVansGameplayModuleContributor> MakeTestRuntimeContributor(
	std::string moduleName,
	std::vector<std::shared_ptr<Vans::IVansActionService>> services = {},
	Vans::VansGameplayModuleAssetCompilerContribution assetCompilerContribution = {},
	Vans::VansGameplayModuleAssetSchemaContribution assetSchemaContribution = {})
{
	return Vans::VansMakeGAFModuleContributor(
		Vans::VansMakeGAFModuleDescriptor(std::move(moduleName), "GAF Contract Module",
			{ "Core" }, {}, Vans::VansGAFModuleSource::Project),
		{}, {},
		[services = std::move(services)](
			Vans::VansGAFRuntimeRegistry& contribution,
			std::string& error)
		{
			for (const auto& service : services)
				if (!contribution.RegisterService(service, error)) return false;
			return true;
		}, std::move(assetCompilerContribution), std::move(assetSchemaContribution));
}

std::shared_ptr<const Vans::IVansGameplayModuleContributor> MakeProjectSchemaContributor(
	Vans::VansGAFProjectConfiguration configuration)
{
	return Vans::VansMakeGAFModuleContributor(
		Vans::VansMakeGAFModuleDescriptor("Project.Script", "Project Script GAF",
			{ "Core" }, {}, Vans::VansGAFModuleSource::Project),
		[configuration](Vans::VansGAFTypeRegistry& registry, std::string& error)
		{
			return configuration.RegisterConfiguredTypes(registry, error);
		},
		[configuration](Vans::VansGAFSchemaRegistry& registry, std::string& error)
		{
			return configuration.RegisterConfiguredSchemas(registry, error);
		},
		{});
}

class PassiveTimelineTestDriver final : public Vans::IVansActionSidecarDriver
{
public:
	bool Start(Vans::VansActionExecutionContext&, std::string&) override { return true; }
	bool Tick(Vans::VansActionExecutionContext&, std::string&) override { return true; }
	void Finish(Vans::VansActionExecutionContext&, Vans::VansActionEndReason) override {}
	std::string_view StableName() const override { return "Timeline.Driver.Session.Test"; }
};

std::shared_ptr<const Vans::IVansGameplayModuleContributor> MakeTestTimelineContributor()
{
	return Vans::VansMakeGAFModuleContributor(
		Vans::VansMakeGAFModuleDescriptor("Timeline", "Timeline GAF Test Driver", { "Core" }),
		Vans::VansRegisterTimelineGAFTypes,
		Vans::VansRegisterTimelineGAFSchemas,
		[](Vans::VansGAFRuntimeRegistry& registry, std::string& error)
		{
			return registry.RegisterSidecarDriver("Timeline.Driver.Session",
				[](const Vans::VansCompiledActionRecord&)
				{ return std::make_unique<PassiveTimelineTestDriver>(); }, error);
		});
}

template<typename TService>
std::shared_ptr<const Vans::IVansGameplayModuleContributor> MakeTestRuntimeContributor(
	std::string moduleName,
	const std::vector<std::shared_ptr<TService>>& services)
{
	std::vector<std::shared_ptr<Vans::IVansActionService>> actionServices;
	actionServices.reserve(services.size());
	for (const auto& service : services) actionServices.push_back(service);
	return MakeTestRuntimeContributor(
		std::move(moduleName), std::move(actionServices));
}

bool LoadContractSkeletonFromModel(
	const std::filesystem::path& modelPath,
	VansGraphics::Skeleton& skeleton,
	std::string& error)
{
	Vans::VansAssetMeta meta;
	if (!Vans::VansAssetMetaStorage::Load(
		Vans::VansAssetMeta::MetaPathFor(modelPath), meta, error))
	{
		error = "Skeleton model metadata is required: " + error;
		return false;
	}
	return VansGraphics::VansSkinnedMeshLoader::LoadSkeletonFromModelAsset(
		modelPath.string(), Vans::ReadSkeletalMeshImportSettings(meta), skeleton, error);
}

std::shared_ptr<const VansGraphics::VansAnimationRigAsset> LoadRigForContract(
	const std::filesystem::path& path, std::string& error)
{
	auto rig = std::make_shared<VansGraphics::VansAnimationRigAsset>();
	if (!VansGraphics::VansAnimationRigStorage::Load(path, *rig, error))
		return {};
	return rig;
}

bool LoadAnimationClipAssetForContract(
	const std::filesystem::path& path,
	std::shared_ptr<const VansGraphics::VansAnimationClipAsset>& outAsset,
	std::string& error)
{
	auto asset = std::make_shared<VansGraphics::VansAnimationClipAsset>();
	if (!VansGraphics::VansAnimationClipIO::Load(
		path.string(), asset->clip, asset->skeleton))
	{
		error = "Animation Clip cannot be loaded: " + path.string();
		return false;
	}
	outAsset = std::move(asset);
	return true;
}

VansGraphics::VansAnimatorMaskResolver ProjectMaskResolverForContract(const std::filesystem::path& project)
{
	return [project](const VansGraphics::VansAnimationLayerDefinition& layer,
		std::shared_ptr<const VansGraphics::VansBoneMaskAsset>& result, std::string& error)
	{
		auto mask = std::make_shared<VansGraphics::VansBoneMaskAsset>();
		if (!VansGraphics::VansBoneMaskStorage::Load(project / layer.maskPathHint, *mask, error)) return false;
		result = std::move(mask);
		return true;
	};
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
	bool BeginsPipeline() const override { return true; }
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
		Vans::VansActionCommandSchema command;
		command.command = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Probe.Run");
		command.stableName = "Probe.Run";
		command.resourcePolicy = Vans::VansActionCommandResourcePolicy::Create;
		capability.commandSchemas.push_back(std::move(command));
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
			Vans::VansActionError::Execution, "requested failure" };
	}
	Vans::VansActionExecutorResult Tick(Vans::VansActionExecutionContext&) override
	{
		return { Vans::VansActionExecutorStatus::Failed,
			Vans::VansActionError::Execution, "requested failure" };
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
	spec.context.correlationId = 9;
	const auto first = effects.Apply(spec);
	const auto second = effects.Apply(spec);
	if (!ExpectGAF(first && first.active && second && second.stacked && second.active == first.active &&
		effects.ActiveCount() == 1 && std::abs(attributes.Current(healthId) - 100.0) < 0.0001 &&
		tags.CountExact(tagDictionary.Find("Effect.Active")->id) == 2 &&
		adapter->addCount == 1 && adapter->updateCount == 1 && adapter->lastIntensity == 2.0,
		"Periodic Effect incorrectly installed a persistent Attribute modifier")) return false;
	const auto overflow = effects.Apply(spec);
	if (!ExpectGAF(!overflow && overflow.error == Vans::VansActionError::Rejected,
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
		budgetBlocked.error == Vans::VansActionError::Budget,
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
	contextSpec.context.SetSerialized(Vans::VansActionContextSlots::Payload,
		Vans::VansSerializedValue::Object({
		{ "bonus", Vans::VansSerializedValue::Float(4.0) }
	}));
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

	const auto* audioCapability = FindTestActionCapability(
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
	serviceParameters.context.SetEntity(Vans::VansActionContextSlots::Owner, { 8, 1 });
	serviceParameters.context.correlationId = 4;
	serviceParameters.intensity = 0.75;
	const Vans::VansGameplayCueKey serviceKey{ 2, serviceCueId, 1 };
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
	policy.steps.push_back({ acquire->TypeId(),
		std::string(acquire->StableName()), Vans::VansSerializedValue::Object({}) });
	policy.steps.push_back({ limit->TypeId(),
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
	first->contentHash = 11;
	first->executor = Vans::VansMakeStableId<Vans::VansActionExecutorIdTag>("Executor.Test");
	std::string error;
	if (!definitions.Register(first, error)) return ExpectGAF(false, error.c_str());
	auto second = std::make_shared<Vans::VansCompiledActionDefinition>(*first);
	second->contentHash = 22;
	if (!definitions.Replace(second, error)) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(definitions.Resolve(first->id) == second,
		"Action Definition safe-point replacement failed")) return false;
	auto invalid = std::make_shared<Vans::VansCompiledActionDefinition>();
	if (!ExpectGAF(!definitions.Register(invalid, error),
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

	const auto& capabilities = TestActionCapabilities();
	std::size_t commandCount = 0;
	const bool everyCapabilityHasCommands = std::all_of(
		capabilities.begin(), capabilities.end(), [](const auto& capability)
		{ return !capability.commandSchemas.empty(); });
	for (const auto& capability : capabilities) commandCount += capability.commandSchemas.size();
	if (!ExpectGAF(capabilities.size() == 9 && everyCapabilityHasCommands &&
		commandCount >= capabilities.size(),
		"GAF 九类标准 Service 或命令目录不完整")) return false;
	auto fakeServices = CreateTestFakeActionServices();
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
		Vans::VansActionError::InvalidDefinition,
		"GAF Service 未拒绝缺少必填字段的命令")) return false;
	invalidCommand.payload = Vans::VansBuildActionCommandSamplePayload(*cameraShot);
	Vans::SetSerializedObjectField(invalidCommand.payload, "typo",
		Vans::VansSerializedValue::Bool(true));
	if (!ExpectGAF(standardRegistry.Execute(invalidCommand).error ==
		Vans::VansActionError::InvalidDefinition,
		"GAF Service 未拒绝未知负载字段")) return false;
	const auto combatServiceId =
		Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Combat");
	const auto resolveHitId =
		Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Combat.ResolveHit");
	const Vans::VansActionCommandSchema* resolveHit =
		standardRegistry.ResolveCommandSchema(combatServiceId, resolveHitId);
	if (!ExpectGAF(resolveHit != nullptr, "GAF Combat Service 命令 Schema 无法解析")) return false;

	Vans::VansGAFModuleEnvironment editorOnly;
	editorOnly.runtime = false;
	editorOnly.cook = false;
	editorOnly.editor = true;
	const auto coreEditor = Vans::VansMakeGAFEditorContributor(
		Vans::VansMakeGAFModuleDescriptor(
			"Core", "GAF Core Editor", {}, {}, Vans::VansGAFModuleSource::Engine, editorOnly),
		[](Vans::VansGAFEditorRegistry& registry, std::string& registerError)
		{
			return registry.Register({ "Core.Driver.Graph", "Graph", "Core",
				Vans::VansGAFExtensionKind::Driver }, registerError);
		});
	const auto cameraEditor = Vans::VansMakeGAFEditorContributor(
		Vans::VansMakeGAFModuleDescriptor(
			"Gameplay.Camera", "Camera GAF Editor", { "Core" }, {},
			Vans::VansGAFModuleSource::Engine, editorOnly),
		[](Vans::VansGAFEditorRegistry& registry, std::string& registerError)
		{
			return registry.Register({ "Camera.Shot", "Camera Shot", "Camera",
				Vans::VansGAFExtensionKind::Operation }, registerError);
		});
	std::vector<std::shared_ptr<const Vans::IVansGameplayEditorContributor>> orderedEditors;
	if (!Vans::VansOrderGameplayEditorContributors(
		{ cameraEditor, coreEditor }, orderedEditors, error) || orderedEditors.size() != 2 ||
		orderedEditors.front()->Descriptor().moduleId != "Core")
		return ExpectGAF(false, error.empty()
			? "GAF Editor contributors were not dependency ordered" : error.c_str());
	Vans::VansGAFEditorRegistry editorRegistry;
	for (const auto& contributor : orderedEditors)
		if (!contributor->RegisterEditor(editorRegistry, error))
			return ExpectGAF(false, error.c_str());
	if (!editorRegistry.Seal(error) ||
		!editorRegistry.Resolve("Core.Driver.Graph") ||
		!editorRegistry.Resolve("Camera.Shot") ||
		editorRegistry.Descriptors().size() != 2)
		return ExpectGAF(false, "GAF Editor registry did not retain contributed descriptors");
	if (!ExpectGAF(!editorRegistry.Register(
		{ "Camera.Shot", "Duplicate", "Camera", Vans::VansGAFExtensionKind::Operation },
		error), "sealed GAF Editor registry accepted a replacement")) return false;
	error.clear();
	Vans::VansAssetObjectRepository emptyAssetObjects;
	Vans::VansGameplayRuntime coreOnlyRuntime;
	if (!ExpectGAF(coreOnlyRuntime.Initialize({}, emptyAssetObjects, error),
		error.empty() ? "GAF Core could not initialize without Gameplay.Primitives"
			: error.c_str())) return false;
	coreOnlyRuntime.Shutdown();
	return true;
}

bool TestGAFResourceLedgerAndTaskContract()
{
	Vans::VansActionResourceLedger ledger;
	std::vector<int> order;
	std::string error;
	Vans::VansActionResourceEntry first;
	first.type = "Probe";
	first.debugName = "first";
	first.release = [&] { order.push_back(10); return true; };
	const auto firstHandle = ledger.Register(std::move(first), error);
	Vans::VansActionResourceEntry second;
	second.type = "Probe";
	second.debugName = "second";
	second.dependsOn = firstHandle;
	second.release = [&] { order.push_back(20); return true; };
	const auto secondHandle = ledger.Register(std::move(second), error);
	if (!firstHandle || !secondHandle) return ExpectGAF(false, error.c_str());
	std::vector<std::string> errors;
	if (!ledger.ReleaseAll(errors)) return false;
	const std::vector<int> expected{ 20, 10 };
	if (!ExpectGAF(order == expected && ledger.ActiveCount() == 0 && ledger.IsReleased(),
		"ResourceLedger lifecycle release order is invalid")) return false;
	if (!ExpectGAF(!ledger.Register({}, error), "已释放 ResourceLedger 仍接受资源")) return false;

	Vans::VansActionResourceLedger actionResources;
	Vans::VansActionResourceLedger hostResources;
	int transferredReleaseCount = 0;
	Vans::VansActionResourceEntry transferred;
	transferred.type = "Probe.Transferred";
	transferred.debugName = "transferred";
	transferred.release = [&] { ++transferredReleaseCount; return true; };
	const Vans::VansActionResourceHandle actionResource =
		actionResources.Register(std::move(transferred), error);
	Vans::VansActionResourceHandle hostResource;
	if (!actionResource || !actionResources.Transfer(
		actionResource, hostResources, hostResource, error))
		return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(actionResources.ActiveCount() == 0 &&
		hostResources.ActiveCount() == 1 && transferredReleaseCount == 0,
		"ResourceLedger transfer released or duplicated the resource")) return false;
	errors.clear();
	if (!actionResources.ReleaseAll(errors) || transferredReleaseCount != 0)
		return ExpectGAF(false, "Action ResourceLedger released a transferred Host resource");
	if (!hostResources.ReleaseAll(errors) || transferredReleaseCount != 1)
		return ExpectGAF(false, "Host ResourceLedger did not release a transferred resource exactly once");

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
	if (!ExpectGAF(tasks.State(taskHandle) == Vans::VansActionTaskState::TimedOut,
		"Action Task terminal state was lost after releasing active budget")) return false;
	Vans::VansActionTaskState consumedState{};
	if (!tasks.Consume(taskHandle, consumedState, error) ||
		!ExpectGAF(consumedState == Vans::VansActionTaskState::TimedOut,
			"Action Task terminal result could not be consumed")) return false;
	error.clear();
	if (!ExpectGAF(!tasks.Consume(taskHandle, consumedState, error),
		"Action Task terminal result was consumable more than once")) return false;
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
	graph->contentHash = 123;
	graph->entryNode = 0;
	graph->nodes.push_back({ "node-a", immediate->TypeId(),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}) });
	graph->nodes.push_back({ "node-b", wait->TypeId(),
		Vans::VansActionGraphNodeKind::Latent, Vans::VansSerializedValue::Object({}) });
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
		budgetResult.error == Vans::VansActionError::Budget && !budgetRuntime.IsRunning(),
		"ExecutionGraph transition budget did not terminate runaway same-tick work")) return false;

	Vans::VansActionGraphNodeRegistry builtIns;
	if (!Vans::VansRegisterBuiltInActionGraphNodes(builtIns, error) || !builtIns.Seal(error))
		return ExpectGAF(false, error.c_str());
	const std::vector<std::string> builtInNames{
		"Action.Graph.Sequence", "Action.Graph.Parallel", "Action.Graph.Race",
		"Action.Graph.Branch", "Action.Graph.Switch", "Action.Graph.Loop",
		"Action.Graph.Repeat", "Action.Graph.Channel", "Action.Graph.Gate",
		"Action.Graph.Wait", "Action.Graph.Timeout", "Core.Graph.Invoke",
		"Core.Graph.ReadBinding", "Core.Graph.WaitSignal", "Core.Graph.AwaitTask",
		"Core.Graph.ReleaseResource", "Core.Graph.TransferResource", "Core.Graph.EmitSignal",
		"Action.Graph.Complete", "Action.Graph.Fail", "Action.Graph.SubAction",
		"Action.Graph.Transition", "Action.Graph.Try"
	};
	for (const std::string& name : builtInNames)
	{
		const auto handler = builtIns.Resolve(
			Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>(name));
		if (!ExpectGAF(handler && handler->StableName() == name,
			"Built-in Action Graph node registry is incomplete")) return false;
	}
	const auto literalBinding = [](const char* type, Vans::VansSerializedValue value)
	{
		return Vans::VansSerializedValue::Object({
			{ "source", Vans::VansSerializedValue::String("Literal") },
			{ "type", Vans::VansSerializedValue::String(type) },
			{ "value", std::move(value) }
		});
	};
	const auto variableBinding = [](const char* type, const char* name)
	{
		return Vans::VansSerializedValue::Object({
			{ "source", Vans::VansSerializedValue::String("Variable") },
			{ "type", Vans::VansSerializedValue::String(type) },
			{ "name", Vans::VansSerializedValue::String(name) }
		});
	};
	const auto outputBinding = [](const char* type, const char* name)
	{
		return Vans::VansSerializedValue::Object({
			{ "target", Vans::VansSerializedValue::String("Variable") },
			{ "type", Vans::VansSerializedValue::String(type) },
			{ "name", Vans::VansSerializedValue::String(name) }
		});
	};
	const auto handleValue = [](Vans::VansGenerationHandle handle)
	{
		return Vans::VansSerializedValue::Object({
			{ "index", Vans::VansSerializedValue::Int(handle.index) },
			{ "generation", Vans::VansSerializedValue::Int(handle.generation) }
		});
	};

	Vans::VansActionVariableStore primitiveVariables;
	if (!primitiveVariables.Initialize({
		{ Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("binding.value"),
			"binding.value", Vans::VansSerializedValue::Int(0) },
		{ Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("signal.payload"),
			"signal.payload", Vans::VansSerializedValue::Object({}) },
		{ Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("resource.host"),
			"resource.host", Vans::VansSerializedValue::Object({}) }
	}, error)) return ExpectGAF(false, error.c_str());
	Vans::VansActionContext primitiveActionContext;
	primitiveActionContext.SetEntity(Vans::VansActionContextSlots::Owner, { 31, 1 });
	primitiveActionContext.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, { 32, 1 });
	Vans::VansActionExecutionContext primitiveContext;
	primitiveContext.context = &primitiveActionContext;
	primitiveContext.variables = &primitiveVariables;
	Vans::VansSerializedValue nodeState = Vans::VansSerializedValue::Object({});

	Vans::VansCompiledActionGraphNode readBindingNode{
		"read-binding",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.ReadBinding"),
		Vans::VansActionGraphNodeKind::Pure,
		Vans::VansSerializedValue::Object({
			{ "input", literalBinding("Int", Vans::VansSerializedValue::Int(42)) },
			{ "output", outputBinding("Int", "binding.value") }
		})
	};
	const auto readBindingHandler = builtIns.Resolve(readBindingNode.type);
	if (!readBindingHandler || readBindingHandler->Start(
		primitiveContext, readBindingNode, nodeState).status !=
		Vans::VansActionGraphNodeStatus::Succeeded)
		return ExpectGAF(false, "ReadBinding node did not resolve and write typed bindings");
	const Vans::VansSerializedValue* bindingValue = primitiveVariables.Get(
		Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("binding.value"));
	if (!ExpectGAF(bindingValue && bindingValue->kind == Vans::VansSerializedValue::Kind::Int &&
		bindingValue->intValue == 42, "ReadBinding node lost the typed value")) return false;

	Vans::VansCompiledActionGraphNode waitSignalNode{
		"wait-signal",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.WaitSignal"),
		Vans::VansActionGraphNodeKind::Latent,
		Vans::VansSerializedValue::Object({
			{ "signal", Vans::VansSerializedValue::String("Signal.Test.ExactAction") },
			{ "timeoutSeconds", Vans::VansSerializedValue::Float(1.0) },
			{ "payloadOutput", outputBinding("Object", "signal.payload") }
		})
	};
	const auto waitSignalHandler = builtIns.Resolve(waitSignalNode.type);
	nodeState = Vans::VansSerializedValue::Object({});
	if (!waitSignalHandler || waitSignalHandler->Start(
		primitiveContext, waitSignalNode, nodeState).status !=
		Vans::VansActionGraphNodeStatus::Waiting)
		return ExpectGAF(false, "WaitSignal node did not enter its latent state");
	std::vector<Vans::VansActionEvent> signalEvents{
		{ Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Signal.Test.ExactAction"),
			"Signal.Test.ExactAction", { 31, 1 }, { 32, 1 },
			Vans::VansSerializedValue::Object({
				{ "marker", Vans::VansSerializedValue::String("Hit") }
			}) }
	};
	primitiveContext.events = &signalEvents;
	if (waitSignalHandler->Tick(primitiveContext, waitSignalNode, nodeState).status !=
		Vans::VansActionGraphNodeStatus::Succeeded)
		return ExpectGAF(false, "WaitSignal node did not consume the exact Action signal");
	const Vans::VansSerializedValue* signalPayload = primitiveVariables.Get(
		Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("signal.payload"));
	if (!ExpectGAF(signalPayload && Vans::ReadSerializedStringField(
		*signalPayload, "marker") == "Hit", "WaitSignal node did not preserve its payload")) return false;
	primitiveContext.events = nullptr;

	Vans::VansActionTaskSet graphTasks;
	Vans::VansActionTaskDesc graphTask;
	graphTask.type = Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Task.GraphProbe");
	graphTask.debugName = "graph task";
	const Vans::VansActionTaskHandle graphTaskHandle = graphTasks.Create(std::move(graphTask), error);
	Vans::VansCompiledActionGraphNode awaitTaskNode{
		"await-task",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.AwaitTask"),
		Vans::VansActionGraphNodeKind::Latent,
		Vans::VansSerializedValue::Object({
			{ "task", literalBinding("Resource", handleValue(graphTaskHandle.value)) }
		})
	};
	primitiveContext.tasks = &graphTasks;
	const auto awaitTaskHandler = builtIns.Resolve(awaitTaskNode.type);
	nodeState = Vans::VansSerializedValue::Object({});
	if (!graphTaskHandle || !awaitTaskHandler || awaitTaskHandler->Start(
		primitiveContext, awaitTaskNode, nodeState).status != Vans::VansActionGraphNodeStatus::Waiting ||
		!graphTasks.Complete(graphTaskHandle, error) || awaitTaskHandler->Tick(
		primitiveContext, awaitTaskNode, nodeState).status != Vans::VansActionGraphNodeStatus::Succeeded)
		return ExpectGAF(false, "AwaitTask node did not observe and consume task completion");

	Vans::VansActionResourceLedger graphActionResources;
	Vans::VansActionResourceLedger graphHostResources;
	int graphResourceReleaseCount = 0;
	Vans::VansActionResourceEntry graphResource;
	graphResource.type = "Probe.GraphResource";
	graphResource.debugName = "graph resource";
	graphResource.release = [&] { ++graphResourceReleaseCount; return true; };
	const Vans::VansActionResourceHandle graphResourceHandle =
		graphActionResources.Register(std::move(graphResource), error);
	Vans::VansCompiledActionGraphNode transferResourceNode{
		"transfer-resource",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.TransferResource"),
		Vans::VansActionGraphNodeKind::Command,
		Vans::VansSerializedValue::Object({
			{ "resource", literalBinding("Resource", handleValue(graphResourceHandle.value)) },
			{ "destination", Vans::VansSerializedValue::String("Host") },
			{ "output", outputBinding("Resource", "resource.host") }
		})
	};
	primitiveContext.resources = &graphActionResources;
	primitiveContext.hostResources = &graphHostResources;
	const auto transferResourceHandler = builtIns.Resolve(transferResourceNode.type);
	nodeState = Vans::VansSerializedValue::Object({});
	if (!graphResourceHandle || !transferResourceHandler || transferResourceHandler->Start(
		primitiveContext, transferResourceNode, nodeState).status !=
		Vans::VansActionGraphNodeStatus::Succeeded || graphActionResources.ActiveCount() != 0 ||
		graphHostResources.ActiveCount() != 1 || graphResourceReleaseCount != 0)
		return ExpectGAF(false, "TransferResource node did not move ownership to the Host ledger");
	Vans::VansCompiledActionGraphNode releaseResourceNode{
		"release-resource",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.ReleaseResource"),
		Vans::VansActionGraphNodeKind::Command,
		Vans::VansSerializedValue::Object({
			{ "resource", variableBinding("Resource", "resource.host") }
		})
	};
	primitiveContext.resources = &graphHostResources;
	const auto releaseResourceHandler = builtIns.Resolve(releaseResourceNode.type);
	if (!releaseResourceHandler || releaseResourceHandler->Start(
		primitiveContext, releaseResourceNode, nodeState).status !=
		Vans::VansActionGraphNodeStatus::Succeeded || graphHostResources.ActiveCount() != 0 ||
		graphResourceReleaseCount != 1)
		return ExpectGAF(false, "ReleaseResource node did not release Host-owned state exactly once");

	Vans::VansActionEvent emittedSignal;
	int emittedSignalCount = 0;
	primitiveContext.emitSignal = [&](Vans::VansActionEvent event, std::string&)
	{
		emittedSignal = std::move(event);
		++emittedSignalCount;
		return true;
	};
	Vans::VansCompiledActionGraphNode emitSignalNode{
		"emit-signal",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.EmitSignal"),
		Vans::VansActionGraphNodeKind::Command,
		Vans::VansSerializedValue::Object({
			{ "signal", Vans::VansSerializedValue::String("Signal.Test.ExactAction") },
			{ "payload", literalBinding("Object", Vans::VansSerializedValue::Object({
				{ "marker", Vans::VansSerializedValue::String("Notify") }
			})) }
		})
	};
	const auto emitSignalHandler = builtIns.Resolve(emitSignalNode.type);
	if (!emitSignalHandler || emitSignalHandler->Start(
		primitiveContext, emitSignalNode, nodeState).status !=
		Vans::VansActionGraphNodeStatus::Succeeded || emittedSignalCount != 1 ||
		emittedSignal.stableName != "Signal.Test.ExactAction" ||
		emittedSignal.source.index != 31 || emittedSignal.target.index != 32 ||
		Vans::ReadSerializedStringField(emittedSignal.payload, "marker") != "Notify")
		return ExpectGAF(false, "EmitSignal node did not route the exact Action signal and payload");

	const auto builtInType = [](const char* name)
	{
		return Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>(name);
	};
	auto repeatGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	repeatGraph->name = "Graph.Repeat";
	repeatGraph->contentHash = 201;
	repeatGraph->entryNode = 0;
	repeatGraph->nodes.push_back({ "repeat", builtInType("Action.Graph.Repeat"),
		Vans::VansActionGraphNodeKind::Flow,
		Vans::VansSerializedValue::Object({ { "count", Vans::VansSerializedValue::Int(2) } }) });
	repeatGraph->nodes.push_back({ "body", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}) });
	repeatGraph->nodes.push_back({ "end", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}) });
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
	parallelGraph->contentHash = 202;
	parallelGraph->entryNode = 0;
	parallelGraph->nodes.push_back({ "parallel", builtInType("Action.Graph.Parallel"),
		Vans::VansActionGraphNodeKind::Flow,
		Vans::VansSerializedValue::Object({ { "branches", Vans::VansSerializedValue::Int(2) } }) });
	parallelGraph->nodes.push_back({ "left", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}) });
	parallelGraph->nodes.push_back({ "right", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}) });
	parallelGraph->nodes.push_back({ "joined", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}) });
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
	raceGraph->contentHash = 203;
	raceGraph->entryNode = 0;
	raceGraph->nodes.push_back({ "race", builtInType("Action.Graph.Race"),
		Vans::VansActionGraphNodeKind::Flow,
		Vans::VansSerializedValue::Object({ { "cancelNodes", Vans::VansSerializedValue::Array({
			Vans::VansSerializedValue::String("wait") }) } }) });
	raceGraph->nodes.push_back({ "wait", builtInType("Action.Graph.Wait"),
		Vans::VansActionGraphNodeKind::Latent,
		Vans::VansSerializedValue::Object({ { "seconds", Vans::VansSerializedValue::Float(10.0) } }) });
	raceGraph->nodes.push_back({ "winner", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}) });
	raceGraph->nodes.push_back({ "end", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}) });
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
	loopGraph->contentHash = 204;
	loopGraph->entryNode = 0;
	loopGraph->nodes.push_back({ "loop", builtInType("Action.Graph.Loop"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({
			{ "condition", Vans::VansSerializedValue::Bool(true) },
			{ "maximumIterations", Vans::VansSerializedValue::Int(2) } }) });
	loopGraph->nodes.push_back({ "body", builtInType("Action.Graph.Complete"),
		Vans::VansActionGraphNodeKind::Flow, Vans::VansSerializedValue::Object({}) });
	loopGraph->edges.push_back({ 0, "Body", 1, 0 });
	loopGraph->edges.push_back({ 1, "Success", 0, 0 });
	Vans::VansActionGraphRuntime loopRuntime;
	for (const auto& diagnostic : loopRuntime.Initialize(loopGraph, &builtIns, 32))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	const Vans::VansActionExecutorResult looped = loopRuntime.Start(context);
	if (!ExpectGAF(looped.status == Vans::VansActionExecutorStatus::Failed &&
		looped.error == Vans::VansActionError::Budget,
		"Loop node did not preserve its specific bounded-loop failure")) return false;

	auto timeoutGraph = std::make_shared<Vans::VansCompiledActionGraph>();
	timeoutGraph->name = "Graph.Timeout";
	timeoutGraph->contentHash = 205;
	timeoutGraph->entryNode = 0;
	timeoutGraph->nodes.push_back({ "timeout", builtInType("Action.Graph.Timeout"),
		Vans::VansActionGraphNodeKind::Latent, Vans::VansSerializedValue::Object({
			{ "seconds", Vans::VansSerializedValue::Float(0.1) },
			{ "fail", Vans::VansSerializedValue::Bool(true) } }) });
	Vans::VansActionGraphRuntime timeoutRuntime;
	for (const auto& diagnostic : timeoutRuntime.Initialize(timeoutGraph, &builtIns, 32))
		if (diagnostic.severity == Vans::VansGameplayDiagnosticSeverity::Error) return false;
	context.deltaSeconds = 0.0;
	if (timeoutRuntime.Start(context).status != Vans::VansActionExecutorStatus::Waiting) return false;
	context.deltaSeconds = 0.1;
	const Vans::VansActionExecutorResult timedOut = timeoutRuntime.Tick(context);
	if (!ExpectGAF(timedOut.status == Vans::VansActionExecutorStatus::Failed &&
		timedOut.error == Vans::VansActionError::Timeout,
		"Timeout node did not expose a stable timeout error")) return false;

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
	lockGraph->nodes.push_back({ "lock",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.Invoke"),
		Vans::VansActionGraphNodeKind::Command, Vans::VansSerializedValue::Object({
			{ "capability", Vans::VansSerializedValue::String("Service.Camera") },
			{ "operation", Vans::VansSerializedValue::String("Camera.LockOn") },
			{ "inputs", Vans::VansSerializedValue::Object({
				{ "target", literalBinding("Object", position(1.0, 0.0, 0.0)) }
			}) },
			{ "outputs", Vans::VansSerializedValue::Object({
				{ "resource", outputBinding("Resource", "camera.lock") }
			}) }
		}) });
	lockGraph->nodes.push_back({ "update",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.Invoke"),
		Vans::VansActionGraphNodeKind::Command, Vans::VansSerializedValue::Object({
			{ "capability", Vans::VansSerializedValue::String("Service.Camera") },
			{ "operation", Vans::VansSerializedValue::String("Camera.UpdateLockOn") },
			{ "inputs", Vans::VansSerializedValue::Object({
				{ "resource", variableBinding("Resource", "camera.lock") },
				{ "target", literalBinding("Object", position(0.0, 0.0, 1.0)) }
			}) },
			{ "outputs", Vans::VansSerializedValue::Object({}) }
		}) });
	lockGraph->edges.push_back({ 0, "Success", 1, 0 });
	Vans::VansActionGraphRuntime lockRuntime;
	for (const auto& diagnostic : lockRuntime.Initialize(lockGraph, &builtIns, 16))
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
	releaseGraph->nodes.push_back({ "release",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.Invoke"),
		Vans::VansActionGraphNodeKind::Command, Vans::VansSerializedValue::Object({
			{ "capability", Vans::VansSerializedValue::String("Service.Camera") },
			{ "operation", Vans::VansSerializedValue::String("Camera.Release") },
			{ "inputs", Vans::VansSerializedValue::Object({
				{ "resource", variableBinding("Resource", "camera.lock") }
			}) },
			{ "outputs", Vans::VansSerializedValue::Object({}) }
		}) });
	Vans::VansActionGraphRuntime releaseRuntime;
	for (const auto& diagnostic : releaseRuntime.Initialize(releaseGraph, &builtIns, 8))
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
	eventGraph->nodes.push_back({ "wait-event",
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Core.Graph.WaitSignal"),
		Vans::VansActionGraphNodeKind::Latent, Vans::VansSerializedValue::Object({
			{ "signal", Vans::VansSerializedValue::String("Camera.BlendComplete") },
			{ "timeoutSeconds", Vans::VansSerializedValue::Float(0.0) },
			{ "payloadOutput", outputBinding("Object", "camera.event") }
		}) });
	Vans::VansActionGraphRuntime eventRuntime;
	for (const auto& diagnostic : eventRuntime.Initialize(eventGraph, &builtIns, 8))
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
			if (request.operation != "Project.Inventory.Consume" ||
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
		bool Commit(const Vans::VansActionExternalCostRequest& request,
			std::string& message) override
		{
			if (!CanCommit(request, message)) return false;
			balance -= request.amount;
			return true;
		}

		double balance = 5.0;
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
	Vans::VansActionDriverRegistry drivers;
	if (!drivers.RegisterExecutorOwned("Test.Executor", error) || !drivers.Seal(error))
		return false;
	Vans::VansActionDefinitionRegistry definitions;
	auto action = std::make_shared<Vans::VansCompiledActionDefinition>();
	action->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.Host");
	action->name = "Action.Test.Host";
	action->contentHash = 901;
	action->executor = executorId;
	action->concurrencyGroup = Vans::VansMakeStableId<Vans::VansActionConcurrencyGroupIdTag>("Group.Test");
	action->concurrencyPolicy = Vans::VansActionConcurrencyPolicy::RejectNew;
	action->program.commit.operations.push_back({ "Gameplay.Attributes.Consume",
		Vans::VansSerializedValue::Object({
			{ "attribute", Vans::VansSerializedValue::String("Character.Energy") },
			{ "amount", Vans::VansSerializedValue::Float(30.0) }
		}) });
	action->program.commit.operations.push_back({ "Core.ExternalCost.Commit",
		Vans::VansSerializedValue::Object({
			{ "operation", Vans::VansSerializedValue::String("Project.Inventory.Consume") },
			{ "resource", Vans::VansSerializedValue::String("Inventory.Test.Ammo") },
			{ "amount", Vans::VansSerializedValue::Float(2.0) }
		}) });
	action->program.commit.operations.push_back({ "Gameplay.Cooldown.Apply",
		Vans::VansSerializedValue::Object({
			{ "duration", Vans::VansSerializedValue::Float(0.5) },
			{ "tag", Vans::VansSerializedValue::String("Cooldown.Test") }
		}) });
	action->program.commit.operations.push_back({ "Gameplay.Cooldown.Apply",
		Vans::VansSerializedValue::Object({
			{ "duration", Vans::VansSerializedValue::Float(1.0) },
			{ "tag", Vans::VansSerializedValue::String("Cooldown.Shared") }
		}) });
	action->program.commit.operations.push_back({ "Gameplay.Tags.Grant",
		Vans::VansSerializedValue::Object({
			{ "tags", Vans::VansSerializedValue::Array({
				Vans::VansSerializedValue::String("Action.Running") }) }
		}) });
	auto persistentDefinition = std::make_shared<Vans::VansCompiledActionDefinition>(*action);
	persistentDefinition->id =
		Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.Persistent");
	persistentDefinition->name = "Action.Test.Persistent";
	persistentDefinition->contentHash = 908;
	persistentDefinition->program.commit.operations.erase(
		persistentDefinition->program.commit.operations.begin() + 1);
	auto queuedAction = std::make_shared<Vans::VansCompiledActionDefinition>(*action);
	queuedAction->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.Queued");
	queuedAction->name = "Action.Test.Queued";
	queuedAction->contentHash = 902;
	queuedAction->concurrencyGroup =
		Vans::VansMakeStableId<Vans::VansActionConcurrencyGroupIdTag>("Group.Queue");
	queuedAction->concurrencyPolicy = Vans::VansActionConcurrencyPolicy::QueueNew;
	queuedAction->concurrencyLimit = 1;
	queuedAction->concurrencyQueueTimeoutSeconds = 1.0;
	queuedAction->program.commit.operations.clear();
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
	transitionSource->program.transitions.push_back({ "Core.Transition.Combo",
		Vans::VansSerializedValue::Object({
			{ "name", Vans::VansSerializedValue::String("BufferedCombo") },
			{ "input", Vans::VansSerializedValue::String("Combo") },
			{ "target", Vans::VansSerializedValue::String(transitionTarget->name) },
			{ "openTime", Vans::VansSerializedValue::Float(0.3) },
			{ "closeTime", Vans::VansSerializedValue::Float(1.0) },
			{ "priority", Vans::VansSerializedValue::Int(10) },
			{ "cancelSource", Vans::VansSerializedValue::Bool(true) },
			{ "contextPatch", Vans::VansSerializedValue::Object({
				{ "comboStage", Vans::VansSerializedValue::Int(2) }
			}) }
		}) });
	transitionSource->program.policies.push_back({ "Core.Policy.InputBuffer",
		Vans::VansSerializedValue::Object({
			{ "enabled", Vans::VansSerializedValue::Bool(true) },
			{ "duration", Vans::VansSerializedValue::Float(0.5) },
			{ "maximumEntries", Vans::VansSerializedValue::Int(1) }
		}) });
	auto failureSource = std::make_shared<Vans::VansCompiledActionDefinition>(*transitionTarget);
	failureSource->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.FailureSource");
	failureSource->name = "Action.Test.FailureSource";
	failureSource->contentHash = 906;
	failureSource->executor = failExecutorId;
	failureSource->program.policies.push_back({ "Core.Policy.Failure",
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String(transitionTarget->name) },
			{ "errors", Vans::VansSerializedValue::Array({
				Vans::VansSerializedValue::String("Execution") }) }
		}) });
	auto targetingAction = std::make_shared<Vans::VansCompiledActionDefinition>(*transitionTarget);
	targetingAction->id = Vans::VansMakeStableId<Vans::VansActionIdTag>("Action.Test.Targeting");
	targetingAction->name = "Action.Test.Targeting";
	targetingAction->contentHash = 907;
	targetingAction->program.activate.operations.push_back({ "Gameplay.Targeting.Resolve",
		Vans::VansSerializedValue::Object({
			{ "asset", Vans::VansSerializedValue::String("Targeting.Test.Primary") }
		}) });
	if (!definitions.Register(action, error) ||
		!definitions.Register(queuedAction, error) ||
		!definitions.Register(timeoutAction, error) ||
		!definitions.Register(transitionTarget, error) ||
		!definitions.Register(transitionSource, error) ||
		!definitions.Register(failureSource, error) ||
		!definitions.Register(targetingAction, error) ||
		!definitions.Register(persistentDefinition, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansTargetingPolicyRegistry targetingPolicies;
	Vans::VansTargetingPolicy targetingPolicy;
	targetingPolicy.id =
		Vans::VansMakeStableId<Vans::VansTargetingPolicyIdTag>("Targeting.Test.Primary");
	targetingPolicy.name = "Targeting.Test.Primary";
	targetingPolicy.steps.push_back({
		Vans::VansMakeStableId<Vans::VansActionGraphNodeTypeIdTag>("Targeting.Acquire.PrimaryTarget"),
		"Targeting.Acquire.PrimaryTarget", Vans::VansSerializedValue::Object({}) });
	targetingPolicy.steps.push_back({
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
	dependencies.drivers = &drivers;
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
	targetingRequest.context.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, { 71, 1 });
	const Vans::VansActionResult targetingDryRun = host.CanActivate(
		targetingRequest.spec, targetingRequest.context);
	const Vans::VansActionResult targeted = host.Activate(targetingRequest);
	const auto targetedSnapshot = targeted ? host.Query(targeted.action) : std::nullopt;
	const Vans::VansTargetDataHandle activeTargetData = targetedSnapshot
		? targetedSnapshot->context.TargetData(Vans::VansActionContextSlots::TargetData)
		: Vans::VansTargetDataHandle{};
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
	SetGrantExtension(grant, "Gameplay.Charges", Vans::VansSerializedValue::Object({
		{ "count", Vans::VansSerializedValue::Int(2) }
	}));
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
	request.context.SetEntity(Vans::VansActionContextSlots::Instigator, { 7, 1 });
	request.context.correlationId = 2;
	const auto first = host.Activate(request);
	if (!ExpectGAF(first && first.action && host.Query(first.action)->state == Vans::VansActionInstanceState::Waiting &&
		std::abs(host.Attributes().Current(energyId) - 70.0) < 0.0001 &&
		std::abs(externalCosts.balance - 3.0) < 0.0001 &&
		host.Tags().Has(tags.Find("Action.Running")->id) &&
		host.Tags().Has(tags.Find("Cooldown.Test")->id) &&
		host.Tags().Has(tags.Find("Cooldown.Shared")->id) && host.IsCooldownActive(action->id),
		"Action Host 未完成激活 Commit")) return false;
	const auto blocked = host.Activate(request);
	if (!ExpectGAF(!blocked && blocked.error == Vans::VansActionError::Rejected,
		"Action Host 未执行 Cooldown 门禁")) return false;
	if (!host.Cancel(first.action, Vans::VansActionCancelReason::User, error)) return false;
	if (!ExpectGAF(host.Query(first.action)->state == Vans::VansActionInstanceState::Ended &&
		std::abs(host.Attributes().Current(energyId) - 70.0) < 0.0001 &&
		std::abs(externalCosts.balance - 3.0) < 0.0001 &&
		!host.Tags().Has(tags.Find("Action.Running")->id),
		"Action cancellation did not preserve committed costs or release lifecycle resources")) return false;
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
	if (!second)
	{
		std::cerr << "[GAF] second Host activation failed: " << second.message << '\n';
		return false;
	}
	host.Tick(0.1);
	host.Tick(0.1);
	if (!ExpectGAF(host.Query(second.action)->state == Vans::VansActionInstanceState::Ended &&
		host.Query(second.action)->endReason == Vans::VansActionEndReason::Completed &&
		std::abs(host.Attributes().Current(energyId) - 40.0) < 0.0001 &&
		std::abs(externalCosts.balance - 1.0) < 0.0001 &&
		executorState->finishCount == 2, "Action Executor 完成路径或 Finish 次数错误")) return false;
	host.Tick(1.0);
	std::shared_ptr<Vans::VansActionHost> hostView(&host, [](Vans::VansActionHost*) {});
	const auto schedulerHandle = scheduler.Register(hostView, error);
	if (!schedulerHandle) return ExpectGAF(false, error.c_str());
	executorState->tickCount = 0;
	Vans::VansActionGrantDesc lateGrant;
	lateGrant.action = transitionTarget->id;
	lateGrant.source = 47;
	const Vans::VansActionSpecHandle lateSpec = host.Grant(lateGrant, error);
	Vans::VansActionActivationRequest lateRequest = request;
	lateRequest.spec = lateSpec;
	const auto lateAction = host.Activate(lateRequest);
	Vans::VansActionEvent event;
	event.type = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>("Action.Event.Probe");
	event.stableName = "Action.Event.Probe";
	if (!lateAction || !host.EnqueueEvent(lateAction.action, std::move(event), error))
		return ExpectGAF(false, lateAction ? error.c_str() : lateAction.message.c_str());
	if (!ExpectGAF(scheduler.RunLateContinuation() && !scheduler.RunLateContinuation() &&
		executorState->eventCount == 1 && executorState->tickCount == 1,
		"ActionScheduler 未限制 SameFrame late continuation 为一次")) return false;
	if (!host.Cancel(lateAction.action, Vans::VansActionCancelReason::System, error) ||
		!scheduler.Unregister(schedulerHandle) ||
		!host.Revoke(lateSpec, Vans::VansActionRevokePolicy::KeepRunning, error)) return false;
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
	queuedRequest.context.SetEntity(Vans::VansActionContextSlots::Instigator, { 7, 1 });
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
		timedSnapshot->error == Vans::VansActionError::Timeout,
		"Concurrency queue timeout was not machine-queryable")) return false;
	if (!host.Cancel(timeoutOwner.action, Vans::VansActionCancelReason::System, error)) return false;
	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::GameLogic);
	if (!ExpectGAF(queuedEvents == 2, "Action queued facts were not published exactly once")) return false;
	if (!host.Revoke(queuedSpec, Vans::VansActionRevokePolicy::CancelRunning, error) ||
		!host.Revoke(timeoutSpec, Vans::VansActionRevokePolicy::CancelRunning, error)) return false;
	Vans::VansActionSetDefinition set;
	set.id = Vans::VansMakeStableId<Vans::VansActionSetIdTag>("ActionSet.Test");
	set.name = "ActionSet.Test";
	SetGrantExtension(grant, "Gameplay.Charges", Vans::VansSerializedValue::Object({
		{ "count", Vans::VansSerializedValue::Int(1) }
	}));
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
	routedActivation.context.SetEntity(Vans::VansActionContextSlots::Owner, { 9, 1 });
	routedActivation.context.SetEntity(Vans::VansActionContextSlots::Instigator, { 9, 1 });
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
	transitionActivation.context.SetEntity(Vans::VansActionContextSlots::Instigator, { 9, 1 });
	transitionActivation.context.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, { 99, 1 });
	const Vans::VansActionResult transitionOwner = transitionHost.Activate(transitionActivation);
	Vans::VansActionContext inputContext;
	inputContext.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, { 100, 1 });
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
	failureActivation.context.SetEntity(Vans::VansActionContextSlots::Instigator, { 9, 1 });
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
	limitedRequest.context.SetEntity(Vans::VansActionContextSlots::Instigator, { 8, 1 });
	Vans::VansActionActivationRequest oversizedPayloadRequest = limitedRequest;
	oversizedPayloadRequest.context.SetSerialized(Vans::VansActionContextSlots::Payload,
		Vans::VansSerializedValue::Object({
		{ "oversized", Vans::VansSerializedValue::String(std::string(64, 'x')) }
	}));
	if (!ExpectGAF(limitedHost.CanActivate(oversizedPayloadRequest.spec,
		oversizedPayloadRequest.context).error == Vans::VansActionError::Budget,
		"Action Host accepted a Context payload above the project budget")) return false;
	const Vans::VansActionResult limitedFirst = limitedHost.Activate(limitedRequest);
	const Vans::VansActionResult limitedBlocked = limitedHost.Activate(limitedRequest);
	if (!ExpectGAF(limitedFirst && !limitedBlocked &&
		limitedBlocked.error == Vans::VansActionError::Budget,
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
	persistentGrant.action = persistentDefinition->id;
	persistentGrant.source = 1001;
	SetGrantExtension(persistentGrant, "Core.Grant.Lifetime",
		Vans::VansSerializedValue::Object({
			{ "policy", Vans::VansSerializedValue::String("Persistent") }
		}));
	const auto persistentSpec = persistenceSource.Grant(persistentGrant, error);
	Vans::VansActionActivationRequest persistentActivation;
	persistentActivation.spec = persistentSpec;
	persistentActivation.context.SetEntity(Vans::VansActionContextSlots::Instigator, { 10, 1 });
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
	const auto persistentLifetime = restoredGrants.empty() ? nullptr : FindCompiledActionRecord(
		restoredGrants.front().extensions, "Core.Grant.Lifetime");
	if (!ExpectGAF(restoredGrants.size() == 1 && persistentLifetime &&
		Vans::ReadSerializedStringField(persistentLifetime->inputs, "policy") == "Persistent",
		"Action Host persistent Grant did not round-trip")) return false;
	if (!ExpectGAF(std::abs(persistenceTarget.Attributes().Base(energyId) - 47.0) < 0.0001,
		"Action Host persistent committed Attribute cost did not round-trip")) return false;
	if (!ExpectGAF(persistenceTarget.IsCooldownActive(persistentDefinition->id),
		"Action Host persistent cooldown did not round-trip")) return false;
	return true;
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
		!Vans::SetSerializedPointer(action, "/variables",
			Vans::VansSerializedValue::Array({
				Vans::VansSerializedValue::Object({
					{ "name", Vans::VansSerializedValue::String("TimelineValue") },
					{ "type", Vans::VansSerializedValue::String("Core.Value.Float") },
					{ "default", Vans::VansSerializedValue::Float(0.25) }
				})
			}), &error) ||
		!Vans::SetSerializedPointer(action, "/phases/execute/drivers/0/type",
			Vans::VansSerializedValue::String("Core.Driver.Graph"), &error) ||
		!Vans::SetSerializedPointer(action, "/phases/execute/drivers/0/inputs",
			Vans::VansSerializedValue::Object({
				{ "graph", Vans::VansSerializedValue::Object({
					{ "assetGuid", Vans::VansSerializedValue::String(graphRecord->guid.ToString()) }
				}) }
			}), &error)) return ExpectGAF(false, error.c_str());
	if (!Vans::SetSerializedPointer(action, "/phases/commit/operations",
		Vans::VansSerializedValue::Array({
			Vans::VansSerializedValue::Object({
				{ "type", Vans::VansSerializedValue::String("Gameplay.Effects.Apply") },
				{ "inputs", Vans::VansSerializedValue::Object({
					{ "asset", Vans::VansSerializedValue::Object({
						{ "assetGuid", Vans::VansSerializedValue::String(effectRecord->guid.ToString()) }
					}) },
					{ "removeOnEnd", Vans::VansSerializedValue::Bool(false) }
				}) }
			})
		}), &error)) return ExpectGAF(false, error.c_str());
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
	Vans::VansAssetObjectRepository sourceAssetObjects;
	if (!BootstrapGameplayMemory(database.All(), sourceAssetObjects, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansIOAudit::Reset();
	Vans::VansGameplayAssetLibrary sourceLibrary;
	if (!sourceLibrary.Load(database.All(), sourceAssetObjects, error) ||
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
	runtimeDependencies.contributors.push_back(
		Vans::VansMakeGameplayPrimitivesGAFContributor());
	const auto runtimeFakeServices = CreateTestFakeActionServices();
	runtimeDependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Test.SourceRuntime", runtimeFakeServices));
	if (!gameplayRuntime.Initialize(database.All(), sourceAssetObjects,
		runtimeSettings, runtimeDependencies, error))
		return ExpectGAF(false, error.c_str());
	const auto sourceRuntimeIO = Vans::VansIOAudit::Snapshot();
	if (!ExpectGAF(std::none_of(sourceRuntimeIO.begin(), sourceRuntimeIO.end(),
		[](const Vans::VansIOEvent& event)
		{
			return event.operation == Vans::VansIOOperation::Read ||
				event.operation == Vans::VansIOOperation::ReadRange;
		}), "GAF source library/runtime read asset data from disk after memory bootstrap"))
		return false;
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
	activation.context.SetEntity(Vans::VansActionContextSlots::Owner, owner);
	activation.context.SetEntity(Vans::VansActionContextSlots::Instigator, owner);
	activation.context.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, owner);
	const Vans::VansActionResult activationResult = runtimeHost->Activate(activation);
	if (!ExpectGAF(activationResult && runtimeHost->Query(activationResult.action).has_value(),
		"Scene ActionHost could not activate its configured Action")) return false;
	Vans::VansActionSystem actionSystem(gameplayRuntime);
	const Vans::VansActionHostRef actionHostRef{ owner };
	Vans::VansActionContext apiContext;
	apiContext.SetEntity(Vans::VansActionContextSlots::Owner, owner);
	apiContext.SetEntity(Vans::VansActionContextSlots::Instigator, owner);
	const auto apiReport = actionSystem.CanActivate(actionHostRef,
		runtimeHost->GrantedActions().front().handle, apiContext);
	const auto apiViews = actionSystem.QueryActive({ actionHostRef });
	const auto apiInspection = actionSystem.Inspect({ actionHostRef, activationResult.action });
	if (!ExpectGAF(apiReport.allowed && apiViews.size() == 1 && apiInspection &&
		apiInspection->instance.handle == activationResult.action,
		"Public ActionSystem API did not validate, query, and inspect the live Host")) return false;
	const Vans::VansActionResult isolatedAction = runtimeHost->Activate(activation);
	if (!ExpectGAF(isolatedAction && isolatedAction.action != activationResult.action,
		"GAF Timeline isolation test could not create two concurrent instances")) return false;
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
			{ "valueType", Vans::VansSerializedValue::String("Float") },
			{ "actionScope", Vans::VansSerializedValue::String("HostQuery") }
		}), { Vans::VansTimelineChannel{
			"gaf-parameter-channel", "value", Vans::VansTimelineValueType::Float,
			Vans::VansTimelineExtrapolation::None, Vans::VansTimelineExtrapolation::None,
			{ { "gaf-parameter-key", 0, 0.75f,
				Vans::VansTimelineInterpolation::Constant } } } });
	addTimelineTrack("Action.Window", 1, 1,
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String("Gameplay.Contract.Root") },
			{ "window", Vans::VansSerializedValue::String("Attack") },
			{ "payload", emptyPayload() },
			{ "actionScope", Vans::VansSerializedValue::String("HostQuery") }
		}));
	addTimelineTrack("Action.Event", 2, 1,
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String("Gameplay.Contract.Root") },
			{ "event", Vans::VansSerializedValue::String("Timeline.Contract.Event") },
			{ "payload", emptyPayload() },
			{ "actionScope", Vans::VansSerializedValue::String("HostQuery") }
		}));
	addTimelineTrack("Action.Marker", 2, 1,
		Vans::VansSerializedValue::Object({
			{ "action", Vans::VansSerializedValue::String("Gameplay.Contract.Root") },
			{ "marker", Vans::VansSerializedValue::String("Contract") },
			{ "payload", emptyPayload() },
			{ "actionScope", Vans::VansSerializedValue::String("HostQuery") }
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
			{ "intensity", Vans::VansSerializedValue::Float(1.0) },
			{ "actionScope", Vans::VansSerializedValue::String("HostQuery") }
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
		if (kind == Vans::VansTimelineSessionKind::Action)
			desc.scope = Vans::VansMakeExactActionTimelineScope(activationResult.action);
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
	Vans::VansSerializedValue isolatedTimelineValue;
	if (!ExpectGAF(actionTimeline &&
		runtimeHost->ReadVariable(activationResult.action, timelineVariable, timelineValue, error) &&
		runtimeHost->ReadVariable(isolatedAction.action, timelineVariable, isolatedTimelineValue, error) &&
		timelineValue.kind == Vans::VansSerializedValue::Kind::Float &&
		std::abs(timelineValue.floatValue - 0.75) < 0.0001 &&
		isolatedTimelineValue.kind == Vans::VansSerializedValue::Kind::Float &&
		std::abs(isolatedTimelineValue.floatValue - 0.25) < 0.0001,
		"ExactAction Timeline scope modified the wrong concurrent Action")) return false;
	const auto actionTimelineState = timelineSessions.Query(actionTimeline);
	if (!ExpectGAF(actionTimelineState &&
		actionTimelineState->state != Vans::VansTimelinePlayerState::Error,
		"Action Timeline event/window/marker/sub-action outputs failed")) return false;
	if (!timelineSessions.Release(actionTimeline) ||
		!runtimeHost->ReadVariable(activationResult.action, timelineVariable, timelineValue, error) ||
		!ExpectGAF(timelineValue.kind == Vans::VansSerializedValue::Kind::Float &&
			std::abs(timelineValue.floatValue - 0.25) < 0.0001,
			"Action Timeline parameter did not restore on session release")) return false;
	const Vans::VansTimelineSessionHandle hostQueryTimeline =
		runTimelineSession(Vans::VansTimelineSessionKind::Component);
	if (!ExpectGAF(hostQueryTimeline &&
		runtimeHost->ReadVariable(activationResult.action, timelineVariable, timelineValue, error) &&
		runtimeHost->ReadVariable(isolatedAction.action, timelineVariable, isolatedTimelineValue, error) &&
		std::abs(timelineValue.floatValue - 0.75) < 0.0001 &&
		std::abs(isolatedTimelineValue.floatValue - 0.75) < 0.0001,
		"Explicit HostQuery Timeline scope did not preserve broadcast behavior")) return false;
	if (!timelineSessions.Release(hostQueryTimeline))
		return ExpectGAF(false, "HostQuery Timeline session could not be released");
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
	Vans::VansAssetObjectRepository cookedAssetObjects;
	if (!BootstrapGameplayMemory(packagedRecords, cookedAssetObjects, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansIOAudit::Reset();
	Vans::VansGameplayAssetLibrary cookedLibrary;
	if (!cookedLibrary.Load(packagedRecords, cookedAssetObjects, error) ||
		!ExpectGAF(cookedLibrary.AssetCount() == sourceLibrary.AssetCount() &&
			cookedLibrary.ResolveAction(actionRecord->guid.ToString()) != nullptr,
			"GAF cooked asset library does not match source-mode resolution")) return false;
	const auto cookedRuntimeIO = Vans::VansIOAudit::Snapshot();
	if (!ExpectGAF(std::none_of(cookedRuntimeIO.begin(), cookedRuntimeIO.end(),
		[](const Vans::VansIOEvent& event)
		{
			return event.operation == Vans::VansIOOperation::Read ||
				event.operation == Vans::VansIOOperation::ReadRange;
		}), "GAF cooked library read packaged data from disk after memory bootstrap"))
		return false;
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
	previousAction.context.SetEntity(VansActionContextSlots::Owner, owner);
	previousAction.context.randomSeed = 7;
	previousAction.context.SetSerialized(VansActionContextSlots::Payload,
		VansSerializedValue::Object({
		{ "mode", VansSerializedValue::String("debug") }
	}));
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
	previousAction.resources.push_back({ { { 3, 1 } }, "Cue", "ChargeLoop", {} });
	previousAction.resourceCount = previousAction.resources.size();
	previousAction.executor.executor = "ExecutionGraph";
	previousAction.executor.activeNodes = { "Acquire" };

	VansActionHostDebugSnapshot previousHost;
	previousHost.owner = owner;
	previousHost.enabled = true;
	previousHost.tags.push_back({ VansMakeStableId<VansGameplayTagIdTag>("State.Ready"), 1 });
	previousHost.attributes.push_back({ health, 100.0, 100.0 });
	previousHost.effects.push_back({ { { 4, 1 } },
		VansMakeStableId<VansEffectIdTag>("Effect.Debug"), 9, 2.0, 0.5, 2, 1 });
	VansGrantedActionSpecSnapshot grant;
	grant.handle = previousAction.sourceSpec;
	grant.action = actionId;
	grant.extensions = {
		{ "Core.Level", VansSerializedValue::Object({
			{ "value", VansSerializedValue::Float(2.0) } }) },
		{ "Gameplay.Input.Binding", VansSerializedValue::Object({
			{ "binding", VansSerializedValue::String("Primary") } }) },
		{ "Gameplay.Tags.Dynamic", VansSerializedValue::Object({
			{ "tags", VansSerializedValue::Array({
				VansSerializedValue::String("Grant.Debug") }) } }) },
		{ "Gameplay.Charges", VansSerializedValue::Object({
			{ "count", VansSerializedValue::Int(3) } }) },
		{ "Core.Grant.Lifetime", VansSerializedValue::Object({
			{ "policy", VansSerializedValue::String("Persistent") } }) }
	};
	grant.source = 91;
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
	currentAction.error = VansActionError::Execution;
	currentAction.correlationId = 8;
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
	errorBreakpoint.error = VansActionError::Execution;
	add(errorBreakpoint);
	VansActionBreakpoint attribute;
	attribute.kind = VansActionBreakpointKind::Attribute;
	attribute.attribute = health;
	attribute.comparison = VansActionBreakpointComparison::Less;
	attribute.value = 90.0;
	add(attribute);
	const auto hits = breakpoints.Evaluate(previous, current);
	if (!ExpectGAF(hits.size() == 6,
		"GAF debugger did not edge-trigger state/node/event/window/error/attribute breakpoints"))
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
	std::string compilerError;
	Vans::VansGameplayAssetCompilerRegistry coreCompilers;
	if (!Vans::VansRegisterCoreGameplayAssetCompilers(coreCompilers, compilerError))
		return ExpectGAF(false, compilerError.c_str());
	if (!ExpectGAF(!Vans::VansRegisterCoreGameplayAssetCompilers(
		coreCompilers, compilerError),
		"GAF asset Compiler registry accepted duplicate asset types")) return false;
	if (!coreCompilers.Seal(compilerError)) return ExpectGAF(false, compilerError.c_str());
	const Vans::VansGameplayCookResult cameraCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::CameraRigProfile,
		schemas.CreateDefault(Vans::VansAssetType::CameraRigProfile));
	if (!ExpectGAF(cameraCook &&
		!Vans::VansGameplayAssetCompiler::Compile(cameraCook.asset, coreCompilers),
		"GAF asset compilation did not reject a missing Camera contributor")) return false;
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
		configuration.allowlist.modules.count("Core") == 1 &&
		configuration.allowlist.capabilities.count("Targeting.Filter.TagQuery") == 0 &&
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
	if (!Vans::SetSerializedPointer(externalActionTemplate, "/metadata/displayName",
		Vans::VansSerializedValue::String("External Action Template"), &error) ||
		!Vans::VansGameplayAssetStorage::SaveSourceAtomic(
			templateProject / "GAFTemplates/Action.vaction",
			externalActionTemplate, error, &externalTemplateConfiguration))
		return ExpectGAF(false, error.c_str());
	Vans::VansGAFProjectConfiguration loadedExternalTemplates;
	if (!Vans::VansGAFProjectConfiguration::LoadForProject(templateProject, sourceRoot,
		loadedExternalTemplates, error)) return ExpectGAF(false, error.c_str());
	const Vans::VansSerializedValue* externalMetadata = Vans::FindSerializedPointer(
		loadedExternalTemplates.templates.at("ActionDefinition"), "/metadata");
	if (!ExpectGAF(externalMetadata && Vans::ReadSerializedStringField(
		*externalMetadata, "displayName") ==
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
	Vans::VansAssetObjectRepository cameraAssetObjects;
	if (!BootstrapGameplayMemory(cameraDatabase.All(), cameraAssetObjects, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGameplayAssetLibrary cameraAssets;
	Vans::VansGAFTypeRegistry cameraTypes;
	Vans::VansGAFSchemaRegistry cameraSchemas;
	Vans::VansGameplayAssetCompilerRegistry cameraCompilers;
	if (!Vans::VansRegisterCoreGAFTypes(cameraTypes, error) ||
		!Vans::VansRegisterGameplayPrimitiveGAFTypes(cameraTypes, error) ||
		!Vans::VansRegisterTimelineGAFTypes(cameraTypes, error) ||
		!cameraTypes.Seal(error)) return ExpectGAF(false, error.c_str());
	cameraSchemas.BindTypes(cameraTypes);
	if (!Vans::VansRegisterCoreGAFSchemas(cameraSchemas, error) ||
		!Vans::VansRegisterGameplayPrimitiveGAFSchemas(cameraSchemas, error) ||
		!Vans::VansRegisterTimelineGAFSchemas(cameraSchemas, error) ||
		!cameraSchemas.Seal(error) ||
		!Vans::VansRegisterDefaultGameplayAssetCompilers(cameraCompilers, error) ||
		!cameraCompilers.Seal(error)) return ExpectGAF(false, error.c_str());
	if (!rigRecord || !shakeRecord ||
		!cameraAssets.Load(cameraDatabase.All(), cameraAssetObjects, {},
			cameraSchemas, cameraCompilers, error) ||
		!ExpectGAF(cameraAssets.ExtensionAssets(
				Vans::VansCameraRigGameplayAssetType).size() == 1 &&
			cameraAssets.ExtensionAssets(
				Vans::VansCameraShakeGameplayAssetType).size() == 1 &&
			cameraAssets.ResolveExtensionAssetAs<Vans::VansCameraRigDefinition>(
				rigRecord->guid.ToString(), Vans::VansCameraRigGameplayAssetType) &&
			cameraAssets.ResolveExtensionAssetAs<Vans::VansCameraShakeDefinition>(
				shakeRecord->guid.ToString(), Vans::VansCameraShakeGameplayAssetType),
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
	if (!Vans::SetSerializedPointer(action, "/phases/commit/operations",
		Vans::VansSerializedValue::Array({
			Vans::VansSerializedValue::Object({
				{ "type", Vans::VansSerializedValue::String("Gameplay.Cooldown.Apply") },
				{ "inputs", Vans::VansSerializedValue::Object({
					{ "duration", Vans::VansSerializedValue::Float(0.5) },
					{ "tag", Vans::VansSerializedValue::String("Cooldown.Primary") }
				}) }
			}),
			Vans::VansSerializedValue::Object({
				{ "type", Vans::VansSerializedValue::String("Gameplay.Cooldown.Apply") },
				{ "inputs", Vans::VansSerializedValue::Object({
					{ "duration", Vans::VansSerializedValue::Float(1.0) },
					{ "tag", Vans::VansSerializedValue::String("Cooldown.Shared") }
				}) }
			})
		}), &error)) return ExpectGAF(false, error.c_str());
	const auto diagnostics = Vans::VansAssetDocumentTypeRegistry::Get().ValidateBeforeSave(
		Vans::VansAssetType::ActionDefinition, "probe.vaction", action);
	for (const auto& diagnostic : diagnostics)
		if (diagnostic.severity == Vans::VansAssetDocumentDiagnosticSeverity::Error)
			return ExpectGAF(false, diagnostic.message.c_str());
	const Vans::VansGameplayCookResult first = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionDefinition, action);
	if (!ExpectGAF(first && first.asset.contentHash != 0,
		"GAF current ActionDefinition did not Cook")) return false;
	Vans::VansSerializedValue unknownAction = action;
	Vans::SetSerializedObjectField(unknownAction, "unknownFutureField",
		Vans::VansSerializedValue::String("must-be-rejected"));
	if (!ExpectGAF(!Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionDefinition, unknownAction),
		"GAF current ActionDefinition accepted an unregistered root field")) return false;
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
	changedCookPolicy.allowlist.capabilities.insert("Targeting.Custom.Contract");
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
		Vans::VansSerializedValue::String("Core.Graph.Invoke"), &error) ||
		!Vans::SetSerializedPointer(cameraGraph, "/nodes/0/properties/capability",
			Vans::VansSerializedValue::String("Camera.Action"), &error)) return false;
	Vans::VansGAFProjectConfiguration blockedCameraCapability = configuration;
	blockedCameraCapability.allowlist.capabilities.erase("Camera.Action");
	const Vans::VansGameplayCookResult blockedCameraCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionGraph, cameraGraph,
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &blockedCameraCapability);
	Vans::VansSerializedValue timelineAction = action;
	if (!Vans::SetSerializedPointer(timelineAction, "/dependencies/capabilities",
		Vans::VansSerializedValue::Array({
			Vans::VansSerializedValue::String("Timeline.Action") }), &error)) return false;
	Vans::VansGAFProjectConfiguration blockedTimelineCapability = configuration;
	blockedTimelineCapability.allowlist.capabilities.erase("Timeline.Action");
	const Vans::VansGameplayCookResult blockedTimelineCook = Vans::VansGameplayAssetStorage::Cook(
		Vans::VansAssetType::ActionDefinition, timelineAction,
		Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &blockedTimelineCapability);
	if (!ExpectGAF(configuredCook && changedPolicyCook &&
		configuredCook.asset.contentHash != changedPolicyCook.asset.contentHash &&
		!blockedGraphCook && std::any_of(blockedGraphCook.diagnostics.begin(),
			blockedGraphCook.diagnostics.end(), [](const auto& diagnostic)
			{ return diagnostic.code == "GAF-PROJECT-NODE-ALLOWLIST"; }) &&
		!blockedCameraCook && std::any_of(blockedCameraCook.diagnostics.begin(),
			blockedCameraCook.diagnostics.end(), [](const auto& diagnostic)
			{ return diagnostic.code == "GAF-PROJECT-CAPABILITY-ALLOWLIST"; }) &&
		!blockedTimelineCook && std::any_of(blockedTimelineCook.diagnostics.begin(),
			blockedTimelineCook.diagnostics.end(), [](const auto& diagnostic)
			{ return diagnostic.code == "GAF-PROJECT-CAPABILITY-ALLOWLIST"; }),
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
		Vans::VansGameplayAssetCompilerRegistry compilers;
		if (!Vans::VansRegisterDefaultGameplayAssetCompilers(compilers, error) ||
			!compilers.Seal(error)) return ExpectGAF(false, error.c_str());
		const Vans::VansGameplayCompileResult compiled =
			Vans::VansGameplayAssetCompiler::Compile(cooked.asset, compilers);
		if (!ExpectGAF(static_cast<bool>(compiled) && compiled.asset.assetType == assetCase.type &&
			compiled.asset.contentHash == cooked.asset.contentHash,
			"GAF 模板无法编译为强类型运行时资产")) return false;
	}
	const auto compiledAction = Vans::VansGameplayAssetCompiler::Compile(first.asset);
	const auto* actionDefinition = std::get_if<std::shared_ptr<const Vans::VansCompiledActionDefinition>>(
		&compiledAction.asset.data);
	const std::size_t cooldownOperationCount = actionDefinition && *actionDefinition
		? static_cast<std::size_t>(std::count_if(
			(*actionDefinition)->program.commit.operations.begin(),
			(*actionDefinition)->program.commit.operations.end(), [](const auto& operation)
			{ return operation.type == "Gameplay.Cooldown.Apply"; })) : 0;
	if (!ExpectGAF(compiledAction && actionDefinition && *actionDefinition &&
		(*actionDefinition)->id == Vans::VansMakeStableId<Vans::VansActionIdTag>("Gameplay.NewAction") &&
		cooldownOperationCount == 2,
		"ActionDefinition 强类型编译结果错误")) return false;

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
	if (!editor.SetValue("/metadata/category", Vans::VansSerializedValue::String("Combat")) ||
		!editor.AppendArrayItem("/metadata/labels", Vans::VansSerializedValue::String("Action.Combat")) ||
		!editor.DuplicateArrayItem("/metadata/labels", 0))
		return ExpectGAF(false, "GAF 编辑器字段或数组命令执行失败");
	if (!ExpectGAF(editor.Document()->sourceDocument.IsDirty() &&
		Vans::FindSerializedPointer(editor.Snapshot(), "/metadata/labels")->arrayItems.size() == 2 &&
		!editor.DiffAgainst(editorBaseline).empty() && editor.PreviewCook(),
		"GAF 编辑器修改、Diff 或 Cook 预览错误")) return false;
	if (!editor.Undo() ||
		!ExpectGAF(Vans::FindSerializedPointer(editor.Snapshot(), "/metadata/labels")->arrayItems.size() == 1,
			"GAF 编辑器 Undo 未恢复数组命令") || !editor.Redo() ||
		!editor.RemoveArrayItem("/metadata/labels", 1) || !editor.ResetField("/metadata/category")) return false;
	const Vans::VansSerializedValue editorSnapshot = editor.Snapshot();
	const Vans::VansSerializedValue* editorMetadata =
		Vans::FindSerializedPointer(editorSnapshot, "/metadata");
	if (!ExpectGAF(editorMetadata &&
		Vans::ReadSerializedStringField(*editorMetadata, "category") == "Gameplay",
		"GAF 编辑器字段默认值重置错误")) return false;
	const auto* actionSchema = schemas.Resolve(Vans::VansAssetType::ActionDefinition);
	const auto operationsSchema = actionSchema ? std::find_if(actionSchema->fields.begin(),
		actionSchema->fields.end(), [](const Vans::VansGameplayPropertySchema& field)
		{
			return field.path == "/phases/commit/operations";
		}) : std::vector<Vans::VansGameplayPropertySchema>::const_iterator{};
	if (!ExpectGAF(actionSchema && operationsSchema != actionSchema->fields.end() &&
		operationsSchema->children.size() == 2 && operationsSchema->hasArrayElement,
		"GAF Action commit phase does not expose typed Operation records")) return false;
	const auto* effectSchema = schemas.Resolve(Vans::VansAssetType::GameplayEffect);
	const auto extensionsSchema = effectSchema ? std::find_if(effectSchema->fields.begin(),
		effectSchema->fields.end(), [](const Vans::VansGameplayPropertySchema& field)
		{
			return field.path == "/extensions";
		}) : std::vector<Vans::VansGameplayPropertySchema>::const_iterator{};
	if (!ExpectGAF(effectSchema && extensionsSchema != effectSchema->fields.end() &&
		extensionsSchema->children.size() == 2 && extensionsSchema->hasArrayElement,
		"GAF Effect schema does not expose typed extension records"))
		return false;
	if (!editor.AppendArrayItem("/phases/commit/operations",
		operationsSchema->arrayElementDefault) ||
		!editor.SetValue("/phases/commit/operations/0/type",
			Vans::VansSerializedValue::String("Gameplay.Attributes.Consume")) ||
		!editor.SetValue("/phases/commit/operations/0/inputs",
			Vans::VansSerializedValue::Object({
				{ "attribute", Vans::VansSerializedValue::String("Resource.Mana") },
				{ "amount", Vans::VansSerializedValue::Float(10.0) }
			})))
		return ExpectGAF(false, "GAF typed Operation authoring failed");
	if (!ExpectGAF(static_cast<bool>(editor.PreviewCook()),
		"GAF typed Operation did not pass current-schema Cook validation")) return false;
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
	if (!ExpectGAF(graphNodeCatalog.size() ==
			Vans::VansBuiltInActionGraphNodeDescriptors().size() &&
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
	Vans::VansAssetObjectRepository assetObjects;
	if (!BootstrapGameplayMemory(database.All(), assetObjects, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGameplayRuntime runtime;
	Vans::VansGameplayRuntimeDependencies dependencies;
	dependencies.contributors.push_back(
		Vans::VansMakeGameplayPrimitivesGAFContributor());
	const auto fakeServices = CreateTestFakeActionServices();
	dependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Test.SampleRuntime", fakeServices));
	dependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Gameplay.Camera", {}, Vans::VansRegisterCameraGameplayAssetCompilers,
		Vans::VansRegisterCameraGameplayAssetSchemas));
	if (!runtime.Initialize(database.All(), assetObjects,
		configuration.settings, dependencies, error))
		return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(runtime.Assets().AssetCount() == 37 &&
		runtime.Assets().Actions().ActionCount() == 10 &&
		runtime.Assets().Cues().size() == 5 &&
		runtime.Assets().ExtensionAssets(
			Vans::VansCameraRigGameplayAssetType).size() == 2 &&
		runtime.Assets().ExtensionAssets(
			Vans::VansCameraShakeGameplayAssetType).size() == 2,
		"GAF sample library typed asset counts are incomplete")) return false;

	const auto fire = runtime.Assets().ResolveAction("Gameplay.Sample.Shooter.Fire");
	const auto finisher = runtime.Assets().ResolveAction("Gameplay.Sample.Melee.Finisher");
	const auto lightAttack = runtime.Assets().ResolveAction("Gameplay.Sample.Melee.LightAttack");
	const auto* fireTargeting = fire ? FindCompiledActionRecord(
		fire->program.activate.operations, "Gameplay.Targeting.Resolve") : nullptr;
	const auto* fireCue = fire ? FindCompiledActionRecord(
		fire->program.execute.operations, "Gameplay.Cue.Emit") : nullptr;
	const Vans::VansSerializedValue* fireCueAssets = fireCue
		? Vans::FindObjectField(fireCue->inputs, "assets") : nullptr;
	const auto* lightAttackCombo = lightAttack ? FindCompiledActionRecord(
		lightAttack->program.transitions, "Core.Transition.Combo") : nullptr;
	if (!ExpectGAF(fire && finisher && lightAttack &&
		CompiledActionReference(fireTargeting, "asset") ==
			"Targeting.Sample.PrimaryEntity" &&
		fireCueAssets && fireCueAssets->kind == Vans::VansSerializedValue::Kind::Array &&
		fireCueAssets->arrayItems.size() == 1 &&
		fireCueAssets->arrayItems.front().kind == Vans::VansSerializedValue::Kind::String &&
		fireCueAssets->arrayItems.front().stringValue == "Cue.Sample.Shooter.Fire" &&
		CompiledActionReference(lightAttackCombo, "target") == finisher->name,
		"GAF sample GUID references did not link to runtime stable IDs")) return false;

	Vans::VansGameplayActionHostSetup setup;
	setup.actionSets = {
		"00000000-0000-4000-8000-000000002005",
		"00000000-0000-4000-8000-000000003006",
		"00000000-0000-4000-8000-000000004007",
		"00000000-0000-4000-8000-000000005301"
	};
	AddHostTagInitializer(setup, "State.HasKey");
	AddHostAttributeInitializer(setup, "Attribute.Ammo", 10.0);
	AddHostAttributeInitializer(setup, "Attribute.Stamina", 20.0);
	AddHostAttributeInitializer(setup, "Attribute.WeaponHeat", 0.0);
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
	context.SetEntity(Vans::VansActionContextSlots::Owner, owner);
	context.SetEntity(Vans::VansActionContextSlots::Instigator, owner);
	context.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, target);
	context.randomSeed = 12345;
	const Vans::VansAttributeId requirementAmmo =
		Vans::VansMakeStableId<Vans::VansAttributeIdTag>("Attribute.Ammo");
	Vans::VansActionContext missingTargetContext = context;
	missingTargetContext.Remove(Vans::VansActionContextSlots::PrimaryTarget);
	if (!ExpectGAF(host->CanActivateAction(fire->id, missingTargetContext).error ==
		Vans::VansActionError::Rejected,
		"GAF TargetData commit requirement accepted a missing target")) return false;
	if (!host->Attributes().AddBase(requirementAmmo, -30.0)) return false;
	if (!ExpectGAF(host->CanActivateAction(fire->id, context).error ==
		Vans::VansActionError::Rejected,
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
	AddHostAttributeInitializer(revokeSetup, "Attribute.Ammo", 10.0);
	AddHostAttributeInitializer(revokeSetup, "Attribute.Stamina", 20.0);
	AddHostAttributeInitializer(revokeSetup, "Attribute.WeaponHeat", 0.0);
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

	Vans::VansAssetObjectRepository assetObjects;
	if (!BootstrapGameplayMemory(database.All(), assetObjects, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGameplayRuntime runtime;
	Vans::VansGameplayRuntimeDependencies windowRuntimeDependencies;
	windowRuntimeDependencies.contributors.push_back(
		Vans::VansMakeGameplayPrimitivesGAFContributor());
	windowRuntimeDependencies.contributors.push_back(
		MakeProjectSchemaContributor(configuration));
	windowRuntimeDependencies.contributors.push_back(MakeTestTimelineContributor());
	if (!runtime.Initialize(database.All(), assetObjects, configuration.settings,
		windowRuntimeDependencies, error))
		return ExpectGAF(false, error.c_str());
	const auto action = runtime.Assets().ResolveAction("Gameplay.DemoHall.Window.Break");
	const auto actionSet = runtime.Assets().ResolveActionSet(
		"4d408a1b-97bc-4453-b3b0-c8e1426b7e1b");
	const auto* graphDriver = action ? FindCompiledActionRecord(
		action->program.execute.drivers, "Core.Driver.Graph") : nullptr;
	const auto* timelineDriver = action ? FindCompiledActionRecord(
		action->program.execute.drivers, "Timeline.Driver.Session") : nullptr;
	if (!ExpectGAF(action && actionSet && action->executionGraph &&
		CompiledActionReference(graphDriver, "graph") ==
			"d33889f3-b988-4aab-9f09-1868151d18d3" && !timelineDriver,
		"DemoHall window-break Action must not duplicate its scene-owned Timeline session")) return false;

	const Vans::VansEntityHandle owner{ 701, 1 };
	const Vans::VansEntityHandle player{ 702, 1 };
	Vans::VansGameplayActionHostSetup setup;
	setup.actionSets.push_back("4d408a1b-97bc-4453-b3b0-c8e1426b7e1b");
	AddHostTagInitializer(setup, "Target.Interactable.Window");
	const auto host = runtime.CreateHost(owner, setup, error);
	if (!ExpectGAF(host && host->GrantedActions().size() == 1,
		"DemoHall window ActionHost did not receive its ActionSet")) return false;

	Vans::VansActionContext context;
	context.SetEntity(Vans::VansActionContextSlots::Owner, owner);
	context.SetEntity(Vans::VansActionContextSlots::Instigator, player);
	context.SetEntity(Vans::VansActionContextSlots::Source, player);
	context.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, owner);
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

bool TestGAFDemoHallPlayerAttackContract()
{
	namespace fs = std::filesystem;
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 6 && !fs::exists(workspace / "DemoHallProject"); ++depth)
		workspace = workspace.parent_path();
	const fs::path projectRoot = workspace / "DemoHallProject";
	const fs::path sourceAssets = projectRoot / "Assets/GAF/PlayerAttack";
	const fs::path responseAssets = projectRoot / "Assets/GAF/WhisperCombat";
	if (!ExpectGAF(fs::is_directory(sourceAssets),
		"DemoHall player-attack GAF assets are missing") ||
		!ExpectGAF(fs::is_directory(responseAssets),
		"DemoHall Whisper hit-response GAF assets are missing")) return false;

	Vans::VansGAFProjectConfiguration configuration;
	std::string error;
	if (!Vans::VansGAFProjectConfiguration::LoadForProject(
		projectRoot, workspace / "ForestEngine/ForestEngine", configuration, error))
		return ExpectGAF(false, error.c_str());

	const fs::path temporaryRoot =
		fs::temp_directory_path() / "ForestGAFDemoHallPlayerAttackContract";
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
	const fs::path assetsRoot = temporaryRoot / "Assets/PlayerAttack";
	fs::create_directories(assetsRoot, filesystemError);
	fs::copy(sourceAssets, assetsRoot,
		fs::copy_options::recursive | fs::copy_options::overwrite_existing, filesystemError);
	if (!ExpectGAF(!filesystemError,
		"DemoHall player-attack assets could not be copied for validation")) return false;
	fs::copy(responseAssets, assetsRoot / "WhisperCombat",
		fs::copy_options::recursive | fs::copy_options::overwrite_existing, filesystemError);
	if (!ExpectGAF(!filesystemError,
		"DemoHall Whisper response assets could not be copied for validation")) return false;
	for (const char* fileName : { "DemoHallTags.vtagtree", "DemoHallTags.vtagtree.meta" })
	{
		fs::copy_file(projectRoot / "Assets/GAF/WindowBreak" / fileName,
			assetsRoot / fileName, fs::copy_options::overwrite_existing, filesystemError);
		if (!ExpectGAF(!filesystemError,
			"DemoHall player-attack TagTree could not be copied for validation")) return false;
	}

	Vans::VansAssetDatabase database(temporaryRoot / "Assets", temporaryRoot / "Library/Artifacts");
	const Vans::VansAssetScanResult scan =
		database.Scan(Vans::VansAssetOperationPolicy::Authoring());
	if (!ExpectGAF(scan && database.All().size() == 16,
		"DemoHall attack, response, and TagTree did not scan as sixteen GAF assets")) return false;
	for (const Vans::VansAssetRecord& record : database.All())
	{
		Vans::VansSerializedValue source;
		const fs::path sourcePath = fs::is_regular_file(record.authoringPath)
			? record.authoringPath : record.sourcePath;
		if (!Vans::VansGameplayAssetStorage::LoadSource(sourcePath, source, error))
			return ExpectGAF(false, error.c_str());
		const Vans::VansGameplayCookResult cooked = Vans::VansGameplayAssetStorage::Cook(
			record.type, source, Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &configuration);
		if (!cooked)
		{
			std::cerr << "[GAF] Cook source: " << sourcePath.string()
				<< " error: " << cooked.error << '\n';
			for (const auto& diagnostic : cooked.diagnostics)
				std::cerr << "[GAF] " << diagnostic.code << " " << diagnostic.fieldPath
					<< ": " << diagnostic.message << '\n';
			return ExpectGAF(false,
				"DemoHall player-attack asset failed configured GAF Cook");
		}
	}

	Vans::VansGameplayRuntimeDependencies runtimeDependencies;
	runtimeDependencies.contributors.push_back(
		Vans::VansMakeGameplayPrimitivesGAFContributor());
	runtimeDependencies.contributors.push_back(MakeProjectSchemaContributor(configuration));
	runtimeDependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Gameplay.Combat", { std::make_shared<Vans::VansFakeActionService>(
			Vans::VansCombatActionCapability()) }));
	runtimeDependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Gameplay.Animation", { std::make_shared<Vans::VansFakeActionService>(
			Vans::VansAnimationActionCapability()) }));
	runtimeDependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Gameplay.Navigation", { std::make_shared<Vans::VansFakeActionService>(
			Vans::VansNavigationActionCapability()) }));
	Vans::VansAssetObjectRepository assetObjects;
	if (!BootstrapGameplayMemory(database.All(), assetObjects, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansGameplayRuntime runtime;
	if (!runtime.Initialize(database.All(), assetObjects,
		configuration.settings, runtimeDependencies, error))
		return ExpectGAF(false, error.c_str());
	const auto crowbarAttack = runtime.Assets().ResolveAction(
		"Gameplay.DemoHall.Player.Attack.Crowbar");
	const auto crowbarTakedown = runtime.Assets().ResolveAction(
		"Gameplay.DemoHall.Player.Attack.Takedown.Crowbar.Attacker");
	const auto whisperHitReact = runtime.Assets().ResolveAction(
		"Gameplay.DemoHall.Whisper.HitReact");
	const auto actionSet = runtime.Assets().ResolveActionSet(
		"4534ddf1-e858-468e-a1ab-9e8b98cf6129");
	const auto whisperActionSet = runtime.Assets().ResolveActionSet(
		"269218a3-6809-48f6-b055-97891161c303");
	if (!ExpectGAF(crowbarAttack && crowbarTakedown && whisperHitReact &&
		actionSet && whisperActionSet &&
		crowbarAttack->executionGraph && crowbarTakedown->executionGraph &&
		whisperHitReact->executionGraph &&
		!crowbarAttack->cancellable && !crowbarAttack->interruptible &&
		!crowbarTakedown->cancellable && !crowbarTakedown->interruptible &&
		crowbarAttack->concurrencyGroup == crowbarTakedown->concurrencyGroup,
		"DemoHall attack or hit-response Action links are incomplete")) return false;
	for (const std::string& capability : crowbarAttack->program.capabilities)
	{
		const Vans::VansActionServiceId required =
			Vans::VansMakeStableId<Vans::VansActionServiceIdTag>(capability);
		if (!runtime.Services().Resolve(required))
			std::cerr << "[GAF] Missing Crowbar service id=" << required.value << '\n';
	}
	const auto combatServiceId = Vans::VansMakeStableId<Vans::VansActionServiceIdTag>(
		"Service.Combat");
	if (!runtime.Services().Resolve(combatServiceId))
		std::cerr << "[GAF] Runtime did not register Service.Combat id="
			<< combatServiceId.value << '\n';

	const Vans::VansActionServiceCapability* combatCapability =
		FindTestActionCapability(
			Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Combat"));
	const Vans::VansActionServiceCapability* navigationCapability =
		FindTestActionCapability(
			Vans::VansMakeStableId<Vans::VansActionServiceIdTag>("Service.Navigation"));
	const auto hasCommand = [](const Vans::VansActionServiceCapability* capability,
		const char* stableName)
	{
		return capability && std::any_of(capability->commandSchemas.begin(),
			capability->commandSchemas.end(), [stableName](const auto& command)
			{ return command.stableName == stableName; });
	};
	if (!ExpectGAF(hasCommand(combatCapability, "Combat.BeginMeleeWindow") &&
		hasCommand(navigationCapability, "Navigation.BlockMovement"),
		"Standard GAF services do not expose melee windows and movement blocking")) return false;
	nlohmann::ordered_json attackGraphSource;
	nlohmann::ordered_json responseGraphSource;
	if (!Vans::VansJsonFileStorage::Read(
		sourceAssets / "CrowbarAttack.vactiongraph", attackGraphSource, error) ||
		!Vans::VansJsonFileStorage::Read(
			responseAssets / "WhisperHitReact.vactiongraph", responseGraphSource, error))
		return ExpectGAF(false, error.c_str());
	bool foundConfiguredMeleeWindow = false;
	for (const auto& node : attackGraphSource.value("nodes", nlohmann::ordered_json::array()))
	{
		const auto properties = node.value("properties", nlohmann::ordered_json::object());
		if (node.value("type", std::string{}) != "Core.Graph.Invoke" ||
			properties.value("operation", std::string{}) != "Combat.BeginMeleeWindow") continue;
		const auto inputs = properties.value("inputs", nlohmann::ordered_json::object());
		const auto literal = [&inputs](const char* name) -> nlohmann::ordered_json
		{
			const auto found = inputs.find(name);
			if (found == inputs.end() || !found->is_object() ||
				found->value("source", std::string{}) != "Literal") return {};
			return found->value("value", nlohmann::ordered_json{});
		};
		foundConfiguredMeleeWindow =
			literal("sourceBase") ==
				"928f6ad1-4aac-4b57-a244-8a021f518401" &&
			literal("sourceTip") ==
				"c28c047e-aebd-4668-8ca6-04de3f348402" &&
			literal("targetLayer") == "Enemy" &&
			literal("targetTag") == "Target.Character.Enemy" &&
			literal("responseAction") ==
				"Gameplay.DemoHall.Whisper.HitReact" &&
			std::abs(literal("startSeconds").get<float>() - 0.72f) < 0.001f &&
			std::abs(literal("endSeconds").get<float>() - 1.38f) < 0.001f &&
			std::abs(literal("sweepRadius").get<float>() - 0.18f) < 0.001f &&
			std::abs(literal("range").get<float>() - 2.2f) < 0.001f &&
			std::abs(literal("halfAngleDegrees").get<float>() - 57.5f) < 0.001f &&
			literal("maximumHits") == 4;
	}
	bool foundMovementBlock = false;
	bool foundHitAnimation = false;
	bool foundResponseWait = false;
	for (const auto& node : responseGraphSource.value("nodes", nlohmann::ordered_json::array()))
	{
		const auto properties = node.value("properties", nlohmann::ordered_json::object());
		const std::string command = properties.value("operation", std::string{});
		foundMovementBlock = foundMovementBlock || command == "Navigation.BlockMovement";
		if (command == "Animation.Play")
		{
			const auto inputs = properties.value("inputs", nlohmann::ordered_json::object());
			foundHitAnimation =
				inputs.value("clip", nlohmann::ordered_json::object()).value(
					"value", std::string{}) == "TakingDamage1" &&
				!inputs.value("loop", nlohmann::ordered_json::object()).value("value", true);
		}
		if (node.value("type", std::string{}) == "Action.Graph.Wait")
			foundResponseWait = std::abs(properties.value("seconds", 0.0f) - 1.333333f) < 0.001f;
	}
	if (!ExpectGAF(foundConfiguredMeleeWindow && foundMovementBlock &&
		foundHitAnimation && foundResponseWait,
		"DemoHall GAF assets lost the melee window or Whisper response configuration")) return false;
	if (!ExpectGAF(
		Vans::VansPointInsideMeleeSector(
			glm::vec3(0.8f, 1.0f, 1.2f), 0.35f, glm::vec3(0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f), 2.2f, 55.0f, 1.0f) &&
		!Vans::VansPointInsideMeleeSector(
			glm::vec3(0.0f, 0.0f, -1.5f), 0.2f, glm::vec3(0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f), 2.2f, 55.0f, 1.0f) &&
		Vans::VansContinuousWeaponPathIntersectsSphere(
			glm::vec3(-1.0f, 1.0f, 1.0f), glm::vec3(-1.0f, 1.0f, 2.0f),
			glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 1.0f, 2.0f),
			glm::vec3(0.0f, 1.0f, 1.5f), 0.15f),
		"Melee sector validation or cross-frame weapon sweep geometry regressed")) return false;

	const Vans::VansEntityHandle player{ 801, 1 };
	Vans::VansGameplayActionHostSetup setup;
	setup.actionSets.push_back("4534ddf1-e858-468e-a1ab-9e8b98cf6129");
	AddHostTagInitializer(setup, "Target.Character.Player");
	const auto host = runtime.CreateHost(player, setup, error);
	if (!host || host->GrantedActions().size() != 2)
		std::cerr << "[GAF] Player host error: " << error << " grants="
			<< (host ? host->GrantedActions().size() : 0) << '\n';
	if (!ExpectGAF(host && host->GrantedActions().size() == 2,
		"DemoHall player ActionHost did not receive both CloseCombat attacks")) return false;

	Vans::VansActionContext context;
	context.SetEntity(Vans::VansActionContextSlots::Owner, player);
	context.SetEntity(Vans::VansActionContextSlots::Instigator, player);
	context.SetEntity(Vans::VansActionContextSlots::Source, player);
	context.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, player);
	const Vans::VansActionResult first = host->ActivateAction(crowbarAttack->id, context);
	const Vans::VansActionResult blocked = host->ActivateAction(crowbarTakedown->id, context);
	if (!ExpectGAF(first && !blocked && host->ActiveActions().size() == 1,
		"DemoHall player attacks did not reject re-entry while an attack was active")) return false;
	runtime.TickEarly(2.6);
	const Vans::VansActionResult second = host->ActivateAction(crowbarTakedown->id, context);
	if (!ExpectGAF(second && host->ActiveActions().size() == 1,
		"Crowbar Takedown did not activate after Crowbar Attack completed")) return false;
	runtime.TickEarly(3.8);
	if (!ExpectGAF(host->ActiveActions().empty(),
		"DemoHall player attack Action Graphs did not complete at authored clip durations")) return false;

	const Vans::VansEntityHandle whisper{ 802, 1 };
	Vans::VansGameplayActionHostSetup whisperSetup;
	whisperSetup.actionSets.push_back("269218a3-6809-48f6-b055-97891161c303");
	AddHostTagInitializer(whisperSetup, "Target.Character.Enemy");
	const auto whisperHost = runtime.CreateHost(whisper, whisperSetup, error);
	Vans::VansActionContext responseContext;
	responseContext.SetEntity(Vans::VansActionContextSlots::Owner, whisper);
	responseContext.SetEntity(Vans::VansActionContextSlots::Instigator, player);
	responseContext.SetEntity(Vans::VansActionContextSlots::Source, player);
	responseContext.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, player);
	const Vans::VansActionResult responseResult = whisperHost
		? whisperHost->ActivateAction(whisperHitReact->id, responseContext)
		: Vans::VansActionResult{};
	if (!ExpectGAF(whisperHost && responseResult && whisperHost->ActiveActions().size() == 1,
		"Whisper hit response could not activate through its own GAF ActionHost")) return false;
	runtime.TickEarly(1.4);
	if (!ExpectGAF(whisperHost->ActiveActions().empty(),
		"Whisper hit response did not release its GAF resources after the configured animation"))
		return false;

	const std::vector<std::pair<fs::path, float>> clips = {
		{ projectRoot / "Assets/Animations/Combat/CloseCombat/A_Crowbar_Attack_Unreal_Take.vclip", 2.533333f },
		{ projectRoot / "Assets/Animations/Combat/CloseCombat/A_TD_Crowbar_Attacker_01_Unreal_Take.vclip", 3.766667f }
	};
	VansGraphics::Skeleton attackClipSkeleton;
	for (const auto& [path, duration] : clips)
	{
		VansGraphics::VansAnimationClip clip;
		VansGraphics::Skeleton skeleton;
		if (!VansGraphics::VansAnimationClipIO::Load(path.string(), clip, skeleton))
			return ExpectGAF(false, "DemoHall attack .vclip could not be loaded");
		auto root = skeleton.boneNameToIndex.find("root");
		if (!ExpectGAF(clip.rootMotion.enabled && clip.rootMotion.boneName == "root" &&
			clip.rootMotion.extractTranslation && clip.rootMotion.extractRotation &&
			root != skeleton.boneNameToIndex.end() &&
			root->second >= 0 && root->second < static_cast<int>(clip.boneKeyframes.size()) &&
			clip.boneKeyframes[root->second].size() > 1 &&
			std::abs(clip.duration - duration) < 0.001f,
			"DemoHall attack .vclip lost its UEFN root-motion contract")) return false;
		const auto& keys = clip.boneKeyframes[root->second];
		float maxTravel = 0.0f;
		for (const auto& key : keys)
		{
			const auto delta = key.position - keys.front().position;
			maxTravel = std::max(maxTravel,
				std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z));
		}
		if (!ExpectGAF(maxTravel > 1.0f,
			"DemoHall attack root-motion track contains no meaningful translation")) return false;

		float maxNonRootTranslation = 0.0f;
		float maxNonRootRotation = 0.0f;
		for (std::size_t boneIndex = 0; boneIndex < clip.boneKeyframes.size(); ++boneIndex)
		{
			if (static_cast<int>(boneIndex) == root->second || clip.boneKeyframes[boneIndex].size() < 2)
				continue;
			const auto& firstKey = clip.boneKeyframes[boneIndex].front();
			for (const auto& key : clip.boneKeyframes[boneIndex])
			{
				const glm::vec3 translationDelta = key.position - firstKey.position;
				maxNonRootTranslation = std::max(maxNonRootTranslation, glm::length(translationDelta));
				maxNonRootRotation = std::max(maxNonRootRotation,
					1.0f - std::abs(glm::dot(key.rotation, firstKey.rotation)));
			}
		}
		if (!ExpectGAF(maxNonRootTranslation > 0.01f || maxNonRootRotation > 0.0001f,
			"DemoHall attack .vclip contains root motion but no animated body pose")) return false;
		if (attackClipSkeleton.bones.empty())
			attackClipSkeleton = skeleton;
	}

	nlohmann::ordered_json animator;
	if (!Vans::VansJsonFileStorage::Read(
		projectRoot / "Assets/MotionMatchDataBase/UEFN_Mannequin.vanimator", animator, error))
		return ExpectGAF(false, error.c_str());
	bool foundAttackSet = false;
	for (const auto& graphSet : animator.value("graphSets", nlohmann::ordered_json::array()))
		foundAttackSet = foundAttackSet ||
			graphSet.value("id", std::string{}) == "graph-set-attack";
	bool attackGraphIsNonMotionMatching = false;
	bool attackStatesUseRootMotion = false;
	int closeCombatClipRefs = 0;
	for (const auto& clipRef : animator.value("clips", nlohmann::ordered_json::array()))
	{
		if (clipRef.value("name", std::string{}).rfind("Attack_", 0) != 0) continue;
		const std::string pathHint = clipRef.value("asset", nlohmann::ordered_json::object())
			.value("pathHint", std::string{});
		if (pathHint.rfind("Assets/Animations/Combat/CloseCombat/", 0) == 0)
			++closeCombatClipRefs;
	}
	for (const auto& graph : animator.value("graphs", nlohmann::ordered_json::array()))
	{
		if (graph.value("id", std::string{}) != "graph-attack") continue;
		attackGraphIsNonMotionMatching = true;
		int rootMotionStates = 0;
		for (const auto& node : graph["graph"].value("nodes", nlohmann::ordered_json::array()))
		{
			attackGraphIsNonMotionMatching = attackGraphIsNonMotionMatching &&
				node.value("type", std::string{}) != "MotionMatching";
			for (const auto& state : node.value("properties", nlohmann::ordered_json::object())
				.value("states", nlohmann::ordered_json::array()))
				if (state.value("rootMotion", false)) ++rootMotionStates;
		}
		attackStatesUseRootMotion = rootMotionStates == 2;
	}

	VansGraphics::Skeleton sceneSkeleton;
	if (!LoadContractSkeletonFromModel(
		projectRoot / "Assets/Models/SKM_UEFN_Mannequin.fbx", sceneSkeleton, error))
		return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(sceneSkeleton.bones.size() == attackClipSkeleton.bones.size(),
		"DemoHall UEFN model and attack clips use different skeleton sizes")) return false;

	VansGraphics::AnimatorAssetData animatorAsset;
	if (!VansGraphics::VansAnimatorIO::Load(
		(projectRoot / "Assets/MotionMatchDataBase/UEFN_Mannequin.vanimator").string(), animatorAsset))
		return ExpectGAF(false, "DemoHall UEFN Animator definition could not be loaded");
	animatorAsset.defaultGraphSetId = "graph-set-attack";
	animatorAsset.graphSets.erase(std::remove_if(
		animatorAsset.graphSets.begin(), animatorAsset.graphSets.end(),
		[](const VansGraphics::VansAnimationGraphSetDefinition& graphSet)
		{
			return graphSet.id != "graph-set-attack";
		}), animatorAsset.graphSets.end());
	animatorAsset.graphs.erase(std::remove_if(
		animatorAsset.graphs.begin(), animatorAsset.graphs.end(),
		[](const VansGraphics::AnimatorGraphAsset& graph)
		{
			return graph.id != "graph-attack" && graph.id != "graph-target-post-process";
		}), animatorAsset.graphs.end());
	animatorAsset.graphSetTransitionRules.clear();
	animatorAsset.clipRefs.erase(std::remove_if(
		animatorAsset.clipRefs.begin(), animatorAsset.clipRefs.end(),
		[](const VansGraphics::AnimatorClipRef& ref)
		{
			return ref.name.rfind("Attack_", 0) != 0;
		}), animatorAsset.clipRefs.end());

	VansGraphics::VansAnimatorRuntimeCompileOptions compileOptions;
	// 此契约只验证 CloseCombat Graph 与 Root Motion；目标后处理图属于另一条
	// 已有专用契约，不能让它的世界查询等待阻塞攻击配置验收。
	compileOptions.enableTargetPostProcess = false;
	compileOptions.enableRootMotion = true;
	compileOptions.queryProfileResolver = [](
		const std::string&, std::uint32_t& collisionMask, std::string&)
	{
		collisionMask = 0xFFFFFFFFu;
		return true;
	};
	compileOptions.rigResolver = [&projectRoot](
		const std::string&, std::string& resolveError)
	{
		return LoadRigForContract(
			projectRoot / "Assets/AnimationRigs/UEFN.vanimrig", resolveError);
	};
		auto attackController = VansGraphics::VansAnimatorRuntimeCompiler::Compile(
		animatorAsset,
		sceneSkeleton,
		[&projectRoot](const VansGraphics::AnimatorClipRef& ref,
			std::shared_ptr<const VansGraphics::VansAnimationClipAsset>& clip,
			std::string& resolveError)
		{
			return LoadAnimationClipAssetForContract(
				projectRoot / ref.pathHint, clip, resolveError);
		},
		ProjectMaskResolverForContract(projectRoot),
		compileOptions,
		error);
	if (!ExpectGAF(attackController != nullptr, error.c_str())) return false;
	attackController->SetInt("AttackVariant", 0);
	attackController->Play();
	attackController->Update(0.0f, sceneSkeleton);
	const std::vector<glm::mat4> attackPoseStart = attackController->GetCachedGlobalTransforms();
	attackController->Update(0.4f, sceneSkeleton);
	const std::vector<glm::mat4>& attackPoseAdvanced = attackController->GetCachedGlobalTransforms();
	float maxRuntimePoseDelta = 0.0f;
	for (std::size_t boneIndex = 0;
		boneIndex < std::min(attackPoseStart.size(), attackPoseAdvanced.size()); ++boneIndex)
	{
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
				maxRuntimePoseDelta = std::max(maxRuntimePoseDelta,
					std::abs(attackPoseAdvanced[boneIndex][column][row]
						- attackPoseStart[boneIndex][column][row]));
	}
	if (!ExpectGAF(attackController->GetCurrentStateName() == "Crowbar Attack" &&
		attackController->GetCurrentPlayTime() > 0.35f && maxRuntimePoseDelta > 0.001f,
		"The non-Motion-Matching DemoHall attack Graph did not advance its runtime pose"))
		return false;

	nlohmann::ordered_json scene;
	if (!Vans::VansJsonFileStorage::Read(projectRoot / "Scenes/DemoHall.json", scene, error))
		return ExpectGAF(false, error.c_str());
	bool foundMannequinHost = false;
	bool foundMannequinController = false;
	bool foundSurvivalHost = false;
	bool foundSurvivalController = false;
	bool foundAxeHitBase = false;
	bool foundAxeHitTip = false;
	bool foundWhisperHurtBody = false;
	bool foundWhisperResponseHost = false;
	const auto hasLocalPosition = [](const nlohmann::ordered_json& entity,
		const glm::vec3& expected)
	{
		for (const auto& component : entity.value(
			"components", nlohmann::ordered_json::array()))
		{
			if (component.value("type", std::string{}) != "Transform") continue;
			const auto position = component.value("data", nlohmann::ordered_json::object())
				.value("position", nlohmann::ordered_json::array());
			return position.size() == 3 &&
				std::abs(position[0].get<float>() - expected.x) < 0.001f &&
				std::abs(position[1].get<float>() - expected.y) < 0.001f &&
				std::abs(position[2].get<float>() - expected.z) < 0.001f;
		}
		return false;
	};
	const auto& sceneEntities = scene.at("entities");
	for (const auto& entity : sceneEntities)
	{
		const std::string entityId = entity.value("id", std::string{});
		const auto& parent = entity.at("parent");
		const bool isAxeChild = parent.is_object() &&
			parent.value("kind", std::string{}) == "entity" &&
			parent.value("entityGuid", std::string{}) ==
				"d12e84ac-99d2-4a35-8e13-33a6c9032f27";
		foundAxeHitBase = foundAxeHitBase ||
			(entityId == "928f6ad1-4aac-4b57-a244-8a021f518401" && isAxeChild &&
			hasLocalPosition(entity, glm::vec3(-14.58f, 72.0f, 21.46f)));
		foundAxeHitTip = foundAxeHitTip ||
			(entityId == "c28c047e-aebd-4668-8ca6-04de3f348402" && isAxeChild &&
				hasLocalPosition(entity, glm::vec3(-14.58f, 154.45f, 19.98f)));
		if (parent.is_object() && parent.value("kind", std::string{}) == "bone" &&
			parent.value("entityGuid", std::string{}) == "d21cfc31-dba5-44cb-a271-f0f2c20cded1")
			for (const auto& component : entity.at("components"))
				if (component.value("type", std::string{}) == "Physics")
				{
					const auto& data = component.at("data");
					foundWhisperHurtBody = foundWhisperHurtBody ||
						(data.value("hitRegion", std::string{}) == "Chest" &&
						data.value("bodyType", std::string{}) == "kinematic" &&
						data.value("layer", std::string{}) == "Enemy" && data.value("isTrigger", false));
				}
		if (entityId == "d21cfc31-dba5-44cb-a271-f0f2c20cded1")
		{
			for (const auto& component : entity.at("components"))
			{
				const auto& data = component.at("data");
				if (component.value("type", std::string{}) == "ActionHost")
				{
					const auto sets = data.value("actionSets", nlohmann::ordered_json::array());
					const auto initializers = data.value(
						"initializers", nlohmann::ordered_json::array());
					foundWhisperResponseHost = !sets.empty() && !initializers.empty() &&
						sets.front().value("guid", std::string{}) ==
							"269218a3-6809-48f6-b055-97891161c303" &&
						initializers.front().value("type", std::string{}) ==
							"Gameplay.Tags.Initialize" &&
						initializers.front().value("inputs", nlohmann::ordered_json::object())
							.value("tag", std::string{}) == "Target.Character.Enemy";
				}
			}
		}
		const bool isMannequin = entityId == "4186c86d-7c0a-4556-8077-d1f80ebc1da5";
		const bool isSurvival = entityId == "38dbe7af-653a-4aeb-bfa7-1ca72e2b972c";
		if (!isMannequin && !isSurvival)
			continue;
		for (const auto& component : entity.at("components"))
		{
			const auto& data = component.at("data");
			if (component.value("type", std::string{}) == "ActionHost")
			{
				const auto sets = data.value("actionSets", nlohmann::ordered_json::array());
				const bool configured = !sets.empty() &&
					sets.front().value("guid", std::string{}) ==
						"4534ddf1-e858-468e-a1ab-9e8b98cf6129";
				if (isMannequin) foundMannequinHost = configured;
				if (isSurvival) foundSurvivalHost = configured;
			}
			if (component.value("type", std::string{}) == "Script" &&
				data.value("entry", std::string{}) == "PlayerAttackController")
			{
				const auto fields = data.value("fields", nlohmann::ordered_json::object());
				const auto attackWeapon = fields.value(
					"attackWeapon", nlohmann::ordered_json::object());
				const bool weaponConfigured = !isSurvival ||
					(attackWeapon.value("domain", std::string{}) == "SceneEntity" &&
					attackWeapon.value("entityGuid", std::string{}) ==
						"d12e84ac-99d2-4a35-8e13-33a6c9032f27" &&
					fields.value("weaponAnimationComponentGuid", std::string{}) ==
						"87f6e3d4-c8fe-458d-82f2-3e56f20b93e5" &&
					fields.value("weaponHandSocketGuid", std::string{}) ==
						"7a33b99b-0664-4b5b-bc86-a5a85ddf03b7" &&
					fields.value("weaponBackSocketGuid", std::string{}) ==
						"0cf5406a-6809-4b8a-9d82-062643893f56");
				const bool configured =
					fields.value("actionHostOwnerGuid", std::string{}) == entityId &&
					fields.value("crowbarAttackActionId", std::string{}) ==
						"Gameplay.DemoHall.Player.Attack.Crowbar" &&
					fields.value("crowbarTakedownAttackerActionId", std::string{}) ==
						"Gameplay.DemoHall.Player.Attack.Takedown.Crowbar.Attacker" &&
					fields.value("attackGraphSetId", std::string{}) == "graph-set-attack" &&
					fields.value("locomotionGraphSetId", std::string{}) == "graph-set-default" &&
					weaponConfigured;
				if (isMannequin) foundMannequinController = configured;
				if (isSurvival) foundSurvivalController = configured;
			}
		}
	}
	nlohmann::ordered_json whisperAnimator;
	if (!Vans::VansJsonFileStorage::Read(
		projectRoot / "Assets/Characters/Whisper/Animation/Whisper.vanimator",
		whisperAnimator, error)) return ExpectGAF(false, error.c_str());
	bool foundWhisperHitAnimation = false;
	for (const auto& graph : whisperAnimator.value("graphs", nlohmann::ordered_json::array()))
		for (const auto& node : graph.value("graph", nlohmann::ordered_json::object())
			.value("nodes", nlohmann::ordered_json::array()))
			for (const auto& state : node.value("properties", nlohmann::ordered_json::object())
				.value("states", nlohmann::ordered_json::array()))
				foundWhisperHitAnimation = foundWhisperHitAnimation ||
					(state.value("name", std::string{}) == "TakingDamage1" &&
					!state.value("loop", true));
	std::string script;
	if (!Vans::VansFileStorage::ReadAllBytes(
		projectRoot / "Scripts/forest_lua_behaviors.lua", script, error))
		return ExpectGAF(false, error.c_str());
	const std::size_t attackScriptBegin = script.find("M.PlayerAttackController");
	// 按下一个模块声明限定近战脚本，新增其他控制器不会污染近战契约检查。
	const std::size_t attackScriptEnd = script.find("\nM.", attackScriptBegin);
	const std::string attackScript = attackScriptBegin != std::string::npos
		? script.substr(attackScriptBegin, attackScriptEnd - attackScriptBegin) : std::string{};
	return ExpectGAF(foundAttackSet && attackGraphIsNonMotionMatching && closeCombatClipRefs == 2 &&
		attackStatesUseRootMotion && foundMannequinHost && foundMannequinController &&
		foundSurvivalHost && foundSurvivalController && foundAxeHitBase && foundAxeHitTip &&
		foundWhisperHurtBody && foundWhisperResponseHost && foundWhisperHitAnimation &&
		attackScript.find("is_key_pressed(\"J\")") != std::string::npos &&
		attackScript.find("self.characterName ~= (character_state.activeName") != std::string::npos &&
		attackScript.find("DemoHall player attack input: KEY_J") != std::string::npos &&
		attackScript.find("DemoHall player attack Graph Set settled") != std::string::npos &&
		attackScript.find("vans.action") != std::string::npos &&
		attackScript.find("try_activate") != std::string::npos &&
		attackScript.find("switch_graph_set") != std::string::npos &&
		attackScript.find("bind_to_socket_profile") != std::string::npos &&
		attackScript.find("self:restore_attack_weapon()") != std::string::npos &&
		attackScript.find("weapon:bind_to_socket_profile(") != std::string::npos &&
		attackScript.find("self.weaponAnimationComponentGuid, socketGuid") != std::string::npos &&
		attackScript.find("configured_string") != std::string::npos &&
		attackScript.find("request_cancel") == std::string::npos,
		"DemoHall player attack is not wired through GAF into the non-MM root-motion Graph Set");
}

bool TestDemoHallHurtBodiesContract()
{
	using namespace VansGraphics;
	using namespace VansEngine;
	namespace fs = std::filesystem;
	// 长胶囊端部、斜向胶囊、退化武器路径以及靠近但未接触的反例。
	const glm::vec3 p(-1, 0.75f, -0.2f), q(-1, 0.75f, 0.2f);
	if (!ExpectGAF(Vans::VansContinuousWeaponPathIntersectsCapsule(p, q,
		-p + glm::vec3(0, 1.5f, 0), -q + glm::vec3(0, 1.5f, 0),
		glm::vec3(0, -0.8f, 0), glm::vec3(0, 0.8f, 0), 0.05f) &&
		!Vans::VansContinuousWeaponPathIntersectsSphere(p, q,
		glm::vec3(1, 0.75f, -0.2f), glm::vec3(1, 0.75f, 0.2f), glm::vec3(0), 0.05f) &&
		!Vans::VansContinuousWeaponPathIntersectsCapsule(glm::vec3(-1, 1.1f, 0), glm::vec3(1, 1.1f, 0),
		glm::vec3(-1, 1.1f, 0), glm::vec3(1, 1.1f, 0), glm::vec3(0, -0.8f, 0), glm::vec3(0, 0.8f, 0), 0.1f),
		"Capsule narrow phase ignored the segment or produced a near-miss hit")) return false;
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 6 && !fs::exists(workspace / "DemoHallProject"); ++depth) workspace = workspace.parent_path();
	const auto project = workspace / "DemoHallProject";
	nlohmann::ordered_json scene;
	std::string error;
	if (!Vans::VansJsonFileStorage::Read(project / "Scenes/DemoHall.json", scene, error)) return ExpectGAF(false, error.c_str());
	auto& physics = VansPhysicsSystem::GetInstance();
	if (!ExpectGAF(physics.Initialize(), "Hurt body PhysX initialization failed")) return false;
	struct PhysicsCleanup { VansPhysicsSystem& physics; ~PhysicsCleanup() { physics.Shutdown(); } } physicsCleanup{physics};
	std::size_t checked = 0;
	for (bool survival : { false, true })
	{
		const std::string ownerName = survival ? "SurvivalCharacter" : "Whisper";
		const auto& owner = *std::find_if(scene["entities"].begin(), scene["entities"].end(),
			[&](const auto& e) { return e.value("name", std::string{}) == ownerName; });
		Skeleton skeleton;
		if (!LoadContractSkeletonFromModel(project / (survival
			? "Assets/Characters/Survival/Models/survival_character.fbx" : "Assets/Characters/Whisper/Models/SK_Whisper.glb"), skeleton, error))
			return ExpectGAF(false, error.c_str());
		VansAnimationController controller;
		VansAnimationNode animation("Hurt body anchor contract");
		animation.SetSkeleton(skeleton);
		if (!ExpectGAF(animation.SetController(&controller), "Hurt body controller binding failed")) return false;
		VansSkeletonAnchorRegistry anchors;
		const auto instance = anchors.RegisterInstance(animation);
		Vans::VansTransformGraph graph(&anchors);
		std::vector<uint32_t> transforms;
		std::vector<std::unique_ptr<VansPhysicsNode>> nodes;
		struct Cleanup
		{
			std::vector<uint32_t>& transforms;
			std::vector<std::unique_ptr<VansPhysicsNode>>& nodes;
			~Cleanup() { nodes.clear(); for (auto id : transforms) VansTransformStore::FreeTransform(id); }
		} cleanup{ transforms, nodes };
		const auto allocate = [&]() { const auto id = VansTransformStore::AllocateTransform(); transforms.push_back(id); return id; };
		const auto ownerId = allocate();
		auto& ownerTransform = VansTransformStore::GetTransform(ownerId);
		ownerTransform.m_Position = glm::vec3(0);
		ownerTransform.m_Rotation = survival ? glm::vec3(-90, 35, 0) : glm::vec3(0, -25, 0);
		ownerTransform.m_Scale = glm::vec3(survival ? .01f : 1.f);
		std::vector<glm::mat4> pose(skeleton.bones.size());
		const auto poseAt = [&](bool articulated)
		{
			for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
			{
				const auto& bone = skeleton.bones[i];
				glm::mat4 local = bone.localTransform;
				if (articulated && (bone.name == "lowerarm_l" || bone.name == "calf_r" || bone.name == "head"))
					local *= glm::rotate(glm::mat4(1), glm::radians(65.0f), glm::vec3(0, 0, 1));
				pose[i] = bone.parentIndex < 0 ? local : pose[bone.parentIndex] * local;
			}
			return controller.SubmitExternalModelPose(pose, skeleton, 0.016f, VansExternalPoseEvaluationMode::DirectFinalPose);
		};
		if (!ExpectGAF(poseAt(false), "Bind pose publication failed")) return false;
		std::unordered_set<std::string> regions;
		std::string animationGuid;
		for (const auto& c : owner["components"])
		{
			if (c.value("type", std::string{}) == "Animation") animationGuid = c["id"].get<std::string>();
			if (c.value("type", std::string{}) == "Physics")
				return ExpectGAF(false, "Character still has the obsolete root hurt capsule");
		}
		for (const auto& entity : scene["entities"])
		{
			const auto& parent = entity["parent"];
			if (!parent.is_object() || parent.value("entityGuid", std::string{}) != owner["id"].get<std::string>()) continue;
			const auto components = Vans::VansScenePhysicsComponentReader::ReadAuthoringComponents(Vans::DecodeSerializedValueJson(entity));
			if (!components.physics || !components.physics->hitRegion) continue;
			const auto& c = *components.physics;
			const auto boneGuid = parent.value("anchorGuid", std::string{});
			if (!ExpectGAF(parent.value("kind", std::string{}) == "bone" &&
				parent.value("animationComponentGuid", std::string{}) == animationGuid &&
				skeleton.boneGuidToIndex.count(boneGuid) && regions.insert(*c.hitRegion).second &&
				c.isTrigger.value_or(false) && c.bodyType.value_or("") == "kinematic" &&
				c.colliderType.value_or("") == "capsule" && c.layer.value_or("") == (survival ? "Player" : "Enemy"),
				"Hurt body region, bone binding, trigger or layer configuration is invalid")) return false;
			Vans::VansLocalTransform local;
			for (const auto& transform : entity["components"])
				if (transform.value("type", std::string{}) == "Transform")
				{
					const auto& d = transform["data"];
					local.position = glm::vec3(d["position"][0], d["position"][1], d["position"][2]);
					local.rotation = glm::quat(d["rotation"][3], d["rotation"][0], d["rotation"][1], d["rotation"][2]);
				}
			const auto id = allocate();
			if (!ExpectGAF(graph.SetAnchorWithLocalTransform(id, ownerId,
				anchors.MakeAnchorHandle(instance, Vans::VansTransformAnchorKind::Bone, boneGuid), local) && graph.Resolve(),
				"Runtime skeleton anchor did not resolve a configured hurt body")) return false;
			PhysicsNodeProperties properties;
			properties.enabled = true;
			properties.bodyType = PhysicsBodyType::Kinematic;
			properties.colliderType = PhysicsColliderType::Capsule;
			properties.isTrigger = true;
			properties.hitRegion = *c.hitRegion;
			properties.layerName = *c.layer;
			properties.capsuleRadius = c.capsuleRadius.value_or(0);
			properties.capsuleHalfHeight = c.capsuleHalfHeight.value_or(0);
			auto node = std::make_unique<VansPhysicsNode>();
			node->SetName(entity["name"].get<std::string>());
			node->Initialize(properties, id);
			nodes.push_back(std::move(node));
		}
		if (!ExpectGAF(regions.size() == 17, "Both DemoHall characters require 17 distinct hurt regions")) return false;
		std::vector<glm::vec3> initialCenters;
		for (const auto& node : nodes) initialCenters.push_back(VansTransformStore::GetTransform(node->GetTransformID()).m_Position);
		float maximumMotion = 0;
		for (bool articulated : { false, true })
		{
			if (!ExpectGAF(poseAt(articulated) && graph.Resolve(), "Animated hurt body pose did not resolve")) return false;
			for (std::size_t i = 0; i < nodes.size(); ++i)
			{
				auto& node = *nodes[i];
				node.UpdatePhysicsFromTransform();
				const auto& t = VansTransformStore::GetTransform(node.GetTransformID());
				const glm::quat rotation(glm::radians(t.m_Rotation));
				const auto axis = rotation * glm::vec3(1,0,0);
				const auto direction = rotation * glm::vec3(0,1,0);
				const float radius = node.GetProperties().capsuleRadius * t.m_Scale.x;
				const float halfHeight = node.GetProperties().capsuleHalfHeight * t.m_Scale.y;
				if (!ExpectGAF(radius > .02f && radius < .3f && halfHeight < .5f,
					"Hurt body world units or dimensions are incorrect")) return false;
				struct OnlyActor final : physx::PxQueryFilterCallback
				{
					const physx::PxRigidActor* actor = nullptr;
					physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape*,
						const physx::PxRigidActor* candidate, physx::PxHitFlags&) override
					{ return candidate == actor ? physx::PxQueryHitType::eBLOCK : physx::PxQueryHitType::eNONE; }
					physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&,
						const physx::PxShape*, const physx::PxRigidActor*) override { return physx::PxQueryHitType::eBLOCK; }
				} filter;
				filter.actor = node.GetActor();
				physx::PxRaycastBuffer result;
				physx::PxQueryFilterData query;
				query.flags |= physx::PxQueryFlag::ePREFILTER;
				const auto origin = t.m_Position + direction * radius * 4.f;
				const auto px = [](glm::vec3 v) { return physx::PxVec3(v.x,v.y,v.z); };
				const bool hit = physics.GetScene()->raycast(px(origin), px(-direction), radius * 8.f,
					result, physx::PxHitFlag::eDEFAULT, query, &filter);
				if (!ExpectGAF(hit && result.hasBlock && result.block.actor == node.GetActor() &&
					std::abs(result.block.distance - radius*3.f) < .005f,
					"PhysX ray did not hit the current bone-bound capsule at its expected surface")) return false;
				if (!ExpectGAF(Vans::VansContinuousWeaponPathIntersectsCapsule(origin, origin,
					t.m_Position - direction * radius * 4.f, t.m_Position - direction * radius * 4.f,
					t.m_Position - axis * halfHeight, t.m_Position + axis * halfHeight, radius),
					"Melee query missed a body hit by PhysX")) return false;
				maximumMotion = std::max(maximumMotion, glm::length(t.m_Position - initialCenters[i]));
				++checked;
			}
		}
		if (!ExpectGAF(maximumMotion > .05f, "Articulated bones did not move their hurt bodies")) return false;
		std::cout << "[HurtBodies] " << ownerName << " regions=" << regions.size()
			<< " posedMotion=" << maximumMotion << " rayAndMelee=34\n";
	}
	std::cout << "[HurtBodies] PASS regions=34 posedShapeChecks=" << checked << '\n';
	return true;
}

bool TestGAFDemoHallMeleeHitRuntimeContract()
{
	namespace fs = std::filesystem;
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 6 && !fs::exists(workspace / "DemoHallProject"); ++depth)
		workspace = workspace.parent_path();
	const fs::path projectRoot = workspace / "DemoHallProject";
	const fs::path temporaryRoot =
		fs::temp_directory_path() / "ForestGAFDemoHallMeleeHitRuntimeContract";
	std::error_code filesystemError;
	fs::remove_all(temporaryRoot, filesystemError);
	struct TemporaryCleanup
	{
		fs::path path;
		~TemporaryCleanup()
		{
			std::error_code ignored;
			fs::remove_all(path, ignored);
		}
	} temporaryCleanup{ temporaryRoot };

	const fs::path assetsRoot = temporaryRoot / "Assets/PlayerAttack";
	fs::create_directories(assetsRoot, filesystemError);
	fs::copy(projectRoot / "Assets/GAF/PlayerAttack", assetsRoot,
		fs::copy_options::recursive | fs::copy_options::overwrite_existing,
		filesystemError);
	if (!ExpectGAF(!filesystemError,
		"DemoHall player-attack assets could not be copied for the runtime hit test"))
		return false;
	fs::copy(projectRoot / "Assets/GAF/WhisperCombat", assetsRoot / "WhisperCombat",
		fs::copy_options::recursive | fs::copy_options::overwrite_existing,
		filesystemError);
	if (!ExpectGAF(!filesystemError,
		"DemoHall Whisper response assets could not be copied for the runtime hit test"))
		return false;
	for (const char* fileName : { "DemoHallTags.vtagtree", "DemoHallTags.vtagtree.meta" })
	{
		fs::copy_file(projectRoot / "Assets/GAF/WindowBreak" / fileName,
			assetsRoot / fileName, fs::copy_options::overwrite_existing, filesystemError);
		if (!ExpectGAF(!filesystemError,
			"DemoHall TagTree could not be copied for the runtime hit test")) return false;
	}

	Vans::VansGAFProjectConfiguration configuration;
	std::string error;
	if (!Vans::VansGAFProjectConfiguration::LoadForProject(
		projectRoot, workspace / "ForestEngine/ForestEngine", configuration, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansAssetDatabase database(
		temporaryRoot / "Assets", temporaryRoot / "Library/Artifacts");
	const Vans::VansAssetScanResult scan =
		database.Scan(Vans::VansAssetOperationPolicy::Authoring());
	if (!ExpectGAF(scan && database.All().size() == 16,
		"DemoHall melee runtime fixture did not scan as sixteen GAF assets")) return false;

	VansGraphics::VansAnimationClip referenceClip;
	VansGraphics::Skeleton whisperSkeleton;
	if (!VansGraphics::VansAnimationClipIO::Load(
		(projectRoot / "Assets/Characters/Whisper/Animations/"
			"Anim_Whisper_Taking_Damage1_Unreal_Take.vclip").string(),
		referenceClip, whisperSkeleton))
		return ExpectGAF(false, "Whisper hit clip could not provide the runtime skeleton");
	VansGraphics::AnimatorAssetData whisperAnimatorAsset;
	if (!VansGraphics::VansAnimatorIO::Load(
		(projectRoot / "Assets/Characters/Whisper/Animation/Whisper.vanimator").string(),
		whisperAnimatorAsset))
		return ExpectGAF(false, "Whisper Animator could not be loaded for the runtime hit test");
	VansGraphics::VansAnimatorRuntimeCompileOptions compileOptions;
	compileOptions.enableTargetPostProcess = false;
	compileOptions.enableRootMotion = false;
	compileOptions.rigResolver = [&projectRoot](
		const std::string&, std::string& resolveError)
	{
		return LoadRigForContract(projectRoot /
			"Assets/Characters/Whisper/Animation/Whisper.vanimrig", resolveError);
	};
	auto whisperController = VansGraphics::VansAnimatorRuntimeCompiler::Compile(
		whisperAnimatorAsset, whisperSkeleton,
		[&projectRoot](const VansGraphics::AnimatorClipRef& ref,
			std::shared_ptr<const VansGraphics::VansAnimationClipAsset>& clip,
			std::string& resolveError)
		{
			return LoadAnimationClipAssetForContract(
				projectRoot / ref.pathHint, clip, resolveError);
		},
		[](const VansGraphics::VansAnimationLayerDefinition&,
			std::shared_ptr<const VansGraphics::VansBoneMaskAsset>&,
			std::string& resolveError)
		{
			resolveError = "Whisper base layer unexpectedly requested a Bone Mask";
			return false;
		}, compileOptions, error);
	if (!ExpectGAF(whisperController != nullptr, error.c_str())) return false;
	whisperController->Play();
	whisperController->Update(0.0f, whisperSkeleton);
	const std::string stateBeforeHit = whisperController->GetCurrentStateName();

	Vans::VansRuntimeWorld world;
	const Vans::VansEntityHandle attacker = world.CreateEntity({
		"demohall-melee-attacker", "Survival Runtime Attacker" });
	const Vans::VansEntityHandle hitBase = world.CreateEntity({
		"928f6ad1-4aac-4b57-a244-8a021f518401", "Survival_Axe_Hit_Base", attacker });
	const Vans::VansEntityHandle hitTip = world.CreateEntity({
		"c28c047e-aebd-4668-8ca6-04de3f348402", "Survival_Axe_Hit_Tip", attacker });
	const Vans::VansEntityHandle whisper = world.CreateEntity({
		"demohall-melee-whisper", "Whisper Runtime Target" });
	if (!ExpectGAF(attacker.IsValid() && hitBase.IsValid() && hitTip.IsValid() &&
		whisper.IsValid(), "DemoHall melee runtime entities could not be created")) return false;

	std::vector<std::uint32_t> transformIds;
	const auto addTransform = [&](Vans::VansEntityHandle owner, const char* guid,
		const glm::vec3& position)
	{
		const std::uint32_t id = VansGraphics::VansTransformStore::AllocateTransform();
		transformIds.push_back(id);
		VansGraphics::VansTransform& transform =
			VansGraphics::VansTransformStore::GetTransform(id);
		transform.m_Position = position;
		transform.m_Rotation = glm::vec3(0.0f);
		transform.m_Scale = glm::vec3(1.0f);
		return world.AddComponent(owner, Vans::VansRuntimeComponentType_Transform,
			Vans::VansRuntimeTransformComponent{ id }, guid).IsValid() ? id : UINT32_MAX;
	};
	const std::uint32_t attackerTransform = addTransform(
		attacker, "demohall-melee-attacker-transform", glm::vec3(0.0f));
	const std::uint32_t baseTransform = addTransform(
		hitBase, "demohall-melee-base-transform", glm::vec3(-1.0f, 1.05f, -0.6f));
	const std::uint32_t tipTransform = addTransform(
		hitTip, "demohall-melee-tip-transform", glm::vec3(-1.0f, 1.05f, -1.6f));
	const std::uint32_t whisperTransform = addTransform(
		whisper, "demohall-melee-whisper-transform", glm::vec3(0.0f, 0.0f, -1.1f));
	if (!ExpectGAF(attackerTransform != UINT32_MAX && baseTransform != UINT32_MAX &&
		tipTransform != UINT32_MAX && whisperTransform != UINT32_MAX,
		"DemoHall melee runtime transforms could not be created"))
	{
		for (std::uint32_t id : transformIds) VansGraphics::VansTransformStore::FreeTransform(id);
		return false;
	}
	// 生产场景的 Survival 根节点带有 X=-90° 与 0.01 缩放作为模型导入
	// 修正。打击前向只能读取 locomotion yaw，不能经过完整模型矩阵。
	VansGraphics::VansTransform& attackerWorld =
		VansGraphics::VansTransformStore::GetTransform(attackerTransform);
	attackerWorld.m_Rotation = glm::vec3(-90.0f, 180.0f, 0.0f);
	attackerWorld.m_Scale = glm::vec3(0.01f);

	VansEngine::VansPhysicsSystem& physicsSystem =
		VansEngine::VansPhysicsSystem::GetInstance();
	if (!ExpectGAF(physicsSystem.Initialize(),
		"PhysX could not initialize for the production hurt-body runtime test"))
	{
		physicsSystem.Shutdown();
		for (std::uint32_t id : transformIds) VansGraphics::VansTransformStore::FreeTransform(id);
		return false;
	}
	VansEngine::VansPhysicsNode hurtBody;
	VansEngine::PhysicsNodeProperties hurtBodyProperties;
	hurtBodyProperties.enabled = true;
	hurtBodyProperties.bodyType = VansEngine::PhysicsBodyType::Kinematic;
	hurtBodyProperties.colliderType = VansEngine::PhysicsColliderType::Capsule;
	hurtBodyProperties.layerName = "Enemy";
	hurtBodyProperties.isTrigger = true;
	hurtBodyProperties.hitRegion = "Chest";
	hurtBodyProperties.capsuleRadius = 0.38f;
	hurtBodyProperties.capsuleHalfHeight = 0.5f;
	hurtBodyProperties.shapeOffset = glm::vec3(0.0f, 1.05f, 0.0f);
	hurtBody.SetName("Whisper_Chest_HurtBody");
	hurtBody.Initialize(hurtBodyProperties, whisperTransform);
	VansEngine::VansPhysicsNode armBody;
	auto armProperties = hurtBodyProperties;
	armProperties.hitRegion = "RightForearm";
	armProperties.capsuleRadius = .08f;
	armProperties.capsuleHalfHeight = .18f;
	armProperties.shapeOffset.x = .45f;
	armBody.SetName("Whisper_RightForearm_HurtBody");
	armBody.Initialize(armProperties, whisperTransform);
	VansEngine::VansPhysicsNode playerBody;
	auto playerProperties = armProperties;
	playerProperties.hitRegion = "Head";
	playerProperties.layerName = "Player";
	playerProperties.shapeOffset = glm::vec3(0);
	const auto playerHead = world.CreateEntity({ "survival-head", "Survival Head", attacker });
	const auto playerHeadTransform = addTransform(playerHead, "survival-head-transform", glm::vec3(0,1.05f,0));
	playerBody.SetName("Survival_Head_HurtBody");
	playerBody.Initialize(playerProperties, playerHeadTransform);
	world.AddComponent(playerHead, Vans::VansRuntimeComponentType_Physics,
		Vans::VansRuntimePhysicsComponent{ &playerBody }, "demohall-survival-head-body");
	const auto armEntity = world.CreateEntity({ "whisper-arm", "Whisper Arm", whisper });
	world.AddComponent(armEntity, Vans::VansRuntimeComponentType_Physics,
		Vans::VansRuntimePhysicsComponent{ &armBody }, "demohall-whisper-arm-body");
	VansEngine::VansCharacterControllerNode whisperCct;
	VansGraphics::VansAnimationNode whisperAnimation("Whisper Runtime Animation");
	whisperAnimation.SetSkeleton(whisperSkeleton);
	const bool controllerBound = whisperAnimation.SetController(whisperController.get());
	const auto chestEntity = world.CreateEntity({ "whisper-chest", "Whisper Chest", whisper });
	const Vans::VansComponentHandle hurtBodyComponent = world.AddComponent(
		chestEntity, Vans::VansRuntimeComponentType_Physics,
		Vans::VansRuntimePhysicsComponent{ &hurtBody }, "demohall-whisper-hurt-body");
	const Vans::VansComponentHandle cctComponent = world.AddComponent(
		whisper, Vans::VansRuntimeComponentType_CharacterController,
		Vans::VansRuntimeCharacterControllerComponent{ &whisperCct },
		"demohall-whisper-cct");
	const Vans::VansComponentHandle animationComponent = world.AddComponent(
		whisper, Vans::VansRuntimeComponentType_Animation,
		Vans::VansRuntimeAnimationComponent{ &whisperAnimation },
		"demohall-whisper-animation");

	Vans::VansGameplayRuntime gameplayRuntime;
	struct RuntimeCleanup
	{
		Vans::VansGameplayRuntime& gameplayRuntime;
		Vans::VansRuntimeWorld& world;
		VansEngine::VansPhysicsNode& hurtBody;
		VansEngine::VansPhysicsNode& armBody;
		VansEngine::VansPhysicsNode& playerBody;
		VansEngine::VansCharacterControllerNode& cct;
		VansEngine::VansPhysicsSystem& physicsSystem;
		std::vector<std::uint32_t>& transformIds;
		~RuntimeCleanup()
		{
			gameplayRuntime.Shutdown();
			world.Clear();
			hurtBody.Shutdown();
			armBody.Shutdown();
			playerBody.Shutdown();
			cct.Release();
			for (std::uint32_t id : transformIds)
				VansGraphics::VansTransformStore::FreeTransform(id);
			physicsSystem.Shutdown();
		}
	} runtimeCleanup{
		gameplayRuntime, world, hurtBody, armBody, playerBody, whisperCct, physicsSystem, transformIds };
	if (!ExpectGAF(hurtBody.IsEnabled() && controllerBound &&
		hurtBodyComponent.IsValid() && cctComponent.IsValid() && animationComponent.IsValid(),
		"Whisper production hurt-body, CCT, or Animation component is unavailable")) return false;

	auto combatService = Vans::VansCombatActionService::Create(
		world, gameplayRuntime, error);
	auto animationService = Vans::VansAnimationActionService::Create(world, error);
	auto navigationService = Vans::VansNavigationActionService::Create(world, error);
	if (!ExpectGAF(combatService && animationService && navigationService, error.c_str()))
		return false;
	Vans::VansGameplayRuntimeDependencies dependencies;
	dependencies.contributors.push_back(
		Vans::VansMakeGameplayPrimitivesGAFContributor());
	dependencies.contributors.push_back(MakeProjectSchemaContributor(configuration));
	dependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Gameplay.Combat", { combatService }));
	dependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Gameplay.Animation", { animationService }));
	dependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Gameplay.Navigation", { navigationService }));
	Vans::VansAssetObjectRepository assetObjects;
	if (!BootstrapGameplayMemory(database.All(), assetObjects, error))
		return ExpectGAF(false, error.c_str());
	if (!gameplayRuntime.Initialize(
		database.All(), assetObjects, configuration.settings, dependencies, error))
		return ExpectGAF(false, error.c_str());

	Vans::VansGameplayActionHostSetup attackerSetup;
	attackerSetup.actionSets.push_back("4534ddf1-e858-468e-a1ab-9e8b98cf6129");
	AddHostTagInitializer(attackerSetup, "Target.Character.Player");
	const auto attackerHost = gameplayRuntime.CreateHost(attacker, attackerSetup, error);
	Vans::VansGameplayActionHostSetup whisperSetup;
	whisperSetup.actionSets.push_back("269218a3-6809-48f6-b055-97891161c303");
	AddHostTagInitializer(whisperSetup, "Target.Character.Enemy");
	const auto whisperHost = gameplayRuntime.CreateHost(whisper, whisperSetup, error);
	const auto crowbarAttack = gameplayRuntime.Assets().ResolveAction(
		"Gameplay.DemoHall.Player.Attack.Crowbar");
	if (!ExpectGAF(attackerHost && whisperHost && crowbarAttack,
		"Production GAF hosts or Crowbar attack did not resolve")) return false;

	Vans::VansActionContext attackContext;
	attackContext.SetEntity(Vans::VansActionContextSlots::Owner, attacker);
	attackContext.SetEntity(Vans::VansActionContextSlots::Instigator, attacker);
	attackContext.SetEntity(Vans::VansActionContextSlots::Source, attacker);
	attackContext.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, whisper);
	const Vans::VansActionResult attackResult =
		attackerHost->ActivateAction(crowbarAttack->id, attackContext);
	if (!ExpectGAF(attackResult && attackerHost->ActiveActions().size() == 1,
		"Production Crowbar GAF Action did not open its configured melee resource"))
		return false;

	combatService->Tick(0.25);
	combatService->Tick(0.25);
	VansGraphics::VansTransformStore::GetTransform(baseTransform).m_Position.x = 1.0f;
	VansGraphics::VansTransformStore::GetTransform(tipTransform).m_Position.x = 1.0f;
	combatService->Tick(0.25);
	const Vans::VansCombatDebugSnapshot hitSnapshot =
		combatService->CaptureDebugSnapshot();
	const bool debugWindowValidated = hitSnapshot.available &&
		!hitSnapshot.windows.empty() && hitSnapshot.windows.front().active &&
		hitSnapshot.windows.front().window == "PrimaryMeleeHit" &&
		glm::length(hitSnapshot.windows.front().forward - glm::vec3(0.0f, 0.0f, -1.0f)) < 0.001f &&
		hitSnapshot.windows.front().hitCount == 1;
	const bool hurtBodyValidated = std::any_of(
		hitSnapshot.hurtBodies.begin(), hitSnapshot.hurtBodies.end(),
		[](const Vans::VansCombatDebugHurtBody& body)
		{
			return body.target == "Whisper Runtime Target" && body.hit &&
				body.region == "Chest" && body.componentGuid == "demohall-whisper-hurt-body" &&
				std::abs(body.radius - 0.38f) < 0.001f;
		});
	if (!(debugWindowValidated && hurtBodyValidated &&
		whisperHost->ActiveActions().size() == 1 &&
		whisperCct.IsGameplayMovementBlocked() &&
		whisperController->GetCurrentStateName() == "TakingDamage1"))
	{
		std::cerr << "[GAF] Runtime hit diagnostic: window=" << debugWindowValidated
			<< " hurtBody=" << hurtBodyValidated
			<< " responseActions=" << whisperHost->ActiveActions().size()
			<< " movementBlocked=" << whisperCct.IsGameplayMovementBlocked()
			<< " animation='" << whisperController->GetCurrentStateName() << "'\n";
	}
	if (!ExpectGAF(debugWindowValidated && hurtBodyValidated &&
		whisperHost->ActiveActions().size() == 1 &&
		whisperCct.IsGameplayMovementBlocked() &&
		whisperController->GetCurrentStateName() == "TakingDamage1",
		"Validated production melee hit did not block Whisper movement and play TakingDamage1"))
		return false;
	const auto responseSnapshot = whisperHost->ActiveActions().front();
	const auto* detailedHit = responseSnapshot.targetData.values.empty() ? nullptr :
		std::get_if<Vans::VansTargetHitResult>(&responseSnapshot.targetData.values.front());
	if (!ExpectGAF(detailedHit && detailedHit->entity == whisper && detailedHit->hitEntity == chestEntity &&
		detailedHit->region == "Chest" && detailedHit->componentGuid == "demohall-whisper-hurt-body" &&
		std::count_if(hitSnapshot.hurtBodies.begin(), hitSnapshot.hurtBodies.end(),
			[](const auto& body) { return body.hit; }) == 1,
		"Multi-part hit lost its region, collider identity or character-level deduplication")) return false;
	Vans::VansTargetData decodedHit;
	if (!Vans::VansDecodeTargetData(Vans::VansEncodeTargetData(responseSnapshot.targetData), decodedHit, error))
		return ExpectGAF(false, error.c_str());
	const auto& roundTrip = std::get<Vans::VansTargetHitResult>(decodedHit.values.front());
	if (!ExpectGAF(roundTrip.region == detailedHit->region && roundTrip.hitEntity == chestEntity &&
		roundTrip.componentGuid == detailedHit->componentGuid && roundTrip.position == detailedHit->position,
		"TargetData serialization discarded detailed body hit data")) return false;
	// Survival 尚未配置受击表现时，也必须能够发出分部位检测结果。
	Vans::VansActionCommand reverse;
	reverse.action = responseSnapshot.handle;
	reverse.stableName = "Combat.BeginMeleeWindow";
	reverse.context.SetEntity(Vans::VansActionContextSlots::Owner, whisper);
	reverse.payload = Vans::DecodeSerializedValueJson(nlohmann::ordered_json{
		{ "sourceBase", "928f6ad1-4aac-4b57-a244-8a021f518401" },
		{ "sourceTip", "c28c047e-aebd-4668-8ca6-04de3f348402" },
		{ "window", "SurvivalProbe" }, { "startSeconds", 0 }, { "endSeconds", .5 },
		{ "targetLayer", "Player" }, { "targetTag", "Target.Character.Player" },
		{ "sweepRadius", .02 }, { "range", 2.2 }, { "halfAngleDegrees", 180 }, { "verticalTolerance", 2 }
	});
	const auto reverseWindow = combatService->Execute(reverse);
	if (!ExpectGAF(static_cast<bool>(reverseWindow), "Detection-only melee window was rejected")) return false;
	VansGraphics::VansTransformStore::GetTransform(baseTransform).m_Position = glm::vec3(-1,1.05f,-.3f);
	VansGraphics::VansTransformStore::GetTransform(tipTransform).m_Position = glm::vec3(-1,1.05f,.3f);
	combatService->Tick(.01);
	VansGraphics::VansTransformStore::GetTransform(baseTransform).m_Position.x = 1;
	VansGraphics::VansTransformStore::GetTransform(tipTransform).m_Position.x = 1;
	combatService->Tick(.01);
	const auto reverseSnapshot = combatService->CaptureDebugSnapshot();
	if (!ExpectGAF(std::any_of(reverseSnapshot.hurtBodies.begin(), reverseSnapshot.hurtBodies.end(),
		[](const auto& body) { return body.hit && body.region == "Head" && body.target == "Survival Runtime Attacker"; }) &&
		attackerHost->ActiveActions().size() == 1,
		"Survival detection required a response animation or targeted the body entity as a separate character")) return false;
	if (!combatService->Release(reverseWindow.resource, error)) return ExpectGAF(false, error.c_str());
	std::cout << "[GAF] Detailed hits: Whisper=Chest Survival=Head multipartDedup=1 targetDataRoundTrip=1\n";

	combatService->Tick(0.25);
	if (!ExpectGAF(whisperHost->ActiveActions().size() == 1 &&
		combatService->CaptureDebugSnapshot().windows.front().hitCount == 1,
		"A single attack window applied the Whisper response more than once")) return false;
	gameplayRuntime.TickEarly(1.4);
	if (!ExpectGAF(whisperHost->ActiveActions().empty() &&
		!whisperCct.IsGameplayMovementBlocked() &&
		whisperController->GetCurrentStateName() == stateBeforeHit,
		"Whisper hit response did not release movement and restore animation state")) return false;

	std::cout << "[GAF] Production melee hit validated: window=PrimaryMeleeHit"
		" hits=1 response=TakingDamage1 movementBlockReleased=1\n";
	return true;
}

bool TestDemoHallCrouchLocomotionContract()
{
	namespace fs = std::filesystem;
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 6 && !fs::exists(workspace / "DemoHallProject"); ++depth)
		workspace = workspace.parent_path();
	const fs::path projectRoot = workspace / "DemoHallProject";
	std::string error;

	nlohmann::ordered_json animator;
	if (!Vans::VansJsonFileStorage::Read(
		projectRoot / "Assets/MotionMatchDataBase/UEFN_Mannequin.vanimator", animator, error))
		return ExpectGAF(false, error.c_str());

	struct StanceGraphExpectation
	{
		const char* graphSetId;
		const char* graphId;
		const char* clipName;
	};
	const std::array<StanceGraphExpectation, 2> expectedGraphs = {{
		{ "graph-set-crouch-enter", "graph-crouch-enter", "StandToCrouch" },
		{ "graph-set-crouch-exit", "graph-crouch-exit", "CrouchToStand" }
	}};
	for (const StanceGraphExpectation& expected : expectedGraphs)
	{
		bool foundSet = false;
		for (const auto& graphSet : animator.value("graphSets", nlohmann::ordered_json::array()))
		{
			if (graphSet.value("id", std::string{}) != expected.graphSetId) continue;
			const auto bindings = graphSet.value("bindings", nlohmann::ordered_json::array());
			foundSet = !bindings.empty() && bindings.front().value("enabled", false) &&
				bindings.front().value("graphId", std::string{}) == expected.graphId &&
				bindings.front().value("layerId", std::string{}) == "layer-base";
		}
		if (!ExpectGAF(foundSet, "DemoHall crouch Graph Set binding is missing")) return false;

		bool foundGraph = false;
		for (const auto& graph : animator.value("graphs", nlohmann::ordered_json::array()))
		{
			if (graph.value("id", std::string{}) != expected.graphId) continue;
			bool containsMotionMatching = false;
			bool preservesWindowBreakSlot = false;
			int matchingStates = 0;
			for (const auto& node : graph["graph"].value("nodes", nlohmann::ordered_json::array()))
			{
				containsMotionMatching = containsMotionMatching ||
					node.value("type", std::string{}) == "MotionMatching";
				preservesWindowBreakSlot = preservesWindowBreakSlot ||
					(node.value("type", std::string{}) == "Slot" &&
					node.value("properties", nlohmann::ordered_json::object())
						.value("slotId", std::string{}) == "slot-window-break-full-body");
				for (const auto& state : node.value("properties", nlohmann::ordered_json::object())
					.value("states", nlohmann::ordered_json::array()))
				{
					if (state.value("name", std::string{}) == expected.clipName &&
						state.value("clip", std::string{}) == expected.clipName &&
						!state.value("loop", true) && state.value("rootMotion", false) &&
						std::abs(state.value("speed", 0.0f) - 1.0f) < 0.0001f)
						++matchingStates;
				}
			}
			foundGraph = !containsMotionMatching && preservesWindowBreakSlot && matchingStates == 1;
		}
		if (!ExpectGAF(foundGraph,
			"DemoHall crouch Graph must be a non-MM root-motion one-shot and preserve the full-body Slot")) return false;
	}

	auto hasTransitionRule = [&](const char* from, const char* to, float duration)
	{
		for (const auto& rule : animator.value("graphSetTransitions", nlohmann::ordered_json::object())
			.value("rules", nlohmann::ordered_json::array()))
		{
			if (rule.value("from", std::string{}) != from ||
				rule.value("to", std::string{}) != to) continue;
			const auto policy = rule.value("policy", nlohmann::ordered_json::object());
			return policy.value("phase", std::string{}) == "matchNormalizedTime" &&
				!policy.value("requireStateMatch", true) &&
				policy.value("interruption", std::string{}) == "reject" &&
				policy.value("rootMotion", std::string{}) == "incomingOnly" &&
				std::abs(policy.value("duration", 0.0f) - duration) < 0.001f;
		}
		return false;
	};
	if (!ExpectGAF(
		hasTransitionRule("graph-set-default", "graph-set-crouch-enter", 0.08f) &&
		hasTransitionRule("graph-set-crouch-enter", "graph-set-default", 0.12f) &&
		hasTransitionRule("graph-set-default", "graph-set-crouch-exit", 0.08f) &&
		hasTransitionRule("graph-set-crouch-exit", "graph-set-default", 0.12f),
		"DemoHall crouch Graph Set transitions lost phase handoff/reject/incoming-only ownership")) return false;

	nlohmann::ordered_json scene;
	if (!Vans::VansJsonFileStorage::Read(projectRoot / "Scenes/DemoHall.json", scene, error))
		return ExpectGAF(false, error.c_str());
	int configuredCharacters = 0;
	std::optional<VansGraphics::MotionMatchingSettings> sceneMotionMatching;
	for (const auto& entity : scene.value("entities", nlohmann::ordered_json::array()))
	{
		const std::string entityId = entity.value("id", std::string{});
		if (entityId != "4186c86d-7c0a-4556-8077-d1f80ebc1da5" &&
			entityId != "38dbe7af-653a-4aeb-bfa7-1ca72e2b972c") continue;
		bool hasSingleOwnerMotionMatching = false;
		bool hasCharacterMove = false;
		for (const auto& component : entity.value("components", nlohmann::ordered_json::array()))
		{
			const auto data = component.value("data", nlohmann::ordered_json::object());
			if (component.value("type", std::string{}) == "Animation")
			{
				if (!sceneMotionMatching)
				{
					const Vans::VansSceneAnimationComponentConfig animationConfig =
						Vans::VansSceneAnimationComponentReader::ReadAuthoringAnimationComponent(
							Vans::DecodeSerializedValueJson(component));
					if (animationConfig.motionMatching)
						sceneMotionMatching = *animationConfig.motionMatching;
				}
				const auto motionMatching = data.value("motion_matching", nlohmann::ordered_json::object());
				bool hasStanceDatabase = false;
				for (const auto& database : motionMatching.value("databases", nlohmann::ordered_json::array()))
					hasStanceDatabase = hasStanceDatabase ||
						database.value("name", std::string{}) == "PSD_DemoHall_Stance_Transitions";
				bool hasStanceSelector = false;
				bool hasStandGraphSetHandoff = false;
				bool hasCrouchGraphSetHandoff = false;
				for (const auto& selector : motionMatching.value("selector", nlohmann::ordered_json::array()))
				{
					const std::string selectorName = selector.value("name", std::string{});
					hasStanceSelector = hasStanceSelector || selectorName == "StanceTransition";
					const auto databases = selector.value("databases", nlohmann::ordered_json::array());
					const bool targetsSingleDatabase = databases.size() == 1;
					hasStandGraphSetHandoff = hasStandGraphSetHandoff ||
						(selectorName == "StandGraphSetHandoff" &&
						selector.value("stance", std::string{}) == "Stand" &&
						selector.value("phase", std::string{}) == "Transition" &&
						selector.value("move_states", nlohmann::ordered_json::array()) ==
							nlohmann::ordered_json::array({ 0 }) && targetsSingleDatabase &&
						databases.front() == "PSD_DemoHall_Stand_Idles");
					hasCrouchGraphSetHandoff = hasCrouchGraphSetHandoff ||
						(selectorName == "CrouchGraphSetHandoff" &&
						selector.value("stance", std::string{}) == "Crouch" &&
						selector.value("phase", std::string{}) == "Transition" &&
						selector.value("move_states", nlohmann::ordered_json::array()) ==
							nlohmann::ordered_json::array({ 4 }) && targetsSingleDatabase &&
						databases.front() == "PSD_DemoHall_Crouch_Idles");
				}
				hasSingleOwnerMotionMatching = motionMatching.value("enabled", false) &&
					!hasStanceDatabase && !hasStanceSelector &&
					hasStandGraphSetHandoff && hasCrouchGraphSetHandoff;
			}
			else if (component.value("type", std::string{}) == "Script" &&
				data.value("entry", std::string{}) == "CharacterMove")
				hasCharacterMove = true;
		}
		if (hasSingleOwnerMotionMatching && hasCharacterMove) ++configuredCharacters;
	}
	if (!ExpectGAF(configuredCharacters == 2,
		"Both DemoHall characters must route stance transitions outside Motion Matching")) return false;

	std::string script;
	if (!Vans::VansFileStorage::ReadAllBytes(
		projectRoot / "Scripts/forest_lua_behaviors.lua", script, error))
		return ExpectGAF(false, error.c_str());
	const std::size_t moveBegin = script.find("M.CharacterMove");
	const std::size_t moveEnd = script.find("M.AnimationControl", moveBegin);
	const std::string moveScript = moveBegin != std::string::npos && moveEnd != std::string::npos
		? script.substr(moveBegin, moveEnd - moveBegin) : std::string{};
	if (!ExpectGAF(
		moveScript.find("is_key_pressed(\"LEFT_CONTROL\")") != std::string::npos &&
		moveScript.find("is_key_down(\"LEFT_CONTROL\")") == std::string::npos &&
		moveScript.find("self.requestedCrouching") != std::string::npos &&
		moveScript.find("self.resolvedCrouching") != std::string::npos &&
		moveScript.find("self:hold_stance_motion(nil)") != std::string::npos &&
		moveScript.find("self:write_stance_parameters(self.resolvedCrouching") != std::string::npos &&
		moveScript.find("self.crouchEnterGraphSetId") != std::string::npos &&
		moveScript.find("self.crouchExitGraphSetId") != std::string::npos &&
		moveScript.find("anim:switch_graph_set(self.locomotionGraphSetId)") != std::string::npos &&
		moveScript.find("set_root_motion_enabled(false)") == std::string::npos,
		"DemoHall CharacterMove does not own a persistent toggle crouch Graph Set flow")) return false;

	VansGraphics::Skeleton sceneSkeleton;
	if (!LoadContractSkeletonFromModel(
		projectRoot / "Assets/Models/SKM_UEFN_Mannequin.fbx", sceneSkeleton, error))
		return ExpectGAF(false, error.c_str());
	for (const StanceGraphExpectation& expected : expectedGraphs)
	{
		VansGraphics::AnimatorAssetData stanceAsset;
		if (!VansGraphics::VansAnimatorIO::Load(
			(projectRoot / "Assets/MotionMatchDataBase/UEFN_Mannequin.vanimator").string(), stanceAsset))
			return ExpectGAF(false, "DemoHall animator could not be loaded for crouch Graph validation");
		stanceAsset.defaultGraphSetId = expected.graphSetId;
		// 此处只验证基础蹲起 Clip；完整叠层由下方往返和手枪对照测试覆盖。
		stanceAsset.layers.resize(1);
		for (auto& graphSet : stanceAsset.graphSets) graphSet.bindings.resize(1);
		stanceAsset.graphSets.erase(std::remove_if(
			stanceAsset.graphSets.begin(), stanceAsset.graphSets.end(),
			[&](const VansGraphics::VansAnimationGraphSetDefinition& graphSet)
			{
				return graphSet.id != expected.graphSetId;
			}), stanceAsset.graphSets.end());
		stanceAsset.graphs.erase(std::remove_if(
			stanceAsset.graphs.begin(), stanceAsset.graphs.end(),
			[&](const VansGraphics::AnimatorGraphAsset& graph)
			{
				return graph.id != expected.graphId;
			}), stanceAsset.graphs.end());
		stanceAsset.graphSetTransitionRules.clear();
		stanceAsset.clipRefs.erase(std::remove_if(
			stanceAsset.clipRefs.begin(), stanceAsset.clipRefs.end(),
			[&](const VansGraphics::AnimatorClipRef& ref)
			{
				return ref.name != expected.clipName;
			}), stanceAsset.clipRefs.end());
		VansGraphics::VansAnimatorRuntimeCompileOptions options;
		options.enableTargetPostProcess = false;
		options.enableRootMotion = true;
		options.rigResolver = [&projectRoot](
			const std::string&, std::string& resolveError)
		{
			return LoadRigForContract(
				projectRoot / "Assets/AnimationRigs/UEFN.vanimrig", resolveError);
		};
		auto controller = VansGraphics::VansAnimatorRuntimeCompiler::Compile(
			stanceAsset, sceneSkeleton,
			[&projectRoot](const VansGraphics::AnimatorClipRef& ref,
				std::shared_ptr<const VansGraphics::VansAnimationClipAsset>& clip,
				std::string& resolveError)
			{
				return LoadAnimationClipAssetForContract(
					projectRoot / ref.pathHint, clip, resolveError);
			},
			ProjectMaskResolverForContract(projectRoot),
			options, error);
		if (!ExpectGAF(controller != nullptr, error.c_str())) return false;
		controller->Play();
		controller->Update(0.0f, sceneSkeleton);
		controller->Update(1.0f / 30.0f, sceneSkeleton);
		if (!ExpectGAF(controller->GetCurrentStateName() == expected.clipName &&
			controller->GetCurrentPlayTime() > 0.0f && controller->HasRootMotionDelta(),
			"DemoHall crouch Graph did not evaluate its authored root-motion one-shot")) return false;
	}

	if (!ExpectGAF(sceneMotionMatching.has_value(),
		"DemoHall crouch contract could not resolve the scene Motion Matching settings")) return false;
	VansGraphics::AnimatorAssetData roundTripAsset;
	if (!VansGraphics::VansAnimatorIO::Load(
		(projectRoot / "Assets/MotionMatchDataBase/UEFN_Mannequin.vanimator").string(), roundTripAsset))
		return ExpectGAF(false, "DemoHall Animator could not be loaded for stance round-trip validation");
	VansGraphics::VansAnimatorRuntimeCompileOptions roundTripOptions;
	roundTripOptions.enableTargetPostProcess = false;
	roundTripOptions.enableRootMotion = true;
	roundTripOptions.rigResolver = [&projectRoot](
		const std::string&, std::string& resolveError)
	{
		return LoadRigForContract(
			projectRoot / "Assets/AnimationRigs/UEFN.vanimrig", resolveError);
	};
	auto roundTripController = VansGraphics::VansAnimatorRuntimeCompiler::Compile(
		roundTripAsset, sceneSkeleton,
		[&projectRoot](const VansGraphics::AnimatorClipRef& ref,
			std::shared_ptr<const VansGraphics::VansAnimationClipAsset>& clip,
			std::string& resolveError)
		{
			return LoadAnimationClipAssetForContract(
				projectRoot / ref.pathHint, clip, resolveError);
		},
		ProjectMaskResolverForContract(projectRoot),
		roundTripOptions, error);
	if (!ExpectGAF(roundTripController != nullptr, error.c_str()) ||
		!ExpectGAF(roundTripController->ConfigureMotionMatching(*sceneMotionMatching, error), error.c_str()))
		return false;

	auto updateFrames = [&](int count)
	{
		for (int frame = 0; frame < count; ++frame)
			roundTripController->Update(1.0f / 60.0f, sceneSkeleton);
	};
	auto hasActiveDatabase = [&](const char* databaseName)
	{
		const auto* debug = roundTripController->GetMotionMatchingDebugData();
		return debug && std::find(debug->activeDatabases.begin(), debug->activeDatabases.end(),
			databaseName) != debug->activeDatabases.end();
	};
	auto setStance = [&](bool crouching)
	{
		roundTripController->SetFloat("Speed", 0.0f);
		roundTripController->SetFloat("Direction", 0.0f);
		roundTripController->SetFloat("IsCrouching", crouching ? 1.0f : 0.0f);
		roundTripController->SetFloat("IsAirborne", 0.0f);
		roundTripController->SetInt("MoveState", crouching ? 4 : 0);
	};
	auto switchGraphSet = [&](const char* graphSetId)
	{
		const auto result = roundTripController->SwitchGraphSet(graphSetId);
		return result == VansGraphics::VansGraphSetSwitchResult::Started ||
			result == VansGraphics::VansGraphSetSwitchResult::Completed ||
			result == VansGraphics::VansGraphSetSwitchResult::AlreadyActive;
	};

	setStance(false);
	roundTripController->Play();
	roundTripController->Update(0.0f, sceneSkeleton);
	updateFrames(12);
	const auto* standDebug = roundTripController->GetMotionMatchingDebugData();
	if (!ExpectGAF(standDebug && standDebug->activeClip == "Idle_Stand" &&
		hasActiveDatabase("PSD_DemoHall_Stand_Idles"),
		"DemoHall default Graph Set did not begin in standing Motion Matching")) return false;

	if (!ExpectGAF(switchGraphSet("graph-set-crouch-enter"),
		"DemoHall standing Graph Set rejected the crouch-enter switch")) return false;
	updateFrames(6);
	if (!ExpectGAF(roundTripController->GetActiveGraphSetId() == "graph-set-crouch-enter" &&
		roundTripController->GetCurrentStateName() == "StandToCrouch",
		"DemoHall crouch-enter Graph Set did not take animation ownership")) return false;
	updateFrames(150);
	setStance(true);
	if (!ExpectGAF(switchGraphSet("graph-set-default"),
		"DemoHall crouch-enter Graph Set rejected the locomotion return")) return false;
	updateFrames(12);
	const auto* crouchDebug = roundTripController->GetMotionMatchingDebugData();
	const bool crouchReturnValid =
		roundTripController->GetActiveGraphSetId() == "graph-set-default" &&
		!roundTripController->IsGraphSetTransitioning() && crouchDebug &&
		crouchDebug->activeClip == "Crouch_Idle" &&
		hasActiveDatabase("PSD_DemoHall_Crouch_Idles") &&
		!hasActiveDatabase("PSD_DemoHall_Stand_Idles");
	if (!crouchReturnValid && crouchDebug)
	{
		std::cout << "[GAF] Crouch return debug: graphSet="
			<< roundTripController->GetActiveGraphSetId()
			<< " transitioning=" << roundTripController->IsGraphSetTransitioning()
			<< " activeClip=" << crouchDebug->activeClip
			<< " selectedClip=" << crouchDebug->selectedClip
			<< " switches=" << crouchDebug->switches << " databases=";
		for (const std::string& database : crouchDebug->activeDatabases)
			std::cout << database << ',';
		std::cout << '\n';
	}
	if (!ExpectGAF(crouchReturnValid,
		"DemoHall crouch-enter return did not resolve to crouching Motion Matching")) return false;

	if (!ExpectGAF(switchGraphSet("graph-set-crouch-exit"),
		"DemoHall crouching Graph Set rejected the crouch-exit switch")) return false;
	updateFrames(6);
	if (!ExpectGAF(roundTripController->GetActiveGraphSetId() == "graph-set-crouch-exit" &&
		roundTripController->GetCurrentStateName() == "CrouchToStand",
		"DemoHall crouch-exit Graph Set did not take animation ownership")) return false;
	updateFrames(150);
	setStance(false);
	if (!ExpectGAF(switchGraphSet("graph-set-default"),
		"DemoHall crouch-exit Graph Set rejected the locomotion return")) return false;
	updateFrames(12);
	const auto* returnedStandDebug = roundTripController->GetMotionMatchingDebugData();
	if (!ExpectGAF(roundTripController->GetActiveGraphSetId() == "graph-set-default" &&
		!roundTripController->IsGraphSetTransitioning() && returnedStandDebug &&
		returnedStandDebug->activeClip == "Idle_Stand" &&
		hasActiveDatabase("PSD_DemoHall_Stand_Idles") &&
		!hasActiveDatabase("PSD_DemoHall_Crouch_Idles"),
		"DemoHall crouch-exit return did not resolve to standing Motion Matching")) return false;
	return true;
}

bool TestDemoHallPlayerVaultContract()
{
	namespace fs = std::filesystem;
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 6 && !fs::exists(workspace / "DemoHallProject"); ++depth)
		workspace = workspace.parent_path();
	const fs::path projectRoot = workspace / "DemoHallProject";
	const fs::path clipPath = projectRoot /
		"Assets/Animations/Traversal/Vault/anim_Vault_Unreal_Take.vclip";
	std::string error;

	VansGraphics::VansAnimationClip vaultClip;
	VansGraphics::Skeleton vaultSkeleton;
	if (!VansGraphics::VansAnimationClipIO::Load(clipPath.string(), vaultClip, vaultSkeleton))
		return ExpectGAF(false, "DemoHall vault .vclip could not be loaded");
	const auto root = vaultSkeleton.boneNameToIndex.find("root");
	if (!ExpectGAF(vaultClip.rootMotion.enabled && vaultClip.rootMotion.boneName == "root" &&
		vaultClip.rootMotion.extractTranslation && vaultClip.rootMotion.extractRotation &&
		std::abs(vaultClip.duration - 1.6f) < 0.001f &&
		root != vaultSkeleton.boneNameToIndex.end() && root->second >= 0 &&
		root->second < static_cast<int>(vaultClip.boneKeyframes.size()) &&
		vaultClip.boneKeyframes[root->second].size() > 1,
		"DemoHall vault clip lost its 1.6 second root-motion contract")) return false;
	float authoredRootTravel = 0.0f;
	const auto& rootKeys = vaultClip.boneKeyframes[root->second];
	for (const auto& key : rootKeys)
		authoredRootTravel = std::max(authoredRootTravel,
			glm::length(key.position - rootKeys.front().position));
	if (!ExpectGAF(authoredRootTravel > 100.0f,
		"DemoHall vault clip contains no meaningful authored traversal")) return false;

	nlohmann::ordered_json animator;
	if (!Vans::VansJsonFileStorage::Read(
		projectRoot / "Assets/MotionMatchDataBase/UEFN_Mannequin.vanimator", animator, error))
		return ExpectGAF(false, error.c_str());
	bool foundVaultClip = false;
	for (const auto& ref : animator.value("clips", nlohmann::ordered_json::array()))
	{
		const auto asset = ref.value("asset", nlohmann::ordered_json::object());
		foundVaultClip = foundVaultClip ||
			(ref.value("name", std::string{}) == "Vault" &&
			asset.value("guid", std::string{}) == "680c6d8e-9fa0-4401-879a-7211eb07f3f6" &&
			asset.value("pathHint", std::string{}) ==
				"Assets/Animations/Traversal/Vault/anim_Vault_Unreal_Take.vclip");
	}
	bool foundVaultSet = false;
	for (const auto& graphSet : animator.value("graphSets", nlohmann::ordered_json::array()))
	{
		if (graphSet.value("id", std::string{}) != "graph-set-vault") continue;
		const auto bindings = graphSet.value("bindings", nlohmann::ordered_json::array());
		foundVaultSet = !bindings.empty() &&
			bindings.front().value("graphId", std::string{}) == "graph-vault" &&
			bindings.front().value("layerId", std::string{}) == "layer-base" &&
			bindings.front().value("enabled", false);
	}
	bool vaultGraphValid = false;
	for (const auto& graph : animator.value("graphs", nlohmann::ordered_json::array()))
	{
		if (graph.value("id", std::string{}) != "graph-vault") continue;
		int vaultStates = 0;
		bool containsMotionMatching = false;
		for (const auto& node : graph["graph"].value("nodes", nlohmann::ordered_json::array()))
		{
			containsMotionMatching = containsMotionMatching ||
				node.value("type", std::string{}) == "MotionMatching";
			for (const auto& state : node.value("properties", nlohmann::ordered_json::object())
				.value("states", nlohmann::ordered_json::array()))
			{
				if (state.value("name", std::string{}) == "Vault" &&
					state.value("clip", std::string{}) == "Vault" &&
					!state.value("loop", true) && state.value("rootMotion", false))
					++vaultStates;
			}
		}
		vaultGraphValid = !containsMotionMatching && vaultStates == 1;
	}
	bool hasVaultEnterRule = false;
	bool hasVaultExitRule = false;
	for (const auto& rule : animator.value("graphSetTransitions", nlohmann::ordered_json::object())
		.value("rules", nlohmann::ordered_json::array()))
	{
		const auto policy = rule.value("policy", nlohmann::ordered_json::object());
		const bool rootMotionIncoming =
			policy.value("rootMotion", std::string{}) == "incomingOnly" &&
			policy.value("phase", std::string{}) == "restart";
		hasVaultEnterRule = hasVaultEnterRule || (rootMotionIncoming &&
			rule.value("from", std::string{}) == "graph-set-default" &&
			rule.value("to", std::string{}) == "graph-set-vault");
		hasVaultExitRule = hasVaultExitRule || (rootMotionIncoming &&
			rule.value("from", std::string{}) == "graph-set-vault" &&
			rule.value("to", std::string{}) == "graph-set-default");
	}

	VansGraphics::Skeleton sceneSkeleton;
	if (!LoadContractSkeletonFromModel(
		projectRoot / "Assets/Models/SKM_UEFN_Mannequin.fbx", sceneSkeleton, error))
		return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(sceneSkeleton.bones.size() == vaultSkeleton.bones.size(),
		"DemoHall UEFN model and vault clip use different skeleton sizes")) return false;

	VansGraphics::AnimatorAssetData animatorAsset;
	if (!VansGraphics::VansAnimatorIO::Load(
		(projectRoot / "Assets/MotionMatchDataBase/UEFN_Mannequin.vanimator").string(), animatorAsset))
		return ExpectGAF(false, "DemoHall UEFN Animator definition could not be loaded for vault");
	animatorAsset.defaultGraphSetId = "graph-set-vault";
	animatorAsset.graphSets.erase(std::remove_if(
		animatorAsset.graphSets.begin(), animatorAsset.graphSets.end(),
		[](const VansGraphics::VansAnimationGraphSetDefinition& graphSet)
		{
			return graphSet.id != "graph-set-vault";
		}), animatorAsset.graphSets.end());
	animatorAsset.graphs.erase(std::remove_if(
		animatorAsset.graphs.begin(), animatorAsset.graphs.end(),
		[](const VansGraphics::AnimatorGraphAsset& graph)
		{
			return graph.id != "graph-vault" && graph.id != "graph-target-post-process";
		}), animatorAsset.graphs.end());
	animatorAsset.graphSetTransitionRules.clear();
	animatorAsset.clipRefs.erase(std::remove_if(
		animatorAsset.clipRefs.begin(), animatorAsset.clipRefs.end(),
		[](const VansGraphics::AnimatorClipRef& ref)
		{
			return ref.name != "Vault";
		}), animatorAsset.clipRefs.end());
	VansGraphics::VansAnimatorRuntimeCompileOptions compileOptions;
	compileOptions.enableTargetPostProcess = true;
	compileOptions.enableRootMotion = true;
	compileOptions.queryProfileResolver = [](
		const std::string&, std::uint32_t& collisionMask, std::string&)
	{
		collisionMask = 0xFFFFFFFFu;
		return true;
	};
	compileOptions.rigResolver = [&projectRoot](
		const std::string&, std::string& resolveError)
	{
		return LoadRigForContract(
			projectRoot / "Assets/AnimationRigs/UEFN.vanimrig", resolveError);
	};
	auto vaultController = VansGraphics::VansAnimatorRuntimeCompiler::Compile(
		animatorAsset, sceneSkeleton,
		[&projectRoot](const VansGraphics::AnimatorClipRef& ref,
			std::shared_ptr<const VansGraphics::VansAnimationClipAsset>& clip,
			std::string& resolveError)
		{
			return LoadAnimationClipAssetForContract(
				projectRoot / ref.pathHint, clip, resolveError);
		},
		ProjectMaskResolverForContract(projectRoot),
		compileOptions, error);
	if (!ExpectGAF(vaultController != nullptr, error.c_str())) return false;
	vaultController->Play();
	vaultController->Update(0.0f, sceneSkeleton);
	float runtimeRootTravel = 0.0f;
	for (int frame = 0; frame < 36; ++frame)
	{
		vaultController->Update(1.0f / 30.0f, sceneSkeleton);
		if (vaultController->HasRootMotionDelta())
			runtimeRootTravel += glm::length(vaultController->GetRootMotionDelta());
	}
	if (!ExpectGAF(vaultController->GetCurrentStateName() == "Vault" &&
		vaultController->GetCurrentPlayTime() > 1.1f && runtimeRootTravel > 50.0f,
		"DemoHall vault Graph did not evaluate its root-motion clip at runtime")) return false;

	nlohmann::ordered_json scene;
	if (!Vans::VansJsonFileStorage::Read(projectRoot / "Scenes/DemoHall.json", scene, error))
		return ExpectGAF(false, error.c_str());
	int configuredCharacters = 0;
	for (const auto& entity : scene.value("entities", nlohmann::ordered_json::array()))
	{
		const std::string entityId = entity.value("id", std::string{});
		if (entityId != "4186c86d-7c0a-4556-8077-d1f80ebc1da5" &&
			entityId != "38dbe7af-653a-4aeb-bfa7-1ca72e2b972c") continue;
		bool animationRoutesRootMotion = false;
		bool hasCct = false;
		bool hasVaultScript = false;
		for (const auto& component : entity.value("components", nlohmann::ordered_json::array()))
		{
			const auto data = component.value("data", nlohmann::ordered_json::object());
			const std::string type = component.value("type", std::string{});
			if (type == "Animation")
			{
				const auto motionModel = data.value("motion_matching", nlohmann::ordered_json::object())
					.value("motion_model", nlohmann::ordered_json::object());
				animationRoutesRootMotion = data.value("root_motion", false) &&
					motionModel.value("drive_mode", std::string{}) == "root_motion" &&
					std::abs(motionModel.value("root_motion_to_world_scale", 0.0f) - 0.01f) < 0.0001f;
			}
			else if (type == "CharacterController")
				hasCct = true;
			else if (type == "Script" &&
				data.value("entry", std::string{}) == "PlayerVaultController")
			{
				const auto fields = data.value("fields", nlohmann::ordered_json::object());
				hasVaultScript = fields.value("vaultGraphSetId", std::string{}) == "graph-set-vault" &&
					fields.value("locomotionGraphSetId", std::string{}) == "graph-set-default" &&
					std::abs(fields.value("vaultDuration", 0.0f) - 1.6f) < 0.001f;
			}
		}
		if (animationRoutesRootMotion && hasCct && hasVaultScript) ++configuredCharacters;
	}
	std::string script;
	if (!Vans::VansFileStorage::ReadAllBytes(
		projectRoot / "Scripts/forest_lua_behaviors.lua", script, error))
		return ExpectGAF(false, error.c_str());
	const std::size_t vaultScriptBegin = script.find("M.PlayerVaultController");
	const std::size_t vaultScriptEnd = script.find("M.RuntimeStartGame", vaultScriptBegin);
	const std::string vaultScript = vaultScriptBegin != std::string::npos
		? script.substr(vaultScriptBegin, vaultScriptEnd - vaultScriptBegin) : std::string{};
	return ExpectGAF(foundVaultClip && foundVaultSet && vaultGraphValid &&
		hasVaultEnterRule && hasVaultExitRule && configuredCharacters == 2 &&
		vaultScript.find("is_key_pressed(\"V\")") != std::string::npos &&
		vaultScript.find("set_root_motion_enabled(true)") != std::string::npos &&
		vaultScript.find("switch_graph_set") != std::string::npos &&
		vaultScript.find("self.vaultElapsed >= self.vaultDuration") != std::string::npos &&
		script.find("character_state.fullBodyActionActive") != std::string::npos &&
		script.find("character_state.attackActive") == std::string::npos,
		"DemoHall vault is not wired through input, Graph Set, root motion, and CCT ownership");
}

bool TestDemoHallWhisperAIContract()
{
	namespace fs = std::filesystem;
	fs::path workspace = fs::current_path();
	for (int depth = 0; depth < 6 && !fs::exists(workspace / "DemoHallProject"); ++depth)
		workspace = workspace.parent_path();
	const fs::path projectRoot = workspace / "DemoHallProject";
	const fs::path whisperRoot = projectRoot / "Assets/Characters/Whisper";
	std::string error;

	VansGraphics::Skeleton modelSkeleton;
	if (!LoadContractSkeletonFromModel(
		whisperRoot / "Models/SK_Whisper.glb", modelSkeleton, error))
		return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(modelSkeleton.bones.size() == 68 &&
		modelSkeleton.boneNameToIndex.find("root") != modelSkeleton.boneNameToIndex.end() &&
		modelSkeleton.boneNameToIndex.find("pelvis") != modelSkeleton.boneNameToIndex.end() &&
		modelSkeleton.boneNameToIndex.find("head") != modelSkeleton.boneNameToIndex.end() &&
		modelSkeleton.boneNameToIndex.find("foot_l") != modelSkeleton.boneNameToIndex.end() &&
		modelSkeleton.boneNameToIndex.find("foot_r") != modelSkeleton.boneNameToIndex.end() &&
		modelSkeleton.boneNameToIndex.find("ball_l") != modelSkeleton.boneNameToIndex.end() &&
		modelSkeleton.boneNameToIndex.find("ball_r") != modelSkeleton.boneNameToIndex.end(),
		"DemoHall Whisper model did not retain its weighted 68-bone skeleton")) return false;
	std::vector<glm::mat4> whisperBindGlobals(
		modelSkeleton.bones.size(), glm::mat4(1.0f));
	for (int bone : modelSkeleton.topologicalOrder)
	{
		whisperBindGlobals[static_cast<std::size_t>(bone)] =
			modelSkeleton.bones[static_cast<std::size_t>(bone)].localTransform;
		const int parent = modelSkeleton.bones[static_cast<std::size_t>(bone)].parentIndex;
		if (parent >= 0)
			whisperBindGlobals[static_cast<std::size_t>(bone)] =
				whisperBindGlobals[static_cast<std::size_t>(parent)] *
				whisperBindGlobals[static_cast<std::size_t>(bone)];
	}
	const auto bindPosition = [&whisperBindGlobals](int bone)
	{
		return glm::vec3(whisperBindGlobals[static_cast<std::size_t>(bone)][3]);
	};
	glm::vec3 whisperAuthoredToeForward =
		(bindPosition(modelSkeleton.boneNameToIndex.at("ball_l")) -
		 bindPosition(modelSkeleton.boneNameToIndex.at("foot_l"))) +
		(bindPosition(modelSkeleton.boneNameToIndex.at("ball_r")) -
		 bindPosition(modelSkeleton.boneNameToIndex.at("foot_r")));
	whisperAuthoredToeForward.y = 0.0f;
	if (!ExpectGAF(glm::length(whisperAuthoredToeForward) > 1.0e-4f,
		"DemoHall Whisper bind pose does not expose a stable anatomical forward")) return false;
	whisperAuthoredToeForward = glm::normalize(whisperAuthoredToeForward);

	struct ClipExpectation
	{
		const char* file;
		float duration;
	};
	static constexpr ClipExpectation Clips[] = {
		{ "Anim_WhisperDead_Unreal_Take.vclip", 0.1f },
		{ "anima_whisper_sittoidle_Unreal_Take.vclip", 3.0f },
		{ "Anim_Whisper_Idle1_Unreal_Take.vclip", 1.9f },
		{ "Anim_Whisper_Idle2_Unreal_Take.vclip", 11.266666f },
		{ "Anim_Whisper_Idle3_Unreal_Take.vclip", 1.833333f },
		{ "Anim_Whisper_Idle4_Unreal_Take.vclip", 11.266666f },
		{ "Anim_Whisper_Walk1_Unreal_Take.vclip", 1.366667f },
		{ "Anim_Whisper_Walk2_Unreal_Take.vclip", 1.333333f },
		{ "Anim_Whisper_Walk_Back_Unreal_Take.vclip", 1.366667f },
		{ "Anim_Whisper_Walk_Left_Unreal_Take.vclip", 1.266667f },
		{ "Anim_Whisper_Walk_Right_Unreal_Take.vclip", 1.266667f },
		{ "Anim_Whisper_Run_Unreal_Take.vclip", 0.633333f },
		{ "Anim_Whisper_Attack1_Unreal_Take.vclip", 1.833333f },
		{ "Anim_Whisper_Attack2_Unreal_Take.vclip", 1.733333f },
		{ "Anim_Whisper_Attack3_Unreal_Take.vclip", 2.1f },
		{ "Anim_Whisper_Taking_Damage1_Unreal_Take.vclip", 1.333333f },
		{ "Anim_Whisper_Taking_Damage2_Unreal_Take.vclip", 0.866667f },
		{ "Anim_Whisper_Death_Unreal_Take.vclip", 2.166667f },
	};
	for (const auto& expected : Clips)
	{
		VansGraphics::VansAnimationClip clip;
		VansGraphics::Skeleton clipSkeleton;
		if (!VansGraphics::VansAnimationClipIO::Load(
			(whisperRoot / "Animations" / expected.file).string(), clip, clipSkeleton))
			return ExpectGAF(false, "DemoHall Whisper preview clip could not be loaded");
		if (!ExpectGAF(std::abs(clip.duration - expected.duration) < 0.002f &&
			clipSkeleton.bones.size() == modelSkeleton.bones.size() &&
			clip.boneKeyframes.size() == modelSkeleton.bones.size(),
			"DemoHall Whisper preview clip duration or skeleton binding changed")) return false;
		for (std::size_t bone = 0; bone < modelSkeleton.bones.size(); ++bone)
			if (!ExpectGAF(clipSkeleton.bones[bone].name == modelSkeleton.bones[bone].name,
				"DemoHall Whisper clip bone order does not match its model")) return false;
		const int spineIndex = modelSkeleton.boneNameToIndex.at("spine_01");
		const auto& spineKeys = clip.boneKeyframes[static_cast<std::size_t>(spineIndex)];
		const glm::vec3 spineBind = glm::vec3(
			modelSkeleton.bones[static_cast<std::size_t>(spineIndex)].localTransform[3]);
		const bool spineTranslationIsValid = !spineKeys.empty() &&
			std::isfinite(spineKeys.front().position.x) &&
			std::isfinite(spineKeys.front().position.y) &&
			std::isfinite(spineKeys.front().position.z) &&
			glm::length(spineKeys.front().position - spineBind) < 0.001f;
		if (!ExpectGAF(spineTranslationIsValid,
			"DemoHall Whisper clip coordinates do not match the glTF skeleton space")) return false;
		if (std::string(expected.file) == "Anim_Whisper_Idle1_Unreal_Take.vclip" &&
			!ExpectGAF(std::abs(spineKeys.front().rotation.y + 0.1038613f) < 0.001f &&
				std::abs(spineKeys.front().rotation.z - 0.0059622f) < 0.001f,
				"DemoHall Whisper clip rotation axes do not match the glTF skeleton space")) return false;
	}

	const auto sampleWhisperClipGlobals = [&whisperRoot, &modelSkeleton](
		const char* file, float requestedTime, std::vector<glm::mat4>& outGlobals)
	{
		VansGraphics::VansAnimationClip clip;
		VansGraphics::Skeleton clipSkeleton;
		if (!VansGraphics::VansAnimationClipIO::Load(
			(whisperRoot / "Animations" / file).string(), clip, clipSkeleton)) return false;
		VansGraphics::VansAnimationSampleRequest request;
		request.currentTime = std::min(requestedTime, std::max(0.0f, clip.duration - 0.0001f));
		request.endTime = clip.duration;
		request.loop = false;
		VansGraphics::VansPosePayload pose;
		if (!VansGraphics::VansAnimationSampler::Sample(
			clip, modelSkeleton, request, pose)) return false;
		outGlobals.assign(modelSkeleton.bones.size(), glm::mat4(1.0f));
		for (std::size_t bone = 0; bone < outGlobals.size(); ++bone)
			outGlobals[bone] = VansGraphics::VansPoseMath::Compose(pose.localPose[bone]);
		for (int bone : modelSkeleton.topologicalOrder)
		{
			const int parent = modelSkeleton.bones[static_cast<std::size_t>(bone)].parentIndex;
			if (parent >= 0)
				outGlobals[static_cast<std::size_t>(bone)] =
					outGlobals[static_cast<std::size_t>(parent)] *
					outGlobals[static_cast<std::size_t>(bone)];
		}
		return true;
	};
	std::vector<glm::mat4> sitEndGlobals;
	std::vector<glm::mat4> idleStartGlobals;
	std::vector<glm::mat4> walkStartGlobals;
	if (!sampleWhisperClipGlobals(
		"anima_whisper_sittoidle_Unreal_Take.vclip", 3.0f, sitEndGlobals) ||
		!sampleWhisperClipGlobals(
			"Anim_Whisper_Idle1_Unreal_Take.vclip", 0.0f, idleStartGlobals) ||
		!sampleWhisperClipGlobals(
			"Anim_Whisper_Walk1_Unreal_Take.vclip", 0.0f, walkStartGlobals))
		return ExpectGAF(false, "DemoHall Whisper facing diagnostic clips could not be sampled");
	const auto rotationDifferenceDegrees = [](const glm::mat4& lhs, const glm::mat4& rhs)
	{
		const glm::quat lhsRotation = glm::normalize(glm::quat_cast(glm::mat3(lhs)));
		const glm::quat rhsRotation = glm::normalize(glm::quat_cast(glm::mat3(rhs)));
		const float cosine = glm::clamp(std::abs(glm::dot(lhsRotation, rhsRotation)), 0.0f, 1.0f);
		return 2.0f * std::acos(cosine) * 57.2957795f;
	};
	const int rootIndex = modelSkeleton.boneNameToIndex.at("root");
	const int pelvisIndex = modelSkeleton.boneNameToIndex.at("pelvis");
	const float sitToIdleRootDelta = rotationDifferenceDegrees(
			sitEndGlobals[static_cast<std::size_t>(rootIndex)],
			idleStartGlobals[static_cast<std::size_t>(rootIndex)]);
	const float sitToIdlePelvisDelta = rotationDifferenceDegrees(
			sitEndGlobals[static_cast<std::size_t>(pelvisIndex)],
			idleStartGlobals[static_cast<std::size_t>(pelvisIndex)]);
	const float sitToWalkRootDelta = rotationDifferenceDegrees(
			sitEndGlobals[static_cast<std::size_t>(rootIndex)],
			walkStartGlobals[static_cast<std::size_t>(rootIndex)]);
	const float sitToWalkPelvisDelta = rotationDifferenceDegrees(
			sitEndGlobals[static_cast<std::size_t>(pelvisIndex)],
			walkStartGlobals[static_cast<std::size_t>(pelvisIndex)]);
	std::cout << "[GAF] Whisper facing deltas: SitEnd->IdleStart root="
		<< sitToIdleRootDelta << " pelvis=" << sitToIdlePelvisDelta
		<< " SitEnd->WalkStart root=" << sitToWalkRootDelta
		<< " pelvis=" << sitToWalkPelvisDelta << '\n';
	if (!ExpectGAF(sitToIdleRootDelta < 0.1f && sitToIdlePelvisDelta < 1.0f &&
		sitToWalkRootDelta < 0.1f && sitToWalkPelvisDelta < 20.0f,
		"DemoHall Whisper stand-up, Idle, and Walk clips do not share one facing basis"))
	{
		return false;
	}

	VansGraphics::VansAnimationClip deformationClip;
	VansGraphics::Skeleton deformationClipSkeleton;
	if (!VansGraphics::VansAnimationClipIO::Load(
		(whisperRoot / "Animations/Anim_Whisper_Idle1_Unreal_Take.vclip").string(),
		deformationClip, deformationClipSkeleton))
		return ExpectGAF(false, "DemoHall Whisper deformation clip could not be loaded");
	VansGraphics::VansAnimationSampleRequest sampleRequest;
	sampleRequest.currentTime = 0.5f;
	sampleRequest.endTime = deformationClip.duration;
	sampleRequest.loop = true;
	VansGraphics::VansPosePayload sampledPose;
	if (!VansGraphics::VansAnimationSampler::Sample(
		deformationClip, modelSkeleton, sampleRequest, sampledPose))
		return ExpectGAF(false, "DemoHall Whisper deformation pose could not be sampled");
	std::vector<glm::mat4> deformationGlobals(modelSkeleton.bones.size(), glm::mat4(1.0f));
	for (std::size_t bone = 0; bone < deformationGlobals.size(); ++bone)
		deformationGlobals[bone] = VansGraphics::VansPoseMath::Compose(sampledPose.localPose[bone]);
	for (int bone : modelSkeleton.topologicalOrder)
	{
		const int parent = modelSkeleton.bones[static_cast<std::size_t>(bone)].parentIndex;
		if (parent >= 0)
			deformationGlobals[static_cast<std::size_t>(bone)] =
				deformationGlobals[static_cast<std::size_t>(parent)] *
				deformationGlobals[static_cast<std::size_t>(bone)];
	}
	VansGraphics::BoneMatricesSSBO deformationMatrices{};
	for (glm::mat4& matrix : deformationMatrices.boneMatrices)
		matrix = glm::mat4(1.0f);
	for (std::size_t bone = 0; bone < modelSkeleton.bones.size(); ++bone)
		deformationMatrices.boneMatrices[bone] = deformationGlobals[bone] *
			modelSkeleton.bones[bone].offsetMatrix;

	Vans::VansAssetMeta whisperModelMeta;
	if (!Vans::VansAssetMetaStorage::Load(
		whisperRoot / "Models/SK_Whisper.glb.meta", whisperModelMeta, error))
		return ExpectGAF(false, error.c_str());
	VansGraphics::VansAnimationPreviewRenderer deformationPreview;
	if (!deformationPreview.PrepareCpu(
		whisperRoot / "Models/SK_Whisper.glb", 1.0f,
		Vans::ReadSkeletalMeshImportSettings(whisperModelMeta), error) ||
		!deformationPreview.RasterizeFrame(
			deformationMatrices, {}, glm::vec3(0.0f), {}, error))
		return ExpectGAF(false, error.c_str());
	const auto& deformationStats = deformationPreview.GetStats();
	std::cout << "[GAF] Whisper skin stats: vertices=" << deformationStats.vertexCount
		<< " unbound=" << deformationStats.unboundVertexCount
		<< " invalidInfluences=" << deformationStats.invalidBoneInfluenceCount
		<< " nonFiniteWeights=" << deformationStats.nonFiniteBoneWeightCount
		<< " maxWeightSumError=" << deformationStats.maxBoneWeightSumError
		<< " invalidDeformed=" << deformationStats.invalidDeformedVertexCount
		<< " radiusRatio=" << deformationStats.deformedRadiusRatio << '\n';
	if (!ExpectGAF(deformationStats.unboundVertexCount == 0 &&
		deformationStats.invalidBoneInfluenceCount == 0 &&
		deformationStats.nonFiniteBoneWeightCount == 0 &&
		deformationStats.maxBoneWeightSumError < 1.0e-5f &&
		deformationStats.invalidDeformedVertexCount == 0 &&
		deformationStats.deformedRadiusRatio > 0.8f &&
		deformationStats.deformedRadiusRatio < 1.25f,
		"DemoHall Whisper asset contains invalid weights or produces exploded CPU skinning")) return false;

	VansGraphics::AnimatorAssetData whisperAnimatorAsset;
	if (!VansGraphics::VansAnimatorIO::Load(
		(whisperRoot / "Animation/Whisper.vanimator").string(), whisperAnimatorAsset))
		return ExpectGAF(false, "DemoHall Whisper Animator definition could not be loaded");
	VansGraphics::VansAnimatorRuntimeCompileOptions whisperCompileOptions;
	whisperCompileOptions.enableTargetPostProcess = false;
	whisperCompileOptions.enableRootMotion = false;
	whisperCompileOptions.rigResolver = [&whisperRoot](
		const std::string&, std::string& resolveError)
	{
		return LoadRigForContract(
			whisperRoot / "Animation/Whisper.vanimrig", resolveError);
	};
	auto whisperController = VansGraphics::VansAnimatorRuntimeCompiler::Compile(
		whisperAnimatorAsset, modelSkeleton,
		[&projectRoot](const VansGraphics::AnimatorClipRef& ref,
			std::shared_ptr<const VansGraphics::VansAnimationClipAsset>& clip,
			std::string& resolveError)
		{
			return LoadAnimationClipAssetForContract(
				projectRoot / ref.pathHint, clip, resolveError);
		},
		[](const VansGraphics::VansAnimationLayerDefinition&,
			std::shared_ptr<const VansGraphics::VansBoneMaskAsset>&,
			std::string& resolveError)
		{
			resolveError = "DemoHall Whisper base layer unexpectedly requested a Bone Mask";
			return false;
		},
		whisperCompileOptions, error);
	if (!ExpectGAF(whisperController != nullptr, error.c_str())) return false;
	const auto& whisperParameters = whisperController->GetParameters();
	const auto whisperWakeParameter = whisperParameters.find("WakeUp");
	const bool demoHallHasWakeTrigger = whisperWakeParameter != whisperParameters.end() &&
		whisperWakeParameter->second.type == VansGraphics::AnimatorParamType::Trigger;
	whisperController->Play();
	whisperController->Update(0.0f, modelSkeleton);
	const bool demoHallBeganDead = whisperController->GetCurrentStateName() == "Dead";
	if (!deformationPreview.RasterizeFrame(
		whisperController->GetBoneMatricesSSBO(), {}, glm::vec3(0.0f), {}, error))
		return ExpectGAF(false, error.c_str());
	const VansGraphics::VansAnimationPreviewRenderStats deadPreviewStats =
		deformationPreview.GetStats();
	std::cout << "[GAF] Whisper Dead preview: renderedTriangles="
		<< deadPreviewStats.renderedTriangleCount
		<< " invalidDeformed=" << deadPreviewStats.invalidDeformedVertexCount
		<< " boundsMin=(" << deadPreviewStats.deformedBoundsMin.x << ','
		<< deadPreviewStats.deformedBoundsMin.y << ','
		<< deadPreviewStats.deformedBoundsMin.z << ") boundsMax=("
		<< deadPreviewStats.deformedBoundsMax.x << ','
		<< deadPreviewStats.deformedBoundsMax.y << ','
		<< deadPreviewStats.deformedBoundsMax.z << ") radiusRatio="
		<< deadPreviewStats.deformedRadiusRatio << '\n';
	whisperController->Update(0.5f, modelSkeleton);
	const bool demoHallLoopedDead = whisperController->GetCurrentStateName() == "Dead";
	whisperController->SetTrigger("WakeUp");
	whisperController->Update(0.0f, modelSkeleton);
	const bool demoHallEnteredSitToIdle =
		whisperController->GetCurrentStateName() == "SitToIdle";
	whisperController->Update(1.5f, modelSkeleton);
	if (!deformationPreview.RasterizeFrame(
		whisperController->GetBoneMatricesSSBO(), {}, glm::vec3(0.0f), {}, error))
		return ExpectGAF(false, error.c_str());
	const VansGraphics::VansAnimationPreviewRenderStats sitToIdlePreviewStats =
		deformationPreview.GetStats();
	std::cout << "[GAF] Whisper SitToIdle preview: renderedTriangles="
		<< sitToIdlePreviewStats.renderedTriangleCount
		<< " invalidDeformed=" << sitToIdlePreviewStats.invalidDeformedVertexCount
		<< " radiusRatio=" << sitToIdlePreviewStats.deformedRadiusRatio << '\n';
	whisperController->Update(1.501f, modelSkeleton);
	const bool demoHallEnteredIdle = whisperController->GetCurrentStateName() == "Idle1";
	whisperController->Update(0.5f, modelSkeleton);
	if (!deformationPreview.RasterizeFrame(
		whisperController->GetBoneMatricesSSBO(), {}, glm::vec3(0.0f), {}, error))
		return ExpectGAF(false, error.c_str());
	const auto& controllerStats = deformationPreview.GetStats();
	float maxControllerMatrixDiff = 0.0f;
	for (std::size_t bone = 0; bone < modelSkeleton.bones.size(); ++bone)
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
				maxControllerMatrixDiff = std::max(maxControllerMatrixDiff,
					std::abs(whisperController->GetBoneMatricesSSBO().boneMatrices[bone][column][row]
						- deformationMatrices.boneMatrices[bone][column][row]));
	if (!ExpectGAF(demoHallHasWakeTrigger && demoHallBeganDead && demoHallLoopedDead &&
		deadPreviewStats.renderedTriangleCount > 0 &&
		deadPreviewStats.invalidDeformedVertexCount == 0 &&
		deadPreviewStats.deformedRadiusRatio > 0.8f &&
		deadPreviewStats.deformedRadiusRatio < 2.0f &&
		demoHallEnteredSitToIdle && demoHallEnteredIdle &&
		sitToIdlePreviewStats.renderedTriangleCount > 0 &&
		sitToIdlePreviewStats.invalidDeformedVertexCount == 0 &&
		sitToIdlePreviewStats.deformedRadiusRatio > 0.8f &&
		sitToIdlePreviewStats.deformedRadiusRatio < 2.0f &&
		whisperController->GetCurrentStateName() == "Idle1" &&
		controllerStats.invalidDeformedVertexCount == 0 &&
		controllerStats.deformedRadiusRatio > 0.8f &&
		controllerStats.deformedRadiusRatio < 1.25f &&
		maxControllerMatrixDiff < 1.0e-5f,
		"DemoHall Whisper runtime controller produced invalid or exploded skinning")) return false;
	const VansGraphics::VansCompiledAnimationRig* whisperRig = whisperController->GetAnimationRig();
	if (!ExpectGAF(whisperRig &&
		glm::dot(whisperRig->modelForward, whisperAuthoredToeForward) > 0.9999f,
		"DemoHall Whisper Rig forward does not match the skeleton toe direction"))
	{
		return false;
	}
	whisperController->SetInt("MoveState", 1);
	whisperController->Update(0.13f, modelSkeleton);
	const bool demoHallEnteredWalk = whisperController->GetCurrentStateName() == "Walk1";
	whisperController->SetInt("MoveState", 2);
	whisperController->Update(0.13f, modelSkeleton);
	const bool demoHallEnteredRun = whisperController->GetCurrentStateName() == "Run";
	whisperController->SetInt("MoveState", 0);
	whisperController->Update(0.13f, modelSkeleton);
	if (!ExpectGAF(demoHallEnteredWalk && demoHallEnteredRun &&
		whisperController->GetCurrentStateName() == "Idle1",
		"DemoHall Whisper MoveState does not route Idle, Walk, and Run")) return false;
	nlohmann::ordered_json animator;
	if (!Vans::VansJsonFileStorage::Read(
		whisperRoot / "Animation/Whisper.vanimator", animator, error))
		return ExpectGAF(false, error.c_str());
	int stateCount = 0;
	int transitionCount = 0;
	bool containsMotionMatching = false;
	bool allStatesDisableRootMotion = true;
	bool defaultsToDead = false;
	for (const auto& graph : animator.value("graphs", nlohmann::ordered_json::array()))
		for (const auto& node : graph.value("graph", nlohmann::ordered_json::object())
			.value("nodes", nlohmann::ordered_json::array()))
		{
			containsMotionMatching = containsMotionMatching ||
				node.value("type", std::string{}) == "MotionMatching";
			const auto properties = node.value("properties", nlohmann::ordered_json::object());
			defaultsToDead = defaultsToDead ||
				properties.value("defaultState", std::string{}) == "Dead";
			for (const auto& state : properties.value("states", nlohmann::ordered_json::array()))
			{
				++stateCount;
				allStatesDisableRootMotion = allStatesDisableRootMotion &&
					!state.value("rootMotion", true);
			}
			transitionCount += static_cast<int>(
				properties.value("transitions", nlohmann::ordered_json::array()).size());
		}
	bool hasMoveParameter = false;
	bool hasWakeParameter = false;
	for (const auto& parameter : animator.value("parameters", nlohmann::ordered_json::array()))
	{
		hasMoveParameter = hasMoveParameter ||
			(parameter.value("name", std::string{}) == "MoveState" &&
			parameter.value("type", std::string{}) == "int" &&
			parameter.value("default", -1) == 0);
		hasWakeParameter = hasWakeParameter ||
			(parameter.value("name", std::string{}) == "WakeUp" &&
			parameter.value("type", std::string{}) == "trigger");
	}
	if (!ExpectGAF(animator.value("clips", nlohmann::ordered_json::array()).size() == 18 &&
		animator.value("graphs", nlohmann::ordered_json::array()).size() == 1 &&
		stateCount == 18 && transitionCount == 8 && defaultsToDead &&
		hasMoveParameter && hasWakeParameter &&
		!containsMotionMatching && allStatesDisableRootMotion,
		"DemoHall Whisper animator is not the Dead-gated gameplay locomotion graph")) return false;

	nlohmann::ordered_json material;
	if (!Vans::VansJsonFileStorage::Read(
		whisperRoot / "Materials/Whisper_Body.mat", material, error))
		return ExpectGAF(false, error.c_str());
	const auto textures = material.value("textures", nlohmann::ordered_json::object());
	if (!ExpectGAF(material.value("materialType", std::string{}) == "pbr" &&
		textures.size() == 5 &&
		fs::is_regular_file(whisperRoot / "Textures/Whisper_Body_Albedo.png") &&
		fs::is_regular_file(whisperRoot / "Textures/Whisper_Body_Normal.png") &&
		fs::is_regular_file(whisperRoot / "Textures/Whisper_Body_AO.png") &&
		fs::is_regular_file(whisperRoot / "Textures/Whisper_Body_Metallic.png") &&
		fs::is_regular_file(whisperRoot / "Textures/Whisper_Body_Roughness.png"),
		"DemoHall Whisper PBR material or imported textures are incomplete")) return false;

	nlohmann::ordered_json scene;
	if (!Vans::VansJsonFileStorage::Read(projectRoot / "Scenes/DemoHall.json", scene, error))
		return ExpectGAF(false, error.c_str());
	bool foundWhisper = false;
	glm::vec3 whisperPosition(0.0f);
	std::vector<glm::vec3> playerPositions;
	for (const auto& entity : scene.value("entities", nlohmann::ordered_json::array()))
	{
		const bool isWhisperEntity = entity.value("id", std::string{}) ==
			"d21cfc31-dba5-44cb-a271-f0f2c20cded1" &&
			entity.value("name", std::string{}) == "Whisper";
		const auto position = entity.value("components", nlohmann::ordered_json::array());
		glm::vec3 entityPosition(0.0f);
		bool hasTransform = false;
		bool playerTargetTag = false;
		for (const auto& component : position)
		{
			const auto data = component.value("data", nlohmann::ordered_json::object());
			if (component.value("type", std::string{}) == "Transform")
			{
				const auto values = data.value("position", nlohmann::ordered_json::array());
				if (values.size() == 3)
				{
					entityPosition = glm::vec3(values[0].get<float>(),
						values[1].get<float>(), values[2].get<float>());
					hasTransform = true;
				}
			}
			else if (component.value("type", std::string{}) == "ActionHost")
				for (const auto& initializer : data.value(
					"initializers", nlohmann::ordered_json::array()))
					playerTargetTag = playerTargetTag ||
						initializer.value("type", std::string{}) ==
							"Gameplay.Tags.Initialize" &&
						initializer.value("inputs", nlohmann::ordered_json::object())
							.value("tag", std::string{}) == "Target.Character.Player";
		}
		if (hasTransform && playerTargetTag) playerPositions.push_back(entityPosition);
		if (!isWhisperEntity) continue;
		bool hasModel = false;
		bool hasAnimation = false;
		bool hasCharacterController = false;
		bool hasNavigationAgent = false;
		bool hasAIAgent = false;
		bool hasPreviewScript = false;
		whisperPosition = entityPosition;
		for (const auto& component : entity.value("components", nlohmann::ordered_json::array()))
		{
			const std::string type = component.value("type", std::string{});
			const auto data = component.value("data", nlohmann::ordered_json::object());
			if (type == "ModelRenderer")
				hasModel = component.value("enabled", false) &&
					data.value("model", nlohmann::ordered_json::object())
					.value("guid", std::string{}) == "d08d594d-f68c-4497-8663-c85a4695f2d6";
			else if (type == "Animation")
				hasAnimation = component.value("enabled", false) &&
					data.value("mesh_group", std::string{}) == "Whisper" &&
					!data.value("root_motion", true) &&
					data.value("auto_play", false) && !data.contains("motion_matching") &&
					data.value("animator", nlohmann::ordered_json::object())
						.value("guid", std::string{}) == "ec68421a-fff2-4db4-82c4-00cbb052c84c" &&
					data.value("rig", nlohmann::ordered_json::object())
						.value("guid", std::string{}) == "873c655f-18a8-4aea-8632-ff095e6cc7cc";
			else if (type == "CharacterController")
				hasCharacterController = component.value("enabled", false) &&
					data.value("layer", std::string{}) == "Enemy" &&
					std::abs(data.value("radius", 0.0f) - 0.35f) < 0.001f;
			else if (type == "NavigationAgent")
				hasNavigationAgent = component.value("enabled", false) &&
					data.value("navigationMesh", nlohmann::ordered_json::object())
						.value("guid", std::string{}) ==
						"fcb8f186-b52d-48fc-bd57-8315d4b0fcad" &&
					std::abs(data.value("maxSpeed", 0.0f) - 1.5f) < 0.001f &&
					std::abs(data.value("acceleration", 0.0f) - 6.0f) < 0.001f &&
					std::abs(data.value("stoppingDistance", 0.0f) - 1.6f) < 0.001f;
			else if (type == "AIAgent")
			{
				const auto facing = data.value("facing", nlohmann::ordered_json::object());
				const auto sight = data.value("sight", nlohmann::ordered_json::object());
				hasAIAgent = component.value("enabled", false) &&
					data.value("behavior", nlohmann::ordered_json::object())
						.value("guid", std::string{}) ==
						"7a8d6d19-57fc-4d5a-9a94-8b1ef226d301" &&
					data.value("targetTag", std::string{}) == "Target.Character.Player" &&
					data.value("readyAnimationState", std::string{}) == "Idle1" &&
					data.value("movementParameter", std::string{}) == "MoveState" &&
					data.value("maxMovementState", 2) == 1 &&
					facing.value("yawOnly", false) &&
					sight.value("enabled", false) &&
					sight.value("blackboardKey", std::string{}) == "HasVisualTarget" &&
					std::abs(sight.value("range", 0.0f) - 14.0f) < 0.001f &&
					std::abs(sight.value("horizontalFovDegrees", 0.0f) - 240.0f) < 0.001f &&
					sight.value("occlusionLayer", std::string{}) == "Environment";
			}
			else if (type == "Script")
				hasPreviewScript = hasPreviewScript ||
					data.value("entry", std::string{}) == "WhisperAnimationPreview";
		}
		foundWhisper = hasTransform && hasModel && hasAnimation &&
			hasCharacterController && hasNavigationAgent && hasAIAgent && !hasPreviewScript;
	}
	Vans::VansAIBehaviorAsset whisperBehavior;
	if (!Vans::VansAIBehaviorAssetStorage::Load(
		projectRoot / "Assets/AI/WhisperChase.vaibehavior", whisperBehavior, error))
		return ExpectGAF(false, error.c_str());
	const auto* dormantState = whisperBehavior.FindState("Dormant");
	const auto* awakeningState = whisperBehavior.FindState("Awakening");
	const auto* armedIdleState = whisperBehavior.FindState("ArmedIdle");
	const auto* patrolState = whisperBehavior.FindState("Patrol");
	const auto* chaseState = whisperBehavior.FindState("Chase");
	const bool behaviorIsPickupGatedChase =
		whisperBehavior.initialState == "Dormant" && dormantState && awakeningState &&
		armedIdleState && patrolState && chaseState &&
		!dormantState->transitions.empty() &&
		dormantState->transitions.front().condition.key == "ActivationRequested" &&
		!awakeningState->transitions.empty() &&
		awakeningState->transitions.front().condition.expectedString == "$ready" &&
		!armedIdleState->transitions.empty() &&
		armedIdleState->transitions.front().targetState == "Patrol" &&
		patrolState->task == Vans::VansAITaskKind::Patrol && patrolState->patrol &&
		std::abs(patrolState->patrol->radius - 4.0f) < 0.001f &&
		!patrolState->transitions.empty() &&
		patrolState->transitions.front().condition.key == "HasVisualTarget" &&
		chaseState->task == Vans::VansAITaskKind::MoveToTarget &&
		!chaseState->transitions.empty() &&
		chaseState->transitions.front().condition.key == "HasVisualTarget" &&
		!chaseState->transitions.front().condition.expectedBool;
	Vans::VansNavigationMesh demoHallNavigation;
	if (!demoHallNavigation.Load(
		projectRoot / "Assets/Navigation/DemoHall.vnavmesh", error))
		return ExpectGAF(false, error.c_str());
	bool allPlayerSpawnsReachable = playerPositions.size() == 2u;
	for (const glm::vec3& playerPosition : playerPositions)
		allPlayerSpawnsReachable = allPlayerSpawnsReachable &&
			demoHallNavigation.FindPath(whisperPosition, playerPosition).status ==
			Vans::VansNavigationPathStatus::Complete;
	std::string script;
	if (!Vans::VansFileStorage::ReadAllBytes(
		projectRoot / "Scripts/forest_lua_behaviors.lua", script, error))
		return ExpectGAF(false, error.c_str());

	const fs::path itemGafRoot = projectRoot / "Assets/GAF/ItemInteraction";
	Vans::VansGAFProjectConfiguration gafConfiguration;
	if (!Vans::VansGAFProjectConfiguration::LoadForProject(
		projectRoot, workspace / "ForestEngine/ForestEngine", gafConfiguration, error))
		return ExpectGAF(false, error.c_str());
	nlohmann::ordered_json pickupAction;
	nlohmann::ordered_json pickupActionSet;
	nlohmann::ordered_json pickupTimeline;
	nlohmann::ordered_json wakeTimeline;
	nlohmann::ordered_json demoHallTags;
	if (!Vans::VansJsonFileStorage::Read(
		itemGafRoot / "FrameStand4Pickup.vaction", pickupAction, error) ||
		!Vans::VansJsonFileStorage::Read(
			itemGafRoot / "FrameStand4Pickup.vactionset", pickupActionSet, error) ||
		!Vans::VansJsonFileStorage::Read(
			projectRoot / "Assets/Cinematics/FrameStand4PickupWake.vtimeline",
			pickupTimeline, error) ||
		!Vans::VansJsonFileStorage::Read(
			projectRoot / "Assets/Cinematics/FrameStand4WhisperWake.vtimeline",
			wakeTimeline, error) ||
		!Vans::VansJsonFileStorage::Read(
			projectRoot / "Assets/GAF/WindowBreak/DemoHallTags.vtagtree",
			demoHallTags, error))
		return ExpectGAF(false, error.c_str());
	const auto gafSourceCompiles = [&error, &gafConfiguration](
		const fs::path& path, Vans::VansAssetType type)
	{
		Vans::VansSerializedValue source;
		if (!Vans::VansGameplayAssetStorage::LoadSource(path, source, error)) return false;
		const Vans::VansGameplayCookResult cooked =
			Vans::VansGameplayAssetStorage::Cook(type, source,
				Vans::VansGameplayAssetSchemaRegistry::BuiltIns(), &gafConfiguration);
		if (!cooked) return false;
		return static_cast<bool>(Vans::VansGameplayAssetCompiler::Compile(cooked.asset));
	};
	const bool pickupGafAssetsCompile =
		gafSourceCompiles(itemGafRoot / "FrameStand4Pickup.vaction",
			Vans::VansAssetType::ActionDefinition) &&
		gafSourceCompiles(itemGafRoot / "FrameStand4Pickup.vactionset",
			Vans::VansAssetType::ActionSet) &&
		gafSourceCompiles(itemGafRoot / "PickupInspect.vactiongraph",
			Vans::VansAssetType::ActionGraph) &&
		gafSourceCompiles(projectRoot / "Assets/GAF/WindowBreak/DemoHallTags.vtagtree",
			Vans::VansAssetType::GameplayTagTree);
	Vans::VansTimelineAsset pickupTimelineAsset;
	if (!Vans::VansTimelineSerialization::Load(
		projectRoot / "Assets/Cinematics/FrameStand4PickupWake.vtimeline",
		pickupTimelineAsset, error))
		return ExpectGAF(false, error.c_str());
	Vans::VansTimelineCompileOptions pickupTimelineCompileOptions;
	pickupTimelineCompileOptions.extensions =
		&Vans::VansTimelineTrackExtensionRegistry::BuiltIns();
	const Vans::VansTimelineCompileResult compiledPickupTimeline =
		Vans::VansTimelineCompiler::Compile(
			pickupTimelineAsset, pickupTimelineCompileOptions);
	Vans::VansTimelineAsset wakeTimelineAsset;
	if (!Vans::VansTimelineSerialization::Load(
		projectRoot / "Assets/Cinematics/FrameStand4WhisperWake.vtimeline",
		wakeTimelineAsset, error))
		return ExpectGAF(false, error.c_str());
	const Vans::VansTimelineCompileResult compiledWakeTimeline =
		Vans::VansTimelineCompiler::Compile(
			wakeTimelineAsset, pickupTimelineCompileOptions);
	const bool pickupTimelineCompiles = static_cast<bool>(compiledPickupTimeline) &&
		static_cast<bool>(compiledWakeTimeline);

	const fs::path pickupRuntimeRoot =
		fs::temp_directory_path() / "ForestGAFDemoHallFrameStand4PickupContract";
	std::error_code pickupFilesystemError;
	fs::remove_all(pickupRuntimeRoot, pickupFilesystemError);
	struct GafPickupCleanup
	{
		fs::path path;
		~GafPickupCleanup()
		{
			std::error_code ignored;
			fs::remove_all(path, ignored);
		}
	} pickupCleanup{ pickupRuntimeRoot };
	const fs::path pickupRuntimeAssets = pickupRuntimeRoot / "Assets";
	fs::create_directories(pickupRuntimeAssets, pickupFilesystemError);
	const std::vector<std::pair<fs::path, fs::path>> pickupRuntimeFiles = {
		{ itemGafRoot / "FrameStand4Pickup.vaction", pickupRuntimeAssets / "FrameStand4Pickup.vaction" },
		{ itemGafRoot / "FrameStand4Pickup.vaction.meta", pickupRuntimeAssets / "FrameStand4Pickup.vaction.meta" },
		{ itemGafRoot / "FrameStand4Pickup.vactionset", pickupRuntimeAssets / "FrameStand4Pickup.vactionset" },
		{ itemGafRoot / "FrameStand4Pickup.vactionset.meta", pickupRuntimeAssets / "FrameStand4Pickup.vactionset.meta" },
		{ itemGafRoot / "PickupInspect.vactiongraph", pickupRuntimeAssets / "PickupInspect.vactiongraph" },
		{ itemGafRoot / "PickupInspect.vactiongraph.meta", pickupRuntimeAssets / "PickupInspect.vactiongraph.meta" },
		{ itemGafRoot / "ItemOwner.vtargeting", pickupRuntimeAssets / "ItemOwner.vtargeting" },
		{ itemGafRoot / "ItemOwner.vtargeting.meta", pickupRuntimeAssets / "ItemOwner.vtargeting.meta" },
		{ projectRoot / "Assets/GAF/WindowBreak/DemoHallTags.vtagtree", pickupRuntimeAssets / "DemoHallTags.vtagtree" },
		{ projectRoot / "Assets/GAF/WindowBreak/DemoHallTags.vtagtree.meta", pickupRuntimeAssets / "DemoHallTags.vtagtree.meta" },
		{ projectRoot / "Assets/Cinematics/FrameStand4PickupWake.vtimeline", pickupRuntimeAssets / "FrameStand4PickupWake.vtimeline" },
		{ projectRoot / "Assets/Cinematics/FrameStand4PickupWake.vtimeline.meta", pickupRuntimeAssets / "FrameStand4PickupWake.vtimeline.meta" },
	};
	for (const auto& [source, destination] : pickupRuntimeFiles)
	{
		fs::copy_file(source, destination, fs::copy_options::overwrite_existing,
			pickupFilesystemError);
		if (!ExpectGAF(!pickupFilesystemError,
			"Frame_Stand_4 GAF runtime fixture could not be copied")) return false;
	}
	Vans::VansAssetDatabase pickupDatabase(
		pickupRuntimeAssets, pickupRuntimeRoot / "Library/Artifacts");
	const Vans::VansAssetScanResult pickupScan =
		pickupDatabase.Scan(Vans::VansAssetOperationPolicy::Authoring());
	if (!ExpectGAF(pickupScan && pickupDatabase.All().size() == 6,
		"Frame_Stand_4 GAF assets did not scan as a six-asset runtime closure")) return false;
	Vans::VansGameplayRuntime pickupRuntime;
	Vans::VansGameplayRuntimeDependencies pickupRuntimeDependencies;
	pickupRuntimeDependencies.contributors.push_back(
		Vans::VansMakeGameplayPrimitivesGAFContributor());
	pickupRuntimeDependencies.contributors.push_back(
		MakeProjectSchemaContributor(gafConfiguration));
	pickupRuntimeDependencies.contributors.push_back(MakeTestTimelineContributor());
	pickupRuntimeDependencies.contributors.push_back(MakeTestRuntimeContributor(
		"Test.PickupRuntime", {}));
	Vans::VansAssetObjectRepository pickupAssetObjects;
	if (!BootstrapGameplayMemory(pickupDatabase.All(), pickupAssetObjects, error))
		return ExpectGAF(false, error.c_str());
	if (!pickupRuntime.Initialize(
		pickupDatabase.All(), pickupAssetObjects, Vans::VansGAFSettings{},
		pickupRuntimeDependencies, error))
		return ExpectGAF(false, error.c_str());
	const auto runtimePickupAction = pickupRuntime.Assets().ResolveAction(
		"Gameplay.DemoHall.Item.FrameStand4PickupWake");
	const auto* runtimePickupSet = pickupRuntime.Assets().ResolveActionSet(
		"e30100ce-50a7-4a44-95fe-d91fc3ad678d");
	const auto* runtimePickupGraph = runtimePickupAction ? FindCompiledActionRecord(
		runtimePickupAction->program.execute.drivers, "Core.Driver.Graph") : nullptr;
	const auto* runtimePickupTimeline = runtimePickupAction ? FindCompiledActionRecord(
		runtimePickupAction->program.execute.drivers, "Timeline.Driver.Session") : nullptr;
	Vans::VansGameplayActionHostSetup pickupHostSetup;
	pickupHostSetup.actionSets.push_back("e30100ce-50a7-4a44-95fe-d91fc3ad678d");
	AddHostTagInitializer(pickupHostSetup, "Target.Interactable.Item");
	const auto pickupHost = pickupRuntime.CreateHost({ 902, 1 }, pickupHostSetup, error);
	const bool pickupRuntimeLinksResolve = runtimePickupAction && runtimePickupSet &&
		runtimePickupAction->executionGraph &&
		CompiledActionReference(runtimePickupGraph, "graph") ==
			"ad301673-a461-4a6b-8137-c3e95c44c474" && !runtimePickupTimeline &&
		runtimePickupSet->grants.size() == 1 && pickupHost &&
		pickupHost->GrantedActions().size() == 1;
	bool pickupWaitsForExternalCompletion = false;
	if (runtimePickupAction && pickupHost)
	{
		Vans::VansActionContext pickupContext;
		pickupContext.SetEntity(Vans::VansActionContextSlots::Owner, { 902, 1 });
		pickupContext.SetEntity(Vans::VansActionContextSlots::Instigator, { 903, 1 });
		pickupContext.SetEntity(Vans::VansActionContextSlots::Source, { 903, 1 });
		pickupContext.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, { 902, 1 });
		const Vans::VansActionResult activated = pickupHost->ActivateAction(
			runtimePickupAction->id, pickupContext);
		pickupRuntime.TickEarly(5.0);
		const auto held = activated ? pickupHost->Query(activated.action) : std::nullopt;
		const bool heldUntilExplicitExit = held &&
			held->state == Vans::VansActionInstanceState::Waiting;
		Vans::VansActionEvent completion;
		completion.stableName = "Interaction.Pickup.Finished";
		completion.type = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>(
			completion.stableName);
		if (heldUntilExplicitExit &&
			pickupHost->EnqueueEvent(activated.action, std::move(completion), error))
		{
			pickupRuntime.TickEarly(0.0);
			const auto ended = pickupHost->Query(activated.action);
			pickupWaitsForExternalCompletion = ended &&
				ended->state == Vans::VansActionInstanceState::Ended &&
				ended->endReason == Vans::VansActionEndReason::Completed;
		}
		std::cout << "[GAF] Pickup preview lifecycle: heldAfter5s="
			<< heldUntilExplicitExit << " explicitCompletion="
			<< pickupWaitsForExternalCompletion << " duplicateActionTimeline="
			<< (runtimePickupTimeline != nullptr) << '\n';
	}

	bool hasPickupScriptBridge = false;
	for (const auto& extension : pickupAction.value(
		"extensions", nlohmann::ordered_json::array()))
	{
		const auto inputs = extension.value(
			"inputs", nlohmann::ordered_json::object());
		hasPickupScriptBridge = hasPickupScriptBridge ||
			extension.value("type", std::string{}) == "Script.Action" &&
			inputs.value("entry", std::string{}) == "InteractablePickup" &&
			inputs.value("callback", std::string{}) == "play_pickup_presentation" &&
			inputs.value("timelineComponentGuid", std::string{}) ==
				"296cfa68-af40-4509-b181-a57925cd6337";
	}
	bool hasGraphDriver = false;
	bool hasTimelineDriver = false;
	const auto phases = pickupAction.value("phases", nlohmann::ordered_json::object());
	const auto execute = phases.value("execute", nlohmann::ordered_json::object());
	for (const auto& driver : execute.value("drivers", nlohmann::ordered_json::array()))
	{
		const auto inputs = driver.value("inputs", nlohmann::ordered_json::object());
		hasGraphDriver = hasGraphDriver ||
			driver.value("type", std::string{}) == "Core.Driver.Graph" &&
			inputs.value("graph", nlohmann::ordered_json::object())
				.value("assetGuid", std::string{}) ==
				"ad301673-a461-4a6b-8137-c3e95c44c474";
		hasTimelineDriver = hasTimelineDriver ||
			driver.value("type", std::string{}) == "Timeline.Driver.Session" &&
			inputs.value("timeline", nlohmann::ordered_json::object())
				.value("assetGuid", std::string{}) ==
				"45453c77-62f3-4c8a-a949-3de554409767";
	}
	const bool pickupActionUsesGAF =
		pickupAction.value("actionId", std::string{}) ==
			"Gameplay.DemoHall.Item.FrameStand4PickupWake" &&
		hasGraphDriver && !hasTimelineDriver && hasPickupScriptBridge;
	bool pickupSetGrantsAction = false;
	for (const auto& grant : pickupActionSet.value(
		"grants", nlohmann::ordered_json::array()))
	{
		bool hasInputBinding = false;
		for (const auto& extension : grant.value(
			"extensions", nlohmann::ordered_json::array()))
		{
			const auto inputs = extension.value("inputs", nlohmann::ordered_json::object());
			hasInputBinding = hasInputBinding ||
				extension.value("type", std::string{}) == "Gameplay.Input.Binding" &&
				inputs.value("binding", std::string{}) == "Input.Interact";
		}
		pickupSetGrantsAction = pickupSetGrantsAction ||
			grant.value("action", nlohmann::ordered_json::object())
				.value("assetGuid", std::string{}) ==
				"cba9cbd4-3a09-484b-aa11-ad3aa825eccb" && hasInputBinding;
	}

	bool timelineBindsFrameStand = false;
	bool timelineBindsWhisperAnimation = false;
	for (const auto& binding : pickupTimeline.value(
		"bindings", nlohmann::ordered_json::array()))
	{
		const std::string bindingId = binding.value("id", std::string{});
		timelineBindsFrameStand = timelineBindsFrameStand ||
			bindingId == "binding-pickup-item" &&
			binding.value("targetGuid", std::string{}) ==
				"a50146bf-af31-5bb8-bcb1-a1a331fc1932";
	}
	for (const auto& binding : wakeTimeline.value(
		"bindings", nlohmann::ordered_json::array()))
	{
		const std::string bindingId = binding.value("id", std::string{});
		timelineBindsWhisperAnimation = timelineBindsWhisperAnimation ||
			bindingId == "binding-whisper-animation" &&
			binding.value("targetGuid", std::string{}) ==
				"d21cfc31-dba5-44cb-a271-f0f2c20cded1" &&
			binding.value("componentGuid", std::string{}) ==
				"a524b6e3-7c08-4ac5-a116-77d44b6099c9" &&
			binding.value("required", false);
	}
	bool timelineFiresWakeTrigger = false;
	int timelineWakeTick = -1;
	int timelineCompletionTick = -1;
	bool timelineCompletesPickupAction = false;
	bool pickupPreviewDoesNotWake = true;
	for (const auto& track : wakeTimeline.value(
		"tracks", nlohmann::ordered_json::array()))
	{
		const std::string type = track.value("type", std::string{});
		const auto extensionData = track.value(
			"extensionData", nlohmann::ordered_json::object());
		if (type == "Timeline.AnimatorParameter")
		{
			bool hasPreviewCompletionKey = false;
			for (const auto& section : track.value(
				"sections", nlohmann::ordered_json::array()))
				for (const auto& channel : section.value(
					"channels", nlohmann::ordered_json::array()))
					for (const auto& key : channel.value(
						"keys", nlohmann::ordered_json::array()))
					{
						const int tick = key.value("tick", -1);
						const bool isWakeKey = tick == 1 && key.value("value", false);
						hasPreviewCompletionKey = hasPreviewCompletionKey || isWakeKey;
						if (isWakeKey) timelineWakeTick = tick;
					}
			timelineFiresWakeTrigger =
				track.value("bindingId", std::string{}) ==
					"binding-whisper-animation" &&
				extensionData.value("parameterName", std::string{}) == "WakeUp" &&
				extensionData.value("parameterType", std::string{}) == "Trigger" &&
				extensionData.value("firePolicy", std::string{}) == "Forward" &&
				extensionData.value("seekPolicy", std::string{}) == "Never" &&
				hasPreviewCompletionKey;
		}
	}
	for (const auto& track : pickupTimeline.value(
		"tracks", nlohmann::ordered_json::array()))
	{
		const std::string type = track.value("type", std::string{});
		pickupPreviewDoesNotWake = pickupPreviewDoesNotWake &&
			type != "Timeline.AnimatorParameter";
		if (type == "Action.Event")
		{
			const auto extensionData = track.value(
				"extensionData", nlohmann::ordered_json::object());
			for (const auto& section : track.value(
				"sections", nlohmann::ordered_json::array()))
				timelineCompletionTick = section.value("startTick", -1);
			timelineCompletesPickupAction =
				extensionData.value("action", std::string{}) ==
					"Gameplay.DemoHall.Item.FrameStand4PickupWake" &&
				extensionData.value("event", std::string{}) ==
					"Interaction.Pickup.Finished";
		}
	}

	bool frameStandPickupConfigured = false;
	bool frameStandTimelineConfigured = false;
	bool frameStandWakeTimelineConfigured = false;
	for (const auto& entity : scene.value("entities", nlohmann::ordered_json::array()))
	{
		const std::string entityName = entity.value("name", std::string{});
		if (entityName == "Frame_Stand_4")
		{
			bool hasTrigger = false;
			bool hasActionHost = false;
			bool hasPickupScript = false;
			for (const auto& component : entity.value(
				"components", nlohmann::ordered_json::array()))
			{
				const std::string type = component.value("type", std::string{});
				const auto data = component.value(
					"data", nlohmann::ordered_json::object());
				if (type == "Physics")
					hasTrigger = data.value("bodyType", std::string{}) == "static" &&
						data.value("layer", std::string{}) == "Trigger" &&
						data.value("isTrigger", false);
				else if (type == "ActionHost")
					for (const auto& actionSet : data.value(
						"actionSets", nlohmann::ordered_json::array()))
						hasActionHost = hasActionHost ||
							actionSet.value("guid", std::string{}) ==
							"e30100ce-50a7-4a44-95fe-d91fc3ad678d";
				else if (type == "Script" && data.value(
					"entry", std::string{}) == "InteractablePickup")
				{
					const auto fields = data.value(
						"fields", nlohmann::ordered_json::object());
					hasPickupScript =
						fields.value("itemId", std::string{}) ==
							"whisper_frame_stand_4" &&
						fields.value("aiTarget", nlohmann::ordered_json::object())
							.value("entityGuid", std::string{}) ==
							"d21cfc31-dba5-44cb-a271-f0f2c20cded1" &&
						fields.value("actionHostOwnerGuid", std::string{}) ==
							"a50146bf-af31-5bb8-bcb1-a1a331fc1932" &&
						fields.value("pickupActionId", std::string{}) ==
							"Gameplay.DemoHall.Item.FrameStand4PickupWake" &&
						fields.value("inspectTimelineComponentGuid", std::string{}) ==
							"296cfa68-af40-4509-b181-a57925cd6337" &&
						fields.value("wakeTimelineComponentGuid", std::string{}) ==
							"7796d4bf-538e-45b0-a6bb-0e85f8182397" &&
						std::abs(fields.value("inspectHoldSeconds", 0.0f) - 0.45f) < 0.001f;
				}
			}
			frameStandPickupConfigured = hasTrigger && hasActionHost && hasPickupScript;
		}
		else if (entityName == "FrameStand4WhisperWakeTimeline")
		{
			for (const auto& component : entity.value(
				"components", nlohmann::ordered_json::array()))
			{
				if (component.value("id", std::string{}) !=
					"7796d4bf-538e-45b0-a6bb-0e85f8182397" ||
					component.value("type", std::string{}) != "Timeline") continue;
				frameStandWakeTimelineConfigured = component.value(
					"data", nlohmann::ordered_json::object())
					.value("timeline", nlohmann::ordered_json::object())
					.value("guid", std::string{}) ==
					"ec7e1ea6-d801-43c5-b9b6-05f6f73df4ca";
			}
		}
		else if (entityName == "FrameStand4PickupTimeline")
		{
			for (const auto& component : entity.value(
				"components", nlohmann::ordered_json::array()))
			{
				if (component.value("id", std::string{}) !=
					"296cfa68-af40-4509-b181-a57925cd6337" ||
					component.value("type", std::string{}) != "Timeline") continue;
				frameStandTimelineConfigured = component.value(
					"data", nlohmann::ordered_json::object())
					.value("timeline", nlohmann::ordered_json::object())
					.value("guid", std::string{}) ==
					"45453c77-62f3-4c8a-a949-3de554409767";
			}
		}
	}
	bool registeredWhisperActionTag = false;
	bool registeredWhisperTargetTag = false;
	for (const auto& tag : demoHallTags.value("tags", nlohmann::ordered_json::array()))
	{
		registeredWhisperActionTag = registeredWhisperActionTag ||
			tag.value("name", std::string{}) == "Action.DemoHall.Whisper.Wake";
		registeredWhisperTargetTag = registeredWhisperTargetTag ||
			tag.value("name", std::string{}) == "Target.DemoHall.WhisperWake";
	}
	const std::size_t pickupScriptBegin = script.find("M.InteractablePickup");
	const std::size_t pickupScriptEnd = script.find("M.GlassBreakInteractable", pickupScriptBegin);
	const std::string pickupScript = pickupScriptBegin != std::string::npos
		? script.substr(pickupScriptBegin, pickupScriptEnd - pickupScriptBegin) : std::string{};
	const std::size_t pickupCommitBegin = pickupScript.find(
		"function M.InteractablePickup:commit_inventory()");
	const std::size_t pickupRotateBegin = pickupScript.find(
		"function M.InteractablePickup:rotate_preview()", pickupCommitBegin);
	const std::string pickupCommitScript =
		pickupCommitBegin != std::string::npos && pickupRotateBegin != std::string::npos
		? pickupScript.substr(pickupCommitBegin, pickupRotateBegin - pickupCommitBegin)
		: std::string{};
	const std::size_t pickupFinishBegin = pickupScript.find(
		"function M.InteractablePickup:finish_pickup_interaction()");
	const std::size_t pickupDisableBegin = pickupScript.find(
		"function M.InteractablePickup:on_disable()", pickupFinishBegin);
	const std::string pickupFinishScript =
		pickupFinishBegin != std::string::npos && pickupDisableBegin != std::string::npos
		? pickupScript.substr(pickupFinishBegin, pickupDisableBegin - pickupFinishBegin)
		: std::string{};
	const std::size_t pickupGameplayRestore = pickupFinishScript.find(
		"runtime.set_mode(runtime.Mode.Gameplay)");
	const std::size_t pickupCommittedGate = pickupFinishScript.find(
		"if self.committed then");
	const std::size_t pickupWakePlay = pickupFinishScript.find(
		"wakeTimeline.play(wakeTimelineGuid, true)");
	const std::size_t pickupActivation = pickupFinishScript.find(
		"vans.ai.request_activation(self.aiTarget)");
	const std::size_t pickupGameplayRelease = pickupFinishScript.find(
		"vans.ai.release_to_gameplay(self.aiTarget)");
	std::string inspectionView;
	if (!Vans::VansFileStorage::ReadAllBytes(
		projectRoot / "Assets/UI/Views/ItemInspection.xaml", inspectionView, error))
		return ExpectGAF(false, error.c_str());
	std::string inspectionScreen;
	if (!Vans::VansFileStorage::ReadAllBytes(
		projectRoot / "Assets/UI/Screens/ItemInspection.vui.json", inspectionScreen, error))
		return ExpectGAF(false, error.c_str());
	const bool pickupInspectionIsMousePreviewWithEscapeExit =
		pickupScript.find("keys.is_mouse_button_down(\"LEFT\")") != std::string::npos &&
		pickupScript.find("keys.get_mouse_delta()") != std::string::npos &&
		script.find("keys.is_key_pressed(\"ESCAPE\")") != std::string::npos &&
		script.find("ui.inspect.exit") == std::string::npos &&
		inspectionView.find("BtnExitInspection") == std::string::npos &&
		inspectionView.find("[Esc]") != std::string::npos &&
		inspectionScreen.find("\"events\": []") != std::string::npos;
	const bool pickupUsesExistingGafInventoryFlow =
		pickupScript.find("vans.action.try_activate") != std::string::npos &&
		pickupScript.find("inventory.add") != std::string::npos &&
		pickupScript.find("timeline.pause") != std::string::npos &&
		pickupScript.find("self:commit_inventory()") != std::string::npos &&
		pickupScript.find("timeline.resume(timelineGuid)") != std::string::npos &&
		pickupScript.find("session.timelineCommitPulse") == std::string::npos &&
		pickupCommitScript.find("vans.ai.request_activation") == std::string::npos &&
		pickupFinishScript.find("local completedOutro") != std::string::npos &&
		pickupFinishScript.find("wakeStarted = completedOutro") != std::string::npos &&
		pickupGameplayRestore != std::string::npos &&
		pickupCommittedGate != std::string::npos &&
		pickupWakePlay != std::string::npos &&
		pickupActivation != std::string::npos &&
		pickupGameplayRelease != std::string::npos &&
		pickupCommittedGate > pickupGameplayRestore &&
		pickupWakePlay > pickupCommittedGate &&
		pickupActivation > pickupWakePlay &&
		pickupGameplayRelease > pickupActivation &&
		pickupScript.find("set_trigger(\"WakeUp\")") == std::string::npos;

	const fs::path animationV2Root = workspace / "AnimationV2Project";
	nlohmann::ordered_json animationV2Scene;
	if (!Vans::VansJsonFileStorage::Read(
		animationV2Root / "Scenes/WhisperPreview.json", animationV2Scene, error))
		return ExpectGAF(false, error.c_str());
	bool foundAnimationV2Preview = false;
	bool foundAnimationV2Camera = false;
	bool animationV2PreviewWakeConfigured = false;
	for (const auto& entity : animationV2Scene.value("entities", nlohmann::ordered_json::array()))
	{
		const std::string entityName = entity.value("name", std::string{});
		bool hasModel = false;
		bool hasAnimation = false;
		bool hasPreviewScript = false;
		bool hasCamera = false;
		bool hasCameraScript = false;
		bool hasCharacterController = false;
		for (const auto& component : entity.value("components", nlohmann::ordered_json::array()))
		{
			const std::string type = component.value("type", std::string{});
			const auto data = component.value("data", nlohmann::ordered_json::object());
			hasCharacterController = hasCharacterController || type == "CharacterController";
			hasCamera = hasCamera || type == "Camera";
			if (type == "ModelRenderer")
				hasModel = data.value("model", nlohmann::ordered_json::object())
					.value("guid", std::string{}) == "d08d594d-f68c-4497-8663-c85a4695f2d6";
			else if (type == "Animation")
				hasAnimation = data.value("mesh_group", std::string{}) == "Whisper" &&
					!data.value("root_motion", true) && data.value("auto_play", false) &&
					data.value("animator", nlohmann::ordered_json::object())
						.value("guid", std::string{}) == "ec68421a-fff2-4db4-82c4-00cbb052c84c" &&
					data.value("rig", nlohmann::ordered_json::object())
						.value("guid", std::string{}) == "873c655f-18a8-4aea-8632-ff095e6cc7cc";
			else if (type == "Script")
			{
				const std::string entry = data.value("entry", std::string{});
				hasPreviewScript = hasPreviewScript || entry == "WhisperAnimationPreview";
				hasCameraScript = hasCameraScript || entry == "WhisperPreviewCamera";
				if (entry == "WhisperAnimationPreview")
				{
					const auto fields = data.value("fields", nlohmann::ordered_json::object());
					animationV2PreviewWakeConfigured = fields.value("cycleAnimations", false) &&
						fields.value("autoWake", false) &&
						std::abs(fields.value("wakeDelay", 0.0f) - 2.0f) < 0.001f;
				}
			}
		}
		if (entityName == "Whisper_Preview")
			foundAnimationV2Preview = hasModel && hasAnimation && hasPreviewScript &&
				!hasCharacterController;
		else if (entityName == "WhisperPreviewCamera")
			foundAnimationV2Camera = hasCamera && hasCameraScript;
	}
	const auto animationV2GI = animationV2Scene.value("settings", nlohmann::ordered_json::object())
		.value("globalIllumination", nlohmann::ordered_json::object());
	const auto animationV2GIRegions = animationV2GI.value(
		"regions", nlohmann::ordered_json::array());
	const bool animationV2GIDisabled = animationV2GIRegions.size() == 1 &&
		!animationV2GIRegions[0].value("enabled", true);
	std::string animationV2Script;
	if (!Vans::VansFileStorage::ReadAllBytes(
		animationV2Root / "Scripts/forest_lua_behaviors.lua", animationV2Script, error))
		return ExpectGAF(false, error.c_str());
	const fs::path animationV2WhisperRoot = animationV2Root / "Assets/Characters/Whisper";

	VansGraphics::Skeleton animationV2Skeleton;
	if (!LoadContractSkeletonFromModel(
		animationV2WhisperRoot / "Models/SK_Whisper.glb", animationV2Skeleton, error))
		return ExpectGAF(false, error.c_str());
	VansGraphics::VansAnimationClip deadClip;
	VansGraphics::VansAnimationClip sitToIdleClip;
	VansGraphics::Skeleton deadSkeleton;
	VansGraphics::Skeleton sitToIdleSkeleton;
	if (!VansGraphics::VansAnimationClipIO::Load(
		(animationV2WhisperRoot / "Animations/Anim_WhisperDead_Unreal_Take.vclip").string(),
		deadClip, deadSkeleton) ||
		!VansGraphics::VansAnimationClipIO::Load(
			(animationV2WhisperRoot / "Animations/anima_whisper_sittoidle_Unreal_Take.vclip").string(),
			sitToIdleClip, sitToIdleSkeleton))
		return ExpectGAF(false, "AnimationV2 Whisper startup clips could not be loaded");
	if (!ExpectGAF(std::abs(deadClip.duration - 0.1f) < 0.002f &&
		std::abs(sitToIdleClip.duration - 3.0f) < 0.002f &&
		deadSkeleton.bones.size() == animationV2Skeleton.bones.size() &&
		sitToIdleSkeleton.bones.size() == animationV2Skeleton.bones.size(),
		"AnimationV2 Whisper startup clips lost their duration or 68-bone binding")) return false;
	for (std::size_t bone = 0; bone < animationV2Skeleton.bones.size(); ++bone)
		if (!ExpectGAF(deadSkeleton.bones[bone].name == animationV2Skeleton.bones[bone].name &&
			sitToIdleSkeleton.bones[bone].name == animationV2Skeleton.bones[bone].name,
			"AnimationV2 Whisper startup clip bone order does not match the model")) return false;

	VansGraphics::AnimatorAssetData animationV2AnimatorAsset;
	if (!VansGraphics::VansAnimatorIO::Load(
		(animationV2WhisperRoot / "Animation/Whisper.vanimator").string(),
		animationV2AnimatorAsset))
		return ExpectGAF(false, "AnimationV2 Whisper startup Animator could not be loaded");
	VansGraphics::VansAnimatorRuntimeCompileOptions animationV2CompileOptions;
	animationV2CompileOptions.enableTargetPostProcess = false;
	animationV2CompileOptions.enableRootMotion = false;
	animationV2CompileOptions.rigResolver = [&animationV2WhisperRoot](
		const std::string&, std::string& resolveError)
	{
		return LoadRigForContract(
			animationV2WhisperRoot / "Animation/Whisper.vanimrig", resolveError);
	};
	auto animationV2Controller = VansGraphics::VansAnimatorRuntimeCompiler::Compile(
		animationV2AnimatorAsset, animationV2Skeleton,
		[&animationV2Root](const VansGraphics::AnimatorClipRef& ref,
			std::shared_ptr<const VansGraphics::VansAnimationClipAsset>& clip,
			std::string& resolveError)
		{
			return LoadAnimationClipAssetForContract(
				animationV2Root / ref.pathHint, clip, resolveError);
		},
		[](const VansGraphics::VansAnimationLayerDefinition&,
			std::shared_ptr<const VansGraphics::VansBoneMaskAsset>&,
			std::string& resolveError)
		{
			resolveError = "AnimationV2 Whisper base layer unexpectedly requested a Bone Mask";
			return false;
		},
		animationV2CompileOptions, error);
	if (!ExpectGAF(animationV2Controller != nullptr, error.c_str())) return false;
	const auto& animationV2Parameters = animationV2Controller->GetParameters();
	const auto wakeParameter = animationV2Parameters.find("WakeUp");
	const bool hasExposedWakeTrigger = wakeParameter != animationV2Parameters.end() &&
		wakeParameter->second.type == VansGraphics::AnimatorParamType::Trigger;
	animationV2Controller->Play();
	animationV2Controller->Update(0.0f, animationV2Skeleton);
	const bool beganDead = animationV2Controller->GetCurrentStateName() == "Dead";
	animationV2Controller->Update(deadClip.duration * 25.0f + 0.017f, animationV2Skeleton);
	const bool loopedDeadUntilWake = animationV2Controller->GetCurrentStateName() == "Dead";
	const bool wakeInitiallyClear = !animationV2Controller->IsTriggerSet("WakeUp");
	animationV2Controller->SetTrigger("WakeUp");
	const bool wakeSignalRaised = animationV2Controller->IsTriggerSet("WakeUp");
	animationV2Controller->Update(0.0f, animationV2Skeleton);
	const bool enteredSitToIdle = animationV2Controller->GetCurrentStateName() == "SitToIdle";
	const bool wakeSignalConsumed = !animationV2Controller->IsTriggerSet("WakeUp");
	animationV2Controller->Update(sitToIdleClip.duration + 0.001f, animationV2Skeleton);
	const bool enteredIdle1 = animationV2Controller->GetCurrentStateName() == "Idle1";
	animationV2Controller->SetInt("PreviewState", 1);
	animationV2Controller->Update(0.13f, animationV2Skeleton);
	const bool switchedToIdle2 = animationV2Controller->GetCurrentStateName() == "Idle2";
	animationV2Controller->SetInt("PreviewState", 15);
	animationV2Controller->Update(0.13f, animationV2Skeleton);
	const bool switchedToOriginalDeath = animationV2Controller->GetCurrentStateName() == "Death";
	animationV2Controller->SetInt("PreviewState", 0);
	animationV2Controller->Update(0.13f, animationV2Skeleton);
	const bool returnedToIdle1 = animationV2Controller->GetCurrentStateName() == "Idle1";
	if (!deformationPreview.RasterizeFrame(
		animationV2Controller->GetBoneMatricesSSBO(), {}, glm::vec3(0.0f), {}, error))
		return ExpectGAF(false, error.c_str());
	const auto& startupStats = deformationPreview.GetStats();
	if (!ExpectGAF(hasExposedWakeTrigger && beganDead && loopedDeadUntilWake &&
		wakeInitiallyClear && wakeSignalRaised && enteredSitToIdle && wakeSignalConsumed &&
		enteredIdle1 && switchedToIdle2 && switchedToOriginalDeath && returnedToIdle1 &&
		startupStats.invalidDeformedVertexCount == 0 &&
		startupStats.deformedRadiusRatio > 0.8f &&
		startupStats.deformedRadiusRatio < 1.25f,
		"AnimationV2 Whisper wake gate or post-wake 16-clip preview routing is invalid")) return false;

	return ExpectGAF(foundWhisper && behaviorIsPickupGatedChase && allPlayerSpawnsReachable &&
		pickupGafAssetsCompile && pickupTimelineCompiles && pickupRuntimeLinksResolve &&
		pickupWaitsForExternalCompletion && pickupActionUsesGAF && pickupSetGrantsAction &&
		pickupInspectionIsMousePreviewWithEscapeExit &&
		timelineBindsFrameStand && timelineBindsWhisperAnimation &&
		timelineFiresWakeTrigger && timelineCompletesPickupAction && pickupPreviewDoesNotWake &&
		timelineCompletionTick == 77999 && timelineWakeTick == 1 &&
		frameStandPickupConfigured && frameStandTimelineConfigured &&
		frameStandWakeTimelineConfigured &&
		registeredWhisperActionTag && registeredWhisperTargetTag &&
		pickupUsesExistingGafInventoryFlow &&
		fs::is_regular_file(whisperRoot /
			"Animations/Anim_WhisperDead_Unreal_Take.vclip") &&
		fs::is_regular_file(whisperRoot /
			"Animations/anima_whisper_sittoidle_Unreal_Take.vclip") &&
		foundAnimationV2Preview && foundAnimationV2Camera && animationV2GIDisabled &&
		animationV2PreviewWakeConfigured &&
		animationV2Script.find("M.WhisperAnimationPreview") != std::string::npos &&
		animationV2Script.find("set_trigger(\"WakeUp\")") != std::string::npos &&
		animationV2Script.find("WHISPER_WAKE_TRANSITION_DURATION") != std::string::npos &&
		animationV2Script.find("M.WhisperPreviewCamera") != std::string::npos &&
		fs::is_regular_file(animationV2WhisperRoot / "Models/SK_Whisper.glb") &&
		fs::is_regular_file(animationV2WhisperRoot / "Animation/Whisper.vanimator") &&
		fs::is_regular_file(animationV2WhisperRoot / "Animation/Whisper.vanimrig"),
		"Frame_Stand_4 pickup is not wired through Whisper wake, AI chase, navigation, CCT, and locomotion");
}

bool TestDemoHallPlayerThrowContract()
{
	using namespace VansGraphics;
	namespace fs = std::filesystem;
	fs::path workspace = fs::current_path();
	for (int i = 0; i < 6 && !fs::exists(workspace / "DemoHallProject"); ++i)
		workspace = workspace.parent_path();
	const fs::path project = workspace / "DemoHallProject";
	std::string error;
	Vans::VansGAFProjectConfiguration config;
	if (!Vans::VansGAFProjectConfiguration::LoadForProject(project,
		workspace / "ForestEngine/ForestEngine", config, error)) return ExpectGAF(false, error.c_str());
	const fs::path temporary = fs::temp_directory_path() / "ForestDemoHallThrowContract";
	fs::remove_all(temporary);
	struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{temporary};
	fs::create_directories(temporary / "Assets");
	for (const char* folder : {"PlayerAttack", "PlayerThrow", "WhisperCombat"})
		fs::copy(project / "Assets/GAF" / folder, temporary / "Assets" / folder, fs::copy_options::recursive);
	for (const char* file : {"DemoHallTags.vtagtree", "DemoHallTags.vtagtree.meta"})
		fs::copy_file(project / "Assets/GAF/WindowBreak" / file, temporary / "Assets" / file);
	Vans::VansAssetDatabase db(temporary / "Assets", temporary / "Library");
	if (!ExpectGAF(static_cast<bool>(db.Scan(Vans::VansAssetOperationPolicy::Authoring())), "Throw assets did not scan")) return false;
	Vans::VansAssetObjectRepository objects;
	if (!BootstrapGameplayMemory(db.All(), objects, error)) return ExpectGAF(false, error.c_str());
	Vans::VansGameplayRuntimeDependencies deps;
	deps.contributors.push_back(Vans::VansMakeGameplayPrimitivesGAFContributor());
	deps.contributors.push_back(MakeProjectSchemaContributor(config));
	deps.contributors.push_back(MakeTestRuntimeContributor("Gameplay.Navigation", {std::make_shared<Vans::VansFakeActionService>(Vans::VansNavigationActionCapability())}));
	deps.contributors.push_back(MakeTestRuntimeContributor("Gameplay.Combat", {std::make_shared<Vans::VansFakeActionService>(Vans::VansCombatActionCapability())}));
	deps.contributors.push_back(MakeTestRuntimeContributor("Gameplay.Animation", {std::make_shared<Vans::VansFakeActionService>(Vans::VansAnimationActionCapability())}));
	deps.contributors.push_back(MakeTestRuntimeContributor("Gameplay.AnimationEvents", {std::make_shared<Vans::VansFakeActionService>(Vans::VansAnimationEventActionCapability())}));
	deps.contributors.push_back(MakeTestRuntimeContributor("Gameplay.Projectile", {std::make_shared<Vans::VansFakeActionService>(Vans::VansProjectileActionCapability())}));
	deps.contributors.push_back(MakeTestRuntimeContributor("Gameplay.Attachment", {std::make_shared<Vans::VansFakeActionService>(Vans::VansAttachmentActionCapability())}));
	Vans::VansGameplayRuntime runtime;
	if (!runtime.Initialize(db.All(), objects, config.settings, deps, error)) return ExpectGAF(false, error.c_str());
	Vans::VansGameplayActionHostSetup setup;
	setup.actionSets = {"4534ddf1-e858-468e-a1ab-9e8b98cf6129", "274440c3-2aa8-567f-b0e2-630bed9a15a4"};
	AddHostTagInitializer(setup, "Target.Character.Player");
	const Vans::VansEntityHandle entity{901, 1};
	auto host = runtime.CreateHost(entity, setup, error);
	auto action = runtime.Assets().ResolveAction("Gameplay.DemoHall.Player.Throw.Smoke");
	auto attack = runtime.Assets().ResolveAction("Gameplay.DemoHall.Player.Attack.Crowbar");
	if (!ExpectGAF(host && action && attack && host->GrantedActions().size() == 3, "Throw grant or action missing")) return false;
	Vans::VansActionContext context;
	context.SetEntity(Vans::VansActionContextSlots::Owner, entity);
	context.SetEntity(Vans::VansActionContextSlots::Instigator, entity);
	context.SetEntity(Vans::VansActionContextSlots::Source, entity);
	context.SetEntity(Vans::VansActionContextSlots::PrimaryTarget, entity);
	context.SetSerialized(Vans::VansActionContextSlots::Payload,
		Vans::VansSerializedValue::Object({{"hasProjectile", Vans::VansSerializedValue::Bool(false)}}));
	auto started = host->ActivateAction(action->id, context);
	if (!ExpectGAF(started && !host->ActivateAction(attack->id, context)
		&& !host->ActivateAction(action->id, context), "GAF throw did not block attack or re-entry")) return false;
	auto signal = [&](Vans::VansActionHandle handle, const char* name)
	{
		Vans::VansActionEvent event;
		event.stableName = name;
		event.type = Vans::VansMakeStableId<Vans::VansActionFieldIdTag>(name);
		if (!host->EnqueueEvent(handle, std::move(event), error)) return false;
		runtime.TickEarly(0.01);
		return true;
	};
	runtime.TickEarly(5.0);
	if (!ExpectGAF(host->ActiveActions().size() == 1, "Throw must wait for Clip release, not elapsed time")) return false;
	if (!signal(started.action, "Throw.Release")) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(host->ActiveActions().size() == 1, "Release must leave the remainder of Throw active")) return false;
	if (!signal(started.action, "Throw.Finished")) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(host->ActiveActions().empty() && host->ActivateAction(attack->id, context)
		&& !host->ActivateAction(action->id, context), "Throw completion or reverse attack exclusion failed")) return false;
	runtime.TickEarly(2.6);
	started = host->ActivateAction(action->id, context);
	if (!ExpectGAF(static_cast<bool>(started), "Throw could not repeat")) return false;
	if (!signal(started.action, "Throw.Release") || !signal(started.action, "Throw.Finished")
		|| !ExpectGAF(host->ActiveActions().empty(), "Repeated Throw did not finish")) return false;

	// 真实 clip 和重定向器提供手掌、腰部及模型挂接的检查数据。
	VansAnimationClip clip;
	Skeleton source, target;
	if (!ExpectGAF(VansAnimationClipIO::Load((project / "Assets/Animations/Interaction/Throw/AS_throw_Unreal_Take.vclip").string(), clip, source)
		&& std::abs(clip.duration - 4.533333f) < 0.001f, "Throw clip duration is invalid")) return false;
	// These are sampler/codec contracts, independent of the DemoHall Lua controller.
	if (!ExpectGAF(clip.events.size() == 2 && clip.events[0].name == "Throw.Release"
		&& std::abs(clip.events[0].time - 38.0f / 30.0f) < 0.0001f
		&& clip.events[1].name == "Throw.Finished", "Throw Clip markers are missing")) return false;
	std::string encoded;
	VansAnimationClip decoded;
	Skeleton decodedSkeleton;
	if (!VansAnimationClipBinaryCodec::Encode(clip, source, encoded, error)
		|| !VansAnimationClipBinaryCodec::Decode(encoded, decoded, decodedSkeleton, error)) return ExpectGAF(false, error.c_str());
	if (!ExpectGAF(decoded.events.size() == clip.events.size() && decoded.events[0].id == clip.events[0].id
		&& decoded.boneKeyframes.size() == clip.boneKeyframes.size(), "Clip event round trip changed animation data")) return false;
	auto sampledEvents = [&](float previous, float current, bool loop)
	{
		VansAnimationSampleRequest request;
		request.previousTime = previous; request.currentTime = current;
		request.endTime = decoded.duration; request.loop = loop;
		VansPosePayload payload;
		VansAnimationSampler::Sample(decoded, decodedSkeleton, request, payload);
		return payload.events;
	};
	const auto crossed = sampledEvents(1.0f, 1.5f, false);
	if (!ExpectGAF(crossed.size() == 1 && crossed[0].name == "Throw.Release" && crossed[0].forward,
		"A coarse animation frame missed or duplicated the release")) return false;
	if (!ExpectGAF(sampledEvents(1.5f, 1.5f, false).empty() && sampledEvents(1.5f, 1.6f, false).empty(),
		"Paused or later animation frames replayed an event")) return false;
	const auto reversed = sampledEvents(1.5f, 1.0f, false);
	if (!ExpectGAF(reversed.size() == 1 && !reversed[0].forward, "Reverse event direction was lost")) return false;
	decoded.events.resize(1);
	const auto looped = sampledEvents(0, decoded.duration * 2 + 1.5f, true);
	if (!ExpectGAF(looped.size() == 3 && looped[0].loopIndex == 0 && looped[2].loopIndex == 2,
		"Multi-loop traversal lost release events")) return false;
	decoded.events[0].time = 0;
	if (!ExpectGAF(sampledEvents(0, 0.1f, false).size() == 1 && sampledEvents(0.1f, 0.2f, false).empty(),
		"Start-of-clip event did not fire exactly once")) return false;
	decoded.events[0].time = decoded.duration + 1;
	if (!ExpectGAF(!VansAnimationClipBinaryCodec::Encode(decoded, decodedSkeleton, encoded, error),
		"Clip codec accepted an out-of-range event")) return false;
	VansAnimationController aliases;
	aliases.AddClip("Throw", clip); aliases.AddClip("OtherTake", clip);
	if (!ExpectGAF(aliases.GetClip("Throw")->stableId != aliases.GetClip("OtherTake")->stableId,
		"Controller aliases collided for imported clips sharing a take name")) return false;
	const auto modelPath = project / "Assets/Characters/Survival/Models/survival_character.fbx";
	Vans::VansAssetMeta modelMeta;
	if (!Vans::VansAssetMetaStorage::Load(Vans::VansAssetMeta::MetaPathFor(modelPath), modelMeta, error)) return ExpectGAF(false, error.c_str());
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(modelPath.string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals);
	if (!ExpectGAF(scene != nullptr, "Survival model failed to load")) return false;
	VansSkinnedMeshLoader::ExtractSkeleton(scene, target, 1.0f, Vans::ReadSkeletalMeshImportSettings(modelMeta));
	VansAnimationRigAsset rig;
	VansCompiledAnimationRig compiled;
	VansRetargetProfileAsset profile;
	if (!VansAnimationRigStorage::Load(project / "Assets/AnimationRigs/Survival.vanimrig", rig, error)
		|| !VansAnimationRigCompiler::Compile(rig, target, compiled, error)
		|| !VansRetargetProfileStorage::Load(project / "Assets/Retarget/RTG_UEFN_To_Survival.vretarget", profile, error)) return ExpectGAF(false, error.c_str());
	VansRetargetRuntimeDesc desc;
	desc.translationScaleMode = profile.translationScaleMode;
	desc.translationScale = profile.explicitTranslationScale;
	desc.rootAlignment = profile.rootAlignment;
	desc.targetModelSpaceAlignment = profile.targetModelSpaceAlignment;
	desc.limbChains = profile.limbChains;
	VansRetargetProcessor processor;
	if (!ExpectGAF(processor.Build(source, target, compiled, desc), "Throw retarget failed")) return false;
	const auto handSocket = std::find_if(rig.sockets.begin(), rig.sockets.end(), [](const auto& s) { return s.name == "LeftHand_Smoke"; });
	const auto waistSocket = std::find_if(rig.sockets.begin(), rig.sockets.end(), [](const auto& s) { return s.name == "Waist_Smoke"; });
	if (!ExpectGAF(handSocket != rig.sockets.end() && waistSocket != rig.sockets.end(), "Smoke sockets missing")) return false;
	const int hand = target.boneNameToIndex.at("hand_l");
	const int pelvis = target.boneNameToIndex.at("pelvis");
	if (!ExpectGAF(compiled.sockets[compiled.FindSocketByGuid(handSocket->guid)].boneIndex == hand
		&& compiled.sockets[compiled.FindSocketByGuid(waistSocket->guid)].boneIndex == pelvis, "Smoke attached to wrong bone")) return false;
	glm::vec3 grip(0), axis(0);
	nlohmann::ordered_json poses = nlohmann::ordered_json::array();
	for (int sample = 0; sample <= 16; ++sample)
	{
		VansAnimationSampleRequest request;
		request.currentTime = clip.duration * sample / 16.0f;
		request.endTime = clip.duration;
		request.loop = false;
		VansPosePayload payload;
		if (!ExpectGAF(VansAnimationSampler::Sample(clip, source, request, payload), "Throw pose sampling failed")) return false;
		std::vector<glm::mat4> sourceModels, targetModels;
		VansPoseMath::ToMatrices(payload.localPose, sourceModels);
		for (int i : source.topologicalOrder) if (source.bones[i].parentIndex >= 0) sourceModels[i] = sourceModels[source.bones[i].parentIndex] * sourceModels[i];
		if (!ExpectGAF(processor.Process(sourceModels, source, target, targetModels), "Throw pose retarget failed")) return false;
		const glm::mat4 inverseHand = glm::inverse(targetModels[hand]);
		auto localBone = [&](const char* name) { return glm::vec3(inverseHand * targetModels[target.boneNameToIndex.at(name)][3]); };
		const glm::vec3 knuckles = (localBone("index_01_l") + localBone("middle_01_l") + localBone("ring_01_l") + localBone("pinky_01_l")) * 0.25f;
		const glm::vec3 width = glm::normalize(localBone("index_01_l") - localBone("pinky_01_l"));
		const glm::vec3 normal = glm::normalize(glm::cross(glm::normalize(knuckles), width));
		grip += knuckles * 0.5f + normal * 2.0f;
		axis += width;
		nlohmann::ordered_json frame;
		frame["time"] = request.currentTime;
		for (const char* name : {"root", "pelvis", "spine_01", "spine_05", "head", "upperarm_l", "lowerarm_l", "hand_l", "index_01_l", "pinky_01_l", "upperarm_r", "lowerarm_r", "hand_r", "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r"})
		{
			const auto p = targetModels[target.boneNameToIndex.at(name)][3];
			if (!ExpectGAF(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z), "Throw retarget produced invalid positions")) return false;
			frame[name] = {p.x,p.y,p.z};
		}
		auto socketMatrix = [](const auto& s) { return glm::translate(glm::mat4(1),s.positionLocal) * glm::mat4_cast(s.rotationLocal); };
		for (const auto* socket : {&*handSocket, &*waistSocket})
		{
			const auto matrix = targetModels[socket == &*handSocket ? hand : pelvis] * socketMatrix(*socket);
			const auto* attachment = compiled.FindAttachmentProfile(
				"f9c8f428-5388-5466-9c5d-a5dbeb564cd5", VansRigAttachmentParentKind::Socket, socket->guid);
			if (!ExpectGAF(attachment != nullptr, "Smoke attachment profile missing")) return false;
			Vans::VansLocalTransform local;
			local.position = attachment->positionLocal;
			local.rotation = attachment->rotationLocal;
			local.scale = attachment->scaleLocal;
			Vans::VansLocalTransform resolved;
			if (!ExpectGAF(Vans::VansLocalTransform::TryFromMatrix(
				glm::scale(glm::mat4(1), glm::vec3(0.01f)) * matrix * local.ToMatrix(), resolved),
				"Smoke world scale cannot be represented by the production TransformGraph")) return false;
			frame[socket->name] = {{matrix[0].x,matrix[1].x,matrix[2].x,matrix[3].x},{matrix[0].y,matrix[1].y,matrix[2].y,matrix[3].y},{matrix[0].z,matrix[1].z,matrix[2].z,matrix[3].z},{0,0,0,1}};
		}
		poses.push_back(frame);
	}
	grip /= 17.0f;
	const glm::quat rotation = glm::rotation(glm::vec3(0,1,0), glm::normalize(axis));
	std::cout << "THROW_GRIP=" << nlohmann::ordered_json({{"position",{grip.x,grip.y,grip.z}}, {"rotation",{rotation.x,rotation.y,rotation.z,rotation.w}}}).dump() << '\n';
	std::ofstream(fs::temp_directory_path() / "ForestDemoHallThrowPoses.json") << poses.dump(2);
	if (!ExpectGAF(glm::length(handSocket->positionLocal - grip) < 0.1f && std::abs(glm::dot(handSocket->rotationLocal,rotation)) > 0.999f, "Smoke grip does not match sampled left palm")) return false;
	lua_State* lua = luaL_newstate();
	if (!ExpectGAF(lua != nullptr, "Throw Lua state unavailable")) return false;
	luaL_openlibs(lua);
	lua_pushstring(lua, project.generic_string().c_str()); lua_setglobal(lua, "throw_project");
	const bool luaOk = luaL_loadfile(lua, (project / "Migration/Throw/throw_lifecycle_test.lua").string().c_str()) == LUA_OK && lua_pcall(lua, 0, 1, 0) == LUA_OK;
	if (!luaOk) error = lua_tostring(lua,-1);
	lua_close(lua);
	return ExpectGAF(luaOk, error.c_str());
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
