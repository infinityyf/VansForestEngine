#include "VansRotationDistributionSolver.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace VansGraphics
{
	VansProceduralSolverResult VansRotationDistributionSolver::Solve(VansPoseWorkspace& pose,
		const VansCompiledAnimationRig& rig, const VansCompiledRotationDistribution& profile,
		const VansProceduralGoal& goal, VansRotationDistributionState& state)
	{
		VansProceduralSolverResult result;
		const int base = profile.baseBoneIndex, tip = profile.tipBoneIndex;
		if (!goal.valid || !std::isfinite(goal.rotationWeight) || goal.rotationWeight < 0 ||
			goal.rotationWeight > 1 || !pose.IsValidBone(base) || !pose.IsValidBone(tip) ||
			pose.GetSkeleton() != rig.skeleton || rig.skeleton->bones[tip].parentIndex != base ||
			profile.recipients.size() > VansMaxProceduralChainBones) return result;
		const float weight = goal.rotationWeight;
		if (weight <= 0)
		{ state = {}; result.status = VansProceduralSolverStatus::NoEffect; return result; }
		const auto targetCheck = VansApplyJointLimit(goal.rotationModel, nullptr);
		if (!targetCheck.valid) return result;
		const glm::vec3 startPosition = pose.GetComponentPosition(tip);
		const glm::vec3 segment = startPosition - pose.GetComponentPosition(base);
		const float length = glm::length(segment);
		if (!std::isfinite(length) || length < 1.e-6f) return result;
		const glm::vec3 axis = segment / length;
		const glm::quat baseRotation = pose.GetComponentRotation(base);
		const glm::quat tipRotation = pose.GetComponentRotation(tip);
		const glm::quat weightedRequestedTip = glm::normalize(glm::slerp(tipRotation, targetCheck.rotation, weight));
		// 绑定姿态定义中性朝向；旋转轴由实际骨段提供，与骨名和模型轴无关。
		const glm::quat delta = targetCheck.rotation * glm::inverse(baseRotation * profile.restTipRotationInBase);
		const float wrappedTwist = VansExtractTwistRadians(delta, axis);
		// 仅记录旋转分支，不累加上帧骨姿态；目标越过 180 度时仍连续。
		const float twist = state.valid
			? state.twistRadians + std::remainder(wrappedTwist - state.twistRadians, 6.28318530718f)
			: wrappedTwist;
		if (!std::isfinite(twist)) return result;
		std::array<glm::quat, VansMaxProceduralChainBones> wantedRecipients;
		std::array<glm::quat, VansMaxProceduralChainBones> originalRecipientRotations;
		std::array<VansBoneTransform, VansMaxProceduralChainBones> originalRecipientLocals;
		for (std::size_t i = 0; i < profile.recipients.size(); ++i)
		{
			const auto& recipient = profile.recipients[i];
			if (!pose.IsValidBone(recipient.boneIndex)) return result;
			const glm::quat desired = glm::angleAxis(twist * recipient.fraction, axis) *
				baseRotation * recipient.restRotationInBase;
			originalRecipientRotations[i] = pose.GetComponentRotation(recipient.boneIndex);
			originalRecipientLocals[i] = pose.GetLocal(recipient.boneIndex);
			wantedRecipients[i] = glm::normalize(desired);
		}
		const glm::quat baseLocal = pose.GetLocal(base).rotation;
		const glm::vec3 localAxis = glm::inverse(baseRotation) * axis;
		const auto originalBaseLocal = pose.GetLocal(base), originalTipLocal = pose.GetLocal(tip);
		const auto rollback = [&]()
		{
			pose.SetLocal(base, originalBaseLocal); pose.SetLocal(tip, originalTipLocal);
			for (std::size_t i = 0; i < profile.recipients.size(); ++i)
				pose.SetLocal(profile.recipients[i].boneIndex, originalRecipientLocals[i]);
			return VansProceduralSolverResult{};
		};
		const float requested = twist * profile.baseFraction;
		float accepted = requested;
		const auto* baseLimit = rig.FindJointLimit(base);
		const auto withinLimit = [&](float angle)
		{
			const auto checked = VansApplyJointLimit(baseLocal * glm::angleAxis(angle, localAxis), baseLimit);
			return checked.valid && !checked.limited;
		};
		const float axisAlignment = baseLimit ? glm::dot(localAxis, baseLimit->axisLocal) : 0;
		if (baseLimit && baseLimit->kind != VansJointLimitKind::Locked && std::abs(axisAlignment) > 0.99999f && withinLimit(0))
		{
			// 同轴自由度直接限制未折返的角度，避免越过一周后绕到限制区间的另一侧。
			const float sign = axisAlignment < 0 ? -1.f : 1.f;
			const float start = VansExtractTwistRadians(glm::inverse(baseLimit->restLocalRotation) * baseLocal, baseLimit->axisLocal);
			accepted = (std::clamp(start + requested * sign, glm::radians(baseLimit->minDegrees),
				glm::radians(baseLimit->maxDegrees)) - start) * sign;
			if (std::abs(accepted - requested) > 1.e-6f) result.limitReason |= VansProceduralLimitReason::Joint;
		}
		else if (baseLimit && !withinLimit(requested))
		{
			// 沿唯一的轴向自由度缩小增量，不能直接投影整个关节而移动已求解的端点。
			float low = 0, high = 1;
			if (withinLimit(0))
				for (int i = 0; i < 16; ++i)
				{
					const float middle = (low + high) * 0.5f;
					if (withinLimit(requested * middle)) low = middle; else high = middle;
				}
			accepted = requested * low;
			result.limitReason |= VansProceduralLimitReason::Joint;
		}
		if (!pose.SetLocalRotation(base, glm::normalize(baseLocal * glm::angleAxis(accepted, localAxis)))) return rollback();
		const auto setLimitedComponent = [&](int bone, const glm::quat& rotation)
		{
			const int parent = rig.skeleton->bones[bone].parentIndex;
			const glm::quat local = parent >= 0 ? glm::inverse(pose.GetComponentRotation(parent)) * rotation : rotation;
			const auto checked = VansApplyJointLimit(local, rig.FindJointLimit(bone));
			if (checked.limited) result.limitReason |= VansProceduralLimitReason::Joint;
			return checked.valid && pose.SetLocalRotation(bone, checked.rotation);
		};
		if (!setLimitedComponent(tip, targetCheck.rotation)) return rollback();
		for (std::size_t i = 0; i < profile.recipients.size(); ++i)
			if (!setLimitedComponent(profile.recipients[i].boneIndex, wantedRecipients[i])) return rollback();
		if (weight < 1)
		{
			// 先求完整受限姿态，再混入输入。输入已超限时，激活的第一帧不能突然跳到边界。
			const auto solvedTip = pose.GetComponentRotation(tip);
			for (std::size_t i = 0; i < profile.recipients.size(); ++i)
				wantedRecipients[i] = pose.GetComponentRotation(profile.recipients[i].boneIndex);
			pose.SetLocalRotation(base, glm::normalize(baseLocal * glm::angleAxis(accepted * weight, localAxis)));
			pose.SetComponentRotation(tip, glm::normalize(glm::slerp(tipRotation, solvedTip, weight)));
			for (std::size_t i = 0; i < profile.recipients.size(); ++i)
				pose.SetComponentRotation(profile.recipients[i].boneIndex,
					glm::normalize(glm::slerp(originalRecipientRotations[i], wantedRecipients[i], weight)));
		}
		if (!pose.IsFinite()) return rollback();
		result.iterations = 1;
		result.effectivePositionError = glm::length(pose.GetComponentPosition(tip) - startPosition);
		result.rotationErrorDegrees = VansQuaternionAngleDegrees(glm::inverse(weightedRequestedTip) * pose.GetComponentRotation(tip));
		result.status = result.limitReason == VansProceduralLimitReason::None
			? VansProceduralSolverStatus::Solved : VansProceduralSolverStatus::Clamped;
		state.twistRadians = twist;
		state.valid = true;
		return result;
	}
}
