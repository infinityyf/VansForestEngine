#pragma once

#include "VansAIBlackboard.h"
#include "VansAIBehaviorAsset.h"
#include "VansAIDiagnostics.h"
#include "VansAIEvents.h"
#include "VansAIRuntimeComponents.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../EventCore/VansScopedEventConnections.h"
#include "../NavigationCore/VansNavigationMesh.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Vans
{
class VansGameplayRuntime;
class VansRuntimeWorld;

class VansAIWorld
{
public:
	VansAIWorld() = default;
	~VansAIWorld();

	bool Initialize(VansRuntimeWorld& world,
		VansGameplayRuntime* gameplayRuntime,
		const VansAssetObjectRepository& assetObjects,
		std::string& error);
	void Shutdown();
	void Update(double deltaSeconds);
	VansAIDiagnostics CaptureDiagnostics() const;

private:
	struct AgentRuntime
	{
		std::shared_ptr<const VansAIBehaviorAsset> behavior;
		std::shared_ptr<const VansNavigationMesh> navigationMesh;
		VansAIBlackboard blackboard;
		std::string currentState;
		VansEntityHandle target;
		VansNavigationPath path;
		std::size_t waypointIndex = 0;
		glm::vec3 lastTargetPosition{ 0.0f };
		glm::vec3 patrolAnchor{ 0.0f };
		glm::vec3 patrolDestination{ 0.0f };
		float perceptionRemaining = 0.0f;
		float decisionRemaining = 0.0f;
		float repathRemaining = 0.0f;
		float commandedSpeed = 0.0f;
		float timeSinceTargetVisible = 0.0f;
		float patrolWaitRemaining = 0.0f;
		std::uint32_t patrolSampleIndex = 0;
		bool hasLastTargetPosition = false;
		bool hasPatrolAnchor = false;
		bool hasPatrolDestination = false;
		bool perceptionStarted = false;
		bool decisionStarted = false;
		bool repathStarted = false;
		bool rawTargetVisible = false;
		bool targetVisible = false;
		bool movementBlocked = false;
		bool lineOfSightTested = false;
		bool lineOfSightBlocked = false;
		bool deltaClampReported = false;
		bool initialized = false;
		glm::vec3 lineOfSightOrigin{ 0.0f };
		glm::vec3 lineOfSightTarget{ 0.0f };
		std::string lineOfSightHit;
		VansAIPathRequestReason lastPathRequestReason = VansAIPathRequestReason::None;
		std::string diagnostic;
		std::string lastLoggedError;
	};

	static std::uint64_t EntityKey(VansEntityHandle entity);
	static void RecordError(AgentRuntime& runtime,
		VansEntityHandle entity, const std::string& message);
	bool InitializeAgent(AgentRuntime& runtime,
		const VansRuntimeAIAgentComponent& ai,
		const VansRuntimeNavigationAgentComponent& navigation,
		std::string& error);
	bool InitializeExistingAgents(std::string& error);
	std::shared_ptr<const VansAIBehaviorAsset> ResolveBehavior(
		const std::string& guid, std::string& error) const;
	std::shared_ptr<const VansNavigationMesh> ResolveNavigationMesh(
		const std::string& guid, std::string& error) const;

	VansRuntimeWorld* m_World = nullptr;
	VansGameplayRuntime* m_GameplayRuntime = nullptr;
	const VansAssetObjectRepository* m_AssetObjects = nullptr;
	VansScopedEventConnections m_Connections;
	std::unordered_map<std::uint64_t, AgentRuntime> m_Agents;
	std::unordered_map<std::uint64_t, std::string> m_PendingActivation;
	std::unordered_map<std::uint64_t, std::string> m_PendingGameplayRelease;
};
}
