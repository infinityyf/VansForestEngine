#include "VansAIWorld.h"
#include "VansAIPerception.h"

#include "../AnimationCore/VansAnimationController.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../EventCore/VansEventBus.h"
#include "../GameplayActionCore/VansActionHost.h"
#include "../GameplayActionCore/VansGameplayRuntime.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../ScriptCore/VansTransform.h"
#include "../SceneRuntime/VansComponentStorage.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../SceneRuntime/VansRuntimeWorld.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <../../GLM/gtc/quaternion.hpp>

namespace Vans
{
namespace
{
template <typename T>
VansComponentStorage<T>* FindStorage(VansRuntimeWorld& world, std::uint16_t typeId)
{
	IVansComponentStorage* storage = world.FindStorage(typeId);
	return storage ? static_cast<VansComponentStorage<T>*>(storage) : nullptr;
}

template <typename T>
const T* FindOwnedEnabledComponent(
	VansComponentStorage<T>* storage,
	VansEntityHandle owner)
{
	if (!storage) return nullptr;
	const auto& headers = storage->Headers();
	const auto& data = storage->DenseData();
	for (std::size_t index = 0; index < headers.size(); ++index)
		if (headers[index].owner == owner && headers[index].effectiveEnabled)
			return &data[index];
	return nullptr;
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
		: VansGraphics::VansTransformStore::GetTransform(transformId).m_Rotation.y;
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

class AIVisionSceneQueryFilter final : public physx::PxQueryFilterCallback
{
public:
	explicit AIVisionSceneQueryFilter(std::uint32_t occlusionLayer)
		: m_OcclusionLayer(occlusionLayer) {}

	physx::PxQueryHitType::Enum preFilter(
		const physx::PxFilterData&,
		const physx::PxShape* shape,
		const physx::PxRigidActor*,
		physx::PxHitFlags&) override
	{
		return Filter(shape);
	}

	physx::PxQueryHitType::Enum postFilter(
		const physx::PxFilterData&,
		const physx::PxQueryHit&,
		const physx::PxShape* shape,
		const physx::PxRigidActor*) override
	{
		return Filter(shape);
	}

private:
	physx::PxQueryHitType::Enum Filter(const physx::PxShape* shape) const
	{
		if (!shape) return physx::PxQueryHitType::eNONE;
		const physx::PxFilterData target = shape->getQueryFilterData();
		if ((target.word2 & 0x1u) != 0u || target.word0 != m_OcclusionLayer)
			return physx::PxQueryHitType::eNONE;
		return physx::PxQueryHitType::eBLOCK;
	}

	std::uint32_t m_OcclusionLayer = 0;
};

bool HasSceneLineOfSight(
	const glm::vec3& observer,
	const glm::vec3& target,
	const std::string& occlusionLayer)
{
	if (occlusionLayer.empty()) return true;
	int layerIndex = -1;
	if (!VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(
		occlusionLayer, layerIndex) || layerIndex < 0)
	{
		return true;
	}

	glm::vec3 direction = target - observer;
	const float distance = glm::length(direction);
	if (!std::isfinite(distance) || distance <= 0.05f) return true;
	direction /= distance;

	VansEngine::VansPhysicsSystem& physics =
		VansEngine::VansPhysicsSystem::GetInstance();
	physx::PxScene* scene = physics.GetScene();
	if (!scene) return true;
	std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
	physx::PxSceneReadLock sceneReadLock(*scene);
	physx::PxQueryFilterData filterData;
	filterData.flags = physx::PxQueryFlag::eSTATIC |
		physx::PxQueryFlag::eDYNAMIC |
		physx::PxQueryFlag::ePREFILTER;
	AIVisionSceneQueryFilter filter(static_cast<std::uint32_t>(layerIndex));
	physx::PxRaycastBuffer hit;
	return !(scene->raycast(
		physx::PxVec3(observer.x, observer.y, observer.z),
		physx::PxVec3(direction.x, direction.y, direction.z),
		distance - 0.05f,
		hit,
		physx::PxHitFlag::eDEFAULT,
		filterData,
		&filter) && hit.hasBlock);
}

void UpdateAnimationMovementParameter(
	VansGraphics::VansAnimationNode* animationNode,
	const VansRuntimeAIAgentComponent& config,
	const VansEngine::VansCharacterControllerNode& controller)
{
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

bool VansAIWorld::Initialize(VansRuntimeWorld& world,
	VansGameplayRuntime* gameplayRuntime,
	std::filesystem::path projectRoot,
	std::string& error)
{
	Shutdown();
	m_World = &world;
	m_GameplayRuntime = gameplayRuntime;
	m_ProjectRoot = std::filesystem::absolute(std::move(projectRoot)).lexically_normal();
	m_Connections.Add(VansEventBus::Get().Subscribe<VansAIActivationRequested>(
		[this](const VansAIActivationRequested& event)
		{
			if (!event.target.IsValid()) return;
			const std::uint64_t key = EntityKey(event.target);
			if (auto found = m_Agents.find(key); found != m_Agents.end() && found->second.initialized)
				found->second.blackboard.SetBool("ActivationRequested", true);
			else
				m_PendingActivation.insert(key);
		}, VansEventLane::GameLogic, 0, "AIActivationRequested"));
	m_Connections.Add(VansEventBus::Get().Subscribe<VansAIGameplayReleased>(
		[this](const VansAIGameplayReleased& event)
		{
			if (!event.target.IsValid()) return;
			const std::uint64_t key = EntityKey(event.target);
			if (auto found = m_Agents.find(key); found != m_Agents.end() && found->second.initialized)
				found->second.blackboard.SetBool("GameplayReleased", true);
			else
				m_PendingGameplayRelease.insert(key);
		}, VansEventLane::GameLogic, 0, "AIGameplayReleased"));
	error.clear();
	return true;
}

void VansAIWorld::Shutdown()
{
	m_Connections.DisconnectAll();
	m_Agents.clear();
	m_BehaviorCache.clear();
	m_NavigationCache.clear();
	m_PendingActivation.clear();
	m_PendingGameplayRelease.clear();
	m_World = nullptr;
	m_GameplayRuntime = nullptr;
	m_ProjectRoot.clear();
}

std::filesystem::path VansAIWorld::ResolveProjectPath(const std::string& path) const
{
	if (path.empty()) return {};
	std::filesystem::path value(path);
	if (value.is_absolute() && value.has_root_name())
		return value.lexically_normal();
	std::string relative = value.generic_string();
	while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\'))
		relative.erase(relative.begin());
	return (m_ProjectRoot / std::filesystem::path(relative)).lexically_normal();
}

std::shared_ptr<VansAIBehaviorAsset> VansAIWorld::LoadBehavior(
	const std::string& path, std::string& error)
{
	const std::filesystem::path resolved = ResolveProjectPath(path);
	const std::string key = resolved.generic_string();
	if (const auto found = m_BehaviorCache.find(key); found != m_BehaviorCache.end())
		return found->second;
	auto asset = std::make_shared<VansAIBehaviorAsset>();
	if (!VansAIBehaviorAssetStorage::Load(resolved, *asset, error)) return nullptr;
	m_BehaviorCache.emplace(key, asset);
	return asset;
}

std::shared_ptr<VansNavigationMesh> VansAIWorld::LoadNavigationMesh(
	const std::string& path, std::string& error)
{
	const std::filesystem::path resolved = ResolveProjectPath(path);
	const std::string key = resolved.generic_string();
	if (const auto found = m_NavigationCache.find(key); found != m_NavigationCache.end())
		return found->second;
	auto asset = std::make_shared<VansNavigationMesh>();
	if (!asset->Load(resolved, error)) return nullptr;
	m_NavigationCache.emplace(key, asset);
	return asset;
}

bool VansAIWorld::InitializeAgent(AgentRuntime& runtime,
	const VansRuntimeAIAgentComponent& ai,
	const VansRuntimeNavigationAgentComponent& navigation,
	std::string& error)
{
	runtime.behavior = LoadBehavior(ai.behaviorPath, error);
	if (!runtime.behavior) return false;
	runtime.navigationMesh = LoadNavigationMesh(navigation.navigationMeshPath, error);
	if (!runtime.navigationMesh) return false;
	if (!runtime.blackboard.Configure(runtime.behavior->blackboard, error)) return false;
	if (ai.sight.enabled)
	{
		if (ai.sight.blackboardKey.empty())
		{
			error = "AIAgent sight requires a Blackboard key";
			return false;
		}
		if (!runtime.blackboard.SetBool(ai.sight.blackboardKey, false, &error))
			return false;
	}
	runtime.currentState = runtime.behavior->initialState;
	runtime.initialized = true;
	return true;
}

void VansAIWorld::Update(double deltaSeconds)
{
	if (!m_World) return;
	const float dt = static_cast<float>(std::clamp(deltaSeconds, 0.0, 0.25));
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
			runtime.diagnostic = "AIAgent requires enabled NavigationAgent and CharacterController components";
			continue;
		}
		if (!runtime.initialized)
		{
			std::string error;
			if (!InitializeAgent(runtime, ai, *navigation, error))
			{
				runtime.diagnostic = error;
				continue;
			}
			if (m_PendingActivation.erase(key) > 0u)
				runtime.blackboard.SetBool("ActivationRequested", true);
			if (m_PendingGameplayRelease.erase(key) > 0u)
				runtime.blackboard.SetBool("GameplayReleased", true);
			VANS_LOG("[AI] Initialized entity=" << header.owner.index
				<< " behavior='" << runtime.behavior->name
				<< "' state='" << runtime.currentState << "'");
		}

		VansGraphics::VansAnimationNode* animationNode =
			animation ? animation->animationNode : nullptr;
		VansEngine::VansCharacterControllerNode& controller = *cct->controllerNode;
		const glm::vec3 agentPosition = CharacterOrigin(controller);

		VansEntityHandle target;
		VansEngine::VansCharacterControllerNode* targetController = nullptr;
		std::size_t taggedTargetCount = 0;
		std::size_t enabledTargetCount = 0;
		if (m_GameplayRuntime && m_GameplayRuntime->IsInitialized())
		{
			const VansGameplayTagDefinition* targetTag =
				m_GameplayRuntime->Assets().Tags().Find(ai.targetTag);
			if (targetTag)
			{
				for (const std::shared_ptr<VansActionHost>& host : m_GameplayRuntime->Hosts())
				{
					if (!host || !host->Tags().Has(targetTag->id)) continue;
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
		}
		runtime.target = target;
		runtime.blackboard.SetEntity("Target", target);

		bool rawTargetVisible = targetController != nullptr;
		glm::vec3 targetPosition(0.0f);
		if (targetController)
			targetPosition = CharacterOrigin(*targetController);
		if (ai.sight.enabled)
		{
			rawTargetVisible = false;
			if (targetController)
			{
				const glm::vec3 observerEye = agentPosition +
					glm::vec3(0.0f, ai.sight.eyeHeight, 0.0f);
				const glm::vec3 targetCenter = targetController->GetPosition();
				const glm::vec3 visualForward = ResolveModelVisualForward(
					animationNode, CurrentOwnerFacingYaw(controller));
				rawTargetVisible = IsTargetInsideAIVisionCone(
					observerEye,
					visualForward,
					targetCenter,
					ai.sight.range,
					ai.sight.horizontalFovDegrees) &&
					HasSceneLineOfSight(
						observerEye, targetCenter, ai.sight.occlusionLayer);
			}

			if (rawTargetVisible)
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
				ai.sight.blackboardKey, runtime.targetVisible);
		}
		else
		{
			runtime.targetVisible = rawTargetVisible;
			runtime.timeSinceTargetVisible = 0.0f;
		}

		for (int transitionBudget = 0; transitionBudget < 4; ++transitionBudget)
		{
			const VansAIStateDefinition* state = runtime.behavior->FindState(runtime.currentState);
			if (!state)
			{
				runtime.diagnostic = "AI runtime state does not resolve: " + runtime.currentState;
				break;
			}
			const VansAITransitionDefinition* selected = nullptr;
			for (const VansAITransitionDefinition& transition : state->transitions)
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
				if (matches) { selected = &transition; break; }
			}
			if (!selected) break;
			const std::string previous = runtime.currentState;
			runtime.currentState = selected->targetState;
			runtime.path = {};
			runtime.waypointIndex = 0;
			runtime.repathRemaining = 0.0f;
			runtime.commandedSpeed = 0.0f;
			runtime.hasLastTargetPosition = false;
			runtime.hasPatrolDestination = false;
			runtime.patrolWaitRemaining = 0.0f;
			VANS_LOG("[AI] Entity=" << header.owner.index << " state '"
				<< previous << "' -> '" << runtime.currentState << "'");
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

		if (state->task == VansAITaskKind::Patrol)
		{
			if (!state->patrol)
			{
				runtime.diagnostic = "Patrol state is missing taskConfig";
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
					if (!runtime.hasPatrolDestination && patrol.radius > 0.0f)
					{
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
							VansNavigationPath candidatePath =
								runtime.navigationMesh->FindPath(agentPosition, candidate);
							if (candidatePath.status != VansNavigationPathStatus::Complete ||
								candidatePath.points.size() < 2u) continue;
							runtime.path = std::move(candidatePath);
							runtime.waypointIndex = 1u;
							runtime.patrolDestination = runtime.path.points.back();
							runtime.hasPatrolDestination = true;
							runtime.diagnostic = runtime.path.diagnostic;
							break;
						}
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
				const bool movedEnough = !runtime.hasLastTargetPosition ||
					PlanarDistance(targetPosition, runtime.lastTargetPosition) >=
						navigation->targetMoveThreshold;
				runtime.repathRemaining -= dt;
				if (runtime.path.status != VansNavigationPathStatus::Complete ||
					runtime.repathRemaining <= 0.0f || movedEnough)
				{
					const VansNavigationPathStatus previousStatus = runtime.path.status;
					runtime.path = runtime.navigationMesh->FindPath(
						agentPosition, targetPosition);
					runtime.waypointIndex = runtime.path.points.size() > 1u ? 1u : 0u;
					runtime.lastTargetPosition = targetPosition;
					runtime.hasLastTargetPosition = true;
					runtime.repathRemaining = navigation->repathInterval;
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

std::optional<VansAIAgentDebugSnapshot> VansAIWorld::DebugAgent(VansEntityHandle entity) const
{
	const auto found = m_Agents.find(EntityKey(entity));
	if (found == m_Agents.end()) return std::nullopt;
	VansAIAgentDebugSnapshot snapshot;
	snapshot.initialized = found->second.initialized;
	snapshot.currentState = found->second.currentState;
	snapshot.target = found->second.target;
	snapshot.targetVisible = found->second.targetVisible;
	snapshot.pathStatus = found->second.path.status;
	snapshot.waypointCount = found->second.path.points.size();
	snapshot.waypointIndex = found->second.waypointIndex;
	snapshot.hasPatrolDestination = found->second.hasPatrolDestination;
	snapshot.patrolDestination = found->second.patrolDestination;
	snapshot.diagnostic = found->second.diagnostic;
	return snapshot;
}
}
