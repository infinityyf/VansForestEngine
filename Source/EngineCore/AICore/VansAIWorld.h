#pragma once

#include "VansAIBlackboard.h"
#include "VansAIBehaviorAsset.h"
#include "VansAIEvents.h"
#include "VansAIRuntimeComponents.h"
#include "../EventCore/VansScopedEventConnections.h"
#include "../NavigationCore/VansNavigationMesh.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Vans
{
class VansGameplayRuntime;
class VansRuntimeWorld;

struct VansAIAgentDebugSnapshot
{
	bool initialized = false;
	std::string currentState;
	VansEntityHandle target;
	bool targetVisible = false;
	VansNavigationPathStatus pathStatus = VansNavigationPathStatus::None;
	std::size_t waypointCount = 0;
	std::size_t waypointIndex = 0;
	bool hasPatrolDestination = false;
	glm::vec3 patrolDestination{ 0.0f };
	std::string diagnostic;
};

class VansAIWorld
{
public:
	VansAIWorld() = default;
	~VansAIWorld();

	bool Initialize(VansRuntimeWorld& world,
		VansGameplayRuntime* gameplayRuntime,
		std::filesystem::path projectRoot,
		std::string& error);
	void Shutdown();
	void Update(double deltaSeconds);
	std::optional<VansAIAgentDebugSnapshot> DebugAgent(VansEntityHandle entity) const;

private:
	struct AgentRuntime
	{
		std::shared_ptr<VansAIBehaviorAsset> behavior;
		std::shared_ptr<VansNavigationMesh> navigationMesh;
		VansAIBlackboard blackboard;
		std::string currentState;
		VansEntityHandle target;
		VansNavigationPath path;
		std::size_t waypointIndex = 0;
		glm::vec3 lastTargetPosition{ 0.0f };
		glm::vec3 patrolAnchor{ 0.0f };
		glm::vec3 patrolDestination{ 0.0f };
		float repathRemaining = 0.0f;
		float commandedSpeed = 0.0f;
		float timeSinceTargetVisible = 0.0f;
		float patrolWaitRemaining = 0.0f;
		std::uint32_t patrolSampleIndex = 0;
		bool hasLastTargetPosition = false;
		bool hasPatrolAnchor = false;
		bool hasPatrolDestination = false;
		bool targetVisible = false;
		bool initialized = false;
		std::string diagnostic;
	};

	static std::uint64_t EntityKey(VansEntityHandle entity);
	std::filesystem::path ResolveProjectPath(const std::string& path) const;
	bool InitializeAgent(AgentRuntime& runtime,
		const VansRuntimeAIAgentComponent& ai,
		const VansRuntimeNavigationAgentComponent& navigation,
		std::string& error);
	std::shared_ptr<VansAIBehaviorAsset> LoadBehavior(
		const std::string& path, std::string& error);
	std::shared_ptr<VansNavigationMesh> LoadNavigationMesh(
		const std::string& path, std::string& error);

	VansRuntimeWorld* m_World = nullptr;
	VansGameplayRuntime* m_GameplayRuntime = nullptr;
	std::filesystem::path m_ProjectRoot;
	VansScopedEventConnections m_Connections;
	std::unordered_map<std::uint64_t, AgentRuntime> m_Agents;
	std::unordered_map<std::string, std::shared_ptr<VansAIBehaviorAsset>> m_BehaviorCache;
	std::unordered_map<std::string, std::shared_ptr<VansNavigationMesh>> m_NavigationCache;
	std::unordered_set<std::uint64_t> m_PendingActivation;
	std::unordered_set<std::uint64_t> m_PendingGameplayRelease;
};
}
