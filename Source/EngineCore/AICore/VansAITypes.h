#pragma once

#include "../SceneRuntime/VansRuntimeHandle.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Vans
{
enum class VansAIValueType : std::uint8_t
{
	Bool,
	Int,
	Float,
	Vector3,
	Entity
};

using VansAIValue = std::variant<bool, std::int64_t, double, glm::vec3, VansEntityHandle>;

struct VansAIBlackboardEntryDefinition
{
	std::string name;
	VansAIValueType type = VansAIValueType::Bool;
	VansAIValue defaultValue = false;
};

enum class VansAIConditionKind : std::uint8_t
{
	BlackboardBool,
	AnimationState
};

struct VansAIConditionDefinition
{
	VansAIConditionKind kind = VansAIConditionKind::BlackboardBool;
	std::string key;
	bool expectedBool = false;
	std::string expectedString;
};

struct VansAITransitionDefinition
{
	VansAIConditionDefinition condition;
	std::string targetState;
};

enum class VansAITaskKind : std::uint8_t
{
	Hold,
	MoveToTarget,
	Patrol
};

struct VansAIPatrolTaskConfig
{
	float radius = 4.0f;
	float waitSeconds = 1.25f;
};

struct VansAIStateDefinition
{
	std::string id;
	VansAITaskKind task = VansAITaskKind::Hold;
	std::optional<VansAIPatrolTaskConfig> patrol;
	std::vector<VansAITransitionDefinition> transitions;
};

struct VansAIBehaviorAsset
{
	std::string name;
	std::vector<VansAIBlackboardEntryDefinition> blackboard;
	std::string initialState;
	std::vector<VansAIStateDefinition> states;

	const VansAIStateDefinition* FindState(const std::string& id) const;
};
}
