#pragma once

#include "../VansAnimationTypes.h"

#include <../../GLM/gtc/quaternion.hpp>

#include <vector>

namespace VansGraphics
{
	// Turn 动画的根 Yaw 曲线在数据库构建时生成。运行时只做插值，避免逐帧
	// 重新扫描动画轨道，也避免把动画名称中的 45/90/180 当作真实转角。
	struct TurnYawProfile
	{
		std::vector<float> sampleTimesSeconds;
		std::vector<float> cumulativeYawDegrees;
		float durationSeconds = 0.0f;
		float motionEndTimeSeconds = 0.0f;
		float totalYawDegrees = 0.0f;
		int directionSign = 0;
		bool valid = false;

		float SampleCumulativeYaw(float timeSeconds) const;
		float RemainingYaw(float timeSeconds) const;
		float RemainingMotionTime(float timeSeconds) const;
	};

	// 提取四元数绕动画 Z 轴的 signed twist。相较直接读取 Euler Z，此定义在
	// 根骨骼同时包含轻微 tilt 时仍保持稳定。
	float ExtractRootMotionYawDegrees(const glm::quat& rotation);

	// 只给根运动叠加 Z 轴修正，不改变位移，也不覆盖原旋转中的 swing/tilt。
	void ApplyRootMotionYawCorrection(float correctionDegrees, glm::quat& inOutRotation);

	bool BuildTurnYawProfile(
		const VansAnimationClip& clip,
		const Skeleton& skeleton,
		int rootMotionBoneIndex,
		float sampleRate,
		float playbackEndMarginSeconds,
		float motionEndThresholdDegrees,
		TurnYawProfile& outProfile);
}
