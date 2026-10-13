#include "VansAIWorld.h"
#include "VansAIPerception.h"

#include "../AnimationCore/VansAnimationController.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../EventCore/VansEventBus.h"
#include "../GameplayActionCore/VansActionHost.h"
#include "../GameplayActionCore/VansGameplayRuntime.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../PhysicsCore/VansPhysicsQuery.h"
#include "../SceneRuntime/Transform/VansTransformStore.h"
#include "../SceneRuntime/VansComponentStorage.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../SceneRuntime/VansRuntimeWorld.h"
#include "../Util/VansLog.h"
#include "../Util/VansProfiler.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <../../GLM/gtc/quaternion.hpp>

namespace Vans
{
namespace
{
constexpr std::uint64_t kPerceptionScheduleLane = 0x7065726365707469ull;
constexpr std::uint64_t kDecisionScheduleLane = 0x6465636973696f6eull;
constexpr std::size_t kMaxDiagnosticAgents = 256u;
constexpr std::size_t kMaxDiagnosticBlackboardEntries = 64u;

bool UsesNavigationTasks(const VansAIBehaviorAsset& behavior)
{
	return std::any_of(behavior.states.begin(), behavior.states.end(), [](const VansAIStateDefinition& state)
	{ return state.task != VansAITaskKind::Hold; });
}

float StableSchedulePhase(
	std::uint64_t entityKey,
	std::uint64_t lane,
	float interval)
{
	std::uint64_t value = entityKey + lane + 0x9e3779b97f4a7c15ull;
	value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
	value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
	value ^= value >> 31u;
	const float unit = static_cast<float>(value & 0xffffu) / 65536.0f;
	return interval * unit;
}

bool AdvanceScheduledUpdate(
	float deltaSeconds,
	float interval,
	std::uint64_t entityKey,
	std::uint64_t lane,
	float& remaining,
	bool& started)
{
	if (!started)
	{
		started = true;
		remaining = interval + StableSchedulePhase(entityKey, lane, interval);
		return true;
	}
	remaining -= deltaSeconds;
	if (remaining > 0.0f) return false;
	do remaining += interval;
	while (remaining <= 0.0f);
	return true;
}

template <typename T>
VansComponentStorage<T>* FindStorage(VansRuntimeWorld& world, std::uint16_t typeId)
{
	return world.FindStorage<T>(typeId);
}

template <typename T>
const T* FindOwnedEnabledComponent(
	const VansComponentStorage<T>* storage,
	VansEntityHandle owner)
{
	return storage ? storage->FindFirstEffectiveOwnedBy(owner) : nullptr;
}

glm::vec3 CharacterOrigin(const VansEngine::VansCharacterControllerNode& controller)
{
	return controller.GetPosition() - controller.GetProperties().m_PositionOffset;
}

const VansGraphics::VansCompiledAnimationRig* ResolveAnimationRig(
	VansGraphics::VansAnimationNode* animationNode)
{
	if (!animationNode) return nullptr;
	VansGraphics::VansAnimationController* controller =
		animationNode->GetCharacterMotionController();
	return controller ? controller->GetAnimationRig() : nullptr;
}

float ResolveOwnerFacingYaw(
	VansGraphics::VansAnimationNode* animationNode,
	float desiredVisualYaw)
{
	const VansGraphics::VansCompiledAnimationRig* rig = ResolveAnimationRig(animationNode);
	return rig ? ResolveModelOwnerFacingYaw(desiredVisualYaw, rig->modelForward)
		: desiredVisualYaw;
}

float CurrentOwnerFacingYaw(const VansEngine::VansCharacterControllerNode& controller)
{
	const std::uint32_t transformId = controller.GetTransformID();
	return transformId == UINT32_MAX ? 0.0f
		: Vans::VansTransformStore::Read(transformId).m_Rotation.y;
}

glm::vec3 ResolveModelVisualForward(
	VansGraphics::VansAnimationNode* animationNode,
	float ownerFacingYaw)
{
	glm::vec3 modelForward(0.0f, 0.0f, 1.0f);
	if (const VansGraphics::VansCompiledAnimationRig* rig = ResolveAnimationRig(animationNode))
		modelForward = rig->modelForward;
	modelForward.y = 0.0f;
	if (glm::length(modelForward) <= 1.0e-4f)
		modelForward = glm::vec3(0.0f, 0.0f, 1.0f);
	const glm::quat ownerRotation = glm::angleAxis(
		glm::radians(ownerFacingYaw), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::vec3 worldForward = ownerRotation * glm::normalize(modelForward);
	worldForward.y = 0.0f;
	return glm::length(worldForward) > 1.0e-4f
		? glm::normalize(worldForward) : glm::vec3(0.0f, 0.0f, 1.0f);
}

struct SceneLineOfSightResult
{
	bool tested = false;
	bool visible = true;
	std::string hitObject;
};

SceneLineOfSightResult QuerySceneLineOfSight(
	const glm::vec3& observer,
	const glm::vec3& target,
	const std::string& occlusionLayer)
{
	SceneLineOfSightResult result;
	if (occlusionLayer.empty()) return result;
	int layerIndex = -1;
	if (!VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(
		occlusionLayer, layerIndex) || layerIndex < 0)
	{
		return result;
	}

	glm::vec3 direction = target - observer;
	const float distance = glm::length(direction);
	if (!std::isfinite(distance) || distance <= 0.05f) return result;
	VansEngine::VansPhysicsRaycastRequest request;
	request.origin = observer;
	request.direction = direction;
	request.distance = distance - 0.05f;
	request.filter.layerMask = 1u << static_cast<std::uint32_t>(layerIndex);
	request.filter.includeTriggers = false;
	VansEngine::VansPhysicsQueryHit hit;
	result.tested = true;
	result.visible = !VansEngine::VansPhysicsQuery::RaycastClosest(request, hit);
	if (!result.visible)
		result.hitObject = hit.objectName;
	return result;
}

void UpdateAnimationMovementParameter(
	VansGraphics::VansAnimationNode* animationNode,
	const VansRuntimeAIAgentComponent& config,
	const VansEngine::VansCharacterControllerNode& controller)
{
	VANS_PROFILE_SCOPE("AI::AnimationWrite", Vans::ProfileCategory::Animation);
	if (!animationNode || config.movementParameter.empty()) return;
	VansGraphics::VansAnimationController* animation =
		animationNode->GetCharacterMotionController();
	if (!animation) return;
	const glm::vec3 velocity = controller.GetTrajectory().currentVelocityWorld;
	const float speed = glm::length(glm::vec2(velocity.x, velocity.z));
	const int moveState = ResolveAIMovementState(speed, config);
	animation->SetInt(config.movementParameter, moveState);
}
}

VansAIWorld::~VansAIWorld()
{
	Shutdown();
}

std::uint64_t VansAIWorld::EntityKey(VansEntityHandle entity)
{
	return (static_cast<std::uint64_t>(entity.generation) << 32u) |
		static_cast<std::uint64_t>(entity.index);
}

void VansAIWorld::RecordError(AgentRuntime& runtime,
	VansEntityHandle entity, const std::string& message)
{
	runtime.diagnostic = message;
	if (runtime.lastLoggedError == message) return;
	runtime.lastLoggedError = message;
	VANS_LOG_ERROR("[AI] Entity=" << entity.index << ' ' << message);
}

bool VansAIWorld::Initialize(VansRuntimeWorld& world,
	VansGameplayRuntime* gameplayRuntime,
	const VansAssetObjectRepository& assetObjects,
	VansNavigationWorld& navigationWorld,
	std::string& error,VansAIInitializationPolicy policy)
{
	Shutdown();
	m_World = &world;
	m_GameplayRuntime = gameplayRuntime;
	m_AssetObjects = &assetObjects;
	m_NavigationWorld = &navigationWorld;
	m_Connections.Add(VansEventBus::Get().Subscribe<VansAIActivationRequested>(
		[this](const VansAIActivationRequested& event)
		{
			if (!event.target.IsValid() || event.sourceGuid.empty()) return;
			const std::uint64_t key = EntityKey(event.target);
			if (auto found = m_Agents.find(key); found != m_Agents.end() && found->second.initialized)
			{
				std::string error;
				if (!found->second.blackboard.SetBool(
					found->second.behavior->bindings.activationRequested, true,
					event.sourceGuid, &error))
				{
					RecordError(found->second, event.target,
						"Activation event rejected: " + error);
				}
			}
			else
				m_PendingActivation.insert_or_assign(key, event.sourceGuid);
		}, VansEventLane::GameLogic, 0));
	m_Connections.Add(VansEventBus::Get().Subscribe<VansAIGameplayReleased>(
		[this](const VansAIGameplayReleased& event)
		{
			if (!event.target.IsValid() || event.sourceGuid.empty()) return;
			const std::uint64_t key = EntityKey(event.target);
			if (auto found = m_Agents.find(key); found != m_Agents.end() && found->second.initialized)
			{
				std::string error;
				if (!found->second.blackboard.SetBool(
					found->second.behavior->bindings.gameplayReleased, true,
					event.sourceGuid, &error))
				{
					RecordError(found->second, event.target,
						"Gameplay release event rejected: " + error);
				}
			}
			else
				m_PendingGameplayRelease.insert_or_assign(key, event.sourceGuid);
		}, VansEventLane::GameLogic, 0));
	if (InitializeExistingAgents(error,policy))
		return true;
	Shutdown();
	return false;
}

void VansAIWorld::Shutdown()
{
	m_Connections.DisconnectAll();
	if (m_NavigationWorld)
		for (auto& [key, runtime] : m_Agents)
		{
			std::string error;
			if (runtime.moveRequest.IsValid()) m_NavigationWorld->Stop(runtime.moveRequest, runtime.requestOwner, error);
            if(runtime.patrolQuery.IsValid()) m_NavigationWorld->ReleaseQuery(runtime.patrolQuery,runtime.requestOwner,error);
		}
	m_Agents.clear();
	m_PendingActivation.clear();
	m_PendingGameplayRelease.clear();
	m_World = nullptr;
	m_GameplayRuntime = nullptr;
	m_AssetObjects = nullptr;
	m_NavigationWorld = nullptr;
}

std::shared_ptr<const VansAIBehaviorAsset> VansAIWorld::ResolveBehavior(
	const std::string& guidText, std::string& error) const
{
	VansAssetGuid guid;
	if (!m_AssetObjects || !VansAssetGuid::TryParse(guidText, guid))
	{
		error = "AIAgent behavior must reference a valid asset GUID";
		return nullptr;
	}
	auto asset = m_AssetObjects->ResolveLatest<VansAIBehaviorAsset>(guid);
	if (!asset)
		error = "AI Behavior asset is not loaded in the object repository: " + guidText;
	return asset;
}

bool VansAIWorld::InitializeAgent(AgentRuntime& runtime,
	const VansRuntimeAIAgentComponent& ai,
	std::string& error)
{
	if (!ValidateAIRuntimeTiming(ai, error)) return false;
	runtime.behavior = ResolveBehavior(ai.behaviorGuid, error);
	if (!runtime.behavior) return false;
	if (!runtime.blackboard.Configure(runtime.behavior->blackboard, error)) return false;
	if (!runtime.blackboard.Has(runtime.behavior->bindings.activationRequested,
			VansAIValueType::Bool) ||
		!runtime.blackboard.Has(runtime.behavior->bindings.gameplayReleased,
			VansAIValueType::Bool) ||
		!runtime.blackboard.Has(runtime.behavior->bindings.target,
			VansAIValueType::Entity))
	{
		error = "AI Behavior bindings do not match the configured Blackboard";
		return false;
	}
	if (runtime.behavior->maxTransitionsPerUpdate < 1u ||
		runtime.behavior->maxTransitionsPerUpdate > 64u)
	{
		error = "AI Behavior maxTransitionsPerUpdate must be in [1, 64]";
		return false;
	}
	if (ai.sight.enabled)
	{
		if (ai.sight.blackboardKey.empty())
		{
			error = "AIAgent sight requires a Blackboard key";
			return false;
		}
		if (!runtime.blackboard.SetBool(ai.sight.blackboardKey, false,
			"AI.Initialization", &error))
			return false;
	}
	runtime.currentState = runtime.behavior->initialState;
	runtime.initialized = true;
	return true;
}

bool VansAIWorld::InitializeExistingAgents(std::string& error,VansAIInitializationPolicy policy)
{
	if (!m_World)
	{
		error = "AI World requires a RuntimeWorld";
		return false;
	}
	auto* aiStorage = FindStorage<VansRuntimeAIAgentComponent>(
		*m_World, VansRuntimeComponentType_AIAgent);
	if (!aiStorage)
	{
		error.clear();
		return true;
	}
	auto* navigationStorage = FindStorage<VansRuntimeNavigationAgentComponent>(
		*m_World, VansRuntimeComponentType_NavigationAgent);
	auto* cctStorage = FindStorage<VansRuntimeCharacterControllerComponent>(
		*m_World, VansRuntimeComponentType_CharacterController);
	const auto& headers = aiStorage->Headers();
	const auto& agents = aiStorage->DenseData();
	for (std::size_t index = 0; index < headers.size(); ++index)
	{
		const VansComponentHeader& header = headers[index];
		if (!header.effectiveEnabled || !m_World->IsAlive(header.owner))
			continue;
		const auto* navigation = FindOwnedEnabledComponent(
			navigationStorage, header.owner);
		const auto* cct = FindOwnedEnabledComponent(cctStorage, header.owner);
		const std::uint64_t key = EntityKey(header.owner);
		if (m_Agents.find(key) != m_Agents.end())
		{
			error = "Entity has more than one enabled AIAgent component";
			VANS_LOG_ERROR("[AI] Entity=" << header.owner.index << ' ' << error);
			return false;
		}
		AgentRuntime runtime;
		if (!InitializeAgent(runtime, agents[index], error))
		{
			VANS_LOG_ERROR("[AI] Entity=" << header.owner.index << ' ' << error);
			return false;
		}
		if (UsesNavigationTasks(*runtime.behavior) && (!navigation || !cct || !cct->controllerNode ||
            (policy==VansAIInitializationPolicy::RequireReadyNavigation && !m_NavigationWorld->HasAgent(header.owner))))
		{ error = "AI moving tasks require a registered NavigationAgent and motor"; return false; }
		if (header.stableGuid.empty()) { error = "AIAgent requires a stable component GUID for navigation ownership"; return false; }
		runtime.requestOwner = header.stableGuid;
		VANS_LOG("[AI] Initialized entity=" << header.owner.index
			<< " behavior='" << runtime.behavior->name
			<< "' state='" << runtime.currentState << "'");
		m_Agents.emplace(key, std::move(runtime));
	}
	error.clear();
	return true;
}

void VansAIWorld::Update(double deltaSeconds)
{
	if (!m_World) return;
	auto removeDeadPendingEvents = [this](auto& pendingEvents)
	{
		for (auto it = pendingEvents.begin(); it != pendingEvents.end();)
		{
			const VansEntityHandle entity{
				static_cast<std::uint32_t>(it->first & 0xffffffffull),
				static_cast<std::uint32_t>(it->first >> 32u) };
			if (!m_World->IsAlive(entity)) it = pendingEvents.erase(it);
			else ++it;
		}
	};
	removeDeadPendingEvents(m_PendingActivation);
	removeDeadPendingEvents(m_PendingGameplayRelease);

	auto* aiStorage = FindStorage<VansRuntimeAIAgentComponent>(
		*m_World, VansRuntimeComponentType_AIAgent);
	auto* navigationStorage = FindStorage<VansRuntimeNavigationAgentComponent>(
		*m_World, VansRuntimeComponentType_NavigationAgent);
	auto* cctStorage = FindStorage<VansRuntimeCharacterControllerComponent>(
		*m_World, VansRuntimeComponentType_CharacterController);
	auto* animationStorage = FindStorage<VansRuntimeAnimationComponent>(
		*m_World, VansRuntimeComponentType_Animation);
	if (!aiStorage) return;

	std::unordered_set<std::uint64_t> liveAgents;
	const auto& aiHeaders = aiStorage->Headers();
	const auto& aiComponents = aiStorage->DenseData();
	for (std::size_t aiIndex = 0; aiIndex < aiHeaders.size(); ++aiIndex)
	{
		const VansComponentHeader& header = aiHeaders[aiIndex];
		if (!header.effectiveEnabled || !m_World->IsAlive(header.owner)) continue;
		const std::uint64_t key = EntityKey(header.owner);
		liveAgents.insert(key);
		const VansRuntimeAIAgentComponent& ai = aiComponents[aiIndex];
		const VansRuntimeNavigationAgentComponent* navigation =
			FindOwnedEnabledComponent(navigationStorage, header.owner);
		const VansRuntimeCharacterControllerComponent* cct =
			FindOwnedEnabledComponent(cctStorage, header.owner);
		const VansRuntimeAnimationComponent* animation =
			FindOwnedEnabledComponent(animationStorage, header.owner);
		AgentRuntime& runtime = m_Agents[key];
		runtime.requestOwner = header.stableGuid;
		if (!runtime.initialized)
		{
			std::string error;
			if (!InitializeAgent(runtime, ai, error))
			{
				RecordError(runtime, header.owner, error);
				continue;
			}
			if (const auto pending = m_PendingActivation.find(key);
				pending != m_PendingActivation.end())
			{
				runtime.blackboard.SetBool(
					runtime.behavior->bindings.activationRequested, true, pending->second);
				m_PendingActivation.erase(pending);
			}
			if (const auto pending = m_PendingGameplayRelease.find(key);
				pending != m_PendingGameplayRelease.end())
			{
				runtime.blackboard.SetBool(
					runtime.behavior->bindings.gameplayReleased, true, pending->second);
				m_PendingGameplayRelease.erase(pending);
			}
			VANS_LOG("[AI] Initialized entity=" << header.owner.index
				<< " behavior='" << runtime.behavior->name
				<< "' state='" << runtime.currentState << "'");
		}
		if (UsesNavigationTasks(*runtime.behavior) && (!navigation || !cct || !cct->controllerNode || !m_NavigationWorld->HasAgent(header.owner)))
		{
			RecordError(runtime, header.owner, "AI moving tasks require a registered NavigationAgent and motor");
            std::string queryError;
            if(runtime.patrolQuery.IsValid()) m_NavigationWorld->ReleaseQuery(runtime.patrolQuery,runtime.requestOwner,queryError);
            runtime.patrolQuery={};
			continue;
		}
		const double nonNegativeDelta = std::isfinite(deltaSeconds)
			? (std::max)(0.0, deltaSeconds) : 0.0;
		const float dt = static_cast<float>((std::min)(
			nonNegativeDelta,
			static_cast<double>(ai.timing.maximumDeltaSeconds)));
		if (nonNegativeDelta > ai.timing.maximumDeltaSeconds &&
			!runtime.deltaClampReported)
		{
			runtime.deltaClampReported = true;
			runtime.diagnostic = "AI delta clamped from " +
				std::to_string(nonNegativeDelta) + " to " +
				std::to_string(ai.timing.maximumDeltaSeconds) + " seconds";
			VANS_LOG_WARN("[AI] Entity=" << header.owner.index << ' '
				<< runtime.diagnostic);
		}
		const bool perceptionDue = AdvanceScheduledUpdate(
			dt,
			ai.timing.perceptionInterval,
			key,
			kPerceptionScheduleLane,
			runtime.perceptionRemaining,
			runtime.perceptionStarted);
		const bool decisionDue = AdvanceScheduledUpdate(
			dt,
			ai.timing.decisionInterval,
			key,
			kDecisionScheduleLane,
			runtime.decisionRemaining,
			runtime.decisionStarted);

		VansGraphics::VansAnimationNode* animationNode =
			animation ? animation->animationNode : nullptr;
		auto* controller = cct ? cct->controllerNode : nullptr;
		runtime.movementBlocked = controller && controller->IsGameplayMovementBlocked();
		const auto* transforms = FindStorage<VansRuntimeTransformComponent>(*m_World, VansRuntimeComponentType_Transform);
		const auto* transform = FindOwnedEnabledComponent(transforms, header.owner);
		if (!controller && (!transform || !VansTransformStore::IsAllocated(transform->transformStoreId)))
		{ RecordError(runtime, header.owner, "AIAgent requires a valid observer Transform"); continue; }
		const glm::vec3 agentPosition = controller ? CharacterOrigin(*controller) : VansTransformStore::Read(transform->transformStoreId).m_Position;
		const float observerYaw = controller ? CurrentOwnerFacingYaw(*controller) : VansTransformStore::Read(transform->transformStoreId).m_Rotation.y;

		VansEngine::VansCharacterControllerNode* targetController = nullptr;
		if (perceptionDue)
		{
			VANS_PROFILE_SCOPE("AI::Perception", Vans::ProfileCategory::Script);
			VansEntityHandle target;
			std::size_t taggedTargetCount = 0;
			std::size_t enabledTargetCount = 0;
			if (m_GameplayRuntime && m_GameplayRuntime->IsInitialized())
			{
				const std::optional<VansGameplayTagId> targetTag =
					m_GameplayRuntime->Assets().Tags().FindId(ai.targetTag);
				if (targetTag)
				{
					for (const std::shared_ptr<VansActionHost>& host : m_GameplayRuntime->Hosts())
					{
						if (!host || !host->Tags().Has(*targetTag)) continue;
						++taggedTargetCount;
						const VansRuntimeCharacterControllerComponent* candidate =
							FindOwnedEnabledComponent(cctStorage, host->Owner());
						if (!candidate || !candidate->controllerNode ||
							!candidate->controllerNode->IsEnabled()) continue;
						++enabledTargetCount;
						target = host->Owner();
						targetController = candidate->controllerNode;
						break;
					}
				}
			}
			if (runtime.target != target)
			{
				const VansEntityRecord* targetRecord = target.IsValid()
					? m_World->Entities().Get(target) : nullptr;
				VANS_LOG("[AI] Entity=" << header.owner.index << " target='"
					<< (targetRecord ? targetRecord->name : std::string("<none>"))
					<< "' taggedCandidates=" << taggedTargetCount
					<< " enabledCandidates=" << enabledTargetCount);
				std::string error;
				if (runtime.moveRequest.IsValid()) m_NavigationWorld->Stop(runtime.moveRequest, runtime.requestOwner, error);
				runtime.moveRequest = {};
				runtime.hasObservedTargetPosition = false;
				runtime.targetVisible = false;
				runtime.timeSinceTargetVisible = 0.0f;
			}
			runtime.target = target;
			runtime.blackboard.SetEntity(runtime.behavior->bindings.target, target,
				"AI.Perception");
			runtime.rawTargetVisible = targetController != nullptr;
			runtime.lineOfSightTested = false;
			runtime.lineOfSightBlocked = false;
			runtime.lineOfSightHit.clear();
			if (ai.sight.enabled && targetController)
			{
				const glm::vec3 observerEye = agentPosition +
					glm::vec3(0.0f, ai.sight.eyeHeight, 0.0f);
				const glm::vec3 targetCenter = targetController->GetPosition();
				const glm::vec3 visualForward = ResolveModelVisualForward(
					animationNode, observerYaw);
				const bool insideVisionCone = IsTargetInsideAIVisionCone(
					observerEye,
					visualForward,
					targetCenter,
					ai.sight.range,
					ai.sight.horizontalFovDegrees);
				runtime.lineOfSightOrigin = observerEye;
				runtime.lineOfSightTarget = targetCenter;
				if (insideVisionCone)
				{
					const SceneLineOfSightResult lineOfSight = QuerySceneLineOfSight(
						observerEye, targetCenter, ai.sight.occlusionLayer);
					runtime.lineOfSightTested = lineOfSight.tested;
					runtime.lineOfSightBlocked = lineOfSight.tested && !lineOfSight.visible;
					runtime.lineOfSightHit = lineOfSight.hitObject;
					runtime.rawTargetVisible = lineOfSight.visible;
				}
				else
				{
					runtime.rawTargetVisible = false;
				}
			}
		}
		if (!targetController && runtime.target.IsValid())
		{
			const VansRuntimeCharacterControllerComponent* targetCct =
				FindOwnedEnabledComponent(cctStorage, runtime.target);
			if (targetCct && targetCct->controllerNode &&
				targetCct->controllerNode->IsEnabled())
			{
				targetController = targetCct->controllerNode;
			}
		}
		if (!targetController)
			runtime.rawTargetVisible = false;
		glm::vec3 targetPosition(0.0f);
		if (targetController)
			targetPosition = CharacterOrigin(*targetController);
		if (ai.sight.enabled)
		{
			if (runtime.rawTargetVisible)
			{
				runtime.timeSinceTargetVisible = 0.0f;
				runtime.targetVisible = true;
				if (perceptionDue)
				{
					runtime.observedTargetPosition = targetPosition;
					runtime.hasObservedTargetPosition = true;
				}
			}
			else if (targetController && runtime.targetVisible)
			{
				runtime.timeSinceTargetVisible += dt;
				runtime.targetVisible = runtime.timeSinceTargetVisible <=
					ai.sight.loseTargetGraceSeconds;
			}
			else
			{
				runtime.timeSinceTargetVisible = 0.0f;
				runtime.targetVisible = false;
			}
			// Navigation runs every frame, while perception is sampled. Keep its goal
			// on the observed sample between polls as well as during sight memory.
			if (runtime.hasObservedTargetPosition)
				targetPosition = runtime.observedTargetPosition;
			if (!runtime.rawTargetVisible && runtime.hasObservedTargetPosition)
			{
				// 遮挡后的宽限期只追踪实际观察到的位置，不能读取墙后的实时目标位置。
				const auto* pursuitState = runtime.behavior->FindState(runtime.currentState);
				const auto move = m_NavigationWorld->CaptureAgent(header.owner);
				if (pursuitState && pursuitState->task == VansAITaskKind::MoveToTarget && move &&
					move->request.serial == runtime.moveRequest.serial &&
					move->state == VansNavigationMoveState::Finished &&
					glm::distance(move->goal, targetPosition) < 1.e-4f)
				{
					// 到达最后可见位置或有界导航失败后，结束这次搜索并让行为图决定后续状态。
					runtime.targetVisible = false;
					runtime.hasObservedTargetPosition = false;
				}
			}
			runtime.blackboard.SetBool(
				ai.sight.blackboardKey, runtime.targetVisible, "AI.Perception");
		}
		else
		{
			runtime.targetVisible = targetController != nullptr;
			runtime.timeSinceTargetVisible = 0.0f;
		}

		if (decisionDue)
		{
			VANS_PROFILE_SCOPE("AI::Decision", Vans::ProfileCategory::Script);
			auto findMatchingTransition = [&](const VansAIStateDefinition& state)
				-> const VansAITransitionDefinition*
			{
				for (const VansAITransitionDefinition& transition : state.transitions)
				{
					bool matches = false;
					if (transition.condition.kind == VansAIConditionKind::BlackboardBool)
					{
						matches = runtime.blackboard.GetBool(transition.condition.key) ==
							transition.condition.expectedBool;
					}
					else if (transition.condition.kind == VansAIConditionKind::AnimationState)
					{
						const std::string& expectedState =
							transition.condition.expectedString == "$ready"
							? ai.readyAnimationState
							: transition.condition.expectedString;
						matches = animationNode && animationNode->GetCurrentStateName() ==
							expectedState;
					}
					if (matches) return &transition;
				}
				return nullptr;
			};
			std::uint32_t appliedTransitions = 0;
			for (; appliedTransitions < runtime.behavior->maxTransitionsPerUpdate;
				++appliedTransitions)
			{
				const VansAIStateDefinition* transitionState =
					runtime.behavior->FindState(runtime.currentState);
				if (!transitionState)
				{
					RecordError(runtime, header.owner,
						"AI runtime state does not resolve: " + runtime.currentState);
					break;
				}
				const VansAITransitionDefinition* selected =
					findMatchingTransition(*transitionState);
				if (!selected) break;
				const std::string previous = runtime.currentState;
				runtime.currentState = selected->targetState;
				std::string error;
				if (runtime.moveRequest.IsValid()) m_NavigationWorld->Stop(runtime.moveRequest, runtime.requestOwner, error);
				runtime.moveRequest = {};
				if(runtime.patrolQuery.IsValid()) m_NavigationWorld->ReleaseQuery(runtime.patrolQuery,runtime.requestOwner,error);
                runtime.patrolQuery={};
				runtime.hasPatrolDestination = false;
				runtime.patrolWaitRemaining = 0.0f;
				VANS_LOG("[AI] Entity=" << header.owner.index << " state '"
					<< previous << "' -> '" << runtime.currentState << "'");
			}
			if (appliedTransitions == runtime.behavior->maxTransitionsPerUpdate)
			{
				const VansAIStateDefinition* nextState =
					runtime.behavior->FindState(runtime.currentState);
				if (nextState && findMatchingTransition(*nextState))
				{
					RecordError(runtime, header.owner,
						"AI transition limit exhausted in state: " + runtime.currentState);
				}
			}
		}

		auto abortMove = [&]()
		{
			std::string error;
			if (runtime.moveRequest.IsValid()) m_NavigationWorld->Stop(runtime.moveRequest, runtime.requestOwner, error);
			runtime.moveRequest = {};
            if(runtime.patrolQuery.IsValid()) m_NavigationWorld->ReleaseQuery(runtime.patrolQuery,runtime.requestOwner,error);
            runtime.patrolQuery={};
		};
		auto requestMove = [&](const glm::vec3& goal, float arrivalRadius, std::optional<float> facingYaw)
		{
			VansNavigationMoveOptions options;
			options.arrivalRadius = arrivalRadius;
			options.facingYaw = facingYaw;
			std::string error;
			runtime.moveRequest = m_NavigationWorld->RequestMove(header.owner, runtime.requestOwner, goal, options, error);
			if (!runtime.moveRequest.IsValid()) RecordError(runtime, header.owner, error);
		};
		const VansAIStateDefinition* state = runtime.behavior->FindState(runtime.currentState);
		if (!state || state->task == VansAITaskKind::Hold)
		{
			abortMove();
		}
		else if (state->task == VansAITaskKind::Patrol)
		{
			if (!state->patrol)
			{
				RecordError(runtime, header.owner, "Patrol state is missing taskConfig");
				abortMove();
			}
			else
			{
				const auto& patrol = *state->patrol;
				if (!runtime.hasPatrolAnchor) { runtime.patrolAnchor = agentPosition; runtime.hasPatrolAnchor = true; }
				if (runtime.patrolWaitRemaining > 0.0f)
					runtime.patrolWaitRemaining = (std::max)(0.0f, runtime.patrolWaitRemaining - dt);
				else if (runtime.hasPatrolDestination)
				{
					const auto navigationState = m_NavigationWorld->CaptureAgent(header.owner);
					if (!navigationState || navigationState->state == VansNavigationMoveState::Finished)
					{
						runtime.hasPatrolDestination = false;
						runtime.moveRequest = {};
						runtime.patrolWaitRemaining = patrol.waitSeconds;
					}
				}
				else
				{
					std::string error;
                    if(!runtime.patrolQuery.IsValid())
                        runtime.patrolQuery=m_NavigationWorld->SampleReachablePoint(header.owner,runtime.requestOwner,
                            runtime.patrolAnchor,patrol.radius,runtime.patrolSampleIndex,error);
                    const auto result=runtime.patrolQuery.IsValid()?
                        m_NavigationWorld->CaptureQuery(runtime.patrolQuery,runtime.requestOwner,error):std::nullopt;
                    const bool pending=result && (result->lifecycle==VansNavigationQueryLifecycle::Queued || result->lifecycle==VansNavigationQueryLifecycle::Running ||
                        result->lifecycle==VansNavigationQueryLifecycle::WaitingForData);
                    if(!pending)
                    {
                        if(result) runtime.patrolSampleIndex+=result->attempts;
                        if(runtime.patrolQuery.IsValid()) m_NavigationWorld->ReleaseQuery(runtime.patrolQuery,runtime.requestOwner,error);
                        runtime.patrolQuery={};
                        if(result && result->point)
                        {
                            runtime.patrolDestination=*result->point; runtime.hasPatrolDestination=true;
                            requestMove(*result->point,.45f,std::nullopt);
                        }
                        else
                        {
                            runtime.diagnostic=result?result->path.diagnostic:error;
                            runtime.patrolWaitRemaining=(std::max)(.25f,patrol.waitSeconds);
                        }
                    }
				}
			}
		}
		else if (state->task == VansAITaskKind::MoveToTarget)
		{
			if (!targetController)
			{
				runtime.diagnostic = "No enabled target matched Gameplay Tag '" + ai.targetTag + "'";
				abortMove();
			}
			else
			{
				const auto facing = ResolveAIChaseFacingDirection(agentPosition, targetPosition, glm::vec3(0.0f, 0.0f, 1.0f));
				const float ownerYaw = ResolveOwnerFacingYaw(animationNode, ResolveAIVisualFacingYawDegrees(facing));
				requestMove(targetPosition, state->moveToTarget.arrivalRadius, ownerYaw);
			}
		}
		if (controller) UpdateAnimationMovementParameter(animationNode, ai, *controller);
	}

	for (auto it = m_Agents.begin(); it != m_Agents.end();)
	{
		if (liveAgents.find(it->first) == liveAgents.end())
		{
			std::string error;
			if (it->second.moveRequest.IsValid()) m_NavigationWorld->Stop(it->second.moveRequest, it->second.requestOwner, error);
            if(it->second.patrolQuery.IsValid()) m_NavigationWorld->ReleaseQuery(it->second.patrolQuery,it->second.requestOwner,error);
			it = m_Agents.erase(it);
		}
		else ++it;
	}
}

VansAIDiagnostics VansAIWorld::CaptureDiagnostics() const
{
	VansAIDiagnostics snapshot;
	snapshot.totalAgents = m_Agents.size();
	std::vector<std::pair<std::uint64_t, const AgentRuntime*>> ordered;
	ordered.reserve(m_Agents.size());
	for (const auto& [key, runtime] : m_Agents)
		ordered.emplace_back(key, &runtime);
	std::sort(ordered.begin(), ordered.end(),
		[](const auto& left, const auto& right) { return left.first < right.first; });
	if (ordered.size() > kMaxDiagnosticAgents)
		ordered.resize(kMaxDiagnosticAgents);
	snapshot.truncated = ordered.size() < snapshot.totalAgents;
	snapshot.agents.reserve(ordered.size());
	for (const auto& [key, runtimePointer] : ordered)
	{
		(void)key;
		const AgentRuntime& runtime = *runtimePointer;
		VansAIAgentDiagnostics agent;
		agent.entity = VansEntityHandle{
			static_cast<std::uint32_t>(key & 0xffffffffull),
			static_cast<std::uint32_t>(key >> 32u) };
		if (m_World)
		{
			if (const VansEntityRecord* record = m_World->Entities().Get(agent.entity))
			{
				agent.entityGuid = record->stableGuid;
				agent.entityName = record->name;
			}
		}
		agent.initialized = runtime.initialized;
		agent.behaviorName = runtime.behavior ? runtime.behavior->name : std::string();
		agent.currentState = runtime.currentState;
		agent.target = runtime.target;
		if (m_World && runtime.target.IsValid())
		{
			if (const VansEntityRecord* record = m_World->Entities().Get(runtime.target))
			{
				agent.targetGuid = record->stableGuid;
				agent.targetName = record->name;
			}
		}
		agent.rawTargetVisible = runtime.rawTargetVisible;
		agent.targetVisible = runtime.targetVisible;
		agent.movementBlocked = runtime.movementBlocked;
		if (m_NavigationWorld)
		{
			const auto navigation = m_NavigationWorld->CaptureAgent(agent.entity);
			if (navigation && navigation->owner == runtime.requestOwner)
			{
				agent.commandedSpeed = navigation->desiredSpeed;
				agent.pathStatus = navigation->path.status;
				agent.pathFailure = navigation->path.failure;
				agent.pathDiagnostic = navigation->path.diagnostic;
				agent.waypointCount = navigation->path.points.size();
				agent.lastPathRequestReason = navigation->reason;
			}
		}
		agent.hasPatrolDestination = runtime.hasPatrolDestination;
		agent.patrolDestination = runtime.patrolDestination;
		agent.lineOfSight.tested = runtime.lineOfSightTested;
		agent.lineOfSight.blocked = runtime.lineOfSightBlocked;
		agent.lineOfSight.origin = runtime.lineOfSightOrigin;
		agent.lineOfSight.target = runtime.lineOfSightTarget;
		agent.lineOfSight.hitObject = runtime.lineOfSightHit;
		agent.blackboard = runtime.blackboard.CaptureDebugSnapshot(
			kMaxDiagnosticBlackboardEntries);
		agent.diagnostic = runtime.diagnostic;
		snapshot.agents.push_back(std::move(agent));
	}
	return snapshot;
}
}
