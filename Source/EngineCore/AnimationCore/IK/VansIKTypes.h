#pragma once

// ────────────────────────────────────────────────────────────────────
//  VansIKTypes — IK 系统的全部基础类型定义
//
//  包含：求解器类型、Profile 类型、关节约束、链定义、目标、求解结果
// ────────────────────────────────────────────────────────────────────

#include "../VansAnimationTypes.h"
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace VansGraphics
{
	// ─── 求解器类型 ────────────────────────────────────────────────

	enum class IKSolverType
	{
		CCD,     // 人体关节链（精确旋转约束）
		FABRIK,  // 非关节链（自然位置投影）
		LookAt   // 朝向/瞄准
	};

	// ─── 预置 Profile 类型 ────────────────────────────────────────

	enum class IKProfileType
	{
		Custom,           // 完全手动配置
		HumanoidArm,      // 手臂: CCD + Hinge(肘) + BallSocket(肩) + Pole
		HumanoidLeg,      // 腿部: CCD + Hinge(膝) + BallSocket(髋) + Pole
		HumanoidSpine,    // 脊柱: CCD + 角度限制
		HumanoidHead,     // 头颈: LookAt
		Tail,             // 尾巴: FABRIK
		Tentacle,         // 触手: FABRIK
		Rope              // 绳索: FABRIK
	};

	// ─── 关节约束类型 ────────────────────────────────────────────

	enum class JointConstraintType
	{
		None,
		BallSocket,   // 球窝关节 — 锥形角度限制（肩/髋）
		Hinge,        // 铰链关节 — 单轴旋转（肘/膝）
		AngleLimit,   // 通用三轴角度范围
		TwistLimit,   // 扭转限制
		Locked        // 完全锁定（不参与 IK）
	};

	// ─── 关节约束定义 ────────────────────────────────────────────

	struct JointConstraint
	{
		JointConstraintType type = JointConstraintType::None;

		// 旋转轴（在骨骼局部空间定义）
		glm::vec3 localXAxis = glm::vec3(1.0f, 0.0f, 0.0f);  // 默认: X = 扭转轴
		glm::vec3 localYAxis = glm::vec3(0.0f, 1.0f, 0.0f);  // 默认: Y = 主弯曲轴
		glm::vec3 localZAxis = glm::vec3(0.0f, 0.0f, 1.0f);  // 默认: Z = 副弯曲轴

		// 角度范围（度）
		float minAngleX = -180.0f, maxAngleX = 180.0f;
		float minAngleY = -180.0f, maxAngleY = 180.0f;
		float minAngleZ = -180.0f, maxAngleZ = 180.0f;

		// BallSocket 的锥形半角
		float coneAngleDeg = 60.0f;

		// 刚度: 0.0 = 软约束, 1.0 = 硬约束
		float stiffness = 1.0f;

		// 参考姿态（通常为 bind pose 局部旋转）
		glm::quat restRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	};

	// ─── IK 链中的单根骨骼 ──────────────────────────────────────

	struct IKBoneLink
	{
		int             boneIndex       = -1;
		std::string     boneName;
		JointConstraint constraint;
		float           stiffnessWeight = 1.0f;   // 该骨骼参与 IK 的权重
		bool            isEffector      = false;  // 是否为末端效应器
	};

	// ─── IK 链定义 ───────────────────────────────────────────────

	struct IKChainDefinition
	{
		std::string             chainName;
		IKSolverType            solverType  = IKSolverType::CCD;
		IKProfileType           profileType = IKProfileType::Custom;

		std::vector<IKBoneLink> bones;  // 从根到末端（有序）

		// 求解参数
		int     maxIterations     = 15;
		float   positionTolerance = 0.0005f;
		float   rotationTolerance = 0.01f;

		// 极向量（仅 CCD 有效，世界空间）
		glm::vec3 poleVector = glm::vec3(0.0f, 0.0f, -1.0f);
		float     poleWeight = 0.0f;   // 0.0 = 关闭极向量

		// 是否同时约束末端旋转
		bool  enableRotationTarget = false;
		float rotationWeight       = 0.0f;

		// 仅供全身 IK 排序使用
		int   solvePriority = 0;
	};

	// ─── IK 目标（每帧设置）─────────────────────────────────────

	struct IKTarget
	{
		glm::vec3 position       = glm::vec3(0.0f);
		glm::quat rotation       = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		float     positionWeight = 1.0f;
		float     rotationWeight = 0.0f;
	};

	// ─── 求解结果 ────────────────────────────────────────────────

	struct IKSolveResult
	{
		bool  converged      = false;
		int   iterationsUsed = 0;
		float finalPosError  = 0.0f;   // 末端到目标的距离
		float finalRotError  = 0.0f;   // 末端旋转差异（度）
	};

}  // namespace VansGraphics
