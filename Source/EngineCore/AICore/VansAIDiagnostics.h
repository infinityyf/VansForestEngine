#pragma once

#include "VansAIBlackboard.h"
#include "../NavigationCore/VansNavigationTypes.h"
#include "../SceneRuntime/VansRuntimeHandle.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Vans
{
enum class VansAIPathRequestReason
{
	None,
	InitialTarget,
	TargetMoved,
	ForceTargetMoved,
	PathRecovery,
	Patrol
};

struct VansAILineOfSightDiagnostics
{
	bool tested = false;
	bool blocked = false;
	glm::vec3 origin{ 0.0f };
	glm::vec3 target{ 0.0f };
	std::string hitObject;
};

struct VansAIAgentDiagnostics
{
	VansEntityHandle entity;
	std::string entityGuid;
	std::string entityName;
	bool initialized = false;
	std::string behaviorName;
	std::string currentState;
	VansEntityHandle target;
	std::string targetGuid;
	std::string targetName;
	bool rawTargetVisible = false;
	bool targetVisible = false;
	bool movementBlocked = false;
	float commandedSpeed = 0.0f;
	VansNavigationPathStatus pathStatus = VansNavigationPathStatus::None;
	VansNavigationPathFailure pathFailure = VansNavigationPathFailure::None;
	std::string pathDiagnostic;
	VansAIPathRequestReason lastPathRequestReason = VansAIPathRequestReason::None;
	std::size_t waypointCount = 0;
	std::size_t waypointIndex = 0;
	bool hasPatrolDestination = false;
	glm::vec3 patrolDestination{ 0.0f };
	VansAILineOfSightDiagnostics lineOfSight;
	VansAIBlackboardDebugSnapshot blackboard;
	std::string diagnostic;
};

struct VansAIDiagnostics
{
	std::size_t totalAgents = 0;
	bool truncated = false;
	std::vector<VansAIAgentDiagnostics> agents;
};
}
