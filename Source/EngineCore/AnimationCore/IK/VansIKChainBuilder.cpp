#include "VansIKChainBuilder.h"

namespace VansGraphics
{
	static int FindBone(const Skeleton& sk, const std::string& name)
	{
		auto it = sk.boneNameToIndex.find(name);
		return it != sk.boneNameToIndex.end() ? it->second : -1;
	}

	IKChainDefinition VansIKChainBuilder::BuildHumanoidArm(
		const Skeleton&    skeleton,
		const std::string& shoulderName,
		const std::string& elbowName,
		const std::string& handName,
		bool               isRightArm)
	{
		IKChainDefinition chain;
		chain.chainName   = isRightArm ? "RightArm" : "LeftArm";
		chain.solverType  = IKSolverType::CCD;
		chain.profileType = IKProfileType::HumanoidArm;
		chain.maxIterations     = 15;
		chain.positionTolerance = 0.0005f;
		// 默认极向量指向角色背后下方（弯曲肘部时向后）
		chain.poleVector = isRightArm ? glm::vec3( 0.5f, -0.5f, -1.0f)
		                              : glm::vec3(-0.5f, -0.5f, -1.0f);
		chain.poleWeight = 0.0f;  // 默认关闭，由用户启用

		// 肩 — 球窝约束
		IKBoneLink shoulder;
		shoulder.boneIndex = FindBone(skeleton, shoulderName);
		shoulder.boneName  = shoulderName;
		shoulder.constraint.type         = JointConstraintType::BallSocket;
		shoulder.constraint.coneAngleDeg = 75.0f;
		shoulder.constraint.stiffness    = 0.95f;
		chain.bones.push_back(shoulder);

		// 肘 — 铰链约束
		IKBoneLink elbow;
		elbow.boneIndex = FindBone(skeleton, elbowName);
		elbow.boneName  = elbowName;
		elbow.constraint.type        = JointConstraintType::Hinge;
		elbow.constraint.localYAxis  = glm::vec3(0, 1, 0);
		elbow.constraint.minAngleY   = 0.0f;
		elbow.constraint.maxAngleY   = 150.0f;
		elbow.constraint.stiffness   = 0.95f;
		chain.bones.push_back(elbow);

		// 手 — 末端
		IKBoneLink hand;
		hand.boneIndex  = FindBone(skeleton, handName);
		hand.boneName   = handName;
		hand.isEffector = true;
		chain.bones.push_back(hand);

		return chain;
	}

	IKChainDefinition VansIKChainBuilder::BuildHumanoidLeg(
		const Skeleton&    skeleton,
		const std::string& hipName,
		const std::string& kneeName,
		const std::string& footName,
		bool               isRightLeg)
	{
		IKChainDefinition chain;
		chain.chainName   = isRightLeg ? "RightLeg" : "LeftLeg";
		chain.solverType  = IKSolverType::CCD;
		chain.profileType = IKProfileType::HumanoidLeg;
		chain.maxIterations     = 15;
		chain.positionTolerance = 0.0005f;
		// 膝盖朝前
		chain.poleVector = glm::vec3(0.0f, 0.0f, 1.0f);
		chain.poleWeight = 0.0f;

		IKBoneLink hip;
		hip.boneIndex = FindBone(skeleton, hipName);
		hip.boneName  = hipName;
		hip.constraint.type         = JointConstraintType::BallSocket;
		hip.constraint.coneAngleDeg = 60.0f;
		hip.constraint.stiffness    = 0.95f;
		chain.bones.push_back(hip);

		IKBoneLink knee;
		knee.boneIndex = FindBone(skeleton, kneeName);
		knee.boneName  = kneeName;
		knee.constraint.type        = JointConstraintType::Hinge;
		knee.constraint.localYAxis  = glm::vec3(0, 1, 0);
		knee.constraint.minAngleY   = 0.0f;
		knee.constraint.maxAngleY   = 140.0f;
		knee.constraint.stiffness   = 0.98f;
		chain.bones.push_back(knee);

		IKBoneLink foot;
		foot.boneIndex  = FindBone(skeleton, footName);
		foot.boneName   = footName;
		foot.isEffector = true;
		chain.bones.push_back(foot);

		return chain;
	}

	IKChainDefinition VansIKChainBuilder::BuildLookAt(
		const Skeleton&                 skeleton,
		const std::vector<std::string>& boneNames,
		const std::vector<float>&       boneWeights)
	{
		IKChainDefinition chain;
		chain.chainName   = "LookAt";
		chain.solverType  = IKSolverType::LookAt;
		chain.profileType = IKProfileType::HumanoidHead;
		chain.maxIterations = 1;

		for (size_t i = 0; i < boneNames.size(); ++i)
		{
			IKBoneLink link;
			link.boneIndex = FindBone(skeleton, boneNames[i]);
			link.boneName  = boneNames[i];
			link.stiffnessWeight = (i < boneWeights.size()) ? boneWeights[i] : 1.0f;
			if (i + 1 == boneNames.size()) link.isEffector = true;
			chain.bones.push_back(link);
		}
		return chain;
	}

	IKChainDefinition VansIKChainBuilder::BuildFABRIKChain(
		const Skeleton&                 skeleton,
		const std::vector<std::string>& boneNamesFromRootToTip,
		int                             maxIterations)
	{
		IKChainDefinition chain;
		chain.chainName   = "FABRIKChain";
		chain.solverType  = IKSolverType::FABRIK;
		chain.profileType = IKProfileType::Tail;
		chain.maxIterations     = maxIterations;
		chain.positionTolerance = 0.001f;

		for (size_t i = 0; i < boneNamesFromRootToTip.size(); ++i)
		{
			IKBoneLink link;
			link.boneIndex = FindBone(skeleton, boneNamesFromRootToTip[i]);
			link.boneName  = boneNamesFromRootToTip[i];
			if (i + 1 == boneNamesFromRootToTip.size()) link.isEffector = true;
			chain.bones.push_back(link);
		}
		return chain;
	}

}  // namespace VansGraphics
