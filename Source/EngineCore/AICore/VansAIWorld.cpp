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
constexpr std::uint64_t kPathScheduleLane = 0x706174685f717565ull;
constexpr std::size_t kMaxDiagnosticAgents = 256u;
constexpr std::size_t kMaxDiagnosticBlackboardEntries = 64u;

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

void ScheduleNextPathQuery(
	float interval,
	std::uint64_t entityKey,
	float& remaining,
	bool& started)
{
	remaining = interval;
	if (!started)
	{
		remaining += StableSchedulePhase(entityKey, kPathScheduleLane, interval);
		started = true;
	}
}

template <typename T>
VansComponentStorage<T>* FindStorage(VansRuntimeWorld& world, std::uint16_t typeId)
{
	return world.FindStorage<T>(typeId);
}

template <typename T>
const T* FindOwnedEnabledComponent(
	VansComponentStorage<T>* storage,
	VansEntityHandle owner)
{
	return storage ? storage->FindFirstEffectiveOwnedBy(owner) : nullptr;
}

glm::vec3 CharacterOrigin(const VansEngine::VansCharacterControllerNode& controller)
{
	return controller.GetPosition() - controller.GetProperties().m_PositionOffset;
}

float PlanarDistance(const glm::vec3& left, const glm::vec3& right)
{
	return glm::length(glm::vec2(left.x - right.x, left.z - right.z));
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

void ApplyHold(
	VansEngine::VansCharacterControllerNode& controller,
	std::optional<float> desiredOwnerFacingYaw = std::nullopt)
{
	VansCharacterMotionIntent intent;
	intent.moveInputLocal = glm::vec2(0.0f);
	intent.movementReferenceYaw = CurrentOwnerFacingYaw(controller);
	intent.desiredSpeed = 0.0f;
	if (desiredOwnerFacingYaw)
	{
		intent.desiredFacingYaw = *desiredOwnerFacingYaw;
		intent.hasFacing = true;
	}
	intent.valid = true;
	controller.SetMotionIntent(intent);
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
	std::string& error)
{
	Shutdown();
	m_World = &world;
	m_GameplayRuntime = gameplayRuntime;
	m_AssetObjects = &assetObjects;
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
	if (InitializeExistingAgents(error))
		return true;
	Shutdown();
	return false;
}

void VansAIWorld::Shutdown()
{
	m_Connections.DisconnectAll();
	m_Agents.clear();
	m_PendingActivation.clear();
	m_PendingGameplayRelease.clear();
	m_World = nullptr;
	m_GameplayRuntime = nullptr;
	m_AssetObjects = nullptr;
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

std::shared_ptr<const VansNavigationMesh> VansAIWorld::ResolveNavigationMesh(
	const std::string& guidText, std::string& error) const
{
	VansAssetGuid guid;
	if (!m_AssetObjects || !VansAssetGuid::TryParse(guidText, guid))
	{
		error = "NavigationAgent navigation mesh must reference a valid asset GUID";
		return nullptr;
	}
	auto asset = m_AssetObjects->ResolveLatest<VansNavigationMesh>(guid);
	if (!asset)
		error = "Navigation Mesh asset is not loaded in the object repository: " + guidText;
	return asset;
}

bool VansAIWorld::InitializeAgent(AgentRuntime& runtime,
	const VansRuntimeAIAgentComponent& ai,
	const VansRuntimeNavigationAgentComponent& navigation,
	std::string& error)
{
	if (!ValidateAIRuntimeTiming(ai, navigation, error)) return false;
	runtime.behavior = ResolveBehavior(ai.behaviorGuid, error);
	if (!runtime.behavior) return false;
	runtime.navigationMesh = ResolveNavigationMesh(navigation.navigationMeshGuid, error);
	if (!runtime.navigationMesh) return false;
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

bool VansAIWorld::InitializeExistingAgents(std::string& error)
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
		if (!navigation || !cct || !cct->controllerNode)
		{
			error = "AIAgent requires enabled NavigationAgent and CharacterController components";
			VANS_LOG_ERROR("[AI] Entity=" << header.owner.index << ' ' << error);
			return false;
		}
		const std::uint64_t key = EntityKey(header.owner);
		if (m_Agents.find(key) != m_Agents.end())
		{
			error = "Entity has more than one enabled AIAgent component";
			VANS_LOG_ERROR("[AI] Entity=" << header.owner.index << ' ' << error);
			return false;
		}
		AgentRuntime runtime;
		if (!InitializeAgent(runtime, agents[index], *navigation, error))
		{
			VANS_LOG_ERROR("[AI] Entity=" << header.owner.index << ' ' << error);
			return false;
		}
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
		if (!navigation || !cct || !cct->controllerNode)
		{
			RecordError(runtime, header.owner,
				"AIAgent requires enabled NavigationAgent and CharacterController components");
			continue;
		}
		if (!runtime.initialized)
		{
			std::string error;
			if (!InitializeAgent(runtime, ai, *navigation, error))
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
		runtime.repathRemaining = (std::max)(
			-navigation->repathInterval,
			runtime.repathRemaining - dt);

		VansGraphics::VansAnimationNode* animationNode =
			animation ? animation->animationNode : nullptr;
		VansEngine::VansCharacterControllerNode& controller = *cct->controllerNode;
		runtime.movementBlocked = controller.IsGameplayMovementBlocked();
		if (runtime.movementBlocked)
			runtime.commandedSpeed = 0.0f;
		const glm::vec3 agentPosition = CharacterOrigin(controller);

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
				runtime.path = {};
				runtime.waypointIndex = 0;
				runtime.hasLastTargetPosition = false;
				runtime.repathRemaining = 0.0f;
				runtime.repathStarted = false;
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
					animationNode, CurrentOwnerFacingYaw(controller));
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
				runtime.path = {};
				runtime.waypointIndex = 0;
				runtime.repathRemaining = 0.0f;
				runtime.repathStarted = false;
				runtime.commandedSpeed = 0.0f;
				runtime.hasLastTargetPosition = false;
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

		const VansAIStateDefinition* state = runtime.behavior->FindState(runtime.currentState);
		if (!state || state->task == VansAITaskKind::Hold)
		{
			runtime.commandedSpeed = 0.0f;
			ApplyHold(controller);
			UpdateAnimationMovementParameter(animationNode, ai, controller);
			continue;
		}

		auto ownerYawForDirection = [&](const glm::vec3& direction)
		{
			const float visualYaw = ResolveAIVisualFacingYawDegrees(direction);
			return ResolveOwnerFacingYaw(animationNode, visualYaw);
		};
		auto holdFacing = [&](const glm::vec3& direction)
		{
			runtime.commandedSpeed = 0.0f;
			ApplyHold(controller, ownerYawForDirection(direction));
		};
		auto drive = [&](const glm::vec3& routeDirection,
			const glm::vec3& visualFacingDirection)
		{
			if (runtime.movementBlocked)
			{
				runtime.commandedSpeed = 0.0f;
				ApplyHold(controller, ownerYawForDirection(visualFacingDirection));
				return;
			}
			runtime.commandedSpeed = (std::min)(navigation->maxSpeed,
				runtime.commandedSpeed + navigation->acceleration * dt);
			const float movementYaw = glm::degrees(std::atan2(
				routeDirection.x, routeDirection.z));
			VansCharacterMotionIntent intent;
			intent.moveInputLocal = glm::vec2(0.0f, 1.0f);
			intent.movementReferenceYaw = movementYaw;
			intent.desiredSpeed = runtime.commandedSpeed;
			intent.desiredFacingYaw = ownerYawForDirection(visualFacingDirection);
			intent.hasFacing = true;
			intent.valid = true;
			controller.SetMotionIntent(intent);
		};
		auto routeDirection = [&]() -> std::optional<glm::vec3>
		{
			if (runtime.path.status != VansNavigationPathStatus::Complete ||
				runtime.path.points.empty()) return std::nullopt;
			while (runtime.waypointIndex + 1u < runtime.path.points.size() &&
				PlanarDistance(agentPosition,
					runtime.path.points[runtime.waypointIndex]) <= 0.35f)
			{
				++runtime.waypointIndex;
			}
			const glm::vec3 waypoint = runtime.path.points[
				(std::min)(runtime.waypointIndex, runtime.path.points.size() - 1u)];
			glm::vec3 direction(
				waypoint.x - agentPosition.x,
				0.0f,
				waypoint.z - agentPosition.z);
			const float length = glm::length(direction);
			if (!std::isfinite(length) || length <= 1.0e-4f)
				return std::nullopt;
			return direction / length;
		};
		auto findPath = [&](const glm::vec3& destination)
		{
			VANS_PROFILE_SCOPE("AI::Path", Vans::ProfileCategory::Script);
			return runtime.navigationMesh->FindPath(agentPosition, destination);
		};

		if (state->task == VansAITaskKind::Patrol)
		{
			if (!state->patrol)
			{
				RecordError(runtime, header.owner, "Patrol state is missing taskConfig");
				runtime.commandedSpeed = 0.0f;
				ApplyHold(controller);
				UpdateAnimationMovementParameter(animationNode, ai, controller);
				continue;
			}
			const VansAIPatrolTaskConfig& patrol = *state->patrol;
			if (!runtime.hasPatrolAnchor)
			{
				runtime.patrolAnchor = agentPosition;
				runtime.hasPatrolAnchor = true;
			}
			if (runtime.patrolWaitRemaining > 0.0f)
			{
				runtime.patrolWaitRemaining = (std::max)(
					0.0f, runtime.patrolWaitRemaining - dt);
				runtime.commandedSpeed = 0.0f;
				ApplyHold(controller);
			}
			else
			{
				if (runtime.hasPatrolDestination &&
					PlanarDistance(agentPosition, runtime.patrolDestination) <= 0.45f)
				{
					runtime.hasPatrolDestination = false;
					runtime.path = {};
					runtime.commandedSpeed = 0.0f;
					runtime.patrolWaitRemaining = patrol.waitSeconds;
					ApplyHold(controller);
				}
				else
				{
					if (!runtime.hasPatrolDestination && patrol.radius > 0.0f &&
						runtime.repathRemaining <= 0.0f)
					{
						runtime.lastPathRequestReason = VansAIPathRequestReason::Patrol;
						constexpr float goldenAngle = 2.39996323f;
						for (int attempt = 0; attempt < 12; ++attempt)
						{
							const std::uint32_t sample = runtime.patrolSampleIndex++;
							const float radiusScale = 0.55f + 0.45f *
								static_cast<float>(sample % 5u) / 4.0f;
							const float angle = goldenAngle * static_cast<float>(sample);
							const glm::vec3 candidate = runtime.patrolAnchor + glm::vec3(
								std::sin(angle) * patrol.radius * radiusScale,
								0.0f,
								std::cos(angle) * patrol.radius * radiusScale);
							VansNavigationPath candidatePath = findPath(candidate);
							if (candidatePath.status != VansNavigationPathStatus::Complete ||
								candidatePath.points.size() < 2u) continue;
							runtime.path = std::move(candidatePath);
							runtime.waypointIndex = 1u;
							runtime.patrolDestination = runtime.path.points.back();
							runtime.hasPatrolDestination = true;
							runtime.diagnostic = runtime.path.diagnostic;
							break;
						}
						ScheduleNextPathQuery(
							navigation->repathInterval,
							key,
							runtime.repathRemaining,
							runtime.repathStarted);
					}

					if (runtime.hasPatrolDestination)
					{
						const std::optional<glm::vec3> direction = routeDirection();
						if (direction) drive(*direction, *direction);
						else
						{
							runtime.hasPatrolDestination = false;
							runtime.commandedSpeed = 0.0f;
							ApplyHold(controller);
						}
					}
					else
					{
						runtime.commandedSpeed = 0.0f;
						runtime.patrolWaitRemaining = (std::max)(
							0.25f, patrol.waitSeconds);
						ApplyHold(controller);
					}
				}
			}
		}
		else if (state->task == VansAITaskKind::MoveToTarget)
		{
			if (!targetController)
			{
				runtime.diagnostic = "No enabled target matched Gameplay Tag '" +
					ai.targetTag + "'";
				runtime.commandedSpeed = 0.0f;
				ApplyHold(controller);
			}
			else
			{
				const glm::vec3 visualFacing = ResolveAIChaseFacingDirection(
					agentPosition, targetPosition, glm::vec3(0.0f, 0.0f, 1.0f));
				const float targetMoveDistance = runtime.hasLastTargetPosition
					? PlanarDistance(targetPosition, runtime.lastTargetPosition)
					: 0.0f;
				const bool movedEnough = runtime.hasLastTargetPosition &&
					targetMoveDistance >= navigation->targetMoveThreshold;
				const bool forceRepath = runtime.hasLastTargetPosition &&
					targetMoveDistance >= navigation->forceRepathDistance;
				const bool pathDue = runtime.repathRemaining <= 0.0f;
				const bool shouldRequestPath = !runtime.hasLastTargetPosition ||
					forceRepath ||
					(pathDue && (runtime.path.status !=
						VansNavigationPathStatus::Complete || movedEnough));
				if (shouldRequestPath)
				{
					if (!runtime.hasLastTargetPosition)
						runtime.lastPathRequestReason = VansAIPathRequestReason::InitialTarget;
					else if (forceRepath)
						runtime.lastPathRequestReason = VansAIPathRequestReason::ForceTargetMoved;
					else if (runtime.path.status != VansNavigationPathStatus::Complete)
						runtime.lastPathRequestReason = VansAIPathRequestReason::PathRecovery;
					else
						runtime.lastPathRequestReason = VansAIPathRequestReason::TargetMoved;
					const VansNavigationPathStatus previousStatus = runtime.path.status;
					runtime.path = findPath(targetPosition);
					runtime.waypointIndex = runtime.path.points.size() > 1u ? 1u : 0u;
					runtime.lastTargetPosition = targetPosition;
					runtime.hasLastTargetPosition = true;
					ScheduleNextPathQuery(
						navigation->repathInterval,
						key,
						runtime.repathRemaining,
						runtime.repathStarted);
					runtime.diagnostic = runtime.path.diagnostic;
					if (runtime.path.status != previousStatus ||
						runtime.path.status != VansNavigationPathStatus::Complete)
					{
						VANS_LOG("[AI] Entity=" << header.owner.index << " path='"
							<< runtime.path.diagnostic << "' corners="
							<< runtime.path.points.size());
					}
				}

				if (PlanarDistance(agentPosition, targetPosition) <=
					navigation->stoppingDistance)
				{
					holdFacing(visualFacing);
				}
				else if (const std::optional<glm::vec3> direction = routeDirection())
				{
					// 移动沿导航路径，正面每帧直接跟踪目标，二者不能共用同一个 yaw。
					drive(*direction, ResolveAIChaseFacingDirection(
						agentPosition, targetPosition, *direction));
				}
				else
				{
					holdFacing(visualFacing);
				}
			}
		}
		UpdateAnimationMovementParameter(animationNode, ai, controller);
	}

	for (auto it = m_Agents.begin(); it != m_Agents.end();)
	{
		if (liveAgents.find(it->first) == liveAgents.end()) it = m_Agents.erase(it);
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
		agent.commandedSpeed = runtime.commandedSpeed;
		agent.pathStatus = runtime.path.status;
		agent.pathFailure = runtime.path.failure;
		agent.pathDiagnostic = runtime.path.diagnostic;
		agent.lastPathRequestReason = runtime.lastPathRequestReason;
		agent.waypointCount = runtime.path.points.size();
		agent.waypointIndex = runtime.waypointIndex;
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
