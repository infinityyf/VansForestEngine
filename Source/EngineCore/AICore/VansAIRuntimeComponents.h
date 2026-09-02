#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace Vans
{
struct VansRuntimeNavigationAgentComponent
{
	std::string navigationMeshGuid;
	std::string navigationMeshPath;
	float maxSpeed = 3.2f;
	float acceleration = 12.0f;
	float stoppingDistance = 1.6f;
	float repathInterval = 0.25f;
	float targetMoveThreshold = 0.75f;
};

struct VansRuntimeAISightConfig
{
	bool enabled = false;
	std::string blackboardKey = "HasVisualTarget";
	float range = 12.0f;
	float horizontalFovDegrees = 120.0f;
	float eyeHeight = 1.5f;
	float loseTargetGraceSeconds = 0.25f;
	std::string occlusionLayer = "Environment";
};

struct VansRuntimeAIFacingConfig
{
	// 仅对显式配置的角色，从场景四元数提取纯 Yaw 作为初始朝向。
	// 默认保留完整 Euler 投影，避免覆盖模型坐标系所需的 X/Z 校正。
	bool yawOnly = false;
};

struct VansRuntimeAIAgentComponent
{
	std::string behaviorGuid;
	std::string behaviorPath;
	std::string targetTag = "Target.Character.Player";
	std::string readyAnimationState = "Idle1";
	std::string movementParameter = "MoveState";
	float idleSpeedThreshold = 0.10f;
	float runSpeedThreshold = 2.20f;
	int maxMovementState = 2;
	VansRuntimeAIFacingConfig facing;
	VansRuntimeAISightConfig sight;
};

inline int ResolveAIMovementState(
	float planarSpeed,
	const VansRuntimeAIAgentComponent& config)
{
	const float speed = std::isfinite(planarSpeed) ? std::max(0.0f, planarSpeed) : 0.0f;
	const int movementState = speed < config.idleSpeedThreshold
		? 0 : (speed < config.runSpeedThreshold ? 1 : 2);
	return std::min(movementState, std::clamp(config.maxMovementState, 0, 2));
}

}
