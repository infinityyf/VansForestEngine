#pragma once

// ────────────────────────────────────────────────────────────────────
//  VansCCDSolver — Cyclic Coordinate Descent IK 求解器
//
//  对链中的关节自末端向根逐个旋转，每次旋转使末端朝向目标。
//  每次迭代后应用关节约束（局部空间）。
//
//  用于人体四肢/脊柱等需要精确解剖学约束的链。
// ────────────────────────────────────────────────────────────────────

#include "VansIKSolver.h"

namespace VansGraphics
{
	class VansCCDSolver : public VansIKSolver
	{
	public:
		IKSolveResult Solve(
			std::vector<glm::mat4>&       localTransforms,
			const std::vector<glm::mat4>& globalTransforms,
			const Skeleton&               skeleton,
			const IKChainDefinition&      chain,
			const IKTarget&               target,
			float                         deltaTime) override;

	private:
		// 单次 CCD 扫描（末端 → 根，不含 root 的 effector 自身）
		// 返回：当前帧末端到目标的距离
		float PerformIteration(
			const IKChainDefinition&      chain,
			const IKTarget&               target,
			std::vector<glm::mat4>&       localTransforms,
			std::vector<glm::mat4>&       globalTransforms,
			const Skeleton&               skeleton);

		// 应用极向量约束（仅 chain.poleWeight > 0 时调用）
		void ApplyPoleVector(
			const IKChainDefinition&      chain,
			std::vector<glm::mat4>&       localTransforms,
			std::vector<glm::mat4>&       globalTransforms,
			const Skeleton&               skeleton);
	};

}  // namespace VansGraphics
