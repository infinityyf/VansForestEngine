#pragma once

#include "VansProceduralTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	inline constexpr std::size_t VansMaxProceduralChainBones = 64;

	enum class VansRigSolverKind { Limb, CCD, FABRIK, Aim };
	enum class VansJointLimitKind { Hinge, SwingTwist, Locked };
	enum class VansRigAttachmentParentKind { Bone, Socket };

	struct VansRigGoalDefinition
	{
		std::string id;
		std::string effectorBone;
	};

	struct VansRigChainDefinition
	{
		std::string id;
		VansRigSolverKind solver = VansRigSolverKind::Limb;
		std::vector<std::string> bones;
		std::string goal;
		// Authored in the chain-root bone's bind-local space. Compilation converts
		// it to the root parent's bind space for a stable runtime pole frame.
		glm::vec3 poleAxisLocal{ 0.0f, 0.0f, 1.0f };
		float softReachStartRatio = 0.97f;
		float maxStretchScale = 1.0f;
		std::vector<float> weights;
		std::vector<float> solveWeights;
		float maxStepDegrees = 180.0f;
		glm::vec3 forwardAxisLocal{ 0.0f, 0.0f, 1.0f };
		glm::vec3 upAxisLocal{ 0.0f, 1.0f, 0.0f };
	};

	struct VansRigJointLimitDefinition
	{
		std::string bone;
		VansJointLimitKind kind = VansJointLimitKind::Hinge;
		glm::vec3 axisLocal{ 1.0f, 0.0f, 0.0f };
		glm::vec3 swingReferenceAxisLocal{ 0.0f, 0.0f, 1.0f };
		float minDegrees = -180.0f;
		float maxDegrees = 180.0f;
		glm::vec2 swingLimitDegrees{ 180.0f, 180.0f };
	};

	struct VansRigSoleSample
	{
		std::string id;
		glm::vec3 positionLocal{ 0.0f };
	};

	struct VansRigContactDefinition
	{
		std::string id;
		std::string chain;
		std::string footBone;
		std::string ballBone;
		glm::vec3 soleForwardLocal{ 0.0f, 0.0f, 1.0f };
		glm::vec3 soleNormalLocal{ 0.0f, 1.0f, 0.0f };
		std::vector<VansRigSoleSample> soleSamplesLocal;
		glm::vec3 heelPivotLocal{ 0.0f };
		glm::vec3 ballPivotLocal{ 0.0f };
		glm::vec3 anklePivotLocal{ 0.0f };
		float sweepRadius = 0.035f;
	};

	struct VansRigSocketDefinition
	{
		std::string guid;
		std::string name;
		std::string boneGuid;
		glm::vec3 positionLocal{ 0.0f };
		glm::quat rotationLocal{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scaleLocal{ 1.0f };
	};

	// 模型相对 Bone/Socket 锚点的资产级位姿。它不引用场景 Entity，
	// 因此可以在不同场景中复用，并由预览工具独立保存到 Animation Rig。
	struct VansRigAttachmentProfileDefinition
	{
		std::string modelGuid;
		VansRigAttachmentParentKind parentKind = VansRigAttachmentParentKind::Socket;
		std::string anchorGuid;
		glm::vec3 positionLocal{ 0.0f };
		glm::quat rotationLocal{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scaleLocal{ 1.0f };
	};

	struct VansAnimationRigAsset
	{
		std::string name;
		std::string skeletonGuid;
		glm::vec3 modelForward{ 0.0f, 0.0f, 1.0f };
		glm::vec3 modelUp{ 0.0f, 1.0f, 0.0f };
		std::unordered_map<std::string, std::string> semanticBones;
		std::vector<VansRigSocketDefinition> sockets;
		std::vector<VansRigAttachmentProfileDefinition> attachmentProfiles;
		std::vector<VansRigGoalDefinition> goals;
		std::vector<VansRigChainDefinition> chains;
		std::vector<VansRigJointLimitDefinition> jointLimits;
		std::vector<VansRigContactDefinition> contacts;
	};

	struct VansCompiledRigGoal
	{
		std::string id;
		int effectorBoneIndex = -1;
	};

	struct VansCompiledRigChain
	{
		std::string id;
		VansRigSolverKind solver = VansRigSolverKind::Limb;
		std::vector<int> boneIndices;
		int goalIndex = -1;
		glm::vec3 poleAxisLocal{ 0.0f, 0.0f, 1.0f };
		// Compiled into the chain-root parent's bind space. Runtime Limb IK uses
		// this stable frame so source-pose thigh twist cannot move the knee pole.
		glm::vec3 poleAxisParentLocal{ 0.0f, 0.0f, 1.0f };
		float softReachStartRatio = 0.97f;
		float maxStretchScale = 1.0f;
		std::vector<float> weights;
		std::vector<float> solveWeights;
		float maxStepDegrees = 180.0f;
		glm::vec3 forwardAxisLocal{ 0.0f, 0.0f, 1.0f };
		glm::vec3 upAxisLocal{ 0.0f, 1.0f, 0.0f };
		std::vector<float> restSegmentLengths;
	};

	struct VansCompiledRigJointLimit
	{
		int boneIndex = -1;
		VansJointLimitKind kind = VansJointLimitKind::Hinge;
		glm::vec3 axisLocal{ 1.0f, 0.0f, 0.0f };
		glm::vec3 swingReferenceAxisLocal{ 0.0f, 0.0f, 1.0f };
		glm::quat restLocalRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		float minDegrees = -180.0f;
		float maxDegrees = 180.0f;
		glm::vec2 swingLimitDegrees{ 180.0f, 180.0f };
	};

	struct VansCompiledRigContact
	{
		std::string id;
		int chainIndex = -1;
		int footBoneIndex = -1;
		int ballBoneIndex = -1;
		glm::vec3 soleForwardLocal{ 0.0f, 0.0f, 1.0f };
		glm::vec3 soleNormalLocal{ 0.0f, 1.0f, 0.0f };
		std::vector<VansRigSoleSample> soleSamplesLocal;
		glm::vec3 heelPivotLocal{ 0.0f };
		glm::vec3 ballPivotLocal{ 0.0f };
		glm::vec3 anklePivotLocal{ 0.0f };
		float sweepRadius = 0.035f;
	};

	struct VansCompiledRigSocket
	{
		std::string guid;
		std::string name;
		int boneIndex = -1;
		glm::mat4 localTransform{ 1.0f };
	};

	struct VansCompiledRigAttachmentProfile
	{
		std::string modelGuid;
		VansRigAttachmentParentKind parentKind = VansRigAttachmentParentKind::Socket;
		std::string anchorGuid;
		glm::vec3 positionLocal{ 0.0f };
		glm::quat rotationLocal{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 scaleLocal{ 1.0f };
	};

	struct VansCompiledAnimationRig
	{
		std::string name;
		std::string skeletonGuid;
		std::uint64_t skeletonSignature = 0;
		const Skeleton* skeleton = nullptr;
		glm::vec3 modelForward{ 0.0f, 0.0f, 1.0f };
		glm::vec3 modelUp{ 0.0f, 1.0f, 0.0f };
		std::unordered_map<std::string, int> semanticBoneIndices;
		std::vector<VansCompiledRigSocket> sockets;
		std::vector<VansCompiledRigAttachmentProfile> attachmentProfiles;
		std::vector<VansCompiledRigGoal> goals;
		std::vector<VansCompiledRigChain> chains;
		std::vector<VansCompiledRigJointLimit> jointLimits;
		std::vector<VansCompiledRigContact> contacts;
		std::unordered_map<std::string, int> goalIndexById;
		std::unordered_map<std::string, int> socketIndexByGuid;
		std::unordered_map<std::string, int> socketIndexByName;
		std::unordered_map<std::string, int> attachmentProfileIndexByKey;
		std::unordered_map<std::string, int> chainIndexById;
		std::unordered_map<std::string, int> contactIndexById;

		int FindGoal(const std::string& id) const;
		int FindSocketByGuid(const std::string& guid) const;
		int FindSocketByName(const std::string& name) const;
		const VansCompiledRigAttachmentProfile* FindAttachmentProfile(
			const std::string& modelGuid,
			VansRigAttachmentParentKind parentKind,
			const std::string& anchorGuid) const;
		int FindChain(const std::string& id) const;
		int FindContact(const std::string& id) const;
		const VansCompiledRigJointLimit* FindJointLimit(int boneIndex) const;
		bool BindSkeleton(const Skeleton& targetSkeleton, std::string& error);
	};

	class VansAnimationRigCompiler
	{
	public:
		static bool Compile(const VansAnimationRigAsset& asset,
		                    const Skeleton& skeleton,
		                    VansCompiledAnimationRig& outRig,
		                    std::string& error);
		static std::uint64_t ComputeSkeletonSignature(const Skeleton& skeleton);
	};
}
