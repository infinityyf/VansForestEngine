#pragma once

#include "../../GameplayActionCore/VansActionServices.h"
#include "../../RuntimeCore/VansGenerationPool.h"
#include "../../GameplayTargeting/VansGameplayTargeting.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace Vans
{
class VansGameplayRuntime;
class VansRuntimeWorld;

const VansActionServiceCapability& VansCombatActionCapability();

struct VansCombatDebugMeleeWindow
{
	std::string owner;
	std::string window;
	bool active = false;
	glm::vec3 origin{ 0.0f };
	glm::vec3 forward{ 0.0f, 0.0f, 1.0f };
	glm::vec3 previousBase{ 0.0f };
	glm::vec3 previousTip{ 0.0f };
	glm::vec3 currentBase{ 0.0f };
	glm::vec3 currentTip{ 0.0f };
	float range = 0.0f;
	float halfAngleDegrees = 0.0f;
	float sweepRadius = 0.0f;
	std::size_t hitCount = 0;
};

struct VansCombatDebugHurtBody
{
	std::string target;
	glm::vec3 center{ 0.0f };
	float radius = 0.0f;
	float halfHeight = 0.0f;
	bool hit = false;
	glm::vec3 axis{ 1.0f, 0.0f, 0.0f };
	std::string region;
	std::string componentGuid;
};

struct VansCombatDebugSnapshot
{
	bool available = false;
	std::vector<VansCombatDebugMeleeWindow> windows;
	std::vector<VansCombatDebugHurtBody> hurtBodies;
};

// 纯几何函数供运行时和契约测试共同使用。扇形校验负责攻击意图，路径校验
// 负责高速武器在相邻帧之间不漏判；二者都通过后才产生命中。
bool VansPointInsideMeleeSector(
	const glm::vec3& point,
	float pointRadius,
	const glm::vec3& origin,
	const glm::vec3& forward,
	float range,
	float halfAngleDegrees,
	float verticalTolerance);

bool VansContinuousWeaponPathIntersectsSphere(
	const glm::vec3& previousBase,
	const glm::vec3& previousTip,
	const glm::vec3& currentBase,
	const glm::vec3& currentTip,
	const glm::vec3& center,
	float combinedRadius);

bool VansContinuousWeaponPathIntersectsCapsule(
	const glm::vec3& previousBase, const glm::vec3& previousTip,
	const glm::vec3& currentBase, const glm::vec3& currentTip,
	const glm::vec3& capsuleStart, const glm::vec3& capsuleEnd,
	float combinedRadius, glm::vec3* axisPoint = nullptr, glm::vec3* weaponPoint = nullptr);

class VansCombatActionService final : public IVansActionService
{
public:
	static std::shared_ptr<VansCombatActionService> Create(
		VansRuntimeWorld& world,
		VansGameplayRuntime& gameplayRuntime,
		std::string& error);

	const VansActionServiceCapability& Capability() const override { return m_Capability; }
	VansActionCommandResult Execute(const VansActionCommand& command) override;
	bool Release(VansGenerationHandle resource, std::string& error) override;

	void Tick(double deltaSeconds) override;
	VansCombatDebugSnapshot CaptureDebugSnapshot() const;

private:
	struct MeleeWindow
	{
		VansActionHandle action;
		VansEntityHandle owner;
		VansEntityHandle instigator;
		std::string baseEntityGuid;
		std::string tipEntityGuid;
		std::string targetLayer;
		std::string targetTag;
		std::string responseAction;
		std::string windowName;
		double elapsedSeconds = 0.0;
		double startSeconds = 0.0;
		double endSeconds = 0.0;
		float sweepRadius = 0.0f;
		float range = 0.0f;
		float halfAngleDegrees = 0.0f;
		float verticalTolerance = 0.0f;
		std::size_t maximumHits = 1;
		bool hasPrevious = false;
		bool windowOpen = false;
		bool sampleActive = false;
		glm::vec3 previousBase{ 0.0f };
		glm::vec3 previousTip{ 0.0f };
		std::unordered_set<std::uint64_t> hitTargets;
	};

	VansCombatActionService(
		VansRuntimeWorld& world,
		VansGameplayRuntime& gameplayRuntime,
		VansActionServiceCapability capability);

	bool SampleWindow(MeleeWindow& window);
	VansEntityHandle ResolveHitTarget(VansEntityHandle entity) const;
	void EmitWindowEvent(MeleeWindow& window, std::string_view edge);
	bool ActivateResponse(MeleeWindow& window, const VansTargetHitResult& hit);

	VansRuntimeWorld& m_World;
	VansGameplayRuntime& m_GameplayRuntime;
	VansActionServiceCapability m_Capability;
	VansGenerationPool<MeleeWindow> m_Windows;
	VansCombatDebugSnapshot m_DebugSnapshot;
};
}
