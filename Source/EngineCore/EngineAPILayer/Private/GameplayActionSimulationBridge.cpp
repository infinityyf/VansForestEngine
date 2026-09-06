#include "GameplayActionSimulationBridge.h"

#include "GameplayActionAuthoringBridge.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../GameplayActionAdapters/VansActionServiceAdapter.h"
#include "../../GameplayActionAdapters/VansGameplayPrimitivesContributor.h"
#include "../../GameplayActionAdapters/Audio/VansAudioActionCapability.h"
#include "../../GameplayActionAdapters/Camera/VansCameraActionService.h"
#include "../../GameplayActionAdapters/Character/VansCharacterActionServices.h"
#include "../../GameplayActionAdapters/Combat/VansCombatActionService.h"
#include "../../GameplayActionAdapters/Physics/VansPhysicsQueryActionCapability.h"
#include "../../GameplayActionAdapters/Projectile/VansProjectileActionCapability.h"
#include "../../GameplayActionAdapters/Projectile/VansProjectileActionService.h"
#include "../../GameplayActionAdapters/Character/VansAnimationEventActionService.h"
#include "../../GameplayActionAdapters/UI/VansUIActionCapability.h"
#include "../../GameplayActionAdapters/VFX/VansVFXActionCapability.h"
#include "../../GameplayActionCore/VansGameplayRuntime.h"
#include "../../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../../ProjectSystem/VansProjectManager.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace Vans::EditorAPI
{
namespace
{
std::vector<std::shared_ptr<VansFakeActionService>> CreateSimulationActionServices()
{
	std::vector<std::shared_ptr<VansFakeActionService>> services;
	for (const VansActionServiceCapability* capability : {
		&VansAnimationActionCapability(),
		&VansAudioActionCapability(),
		&VansVFXActionCapability(),
		&VansCombatActionCapability(),
		&VansPhysicsQueryActionCapability(),
		&VansProjectileActionCapability(),
		&VansAnimationEventActionCapability(),
		&VansAttachmentActionCapability(),
		&VansCameraActionCapability(),
		&VansNavigationActionCapability(),
		&VansUIActionCapability() })
		services.push_back(std::make_shared<VansFakeActionService>(*capability));
	return services;
}

const char* ActionState(VansActionInstanceState state)
{
	switch (state)
	{
	case VansActionInstanceState::Created: return "Created";
	case VansActionInstanceState::Queued: return "Queued";
	case VansActionInstanceState::Resolving: return "Resolving";
	case VansActionInstanceState::BuildingContext: return "BuildingContext";
	case VansActionInstanceState::Validating: return "Validating";
	case VansActionInstanceState::Preparing: return "Preparing";
	case VansActionInstanceState::Committing: return "Committing";
	case VansActionInstanceState::Committed: return "Committed";
	case VansActionInstanceState::Running: return "Running";
	case VansActionInstanceState::Waiting: return "Waiting";
	case VansActionInstanceState::Transitioning: return "Transitioning";
	case VansActionInstanceState::Ending: return "Ending";
	case VansActionInstanceState::Ended: return "Ended";
	}
	return "Unknown";
}

const char* ActionError(VansActionError error)
{
	return VansActionErrorCategoryName(error);
}

const char* EndReason(VansActionEndReason reason)
{
	switch (reason)
	{
	case VansActionEndReason::Completed: return "Completed";
	case VansActionEndReason::Failed: return "Failed";
	case VansActionEndReason::Cancelled: return "Cancelled";
	case VansActionEndReason::Interrupted: return "Interrupted";
	case VansActionEndReason::TimedOut: return "TimedOut";
	case VansActionEndReason::CommitFailed: return "CommitFailed";
	case VansActionEndReason::OwnerDestroyed: return "OwnerDestroyed";
	}
	return "Unknown";
}

std::string Handle(VansGenerationHandle handle)
{
	return handle ? std::to_string(handle.index) + ":" + std::to_string(handle.generation) : "None";
}

std::string TargetValue(const VansTargetDataValue& value)
{
	std::ostringstream stream;
	if (const auto* entity = std::get_if<VansEntityHandle>(&value))
		stream << "Entity " << entity->index << ':' << entity->generation;
	else if (const auto* location = std::get_if<VansTargetLocation>(&value))
		stream << "Location " << location->value[0] << ", " << location->value[1]
			<< ", " << location->value[2];
	else if (const auto* direction = std::get_if<VansTargetDirection>(&value))
		stream << "Direction " << direction->value[0] << ", " << direction->value[1]
			<< ", " << direction->value[2];
	else if (const auto* transform = std::get_if<VansTargetTransform>(&value))
		stream << "Transform " << transform->position[0] << ", " << transform->position[1]
			<< ", " << transform->position[2];
	else if (const auto* ray = std::get_if<VansTargetRay>(&value))
		stream << "Ray origin " << ray->origin[0] << ", " << ray->origin[1] << ", "
			<< ray->origin[2] << " direction " << ray->direction[0] << ", "
			<< ray->direction[1] << ", " << ray->direction[2] << " length " << ray->length;
	else if (const auto* hit = std::get_if<VansTargetHitResult>(&value))
		stream << "Hit Entity " << hit->entity.index << ':' << hit->entity.generation
			<< " distance " << hit->distance;
	else if (const auto* deferred = std::get_if<VansDeferredTargetQuery>(&value))
		stream << "Deferred Query Service " << deferred->service.value;
	return stream.str();
}

GAFDebugActionSnapshot ActionSnapshot(
	const VansActionInstanceSnapshot& source,
	const VansGameplayAssetLibrary& assets)
{
	GAFDebugActionSnapshot result;
	result.handle = Handle(source.handle.value);
	const auto definition = assets.Actions().Resolve(source.action);
	result.actionId = definition ? definition->name : std::to_string(source.action.value);
	result.state = ActionState(source.state);
	result.endReason = EndReason(source.endReason);
	result.error = ActionError(source.error);
	result.elapsedSeconds = source.elapsedSeconds;
	result.correlationId = std::to_string(source.correlationId);
	result.executor = source.executor.executor;
	result.activeNodes = source.executor.activeNodes;
	result.waitingNodes = source.executor.waitingNodes;
	if (source.hasTargetData)
		for (const auto& target : source.targetData.values)
			result.targets.push_back(TargetValue(target));
	for (const auto& variable : source.variables)
	{
		std::string name = std::to_string(variable.field.value);
		if (definition)
			for (const auto& field : definition->variables)
				if (field.id == variable.field) { name = field.name; break; }
		result.variables.push_back({ std::move(name),
			EncodeSerializedValueJson<nlohmann::ordered_json>(variable.value).dump() });
	}
	for (const auto& task : source.tasks)
		result.tasks.push_back({ Handle(task.handle.value), std::to_string(task.type.value),
			task.debugName, std::to_string(static_cast<int>(task.state)),
			task.elapsedSeconds, task.timeoutSeconds });
	for (const auto& resource : source.resources)
		result.resources.push_back({ Handle(resource.handle.value), resource.type,
			resource.debugName, Handle(resource.dependsOn.value) });
	for (const auto& event : source.recentEvents)
		result.recentEvents.push_back(std::to_string(event.sequence) + " " + event.stableName);
	for (const auto& trace : source.trace)
		result.trace.push_back(std::to_string(trace.elapsedSeconds) + " " +
			ActionState(trace.state) + " " + trace.message);
	return result;
}

GAFRuntimeDebugSnapshot BuildStep(
	const VansGameplayRuntime& runtime,
	const std::shared_ptr<VansActionHost>& host,
	VansActionHandle action,
	std::uint64_t frame,
	double timeSeconds)
{
	GAFRuntimeDebugSnapshot result;
	result.available = true;
	result.frame = frame;
	result.timeSeconds = timeSeconds;
	result.contentManifestHash = runtime.Assets().ContentManifestHash();
	GAFDebugHostSnapshot hostSnapshot;
	hostSnapshot.owner = Handle({ host->Owner().index, host->Owner().generation });
	hostSnapshot.enabled = host->IsEnabled();
	hostSnapshot.activeCueCount = host->Cues().ActiveCount();
	for (const auto& [tag, count] : host->Tags().Snapshot())
	{
		const auto* definition = runtime.Assets().Tags().Resolve(tag);
		hostSnapshot.tags.push_back({ definition ? definition->name : std::to_string(tag.value),
			std::to_string(count) });
	}
	for (const auto& attribute : host->Attributes().Capture())
	{
		const auto* definition = runtime.Assets().Attributes().Resolve(attribute.attribute);
		std::ostringstream value;
		value << attribute.currentValue << " (base " << attribute.baseValue << ')';
		hostSnapshot.attributes.push_back({ definition ? definition->name :
			std::to_string(attribute.attribute.value), value.str() });
	}
	for (const auto& effect : host->Effects().Snapshot())
		hostSnapshot.effects.push_back({ std::to_string(effect.effect.value),
			"stacks=" + std::to_string(effect.stacks) +
			" remaining=" + std::to_string(effect.remainingSeconds) });
	for (const auto& grant : host->GrantedActions())
	{
		const auto definition = runtime.Assets().Actions().Resolve(grant.action);
		hostSnapshot.grants.push_back({ definition ? definition->name :
			std::to_string(grant.action.value), "extensions=" +
			std::to_string(grant.extensions.size()) });
	}
	std::vector<VansActionInstanceSnapshot> actions = host->ActiveActions();
	if (action)
	{
		const auto queried = host->Query(action);
		const bool present = std::any_of(actions.begin(), actions.end(), [&](const auto& value)
			{ return value.handle == action; });
		if (queried && !present) actions.push_back(*queried);
	}
	for (const auto& actionSnapshot : actions)
		hostSnapshot.actions.push_back(ActionSnapshot(actionSnapshot, runtime.Assets()));
	result.hosts.push_back(std::move(hostSnapshot));
	return result;
}

std::string NormalizePath(std::filesystem::path path)
{
	std::string value = path.lexically_normal().generic_string();
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
	return value;
}
}

GAFSimulationResult GameplayActionSimulationBridge::Simulate(
	const GAFSimulationRequest& request)
{
	GAFSimulationResult result;
	auto& projectManager = VansProjectManager::Get();
	if (!projectManager.IsProjectLoaded())
	{
		result.message = "Open a project before running the Gameplay Action Simulator";
		return result;
	}
	if (request.sourcePath.empty() || request.tickCount > 10000 ||
		!std::isfinite(request.deltaSeconds) || request.deltaSeconds < 0.0 ||
		!std::isfinite(request.targetX) || !std::isfinite(request.targetY) ||
		!std::isfinite(request.targetZ) || !std::isfinite(request.rayDirectionX) ||
		!std::isfinite(request.rayDirectionY) || !std::isfinite(request.rayDirectionZ) ||
		!std::isfinite(request.rayLength))
	{
		result.message = "Gameplay Action simulation parameters are invalid or exceed the tick budget";
		return result;
	}
	const GAFEditorDocumentSnapshot document = GameplayActionAuthoringBridge::Open(request.sourcePath);
	if (!document.success || !document.cookable)
	{
		result.message = document.success
			? "The current GAF document must pass Cook validation before simulation" : document.message;
		return result;
	}
	VansSerializedValue sourceDocument;
	VansSerializedValue payload;
	try
	{
		sourceDocument = DecodeSerializedValueJson(
			nlohmann::ordered_json::parse(document.canonicalJson));
		payload = DecodeSerializedValueJson(nlohmann::ordered_json::parse(
			request.payloadJson.empty() ? "{}" : request.payloadJson));
	}
	catch (const std::exception& exception)
	{
		result.message = std::string("Simulation JSON is invalid: ") + exception.what();
		return result;
	}
	result.actionReference = request.actionReference;
	if (result.actionReference.empty() && document.assetType == AssetType::ActionDefinition)
		result.actionReference = ReadSerializedStringField(sourceDocument, "id");
	if (result.actionReference.empty())
	{
		result.message = "Select an Action reference when simulating a non-Action asset";
		return result;
	}
	const VansGAFProjectConfiguration* configuration =
		projectManager.GetGAFProjectConfiguration();
	std::string error;
	if (!configuration)
	{
		result.message = "GAF project configuration is unavailable in memory";
		return result;
	}
	if (request.payloadJson.size() > configuration->settings.performance.maximumPayloadBytes)
	{
		result.message = "Simulation payload exceeds the project maximumPayloadBytes budget";
		return result;
	}
	std::vector<VansAssetRecord> records = projectManager.EnumerateAssetRecords();
	const std::string sourcePath = NormalizePath(request.sourcePath);
	const bool indexed = std::any_of(records.begin(), records.end(), [&](const auto& record)
	{
		return (!record.authoringPath.empty() && NormalizePath(record.authoringPath) == sourcePath) ||
			(!record.sourcePath.empty() && NormalizePath(record.sourcePath) == sourcePath);
	});
	if (!indexed)
	{
		result.message = "The current GAF document is not present in the project asset index";
		return result;
	}
	auto fakeServices = CreateSimulationActionServices();
	VansGameplayRuntimeDependencies dependencies;
	dependencies.sourceOverrides.push_back({ request.sourcePath, std::move(sourceDocument) });
	dependencies.contributors.push_back(VansMakeGameplayPrimitivesGAFContributor());
	dependencies.contributors.push_back(VansMakeGAFModuleContributor(
		VansMakeGAFModuleDescriptor("Simulation", "GAF Action Simulation",
			{ "Core" }, {}, VansGAFModuleSource::Engine),
		{}, {},
		[fakeServices = std::move(fakeServices)](
			VansGAFRuntimeRegistry& contribution,
			std::string& contributionError)
		{
			for (const auto& service : fakeServices)
				if (!contribution.RegisterService(service, contributionError)) return false;
			return true;
		}));
	VansGameplayRuntime runtime;
	if (!runtime.Initialize(records, projectManager.GetAssetObjectRepository(),
		configuration->settings, dependencies, error))
	{
		result.message = "Gameplay Action Simulator runtime failed to initialize: " + error;
		return result;
	}
	VansGameplayActionHostSetup setup;
	setup.grants.push_back({ result.actionReference });
	for (const GAFSimulationInitializer& initializer : request.initializers)
	{
		nlohmann::ordered_json inputsJson;
		try
		{
			inputsJson = nlohmann::ordered_json::parse(initializer.inputsJson);
		}
		catch (const std::exception& exception)
		{
			result.message = "Simulation Host initializer JSON is invalid: " +
				std::string(exception.what());
			return result;
		}
		VansSerializedValue inputs = DecodeSerializedValueJson(inputsJson);
		if (initializer.type.empty() || inputs.kind != VansSerializedValue::Kind::Object)
		{
			result.message = "Simulation Host initializer requires a TypeId and object inputs";
			return result;
		}
		setup.initializers.push_back({ initializer.type, std::move(inputs) });
	}
	const VansEntityHandle owner{ request.owner.index, request.owner.generation };
	auto host = runtime.CreateHost(owner, setup, error);
	if (!host)
	{
		result.message = "Gameplay Action Simulator Host failed to initialize: " + error;
		return result;
	}
	const auto action = runtime.Assets().ResolveAction(result.actionReference);
	const auto grants = host->GrantedActions();
	const auto grant = action ? std::find_if(grants.begin(), grants.end(), [&](const auto& candidate)
		{ return candidate.action == action->id; }) : grants.end();
	if (!action || grant == grants.end())
	{
		result.message = "Simulation Action could not be resolved or granted";
		return result;
	}
	VansActionContext context;
	context.SetEntity(VansActionContextSlots::Owner, owner);
	context.SetEntity(VansActionContextSlots::Instigator,
		{ request.instigator.index, request.instigator.generation });
	context.SetEntity(VansActionContextSlots::Source, owner);
	context.randomSeed = request.randomSeed;
	context.SetSerialized(VansActionContextSlots::Payload, std::move(payload));
	VansTargetData targetData;
	switch (request.targetKind)
	{
	case GAFSimulationTargetKind::None: break;
	case GAFSimulationTargetKind::Entity:
		context.SetEntity(VansActionContextSlots::PrimaryTarget,
			{ request.primaryTarget.index, request.primaryTarget.generation }); break;
	case GAFSimulationTargetKind::Location:
		targetData.values.push_back(VansTargetLocation{
			{ request.targetX, request.targetY, request.targetZ } }); break;
	case GAFSimulationTargetKind::Ray:
		if (request.rayLength <= 0.0 || request.rayDirectionX * request.rayDirectionX +
			request.rayDirectionY * request.rayDirectionY +
			request.rayDirectionZ * request.rayDirectionZ <= 0.0)
		{
			result.message = "Simulation Ray requires a non-zero direction and positive length";
			return result;
		}
		targetData.values.push_back(VansTargetRay{
			{ request.targetX, request.targetY, request.targetZ },
			{ request.rayDirectionX, request.rayDirectionY, request.rayDirectionZ },
			request.rayLength }); break;
	case GAFSimulationTargetKind::EntitySet:
		for (const GAFSimulationEntity& entity : request.targetEntities)
			targetData.values.push_back(VansEntityHandle{ entity.index, entity.generation });
		if (targetData.values.empty())
		{
			result.message = "Simulation EntitySet requires at least one entity";
			return result;
		}
		context.SetEntity(VansActionContextSlots::PrimaryTarget,
			std::get<VansEntityHandle>(targetData.values.front()));
		break;
	}
	if (!targetData.values.empty()) context.SetTargetData(
		VansActionContextSlots::TargetData, host->StoreTargetData(std::move(targetData)));
	VansActionActivationRequest activation;
	activation.spec = grant->handle;
	activation.context = std::move(context);
	const VansActionResult canActivate = host->CanActivate(activation.spec, activation.context);
	result.canActivate = static_cast<bool>(canActivate);
	result.error = ActionError(canActivate.error);
	result.reasonCode = canActivate.StableReasonCode();
	result.message = canActivate.message;
	result.disposition = result.canActivate ? "Allowed" : "Rejected";
	VansActionHandle actionHandle;
	if (request.mode == GAFSimulationMode::Execute && result.canActivate)
	{
		const VansActionResult activated = host->Activate(activation);
		result.activated = static_cast<bool>(activated);
		actionHandle = activated.action;
		result.error = ActionError(activated.error);
		result.reasonCode = activated.StableReasonCode();
		result.message = activated.message;
		result.disposition = activated ?
			(activated.disposition == VansActionActivationDisposition::Queued ? "Queued" : "Activated")
			: "Rejected";
		result.steps.push_back(BuildStep(runtime, host, actionHandle, 0, 0.0));
		for (std::uint32_t tick = 0; tick < request.tickCount; ++tick)
		{
			runtime.TickEarly(request.deltaSeconds);
			runtime.RunLateContinuation();
			result.steps.push_back(BuildStep(runtime, host, actionHandle,
				static_cast<std::uint64_t>(tick) + 1,
				request.deltaSeconds * static_cast<double>(tick + 1)));
		}
	}
	else result.steps.push_back(BuildStep(runtime, host, {}, 0, 0.0));
	for (const auto& service : fakeServices)
		result.serviceActivity.push_back({ service->Capability().stableName,
			"commands=" + std::to_string(service->ExecutedCommandCount()) +
			" activeResources=" + std::to_string(service->ActiveResourceCount()) });
	result.success = true;
	return result;
}
}
