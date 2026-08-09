#include "AnimationAuthoringBridge.h"

#include "../../AnimationCore/VansAnimatorIO.h"
#include "../../AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../../AssetCore/VansAssetGuid.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Vans::EditorAPI
{
	namespace
	{
		template<typename To, typename From>
		To BridgeEnum(From value)
		{
			return static_cast<To>(static_cast<int>(value));
		}

		static_assert(static_cast<int>(AnimGraphNodeType::FootPlacement) ==
			static_cast<int>(VansGraphics::AnimGraphNodeType::FootPlacement));
		static_assert(static_cast<int>(AnimatorParamType::Quaternion) ==
			static_cast<int>(VansGraphics::AnimatorParamType::Quaternion));
		static_assert(static_cast<int>(CompareOp::LessEqual) ==
			static_cast<int>(VansGraphics::CompareOp::LessEqual));
		static_assert(static_cast<int>(IKProfileType::Rope) ==
			static_cast<int>(VansGraphics::IKProfileType::Rope));
		static_assert(static_cast<int>(JointConstraintType::Locked) ==
			static_cast<int>(VansGraphics::JointConstraintType::Locked));
		static_assert(static_cast<int>(VansLayerSyncMode::SyncedGraph) ==
			static_cast<int>(VansGraphics::VansLayerSyncMode::SyncedGraph));

		AnimationVector3DTO ToDTO(const glm::vec3& value)
		{
			return { value.x, value.y, value.z };
		}

		AnimationQuaternionDTO ToDTO(const glm::quat& value)
		{
			return { value.x, value.y, value.z, value.w };
		}

		glm::vec3 ToNative(const AnimationVector3DTO& value)
		{
			return { value.x, value.y, value.z };
		}

		glm::quat ToNative(const AnimationQuaternionDTO& value)
		{
			return { value.w, value.x, value.y, value.z };
		}

		JointConstraintDTO ToDTO(const VansGraphics::JointConstraint& source)
		{
			JointConstraintDTO result;
			result.type = BridgeEnum<JointConstraintType>(source.type);
			result.localXAxis = ToDTO(source.localXAxis);
			result.localYAxis = ToDTO(source.localYAxis);
			result.localZAxis = ToDTO(source.localZAxis);
			result.minAngleX = source.minAngleX; result.maxAngleX = source.maxAngleX;
			result.minAngleY = source.minAngleY; result.maxAngleY = source.maxAngleY;
			result.minAngleZ = source.minAngleZ; result.maxAngleZ = source.maxAngleZ;
			result.coneAngleDeg = source.coneAngleDeg;
			result.stiffness = source.stiffness;
			result.restRotation = ToDTO(source.restRotation);
			return result;
		}

		VansGraphics::JointConstraint ToNative(const JointConstraintDTO& source)
		{
			VansGraphics::JointConstraint result;
			result.type = BridgeEnum<VansGraphics::JointConstraintType>(source.type);
			result.localXAxis = ToNative(source.localXAxis);
			result.localYAxis = ToNative(source.localYAxis);
			result.localZAxis = ToNative(source.localZAxis);
			result.minAngleX = source.minAngleX; result.maxAngleX = source.maxAngleX;
			result.minAngleY = source.minAngleY; result.maxAngleY = source.maxAngleY;
			result.minAngleZ = source.minAngleZ; result.maxAngleZ = source.maxAngleZ;
			result.coneAngleDeg = source.coneAngleDeg;
			result.stiffness = source.stiffness;
			result.restRotation = ToNative(source.restRotation);
			return result;
		}

		IKChainDefinitionDTO ToDTO(const VansGraphics::IKChainDefinition& source)
		{
			IKChainDefinitionDTO result;
			result.chainName = source.chainName;
			result.solverType = BridgeEnum<IKSolverType>(source.solverType);
			result.profileType = BridgeEnum<IKProfileType>(source.profileType);
			for (const auto& bone : source.bones)
			{
				IKBoneLinkDTO item;
				item.boneIndex = bone.boneIndex;
				item.boneName = bone.boneName;
				item.constraint = ToDTO(bone.constraint);
				item.stiffnessWeight = bone.stiffnessWeight;
				item.isEffector = bone.isEffector;
				result.bones.push_back(std::move(item));
			}
			result.maxIterations = source.maxIterations;
			result.positionTolerance = source.positionTolerance;
			result.rotationTolerance = source.rotationTolerance;
			result.poleVector = ToDTO(source.poleVector);
			result.poleWeight = source.poleWeight;
			result.poleSpace = BridgeEnum<IKCoordinateSpace>(source.poleSpace);
			result.poleReferenceBoneIndex = source.poleReferenceBoneIndex;
			result.poleReferenceBoneName = source.poleReferenceBoneName;
			result.enableRotationTarget = source.enableRotationTarget;
			result.rotationWeight = source.rotationWeight;
			result.maintainEffectorGlobalRotation = source.maintainEffectorGlobalRotation;
			result.allowStretch = source.allowStretch;
			result.startStretchRatio = source.startStretchRatio;
			result.maxStretchScale = source.maxStretchScale;
			result.solvePriority = source.solvePriority;
			return result;
		}

		VansGraphics::IKChainDefinition ToNative(const IKChainDefinitionDTO& source)
		{
			VansGraphics::IKChainDefinition result;
			result.chainName = source.chainName;
			result.solverType = BridgeEnum<VansGraphics::IKSolverType>(source.solverType);
			result.profileType = BridgeEnum<VansGraphics::IKProfileType>(source.profileType);
			for (const auto& bone : source.bones)
			{
				VansGraphics::IKBoneLink item;
				item.boneIndex = bone.boneIndex;
				item.boneName = bone.boneName;
				item.constraint = ToNative(bone.constraint);
				item.stiffnessWeight = bone.stiffnessWeight;
				item.isEffector = bone.isEffector;
				result.bones.push_back(std::move(item));
			}
			result.maxIterations = source.maxIterations;
			result.positionTolerance = source.positionTolerance;
			result.rotationTolerance = source.rotationTolerance;
			result.poleVector = ToNative(source.poleVector);
			result.poleWeight = source.poleWeight;
			result.poleSpace = BridgeEnum<VansGraphics::IKCoordinateSpace>(source.poleSpace);
			result.poleReferenceBoneIndex = source.poleReferenceBoneIndex;
			result.poleReferenceBoneName = source.poleReferenceBoneName;
			result.enableRotationTarget = source.enableRotationTarget;
			result.rotationWeight = source.rotationWeight;
			result.maintainEffectorGlobalRotation = source.maintainEffectorGlobalRotation;
			result.allowStretch = source.allowStretch;
			result.startStretchRatio = source.startStretchRatio;
			result.maxStretchScale = source.maxStretchScale;
			result.solvePriority = source.solvePriority;
			return result;
		}

		FootPlacementSettingsDTO ToDTO(const VansGraphics::FootPlacementSettings& source)
		{
			FootPlacementSettingsDTO r;
			r.enabled = source.enabled; r.probeOriginHeight = source.probeOriginHeight;
			r.probeLength = source.probeLength; r.footHalfLength = source.footHalfLength;
			r.footHalfWidth = source.footHalfWidth; r.ankleHeight = source.ankleHeight;
			r.fullContactHeight = source.fullContactHeight; r.contactFadeHeight = source.contactFadeHeight;
			r.maxStepUp = source.maxStepUp; r.maxStepDown = source.maxStepDown;
			r.maxSlopeDeg = source.maxSlopeDeg; r.pelvisMaxDrop = source.pelvisMaxDrop;
			r.pelvisSmoothTime = source.pelvisSmoothTime; r.offsetSmoothTime = source.offsetSmoothTime;
			r.normalSmoothTime = source.normalSmoothTime; r.weightSmoothTime = source.weightSmoothTime;
			r.globalWeightSmoothTime = source.globalWeightSmoothTime; r.ikWeight = source.ikWeight;
			r.rotationWeight = source.rotationWeight; r.maxLegExtensionRatio = source.maxLegExtensionRatio;
			r.poleSmoothTime = source.poleSmoothTime; r.kneePoleModelDir = ToDTO(source.kneePoleModelDir);
			r.kneePoleModelWeight = source.kneePoleModelWeight; r.debugVisualization = source.debugVisualization;
			r.collisionMask = source.collisionMask; r.airborneParameter = source.airborneParameter;
			r.bones.pelvis = source.bones.pelvis; r.bones.leftHip = source.bones.leftHip;
			r.bones.leftKnee = source.bones.leftKnee; r.bones.leftFoot = source.bones.leftFoot;
			r.bones.rightHip = source.bones.rightHip; r.bones.rightKnee = source.bones.rightKnee;
			r.bones.rightFoot = source.bones.rightFoot;
			return r;
		}

		VansGraphics::FootPlacementSettings ToNative(const FootPlacementSettingsDTO& source)
		{
			VansGraphics::FootPlacementSettings r;
			r.enabled = source.enabled; r.probeOriginHeight = source.probeOriginHeight;
			r.probeLength = source.probeLength; r.footHalfLength = source.footHalfLength;
			r.footHalfWidth = source.footHalfWidth; r.ankleHeight = source.ankleHeight;
			r.fullContactHeight = source.fullContactHeight; r.contactFadeHeight = source.contactFadeHeight;
			r.maxStepUp = source.maxStepUp; r.maxStepDown = source.maxStepDown;
			r.maxSlopeDeg = source.maxSlopeDeg; r.pelvisMaxDrop = source.pelvisMaxDrop;
			r.pelvisSmoothTime = source.pelvisSmoothTime; r.offsetSmoothTime = source.offsetSmoothTime;
			r.normalSmoothTime = source.normalSmoothTime; r.weightSmoothTime = source.weightSmoothTime;
			r.globalWeightSmoothTime = source.globalWeightSmoothTime; r.ikWeight = source.ikWeight;
			r.rotationWeight = source.rotationWeight; r.maxLegExtensionRatio = source.maxLegExtensionRatio;
			r.poleSmoothTime = source.poleSmoothTime; r.kneePoleModelDir = ToNative(source.kneePoleModelDir);
			r.kneePoleModelWeight = source.kneePoleModelWeight; r.debugVisualization = source.debugVisualization;
			r.collisionMask = source.collisionMask; r.airborneParameter = source.airborneParameter;
			r.bones.pelvis = source.bones.pelvis; r.bones.leftHip = source.bones.leftHip;
			r.bones.leftKnee = source.bones.leftKnee; r.bones.leftFoot = source.bones.leftFoot;
			r.bones.rightHip = source.bones.rightHip; r.bones.rightKnee = source.bones.rightKnee;
			r.bones.rightFoot = source.bones.rightFoot;
			return r;
		}

		TransitionConditionDTO ToDTO(const VansGraphics::TransitionCondition& source)
		{
			return { source.paramName, BridgeEnum<CompareOp>(source.op),
				source.floatVal, source.boolVal, source.intVal };
		}

		VansGraphics::TransitionCondition ToNative(const TransitionConditionDTO& source)
		{
			return { source.paramName, BridgeEnum<VansGraphics::CompareOp>(source.op),
				source.floatVal, source.boolVal, source.intVal };
		}

		AnimatorStateDTO ToDTO(const VansGraphics::AnimatorState& source)
		{
			return { source.name, source.clipName, source.speed, source.loop,
				source.rootMotion, source.startTime, source.endTime };
		}

		VansGraphics::AnimatorState ToNative(const AnimatorStateDTO& source)
		{
			return { source.name, source.clipName, source.speed, source.loop,
				source.rootMotion, source.startTime, source.endTime };
		}

		AnimatorTransitionDTO ToDTO(const VansGraphics::AnimatorTransition& source)
		{
			AnimatorTransitionDTO result;
			result.fromState = source.fromState; result.toState = source.toState;
			result.blendDuration = source.blendDuration; result.hasExitTime = source.hasExitTime;
			result.exitTime = source.exitTime;
			for (const auto& condition : source.conditions)
				result.conditions.push_back(ToDTO(condition));
			return result;
		}

		VansGraphics::AnimatorTransition ToNative(const AnimatorTransitionDTO& source)
		{
			VansGraphics::AnimatorTransition result;
			result.fromState = source.fromState; result.toState = source.toState;
			result.blendDuration = source.blendDuration; result.hasExitTime = source.hasExitTime;
			result.exitTime = source.exitTime;
			for (const auto& condition : source.conditions)
				result.conditions.push_back(ToNative(condition));
			return result;
		}

		std::unique_ptr<AnimationNodeDTO> ToDTO(const VansGraphics::VansAnimGraphNode& source)
		{
			auto result = AnimationGraphDTO::CreateNodeByType(
				BridgeEnum<AnimGraphNodeType>(source.GetType()));
			if (!result)
				return nullptr;
			result->m_NodeId = source.GetNodeId();
			result->m_Name = source.GetName();
			result->m_EditorPosX = source.m_EditorPosX;
			result->m_EditorPosY = source.m_EditorPosY;

			switch (source.GetType())
			{
			case VansGraphics::AnimGraphNodeType::Clip:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphClipNode&>(source);
				result->m_ClipName = n.m_ClipName; result->m_Speed = n.m_Speed; result->m_Loop = n.m_Loop;
				break;
			}
			case VansGraphics::AnimGraphNodeType::Blend:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphBlendNode&>(source);
				result->m_ParamName = n.m_ParamName; result->m_FixedAlpha = n.m_FixedAlpha; result->m_UseParam = n.m_UseParam;
				break;
			}
			case VansGraphics::AnimGraphNodeType::Blend1D:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphBlend1DNode&>(source);
				result->m_ParamName = n.m_ParamName; result->m_Thresholds = n.m_Thresholds;
				break;
			}
			case VansGraphics::AnimGraphNodeType::IfCondition:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphIfConditionNode&>(source);
				result->m_ParamName = n.m_ParamName; result->m_CompareOp = BridgeEnum<CompareOp>(n.m_CompareOp);
				result->m_FloatVal = n.m_FloatVal; result->m_BoolVal = n.m_BoolVal; result->m_IntVal = n.m_IntVal;
				break;
			}
			case VansGraphics::AnimGraphNodeType::Switch:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphSwitchNode&>(source);
				result->m_ParamName = n.m_ParamName; result->m_CaseCount = n.m_CaseCount;
				break;
			}
			case VansGraphics::AnimGraphNodeType::AdditiveBlend:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphAdditiveBlendNode&>(source);
				result->m_ParamName = n.m_ParamName; result->m_FixedWeight = n.m_FixedWeight; result->m_UseParam = n.m_UseParam;
				break;
			}
			case VansGraphics::AnimGraphNodeType::SpeedScale:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphSpeedScaleNode&>(source);
				result->m_ParamName = n.m_ParamName; result->m_FixedSpeed = n.m_FixedSpeed; result->m_UseParam = n.m_UseParam;
				break;
			}
			case VansGraphics::AnimGraphNodeType::StateMachine:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphStateMachineNode&>(source);
				for (const auto& state : n.m_States) result->m_States.push_back(ToDTO(state));
				for (const auto& transition : n.m_Transitions) result->m_Transitions.push_back(ToDTO(transition));
				result->m_DefaultStateName = n.m_DefaultStateName;
				break;
			}
			case VansGraphics::AnimGraphNodeType::MotionMatching:
				result->m_EnableFallbackInput = static_cast<const VansGraphics::AnimGraphMotionMatchingNode&>(source).m_EnableFallbackInput; break;
			case VansGraphics::AnimGraphNodeType::Slot:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphSlotNode&>(source);
				result->m_SlotId = n.m_SlotId; result->m_EnableFallbackInput = n.m_EnableFallbackInput;
				break;
			}
			case VansGraphics::AnimGraphNodeType::IK:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphIKNode&>(source);
				result->m_Chain = ToDTO(n.m_Chain); result->m_TargetPosParamName = n.m_TargetPosParamName;
				result->m_TargetRotParamName = n.m_TargetRotParamName; result->m_WeightParamName = n.m_WeightParamName;
				result->m_UseFixedTarget = n.m_UseFixedTarget; result->m_FixedTargetPos = ToDTO(n.m_FixedTargetPos);
				result->m_FixedTargetRot = ToDTO(n.m_FixedTargetRot); result->m_FixedWeight = n.m_FixedWeight;
				result->m_TargetPositionSpace = BridgeEnum<IKCoordinateSpace>(n.m_TargetPositionSpace);
				result->m_TargetRotationSpace = BridgeEnum<IKCoordinateSpace>(n.m_TargetRotationSpace);
				result->m_TargetReferenceBoneName = n.m_TargetReferenceBoneName;
				break;
			}
			case VansGraphics::AnimGraphNodeType::TwoBoneIK:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphTwoBoneIKNode&>(source);
				result->m_RootBoneName = n.m_RootBoneName; result->m_MidBoneName = n.m_MidBoneName; result->m_TipBoneName = n.m_TipBoneName;
				result->m_UseLegProfile = n.m_UseLegProfile; result->m_IsRightSide = n.m_IsRightSide;
				result->m_HingeMinAngle = n.m_HingeMinAngle; result->m_HingeMaxAngle = n.m_HingeMaxAngle; result->m_ConeAngle = n.m_ConeAngle;
				result->m_UsePoleVector = n.m_UsePoleVector; result->m_PoleVector = ToDTO(n.m_PoleVector); result->m_PoleWeight = n.m_PoleWeight;
				result->m_TargetPosParamName = n.m_TargetPosParamName; result->m_TargetRotParamName = n.m_TargetRotParamName; result->m_WeightParamName = n.m_WeightParamName;
				result->m_UseFixedTarget = n.m_UseFixedTarget; result->m_FixedTargetPos = ToDTO(n.m_FixedTargetPos); result->m_FixedTargetRot = ToDTO(n.m_FixedTargetRot);
				result->m_FixedWeight = n.m_FixedWeight; result->m_EnableRotationTarget = n.m_EnableRotationTarget; result->m_RotationWeight = n.m_RotationWeight;
				result->m_TargetPositionSpace = BridgeEnum<IKCoordinateSpace>(n.m_TargetPositionSpace); result->m_TargetRotationSpace = BridgeEnum<IKCoordinateSpace>(n.m_TargetRotationSpace);
				result->m_TargetReferenceBoneName = n.m_TargetReferenceBoneName; result->m_PoleSpace = BridgeEnum<IKCoordinateSpace>(n.m_PoleSpace);
				result->m_PoleReferenceBoneName = n.m_PoleReferenceBoneName; result->m_MaintainEffectorGlobalRotation = n.m_MaintainEffectorGlobalRotation;
				result->m_AllowStretch = n.m_AllowStretch; result->m_StartStretchRatio = n.m_StartStretchRatio; result->m_MaxStretchScale = n.m_MaxStretchScale;
				break;
			}
			case VansGraphics::AnimGraphNodeType::LookAt:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphLookAtNode&>(source);
				result->m_BoneNames = n.m_BoneNames; result->m_BoneWeights = n.m_BoneWeights; result->m_MaxAnglePerBoneDeg = n.m_MaxAnglePerBoneDeg;
				result->m_ForwardAxis = ToDTO(n.m_ForwardAxis); result->m_WorldForward = ToDTO(n.m_WorldForward); result->m_ModelUp = ToDTO(n.m_ModelUp); result->m_UpWeight = n.m_UpWeight;
				result->m_TargetPosParamName = n.m_TargetPosParamName; result->m_WeightParamName = n.m_WeightParamName; result->m_UseFixedTarget = n.m_UseFixedTarget;
				result->m_FixedTargetPos = ToDTO(n.m_FixedTargetPos); result->m_FixedWeight = n.m_FixedWeight;
				result->m_TargetPositionSpace = BridgeEnum<IKCoordinateSpace>(n.m_TargetPositionSpace); result->m_TargetReferenceBoneName = n.m_TargetReferenceBoneName;
				break;
			}
			case VansGraphics::AnimGraphNodeType::FootPlacement:
				result->m_Settings = ToDTO(static_cast<const VansGraphics::AnimGraphFootPlacementNode&>(source).m_Settings); break;
			default: break;
			}
			return result;
		}

		std::unique_ptr<VansGraphics::VansAnimGraphNode> ToNative(const AnimationNodeDTO& source)
		{
			auto result = VansGraphics::VansAnimGraph::CreateNodeByType(
				BridgeEnum<VansGraphics::AnimGraphNodeType>(source.m_Type));
			if (!result)
				return nullptr;
			result->SetName(source.m_Name);
			result->m_EditorPosX = source.m_EditorPosX;
			result->m_EditorPosY = source.m_EditorPosY;

			switch (result->GetType())
			{
			case VansGraphics::AnimGraphNodeType::Clip:
			{
				auto& n = static_cast<VansGraphics::AnimGraphClipNode&>(*result);
				n.m_ClipName = source.m_ClipName; n.m_Speed = source.m_Speed; n.m_Loop = source.m_Loop;
				break;
			}
			case VansGraphics::AnimGraphNodeType::Blend:
			{
				auto& n = static_cast<VansGraphics::AnimGraphBlendNode&>(*result);
				n.m_ParamName = source.m_ParamName; n.m_FixedAlpha = source.m_FixedAlpha; n.m_UseParam = source.m_UseParam;
				break;
			}
			case VansGraphics::AnimGraphNodeType::Blend1D:
			{
				auto& n = static_cast<VansGraphics::AnimGraphBlend1DNode&>(*result);
				n.m_ParamName = source.m_ParamName; n.m_Thresholds = source.m_Thresholds;
				break;
			}
			case VansGraphics::AnimGraphNodeType::IfCondition:
			{
				auto& n = static_cast<VansGraphics::AnimGraphIfConditionNode&>(*result);
				n.m_ParamName = source.m_ParamName; n.m_CompareOp = BridgeEnum<VansGraphics::CompareOp>(source.m_CompareOp);
				n.m_FloatVal = source.m_FloatVal; n.m_BoolVal = source.m_BoolVal; n.m_IntVal = source.m_IntVal;
				break;
			}
			case VansGraphics::AnimGraphNodeType::Switch:
			{
				auto& n = static_cast<VansGraphics::AnimGraphSwitchNode&>(*result);
				n.m_ParamName = source.m_ParamName; n.m_CaseCount = source.m_CaseCount;
				break;
			}
			case VansGraphics::AnimGraphNodeType::AdditiveBlend:
			{
				auto& n = static_cast<VansGraphics::AnimGraphAdditiveBlendNode&>(*result);
				n.m_ParamName = source.m_ParamName; n.m_FixedWeight = source.m_FixedWeight; n.m_UseParam = source.m_UseParam;
				break;
			}
			case VansGraphics::AnimGraphNodeType::SpeedScale:
			{
				auto& n = static_cast<VansGraphics::AnimGraphSpeedScaleNode&>(*result);
				n.m_ParamName = source.m_ParamName; n.m_FixedSpeed = source.m_FixedSpeed; n.m_UseParam = source.m_UseParam;
				break;
			}
			case VansGraphics::AnimGraphNodeType::StateMachine:
			{
				auto& n = static_cast<VansGraphics::AnimGraphStateMachineNode&>(*result);
				for (const auto& state : source.m_States) n.m_States.push_back(ToNative(state));
				for (const auto& transition : source.m_Transitions) n.m_Transitions.push_back(ToNative(transition));
				n.m_DefaultStateName = source.m_DefaultStateName;
				break;
			}
			case VansGraphics::AnimGraphNodeType::MotionMatching:
				static_cast<VansGraphics::AnimGraphMotionMatchingNode&>(*result).m_EnableFallbackInput = source.m_EnableFallbackInput; break;
			case VansGraphics::AnimGraphNodeType::Slot:
			{
				auto& n = static_cast<VansGraphics::AnimGraphSlotNode&>(*result);
				n.m_SlotId = source.m_SlotId; n.m_EnableFallbackInput = source.m_EnableFallbackInput;
				break;
			}
			case VansGraphics::AnimGraphNodeType::IK:
			{
				auto& n = static_cast<VansGraphics::AnimGraphIKNode&>(*result);
				n.m_Chain = ToNative(source.m_Chain); n.m_TargetPosParamName = source.m_TargetPosParamName;
				n.m_TargetRotParamName = source.m_TargetRotParamName; n.m_WeightParamName = source.m_WeightParamName;
				n.m_UseFixedTarget = source.m_UseFixedTarget; n.m_FixedTargetPos = ToNative(source.m_FixedTargetPos);
				n.m_FixedTargetRot = ToNative(source.m_FixedTargetRot); n.m_FixedWeight = source.m_FixedWeight;
				n.m_TargetPositionSpace = BridgeEnum<VansGraphics::IKCoordinateSpace>(source.m_TargetPositionSpace);
				n.m_TargetRotationSpace = BridgeEnum<VansGraphics::IKCoordinateSpace>(source.m_TargetRotationSpace);
				n.m_TargetReferenceBoneName = source.m_TargetReferenceBoneName;
				break;
			}
			case VansGraphics::AnimGraphNodeType::TwoBoneIK:
			{
				auto& n = static_cast<VansGraphics::AnimGraphTwoBoneIKNode&>(*result);
				n.m_RootBoneName = source.m_RootBoneName; n.m_MidBoneName = source.m_MidBoneName; n.m_TipBoneName = source.m_TipBoneName;
				n.m_UseLegProfile = source.m_UseLegProfile; n.m_IsRightSide = source.m_IsRightSide;
				n.m_HingeMinAngle = source.m_HingeMinAngle; n.m_HingeMaxAngle = source.m_HingeMaxAngle; n.m_ConeAngle = source.m_ConeAngle;
				n.m_UsePoleVector = source.m_UsePoleVector; n.m_PoleVector = ToNative(source.m_PoleVector); n.m_PoleWeight = source.m_PoleWeight;
				n.m_TargetPosParamName = source.m_TargetPosParamName; n.m_TargetRotParamName = source.m_TargetRotParamName; n.m_WeightParamName = source.m_WeightParamName;
				n.m_UseFixedTarget = source.m_UseFixedTarget; n.m_FixedTargetPos = ToNative(source.m_FixedTargetPos); n.m_FixedTargetRot = ToNative(source.m_FixedTargetRot);
				n.m_FixedWeight = source.m_FixedWeight; n.m_EnableRotationTarget = source.m_EnableRotationTarget; n.m_RotationWeight = source.m_RotationWeight;
				n.m_TargetPositionSpace = BridgeEnum<VansGraphics::IKCoordinateSpace>(source.m_TargetPositionSpace); n.m_TargetRotationSpace = BridgeEnum<VansGraphics::IKCoordinateSpace>(source.m_TargetRotationSpace);
				n.m_TargetReferenceBoneName = source.m_TargetReferenceBoneName; n.m_PoleSpace = BridgeEnum<VansGraphics::IKCoordinateSpace>(source.m_PoleSpace);
				n.m_PoleReferenceBoneName = source.m_PoleReferenceBoneName; n.m_MaintainEffectorGlobalRotation = source.m_MaintainEffectorGlobalRotation;
				n.m_AllowStretch = source.m_AllowStretch; n.m_StartStretchRatio = source.m_StartStretchRatio; n.m_MaxStretchScale = source.m_MaxStretchScale;
				break;
			}
			case VansGraphics::AnimGraphNodeType::LookAt:
			{
				auto& n = static_cast<VansGraphics::AnimGraphLookAtNode&>(*result);
				n.m_BoneNames = source.m_BoneNames; n.m_BoneWeights = source.m_BoneWeights; n.m_MaxAnglePerBoneDeg = source.m_MaxAnglePerBoneDeg;
				n.m_ForwardAxis = ToNative(source.m_ForwardAxis); n.m_WorldForward = ToNative(source.m_WorldForward); n.m_ModelUp = ToNative(source.m_ModelUp); n.m_UpWeight = source.m_UpWeight;
				n.m_TargetPosParamName = source.m_TargetPosParamName; n.m_WeightParamName = source.m_WeightParamName; n.m_UseFixedTarget = source.m_UseFixedTarget;
				n.m_FixedTargetPos = ToNative(source.m_FixedTargetPos); n.m_FixedWeight = source.m_FixedWeight;
				n.m_TargetPositionSpace = BridgeEnum<VansGraphics::IKCoordinateSpace>(source.m_TargetPositionSpace); n.m_TargetReferenceBoneName = source.m_TargetReferenceBoneName;
				break;
			}
			case VansGraphics::AnimGraphNodeType::FootPlacement:
				static_cast<VansGraphics::AnimGraphFootPlacementNode&>(*result).m_Settings = ToNative(source.m_Settings); break;
			default: break;
			}
			return result;
		}

		std::unique_ptr<AnimationGraphDTO> ToDTO(const VansGraphics::VansAnimGraph& source)
		{
			auto result = std::make_unique<AnimationGraphDTO>();
			result->entryNodeId = source.GetEntryNodeId();
			result->outputNodeId = source.GetOutputNodeId();
			for (const auto& [id, node] : source.GetNodes())
			{
				auto converted = ToDTO(*node);
				if (converted)
				{
					result->nodes.emplace(id, std::move(converted));
					result->nextNodeId = std::max(result->nextNodeId, id + 1);
				}
			}
			for (const auto& link : source.GetLinks())
			{
				result->links.push_back({ link.linkId, link.fromNodeId, link.fromPinIndex, link.toNodeId, link.toPinIndex });
				result->nextLinkId = std::max(result->nextLinkId, link.linkId + 1);
			}
			return result;
		}

		std::unique_ptr<VansGraphics::VansAnimGraph> ToNative(
			const AnimationGraphDTO& source, std::string& error)
		{
			auto result = std::make_unique<VansGraphics::VansAnimGraph>();
			std::vector<int> nodeIds;
			nodeIds.reserve(source.nodes.size());
			for (const auto& [id, ignored] : source.nodes) nodeIds.push_back(id);
			std::sort(nodeIds.begin(), nodeIds.end());
			for (int id : nodeIds)
			{
				const auto& node = source.nodes.at(id);
				if (!node || node->m_NodeId != id || !result->AddNodeWithId(ToNative(*node), id))
				{
					error = "Animator DTO contains an invalid or duplicate node identity";
					return nullptr;
				}
			}
			std::vector<AnimGraphLinkDTO> links = source.links;
			std::sort(links.begin(), links.end(), [](const auto& a, const auto& b) { return a.linkId < b.linkId; });
			for (const auto& link : links)
			{
				if (!result->AddLinkWithId(link.linkId, link.fromNodeId, link.fromPinIndex,
					link.toNodeId, link.toPinIndex))
				{
					error = "Animator DTO contains an invalid or duplicate link identity";
					return nullptr;
				}
			}
			return result;
		}

		std::unique_ptr<AnimatorDocumentDTO> ToDTO(const VansGraphics::AnimatorAssetData& source)
		{
			auto result = std::make_unique<AnimatorDocumentDTO>();
			result->name = source.name;
			for (const auto& parameter : source.parameters)
			{
				AnimatorParameterDTO item;
				item.name = parameter.name; item.type = BridgeEnum<AnimatorParamType>(parameter.type);
				item.floatVal = parameter.floatVal; item.boolVal = parameter.boolVal; item.intVal = parameter.intVal;
				item.vec3Val = ToDTO(parameter.vec3Val); item.quatVal = ToDTO(parameter.quatVal);
				result->parameters.push_back(std::move(item));
			}
			for (const auto& clip : source.clipRefs)
				result->clipRefs.push_back({ clip.name, clip.assetGuid, clip.pathHint });
			for (const auto& graph : source.graphs)
			{
				AnimatorGraphDTO item;
				item.id = graph.id; item.name = graph.name;
				item.role = graph.role == VansGraphics::AnimatorGraphAsset::Role::Pose
					? AnimatorGraphRole::Pose : AnimatorGraphRole::TargetPostProcess;
				if (graph.graph) item.graph = ToDTO(*graph.graph);
				result->graphs.push_back(std::move(item));
			}
			for (const auto& layer : source.layers)
			{
				AnimationLayerDTO item;
				item.id = layer.id; item.name = layer.name; item.graphId = layer.graphId;
				item.kind = BridgeEnum<VansAnimationLayerKind>(layer.kind);
				item.maskGuid = layer.maskGuid; item.maskPathHint = layer.maskPathHint;
				item.blendMode = BridgeEnum<VansLayerBlendMode>(layer.blendMode);
				item.rotationSpace = BridgeEnum<VansRotationBlendSpace>(layer.rotationSpace);
				item.additiveReference = BridgeEnum<VansAdditiveReferenceMode>(layer.additiveReference);
				item.referenceClipName = layer.referenceClipName; item.referenceTime = layer.referenceTime;
				item.weightParameter = layer.weightParameter; item.fixedWeight = layer.fixedWeight;
				item.useWeightParameter = layer.useWeightParameter; item.weightSmoothingTime = layer.weightSmoothingTime;
				item.rootMotion = BridgeEnum<VansLayerRootMotionMode>(layer.rootMotion);
				item.curves = BridgeEnum<VansLayerCurveMode>(layer.curves);
				item.events = BridgeEnum<VansLayerEventMode>(layer.events);
				item.nodeTracks = BridgeEnum<VansLayerNodeTrackMode>(layer.nodeTracks);
				item.sync = BridgeEnum<VansLayerSyncMode>(layer.sync); item.syncLeaderLayerId = layer.syncLeaderLayerId;
				item.eventWeightThreshold = layer.eventWeightThreshold; item.enabled = layer.enabled;
				item.updateWhenWeightIsZero = layer.updateWhenWeightIsZero;
				result->layers.push_back(std::move(item));
			}
			for (const auto& slot : source.slots)
			{
				AnimationSlotDTO item;
				item.id = slot.id; item.name = slot.name; item.layerId = slot.layerId; item.slotNodeId = slot.slotNodeId;
				item.concurrency = BridgeEnum<VansSlotConcurrency>(slot.concurrency); item.maxQueueDepth = slot.maxQueueDepth;
				item.defaultBlendIn = slot.defaultBlendIn; item.defaultBlendOut = slot.defaultBlendOut; item.interruptible = slot.interruptible;
				result->slots.push_back(std::move(item));
			}
			result->editor.previewModelGuid = source.editor.previewModelGuid;
			result->editor.previewModelPathHint = source.editor.previewModelPathHint;
			return result;
		}

		bool ToNative(const AnimatorDocumentDTO& source, VansGraphics::AnimatorAssetData& result,
			std::string& error)
		{
			result.name = source.name;
			for (const auto& parameter : source.parameters)
			{
				VansGraphics::AnimatorParameter item;
				item.name = parameter.name; item.type = BridgeEnum<VansGraphics::AnimatorParamType>(parameter.type);
				item.floatVal = parameter.floatVal; item.boolVal = parameter.boolVal; item.intVal = parameter.intVal;
				item.vec3Val = ToNative(parameter.vec3Val); item.quatVal = ToNative(parameter.quatVal);
				result.parameters.push_back(std::move(item));
			}
			for (const auto& clip : source.clipRefs)
				result.clipRefs.push_back({ clip.name, clip.assetGuid, clip.pathHint });
			for (const auto& graph : source.graphs)
			{
				if (!graph.graph) { error = "Animator DTO graph is null"; return false; }
				VansGraphics::AnimatorGraphAsset item;
				item.id = graph.id; item.name = graph.name;
				item.role = graph.role == AnimatorGraphRole::Pose
					? VansGraphics::AnimatorGraphAsset::Role::Pose
					: VansGraphics::AnimatorGraphAsset::Role::TargetPostProcess;
				item.graph = ToNative(*graph.graph, error);
				if (!item.graph) return false;
				result.graphs.push_back(std::move(item));
			}
			for (const auto& layer : source.layers)
			{
				VansGraphics::VansAnimationLayerDefinition item;
				item.id = layer.id; item.name = layer.name; item.graphId = layer.graphId;
				item.kind = BridgeEnum<VansGraphics::VansAnimationLayerKind>(layer.kind);
				item.maskGuid = layer.maskGuid; item.maskPathHint = layer.maskPathHint;
				item.blendMode = BridgeEnum<VansGraphics::VansLayerBlendMode>(layer.blendMode);
				item.rotationSpace = BridgeEnum<VansGraphics::VansRotationBlendSpace>(layer.rotationSpace);
				item.additiveReference = BridgeEnum<VansGraphics::VansAdditiveReferenceMode>(layer.additiveReference);
				item.referenceClipName = layer.referenceClipName; item.referenceTime = layer.referenceTime;
				item.weightParameter = layer.weightParameter; item.fixedWeight = layer.fixedWeight;
				item.useWeightParameter = layer.useWeightParameter; item.weightSmoothingTime = layer.weightSmoothingTime;
				item.rootMotion = BridgeEnum<VansGraphics::VansLayerRootMotionMode>(layer.rootMotion);
				item.curves = BridgeEnum<VansGraphics::VansLayerCurveMode>(layer.curves);
				item.events = BridgeEnum<VansGraphics::VansLayerEventMode>(layer.events);
				item.nodeTracks = BridgeEnum<VansGraphics::VansLayerNodeTrackMode>(layer.nodeTracks);
				item.sync = BridgeEnum<VansGraphics::VansLayerSyncMode>(layer.sync); item.syncLeaderLayerId = layer.syncLeaderLayerId;
				item.eventWeightThreshold = layer.eventWeightThreshold; item.enabled = layer.enabled;
				item.updateWhenWeightIsZero = layer.updateWhenWeightIsZero;
				result.layers.push_back(std::move(item));
			}
			for (const auto& slot : source.slots)
			{
				VansGraphics::VansAnimationSlotDefinition item;
				item.id = slot.id; item.name = slot.name; item.layerId = slot.layerId; item.slotNodeId = slot.slotNodeId;
				item.concurrency = BridgeEnum<VansGraphics::VansSlotConcurrency>(slot.concurrency); item.maxQueueDepth = slot.maxQueueDepth;
				item.defaultBlendIn = slot.defaultBlendIn; item.defaultBlendOut = slot.defaultBlendOut; item.interruptible = slot.interruptible;
				result.slots.push_back(std::move(item));
			}
			result.editor.previewModelGuid = source.editor.previewModelGuid;
			result.editor.previewModelPathHint = source.editor.previewModelPathHint;
			return true;
		}

		BoneMaskDocumentDTO ToDTO(const VansGraphics::VansBoneMaskAsset& source)
		{
			BoneMaskDocumentDTO result;
			result.id = source.id; result.name = source.name;
			result.previewSkeletonGuid = source.previewSkeletonGuid;
			result.previewSkeletonPathHint = source.previewSkeletonPathHint;
			result.defaultWeight = source.defaultWeight;
			for (const auto& rule : source.branchRules)
				result.branchRules.push_back({ rule.id, BridgeEnum<BoneMaskRuleMode>(rule.mode),
					rule.rootBone, rule.includeDescendants, rule.maxDepth, rule.rootWeight,
					rule.endWeight, BridgeEnum<BoneMaskFalloff>(rule.falloff) });
			result.explicitWeights = source.explicitWeights;
			result.editorExpandedBones = source.editorExpandedBones;
			return result;
		}

		VansGraphics::VansBoneMaskAsset ToNative(const BoneMaskDocumentDTO& source)
		{
			VansGraphics::VansBoneMaskAsset result;
			result.id = source.id; result.name = source.name;
			result.previewSkeletonGuid = source.previewSkeletonGuid;
			result.previewSkeletonPathHint = source.previewSkeletonPathHint;
			result.defaultWeight = source.defaultWeight;
			for (const auto& rule : source.branchRules)
				result.branchRules.push_back({ rule.id, BridgeEnum<VansGraphics::VansBoneMaskRuleMode>(rule.mode),
					rule.rootBone, rule.includeDescendants, rule.maxDepth, rule.rootWeight,
					rule.endWeight, BridgeEnum<VansGraphics::VansBoneMaskFalloff>(rule.falloff) });
			result.explicitWeights = source.explicitWeights;
			result.editorExpandedBones = source.editorExpandedBones;
			return result;
		}

		std::filesystem::path MakeUniquePath(const std::filesystem::path& directory,
			const std::string& baseName, const std::string& extension)
		{
			for (int index = 0; index < 1000; ++index)
			{
				const std::string suffix = index == 0 ? "" : " " + std::to_string(index);
				const auto candidate = directory / (baseName + suffix + extension);
				if (!std::filesystem::exists(candidate)) return candidate;
			}
			return {};
		}
	}

	AnimatorDocumentDecodeResult AnimationAuthoringBridge::DecodeAnimator(const std::string& canonicalJson)
	{
		AnimatorDocumentDecodeResult result;
		try
		{
			const auto root = nlohmann::json::parse(canonicalJson);
			VansGraphics::AnimatorAssetData native;
			if (!VansGraphics::VansAnimatorIO::DeserializeFromJsonObject(root, native, result.message))
				return result;
			result.document = ToDTO(native);
			result.success = static_cast<bool>(result.document);
		}
		catch (const std::exception& exception) { result.message = exception.what(); }
		return result;
	}

	AnimatorDocumentEncodeResult AnimationAuthoringBridge::EncodeAnimator(const AnimatorDocumentDTO& document)
	{
		AnimatorDocumentEncodeResult result;
		VansGraphics::AnimatorAssetData native;
		if (!ToNative(document, native, result.message)) return result;
		nlohmann::json root;
		if (!VansGraphics::VansAnimatorIO::SerializeToJsonObject(native, root, result.message)) return result;
		result.canonicalJson = root.dump(4);
		result.success = true;
		return result;
	}

	BoneMaskDocumentDecodeResult AnimationAuthoringBridge::DecodeBoneMask(const std::string& canonicalJson)
	{
		BoneMaskDocumentDecodeResult result;
		try
		{
			const auto root = nlohmann::json::parse(canonicalJson);
			VansGraphics::VansBoneMaskAsset native;
			if (!VansGraphics::VansBoneMaskStorage::DeserializeFromJsonObject(root, native, result.message))
				return result;
			result.document = ToDTO(native);
			result.success = true;
		}
		catch (const std::exception& exception) { result.message = exception.what(); }
		return result;
	}

	BoneMaskDocumentEncodeResult AnimationAuthoringBridge::EncodeBoneMask(const BoneMaskDocumentDTO& document)
	{
		BoneMaskDocumentEncodeResult result;
		nlohmann::json root;
		if (!VansGraphics::VansBoneMaskStorage::SerializeToJsonObject(ToNative(document), root, result.message))
			return result;
		result.canonicalJson = root.dump(4);
		result.success = true;
		return result;
	}

	BoneMaskCompileResult AnimationAuthoringBridge::CompileBoneMask(
		const BoneMaskDocumentDTO& document, const AssetSkeletonSnapshot& snapshot)
	{
		BoneMaskCompileResult result;
		if (!snapshot.available || snapshot.bones.empty())
		{
			result.message = snapshot.error.empty() ? "Preview skeleton is unavailable" : snapshot.error;
			return result;
		}
		VansGraphics::Skeleton skeleton;
		skeleton.bones.resize(snapshot.bones.size());
		for (std::size_t index = 0; index < snapshot.bones.size(); ++index)
		{
			auto& bone = skeleton.bones[index];
			bone.id = static_cast<int>(index); bone.name = snapshot.bones[index].name;
			bone.parentIndex = snapshot.bones[index].parentIndex;
			skeleton.boneNameToIndex[bone.name] = static_cast<int>(index);
		}
		for (std::size_t index = 0; index < skeleton.bones.size(); ++index)
		{
			const int parent = skeleton.bones[index].parentIndex;
			if (parent >= 0 && parent < static_cast<int>(skeleton.bones.size()))
				skeleton.bones[static_cast<std::size_t>(parent)].children.push_back(static_cast<int>(index));
		}
		skeleton.BuildTopologicalOrder();
		const auto compiled = VansGraphics::VansBoneMaskCompiler::Compile(ToNative(document), skeleton);
		result.valid = compiled.valid; result.allZero = compiled.allZero; result.allOne = compiled.allOne;
		result.rootWeight = compiled.rootWeight; result.weights = compiled.weights; result.activeBones = compiled.activeBones;
		for (const auto& diagnostic : compiled.diagnostics)
			result.diagnostics.push_back({ BridgeEnum<BoneMaskDiagnosticSeverity>(diagnostic.severity),
				diagnostic.ruleId, diagnostic.message });
		if (!result.valid) result.message = "Bone mask compilation failed";
		return result;
	}

	AnimationAuthoringAssetCreateResult AnimationAuthoringBridge::CreateAsset(
		const AnimationAuthoringAssetCreateRequest& request)
	{
		AnimationAuthoringAssetCreateResult result;
		const std::filesystem::path directory(request.directoryPath);
		if (directory.empty() || !std::filesystem::is_directory(directory))
		{
			result.message = "Choose a project folder before creating an animation asset";
			return result;
		}
		std::string error;
		if (request.kind == AnimationAuthoringAssetKind::BoneMask)
		{
			const auto path = MakeUniquePath(directory, "New Bone Mask", ".vbonemask");
			VansGraphics::VansBoneMaskAsset asset;
			asset.id = Vans::VansAssetGuid::New().ToString(); asset.name = path.stem().string();
			result.success = !path.empty() && VansGraphics::VansBoneMaskStorage::SaveAtomic(path, asset, error);
			result.assetPath = result.success ? path.string() : std::string{};
		}
		else
		{
			const auto path = MakeUniquePath(directory, "New Animator", ".vanimator");
			VansGraphics::AnimatorAssetData asset; asset.name = path.stem().string();
			VansGraphics::AnimatorGraphAsset graph; graph.id = Vans::VansAssetGuid::New().ToString();
			graph.name = "Base Graph"; graph.role = VansGraphics::AnimatorGraphAsset::Role::Pose;
			graph.graph = std::make_unique<VansGraphics::VansAnimGraph>();
			const int entry = graph.graph->AddNode(VansGraphics::VansAnimGraph::CreateNodeByType(VansGraphics::AnimGraphNodeType::Entry));
			const int output = graph.graph->AddNode(VansGraphics::VansAnimGraph::CreateNodeByType(VansGraphics::AnimGraphNodeType::Output));
			graph.graph->GetNode(entry)->m_EditorPosX = 40.0f; graph.graph->GetNode(output)->m_EditorPosX = 360.0f;
			graph.graph->AddLink(entry, 0, output, 0); const std::string graphId = graph.id; asset.graphs.push_back(std::move(graph));
			VansGraphics::VansAnimationLayerDefinition base; base.id = Vans::VansAssetGuid::New().ToString();
			base.name = "Base"; base.graphId = graphId; base.kind = VansGraphics::VansAnimationLayerKind::Base; asset.layers.push_back(std::move(base));
			result.success = !path.empty() && VansGraphics::VansAnimatorIO::Save(path.string(), asset, error);
			result.assetPath = result.success ? path.string() : std::string{};
		}
		result.message = result.success ? "Animation asset created" : error;
		return result;
	}
}
