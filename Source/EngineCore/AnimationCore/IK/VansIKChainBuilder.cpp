#include "VansIKChainBuilder.h"
#include "VansIKSolver.h"

#include <cmath>

namespace VansGraphics
{
	static int FindBone(const Skeleton& sk, const std::string& name)
	{
		auto it = sk.boneNameToIndex.find(name);
		return it != sk.boneNameToIndex.end() ? it->second : -1;
	}

	static glm::mat4 BindModelTransform(const Skeleton& skeleton, int boneIndex)
	{
		if (boneIndex < 0 || boneIndex >= static_cast<int>(skeleton.bones.size()))
			return glm::mat4(1.0f);
		return glm::inverse(skeleton.bones[boneIndex].offsetMatrix);
	}

	static glm::quat BindLocalRotation(const Skeleton& skeleton, int boneIndex)
	{
		const glm::mat4 bindModel = BindModelTransform(skeleton, boneIndex);
		const int parentIndex = boneIndex >= 0 && boneIndex < static_cast<int>(skeleton.bones.size())
			? skeleton.bones[boneIndex].parentIndex : -1;
		const glm::mat4 bindLocal = parentIndex >= 0
			? glm::inverse(BindModelTransform(skeleton, parentIndex)) * bindModel
			: bindModel;
		return IK_ExtractRotation(bindLocal);
	}

	static glm::vec3 BindLocalDirectionToChild(const Skeleton& skeleton, int boneIndex, int childIndex)
	{
		if (boneIndex < 0 || childIndex < 0 ||
		    boneIndex >= static_cast<int>(skeleton.bones.size()) ||
		    childIndex >= static_cast<int>(skeleton.bones.size()))
			return glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::mat4 bindModel = BindModelTransform(skeleton, boneIndex);
		const glm::vec3 direction = IK_ExtractTranslation(BindModelTransform(skeleton, childIndex)) -
		                            IK_ExtractTranslation(bindModel);
		if (glm::length(direction) < 1e-5f) return glm::vec3(0.0f, 1.0f, 0.0f);
		return glm::normalize(glm::conjugate(IK_ExtractRotation(bindModel)) * glm::normalize(direction));
	}

	static glm::vec3 BindLocalHingeAxis(const Skeleton& skeleton,
	                                   int parentIndex,
	                                   int jointIndex,
	                                   int childIndex,
	                                   const glm::vec3& fallbackModelPole)
	{
		if (parentIndex < 0 || jointIndex < 0 || childIndex < 0 ||
		    parentIndex >= static_cast<int>(skeleton.bones.size()) ||
		    jointIndex >= static_cast<int>(skeleton.bones.size()) ||
		    childIndex >= static_cast<int>(skeleton.bones.size()))
			return glm::vec3(0.0f, 1.0f, 0.0f);
		const glm::vec3 parent = IK_ExtractTranslation(BindModelTransform(skeleton, parentIndex));
		const glm::mat4 jointModel = BindModelTransform(skeleton, jointIndex);
		const glm::vec3 joint = IK_ExtractTranslation(jointModel);
		const glm::vec3 child = IK_ExtractTranslation(BindModelTransform(skeleton, childIndex));
		const glm::vec3 lowerSegment = child - joint;
		if (glm::length(lowerSegment) < 1e-5f) return glm::vec3(1.0f, 0.0f, 0.0f);
		const glm::vec3 lowerDirection = glm::normalize(lowerSegment);
		glm::vec3 axis = glm::cross(joint - parent, child - joint);
		if (glm::length(axis) < 1e-5f)
		{
			glm::vec3 poleDirection = fallbackModelPole - lowerDirection * glm::dot(fallbackModelPole, lowerDirection);
			if (glm::length(poleDirection) < 1e-5f)
			{
				const glm::vec3 reference = std::abs(lowerDirection.y) < 0.75f
					? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
				poleDirection = reference - lowerDirection * glm::dot(reference, lowerDirection);
			}
			axis = glm::cross(lowerDirection, glm::normalize(poleDirection));
		}
		if (glm::length(axis) < 1e-5f) return glm::vec3(1.0f, 0.0f, 0.0f);
		axis = glm::normalize(axis);
		return glm::normalize(glm::conjugate(IK_ExtractRotation(jointModel)) * axis);
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
		shoulder.constraint.restRotation = BindLocalRotation(skeleton, shoulder.boneIndex);
		shoulder.constraint.localYAxis   = BindLocalDirectionToChild(skeleton, shoulder.boneIndex, FindBone(skeleton, elbowName));
		shoulder.constraint.coneAngleDeg = 75.0f;
		shoulder.constraint.stiffness    = 0.95f;
		chain.bones.push_back(shoulder);

		// 肘 — 铰链约束
		IKBoneLink elbow;
		elbow.boneIndex = FindBone(skeleton, elbowName);
		elbow.boneName  = elbowName;
		elbow.constraint.type        = JointConstraintType::Hinge;
		elbow.constraint.restRotation = BindLocalRotation(skeleton, elbow.boneIndex);
		elbow.constraint.localYAxis  = BindLocalHingeAxis(
			skeleton, shoulder.boneIndex, elbow.boneIndex, FindBone(skeleton, handName),
			glm::vec3(0.0f, -0.5f, -1.0f));
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
		hip.constraint.restRotation = BindLocalRotation(skeleton, hip.boneIndex);
		hip.constraint.localYAxis   = BindLocalDirectionToChild(skeleton, hip.boneIndex, FindBone(skeleton, kneeName));
		hip.constraint.coneAngleDeg = 60.0f;
		hip.constraint.stiffness    = 0.95f;
		chain.bones.push_back(hip);

		IKBoneLink knee;
		knee.boneIndex = FindBone(skeleton, kneeName);
		knee.boneName  = kneeName;
		knee.constraint.type        = JointConstraintType::Hinge;
		knee.constraint.restRotation = BindLocalRotation(skeleton, knee.boneIndex);
		knee.constraint.localYAxis  = BindLocalHingeAxis(
			skeleton, hip.boneIndex, knee.boneIndex, FindBone(skeleton, footName),
			glm::vec3(0.0f, 0.0f, 1.0f));
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
