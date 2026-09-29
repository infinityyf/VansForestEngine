#pragma once

#include "VansPoseTypes.h"
#include <map>

namespace VansGraphics
{
	// 历史必须拥有内存；不能保存帧分配器或 Clip 的 string_view。
	struct VansInertializationCurve
	{
		std::string name;
		float value = 0;
	};
	struct VansInertializationSnapshot
	{
		std::vector<VansBoneTransform> bones;
		std::map<std::uint64_t, VansInertializationCurve> curves;
		glm::mat4 ownerWorld{1};
		float deltaTime = 0;
	};
	struct VansInertializationBoneDiff
	{
		glm::vec3 translationAxis{0}, rotationAxis{1,0,0}, scaleAxis{0};
		float translation = 0, translationSpeed = 0;
		float angle = 0, angularSpeed = 0;
		float scale = 0, scaleSpeed = 0;
	};
	struct VansInertializationCurveDiff
	{
		std::string name;
		float delta = 0, derivative = 0;
	};
	struct VansInertializationState
	{
		VansInertializationSnapshot previous, current;
		std::vector<VansInertializationBoneDiff> boneDiffs;
		std::map<std::uint64_t, VansInertializationCurveDiff> curveDiffs;
		std::vector<float> requests;
		float deltaTime = 0, elapsed = 0, duration = 0, deficit = 0;
		bool active = false;

		bool IsFinite() const;
		static float Decay(float offset, float velocity, float time, float duration);
		void Evaluate(VansPosePayload& pose, const glm::mat4& ownerWorld, float teleportDistance);
	};
}
