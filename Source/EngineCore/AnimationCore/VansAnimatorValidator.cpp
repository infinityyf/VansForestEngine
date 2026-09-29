#include "VansAnimatorValidator.h"

#include "../AssetCore/VansAssetGuid.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace VansGraphics
{
	namespace
	{
		const AnimGraphStateMachineNode* FindPrimaryStateMachine(const VansAnimGraph& graph)
		{
			std::vector<int> executionPlan;
			std::string error;
			if (!graph.BuildExecutionPlan(executionPlan, error))
				return nullptr;
			for (int nodeId : executionPlan)
			{
				const VansAnimGraphNode* node = graph.GetNode(nodeId);
				if (node && node->GetType() == VansAnimGraphNodeType::StateMachine)
					return static_cast<const AnimGraphStateMachineNode*>(node);
			}
			return nullptr;
		}
	}

	bool VansAnimatorValidator::ValidateGraph(
		const VansAnimGraph& graph,
		AnimatorGraphAsset::Role role,
		const std::string& graphName,
		std::string& error)
	{
		error.clear();
		std::vector<int> executionPlan;
		std::string executionError;
		if (!graph.BuildExecutionPlan(executionPlan, executionError))
		{
			error = "Animator Graph '" + graphName
				+ "' failed compilation: " + executionError;
			return false;
		}

		std::size_t targetInputCount = 0;
		bool targetInputReachable = false;
		for (const auto& [nodeId, node] : graph.GetNodes())
		{
			if (!node)
				continue;
			const VansAnimGraphNodeType type = node->GetType();
			if (type == VansAnimGraphNodeType::TargetPoseInput)
			{
				++targetInputCount;
				targetInputReachable = std::find(
					executionPlan.begin(), executionPlan.end(), nodeId) != executionPlan.end();
			}

			if (role == AnimatorGraphAsset::Role::Pose)
			{
				if (type == VansAnimGraphNodeType::Goal
					|| type == VansAnimGraphNodeType::AimConstraint
					|| type == VansAnimGraphNodeType::Grounding
					|| type == VansAnimGraphNodeType::LimbIK
					|| type == VansAnimGraphNodeType::ChainIK
					|| type == VansAnimGraphNodeType::PoseCheckpoint
					|| type == VansAnimGraphNodeType::RotationDistribution)
				{
					error = "Pose Graph '" + graphName
						+ "' cannot contain target procedural nodes";
					return false;
				}
			}
			else
			{
				switch (type)
				{
				case VansAnimGraphNodeType::Entry:
				case VansAnimGraphNodeType::Clip:
				case VansAnimGraphNodeType::Inertialization:
				case VansAnimGraphNodeType::SpeedScale:
				case VansAnimGraphNodeType::StateMachine:
				case VansAnimGraphNodeType::MotionMatching:
				case VansAnimGraphNodeType::Slot:
					error = "Target Post Process Graph '" + graphName
						+ "' contains a pose-source or playback node";
					return false;
				default:
					break;
				}
			}
		}

		if (role == AnimatorGraphAsset::Role::Pose)
		{
			if (targetInputCount != 0)
			{
				error = "Pose Graph '" + graphName + "' cannot contain Target Pose Input";
				return false;
			}
		}
		else if (targetInputCount != 1 || !targetInputReachable)
		{
			error = "Target Post Process Graph '" + graphName
				+ "' requires exactly one reachable Target Pose Input";
			return false;
		}
		return true;
	}

	bool VansAnimatorValidator::Validate(const AnimatorAssetData& data, std::string& error)
	{
		error.clear();
		if (data.name.empty())
		{
			error = "Animator name cannot be empty";
			return false;
		}
		Vans::VansAssetGuid animationRigGuid;
		if (!Vans::VansAssetGuid::TryParse(data.animationRigGuid, animationRigGuid))
		{
			error = "Animator requires a valid animationRigGuid";
			return false;
		}
		if (!data.editor.previewModelGuid.empty())
		{
			Vans::VansAssetGuid previewModelGuid;
			if (!Vans::VansAssetGuid::TryParse(data.editor.previewModelGuid, previewModelGuid))
			{
				error = "Animator preview model requires a valid asset GUID";
				return false;
			}
		}

		std::unordered_map<std::string, AnimatorParamType> parameters;
		for (const AnimatorParameter& parameter : data.parameters)
		{
			if (parameter.name.empty() || !parameters.emplace(parameter.name, parameter.type).second)
			{
				error = "Animator parameter names must be non-empty and unique";
				return false;
			}
			const bool finiteDefault = parameter.type == AnimatorParamType::Float
				? std::isfinite(parameter.floatVal)
				: parameter.type == AnimatorParamType::Vector3
					? std::isfinite(parameter.vec3Val.x) && std::isfinite(parameter.vec3Val.y)
						&& std::isfinite(parameter.vec3Val.z)
					: parameter.type == AnimatorParamType::Quaternion
						? std::isfinite(parameter.quatVal.x) && std::isfinite(parameter.quatVal.y)
							&& std::isfinite(parameter.quatVal.z) && std::isfinite(parameter.quatVal.w)
						: true;
			if (!finiteDefault)
			{
				error = "Animator parameter '" + parameter.name + "' has a non-finite default value";
				return false;
			}
		}
		auto hasParameter = [&](const std::string& name, AnimatorParamType type, bool optional = false)
		{
			if (name.empty()) return optional;
			const auto found = parameters.find(name);
			return found != parameters.end() && found->second == type;
		};
		auto finiteVec3 = [](const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		};
		auto finiteQuat = [](const glm::quat& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y)
				&& std::isfinite(value.z) && std::isfinite(value.w);
		};
		auto finiteWeight = [](float value)
		{
			return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
		};
		auto validateGoal = [&](const VansGraphGoalDefinition& goal,
		                        bool requireGoalId,
		                        const std::string& owner) -> bool
		{
			if ((requireGoalId && goal.goalId.empty())
				|| !finiteVec3(goal.fixedPositionModel) || !finiteVec3(goal.poleOffsetLocal)
				|| (!goal.poleBoneName.empty() && goal.source != VansGraphGoalSource::PoseBone)
				|| !finiteQuat(goal.fixedRotationModel)
				|| glm::dot(goal.fixedRotationModel, goal.fixedRotationModel) <= 1.0e-8f
				|| !finiteWeight(goal.fixedPositionWeight) || !finiteWeight(goal.fixedRotationWeight)
				|| !hasParameter(goal.weightParameter, AnimatorParamType::Float, true))
			{
				error = owner + " has an invalid Goal id, transform, or weight";
				return false;
			}
			switch (goal.source)
			{
			case VansGraphGoalSource::Binding:
				if (!goal.binding.empty()) return true;
				error = owner + " requires a non-empty target binding";
				return false;
			case VansGraphGoalSource::Parameters:
				if (hasParameter(goal.positionParameter, AnimatorParamType::Vector3)
					&& hasParameter(goal.rotationParameter, AnimatorParamType::Quaternion, true)
					&& hasParameter(goal.weightParameter, AnimatorParamType::Float, true)) return true;
				error = owner + " has a missing or mistyped target parameter";
				return false;
			case VansGraphGoalSource::Fixed:
				return true;
			case VansGraphGoalSource::PoseBone:
				if (!goal.boneName.empty()
					&& hasParameter(goal.positionParameter, AnimatorParamType::Vector3, true)
					&& hasParameter(goal.rotationParameter, AnimatorParamType::Quaternion, true)) return true;
				error = owner + " has a missing pose bone or mistyped offset parameter";
				return false;
			}
			error = owner + " has an invalid Goal source";
			return false;
		};

		std::unordered_set<std::string> clipNames;
		for (const AnimatorClipRef& clip : data.clipRefs)
		{
			Vans::VansAssetGuid guid;
			if (clip.name.empty() || !Vans::VansAssetGuid::TryParse(clip.assetGuid, guid)
				|| !clipNames.insert(clip.name).second)
			{
				error = "Animator Clip names must be non-empty and unique, and every Clip requires a valid asset GUID";
				return false;
			}
		}

		std::unordered_set<std::string> graphIds;
		std::unordered_map<std::string, AnimatorGraphAsset::Role> graphRoles;
		std::size_t targetPostProcessGraphCount = 0;
		for (const AnimatorGraphAsset& graph : data.graphs)
		{
			if (graph.id.empty() || graph.name.empty() || !graph.graph || !graphIds.insert(graph.id).second)
			{
				error = "Animator Graph ids/names must be non-empty and unique, and every Graph needs a definition";
				return false;
			}
			graphRoles.emplace(graph.id, graph.role);
			if (!ValidateGraph(*graph.graph, graph.role, graph.name, error))
				return false;
			if (graph.role == AnimatorGraphAsset::Role::TargetPostProcess)
				++targetPostProcessGraphCount;
			std::unordered_set<std::string> checkpointIds;
			for (const auto& [nodeId, node] : graph.graph->GetNodes())
			{
				if (!node)
					continue;
				if (node->GetType() == VansAnimGraphNodeType::Clip)
				{
					const auto* clipNode = static_cast<const AnimGraphClipNode*>(node.get());
					if (clipNames.find(clipNode->m_ClipName) == clipNames.end()
						|| !std::isfinite(clipNode->m_Speed)
						|| !std::isfinite(clipNode->m_StartPosition) || clipNode->m_StartPosition < 0
						|| (clipNode->m_StartPosition != 0 && clipNode->HasExplicitSampleTime())
						|| !std::isfinite(clipNode->m_SampleTime) || (clipNode->m_SampleTime < 0 && clipNode->m_SampleTime != -1)
						|| (clipNode->m_AdditiveMode!=VansAdditivePoseMode::LocalSpherical && clipNode->m_AdditiveMode!=VansAdditivePoseMode::LocalLinear && clipNode->m_AdditiveMode!=VansAdditivePoseMode::MeshRotationLinear)
						|| (clipNode->m_AdditiveMode!=VansAdditivePoseMode::LocalSpherical && clipNode->m_AdditiveReferenceTime<0)
						|| !std::isfinite(clipNode->m_AdditiveReferenceTime) || clipNode->m_AdditiveReferenceTime < -1
						|| (!clipNode->m_AdditiveReferenceClip.empty() && (clipNode->m_AdditiveReferenceTime < 0
							|| clipNames.find(clipNode->m_AdditiveReferenceClip) == clipNames.end()))
						|| (!clipNode->m_LoopParameter.empty() && (!hasParameter(clipNode->m_LoopParameter, AnimatorParamType::Bool)
							|| clipNode->HasExplicitSampleTime()))
						|| (!clipNode->m_SampleTimeParameter.empty()
							&& !hasParameter(clipNode->m_SampleTimeParameter, AnimatorParamType::Float)))
					{
						error = "Graph '" + graph.name + "' contains a Clip node with an invalid Clip binding or speed";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::StateMachine)
				{
					const auto* stateMachine = static_cast<const AnimGraphStateMachineNode*>(node.get());
					std::unordered_set<std::string> states;
					for (const AnimatorState& state : stateMachine->m_States)
					{
						const bool hasPoseReference = state.poseNodeId >= 0;
						const auto* poseNode = hasPoseReference
							? graph.graph->GetNode(state.poseNodeId) : nullptr;
						bool poseReferenceValid = hasPoseReference && poseNode
							&& poseNode->GetType() != VansAnimGraphNodeType::Entry
							&& poseNode->GetType() != VansAnimGraphNodeType::Output;
						if (poseReferenceValid)
						{
							const auto posePins = poseNode->GetPins();
							poseReferenceValid = std::any_of(
								posePins.begin(), posePins.end(),
								[](const AnimGraphPin& pin)
								{
									return pin.kind == AnimGraphPinKind::Output
										&& pin.type == AnimGraphPinType::Pose;
								});
						}
						const bool clipReferenceValid = clipNames.find(state.clipName) != clipNames.end();
						if ((state.conduit && (hasPoseReference || !state.clipName.empty()))
							|| (!state.entryConditionParameter.empty() && (!state.conduit || !hasParameter(state.entryConditionParameter, AnimatorParamType::Bool)))
							|| state.poseNodeId < -1
							|| state.name.empty() || !states.insert(state.name).second
							|| (!hasPoseReference && !state.clipName.empty() && !clipReferenceValid)
							|| (hasPoseReference && !poseReferenceValid)
							|| (!state.clipName.empty() && !clipReferenceValid)
							|| (!state.speedParameter.empty()
								&& !hasParameter(state.speedParameter, AnimatorParamType::Float))
							|| !std::isfinite(state.speed) || !std::isfinite(state.startTime)
							|| !std::isfinite(state.endTime) || state.startTime < 0.0f
							|| (state.endTime >= 0.0f && state.endTime < state.startTime))
						{
							error = "State Machine in Graph '" + graph.name
								+ "' contains an invalid State definition";
							return false;
						}
					}
					if (states.empty() || states.find(stateMachine->m_DefaultStateName) == states.end())
					{
						error = "State Machine in Graph '" + graph.name
							+ "' requires a non-empty State set and valid default State";
						return false;
					}
					for (const AnimatorTransition& transition : stateMachine->m_Transitions)
					{
						// ExitTime is normalized against the source state's clip clock.
						// A pose-only state can still be a valid generic state, but it
						// must not request an exit-time transition without a clip clock.
						// This keeps subgraph states deterministic instead of silently
						// treating their normalized time as zero forever.
						bool hasExitTimeClock = !transition.hasExitTime;
						if (transition.hasExitTime)
						{
							auto sourceHasClip = [&](const std::string& sourceName)
							{
								return std::any_of(
									stateMachine->m_States.begin(), stateMachine->m_States.end(),
									[&](const AnimatorState& source)
									{
										return source.name == sourceName
											&& !source.clipName.empty();
									});
							};
							hasExitTimeClock = transition.fromState == "*"
								? std::all_of(
										stateMachine->m_States.begin(), stateMachine->m_States.end(),
										[](const AnimatorState& source)
										{
											return !source.clipName.empty();
										})
								: sourceHasClip(transition.fromState);
						}
						if ((transition.fromState != "*" && states.find(transition.fromState) == states.end())
							|| states.find(transition.toState) == states.end()
							|| !std::isfinite(transition.blendDuration) || transition.blendDuration < 0.0f
							|| !std::isfinite(transition.exitTime)
							|| transition.exitTime < 0.0f || transition.exitTime > 1.0f
							|| !hasExitTimeClock
							|| (transition.automaticRemainingTime && (transition.hasExitTime || transition.requireRelevantClipFinished || !transition.conditions.empty())))
						{
							error = "State Machine in Graph '" + graph.name
								+ "' contains an invalid Transition";
							return false;
						}

						for (const auto& factor : transition.boneBlendFactors)
							if (factor.first.empty() || !std::isfinite(factor.second) || factor.second <= 0.0f)
							{
								error = "Invalid transition bone blend factor in Graph '" + graph.name + "'";
								return false;
							}
						for (const auto* curve : {&transition.blendCurve, &transition.durationScaleCurve})
						{
						float previousCurveTime = -1.0f;
						for (const auto& key : *curve)
						{
							if (!std::isfinite(key.time) || !std::isfinite(key.value)
								|| !std::isfinite(key.arriveTangent) || !std::isfinite(key.leaveTangent)
								|| key.time <= previousCurveTime || key.time < 0.0f || key.time > 1.0f)
							{
								error = "Invalid transition blend curve in Graph '" + graph.name + "'";
								return false;
							}
							previousCurveTime = key.time;
						}
						}

						for (const TransitionCondition& condition : transition.conditions)
						{
							if (condition.source != AnimatorConditionSource::Parameter)
							{
								const auto* queried = condition.machineNodeId < 0 ? node.get() : graph.graph->GetNode(condition.machineNodeId);
								if ((condition.source != AnimatorConditionSource::MachineWeight && condition.source != AnimatorConditionSource::StateElapsedTime)
									|| !condition.paramName.empty() || condition.machineNodeId < -1 || !std::isfinite(condition.floatVal)
									|| !queried || queried->GetType() != VansAnimGraphNodeType::StateMachine)
								{ error = "Invalid state-machine runtime condition in Graph '" + graph.name + "'"; return false; }
								continue;
							}
							if (condition.machineNodeId != -1)
							{ error = "Parameter condition cannot reference a state machine"; return false; }
							const auto parameter = parameters.find(condition.paramName);
							if (parameter == parameters.end()
								|| parameter->second == AnimatorParamType::Vector3
								|| parameter->second == AnimatorParamType::Quaternion
								|| ((parameter->second == AnimatorParamType::Bool
									|| parameter->second == AnimatorParamType::Trigger)
									&& condition.op != CompareOp::Equal && condition.op != CompareOp::NotEqual))
							{
								error = "State Machine in Graph '" + graph.name
									+ "' contains an invalid Transition condition";
								return false;
							}
						}
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::Blend)
				{
					const auto* blend = static_cast<const AnimGraphBlendNode*>(node.get());
					if (!std::isfinite(blend->m_FixedAlpha) || blend->m_FixedAlpha < 0.0f
						|| blend->m_FixedAlpha > 1.0f
						|| (blend->m_UseParam && !hasParameter(blend->m_ParamName, AnimatorParamType::Float)))
					{
						error = "Blend node in Graph '" + graph.name + "' has an invalid alpha source";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::Blend1D)
				{
					const auto* blend = static_cast<const AnimGraphBlend1DNode*>(node.get());
					if (!hasParameter(blend->m_ParamName, AnimatorParamType::Float)
						|| blend->m_Thresholds.empty()
						|| !std::is_sorted(blend->m_Thresholds.begin(), blend->m_Thresholds.end())
						|| std::any_of(blend->m_Thresholds.begin(), blend->m_Thresholds.end(),
							[](float value) { return !std::isfinite(value); }))
					{
						error = "Blend1D node in Graph '" + graph.name + "' requires a float parameter and sorted finite thresholds";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::ModifyCurve)
				{
					const auto* modifier=static_cast<const AnimGraphModifyCurveNode*>(node.get());
					bool valid=std::isfinite(modifier->m_Alpha)
						&& (modifier->m_Mode==VansCurveModifyMode::Blend || modifier->m_Mode==VansCurveModifyMode::Scale)
						&& (modifier->m_AlphaParameter.empty() || hasParameter(modifier->m_AlphaParameter,AnimatorParamType::Float));
					std::unordered_set<std::string> names;
					for (const auto& curve:modifier->m_Curves)
						valid=valid && !curve.name.empty() && names.insert(curve.name).second && std::isfinite(curve.value)
							&& (curve.parameter.empty() || hasParameter(curve.parameter,AnimatorParamType::Float));
					if (!valid)
					{ error="ModifyCurve in Graph '"+graph.name+"' requires unique curve names, finite values and float parameters"; return false; }
				}
				else if (node->GetType() == VansAnimGraphNodeType::ComponentBoneScale)
				{
					const auto* scaler=static_cast<const AnimGraphComponentBoneScaleNode*>(node.get());
					if (scaler->m_BoneName.empty() || !std::isfinite(scaler->m_Scale.x)
						|| !std::isfinite(scaler->m_Scale.y) || !std::isfinite(scaler->m_Scale.z)
						|| !std::isfinite(scaler->m_Alpha) || (scaler->m_AlphaParameter.size()
							&& !hasParameter(scaler->m_AlphaParameter,AnimatorParamType::Float)))
					{ error="ComponentBoneScale in Graph '"+graph.name+"' requires a bone, finite scale/alpha and a float alpha parameter"; return false; }
				}
				else if (node->GetType() == VansAnimGraphNodeType::ComponentBoneTransform)
				{
					const auto* transform=static_cast<const AnimGraphComponentBoneTransformNode*>(node.get());
					if (graph.role != AnimatorGraphAsset::Role::TargetPostProcess || transform->m_BoneName.empty()
						|| (transform->m_PositionParameter.empty() && transform->m_RotationParameter.empty())
						|| !std::isfinite(transform->m_Alpha)
						|| (!transform->m_PositionParameter.empty()
							&& !hasParameter(transform->m_PositionParameter,AnimatorParamType::Vector3))
						|| (!transform->m_RotationParameter.empty()
							&& !hasParameter(transform->m_RotationParameter,AnimatorParamType::Quaternion))
						|| (!transform->m_AlphaParameter.empty()
							&& !hasParameter(transform->m_AlphaParameter,AnimatorParamType::Float)))
					{ error="ComponentBoneTransform requires a target post-process Graph, bone and typed pose parameters"; return false; }
				}
				else if (node->GetType() == VansAnimGraphNodeType::MultiWayBlend)
				{
					const auto* blend = static_cast<const AnimGraphMultiWayBlendNode*>(node.get());
					if (blend->m_WeightParameters.empty() || std::any_of(
						blend->m_WeightParameters.begin(), blend->m_WeightParameters.end(),
						[&](const auto& name) { return !hasParameter(name, AnimatorParamType::Float); }))
					{
						error = "MultiWayBlend node in Graph '" + graph.name + "' requires float weight parameters";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::BlendSpace2D)
				{
					const auto* blend = static_cast<const AnimGraphBlendSpace2DNode*>(node.get());
					bool finiteSamples = !blend->m_Samples.empty();
					for (const auto& sample : blend->m_Samples)
						finiteSamples = finiteSamples && std::isfinite(sample.x) && std::isfinite(sample.y);
					bool validGrid = true;
					if (blend->m_BilinearGrid && finiteSamples)
					{
						std::set<float> xs, ys;
						std::set<std::pair<float, float>> points;
						for (const auto& sample : blend->m_Samples)
						{ xs.insert(sample.x); ys.insert(sample.y); points.insert({sample.x, sample.y}); }
						validGrid = blend->m_Samples.size() == 4 && points.size() == 4 && xs.size() == 2 && ys.size() == 2;
					}
					if (!hasParameter(blend->m_XParamName, AnimatorParamType::Float)
						|| !hasParameter(blend->m_YParamName, AnimatorParamType::Float)
						|| !finiteSamples || !validGrid
						|| !std::isfinite(blend->m_CubicFilterWindowX) || blend->m_CubicFilterWindowX < 0
						|| !std::isfinite(blend->m_CubicFilterWindowY) || blend->m_CubicFilterWindowY < 0)
					{
						error = "BlendSpace2D node in Graph '" + graph.name
							+ "' requires two float parameters and finite samples";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::IfCondition)
				{
					const auto* condition = static_cast<const AnimGraphIfConditionNode*>(node.get());
					const auto parameter = parameters.find(condition->m_ParamName);
					if (parameter == parameters.end()
						|| parameter->second == AnimatorParamType::Vector3
						|| parameter->second == AnimatorParamType::Quaternion
						|| ((parameter->second == AnimatorParamType::Bool || parameter->second == AnimatorParamType::Trigger)
							&& condition->m_CompareOp != CompareOp::Equal
							&& condition->m_CompareOp != CompareOp::NotEqual)
						|| !std::isfinite(condition->m_FloatVal))
					{
						error = "If Condition node in Graph '" + graph.name + "' has an invalid parameter or comparison";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::Switch)
				{
					const auto* switchNode = static_cast<const AnimGraphSwitchNode*>(node.get());
					if (!hasParameter(switchNode->m_ParamName, AnimatorParamType::Int)
						|| switchNode->m_CaseCount < 1)
					{
						error = "Switch node in Graph '" + graph.name + "' requires an int parameter and at least one case";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::AdditiveBlend)
				{
					const auto* blend = static_cast<const AnimGraphAdditiveBlendNode*>(node.get());
					if (!std::isfinite(blend->m_FixedWeight) || blend->m_FixedWeight < 0.0f
						|| blend->m_FixedWeight > 1.0f
						|| (blend->m_AdditiveMode!=VansAdditivePoseMode::LocalSpherical && blend->m_AdditiveMode!=VansAdditivePoseMode::LocalLinear && blend->m_AdditiveMode!=VansAdditivePoseMode::MeshRotationLinear)
						|| (blend->m_UseParam && !hasParameter(blend->m_ParamName, AnimatorParamType::Float)))
					{
						error = "Additive Blend node in Graph '" + graph.name + "' has an invalid weight source";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::SpeedScale)
				{
					const auto* speed = static_cast<const AnimGraphSpeedScaleNode*>(node.get());
					if (!std::isfinite(speed->m_FixedSpeed)
						|| (speed->m_UseParam && !hasParameter(speed->m_ParamName, AnimatorParamType::Float)))
					{
						error = "Speed Scale node in Graph '" + graph.name + "' has an invalid speed source";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::Goal)
				{
					const auto* goal = static_cast<const AnimGraphGoalNode*>(node.get());
					if (!validateGoal(goal->m_Goal, true, "Goal node in Graph '" + graph.name + "'"))
						return false;
				}
				else if (node->GetType() == VansAnimGraphNodeType::AimConstraint)
				{
					const auto* aim = static_cast<const AnimGraphAimConstraintNode*>(node.get());
					const auto& settings = aim->m_Settings;
					const bool pointMode = settings.mode == VansAimConstraintMode::LookAtPoint;
					const bool directionMode = settings.mode == VansAimConstraintMode::LookAtDirection ||
						settings.mode == VansAimConstraintMode::PitchOffset;
					if (aim->m_ChainId.empty()
						|| (!pointMode && !directionMode)
						|| (!aim->m_PivotBone.empty() && settings.mode != VansAimConstraintMode::PitchOffset)
						|| (pointMode && !validateGoal(aim->m_Target, false, "Aim Constraint in Graph '" + graph.name + "'"))
						|| (directionMode && (!hasParameter(aim->m_DirectionParameter, AnimatorParamType::Vector3) ||
							(!aim->m_DirectionWeightParameter.empty() && !hasParameter(aim->m_DirectionWeightParameter, AnimatorParamType::Float))))
						|| !std::isfinite(settings.yawLimitDegrees.x)
						|| !std::isfinite(settings.yawLimitDegrees.y)
						|| settings.yawLimitDegrees.x > settings.yawLimitDegrees.y
						|| settings.yawLimitDegrees.x < -180.0f || settings.yawLimitDegrees.y > 180.0f
						|| !std::isfinite(settings.pitchLimitDegrees.x)
						|| !std::isfinite(settings.pitchLimitDegrees.y)
						|| settings.pitchLimitDegrees.x > settings.pitchLimitDegrees.y
						|| settings.pitchLimitDegrees.x < -180.0f || settings.pitchLimitDegrees.y > 180.0f
						|| !std::isfinite(settings.maxAngularSpeedDegrees)
						|| settings.maxAngularSpeedDegrees <= 0.0f
						|| !finiteWeight(settings.weight)
						|| !std::isfinite(aim->m_TargetHalfLife) || aim->m_TargetHalfLife < 0.0f)
					{
						if (error.empty())
							error = "Aim Constraint in Graph '" + graph.name + "' has invalid settings";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::Grounding)
				{
					const auto& settings = static_cast<const AnimGraphGroundingNode*>(node.get())->m_Settings;
					std::unordered_set<std::string> contacts;
					const auto finiteNonNegative = [](float value)
					{ return std::isfinite(value) && value >= 0.0f; };
					const bool invalidContacts = settings.contacts.empty()
						|| std::any_of(settings.contacts.begin(), settings.contacts.end(),
							[&](const std::string& id) { return id.empty() || !contacts.insert(id).second; });
					if (invalidContacts || settings.query.profile.empty() || settings.query.collisionMask != 0
						|| !finiteWeight(settings.weight)
						|| !finiteNonNegative(settings.query.startDistanceAgainstApproach)
						|| !finiteNonNegative(settings.query.endDistanceAlongApproach)
						|| !finiteNonNegative(settings.query.maxStepUp)
						|| !finiteNonNegative(settings.query.maxStepDown)
						|| !finiteNonNegative(settings.query.maxPlaneResidual)
						|| !std::isfinite(settings.query.maxNormalDeviationDegrees)
						|| settings.query.maxNormalDeviationDegrees < 0.0f
						|| settings.query.maxNormalDeviationDegrees >= 90.0f
						|| !std::isfinite(settings.query.maxSlopeDegrees)
						|| settings.query.maxSlopeDegrees < 0.0f || settings.query.maxSlopeDegrees >= 90.0f
						|| !std::isfinite(settings.plant.enterPhase) || !std::isfinite(settings.plant.exitPhase)
						|| settings.plant.exitPhase < 0.0f || settings.plant.enterPhase > 1.0f
						|| settings.plant.enterPhase <= settings.plant.exitPhase
						|| !finiteNonNegative(settings.plant.unplantDistance)
						|| !finiteNonNegative(settings.plant.replantDistance)
						|| settings.plant.replantDistance > settings.plant.unplantDistance
						|| !finiteNonNegative(settings.plant.unplantAngleDegrees)
						|| !finiteNonNegative(settings.plant.replantAngleDegrees)
						|| settings.plant.replantAngleDegrees > settings.plant.unplantAngleDegrees
						|| !finiteNonNegative(settings.plant.weightHalfLife)
						|| !finiteNonNegative(settings.alignment.fullContactHeight)
						|| !finiteNonNegative(settings.alignment.contactFadeHeight)
						|| settings.alignment.contactFadeHeight <= settings.alignment.fullContactHeight
						|| !finiteNonNegative(settings.alignment.normalHalfLife)
						|| !finiteWeight(settings.alignment.rotationWeight)
						|| !finiteNonNegative(settings.pelvis.maxUpOffset)
						|| !finiteNonNegative(settings.pelvis.maxDownOffset)
						|| !finiteNonNegative(settings.pelvis.maxHorizontalOffset)
						|| !finiteNonNegative(settings.pelvis.halfLife)
						|| (settings.plant.pivot != VansPlantPivot::Heel
							&& settings.plant.pivot != VansPlantPivot::Ball
							&& settings.plant.pivot != VansPlantPivot::Ankle)
						|| (settings.plant.lockEnabled && settings.plantSignal.empty()))
					{
						error = "Grounding node in Graph '" + graph.name
							+ "' has invalid authoring settings or a runtime-only collision mask";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::LimbIK)
				{
					const auto* limb = static_cast<const AnimGraphLimbIKNode*>(node.get());
					std::unordered_set<std::string> chains;
					if (limb->m_ChainIds.empty()
						|| std::any_of(limb->m_ChainIds.begin(), limb->m_ChainIds.end(),
							[&](const std::string& id) { return id.empty() || !chains.insert(id).second; })
						|| !std::isfinite(limb->m_Settings.positionTolerance)
						|| limb->m_Settings.positionTolerance <= 0.0f
						|| !finiteWeight(limb->m_Settings.weight)
						|| (limb->m_Settings.tipRotationMode
								!= VansLimbTipRotationMode::PreserveInput
							&& limb->m_Settings.tipRotationMode
								!= VansLimbTipRotationMode::MatchGoal
							&& limb->m_Settings.tipRotationMode
								!= VansLimbTipRotationMode::FollowChain))
					{
						error = "Limb IK node in Graph '" + graph.name + "' has invalid chains or settings";
						return false;
					}
				}
				else if (node->GetType() == VansAnimGraphNodeType::ChainIK)
				{
					const auto* chain = static_cast<const AnimGraphChainIKNode*>(node.get());
					std::unordered_set<std::string> chains;
					if (chain->m_ChainIds.empty()
						|| std::any_of(chain->m_ChainIds.begin(), chain->m_ChainIds.end(),
							[&](const std::string& id) { return id.empty() || !chains.insert(id).second; })
						|| chain->m_Settings.maxIterations < 1 || chain->m_Settings.maxIterations > 64
						|| !std::isfinite(chain->m_Settings.positionTolerance)
						|| chain->m_Settings.positionTolerance <= 0.0f
						|| !finiteWeight(chain->m_Settings.weight))
					{
						error = "Chain IK node in Graph '" + graph.name + "' has invalid chains or settings";
						return false;
					}
				}
				if (node->GetType() == VansAnimGraphNodeType::RotationDistribution &&
					static_cast<const AnimGraphRotationDistributionNode*>(node.get())->m_RotationProfileId.empty())
				{ error = "Rotation Distribution requires a Rig profile id"; return false; }
				if (node->GetType() == VansAnimGraphNodeType::PoseCheckpoint)
				{
					const auto& checkpoint = *static_cast<const AnimGraphPoseCheckpointNode*>(node.get());
					std::unordered_set<std::string> bones;
					if (checkpoint.m_CheckpointId.empty() || !checkpointIds.insert(checkpoint.m_CheckpointId).second ||
						checkpoint.m_Bones.empty() || std::any_of(checkpoint.m_Bones.begin(), checkpoint.m_Bones.end(),
							[&](const auto& bone) { return bone.empty() || !bones.insert(bone).second; }))
					{ error = "Pose Checkpoint requires unique identifiers and non-empty bones"; return false; }
				}
			}
		}
		if (targetPostProcessGraphCount > 1)
		{
			error = "Animator can contain at most one Target Post Process Graph";
			return false;
		}

		if (data.layers.empty())
		{
			error = "Animator requires a Layer Stack";
			return false;
		}
		std::unordered_set<std::string> layerIds;
		std::size_t baseCount = 0;
		for (std::size_t index = 0; index < data.layers.size(); ++index)
		{
			const VansAnimationLayerDefinition& layer = data.layers[index];
			if (layer.id.empty() || layer.name.empty() || !layerIds.insert(layer.id).second)
			{
				error = "Animator Layer ids/names must be non-empty and Layer ids must be unique";
				return false;
			}
			if (layer.kind == VansAnimationLayerKind::Base)
			{
				++baseCount;
				if (index != 0)
				{
					error = "The Base Layer must be the first Layer";
					return false;
				}
			}
			else
			{
				// An empty mask is the generic full-body overlay form. It is used
				// by source-relative additive layers (for example ALS secondary
				// motion) and is expanded to a full-body runtime mask by the
				// controller. A non-empty path still requires a real asset GUID.
				Vans::VansAssetGuid maskGuid;
				const bool hasNoMask = layer.maskGuid.empty() && layer.maskPathHint.empty();
				if (!hasNoMask && !Vans::VansAssetGuid::TryParse(layer.maskGuid, maskGuid))
				{
					error = "Overlay Layer '" + layer.name + "' requires a valid Bone Mask asset GUID";
					return false;
				}
			}
			if (!std::isfinite(layer.fixedWeight) || !std::isfinite(layer.weightSmoothingTime)
				|| !std::isfinite(layer.referenceTime) || !std::isfinite(layer.eventWeightThreshold)
				|| layer.fixedWeight < 0.0f || layer.fixedWeight > 1.0f
				|| layer.weightSmoothingTime < 0.0f
				|| layer.eventWeightThreshold < 0.0f || layer.eventWeightThreshold > 1.0f)
			{
				error = "Layer '" + layer.name + "' contains an invalid numeric policy";
				return false;
			}
			if (layer.useWeightParameter)
			{
				auto parameter = parameters.find(layer.weightParameter);
				if (parameter == parameters.end() || parameter->second != AnimatorParamType::Float)
				{
					error = "Layer '" + layer.name + "' requires an existing float weight parameter";
					return false;
				}
			}
			if (!layer.dynamicAdditiveWeightParameter.empty())
			{
				auto parameter = parameters.find(layer.dynamicAdditiveWeightParameter);
				if (!layer.dynamicAdditive || parameter == parameters.end()
					|| parameter->second != AnimatorParamType::Float)
				{
					error = "Layer '" + layer.name + "' requires an existing float dynamic-additive parameter";
					return false;
				}
			}
			if ((!layer.dynamicAdditiveBaseLayerId.empty()
					|| !layer.dynamicAdditiveReferenceLayerId.empty())
				&& (!layer.dynamicAdditive
					|| layer.dynamicAdditiveBaseLayerId.empty()
					|| layer.dynamicAdditiveReferenceLayerId.empty()))
			{
				error = "Layer '" + layer.name + "' has incomplete dynamic-additive pose inputs";
				return false;
			}
			if (layer.additiveReference == VansAdditiveReferenceMode::ReferenceClip
				&& clipNames.find(layer.referenceClipName) == clipNames.end())
			{
				error = "Layer '" + layer.name + "' references an unknown additive reference clip";
				return false;
			}
		}
		if (baseCount != 1)
		{
			error = "Animator Layer Stack requires exactly one Base Layer";
			return false;
		}
		for (std::size_t index = 0; index < data.layers.size(); ++index)
		{
			const VansAnimationLayerDefinition& layer = data.layers[index];
			if (layer.sync != VansLayerSyncMode::Independent
				&& (layer.syncLeaderLayerId.empty() || layerIds.find(layer.syncLeaderLayerId) == layerIds.end()
					|| layer.syncLeaderLayerId == layer.id))
			{
				error = "Layer '" + layer.name + "' has an invalid sync leader";
				return false;
			}
			if (layer.sync == VansLayerSyncMode::Independent)
				continue;
			std::size_t leaderIndex = data.layers.size();
			for (std::size_t candidate = 0; candidate < index; ++candidate)
				if (data.layers[candidate].id == layer.syncLeaderLayerId)
				{
					leaderIndex = candidate;
					break;
				}
			if (leaderIndex == data.layers.size())
			{
				error = "Layer '" + layer.name + "' requires an earlier sync leader";
				return false;
			}
		}

		if (data.graphSets.empty() || data.defaultGraphSetId.empty())
		{
			error = "Animator requires Graph Sets and a default Graph Set ID";
			return false;
		}
		if (!std::isfinite(data.defaultGraphSetTransition.duration)
			|| data.defaultGraphSetTransition.duration < 0.0f)
		{
			error = "Default Graph Set transition duration must be finite and non-negative";
			return false;
		}
		std::unordered_set<std::string> graphSetIds;
		bool hasDefaultGraphSet = false;
		for (const VansAnimationGraphSetDefinition& graphSet : data.graphSets)
		{
			if (graphSet.id.empty() || graphSet.name.empty()
				|| !graphSetIds.insert(graphSet.id).second
				|| graphSet.bindings.size() != data.layers.size())
			{
				error = "Graph Sets require unique IDs, names, and one ordered binding per Layer";
				return false;
			}
			hasDefaultGraphSet = hasDefaultGraphSet || graphSet.id == data.defaultGraphSetId;
			for (std::size_t index = 0; index < data.layers.size(); ++index)
			{
				const VansAnimationLayerDefinition& layer = data.layers[index];
				const VansAnimationGraphBindingDefinition& binding = graphSet.bindings[index];
				if (binding.layerId != layer.id)
				{
					error = "Graph Set '" + graphSet.name + "' bindings must follow Layer Stack order";
					return false;
				}
				if (!binding.enabled)
				{
					if (layer.kind == VansAnimationLayerKind::Base || !binding.graphId.empty())
					{
						error = "Only Overlay bindings may be disabled, and disabled bindings must not reference a Graph";
						return false;
					}
					continue;
				}
				if (graphIds.find(binding.graphId) == graphIds.end()
					|| graphRoles.at(binding.graphId) != AnimatorGraphAsset::Role::Pose)
				{
					error = "Enabled Graph Set binding for Layer '" + layer.name
						+ "' must reference a Pose Graph";
					return false;
				}
				if (!layer.poseSourceLayerId.empty())
				{
					std::size_t sourceIndex = index;
					for (std::size_t candidate = 0; candidate < index; ++candidate)
						if (data.layers[candidate].id == layer.poseSourceLayerId)
						{
							sourceIndex = candidate;
							break;
						}
					if (layer.kind != VansAnimationLayerKind::Overlay
						|| layer.sync != VansLayerSyncMode::Independent
						|| sourceIndex == index || !graphSet.bindings[sourceIndex].enabled
						|| (data.layers[sourceIndex].kind != VansAnimationLayerKind::Base
							&& !data.layers[sourceIndex].updateWhenWeightIsZero)
						|| graphSet.bindings[sourceIndex].graphId != binding.graphId)
					{
						error = "Layer '" + layer.name
							+ "' requires an enabled earlier pose source bound to the same Graph";
						return false;
					}
				}
				if (!layer.weightCurveSourceLayerId.empty())
				{
					std::size_t sourceIndex = index;
					for (std::size_t source = 0; source < index; ++source)
						if (data.layers[source].id == layer.weightCurveSourceLayerId)
						{
							sourceIndex = source;
							break;
						}
					if (layer.weightCurve.empty() || sourceIndex == index
						|| !graphSet.bindings[sourceIndex].enabled
						|| !data.layers[sourceIndex].updateWhenWeightIsZero)
					{
						error = "Layer '" + layer.name + "' requires an enabled earlier curve source";
						return false;
					}
				}
				if (layer.dynamicAdditive && !layer.dynamicAdditiveBaseLayerId.empty())
				{
					auto earlierIndex = [&](const std::string& id) {
						for (std::size_t source = 0; source < index; ++source)
							if (data.layers[source].id == id && graphSet.bindings[source].enabled
								&& data.layers[source].updateWhenWeightIsZero)
								return source;
						return index;
					};
					if (earlierIndex(layer.dynamicAdditiveBaseLayerId) == index
						|| earlierIndex(layer.dynamicAdditiveReferenceLayerId) == index)
					{
						error = "Layer '" + layer.name + "' requires enabled earlier dynamic-additive pose inputs";
						return false;
					}
				}
				if (layer.sync == VansLayerSyncMode::Independent)
					continue;
				std::size_t leaderIndex = data.layers.size();
				for (std::size_t leader = 0; leader < index; ++leader)
					if (data.layers[leader].id == layer.syncLeaderLayerId)
					{
						leaderIndex = leader;
						break;
					}
				if (leaderIndex == data.layers.size() || !graphSet.bindings[leaderIndex].enabled)
				{
					error = "Synced Layer '" + layer.name
						+ "' requires an enabled earlier leader in every Graph Set";
					return false;
				}
				if (layer.sync != VansLayerSyncMode::SyncedGraph)
					continue;
				const VansAnimGraph* leaderGraph = data.FindGraph(graphSet.bindings[leaderIndex].graphId);
				const VansAnimGraph* followerGraph = data.FindGraph(binding.graphId);
				const AnimGraphStateMachineNode* leaderStateMachine = leaderGraph
					? FindPrimaryStateMachine(*leaderGraph) : nullptr;
				const AnimGraphStateMachineNode* followerStateMachine = followerGraph
					? FindPrimaryStateMachine(*followerGraph) : nullptr;
				if (!leaderStateMachine || !followerStateMachine)
				{
					error = "Synced Graph Layer '" + layer.name
						+ "' requires primary State Machine nodes on both bindings";
					return false;
				}
				std::unordered_set<std::string> leaderStates;
				std::unordered_set<std::string> followerStates;
				for (const AnimatorState& state : leaderStateMachine->m_States)
					leaderStates.insert(state.name);
				for (const AnimatorState& state : followerStateMachine->m_States)
					followerStates.insert(state.name);
				if (leaderStates != followerStates)
				{
					error = "Synced Graph Layer '" + layer.name
						+ "' must expose the same logical State names as its leader";
					return false;
				}
			}
		}
		if (!hasDefaultGraphSet)
		{
			error = "defaultGraphSetId does not reference a Graph Set";
			return false;
		}
		std::unordered_set<std::string> transitionPairs;
		for (const VansGraphSetTransitionRule& rule : data.graphSetTransitionRules)
		{
			if (graphSetIds.find(rule.fromGraphSetId) == graphSetIds.end()
				|| graphSetIds.find(rule.toGraphSetId) == graphSetIds.end()
				|| rule.fromGraphSetId == rule.toGraphSetId
				|| !std::isfinite(rule.policy.duration) || rule.policy.duration < 0.0f
				|| !transitionPairs.insert(rule.fromGraphSetId + "\n" + rule.toGraphSetId).second)
			{
				error = "Graph Set transition rules require unique valid source/target pairs and durations";
				return false;
			}
		}

		std::unordered_set<std::string> slotIds;
		for (const VansAnimationSlotDefinition& slot : data.slots)
		{
			if (slot.id.empty() || slot.name.empty() || !slotIds.insert(slot.id).second
				|| layerIds.find(slot.layerId) == layerIds.end()
				|| !std::isfinite(slot.defaultBlendIn) || slot.defaultBlendIn < 0.0f
				|| !std::isfinite(slot.defaultBlendOut) || slot.defaultBlendOut < 0.0f)
			{
				error = "Animator contains an invalid Slot definition";
				return false;
			}
		}
		for (const VansAnimationLayerDefinition& layer : data.layers)
			if (!layer.slotId.empty() && slotIds.find(layer.slotId) == slotIds.end())
			{
				error = "Layer '" + layer.name + "' references an unknown Slot";
				return false;
			}
		for (const VansAnimationGraphSetDefinition& graphSet : data.graphSets)
		{
			for (std::size_t layerIndex = 0; layerIndex < data.layers.size(); ++layerIndex)
			{
				const VansAnimationGraphBindingDefinition& binding = graphSet.bindings[layerIndex];
				if (!binding.enabled)
					continue;
				const VansAnimationLayerDefinition& layer = data.layers[layerIndex];
				const VansAnimGraph* graph = data.FindGraph(binding.graphId);
				std::unordered_map<std::string, std::size_t> graphSlotCounts;
				for (const auto& [nodeId, node] : graph->GetNodes())
					if (node && node->GetType() == VansAnimGraphNodeType::Slot)
						++graphSlotCounts[static_cast<const AnimGraphSlotNode*>(node.get())->m_SlotId];
				if (!layer.slotId.empty())
					++graphSlotCounts[layer.slotId];
				for (const VansAnimationSlotDefinition& slot : data.slots)
				{
					if (slot.layerId != layer.id)
						continue;
					if (graphSlotCounts[slot.id] != 1)
					{
						error = "Slot '" + slot.name
							+ "' requires exactly one semantic Slot node in Graph '"
							+ binding.graphId + "' of Graph Set '" + graphSet.name + "'";
						return false;
					}
				}
				for (const auto& [slotId, count] : graphSlotCounts)
				{
					if (slotId.empty() || count != 1
						|| std::none_of(data.slots.begin(), data.slots.end(),
							[&](const VansAnimationSlotDefinition& slot)
							{ return slot.id == slotId && slot.layerId == layer.id; }))
					{
						error = "Slot nodes in enabled Graph bindings require one matching semantic Slot definition";
						return false;
					}
				}
			}
		}
		return true;
	}

}
