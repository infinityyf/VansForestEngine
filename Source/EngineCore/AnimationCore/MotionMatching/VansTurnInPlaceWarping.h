#pragma once

#include "VansRootMotionYaw.h"

#include <cstdint>
#include <string>

namespace VansGraphics
{
	struct TurnInPlaceWarpingSettings
	{
		// 新行为必须由项目显式选择，未迁移的 Motion Matching 保持原路径。
		bool enabled = false;
		float maxTargetPredictionTime = 0.5f;
		float proceduralTargetTime = 0.2f;
		float correctionHalfLife = 0.04f;
		float minRootYawScaleRatio = 0.75f;
		float maxRootYawScaleRatio = 1.25f;
		float rootYawThresholdDegrees = 1.0f;
		float maxAdditiveCorrectionDegrees = 15.0f;
		float maxAdditiveYawRateDegreesPerSecond = 240.0f;
		float finalToleranceDegrees = 1.0f;
		float endpointScaleCostWeight = 4.0f;
		float endpointResidualCostWeight = 4.0f;
	};

	struct TurnInPlaceReachability
	{
		bool reachable = false;
		float authoredRemainingYawDegrees = 0.0f;
		float scaleRatio = 1.0f;
		float residualDegrees = 0.0f;
		float endpointCost = 0.0f;
		std::string reason;
	};

	struct TurnInPlaceWarpingResult : TurnInPlaceReachability
	{
		bool active = false;
		bool limited = false;
		bool needsReplan = false;
		float targetDeltaDegrees = 0.0f;
		float authoredFrameYawDegrees = 0.0f;
		float appliedScaleCorrectionDegrees = 0.0f;
		float appliedAdditiveCorrectionDegrees = 0.0f;
		float appliedYawRateDegreesPerSecond = 0.0f;
		float accumulatedAdditiveCorrectionDegrees = 0.0f;
	};

	// 原地 Turn 的端点修正器与移动 Steering 拥有独立状态。它只调整已选
	// Turn 动画的根 Yaw，不负责候选搜索、位移或 CCT Transform 提交。
	class VansTurnInPlaceWarping
	{
	public:
		void Configure(const TurnInPlaceWarpingSettings& settings);
		void Reset();

		TurnInPlaceReachability EvaluateCandidate(
			float targetDeltaDegrees,
			float authoredRemainingYawDegrees,
			float availableCorrectionTimeSeconds) const;

		TurnInPlaceWarpingResult Apply(
			float deltaTime,
			std::uint64_t sourceClipId,
			float animationTimeSeconds,
			float targetDeltaDegrees,
			float authoredRemainingYawDegrees,
			float remainingClipTimeSeconds,
			glm::quat& inOutRootMotionRotation);

	private:
		TurnInPlaceWarpingSettings m_Settings;
		std::uint64_t m_ActiveClipId = 0;
		float m_LastAnimationTimeSeconds = -1.0f;
		float m_AdditiveYawRateDegreesPerSecond = 0.0f;
		float m_AccumulatedAdditiveCorrectionDegrees = 0.0f;
	};
}
