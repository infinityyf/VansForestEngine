#pragma once

#include <../../GLM/gtc/quaternion.hpp>

namespace VansGraphics
{
	struct RootMotionSteeringSettings
	{
		bool enabled = true;
		float predictionTime = 0.5f;
		float correctionHalfLife = 0.08f;
		float maxCorrectionAngleDegrees = 75.0f;
		float maxCorrectionYawRateDegreesPerSecond = 360.0f;
		float minMovementSpeed = 0.10f;
	};

	struct RootMotionSteeringResult
	{
		float targetFacingDeltaDegrees = 0.0f;
		float authoredFacingDeltaDegrees = 0.0f;
		float requestedCorrectionDegrees = 0.0f;
		float appliedCorrectionDegrees = 0.0f;
		float appliedYawRateDegreesPerSecond = 0.0f;
		bool active = false;
		bool limited = false;
	};

	// 移动中的朝向修正属于 Root Motion 的连续后处理，不应通过每帧重选一次
	// 离散 Turn clip 来追逐相机。该模块只修改当帧 Root Motion 旋转；位移仍完全
	// 来自被选动画，CCT 因而继续保持单一运动权威。
	class VansRootMotionSteering
	{
	public:
		void Configure(const RootMotionSteeringSettings& settings);
		void Reset();

		RootMotionSteeringResult Apply(
			float deltaTime,
			float movementSpeed,
			float currentFacingYawDegrees,
			float targetFacingYawDegrees,
			float authoredFutureYawDegrees,
			glm::quat& inOutRootMotionRotation);

	private:
		RootMotionSteeringSettings m_Settings;
		float m_CorrectionYawRateDegreesPerSecond = 0.0f;
	};
}
