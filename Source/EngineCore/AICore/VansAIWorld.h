#pragma once

#include "VansAIBlackboard.h"
#include "VansAIBehaviorAsset.h"
#include "VansAIDiagnostics.h"
#include "VansAIEvents.h"
#include "VansAIRuntimeComponents.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../EventCore/VansScopedEventConnections.h"
#include "../NavigationCore/VansNavigationWorld.h"
#include "../NavigationCore/VansNavigationRuntimeComponents.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Vans
{
class VansGameplayRuntime;
class VansRuntimeWorld;
enum class VansAIInitializationPolicy { RequireReadyNavigation, Authoring };

class VansAIWorld
{
public:
	VansAIWorld() = default;
	~VansAIWorld();

	bool Initialize(VansRuntimeWorld& world,
		VansGameplayRuntime* gameplayRuntime,
		const VansAssetObjectRepository& assetObjects,
		VansNavigationWorld& navigationWorld,
		std::string& error, VansAIInitializationPolicy policy=VansAIInitializationPolicy::RequireReadyNavigation);
	void Shutdown();
	void Update(double deltaSeconds);
	VansAIDiagnostics CaptureDiagnostics() const;

private:
	struct AgentRuntime
	{
		std::shared_ptr<const VansAIBehaviorAsset> behavior;
		VansAIBlackboard blackboard;
		VansNavigationRequestHandle moveRequest;
        VansNavigationQueryHandle patrolQuery;
		std::string requestOwner;
		std::string currentState;
		VansEntityHandle target;
		glm::vec3 patrolAnchor{ 0.0f };
		glm::vec3 patrolDestination{ 0.0f };
		float perceptionRemaining = 0.0f;
		float decisionRemaining = 0.0f;
		float timeSinceTargetVisible = 0.0f;
		float patrolWaitRemaining = 0.0f;
		std::uint32_t patrolSampleIndex = 0;
		bool hasPatrolAnchor = false;
		bool hasPatrolDestination = false;
		bool perceptionStarted = false;
		bool decisionStarted = false;
		bool rawTargetVisible = false;
		bool targetVisible = false;
		bool hasObservedTargetPosition = false;
		glm::vec3 observedTargetPosition{ 0.0f };
		bool movementBlocked = false;
		bool lineOfSightTested = false;
		bool lineOfSightBlocked = false;
		bool deltaClampReported = false;
		bool initialized = false;
		glm::vec3 lineOfSightOrigin{ 0.0f };
		glm::vec3 lineOfSightTarget{ 0.0f };
		std::string lineOfSightHit;
		std::string diagnostic;
		std::string lastLoggedError;
	};

	static std::uint64_t EntityKey(VansEntityHandle entity);
	static void RecordError(AgentRuntime& runtime,
		VansEntityHandle entity, const std::string& message);
	bool InitializeAgent(AgentRuntime& runtime,
		const VansRuntimeAIAgentComponent& ai,
		std::string& error);
	bool InitializeExistingAgents(std::string& error,VansAIInitializationPolicy policy);
	std::shared_ptr<const VansAIBehaviorAsset> ResolveBehavior(
		const std::string& guid, std::string& error) const;


	VansRuntimeWorld* m_World = nullptr;
	VansGameplayRuntime* m_GameplayRuntime = nullptr;
	const VansAssetObjectRepository* m_AssetObjects = nullptr;
	VansNavigationWorld* m_NavigationWorld = nullptr;
	VansScopedEventConnections m_Connections;
	std::unordered_map<std::uint64_t, AgentRuntime> m_Agents;
	std::unordered_map<std::uint64_t, std::string> m_PendingActivation;
	std::unordered_map<std::uint64_t, std::string> m_PendingGameplayRelease;
};
}
