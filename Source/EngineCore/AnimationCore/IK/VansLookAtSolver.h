#pragma once

// ────────────────────────────────────────────────────────────────────
//  VansLookAtSolver — 朝向/瞄准求解器
//
//  让一根或多根骨骼朝向目标点。
//  支持脊柱权重分发（多骨骼按比例分担总旋转）和单骨骼最大角度限制。
// ────────────────────────────────────────────────────────────────────

#include "VansIKSolver.h"

namespace VansGraphics
{
	class VansLookAtSolver : public VansIKSolver
	{
	public:
		IKSolveResult Solve(
			std::vector<glm::mat4>&       localTransforms,
			const std::vector<glm::mat4>& globalTransforms,
			const Skeleton&               skeleton,
			const IKChainDefinition&      chain,
			const IKTarget&               target,
			float                         deltaTime) override;

		// 局部空间的"前向"轴，用于决定骨骼朝向哪条轴瞄准目标。
		// 默认 -Z（OpenGL 习惯）。
		glm::vec3 m_ForwardAxis = glm::vec3(0.0f, 0.0f, -1.0f);

		// 单骨骼最大旋转角度（度）。<=0 表示无限制。
		float     m_MaxAnglePerBoneDeg = 80.0f;
	};

}  // namespace VansGraphics
