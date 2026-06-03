#pragma once

// ────────────────────────────────────────────────────────────────────
//  VansIKChainBuilder — 预置 IK 链工厂
//
//  根据骨骼名快速构建常见的人体/非人体 IK 链。
//  约束默认值参见 IK 设计文档附录 A（人体关节约束参考值）。
// ────────────────────────────────────────────────────────────────────

#include "VansIKTypes.h"
#include "../VansAnimationTypes.h"
#include <string>
#include <vector>

namespace VansGraphics
{
	class VansIKChainBuilder
	{
	public:
		// 人体手臂: shoulder → upperArm(elbow 父节点) → hand
		// 注：传入的三个名字按 root→tip 顺序，shoulder/elbow 为关节，hand 为末端。
		static IKChainDefinition BuildHumanoidArm(
			const Skeleton&    skeleton,
			const std::string& shoulderName,
			const std::string& elbowName,
			const std::string& handName,
			bool               isRightArm);

		// 人体腿部: hip → knee → foot
		static IKChainDefinition BuildHumanoidLeg(
			const Skeleton&    skeleton,
			const std::string& hipName,
			const std::string& kneeName,
			const std::string& footName,
			bool               isRightLeg);

		// LookAt 链（多骨骼按权重分摊朝向）
		static IKChainDefinition BuildLookAt(
			const Skeleton&                 skeleton,
			const std::vector<std::string>& boneNames,
			const std::vector<float>&       boneWeights);

		// 非关节 FABRIK 链（尾巴/触手/绳索）
		static IKChainDefinition BuildFABRIKChain(
			const Skeleton&                 skeleton,
			const std::vector<std::string>& boneNamesFromRootToTip,
			int                             maxIterations = 10);
	};

}  // namespace VansGraphics
