#pragma once

// ────────────────────────────────────────────────────────────────────
//  VansIKSolver — IK 求解器抽象基类
//
//  所有具体求解器（CCD/FABRIK/LookAt）都从此派生。
//  求解器以"链定义 + 目标 + 全局变换快照"为输入，
//  直接修改 localTransforms 中链上骨骼的旋转分量。
// ────────────────────────────────────────────────────────────────────

#include "VansIKTypes.h"
#include "../VansAnimationTypes.h"
#include <vector>

namespace VansGraphics
{
	class VansIKSolver
	{
	public:
		virtual ~VansIKSolver() = default;

		// 执行 IK 求解。
		//   localTransforms : 输入/输出，骨骼局部变换（修改链上骨骼）
		//   globalTransforms: 输入快照，链开始求解前的全局变换
		//   skeleton        : 骨架信息
		//   chain           : 链定义
		//   target          : 目标位置/旋转
		//   deltaTime       : 帧时间（用于平滑）
		virtual IKSolveResult Solve(
			std::vector<glm::mat4>&       localTransforms,
			const std::vector<glm::mat4>& globalTransforms,
			const Skeleton&               skeleton,
			const IKChainDefinition&      chain,
			const IKTarget&               target,
			float                         deltaTime) = 0;
	};

	// ─── 公共工具 ────────────────────────────────────────────────

	// 从 4x4 变换矩阵提取平移分量（最后一列）
	glm::vec3 IK_ExtractTranslation(const glm::mat4& m);

	// 从 4x4 变换矩阵提取旋转四元数（忽略缩放，先归一化基向量）
	glm::quat IK_ExtractRotation(const glm::mat4& m);

	// 从 4x4 变换矩阵提取缩放向量（基向量长度）
	glm::vec3 IK_ExtractScale(const glm::mat4& m);

	// 从 平移+旋转+缩放 重组 4x4 矩阵
	glm::mat4 IK_ComposeMatrix(const glm::vec3& t, const glm::quat& r, const glm::vec3& s);

	// 仅替换矩阵的旋转部分（保留 scale 和 translation）
	void IK_SetRotation(glm::mat4& m, const glm::quat& r);

	void IK_ApplyEffectorRotationTarget(
		std::vector<glm::mat4>& localTransforms,
		std::vector<glm::mat4>& globalTransforms,
		const Skeleton&         skeleton,
		int                     effectorBoneIdx,
		const IKTarget&         target);

	// 按拓扑顺序更新指定起始骨骼的所有后代全局变换
	void IK_UpdateGlobalsForSubtree(
		int                           rootBoneIdx,
		const std::vector<glm::mat4>& localTransforms,
		std::vector<glm::mat4>&       globalTransforms,
		const Skeleton&               skeleton);

}  // namespace VansGraphics
