#include "AnimationAuthoringBridge.h"

#include "../../AnimationCore/VansAnimatorIO.h"
#include "../../AnimationCore/Storage/VansAnimationRigStorage.h"
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

		static_assert(static_cast<int>(AnimGraphNodeType::ChainIK) ==
			static_cast<int>(VansGraphics::AnimGraphNodeType::ChainIK));
		static_assert(static_cast<int>(AnimatorParamType::Quaternion) ==
			static_cast<int>(VansGraphics::AnimatorParamType::Quaternion));
		static_assert(static_cast<int>(CompareOp::LessEqual) ==
			static_cast<int>(VansGraphics::CompareOp::LessEqual));
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

		AnimationGoalDefinitionDTO ToDTO(const VansGraphics::VansGraphGoalDefinition& source)
		{
			AnimationGoalDefinitionDTO result;
			result.goalId = source.goalId;
			result.source = BridgeEnum<AnimationGoalSource>(source.source);
			result.binding = source.binding;
			result.positionParameter = source.positionParameter;
			result.rotationParameter = source.rotationParameter;
			result.weightParameter = source.weightParameter;
			result.fixedPositionModel = ToDTO(source.fixedPositionModel);
			result.fixedRotationModel = ToDTO(source.fixedRotationModel);
			result.fixedPositionWeight = source.fixedPositionWeight;
			result.fixedRotationWeight = source.fixedRotationWeight;
			return result;
		}

		VansGraphics::VansGraphGoalDefinition ToNative(const AnimationGoalDefinitionDTO& source)
		{
			VansGraphics::VansGraphGoalDefinition result;
			result.goalId = source.goalId;
			result.source = BridgeEnum<VansGraphics::VansGraphGoalSource>(source.source);
			result.binding = source.binding;
			result.positionParameter = source.positionParameter;
			result.rotationParameter = source.rotationParameter;
			result.weightParameter = source.weightParameter;
			result.fixedPositionModel = ToNative(source.fixedPositionModel);
			result.fixedRotationModel = ToNative(source.fixedRotationModel);
			result.fixedPositionWeight = source.fixedPositionWeight;
			result.fixedRotationWeight = source.fixedRotationWeight;
			return result;
		}

		AnimationGroundingSettingsDTO ToDTO(const VansGraphics::VansGroundingSettings& source)
		{
			AnimationGroundingSettingsDTO result;
			result.contacts = source.contacts;
			result.query.profile = source.query.profile;
			result.query.startDistanceAgainstApproach = source.query.startDistanceAgainstApproach;
			result.query.endDistanceAlongApproach = source.query.endDistanceAlongApproach;
			result.query.maxStepUp = source.query.maxStepUp;
			result.query.maxStepDown = source.query.maxStepDown;
			result.query.maxSlopeDegrees = source.query.maxSlopeDegrees;
			result.query.maxPlaneResidual = source.query.maxPlaneResidual;
			result.query.maxNormalDeviationDegrees = source.query.maxNormalDeviationDegrees;
			result.plantSignal = source.plantSignal;
			result.plant.lockEnabled = source.plant.lockEnabled;
			result.plant.enterPhase = source.plant.enterPhase;
			result.plant.exitPhase = source.plant.exitPhase;
			result.plant.unplantDistance = source.plant.unplantDistance;
			result.plant.replantDistance = source.plant.replantDistance;
			result.plant.unplantAngleDegrees = source.plant.unplantAngleDegrees;
			result.plant.replantAngleDegrees = source.plant.replantAngleDegrees;
			result.plant.pivot = BridgeEnum<AnimationPlantPivot>(source.plant.pivot);
			result.plant.weightHalfLife = source.plant.weightHalfLife;
			result.alignment.fullContactHeight = source.alignment.fullContactHeight;
			result.alignment.contactFadeHeight = source.alignment.contactFadeHeight;
			result.alignment.normalHalfLife = source.alignment.normalHalfLife;
			result.alignment.rotationWeight = source.alignment.rotationWeight;
			result.pelvis.maxUpOffset = source.pelvis.maxUpOffset;
			result.pelvis.maxDownOffset = source.pelvis.maxDownOffset;
			result.pelvis.maxHorizontalOffset = source.pelvis.maxHorizontalOffset;
			result.pelvis.halfLife = source.pelvis.halfLife;
			result.weight = source.weight;
			return result;
		}

		VansGraphics::VansGroundingSettings ToNative(const AnimationGroundingSettingsDTO& source)
		{
			VansGraphics::VansGroundingSettings result;
			result.contacts = source.contacts;
			result.query.profile = source.query.profile;
			result.query.startDistanceAgainstApproach = source.query.startDistanceAgainstApproach;
			result.query.endDistanceAlongApproach = source.query.endDistanceAlongApproach;
			result.query.maxStepUp = source.query.maxStepUp;
			result.query.maxStepDown = source.query.maxStepDown;
			result.query.maxSlopeDegrees = source.query.maxSlopeDegrees;
			result.query.maxPlaneResidual = source.query.maxPlaneResidual;
			result.query.maxNormalDeviationDegrees = source.query.maxNormalDeviationDegrees;
			result.plantSignal = source.plantSignal;
			result.plant.lockEnabled = source.plant.lockEnabled;
			result.plant.enterPhase = source.plant.enterPhase;
			result.plant.exitPhase = source.plant.exitPhase;
			result.plant.unplantDistance = source.plant.unplantDistance;
			result.plant.replantDistance = source.plant.replantDistance;
			result.plant.unplantAngleDegrees = source.plant.unplantAngleDegrees;
			result.plant.replantAngleDegrees = source.plant.replantAngleDegrees;
			result.plant.pivot = BridgeEnum<VansGraphics::VansPlantPivot>(source.plant.pivot);
			result.plant.weightHalfLife = source.plant.weightHalfLife;
			result.alignment.fullContactHeight = source.alignment.fullContactHeight;
			result.alignment.contactFadeHeight = source.alignment.contactFadeHeight;
			result.alignment.normalHalfLife = source.alignment.normalHalfLife;
			result.alignment.rotationWeight = source.alignment.rotationWeight;
			result.pelvis.maxUpOffset = source.pelvis.maxUpOffset;
			result.pelvis.maxDownOffset = source.pelvis.maxDownOffset;
			result.pelvis.maxHorizontalOffset = source.pelvis.maxHorizontalOffset;
			result.pelvis.halfLife = source.pelvis.halfLife;
			result.weight = source.weight;
			return result;
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
			case VansGraphics::AnimGraphNodeType::Goal:
			{
				result->m_Goal = ToDTO(static_cast<const VansGraphics::AnimGraphGoalNode&>(source).m_Goal);
				break;
			}
			case VansGraphics::AnimGraphNodeType::AimConstraint:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphAimConstraintNode&>(source);
				result->m_ChainId = n.m_ChainId;
				result->m_Target = ToDTO(n.m_Target);
				result->m_AimSettings.minYawDegrees = n.m_Settings.yawLimitDegrees.x;
				result->m_AimSettings.maxYawDegrees = n.m_Settings.yawLimitDegrees.y;
				result->m_AimSettings.minPitchDegrees = n.m_Settings.pitchLimitDegrees.x;
				result->m_AimSettings.maxPitchDegrees = n.m_Settings.pitchLimitDegrees.y;
				result->m_AimSettings.maxAngularSpeedDegrees = n.m_Settings.maxAngularSpeedDegrees;
				result->m_AimSettings.weight = n.m_Settings.weight;
				result->m_TargetHalfLife = n.m_TargetHalfLife;
				break;
			}
			case VansGraphics::AnimGraphNodeType::Grounding:
			{
				result->m_GroundingSettings = ToDTO(
					static_cast<const VansGraphics::AnimGraphGroundingNode&>(source).m_Settings);
				break;
			}
			case VansGraphics::AnimGraphNodeType::LimbIK:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphLimbIKNode&>(source);
				result->m_ChainIds = n.m_ChainIds;
				result->m_LimbSettings.tipRotationMode = BridgeEnum<AnimationLimbTipRotationMode>(n.m_Settings.tipRotationMode);
				result->m_LimbSettings.positionTolerance = n.m_Settings.positionTolerance;
				result->m_LimbSettings.weight = n.m_Settings.weight;
				result->m_LimbSettings.commitClampedPose = n.m_Settings.commitClampedPose;
				break;
			}
			case VansGraphics::AnimGraphNodeType::ChainIK:
			{
				const auto& n = static_cast<const VansGraphics::AnimGraphChainIKNode&>(source);
				result->m_ChainIds = n.m_ChainIds;
				result->m_ChainSettings.maxIterations = n.m_Settings.maxIterations;
				result->m_ChainSettings.positionTolerance = n.m_Settings.positionTolerance;
				result->m_ChainSettings.weight = n.m_Settings.weight;
				result->m_ChainSettings.commitClampedPose = n.m_Settings.commitClampedPose;
				break;
			}
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
			case VansGraphics::AnimGraphNodeType::Goal:
			{
				static_cast<VansGraphics::AnimGraphGoalNode&>(*result).m_Goal = ToNative(source.m_Goal);
				break;
			}
			case VansGraphics::AnimGraphNodeType::AimConstraint:
			{
				auto& n = static_cast<VansGraphics::AnimGraphAimConstraintNode&>(*result);
				n.m_ChainId = source.m_ChainId;
				n.m_Target = ToNative(source.m_Target);
				n.m_Settings.yawLimitDegrees = { source.m_AimSettings.minYawDegrees, source.m_AimSettings.maxYawDegrees };
				n.m_Settings.pitchLimitDegrees = { source.m_AimSettings.minPitchDegrees, source.m_AimSettings.maxPitchDegrees };
				n.m_Settings.maxAngularSpeedDegrees = source.m_AimSettings.maxAngularSpeedDegrees;
				n.m_Settings.weight = source.m_AimSettings.weight;
				n.m_TargetHalfLife = source.m_TargetHalfLife;
				break;
			}
			case VansGraphics::AnimGraphNodeType::Grounding:
			{
				static_cast<VansGraphics::AnimGraphGroundingNode&>(*result).m_Settings =
					ToNative(source.m_GroundingSettings);
				break;
			}
			case VansGraphics::AnimGraphNodeType::LimbIK:
			{
				auto& n = static_cast<VansGraphics::AnimGraphLimbIKNode&>(*result);
				n.m_ChainIds = source.m_ChainIds;
				n.m_Settings.tipRotationMode = BridgeEnum<VansGraphics::VansLimbTipRotationMode>(source.m_LimbSettings.tipRotationMode);
				n.m_Settings.positionTolerance = source.m_LimbSettings.positionTolerance;
				n.m_Settings.weight = source.m_LimbSettings.weight;
				n.m_Settings.commitClampedPose = source.m_LimbSettings.commitClampedPose;
				break;
			}
			case VansGraphics::AnimGraphNodeType::ChainIK:
			{
				auto& n = static_cast<VansGraphics::AnimGraphChainIKNode&>(*result);
				n.m_ChainIds = source.m_ChainIds;
				n.m_Settings.maxIterations = source.m_ChainSettings.maxIterations;
				n.m_Settings.positionTolerance = source.m_ChainSettings.positionTolerance;
				n.m_Settings.weight = source.m_ChainSettings.weight;
				n.m_Settings.commitClampedPose = source.m_ChainSettings.commitClampedPose;
				break;
			}
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
			result->animationRigGuid = source.animationRigGuid;
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
				item.id = layer.id; item.name = layer.name;
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
				item.eventWeightThreshold = layer.eventWeightThreshold;
				item.updateWhenWeightIsZero = layer.updateWhenWeightIsZero;
				result->layers.push_back(std::move(item));
			}
			for (const auto& graphSet : source.graphSets)
			{
				AnimationGraphSetDTO item;
				item.id = graphSet.id; item.name = graphSet.name;
				for (const auto& binding : graphSet.bindings)
					item.bindings.push_back({ binding.layerId, binding.graphId, binding.enabled });
				result->graphSets.push_back(std::move(item));
			}
			result->defaultGraphSetId = source.defaultGraphSetId;
			result->defaultGraphSetTransition.duration = source.defaultGraphSetTransition.duration;
			result->defaultGraphSetTransition.curve = BridgeEnum<VansGraphSetBlendCurve>(source.defaultGraphSetTransition.curve);
			result->defaultGraphSetTransition.phase = BridgeEnum<VansGraphSetPhasePolicy>(source.defaultGraphSetTransition.phase);
			result->defaultGraphSetTransition.events = BridgeEnum<VansGraphSetEventPolicy>(source.defaultGraphSetTransition.events);
			result->defaultGraphSetTransition.rootMotion = BridgeEnum<VansGraphSetRootMotionPolicy>(source.defaultGraphSetTransition.rootMotion);
			result->defaultGraphSetTransition.interruption = BridgeEnum<VansGraphSetInterruptionPolicy>(source.defaultGraphSetTransition.interruption);
			result->defaultGraphSetTransition.requireStateMatch = source.defaultGraphSetTransition.requireStateMatch;
			for (const auto& rule : source.graphSetTransitionRules)
			{
				GraphSetTransitionRuleDTO item;
				item.fromGraphSetId = rule.fromGraphSetId; item.toGraphSetId = rule.toGraphSetId;
				item.policy.duration = rule.policy.duration;
				item.policy.curve = BridgeEnum<VansGraphSetBlendCurve>(rule.policy.curve);
				item.policy.phase = BridgeEnum<VansGraphSetPhasePolicy>(rule.policy.phase);
				item.policy.events = BridgeEnum<VansGraphSetEventPolicy>(rule.policy.events);
				item.policy.rootMotion = BridgeEnum<VansGraphSetRootMotionPolicy>(rule.policy.rootMotion);
				item.policy.interruption = BridgeEnum<VansGraphSetInterruptionPolicy>(rule.policy.interruption);
				item.policy.requireStateMatch = rule.policy.requireStateMatch;
				result->graphSetTransitionRules.push_back(std::move(item));
			}
			for (const auto& slot : source.slots)
			{
				AnimationSlotDTO item;
				item.id = slot.id; item.name = slot.name; item.layerId = slot.layerId;
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
			result.animationRigGuid = source.animationRigGuid;
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
				item.id = layer.id; item.name = layer.name;
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
				item.eventWeightThreshold = layer.eventWeightThreshold;
				item.updateWhenWeightIsZero = layer.updateWhenWeightIsZero;
				result.layers.push_back(std::move(item));
			}
			for (const auto& graphSet : source.graphSets)
			{
				VansGraphics::VansAnimationGraphSetDefinition item;
				item.id = graphSet.id; item.name = graphSet.name;
				for (const auto& binding : graphSet.bindings)
					item.bindings.push_back({ binding.layerId, binding.graphId, binding.enabled });
				result.graphSets.push_back(std::move(item));
			}
			result.defaultGraphSetId = source.defaultGraphSetId;
			result.defaultGraphSetTransition.duration = source.defaultGraphSetTransition.duration;
			result.defaultGraphSetTransition.curve = BridgeEnum<VansGraphics::VansGraphSetBlendCurve>(source.defaultGraphSetTransition.curve);
			result.defaultGraphSetTransition.phase = BridgeEnum<VansGraphics::VansGraphSetPhasePolicy>(source.defaultGraphSetTransition.phase);
			result.defaultGraphSetTransition.events = BridgeEnum<VansGraphics::VansGraphSetEventPolicy>(source.defaultGraphSetTransition.events);
			result.defaultGraphSetTransition.rootMotion = BridgeEnum<VansGraphics::VansGraphSetRootMotionPolicy>(source.defaultGraphSetTransition.rootMotion);
			result.defaultGraphSetTransition.interruption = BridgeEnum<VansGraphics::VansGraphSetInterruptionPolicy>(source.defaultGraphSetTransition.interruption);
			result.defaultGraphSetTransition.requireStateMatch = source.defaultGraphSetTransition.requireStateMatch;
			for (const auto& rule : source.graphSetTransitionRules)
			{
				VansGraphics::VansGraphSetTransitionRule item;
				item.fromGraphSetId = rule.fromGraphSetId; item.toGraphSetId = rule.toGraphSetId;
				item.policy.duration = rule.policy.duration;
				item.policy.curve = BridgeEnum<VansGraphics::VansGraphSetBlendCurve>(rule.policy.curve);
				item.policy.phase = BridgeEnum<VansGraphics::VansGraphSetPhasePolicy>(rule.policy.phase);
				item.policy.events = BridgeEnum<VansGraphics::VansGraphSetEventPolicy>(rule.policy.events);
				item.policy.rootMotion = BridgeEnum<VansGraphics::VansGraphSetRootMotionPolicy>(rule.policy.rootMotion);
				item.policy.interruption = BridgeEnum<VansGraphics::VansGraphSetInterruptionPolicy>(rule.policy.interruption);
				item.policy.requireStateMatch = rule.policy.requireStateMatch;
				result.graphSetTransitionRules.push_back(std::move(item));
			}
			for (const auto& slot : source.slots)
			{
				VansGraphics::VansAnimationSlotDefinition item;
				item.id = slot.id; item.name = slot.name; item.layerId = slot.layerId;
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

		AnimationRigDocumentDTO ToDTO(const VansGraphics::VansAnimationRigAsset& source)
		{
			AnimationRigDocumentDTO result;
			result.name = source.name;
			result.skeletonGuid = source.skeletonGuid;
			result.modelForward = { source.modelForward.x, source.modelForward.y, source.modelForward.z };
			result.modelUp = { source.modelUp.x, source.modelUp.y, source.modelUp.z };
			result.semanticBones.assign(source.semanticBones.begin(), source.semanticBones.end());
			std::sort(result.semanticBones.begin(), result.semanticBones.end(),
				[](const auto& left, const auto& right) { return left.first < right.first; });
			for (const auto& socket : source.sockets)
			{
				AnimationRigSocketDTO item;
				item.guid = socket.guid;
				item.name = socket.name;
				item.boneGuid = socket.boneGuid;
				item.positionLocal = { socket.positionLocal.x, socket.positionLocal.y, socket.positionLocal.z };
				item.rotationLocal = { socket.rotationLocal.x, socket.rotationLocal.y,
					socket.rotationLocal.z, socket.rotationLocal.w };
				item.scaleLocal = { socket.scaleLocal.x, socket.scaleLocal.y, socket.scaleLocal.z };
				result.sockets.push_back(std::move(item));
			}
			for (const auto& profile : source.attachmentProfiles)
			{
				AnimationRigAttachmentProfileDTO item;
				item.modelGuid = profile.modelGuid;
				item.parentKind = BridgeEnum<AnimationRigAttachmentParentKind>(profile.parentKind);
				item.anchorGuid = profile.anchorGuid;
				item.positionLocal = { profile.positionLocal.x, profile.positionLocal.y,
					profile.positionLocal.z };
				item.rotationLocal = { profile.rotationLocal.x, profile.rotationLocal.y,
					profile.rotationLocal.z, profile.rotationLocal.w };
				item.scaleLocal = { profile.scaleLocal.x, profile.scaleLocal.y,
					profile.scaleLocal.z };
				result.attachmentProfiles.push_back(std::move(item));
			}
			for (const auto& goal : source.goals)
				result.goals.push_back({ goal.id, goal.effectorBone });
			for (const auto& chain : source.chains)
			{
				AnimationRigChainDTO item;
				item.id = chain.id;
				item.solver = BridgeEnum<AnimationRigSolverKind>(chain.solver);
				item.bones = chain.bones;
				item.goal = chain.goal;
				item.poleAxisLocal = { chain.poleAxisLocal.x, chain.poleAxisLocal.y, chain.poleAxisLocal.z };
				item.softReachStartRatio = chain.softReachStartRatio;
				item.maxStretchScale = chain.maxStretchScale;
				item.weights = chain.weights;
				item.solveWeights = chain.solveWeights;
				item.maxStepDegrees = chain.maxStepDegrees;
				item.forwardAxisLocal = { chain.forwardAxisLocal.x, chain.forwardAxisLocal.y, chain.forwardAxisLocal.z };
				item.upAxisLocal = { chain.upAxisLocal.x, chain.upAxisLocal.y, chain.upAxisLocal.z };
				result.chains.push_back(std::move(item));
			}
			for (const auto& limit : source.jointLimits)
			{
				AnimationRigJointLimitDTO item;
				item.bone = limit.bone;
				item.kind = BridgeEnum<AnimationRigJointLimitKind>(limit.kind);
				item.axisLocal = { limit.axisLocal.x, limit.axisLocal.y, limit.axisLocal.z };
				item.swingReferenceAxisLocal = { limit.swingReferenceAxisLocal.x,
					limit.swingReferenceAxisLocal.y, limit.swingReferenceAxisLocal.z };
				item.minDegrees = limit.minDegrees;
				item.maxDegrees = limit.maxDegrees;
				item.swingLimitDegrees = { limit.swingLimitDegrees.x, limit.swingLimitDegrees.y };
				result.jointLimits.push_back(std::move(item));
			}
			for (const auto& contact : source.contacts)
			{
				AnimationRigContactDTO item;
				item.id = contact.id;
				item.chain = contact.chain;
				item.footBone = contact.footBone;
				item.ballBone = contact.ballBone;
				item.soleForwardLocal = { contact.soleForwardLocal.x, contact.soleForwardLocal.y, contact.soleForwardLocal.z };
				item.soleNormalLocal = { contact.soleNormalLocal.x, contact.soleNormalLocal.y, contact.soleNormalLocal.z };
				for (const auto& sample : contact.soleSamplesLocal)
					item.soleSamplesLocal.push_back({ sample.id,
						{ sample.positionLocal.x, sample.positionLocal.y, sample.positionLocal.z } });
				item.heelPivotLocal = { contact.heelPivotLocal.x, contact.heelPivotLocal.y, contact.heelPivotLocal.z };
				item.ballPivotLocal = { contact.ballPivotLocal.x, contact.ballPivotLocal.y, contact.ballPivotLocal.z };
				item.anklePivotLocal = { contact.anklePivotLocal.x, contact.anklePivotLocal.y, contact.anklePivotLocal.z };
				item.sweepRadius = contact.sweepRadius;
				result.contacts.push_back(std::move(item));
			}
			return result;
		}

		VansGraphics::VansAnimationRigAsset ToNative(const AnimationRigDocumentDTO& source)
		{
			VansGraphics::VansAnimationRigAsset result;
			result.name = source.name;
			result.skeletonGuid = source.skeletonGuid;
			result.modelForward = { source.modelForward.x, source.modelForward.y, source.modelForward.z };
			result.modelUp = { source.modelUp.x, source.modelUp.y, source.modelUp.z };
			for (const auto& semantic : source.semanticBones)
				result.semanticBones.emplace(semantic.first, semantic.second);
			for (const auto& socket : source.sockets)
			{
				VansGraphics::VansRigSocketDefinition item;
				item.guid = socket.guid;
				item.name = socket.name;
				item.boneGuid = socket.boneGuid;
				item.positionLocal = { socket.positionLocal.x, socket.positionLocal.y, socket.positionLocal.z };
				item.rotationLocal = { socket.rotationLocal.w, socket.rotationLocal.x,
					socket.rotationLocal.y, socket.rotationLocal.z };
				item.scaleLocal = { socket.scaleLocal.x, socket.scaleLocal.y, socket.scaleLocal.z };
				result.sockets.push_back(std::move(item));
			}
			for (const auto& profile : source.attachmentProfiles)
			{
				VansGraphics::VansRigAttachmentProfileDefinition item;
				item.modelGuid = profile.modelGuid;
				item.parentKind = BridgeEnum<VansGraphics::VansRigAttachmentParentKind>(
					profile.parentKind);
				item.anchorGuid = profile.anchorGuid;
				item.positionLocal = { profile.positionLocal.x, profile.positionLocal.y,
					profile.positionLocal.z };
				item.rotationLocal = { profile.rotationLocal.w, profile.rotationLocal.x,
					profile.rotationLocal.y, profile.rotationLocal.z };
				item.scaleLocal = { profile.scaleLocal.x, profile.scaleLocal.y,
					profile.scaleLocal.z };
				result.attachmentProfiles.push_back(std::move(item));
			}
			for (const auto& goal : source.goals)
				result.goals.push_back({ goal.id, goal.effectorBone });
			for (const auto& chain : source.chains)
			{
				VansGraphics::VansRigChainDefinition item;
				item.id = chain.id;
				item.solver = BridgeEnum<VansGraphics::VansRigSolverKind>(chain.solver);
				item.bones = chain.bones;
				item.goal = chain.goal;
				item.poleAxisLocal = { chain.poleAxisLocal.x, chain.poleAxisLocal.y, chain.poleAxisLocal.z };
				item.softReachStartRatio = chain.softReachStartRatio;
				item.maxStretchScale = chain.maxStretchScale;
				item.weights = chain.weights;
				item.solveWeights = chain.solveWeights;
				item.maxStepDegrees = chain.maxStepDegrees;
				item.forwardAxisLocal = { chain.forwardAxisLocal.x, chain.forwardAxisLocal.y, chain.forwardAxisLocal.z };
				item.upAxisLocal = { chain.upAxisLocal.x, chain.upAxisLocal.y, chain.upAxisLocal.z };
				result.chains.push_back(std::move(item));
			}
			for (const auto& limit : source.jointLimits)
			{
				VansGraphics::VansRigJointLimitDefinition item;
				item.bone = limit.bone;
				item.kind = BridgeEnum<VansGraphics::VansJointLimitKind>(limit.kind);
				item.axisLocal = { limit.axisLocal.x, limit.axisLocal.y, limit.axisLocal.z };
				item.swingReferenceAxisLocal = { limit.swingReferenceAxisLocal.x,
					limit.swingReferenceAxisLocal.y, limit.swingReferenceAxisLocal.z };
				item.minDegrees = limit.minDegrees;
				item.maxDegrees = limit.maxDegrees;
				item.swingLimitDegrees = { limit.swingLimitDegrees.x, limit.swingLimitDegrees.y };
				result.jointLimits.push_back(std::move(item));
			}
			for (const auto& contact : source.contacts)
			{
				VansGraphics::VansRigContactDefinition item;
				item.id = contact.id;
				item.chain = contact.chain;
				item.footBone = contact.footBone;
				item.ballBone = contact.ballBone;
				item.soleForwardLocal = { contact.soleForwardLocal.x, contact.soleForwardLocal.y, contact.soleForwardLocal.z };
				item.soleNormalLocal = { contact.soleNormalLocal.x, contact.soleNormalLocal.y, contact.soleNormalLocal.z };
				for (const auto& sample : contact.soleSamplesLocal)
					item.soleSamplesLocal.push_back({ sample.id,
						{ sample.positionLocal.x, sample.positionLocal.y, sample.positionLocal.z } });
				item.heelPivotLocal = { contact.heelPivotLocal.x, contact.heelPivotLocal.y, contact.heelPivotLocal.z };
				item.ballPivotLocal = { contact.ballPivotLocal.x, contact.ballPivotLocal.y, contact.ballPivotLocal.z };
				item.anklePivotLocal = { contact.anklePivotLocal.x, contact.anklePivotLocal.y, contact.anklePivotLocal.z };
				item.sweepRadius = contact.sweepRadius;
				result.contacts.push_back(std::move(item));
			}
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

	AnimationRigDocumentDecodeResult AnimationAuthoringBridge::DecodeAnimationRig(
		const std::string& canonicalJson)
	{
		AnimationRigDocumentDecodeResult result;
		try
		{
			const auto root = nlohmann::json::parse(canonicalJson);
			VansGraphics::VansAnimationRigAsset native;
			if (!VansGraphics::VansAnimationRigStorage::DeserializeFromJsonObject(
				root, native, result.message))
				return result;
			result.document = ToDTO(native);
			result.success = true;
		}
		catch (const std::exception& exception) { result.message = exception.what(); }
		return result;
	}

	AnimationRigDocumentEncodeResult AnimationAuthoringBridge::EncodeAnimationRig(
		const AnimationRigDocumentDTO& document)
	{
		AnimationRigDocumentEncodeResult result;
		nlohmann::json root;
		if (!VansGraphics::VansAnimationRigStorage::SerializeToJsonObject(
			ToNative(document), root, result.message))
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
		else if (request.kind == AnimationAuthoringAssetKind::AnimationRig)
		{
			const auto path = MakeUniquePath(directory, "New Animation Rig", ".vanimrig");
			VansGraphics::VansAnimationRigAsset asset;
			asset.name = path.stem().string();
			result.success = !path.empty()
				&& VansGraphics::VansAnimationRigStorage::SaveAtomic(path, asset, error);
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
			base.name = "Base"; base.kind = VansGraphics::VansAnimationLayerKind::Base;
			const std::string baseLayerId = base.id; asset.layers.push_back(std::move(base));
			VansGraphics::VansAnimationGraphSetDefinition graphSet;
			graphSet.id = Vans::VansAssetGuid::New().ToString(); graphSet.name = "Default";
			graphSet.bindings.push_back({ baseLayerId, graphId, true });
			asset.defaultGraphSetId = graphSet.id; asset.graphSets.push_back(std::move(graphSet));
			result.success = !path.empty() && VansGraphics::VansAnimatorIO::Save(path.string(), asset, error);
			result.assetPath = result.success ? path.string() : std::string{};
		}
		result.message = result.success ? "Animation asset created" : error;
		return result;
	}
}
