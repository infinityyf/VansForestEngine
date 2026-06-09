#include "VansLookAtSolver.h"
#include "VansIKConstraint.h"
#include "../../Util/VansLog.h"
#include <cstdio>

#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace VansGraphics
{
	IKSolveResult VansLookAtSolver::Solve(
		std::vector<glm::mat4>&       localTransforms,
		const std::vector<glm::mat4>& globalTransformsIn,
		const Skeleton&               skeleton,
		const IKChainDefinition&      chain,
		const IKTarget&               target,
		float                         deltaTime)
	{
		IKSolveResult result;
		const int N = static_cast<int>(chain.bones.size());
		if (N < 1 || target.positionWeight < 1e-4f) return result;
		if (globalTransformsIn.size() != skeleton.bones.size()) return result;

		std::vector<glm::mat4> globals = globalTransformsIn;

		// 计算所有 link 的累计权重（用于权重归一化）
		float totalWeight = 0.0f;
		for (const auto& l : chain.bones) totalWeight += l.stiffnessWeight;
		if (totalWeight < 1e-6f) totalWeight = static_cast<float>(N);

		float maxAngleRad = (m_MaxAnglePerBoneDeg > 0.0f)
			? glm::radians(m_MaxAnglePerBoneDeg) : 1e9f;

		// 是否使用 root-local 前向参考（每骨骼自动从绑定姿态推导 local 前向）
		const bool useWorldForward = glm::length(m_WorldForward) > 1e-3f;
		glm::vec3  worldForwardN   = useWorldForward
			? glm::normalize(m_WorldForward) : glm::vec3(0.0f, 0.0f, -1.0f);

		// 诊断：每调用 300 次打印一次骨骼位置/前向/目标，帮助定位 IK 坐标系问题。
		static int s_debugCount = 0;
		const bool doDebug = ((++s_debugCount) % 300 == 1);
		if (doDebug)
		{
			VANS_LOG("[LookAtSolver] target=(" << target.position.x << "," << target.position.y << "," << target.position.z
				<< ") weight=" << target.positionWeight << " useWorldFwd=" << (useWorldForward ? 1 : 0));
			for (int i = 0; i < N && i < 3; ++i)
			{
				if (chain.bones[i].boneIndex < 0) continue;
				glm::vec3 jp = IK_ExtractTranslation(globals[chain.bones[i].boneIndex]);
				glm::quat br = IK_ExtractRotation(globals[chain.bones[i].boneIndex]);
				// 计算该骨骼的 localForward 和 curDir 用于调试
				glm::vec3 dbgLocalFwd;
				if (useWorldForward) {
					glm::quat bgr = IK_ExtractRotation(glm::inverse(skeleton.bones[chain.bones[i].boneIndex].offsetMatrix));
					dbgLocalFwd = glm::normalize(glm::inverse(bgr) * worldForwardN);
				} else {
					dbgLocalFwd = m_ForwardAxis;
				}
				glm::vec3 dbgCurDir = useWorldForward ? worldForwardN : glm::normalize(br * m_ForwardAxis);
				glm::vec3 dbgDesDir = glm::normalize(target.position - jp);
				VANS_LOG("[LookAtSolver]   bone='" << chain.bones[i].boneName
					<< "' pos=(" << jp.x << "," << jp.y << "," << jp.z
					<< ") localFwd=(" << dbgLocalFwd.x << "," << dbgLocalFwd.y << "," << dbgLocalFwd.z
					<< ") curDir=(" << dbgCurDir.x << "," << dbgCurDir.y << "," << dbgCurDir.z
					<< ") desDir=(" << dbgDesDir.x << "," << dbgDesDir.y << "," << dbgDesDir.z << ")");
			}
		}

		for (int i = 0; i < N; ++i)
		{
			const IKBoneLink& link = chain.bones[i];
			if (link.boneIndex < 0) continue;
			if (link.constraint.type == JointConstraintType::Locked) continue;

			glm::mat4& boneGlobal = globals[link.boneIndex];
			glm::vec3 jointPos = IK_ExtractTranslation(boneGlobal);
			glm::quat boneRot  = IK_ExtractRotation(boneGlobal);

			// 计算"当前前向"方向：
			//  - useWorldForward = true：直接使用 skeleton-local 参考前向（worldForwardN）。
			//    不从绑定姿态推导骨骼 local 轴——绑定姿态推导在当前动画帧中会因旋转偏移
			//    导致 curDir 指向错误方向（如指天），产生约 100° 的误差旋转。
			//    直接使用 worldForwardN 时 curDir 恒为角色前向，angle 仅约 10°，符合预期。
			//  - 否则按旧逻辑使用固定 m_ForwardAxis（bone-local）。
			glm::vec3 curDir;
			if (useWorldForward)
			{
				curDir = worldForwardN;
			}
			else
			{
				curDir = glm::normalize(boneRot * m_ForwardAxis);
			}
			glm::vec3 desDir = target.position - jointPos;
			float dl = glm::length(desDir);
			if (dl < 1e-6f) continue;
			desDir /= dl;

			float d = glm::clamp(glm::dot(curDir, desDir), -1.0f, 1.0f);
			if (d > 0.99999f) continue;

			float angle = std::acos(d);
			// 单骨骼角度限制
			if (angle > maxAngleRad) angle = maxAngleRad;

			// 按权重分摊
			float weight = (link.stiffnessWeight / totalWeight) * target.positionWeight;
			angle *= weight;

			glm::vec3 axis = glm::cross(curDir, desDir);
			float al = glm::length(axis);
			if (al < 1e-6f) continue;
			axis /= al;

			glm::quat worldDelta = glm::angleAxis(angle, axis);

			const BoneInfo& bone = skeleton.bones[link.boneIndex];
			glm::quat parentRot = (bone.parentIndex >= 0)
				? IK_ExtractRotation(globals[bone.parentIndex])
				: glm::quat(1, 0, 0, 0);

			glm::quat localDelta = IK_WorldDeltaToLocal(parentRot, worldDelta);
			glm::quat curLocal   = IK_ExtractRotation(localTransforms[link.boneIndex]);
			glm::quat newLocal   = glm::normalize(localDelta * curLocal);

			if (link.constraint.type != JointConstraintType::None)
				newLocal = IK_ApplyJointConstraint(newLocal, link.constraint);

			IK_SetRotation(localTransforms[link.boneIndex], newLocal);
			IK_UpdateGlobalsForSubtree(link.boneIndex, localTransforms, globals, skeleton);
		}

		result.converged = true;
		result.iterationsUsed = 1;
		return result;
	}

}  // namespace VansGraphics
