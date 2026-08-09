#include "VansAnimGraph.h"
#include "VansAnimationController.h"
#include "VansAnimationSampler.h"
#include "VansAnimationLayer.h"
#include "VansPoseMath.h"
#include "VansPosePayloadMixer.h"
#include "IK/VansIKSolver.h"
#include "IK/VansCCDSolver.h"
#include "IK/VansFABRIKSolver.h"
#include "IK/VansLookAtSolver.h"
#include "IK/VansTwoBoneIKSolver.h"
#include "IK/VansIKChainBuilder.h"
#include "IK/VansIKConstraint.h"
#include "MotionMatching/VansMotionMatching.h"
#include <../../GLM/gtc/constants.hpp>
#include <../../GLM/gtc/quaternion.hpp>
#include <../../GLM/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <../../GLM/gtx/quaternion.hpp>
#include <../../GLM/gtx/matrix_decompose.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace VansGraphics
{
	// ═════════════════════════════════════════════════════════════
	//  工具函数
	// ═════════════════════════════════════════════════════════════

	static const char* IKSolverTypeToString(IKSolverType type)
	{
		switch (type)
		{
		case IKSolverType::TwoBone: return "TwoBone";
		case IKSolverType::CCD: return "CCD";
		case IKSolverType::FABRIK: return "FABRIK";
		case IKSolverType::LookAt: return "LookAt";
		}
		return "CCD";
	}

	static IKSolverType StringToIKSolverType(const std::string& value)
	{
		if (value == "TwoBone") return IKSolverType::TwoBone;
		if (value == "FABRIK") return IKSolverType::FABRIK;
		if (value == "LookAt") return IKSolverType::LookAt;
		return IKSolverType::CCD;
	}

	static const char* IKSpaceToString(IKCoordinateSpace space)
	{
		switch (space)
		{
		case IKCoordinateSpace::Model: return "Model";
		case IKCoordinateSpace::World: return "World";
		case IKCoordinateSpace::Bone: return "Bone";
		case IKCoordinateSpace::ParentBone: return "ParentBone";
		}
		return "Model";
	}

	static IKCoordinateSpace StringToIKSpace(const std::string& value)
	{
		if (value == "World") return IKCoordinateSpace::World;
		if (value == "Bone") return IKCoordinateSpace::Bone;
		if (value == "ParentBone") return IKCoordinateSpace::ParentBone;
		return IKCoordinateSpace::Model;
	}

	// ═════════════════════════════════════════════════════════════
	//  节点类型名称映射
	// ═════════════════════════════════════════════════════════════

	const char* VansAnimGraphNode::TypeToString(AnimGraphNodeType type)
	{
		switch (type)
		{
		case AnimGraphNodeType::Entry:          return "Entry";
		case AnimGraphNodeType::Output:         return "Output";
		case AnimGraphNodeType::Clip:           return "Clip";
		case AnimGraphNodeType::Blend:          return "Blend";
		case AnimGraphNodeType::Blend1D:        return "Blend1D";
		case AnimGraphNodeType::IfCondition:    return "IfCondition";
		case AnimGraphNodeType::Switch:         return "Switch";
		case AnimGraphNodeType::AdditiveBlend:  return "AdditiveBlend";
		case AnimGraphNodeType::SpeedScale:     return "SpeedScale";
		case AnimGraphNodeType::StateMachine:   return "StateMachine";
		case AnimGraphNodeType::MotionMatching: return "MotionMatching";
		case AnimGraphNodeType::Slot:           return "Slot";
		case AnimGraphNodeType::TargetPoseInput:return "TargetPoseInput";
		case AnimGraphNodeType::IK:             return "IK";
		case AnimGraphNodeType::TwoBoneIK:      return "TwoBoneIK";
		case AnimGraphNodeType::LookAt:         return "LookAt";
		case AnimGraphNodeType::FootPlacement:  return "FootPlacement";
		}
		return "Unknown";
	}

	// ═════════════════════════════════════════════════════════════
	//  EntryNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphEntryNode::AnimGraphEntryNode()
	{
		m_Type = AnimGraphNodeType::Entry;
		m_Name = "Entry";
	}

	std::vector<AnimGraphPin> AnimGraphEntryNode::GetPins() const
	{
		// Entry 只有一个 Output Pin（连接到图中的第一个处理节点）
		return { { 0, "Out", AnimGraphPinType::Pose, AnimGraphPinKind::Output } };
	}

	AnimGraphPose AnimGraphEntryNode::Evaluate(const AnimGraphContext& ctx,
	                                           VansAnimGraphInstance& instance) const
	{
		// Entry 不产生数据，返回空 Pose
		return {};
	}

	// ═════════════════════════════════════════════════════════════
	//  OutputNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphOutputNode::AnimGraphOutputNode()
	{
		m_Type = AnimGraphNodeType::Output;
		m_Name = "Output";
	}

	std::vector<AnimGraphPin> AnimGraphOutputNode::GetPins() const
	{
		// Output 只有一个 Input Pin（接收最终 Pose）
		return { { 0, "In", AnimGraphPinType::Pose, AnimGraphPinKind::Input } };
	}

	AnimGraphPose AnimGraphOutputNode::Evaluate(const AnimGraphContext& ctx,
	                                            VansAnimGraphInstance& instance) const
	{
		return instance.EvaluateInput(m_NodeId, 0, ctx);
	}

	// ═════════════════════════════════════════════════════════════
	//  ClipNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphClipNode::AnimGraphClipNode()
	{
		m_Type = AnimGraphNodeType::Clip;
		m_Name = "Clip";
	}

	std::vector<AnimGraphPin> AnimGraphClipNode::GetPins() const
	{
		// 只有一个 Output Pin（输出采样后的 Pose）
		return { { 0, "Pose", AnimGraphPinType::Pose, AnimGraphPinKind::Output } };
	}

	AnimGraphPose AnimGraphClipNode::Evaluate(const AnimGraphContext& ctx,
	                                          VansAnimGraphInstance& instance) const
	{
		AnimGraphPose pose;
		if (!ctx.clips || !ctx.skeleton) return pose;
		auto it = ctx.clips->find(m_ClipName);
		if (it == ctx.clips->end()) return pose;
		const VansAnimGraphClipRuntimeState& runtime = instance.GetClipState(m_NodeId);
		VansAnimationSampleRequest request;
		request.previousTime = runtime.previousTime;
		request.currentTime = runtime.currentTime;
		request.loop = m_Loop;
		request.sourceNodeId = static_cast<std::uint64_t>(m_NodeId);
		VansAnimationSampler::Sample(it->second, *ctx.skeleton, request, pose);
		return pose;
	}

	// ═════════════════════════════════════════════════════════════
	//  BlendNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphBlendNode::AnimGraphBlendNode()
	{
		m_Type = AnimGraphNodeType::Blend;
		m_Name = "Blend";
	}

	std::vector<AnimGraphPin> AnimGraphBlendNode::GetPins() const
	{
		return {
			{ 0, "Pose A",  AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 1, "Pose B",  AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 0, "Result",  AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphBlendNode::Evaluate(const AnimGraphContext& ctx,
	                                           VansAnimGraphInstance& instance) const
	{
		AnimGraphPose poseA = instance.EvaluateInput(m_NodeId, 0, ctx);
		AnimGraphPose poseB = instance.EvaluateInput(m_NodeId, 1, ctx);

		if (!poseA.valid) return poseB;
		if (!poseB.valid) return poseA;

		// 确定 alpha
		float alpha = m_FixedAlpha;
		if (m_UseParam && ctx.parameters)
		{
			auto it = ctx.parameters->find(m_ParamName);
			if (it != ctx.parameters->end() && it->second.type == AnimatorParamType::Float)
				alpha = it->second.floatVal;
		}

		return VansPosePayloadMixer::BlendOverride(poseA, poseB, alpha);
	}

	// ═════════════════════════════════════════════════════════════
	//  Blend1DNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphBlend1DNode::AnimGraphBlend1DNode()
	{
		m_Type = AnimGraphNodeType::Blend1D;
		m_Name = "Blend1D";
	}

	std::vector<AnimGraphPin> AnimGraphBlend1DNode::GetPins() const
	{
		std::vector<AnimGraphPin> pins;
		// N 个输入 Pose Pin
		for (int i = 0; i < static_cast<int>(m_Thresholds.size()); ++i)
		{
			pins.push_back({
				i,
				"Pose " + std::to_string(i),
				AnimGraphPinType::Pose,
				AnimGraphPinKind::Input
			});
		}
		// 1 个输出 Pose Pin
		pins.push_back({ 0, "Result", AnimGraphPinType::Pose, AnimGraphPinKind::Output });
		return pins;
	}

	AnimGraphPose AnimGraphBlend1DNode::Evaluate(const AnimGraphContext& ctx,
	                                             VansAnimGraphInstance& instance) const
	{
		if (m_Thresholds.empty()) return {};

		// 获取参数值
		float paramValue = 0.0f;
		if (ctx.parameters)
		{
			auto it = ctx.parameters->find(m_ParamName);
			if (it != ctx.parameters->end() && it->second.type == AnimatorParamType::Float)
				paramValue = it->second.floatVal;
		}

		int count = static_cast<int>(m_Thresholds.size());
		if (count == 1)
		{
			// 只有一个入口，直接输出
			return instance.EvaluateInput(m_NodeId, 0, ctx);
		}

		// 找到 paramValue 落在哪两个阈值之间
		if (paramValue <= m_Thresholds.front())
		{
			return instance.EvaluateInput(m_NodeId, 0, ctx);
		}
		if (paramValue >= m_Thresholds.back())
		{
			return instance.EvaluateInput(m_NodeId, count - 1, ctx);
		}

		for (int i = 0; i < count - 1; ++i)
		{
			if (paramValue >= m_Thresholds[i] && paramValue <= m_Thresholds[i + 1])
			{
				float range = m_Thresholds[i + 1] - m_Thresholds[i];
				float alpha = (range > 0.0001f)
					? (paramValue - m_Thresholds[i]) / range
					: 0.0f;

				AnimGraphPose poseA = instance.EvaluateInput(m_NodeId, i, ctx);
				AnimGraphPose poseB = instance.EvaluateInput(m_NodeId, i + 1, ctx);

				if (!poseA.valid) return poseB;
				if (!poseB.valid) return poseA;

				return VansPosePayloadMixer::BlendOverride(poseA, poseB, alpha);
			}
		}

		return {};
	}

	// ═════════════════════════════════════════════════════════════
	//  IfConditionNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphIfConditionNode::AnimGraphIfConditionNode()
	{
		m_Type = AnimGraphNodeType::IfCondition;
		m_Name = "IfCondition";
	}

	std::vector<AnimGraphPin> AnimGraphIfConditionNode::GetPins() const
	{
		return {
			{ 0, "True",   AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 1, "False",  AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 0, "Result", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphIfConditionNode::Evaluate(const AnimGraphContext& ctx,
	                                                 VansAnimGraphInstance& instance) const
	{
		bool condResult = false;

		if (ctx.parameters)
		{
			auto it = ctx.parameters->find(m_ParamName);
			if (it != ctx.parameters->end())
			{
				const AnimatorParameter& param = it->second;
				switch (param.type)
				{
				case AnimatorParamType::Float:
				{
					float a = param.floatVal;
					float b = m_FloatVal;
					switch (m_CompareOp)
					{
					case CompareOp::Greater:      condResult = a > b;  break;
					case CompareOp::Less:         condResult = a < b;  break;
					case CompareOp::Equal:        condResult = std::abs(a - b) < 0.0001f; break;
					case CompareOp::NotEqual:     condResult = std::abs(a - b) >= 0.0001f; break;
					case CompareOp::GreaterEqual: condResult = a >= b; break;
					case CompareOp::LessEqual:    condResult = a <= b; break;
					}
					break;
				}
				case AnimatorParamType::Bool:
				case AnimatorParamType::Trigger:
					condResult = (m_CompareOp == CompareOp::Equal)
						? (param.boolVal == m_BoolVal)
						: (param.boolVal != m_BoolVal);
					break;
				case AnimatorParamType::Int:
				{
					int a = param.intVal;
					int b = m_IntVal;
					switch (m_CompareOp)
					{
					case CompareOp::Greater:      condResult = a > b;  break;
					case CompareOp::Less:         condResult = a < b;  break;
					case CompareOp::Equal:        condResult = a == b; break;
					case CompareOp::NotEqual:     condResult = a != b; break;
					case CompareOp::GreaterEqual: condResult = a >= b; break;
					case CompareOp::LessEqual:    condResult = a <= b; break;
					}
					break;
				}
				}
			}
		}

		int pinIndex = condResult ? 0 : 1;
		return instance.EvaluateInput(m_NodeId, pinIndex, ctx);
	}

	// ═════════════════════════════════════════════════════════════
	//  SwitchNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphSwitchNode::AnimGraphSwitchNode()
	{
		m_Type = AnimGraphNodeType::Switch;
		m_Name = "Switch";
	}

	std::vector<AnimGraphPin> AnimGraphSwitchNode::GetPins() const
	{
		std::vector<AnimGraphPin> pins;
		for (int i = 0; i < m_CaseCount; ++i)
		{
			pins.push_back({
				i,
				"Case " + std::to_string(i),
				AnimGraphPinType::Pose,
				AnimGraphPinKind::Input
			});
		}
		pins.push_back({ 0, "Result", AnimGraphPinType::Pose, AnimGraphPinKind::Output });
		return pins;
	}

	AnimGraphPose AnimGraphSwitchNode::Evaluate(const AnimGraphContext& ctx,
	                                            VansAnimGraphInstance& instance) const
	{
		int selectedCase = 0;
		if (ctx.parameters)
		{
			auto it = ctx.parameters->find(m_ParamName);
			if (it != ctx.parameters->end() && it->second.type == AnimatorParamType::Int)
				selectedCase = it->second.intVal;
		}

		// clamp 到有效范围
		selectedCase = std::clamp(selectedCase, 0, m_CaseCount - 1);

		return instance.EvaluateInput(m_NodeId, selectedCase, ctx);
	}

	// ═════════════════════════════════════════════════════════════
	//  AdditiveBlendNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphAdditiveBlendNode::AnimGraphAdditiveBlendNode()
	{
		m_Type = AnimGraphNodeType::AdditiveBlend;
		m_Name = "AdditiveBlend";
	}

	std::vector<AnimGraphPin> AnimGraphAdditiveBlendNode::GetPins() const
	{
		return {
			{ 0, "Base",     AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 1, "Additive", AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 0, "Result",   AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphAdditiveBlendNode::Evaluate(const AnimGraphContext& ctx,
	                                                   VansAnimGraphInstance& instance) const
	{
		AnimGraphPose basePose = instance.EvaluateInput(m_NodeId, 0, ctx);
		AnimGraphPose additivePose = instance.EvaluateInput(m_NodeId, 1, ctx);

		if (!basePose.valid) return basePose;
		if (!additivePose.valid) return basePose;

		float weight = m_FixedWeight;
		if (m_UseParam && ctx.parameters)
		{
			auto it = ctx.parameters->find(m_ParamName);
			if (it != ctx.parameters->end() && it->second.type == AnimatorParamType::Float)
				weight = it->second.floatVal;
		}

		return VansPosePayloadMixer::ApplyAdditive(basePose, additivePose, weight);
	}

	// ═════════════════════════════════════════════════════════════
	//  SpeedScaleNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphSpeedScaleNode::AnimGraphSpeedScaleNode()
	{
		m_Type = AnimGraphNodeType::SpeedScale;
		m_Name = "SpeedScale";
	}

	std::vector<AnimGraphPin> AnimGraphSpeedScaleNode::GetPins() const
	{
		return {
			{ 0, "Pose",   AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 0, "Result", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphSpeedScaleNode::Evaluate(const AnimGraphContext& ctx,
	                                                VansAnimGraphInstance& instance) const
	{
		float speed = m_FixedSpeed;
		if (m_UseParam && ctx.parameters)
		{
			auto parameter = ctx.parameters->find(m_ParamName);
			if (parameter != ctx.parameters->end()
			    && parameter->second.type == AnimatorParamType::Float)
				speed = parameter->second.floatVal;
		}
		AnimGraphContext scaledContext = ctx;
		scaledContext.deltaTime *= speed;
		return instance.EvaluateInput(m_NodeId, 0, scaledContext);
	}

	// ═════════════════════════════════════════════════════════════
	//  StateMachineNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphStateMachineNode::AnimGraphStateMachineNode()
	{
		m_Type = AnimGraphNodeType::StateMachine;
		m_Name = "StateMachine";
	}

	std::vector<AnimGraphPin> AnimGraphStateMachineNode::GetPins() const
	{
		return { { 0, "Pose", AnimGraphPinType::Pose, AnimGraphPinKind::Output } };
	}

	AnimGraphPose AnimGraphStateMachineNode::Evaluate(const AnimGraphContext& ctx,
	                                                  VansAnimGraphInstance& instance) const
	{
		if (!ctx.clips || !ctx.skeleton)
			return {};

		VansAnimGraphStateMachineRuntimeState& runtime =
			instance.GetStateMachineState(m_NodeId, *this);
		auto findState = [this](const std::string& name) -> const AnimatorState*
		{
			for (const AnimatorState& state : m_States)
				if (state.name == name)
					return &state;
			return nullptr;
		};
		auto conditionsPass = [&ctx](const AnimatorTransition& transition)
		{
			if (!ctx.parameters)
				return transition.conditions.empty();
			for (const TransitionCondition& condition : transition.conditions)
			{
				auto parameterIt = ctx.parameters->find(condition.paramName);
				if (parameterIt == ctx.parameters->end())
					return false;
				const AnimatorParameter& parameter = parameterIt->second;
				bool satisfied = false;
				switch (parameter.type)
				{
				case AnimatorParamType::Float:
					switch (condition.op)
					{
					case CompareOp::Greater: satisfied = parameter.floatVal > condition.floatVal; break;
					case CompareOp::Less: satisfied = parameter.floatVal < condition.floatVal; break;
					case CompareOp::Equal: satisfied = std::abs(parameter.floatVal - condition.floatVal) < 0.0001f; break;
					case CompareOp::NotEqual: satisfied = std::abs(parameter.floatVal - condition.floatVal) >= 0.0001f; break;
					case CompareOp::GreaterEqual: satisfied = parameter.floatVal >= condition.floatVal; break;
					case CompareOp::LessEqual: satisfied = parameter.floatVal <= condition.floatVal; break;
					}
					break;
				case AnimatorParamType::Bool:
				case AnimatorParamType::Trigger:
					satisfied = condition.op == CompareOp::Equal
						? parameter.boolVal == condition.boolVal
						: parameter.boolVal != condition.boolVal;
					break;
				case AnimatorParamType::Int:
					switch (condition.op)
					{
					case CompareOp::Greater: satisfied = parameter.intVal > condition.intVal; break;
					case CompareOp::Less: satisfied = parameter.intVal < condition.intVal; break;
					case CompareOp::Equal: satisfied = parameter.intVal == condition.intVal; break;
					case CompareOp::NotEqual: satisfied = parameter.intVal != condition.intVal; break;
					case CompareOp::GreaterEqual: satisfied = parameter.intVal >= condition.intVal; break;
					case CompareOp::LessEqual: satisfied = parameter.intVal <= condition.intVal; break;
					}
					break;
				case AnimatorParamType::Vector3:
				case AnimatorParamType::Quaternion:
					return false;
				}
				if (!satisfied)
					return false;
			}
			return true;
		};
		auto startTransition = [&](const AnimatorTransition& transition)
		{
			runtime.previousStateName = runtime.currentStateName;
			runtime.currentStateName = transition.toState;
			runtime.blendAlpha = 0.0f;
			runtime.blendDuration = std::max(0.0f, transition.blendDuration);
			runtime.blendState = runtime.blendDuration > 0.0f
				? ControllerBlendState::Blending
				: ControllerBlendState::Idle;
			if (const AnimatorState* target = findState(runtime.currentStateName))
			{
				runtime.stateTimes[target->name] = target->startTime;
				runtime.previousStateTimes[target->name] = target->startTime;
			}
			if (ctx.parameters)
			{
				for (const TransitionCondition& condition : transition.conditions)
				{
					auto parameterIt = ctx.parameters->find(condition.paramName);
					if (parameterIt != ctx.parameters->end()
					    && parameterIt->second.type == AnimatorParamType::Trigger)
						parameterIt->second.boolVal = false;
				}
			}
		};

		if (!ctx.synchronizedStateFollower)
		{
			for (const AnimatorTransition& transition : m_Transitions)
			{
				const bool fromAny = transition.fromState == "*"
					&& transition.toState != runtime.currentStateName;
				const bool fromCurrent = transition.fromState == runtime.currentStateName;
				if (!fromAny && !fromCurrent)
					continue;
				if (fromCurrent && transition.hasExitTime)
				{
					const AnimatorState* current = findState(runtime.currentStateName);
					if (!current)
						continue;
					auto clipIt = ctx.clips->find(current->clipName);
					if (clipIt != ctx.clips->end() && clipIt->second.duration > 0.0f)
					{
						const float normalizedTime = runtime.stateTimes[current->name] / clipIt->second.duration;
						if (normalizedTime < transition.exitTime)
							continue;
					}
				}
				if (conditionsPass(transition))
				{
					startTransition(transition);
					break;
				}
			}
		}

			auto sampleState = [&](const AnimatorState& state) -> AnimGraphPose
			{
				AnimGraphPose pose;
				auto clipIt = ctx.clips->find(state.clipName);
				if (clipIt == ctx.clips->end())
					return pose;
				const VansAnimationClip& clip = clipIt->second;
				const float start = state.startTime;
				const float end = state.endTime < 0.0f ? clip.duration : state.endTime;
				VansAnimationSampleRequest request;
				request.previousTime = runtime.previousStateTimes[state.name];
				request.currentTime = runtime.stateTimes[state.name];
				request.startTime = start;
				request.endTime = end;
				request.loop = state.loop;
				request.sourceNodeId = static_cast<std::uint64_t>(m_NodeId);
				VansAnimationSampler::Sample(clip, *ctx.skeleton, request, pose);
				return pose;
			};

		const AnimatorState* current = findState(runtime.currentStateName);
		if (!current)
			return {};
		AnimGraphPose currentPose = sampleState(*current);
		if (runtime.blendState != ControllerBlendState::Blending)
			return currentPose;

		const AnimatorState* previous = findState(runtime.previousStateName);
		if (!previous)
		{
			runtime.blendState = ControllerBlendState::Idle;
			return currentPose;
		}
		if (!ctx.synchronizedStateFollower)
		{
			runtime.blendAlpha = runtime.blendDuration <= 0.0f
				? 1.0f
				: std::min(1.0f, runtime.blendAlpha + ctx.deltaTime / runtime.blendDuration);
			if (runtime.blendAlpha >= 1.0f)
				runtime.blendState = ControllerBlendState::Idle;
		}
		return VansPosePayloadMixer::BlendOverride(
			sampleState(*previous), currentPose, runtime.blendAlpha);
	}

	// ═════════════════════════════════════════════════════════════
	//  VansAnimGraph
	// ═════════════════════════════════════════════════════════════

	VansAnimGraph::VansAnimGraph()  = default;
	VansAnimGraph::~VansAnimGraph() = default;

	int VansAnimGraph::AddNode(std::unique_ptr<VansAnimGraphNode> node)
	{
		if (!node)
			return -1;
		if (node->GetType() == AnimGraphNodeType::Entry && m_EntryNodeId >= 0)
			return -1;
		if (node->GetType() == AnimGraphNodeType::Output && m_OutputNodeId >= 0)
			return -1;

		int id = m_NextNodeId++;
		node->m_NodeId = id;

		// 自动记录 Entry / Output 节点
		if (node->GetType() == AnimGraphNodeType::Entry)
			m_EntryNodeId = id;
		else if (node->GetType() == AnimGraphNodeType::Output)
			m_OutputNodeId = id;

		m_Nodes[id] = std::move(node);
		return id;
	}

	bool VansAnimGraph::AddNodeWithId(std::unique_ptr<VansAnimGraphNode> node, int nodeId)
	{
		if (!node || nodeId <= 0 || m_Nodes.find(nodeId) != m_Nodes.end())
			return false;
		if (node->GetType() == AnimGraphNodeType::Entry && m_EntryNodeId >= 0)
			return false;
		if (node->GetType() == AnimGraphNodeType::Output && m_OutputNodeId >= 0)
			return false;

		node->m_NodeId = nodeId;
		if (node->GetType() == AnimGraphNodeType::Entry)
			m_EntryNodeId = nodeId;
		else if (node->GetType() == AnimGraphNodeType::Output)
			m_OutputNodeId = nodeId;
		m_Nodes.emplace(nodeId, std::move(node));
		m_NextNodeId = std::max(m_NextNodeId, nodeId + 1);
		return true;
	}

	void VansAnimGraph::RemoveNode(int nodeId)
	{
		// 删除相关连线
		m_Links.erase(
			std::remove_if(m_Links.begin(), m_Links.end(),
				[nodeId](const AnimGraphLink& l) {
					return l.fromNodeId == nodeId || l.toNodeId == nodeId;
				}),
			m_Links.end());

		if (nodeId == m_EntryNodeId)  m_EntryNodeId = -1;
		if (nodeId == m_OutputNodeId) m_OutputNodeId = -1;

		m_Nodes.erase(nodeId);
	}

	VansAnimGraphNode* VansAnimGraph::GetNode(int nodeId)
	{
		auto it = m_Nodes.find(nodeId);
		return (it != m_Nodes.end()) ? it->second.get() : nullptr;
	}

	const VansAnimGraphNode* VansAnimGraph::GetNode(int nodeId) const
	{
		auto it = m_Nodes.find(nodeId);
		return (it != m_Nodes.end()) ? it->second.get() : nullptr;
	}

	int VansAnimGraph::AddLink(int fromNodeId, int fromPinIndex, int toNodeId, int toPinIndex)
	{
		VansAnimGraphNode* fromNode = GetNode(fromNodeId);
		VansAnimGraphNode* toNode = GetNode(toNodeId);
		if (!fromNode || !toNode || fromNodeId == toNodeId)
			return -1;

		const std::vector<AnimGraphPin> fromPins = fromNode->GetPins();
		const std::vector<AnimGraphPin> toPins = toNode->GetPins();
		const AnimGraphPin* outputPin = nullptr;
		const AnimGraphPin* inputPin = nullptr;
		for (const AnimGraphPin& pin : fromPins)
		{
			if (pin.kind == AnimGraphPinKind::Output && pin.pinIndex == fromPinIndex)
			{
				outputPin = &pin;
				break;
			}
		}
		for (const AnimGraphPin& pin : toPins)
		{
			if (pin.kind == AnimGraphPinKind::Input && pin.pinIndex == toPinIndex)
			{
				inputPin = &pin;
				break;
			}
		}
		if (!outputPin || !inputPin || outputPin->type != inputPin->type)
			return -1;

		// 检查目标输入 Pin 是否已有连线（一个输入只能有一条连线）
		for (const auto& link : m_Links)
		{
			if (link.toNodeId == toNodeId && link.toPinIndex == toPinIndex)
				return -1;  // 已有连线，拒绝
		}

		// 新边 from -> to；若已有 to -> ... -> from 路径则会形成环。
		std::vector<int> pending{ toNodeId };
		std::unordered_set<int> visited;
		while (!pending.empty())
		{
			const int current = pending.back();
			pending.pop_back();
			if (current == fromNodeId)
				return -1;
			if (!visited.insert(current).second)
				continue;
			for (const AnimGraphLink& existing : m_Links)
			{
				if (existing.fromNodeId == current)
					pending.push_back(existing.toNodeId);
			}
		}

		AnimGraphLink link;
		link.linkId       = m_NextLinkId++;
		link.fromNodeId   = fromNodeId;
		link.fromPinIndex = fromPinIndex;
		link.toNodeId     = toNodeId;
		link.toPinIndex   = toPinIndex;
		m_Links.push_back(link);
		return link.linkId;
	}

	bool VansAnimGraph::AddLinkWithId(int linkId, int fromNodeId, int fromPinIndex,
	                                  int toNodeId, int toPinIndex)
	{
		if (linkId <= 0 || std::any_of(m_Links.begin(), m_Links.end(),
			[linkId](const AnimGraphLink& link) { return link.linkId == linkId; }))
			return false;
		const int generatedId = AddLink(fromNodeId, fromPinIndex, toNodeId, toPinIndex);
		if (generatedId < 0)
			return false;
		m_Links.back().linkId = linkId;
		m_NextLinkId = std::max(m_NextLinkId, linkId + 1);
		return true;
	}

	void VansAnimGraph::RemoveLink(int linkId)
	{
		m_Links.erase(
			std::remove_if(m_Links.begin(), m_Links.end(),
				[linkId](const AnimGraphLink& l) { return l.linkId == linkId; }),
			m_Links.end());
	}

	const VansAnimGraphNode* VansAnimGraph::GetInputNode(int nodeId, int inputPinIndex) const
	{
		for (const auto& link : m_Links)
		{
			if (link.toNodeId == nodeId && link.toPinIndex == inputPinIndex)
			{
				auto it = m_Nodes.find(link.fromNodeId);
				if (it != m_Nodes.end())
					return it->second.get();
			}
		}
		return nullptr;
	}

	bool VansAnimGraph::BuildExecutionPlan(std::vector<int>& outPlan, std::string& outError) const
	{
		outPlan.clear();
		outError.clear();
		if (m_OutputNodeId < 0 || !GetNode(m_OutputNodeId))
		{
			outError = "Animation graph requires exactly one Output node";
			return false;
		}

		std::unordered_map<int, int> visitState;
		std::function<bool(int)> visit = [&](int nodeId)
		{
			int& state = visitState[nodeId];
			if (state == 2)
				return true;
			if (state == 1)
			{
				outError = "Animation graph contains a directed cycle at node " + std::to_string(nodeId);
				return false;
			}
			if (!GetNode(nodeId))
			{
				outError = "Animation graph execution plan references missing node " + std::to_string(nodeId);
				return false;
			}

			state = 1;
			std::vector<const AnimGraphLink*> inputs;
			for (const AnimGraphLink& link : m_Links)
				if (link.toNodeId == nodeId)
					inputs.push_back(&link);
			std::sort(inputs.begin(), inputs.end(), [](const AnimGraphLink* first, const AnimGraphLink* second)
			{
				if (first->toPinIndex != second->toPinIndex)
					return first->toPinIndex < second->toPinIndex;
				return first->fromNodeId < second->fromNodeId;
			});
			for (const AnimGraphLink* link : inputs)
				if (!visit(link->fromNodeId))
					return false;
			state = 2;
			outPlan.push_back(nodeId);
			return true;
		};

		if (!visit(m_OutputNodeId))
			return false;
		for (const auto& [nodeId, node] : m_Nodes)
		{
			if (node && node->GetType() != AnimGraphNodeType::Entry
			    && visitState[nodeId] != 2)
			{
				outError = "Animation graph contains unreachable node " + std::to_string(nodeId);
				outPlan.clear();
				return false;
			}
		}

		// A stateful source may be shared, but every path to it must cross the same
		// SpeedScale nodes. Otherwise a single playback clock would have two speeds.
		std::unordered_map<int, std::vector<int>> sourceSpeedPaths;
		std::function<bool(int, std::vector<int>)> validateSpeedPath =
			[&](int nodeId, std::vector<int> speedPath)
		{
			const VansAnimGraphNode* node = GetNode(nodeId);
			if (!node)
				return false;
			if (node->GetType() == AnimGraphNodeType::SpeedScale)
				speedPath.push_back(nodeId);
			const bool statefulSource = node->GetType() == AnimGraphNodeType::Clip
				|| node->GetType() == AnimGraphNodeType::StateMachine
				|| node->GetType() == AnimGraphNodeType::MotionMatching;
			if (statefulSource)
			{
				auto [found, inserted] = sourceSpeedPaths.emplace(nodeId, speedPath);
				if (!inserted && found->second != speedPath)
				{
					outError = "Animation graph routes stateful node " + std::to_string(nodeId)
						+ " through conflicting SpeedScale paths";
					return false;
				}
			}
			for (const AnimGraphLink& link : m_Links)
				if (link.toNodeId == nodeId && !validateSpeedPath(link.fromNodeId, speedPath))
					return false;
			return true;
		};
		if (!validateSpeedPath(m_OutputNodeId, {}))
		{
			outPlan.clear();
			return false;
		}
		return true;
	}

	VansAnimGraphInstance::VansAnimGraphInstance(const VansAnimGraph& definition)
		: m_Definition(definition)
	{
		m_Definition.BuildExecutionPlan(m_ExecutionPlan, m_CompileError);
		m_EvaluationCache.reserve(m_ExecutionPlan.size());
		m_EvaluatedNodes.reserve(m_ExecutionPlan.size());
		m_EvaluatingNodes.reserve(m_ExecutionPlan.size());
		m_PreviousActiveNodes.reserve(m_ExecutionPlan.size());
		m_ActiveTimeScales.reserve(m_ExecutionPlan.size());
		m_HasActiveTimeScale.reserve(m_ExecutionPlan.size());
		for (int nodeId : m_ExecutionPlan)
		{
			m_EvaluationCache.try_emplace(nodeId);
			m_EvaluatedNodes.emplace(nodeId, false);
			m_EvaluatingNodes.emplace(nodeId, false);
			m_PreviousActiveNodes.emplace(nodeId, false);
			m_ActiveTimeScales.emplace(nodeId, 1.0f);
			m_HasActiveTimeScale.emplace(nodeId, false);
		}
		Reset();
	}

	VansAnimGraphInstance::~VansAnimGraphInstance() = default;

	AnimGraphPose VansAnimGraphInstance::Evaluate(const AnimGraphContext& ctx)
	{
		if (!IsCompiled())
			return {};
		for (int nodeId : m_ExecutionPlan)
		{
			m_EvaluatedNodes[nodeId] = false;
			m_EvaluatingNodes[nodeId] = false;
		}
		AnimGraphPose result = EvaluateNode(m_Definition.GetOutputNodeId(), ctx);
		for (int nodeId : m_ExecutionPlan)
			m_PreviousActiveNodes[nodeId] = m_EvaluatedNodes[nodeId];
		return result;
	}

	AnimGraphPose VansAnimGraphInstance::EvaluateNode(int nodeId, const AnimGraphContext& ctx)
	{
		auto evaluated = m_EvaluatedNodes.find(nodeId);
		if (evaluated == m_EvaluatedNodes.end())
			return {};
		if (evaluated->second)
			return m_EvaluationCache.at(nodeId);
		if (m_EvaluatingNodes[nodeId])
			return {};
		m_EvaluatingNodes[nodeId] = true;

		const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
		AnimGraphPose result = node ? node->Evaluate(ctx, *this) : AnimGraphPose{};
		m_EvaluatingNodes[nodeId] = false;
		m_EvaluationCache.at(nodeId) = result;
		m_EvaluatedNodes[nodeId] = true;
		return m_EvaluationCache.at(nodeId);
	}

	AnimGraphPose VansAnimGraphInstance::EvaluateInput(
		int nodeId, int inputPinIndex, const AnimGraphContext& ctx)
	{
		const VansAnimGraphNode* input = m_Definition.GetInputNode(nodeId, inputPinIndex);
		return input ? EvaluateNode(input->GetNodeId(), ctx) : AnimGraphPose{};
	}

	void VansAnimGraphInstance::AdvanceTime(float deltaTime, const AnimGraphContext& ctx)
	{
		if (!IsCompiled())
			return;

		for (int nodeId : m_ExecutionPlan)
			m_HasActiveTimeScale[nodeId] = false;
		auto resolveTimeScale = [&](auto&& self, int nodeId, float scale) -> void
		{
			auto active = m_PreviousActiveNodes.find(nodeId);
			if (active == m_PreviousActiveNodes.end() || !active->second)
				return;
			if (m_HasActiveTimeScale[nodeId])
				return; // Conflicting structural paths are rejected by BuildExecutionPlan.
			m_HasActiveTimeScale[nodeId] = true;
			m_ActiveTimeScales[nodeId] = scale;
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (!node)
				return;
			float inputScale = scale;
			if (node->GetType() == AnimGraphNodeType::SpeedScale)
			{
				const auto* speedNode = static_cast<const AnimGraphSpeedScaleNode*>(node);
				float speed = speedNode->m_FixedSpeed;
				if (speedNode->m_UseParam && ctx.parameters)
				{
					auto parameter = ctx.parameters->find(speedNode->m_ParamName);
					if (parameter != ctx.parameters->end()
					    && parameter->second.type == AnimatorParamType::Float)
						speed = parameter->second.floatVal;
				}
				inputScale *= speed;
			}
			for (const AnimGraphLink& link : m_Definition.GetLinks())
				if (link.toNodeId == nodeId)
					self(self, link.fromNodeId, inputScale);
		};
		resolveTimeScale(resolveTimeScale, m_Definition.GetOutputNodeId(), 1.0f);

		for (int nodeId : m_ExecutionPlan)
		{
			if (!m_PreviousActiveNodes[nodeId])
				continue;
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (!node)
				continue;
			if (node->GetType() == AnimGraphNodeType::Clip)
			{
				const auto* clipNode = static_cast<const AnimGraphClipNode*>(node);
				VansAnimGraphClipRuntimeState& state = GetClipState(nodeId);
				state.previousTime = state.currentTime;
				const float timeScale = m_HasActiveTimeScale[nodeId]
					? m_ActiveTimeScales[nodeId] : 1.0f;
				state.currentTime += deltaTime * timeScale * clipNode->m_Speed;
				continue;
			}
			if (node->GetType() != AnimGraphNodeType::StateMachine || !ctx.clips)
				continue;

			const auto* stateMachine = static_cast<const AnimGraphStateMachineNode*>(node);
			VansAnimGraphStateMachineRuntimeState& runtime =
				GetStateMachineState(nodeId, *stateMachine);
			auto findState = [&](const std::string& name) -> const AnimatorState*
			{
				for (const AnimatorState& state : stateMachine->m_States)
					if (state.name == name)
						return &state;
				return nullptr;
			};
				auto advanceState = [&](const AnimatorState* state)
				{
				if (!state)
					return;
				auto clipIt = ctx.clips->find(state->clipName);
				if (clipIt == ctx.clips->end())
					return;
				const float start = state->startTime;
					const float end = state->endTime < 0.0f ? clipIt->second.duration : state->endTime;
					const float range = end - start;
					float& time = runtime.stateTimes[state->name];
					float& previousTime = runtime.previousStateTimes[state->name];
					previousTime = time;
					if (range <= 0.0f)
					{
						time = start;
						previousTime = start;
						return;
					}
					const float timeScale = m_HasActiveTimeScale[nodeId]
						? m_ActiveTimeScales[nodeId] : 1.0f;
					time += deltaTime * timeScale * state->speed;
					if (!state->loop)
						time = std::clamp(time, start, end);
				};
			advanceState(findState(runtime.currentStateName));
			if (runtime.blendState == ControllerBlendState::Blending)
				advanceState(findState(runtime.previousStateName));
		}
	}

	void VansAnimGraphInstance::Reset()
	{
		m_ClipStates.clear();
		m_StateMachineStates.clear();
		m_IKSolverStates.clear();
		m_IKSolverTypes.clear();
		for (int nodeId : m_ExecutionPlan)
		{
			m_EvaluationCache[nodeId] = {};
			m_EvaluatedNodes[nodeId] = false;
			m_EvaluatingNodes[nodeId] = false;
			m_PreviousActiveNodes[nodeId] = false;
		}
	}

	bool VansAnimGraphInstance::PlayState(const std::string& stateName)
	{
		for (int nodeId : m_ExecutionPlan)
		{
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (!node || node->GetType() != AnimGraphNodeType::StateMachine)
				continue;
			const auto* stateMachine = static_cast<const AnimGraphStateMachineNode*>(node);
			for (const AnimatorState& state : stateMachine->m_States)
			{
				if (state.name != stateName)
					continue;
				VansAnimGraphStateMachineRuntimeState& runtime =
					GetStateMachineState(nodeId, *stateMachine);
				runtime.currentStateName = stateName;
				runtime.previousStateName.clear();
				runtime.blendAlpha = 0.0f;
				runtime.blendDuration = 0.0f;
					runtime.blendState = ControllerBlendState::Idle;
					runtime.stateTimes[stateName] = state.startTime;
					runtime.previousStateTimes[stateName] = state.startTime;
					return true;
			}
		}
		return false;
	}

	std::string VansAnimGraphInstance::GetCurrentStateName() const
	{
		for (int nodeId : m_ExecutionPlan)
		{
			auto it = m_StateMachineStates.find(nodeId);
			if (it != m_StateMachineStates.end() && !it->second.currentStateName.empty())
				return it->second.currentStateName;
		}
		return {};
	}

	float VansAnimGraphInstance::GetPrimaryPlaybackTime() const
	{
		for (int nodeId : m_ExecutionPlan)
		{
			auto stateMachineIt = m_StateMachineStates.find(nodeId);
			if (stateMachineIt != m_StateMachineStates.end())
			{
				auto timeIt = stateMachineIt->second.stateTimes.find(
					stateMachineIt->second.currentStateName);
				if (timeIt != stateMachineIt->second.stateTimes.end())
					return timeIt->second;
			}
			auto clipIt = m_ClipStates.find(nodeId);
			if (clipIt != m_ClipStates.end())
				return clipIt->second.currentTime;
		}
		return 0.0f;
	}

	const std::string& VansAnimGraphInstance::GetPrimaryClipName() const
	{
		for (int nodeId : m_ExecutionPlan)
		{
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (!node)
				continue;
			if (node->GetType() == AnimGraphNodeType::StateMachine)
			{
				auto runtimeIt = m_StateMachineStates.find(nodeId);
				if (runtimeIt == m_StateMachineStates.end())
					continue;
				const auto* stateMachine = static_cast<const AnimGraphStateMachineNode*>(node);
				for (const AnimatorState& state : stateMachine->m_States)
					if (state.name == runtimeIt->second.currentStateName)
						return state.clipName;
			}
			if (node->GetType() == AnimGraphNodeType::Clip
			    && m_ClipStates.find(nodeId) != m_ClipStates.end())
				return static_cast<const AnimGraphClipNode*>(node)->m_ClipName;
		}
		static const std::string empty;
		return empty;
	}

	bool VansAnimGraphInstance::SetPrimaryPlaybackTime(float time, const std::string& stateName)
	{
		if (!std::isfinite(time))
			return false;
		for (int nodeId : m_ExecutionPlan)
		{
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (!node)
				continue;
			if (node->GetType() == AnimGraphNodeType::StateMachine)
			{
				const auto* definition = static_cast<const AnimGraphStateMachineNode*>(node);
				VansAnimGraphStateMachineRuntimeState& runtime = GetStateMachineState(nodeId, *definition);
				if (!stateName.empty() && stateName != runtime.currentStateName)
				{
					bool found = false;
					for (const AnimatorState& state : definition->m_States)
						if (state.name == stateName) { found = true; break; }
					if (!found)
						return false;
					runtime.currentStateName = stateName;
					runtime.previousStateName.clear();
					runtime.blendAlpha = 0.0f;
					runtime.blendDuration = 0.0f;
					runtime.blendState = ControllerBlendState::Idle;
				}
				float& current = runtime.stateTimes[runtime.currentStateName];
				runtime.previousStateTimes[runtime.currentStateName] = current;
				current = time;
				return true;
			}
			if (node->GetType() == AnimGraphNodeType::Clip)
			{
				VansAnimGraphClipRuntimeState& runtime = GetClipState(nodeId);
				runtime.previousTime = runtime.currentTime;
				runtime.currentTime = time;
				return true;
			}
		}
		return false;
	}

	bool VansAnimGraphInstance::SynchronizePrimaryStateMachineFrom(
		const VansAnimGraphInstance& leader,
		const std::unordered_map<std::string, VansAnimationClip>& clips)
	{
		int leaderNodeId = -1;
		const AnimGraphStateMachineNode* leaderDefinition = nullptr;
		const VansAnimGraphStateMachineRuntimeState* leaderRuntime = nullptr;
		for (int nodeId : leader.m_ExecutionPlan)
		{
			const VansAnimGraphNode* node = leader.m_Definition.GetNode(nodeId);
			auto runtime = leader.m_StateMachineStates.find(nodeId);
			if (node && node->GetType() == AnimGraphNodeType::StateMachine
				&& runtime != leader.m_StateMachineStates.end())
			{
				leaderNodeId = nodeId;
				leaderDefinition = static_cast<const AnimGraphStateMachineNode*>(node);
				leaderRuntime = &runtime->second;
				break;
			}
		}
		if (leaderNodeId < 0 || !leaderDefinition || !leaderRuntime
			|| leaderRuntime->currentStateName.empty())
			return false;

		int followerNodeId = -1;
		const AnimGraphStateMachineNode* followerDefinition = nullptr;
		for (int nodeId : m_ExecutionPlan)
		{
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (node && node->GetType() == AnimGraphNodeType::StateMachine)
			{
				followerNodeId = nodeId;
				followerDefinition = static_cast<const AnimGraphStateMachineNode*>(node);
				break;
			}
		}
		if (followerNodeId < 0 || !followerDefinition)
			return false;

		auto findState = [](const AnimGraphStateMachineNode& definition,
		                    const std::string& stateName) -> const AnimatorState*
		{
			for (const AnimatorState& state : definition.m_States)
				if (state.name == stateName)
					return &state;
			return nullptr;
		};
		const AnimatorState* leaderCurrent = findState(*leaderDefinition, leaderRuntime->currentStateName);
		const AnimatorState* followerCurrent = findState(*followerDefinition, leaderRuntime->currentStateName);
		if (!leaderCurrent || !followerCurrent)
			return false;
		const AnimatorState* leaderPrevious = nullptr;
		const AnimatorState* followerPrevious = nullptr;
		if (leaderRuntime->blendState == ControllerBlendState::Blending)
		{
			leaderPrevious = findState(*leaderDefinition, leaderRuntime->previousStateName);
			followerPrevious = findState(*followerDefinition, leaderRuntime->previousStateName);
			if (!leaderPrevious || !followerPrevious)
				return false;
		}

		auto mapTime = [&clips](const AnimatorState& sourceState,
		                       const AnimatorState& targetState,
		                       float sourceTime) -> float
		{
			auto sourceClip = clips.find(sourceState.clipName);
			auto targetClip = clips.find(targetState.clipName);
			if (sourceClip == clips.end() || targetClip == clips.end())
				return targetState.startTime;
			const float sourceEnd = sourceState.endTime < 0.0f
				? sourceClip->second.duration : sourceState.endTime;
			const float targetEnd = targetState.endTime < 0.0f
				? targetClip->second.duration : targetState.endTime;
			const float sourceSpan = sourceEnd - sourceState.startTime;
			const float targetSpan = targetEnd - targetState.startTime;
			if (sourceSpan <= 0.0f || targetSpan <= 0.0f)
				return targetState.startTime;
			const float rawProgress = (sourceTime - sourceState.startTime) / sourceSpan;
			return targetState.startTime + rawProgress * targetSpan;
		};

		VansAnimGraphStateMachineRuntimeState& followerRuntime =
			GetStateMachineState(followerNodeId, *followerDefinition);
		followerRuntime.currentStateName = leaderRuntime->currentStateName;
		followerRuntime.previousStateName = leaderRuntime->previousStateName;
		followerRuntime.blendAlpha = leaderRuntime->blendAlpha;
		followerRuntime.blendDuration = leaderRuntime->blendDuration;
		followerRuntime.blendState = leaderRuntime->blendState;
		followerRuntime.stateTimes.clear();
		followerRuntime.previousStateTimes.clear();

		auto synchronizeTimes = [&](const AnimatorState& sourceState, const AnimatorState& targetState)
		{
			auto current = leaderRuntime->stateTimes.find(sourceState.name);
			auto previous = leaderRuntime->previousStateTimes.find(sourceState.name);
			const float currentTime = current != leaderRuntime->stateTimes.end()
				? current->second : sourceState.startTime;
			const float previousTime = previous != leaderRuntime->previousStateTimes.end()
				? previous->second : currentTime;
			followerRuntime.stateTimes[targetState.name] = mapTime(sourceState, targetState, currentTime);
			followerRuntime.previousStateTimes[targetState.name] = mapTime(sourceState, targetState, previousTime);
		};
		synchronizeTimes(*leaderCurrent, *followerCurrent);
		if (leaderPrevious && followerPrevious)
			synchronizeTimes(*leaderPrevious, *followerPrevious);
		return true;
	}

	VansAnimGraphRuntimeStateSnapshot VansAnimGraphInstance::CaptureRuntimeState() const
	{
		VansAnimGraphRuntimeStateSnapshot snapshot;
		snapshot.clipStates = m_ClipStates;
		snapshot.stateMachineStates = m_StateMachineStates;
		return snapshot;
	}

	bool VansAnimGraphInstance::RestoreRuntimeState(
		const VansAnimGraphRuntimeStateSnapshot& snapshot)
	{
		bool fullyCompatible = true;
		m_ClipStates.clear();
		m_StateMachineStates.clear();
		for (int nodeId : m_ExecutionPlan)
		{
			m_EvaluationCache[nodeId] = {};
			m_EvaluatedNodes[nodeId] = false;
			m_EvaluatingNodes[nodeId] = false;
			m_PreviousActiveNodes[nodeId] = false;
		}

		for (const auto& [nodeId, state] : snapshot.clipStates)
		{
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (!node || node->GetType() != AnimGraphNodeType::Clip
				|| !std::isfinite(state.previousTime) || !std::isfinite(state.currentTime))
			{
				fullyCompatible = false;
				continue;
			}
			m_ClipStates.emplace(nodeId, state);
		}

		for (const auto& [nodeId, source] : snapshot.stateMachineStates)
		{
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (!node || node->GetType() != AnimGraphNodeType::StateMachine)
			{
				fullyCompatible = false;
				continue;
			}
			const auto* definition = static_cast<const AnimGraphStateMachineNode*>(node);
			std::unordered_set<std::string> stateNames;
			for (const AnimatorState& state : definition->m_States)
				stateNames.insert(state.name);
			if (stateNames.find(source.currentStateName) == stateNames.end())
			{
				fullyCompatible = false;
				continue;
			}

			VansAnimGraphStateMachineRuntimeState restored = source;
			auto retainValidTimes = [&](std::unordered_map<std::string, float>& times)
			{
				for (auto it = times.begin(); it != times.end();)
				{
					if (stateNames.find(it->first) == stateNames.end() || !std::isfinite(it->second))
					{
						fullyCompatible = false;
						it = times.erase(it);
					}
					else ++it;
				}
			};
			retainValidTimes(restored.stateTimes);
			retainValidTimes(restored.previousStateTimes);
			for (const AnimatorState& state : definition->m_States)
			{
				restored.stateTimes.try_emplace(state.name, state.startTime);
				restored.previousStateTimes.try_emplace(state.name, state.startTime);
			}
			if (restored.blendState == ControllerBlendState::Blending
				&& stateNames.find(restored.previousStateName) == stateNames.end())
			{
				restored.previousStateName.clear();
				restored.blendState = ControllerBlendState::Idle;
				restored.blendAlpha = 0.0f;
				restored.blendDuration = 0.0f;
				fullyCompatible = false;
			}
			if (!std::isfinite(restored.blendAlpha) || !std::isfinite(restored.blendDuration))
			{
				restored.previousStateName.clear();
				restored.blendState = ControllerBlendState::Idle;
				restored.blendAlpha = 0.0f;
				restored.blendDuration = 0.0f;
				fullyCompatible = false;
			}
			m_StateMachineStates.emplace(nodeId, std::move(restored));
		}
		return fullyCompatible;
	}

	float VansAnimGraphInstance::GetClipTime(int nodeId) const
	{
		auto it = m_ClipStates.find(nodeId);
		return it == m_ClipStates.end() ? 0.0f : it->second.currentTime;
	}

	VansAnimGraphClipRuntimeState& VansAnimGraphInstance::GetClipState(int nodeId)
	{
		return m_ClipStates[nodeId];
	}

	VansAnimGraphStateMachineRuntimeState& VansAnimGraphInstance::GetStateMachineState(
		int nodeId, const AnimGraphStateMachineNode& definition)
	{
		auto [it, inserted] = m_StateMachineStates.try_emplace(nodeId);
		if (inserted)
		{
			it->second.currentStateName = definition.m_DefaultStateName;
			for (const AnimatorState& state : definition.m_States)
			{
				it->second.stateTimes[state.name] = state.startTime;
				it->second.previousStateTimes[state.name] = state.startTime;
			}
		}
		return it->second;
	}

	VansIKSolver* VansAnimGraphInstance::GetIKSolver(int nodeId, IKSolverType type)
	{
		auto typeIt = m_IKSolverTypes.find(nodeId);
		if (typeIt != m_IKSolverTypes.end() && typeIt->second == type)
			return m_IKSolverStates[nodeId].get();

		std::unique_ptr<VansIKSolver> solver;
		switch (type)
		{
		case IKSolverType::TwoBone: solver = std::make_unique<VansTwoBoneIKSolver>(); break;
		case IKSolverType::CCD: solver = std::make_unique<VansCCDSolver>(); break;
		case IKSolverType::FABRIK: solver = std::make_unique<VansFABRIKSolver>(); break;
		case IKSolverType::LookAt: solver = std::make_unique<VansLookAtSolver>(); break;
		}
		m_IKSolverTypes[nodeId] = type;
		m_IKSolverStates[nodeId] = std::move(solver);
		return m_IKSolverStates[nodeId].get();
	}

	VansAnimGraphIKRuntimeState& VansAnimGraphInstance::GetIKRuntimeState(int nodeId)
	{
		return m_IKRuntimeStates[nodeId];
	}

	// ─── 节点工厂 ──────────────────────────────────────────────

	std::unique_ptr<VansAnimGraphNode> VansAnimGraph::CreateNodeByType(AnimGraphNodeType type)
	{
		switch (type)
		{
		case AnimGraphNodeType::Entry:         return std::make_unique<AnimGraphEntryNode>();
		case AnimGraphNodeType::Output:        return std::make_unique<AnimGraphOutputNode>();
		case AnimGraphNodeType::Clip:          return std::make_unique<AnimGraphClipNode>();
		case AnimGraphNodeType::Blend:         return std::make_unique<AnimGraphBlendNode>();
		case AnimGraphNodeType::Blend1D:       return std::make_unique<AnimGraphBlend1DNode>();
		case AnimGraphNodeType::IfCondition:   return std::make_unique<AnimGraphIfConditionNode>();
		case AnimGraphNodeType::Switch:        return std::make_unique<AnimGraphSwitchNode>();
		case AnimGraphNodeType::AdditiveBlend: return std::make_unique<AnimGraphAdditiveBlendNode>();
		case AnimGraphNodeType::SpeedScale:    return std::make_unique<AnimGraphSpeedScaleNode>();
		case AnimGraphNodeType::StateMachine:  return std::make_unique<AnimGraphStateMachineNode>();
		case AnimGraphNodeType::MotionMatching:return std::make_unique<AnimGraphMotionMatchingNode>();
		case AnimGraphNodeType::Slot:          return std::make_unique<AnimGraphSlotNode>();
		case AnimGraphNodeType::TargetPoseInput:return std::make_unique<AnimGraphTargetPoseInputNode>();
		case AnimGraphNodeType::IK:            return std::make_unique<AnimGraphIKNode>();
		case AnimGraphNodeType::TwoBoneIK:     return std::make_unique<AnimGraphTwoBoneIKNode>();
		case AnimGraphNodeType::LookAt:        return std::make_unique<AnimGraphLookAtNode>();
		case AnimGraphNodeType::FootPlacement: return std::make_unique<AnimGraphFootPlacementNode>();
		}
		return nullptr;
	}

	std::unique_ptr<VansAnimGraphNode> VansAnimGraph::CreateNodeByTypeName(const std::string& typeName)
	{
		if (typeName == "Entry")          return CreateNodeByType(AnimGraphNodeType::Entry);
		if (typeName == "Output")         return CreateNodeByType(AnimGraphNodeType::Output);
		if (typeName == "Clip")           return CreateNodeByType(AnimGraphNodeType::Clip);
		if (typeName == "Blend")          return CreateNodeByType(AnimGraphNodeType::Blend);
		if (typeName == "Blend1D")        return CreateNodeByType(AnimGraphNodeType::Blend1D);
		if (typeName == "IfCondition")    return CreateNodeByType(AnimGraphNodeType::IfCondition);
		if (typeName == "Switch")         return CreateNodeByType(AnimGraphNodeType::Switch);
		if (typeName == "AdditiveBlend")  return CreateNodeByType(AnimGraphNodeType::AdditiveBlend);
		if (typeName == "SpeedScale")     return CreateNodeByType(AnimGraphNodeType::SpeedScale);
		if (typeName == "StateMachine")   return CreateNodeByType(AnimGraphNodeType::StateMachine);
		if (typeName == "MotionMatching") return CreateNodeByType(AnimGraphNodeType::MotionMatching);
		if (typeName == "Slot")           return CreateNodeByType(AnimGraphNodeType::Slot);
		if (typeName == "TargetPoseInput")return CreateNodeByType(AnimGraphNodeType::TargetPoseInput);
		if (typeName == "IK")             return CreateNodeByType(AnimGraphNodeType::IK);
		if (typeName == "TwoBoneIK")      return CreateNodeByType(AnimGraphNodeType::TwoBoneIK);
		if (typeName == "LookAt")         return CreateNodeByType(AnimGraphNodeType::LookAt);
		if (typeName == "FootPlacement")  return CreateNodeByType(AnimGraphNodeType::FootPlacement);
		return nullptr;
	}

	// ═════════════════════════════════════════════════════════════
	//  JSON 序列化
	// ═════════════════════════════════════════════════════════════

	// CompareOp 序列化辅助
	static const char* CompareOpToString(CompareOp op)
	{
		switch (op)
		{
		case CompareOp::Greater:      return ">";
		case CompareOp::Less:         return "<";
		case CompareOp::Equal:        return "==";
		case CompareOp::NotEqual:     return "!=";
		case CompareOp::GreaterEqual: return ">=";
		case CompareOp::LessEqual:    return "<=";
		}
		return "==";
	}

	static CompareOp StringToCompareOp(const std::string& s)
	{
		if (s == ">")  return CompareOp::Greater;
		if (s == "<")  return CompareOp::Less;
		if (s == "==") return CompareOp::Equal;
		if (s == "!=") return CompareOp::NotEqual;
		if (s == ">=") return CompareOp::GreaterEqual;
		if (s == "<=") return CompareOp::LessEqual;
		return CompareOp::Equal;
	}

	// 序列化单个节点的特有属性
	static nlohmann::json SerializeNodeProperties(const VansAnimGraphNode* node)
	{
		nlohmann::json props = nlohmann::json::object();
		switch (node->GetType())
		{
		case AnimGraphNodeType::Clip:
		{
			auto* n = static_cast<const AnimGraphClipNode*>(node);
			props["clipName"] = n->m_ClipName;
			props["speed"]    = n->m_Speed;
			props["loop"]     = n->m_Loop;
			break;
		}
		case AnimGraphNodeType::Blend:
		{
			auto* n = static_cast<const AnimGraphBlendNode*>(node);
			props["paramName"]  = n->m_ParamName;
			props["fixedAlpha"] = n->m_FixedAlpha;
			props["useParam"]   = n->m_UseParam;
			break;
		}
		case AnimGraphNodeType::Blend1D:
		{
			auto* n = static_cast<const AnimGraphBlend1DNode*>(node);
			props["paramName"]  = n->m_ParamName;
			props["thresholds"] = n->m_Thresholds;
			break;
		}
		case AnimGraphNodeType::IfCondition:
		{
			auto* n = static_cast<const AnimGraphIfConditionNode*>(node);
			props["paramName"] = n->m_ParamName;
			props["op"]        = CompareOpToString(n->m_CompareOp);
			props["floatVal"]  = n->m_FloatVal;
			props["boolVal"]   = n->m_BoolVal;
			props["intVal"]    = n->m_IntVal;
			break;
		}
		case AnimGraphNodeType::Switch:
		{
			auto* n = static_cast<const AnimGraphSwitchNode*>(node);
			props["paramName"] = n->m_ParamName;
			props["caseCount"] = n->m_CaseCount;
			break;
		}
		case AnimGraphNodeType::AdditiveBlend:
		{
			auto* n = static_cast<const AnimGraphAdditiveBlendNode*>(node);
			props["paramName"]   = n->m_ParamName;
			props["fixedWeight"] = n->m_FixedWeight;
			props["useParam"]    = n->m_UseParam;
			break;
		}
		case AnimGraphNodeType::SpeedScale:
		{
			auto* n = static_cast<const AnimGraphSpeedScaleNode*>(node);
			props["paramName"]  = n->m_ParamName;
			props["fixedSpeed"] = n->m_FixedSpeed;
			props["useParam"]   = n->m_UseParam;
			break;
		}
		case AnimGraphNodeType::StateMachine:
		{
			auto* n = static_cast<const AnimGraphStateMachineNode*>(node);
			props["defaultState"] = n->m_DefaultStateName;

			nlohmann::json statesJson = nlohmann::json::array();
			for (const auto& s : n->m_States)
			{
				statesJson.push_back({
					{ "name", s.name },
					{ "clip", s.clipName },
					{ "speed", s.speed },
					{ "loop", s.loop },
					{ "rootMotion", s.rootMotion }
				});
			}
			props["states"] = statesJson;

			nlohmann::json transJson = nlohmann::json::array();
			for (const auto& t : n->m_Transitions)
			{
				nlohmann::json tj;
				tj["from"]          = t.fromState;
				tj["to"]            = t.toState;
				tj["blendDuration"] = t.blendDuration;
				tj["hasExitTime"]   = t.hasExitTime;
				tj["exitTime"]      = t.exitTime;

				nlohmann::json condsJson = nlohmann::json::array();
				for (const auto& c : t.conditions)
				{
					condsJson.push_back({
						{ "param", c.paramName },
						{ "op",    CompareOpToString(c.op) },
						{ "floatVal", c.floatVal },
						{ "boolVal",  c.boolVal },
						{ "intVal",   c.intVal }
					});
				}
				tj["conditions"] = condsJson;
				transJson.push_back(tj);
			}
			props["transitions"] = transJson;
			break;
		}
		case AnimGraphNodeType::MotionMatching:
		{
			auto* n = static_cast<const AnimGraphMotionMatchingNode*>(node);
			props["enableFallbackInput"] = n->m_EnableFallbackInput;
			break;
		}
		case AnimGraphNodeType::Slot:
		{
			auto* n = static_cast<const AnimGraphSlotNode*>(node);
			props["slotId"] = n->m_SlotId;
			props["enableFallbackInput"] = n->m_EnableFallbackInput;
			break;
		}
		case AnimGraphNodeType::IK:
		{
			auto* n = static_cast<const AnimGraphIKNode*>(node);
			props["solverType"] = IKSolverTypeToString(n->m_Chain.solverType);
			props["chainName"]      = n->m_Chain.chainName;
			props["maxIterations"]  = n->m_Chain.maxIterations;
			props["positionTol"]    = n->m_Chain.positionTolerance;
			props["rotationTol"]    = n->m_Chain.rotationTolerance;
			props["enableRotationTarget"] = n->m_Chain.enableRotationTarget;
			props["rotationWeight"] = n->m_Chain.rotationWeight;
			props["poleVector"]     = { n->m_Chain.poleVector.x, n->m_Chain.poleVector.y, n->m_Chain.poleVector.z };
			props["poleWeight"]     = n->m_Chain.poleWeight;
			props["poleSpace"]      = IKSpaceToString(n->m_Chain.poleSpace);
			props["poleReferenceBone"] = n->m_Chain.poleReferenceBoneName;
			props["maintainEffectorGlobalRotation"] = n->m_Chain.maintainEffectorGlobalRotation;
			props["allowStretch"] = n->m_Chain.allowStretch;
			props["startStretchRatio"] = n->m_Chain.startStretchRatio;
			props["maxStretchScale"] = n->m_Chain.maxStretchScale;
			nlohmann::json bonesJson = nlohmann::json::array();
			for (const auto& b : n->m_Chain.bones)
			{
				nlohmann::json bj;
				bj["name"]       = b.boneName;
				bj["isEffector"] = b.isEffector;
				bj["weight"]     = b.stiffnessWeight;
				bj["constraint"] = static_cast<int>(b.constraint.type);
				bj["coneDeg"]    = b.constraint.coneAngleDeg;
				bj["minY"]       = b.constraint.minAngleY;
				bj["maxY"]       = b.constraint.maxAngleY;
				bj["stiffness"]  = b.constraint.stiffness;
				bj["axisX"] = { b.constraint.localXAxis.x, b.constraint.localXAxis.y, b.constraint.localXAxis.z };
				bj["axisY"] = { b.constraint.localYAxis.x, b.constraint.localYAxis.y, b.constraint.localYAxis.z };
				bj["axisZ"] = { b.constraint.localZAxis.x, b.constraint.localZAxis.y, b.constraint.localZAxis.z };
				bj["minX"] = b.constraint.minAngleX;
				bj["maxX"] = b.constraint.maxAngleX;
				bj["minZ"] = b.constraint.minAngleZ;
				bj["maxZ"] = b.constraint.maxAngleZ;
				bj["restRotation"] = { b.constraint.restRotation.x, b.constraint.restRotation.y,
				                         b.constraint.restRotation.z, b.constraint.restRotation.w };
				bonesJson.push_back(bj);
			}
			props["bones"] = bonesJson;
			props["targetPosParam"] = n->m_TargetPosParamName;
			props["targetRotParam"] = n->m_TargetRotParamName;
			props["weightParam"]    = n->m_WeightParamName;
			props["useFixed"]       = n->m_UseFixedTarget;
			props["fixedPos"]       = { n->m_FixedTargetPos.x, n->m_FixedTargetPos.y, n->m_FixedTargetPos.z };
			props["fixedRot"]       = { n->m_FixedTargetRot.x, n->m_FixedTargetRot.y, n->m_FixedTargetRot.z, n->m_FixedTargetRot.w };
			props["fixedWeight"]    = n->m_FixedWeight;
			props["targetPositionSpace"] = IKSpaceToString(n->m_TargetPositionSpace);
			props["targetRotationSpace"] = IKSpaceToString(n->m_TargetRotationSpace);
			props["targetReferenceBone"] = n->m_TargetReferenceBoneName;
			break;
		}
		case AnimGraphNodeType::TwoBoneIK:
		{
			auto* n = static_cast<const AnimGraphTwoBoneIKNode*>(node);
			props["root"]           = n->m_RootBoneName;
			props["mid"]            = n->m_MidBoneName;
			props["tip"]            = n->m_TipBoneName;
			props["profile"]        = n->m_UseLegProfile ? "Leg" : "Arm";
			props["isRightSide"]    = n->m_IsRightSide;
			props["hingeMin"]       = n->m_HingeMinAngle;
			props["hingeMax"]       = n->m_HingeMaxAngle;
			props["coneAngle"]      = n->m_ConeAngle;
			props["usePole"]        = n->m_UsePoleVector;
			props["poleVector"]     = { n->m_PoleVector.x, n->m_PoleVector.y, n->m_PoleVector.z };
			props["poleWeight"]     = n->m_PoleWeight;
			props["targetPosParam"] = n->m_TargetPosParamName;
			props["targetRotParam"] = n->m_TargetRotParamName;
			props["weightParam"]    = n->m_WeightParamName;
			props["useFixed"]       = n->m_UseFixedTarget;
			props["fixedPos"]       = { n->m_FixedTargetPos.x, n->m_FixedTargetPos.y, n->m_FixedTargetPos.z };
			props["fixedRot"]       = { n->m_FixedTargetRot.x, n->m_FixedTargetRot.y, n->m_FixedTargetRot.z, n->m_FixedTargetRot.w };
			props["fixedWeight"]    = n->m_FixedWeight;
			props["enableRotationTarget"] = n->m_EnableRotationTarget;
			props["rotationWeight"] = n->m_RotationWeight;
			props["targetPositionSpace"] = IKSpaceToString(n->m_TargetPositionSpace);
			props["targetRotationSpace"] = IKSpaceToString(n->m_TargetRotationSpace);
			props["targetReferenceBone"] = n->m_TargetReferenceBoneName;
			props["poleSpace"] = IKSpaceToString(n->m_PoleSpace);
			props["poleReferenceBone"] = n->m_PoleReferenceBoneName;
			props["maintainEffectorGlobalRotation"] = n->m_MaintainEffectorGlobalRotation;
			props["allowStretch"] = n->m_AllowStretch;
			props["startStretchRatio"] = n->m_StartStretchRatio;
			props["maxStretchScale"] = n->m_MaxStretchScale;
			break;
		}
		case AnimGraphNodeType::LookAt:
		{
			auto* n = static_cast<const AnimGraphLookAtNode*>(node);
			props["bones"]          = n->m_BoneNames;
			props["weights"]        = n->m_BoneWeights;
			props["maxAngleDeg"]    = n->m_MaxAnglePerBoneDeg;
			props["forward"]        = { n->m_ForwardAxis.x, n->m_ForwardAxis.y, n->m_ForwardAxis.z };
			props["worldForward"]   = { n->m_WorldForward.x, n->m_WorldForward.y, n->m_WorldForward.z };
			props["modelUp"]        = { n->m_ModelUp.x, n->m_ModelUp.y, n->m_ModelUp.z };
			props["upWeight"]       = n->m_UpWeight;
			props["targetPosParam"] = n->m_TargetPosParamName;
			props["weightParam"]    = n->m_WeightParamName;
			props["useFixed"]       = n->m_UseFixedTarget;
			props["fixedPos"]       = { n->m_FixedTargetPos.x, n->m_FixedTargetPos.y, n->m_FixedTargetPos.z };
			props["fixedWeight"]    = n->m_FixedWeight;
			props["targetPositionSpace"] = IKSpaceToString(n->m_TargetPositionSpace);
			props["targetReferenceBone"] = n->m_TargetReferenceBoneName;
			break;
		}
		case AnimGraphNodeType::FootPlacement:
		{
			auto* n = static_cast<const AnimGraphFootPlacementNode*>(node);
			const FootPlacementSettings& s = n->m_Settings;
			props = {
				{ "enabled", s.enabled },
				{ "probeOriginHeight", s.probeOriginHeight }, { "probeLength", s.probeLength },
				{ "footHalfLength", s.footHalfLength }, { "footHalfWidth", s.footHalfWidth },
				{ "ankleHeight", s.ankleHeight }, { "fullContactHeight", s.fullContactHeight },
				{ "contactFadeHeight", s.contactFadeHeight }, { "maxStepUp", s.maxStepUp },
				{ "maxStepDown", s.maxStepDown }, { "maxSlopeDeg", s.maxSlopeDeg },
				{ "pelvisMaxDrop", s.pelvisMaxDrop }, { "pelvisSmoothTime", s.pelvisSmoothTime },
				{ "offsetSmoothTime", s.offsetSmoothTime }, { "normalSmoothTime", s.normalSmoothTime },
				{ "weightSmoothTime", s.weightSmoothTime }, { "globalWeightSmoothTime", s.globalWeightSmoothTime },
				{ "ikWeight", s.ikWeight }, { "rotationWeight", s.rotationWeight },
				{ "maxLegExtensionRatio", s.maxLegExtensionRatio }, { "poleSmoothTime", s.poleSmoothTime },
				{ "kneePoleModelDir", { s.kneePoleModelDir.x, s.kneePoleModelDir.y, s.kneePoleModelDir.z } },
				{ "kneePoleModelWeight", s.kneePoleModelWeight },
				{ "debugVisualization", s.debugVisualization }, { "collisionMask", s.collisionMask },
				{ "airborneParameter", s.airborneParameter },
				{ "bones", {
					{ "pelvis", s.bones.pelvis }, { "leftHip", s.bones.leftHip }, { "leftKnee", s.bones.leftKnee },
					{ "leftFoot", s.bones.leftFoot }, { "rightHip", s.bones.rightHip }, { "rightKnee", s.bones.rightKnee },
					{ "rightFoot", s.bones.rightFoot }
				} }
			};
			break;
		}
		default:
			break;
		}
		return props;
	}

	// 反序列化节点属性
	static void DeserializeNodeProperties(VansAnimGraphNode* node, const nlohmann::json& props)
	{
		switch (node->GetType())
		{
		case AnimGraphNodeType::Clip:
		{
			auto* n = static_cast<AnimGraphClipNode*>(node);
			if (props.contains("clipName")) n->m_ClipName = props["clipName"].get<std::string>();
			if (props.contains("speed"))    n->m_Speed    = props["speed"].get<float>();
			if (props.contains("loop"))     n->m_Loop     = props["loop"].get<bool>();
			break;
		}
		case AnimGraphNodeType::Blend:
		{
			auto* n = static_cast<AnimGraphBlendNode*>(node);
			if (props.contains("paramName"))  n->m_ParamName  = props["paramName"].get<std::string>();
			if (props.contains("fixedAlpha")) n->m_FixedAlpha = props["fixedAlpha"].get<float>();
			if (props.contains("useParam"))   n->m_UseParam   = props["useParam"].get<bool>();
			break;
		}
		case AnimGraphNodeType::Blend1D:
		{
			auto* n = static_cast<AnimGraphBlend1DNode*>(node);
			if (props.contains("paramName"))  n->m_ParamName  = props["paramName"].get<std::string>();
			if (props.contains("thresholds")) n->m_Thresholds = props["thresholds"].get<std::vector<float>>();
			break;
		}
		case AnimGraphNodeType::IfCondition:
		{
			auto* n = static_cast<AnimGraphIfConditionNode*>(node);
			if (props.contains("paramName")) n->m_ParamName = props["paramName"].get<std::string>();
			if (props.contains("op"))        n->m_CompareOp = StringToCompareOp(props["op"].get<std::string>());
			if (props.contains("floatVal"))  n->m_FloatVal  = props["floatVal"].get<float>();
			if (props.contains("boolVal"))   n->m_BoolVal   = props["boolVal"].get<bool>();
			if (props.contains("intVal"))    n->m_IntVal    = props["intVal"].get<int>();
			break;
		}
		case AnimGraphNodeType::Switch:
		{
			auto* n = static_cast<AnimGraphSwitchNode*>(node);
			if (props.contains("paramName")) n->m_ParamName = props["paramName"].get<std::string>();
			if (props.contains("caseCount")) n->m_CaseCount = props["caseCount"].get<int>();
			break;
		}
		case AnimGraphNodeType::AdditiveBlend:
		{
			auto* n = static_cast<AnimGraphAdditiveBlendNode*>(node);
			if (props.contains("paramName"))   n->m_ParamName   = props["paramName"].get<std::string>();
			if (props.contains("fixedWeight")) n->m_FixedWeight = props["fixedWeight"].get<float>();
			if (props.contains("useParam"))    n->m_UseParam    = props["useParam"].get<bool>();
			break;
		}
		case AnimGraphNodeType::SpeedScale:
		{
			auto* n = static_cast<AnimGraphSpeedScaleNode*>(node);
			if (props.contains("paramName"))  n->m_ParamName  = props["paramName"].get<std::string>();
			if (props.contains("fixedSpeed")) n->m_FixedSpeed = props["fixedSpeed"].get<float>();
			if (props.contains("useParam"))   n->m_UseParam   = props["useParam"].get<bool>();
			break;
		}
		case AnimGraphNodeType::StateMachine:
		{
			auto* n = static_cast<AnimGraphStateMachineNode*>(node);
			if (props.contains("defaultState"))
				n->m_DefaultStateName = props["defaultState"].get<std::string>();

			if (props.contains("states"))
			{
				for (const auto& sj : props["states"])
				{
					AnimatorState s;
					if (sj.contains("name"))       s.name       = sj["name"].get<std::string>();
					if (sj.contains("clip"))        s.clipName   = sj["clip"].get<std::string>();
					if (sj.contains("speed"))       s.speed      = sj["speed"].get<float>();
					if (sj.contains("loop"))        s.loop       = sj["loop"].get<bool>();
					if (sj.contains("rootMotion"))  s.rootMotion = sj["rootMotion"].get<bool>();
					n->m_States.push_back(s);
				}
			}

			if (props.contains("transitions"))
			{
				for (const auto& tj : props["transitions"])
				{
					AnimatorTransition t;
					if (tj.contains("from"))          t.fromState     = tj["from"].get<std::string>();
					if (tj.contains("to"))            t.toState       = tj["to"].get<std::string>();
					if (tj.contains("blendDuration")) t.blendDuration = tj["blendDuration"].get<float>();
					if (tj.contains("hasExitTime"))   t.hasExitTime   = tj["hasExitTime"].get<bool>();
					if (tj.contains("exitTime"))      t.exitTime      = tj["exitTime"].get<float>();

					if (tj.contains("conditions"))
					{
						for (const auto& cj : tj["conditions"])
						{
							TransitionCondition cond;
							if (cj.contains("param"))    cond.paramName = cj["param"].get<std::string>();
							if (cj.contains("op"))       cond.op = StringToCompareOp(cj["op"].get<std::string>());
							if (cj.contains("floatVal")) cond.floatVal = cj["floatVal"].get<float>();
							if (cj.contains("boolVal"))  cond.boolVal  = cj["boolVal"].get<bool>();
							if (cj.contains("intVal"))   cond.intVal   = cj["intVal"].get<int>();
							t.conditions.push_back(cond);
						}
					}
					n->m_Transitions.push_back(t);
				}
			}
			break;
		}
		case AnimGraphNodeType::MotionMatching:
		{
			auto* n = static_cast<AnimGraphMotionMatchingNode*>(node);
			if (props.contains("enableFallbackInput"))
				n->m_EnableFallbackInput = props["enableFallbackInput"].get<bool>();
			break;
		}
		case AnimGraphNodeType::Slot:
		{
			auto* n = static_cast<AnimGraphSlotNode*>(node);
			if (props.contains("slotId")) n->m_SlotId = props["slotId"].get<std::string>();
			if (props.contains("enableFallbackInput"))
				n->m_EnableFallbackInput = props["enableFallbackInput"].get<bool>();
			break;
		}
		case AnimGraphNodeType::IK:
		{
			auto* n = static_cast<AnimGraphIKNode*>(node);
			if (props.contains("solverType"))
			{
				n->m_Chain.solverType = StringToIKSolverType(props["solverType"].get<std::string>());
			}
			if (props.contains("chainName"))     n->m_Chain.chainName        = props["chainName"].get<std::string>();
			if (props.contains("maxIterations")) n->m_Chain.maxIterations    = props["maxIterations"].get<int>();
			if (props.contains("positionTol"))   n->m_Chain.positionTolerance= props["positionTol"].get<float>();
			if (props.contains("rotationTol"))   n->m_Chain.rotationTolerance= props["rotationTol"].get<float>();
			if (props.contains("enableRotationTarget")) n->m_Chain.enableRotationTarget = props["enableRotationTarget"].get<bool>();
			if (props.contains("rotationWeight")) n->m_Chain.rotationWeight = props["rotationWeight"].get<float>();
			if (props.contains("poleVector") && props["poleVector"].is_array() && props["poleVector"].size() >= 3)
			{
				n->m_Chain.poleVector.x = props["poleVector"][0].get<float>();
				n->m_Chain.poleVector.y = props["poleVector"][1].get<float>();
				n->m_Chain.poleVector.z = props["poleVector"][2].get<float>();
			}
			if (props.contains("poleWeight"))    n->m_Chain.poleWeight       = props["poleWeight"].get<float>();
			if (props.contains("poleSpace")) n->m_Chain.poleSpace = StringToIKSpace(props["poleSpace"].get<std::string>());
			if (props.contains("poleReferenceBone")) n->m_Chain.poleReferenceBoneName = props["poleReferenceBone"].get<std::string>();
			if (props.contains("maintainEffectorGlobalRotation")) n->m_Chain.maintainEffectorGlobalRotation = props["maintainEffectorGlobalRotation"].get<bool>();
			if (props.contains("allowStretch")) n->m_Chain.allowStretch = props["allowStretch"].get<bool>();
			if (props.contains("startStretchRatio")) n->m_Chain.startStretchRatio = props["startStretchRatio"].get<float>();
			if (props.contains("maxStretchScale")) n->m_Chain.maxStretchScale = props["maxStretchScale"].get<float>();
			if (props.contains("bones"))
			{
				for (const auto& bj : props["bones"])
				{
					IKBoneLink b;
					if (bj.contains("name"))       b.boneName        = bj["name"].get<std::string>();
					if (bj.contains("isEffector")) b.isEffector      = bj["isEffector"].get<bool>();
					if (bj.contains("weight"))     b.stiffnessWeight = bj["weight"].get<float>();
					if (bj.contains("constraint")) b.constraint.type = static_cast<JointConstraintType>(bj["constraint"].get<int>());
					if (bj.contains("coneDeg"))    b.constraint.coneAngleDeg = bj["coneDeg"].get<float>();
					if (bj.contains("minY"))       b.constraint.minAngleY    = bj["minY"].get<float>();
					if (bj.contains("maxY"))       b.constraint.maxAngleY    = bj["maxY"].get<float>();
					if (bj.contains("stiffness"))  b.constraint.stiffness    = bj["stiffness"].get<float>();
					auto readAxis = [&](const char* key, glm::vec3& axis)
					{
						if (bj.contains(key) && bj[key].is_array() && bj[key].size() >= 3)
							axis = glm::vec3(bj[key][0].get<float>(), bj[key][1].get<float>(), bj[key][2].get<float>());
					};
					readAxis("axisX", b.constraint.localXAxis);
					readAxis("axisY", b.constraint.localYAxis);
					readAxis("axisZ", b.constraint.localZAxis);
					if (bj.contains("minX")) b.constraint.minAngleX = bj["minX"].get<float>();
					if (bj.contains("maxX")) b.constraint.maxAngleX = bj["maxX"].get<float>();
					if (bj.contains("minZ")) b.constraint.minAngleZ = bj["minZ"].get<float>();
					if (bj.contains("maxZ")) b.constraint.maxAngleZ = bj["maxZ"].get<float>();
					if (bj.contains("restRotation") && bj["restRotation"].is_array() && bj["restRotation"].size() >= 4)
					{
						b.constraint.restRotation = glm::normalize(glm::quat(
							bj["restRotation"][3].get<float>(), bj["restRotation"][0].get<float>(),
							bj["restRotation"][1].get<float>(), bj["restRotation"][2].get<float>()));
					}
					n->m_Chain.bones.push_back(b);
				}
			}
			if (props.contains("targetPosParam")) n->m_TargetPosParamName = props["targetPosParam"].get<std::string>();
			if (props.contains("targetRotParam")) n->m_TargetRotParamName = props["targetRotParam"].get<std::string>();
			if (props.contains("weightParam"))    n->m_WeightParamName    = props["weightParam"].get<std::string>();
			if (props.contains("useFixed"))       n->m_UseFixedTarget     = props["useFixed"].get<bool>();
			if (props.contains("fixedPos") && props["fixedPos"].is_array() && props["fixedPos"].size() >= 3)
			{
				n->m_FixedTargetPos.x = props["fixedPos"][0].get<float>();
				n->m_FixedTargetPos.y = props["fixedPos"][1].get<float>();
				n->m_FixedTargetPos.z = props["fixedPos"][2].get<float>();
			}
			if (props.contains("fixedRot") && props["fixedRot"].is_array() && props["fixedRot"].size() >= 4)
			{
				n->m_FixedTargetRot.x = props["fixedRot"][0].get<float>();
				n->m_FixedTargetRot.y = props["fixedRot"][1].get<float>();
				n->m_FixedTargetRot.z = props["fixedRot"][2].get<float>();
				n->m_FixedTargetRot.w = props["fixedRot"][3].get<float>();
			}
			if (props.contains("fixedWeight"))    n->m_FixedWeight        = props["fixedWeight"].get<float>();
			if (props.contains("targetPositionSpace")) n->m_TargetPositionSpace = StringToIKSpace(props["targetPositionSpace"].get<std::string>());
			if (props.contains("targetRotationSpace")) n->m_TargetRotationSpace = StringToIKSpace(props["targetRotationSpace"].get<std::string>());
			if (props.contains("targetReferenceBone")) n->m_TargetReferenceBoneName = props["targetReferenceBone"].get<std::string>();
			break;
		}
		case AnimGraphNodeType::TwoBoneIK:
		{
			auto* n = static_cast<AnimGraphTwoBoneIKNode*>(node);
			if (props.contains("root"))     n->m_RootBoneName  = props["root"].get<std::string>();
			if (props.contains("mid"))      n->m_MidBoneName   = props["mid"].get<std::string>();
			if (props.contains("tip"))      n->m_TipBoneName   = props["tip"].get<std::string>();
			if (props.contains("profile"))  n->m_UseLegProfile = props["profile"].get<std::string>() == "Leg";
			if (props.contains("isRightSide")) n->m_IsRightSide = props["isRightSide"].get<bool>();
			if (props.contains("hingeMin")) n->m_HingeMinAngle = props["hingeMin"].get<float>();
			if (props.contains("hingeMax")) n->m_HingeMaxAngle = props["hingeMax"].get<float>();
			if (props.contains("coneAngle"))n->m_ConeAngle     = props["coneAngle"].get<float>();
			if (props.contains("usePole"))  n->m_UsePoleVector = props["usePole"].get<bool>();
			if (props.contains("poleVector") && props["poleVector"].is_array() && props["poleVector"].size() >= 3)
			{
				n->m_PoleVector.x = props["poleVector"][0].get<float>();
				n->m_PoleVector.y = props["poleVector"][1].get<float>();
				n->m_PoleVector.z = props["poleVector"][2].get<float>();
			}
			if (props.contains("poleWeight"))   n->m_PoleWeight  = props["poleWeight"].get<float>();
			if (props.contains("targetPosParam")) n->m_TargetPosParamName = props["targetPosParam"].get<std::string>();
			if (props.contains("targetRotParam")) n->m_TargetRotParamName = props["targetRotParam"].get<std::string>();
			if (props.contains("weightParam"))    n->m_WeightParamName    = props["weightParam"].get<std::string>();
			if (props.contains("useFixed"))       n->m_UseFixedTarget     = props["useFixed"].get<bool>();
			if (props.contains("fixedPos") && props["fixedPos"].is_array() && props["fixedPos"].size() >= 3)
			{
				n->m_FixedTargetPos.x = props["fixedPos"][0].get<float>();
				n->m_FixedTargetPos.y = props["fixedPos"][1].get<float>();
				n->m_FixedTargetPos.z = props["fixedPos"][2].get<float>();
			}
			if (props.contains("fixedRot") && props["fixedRot"].is_array() && props["fixedRot"].size() >= 4)
			{
				n->m_FixedTargetRot.x = props["fixedRot"][0].get<float>();
				n->m_FixedTargetRot.y = props["fixedRot"][1].get<float>();
				n->m_FixedTargetRot.z = props["fixedRot"][2].get<float>();
				n->m_FixedTargetRot.w = props["fixedRot"][3].get<float>();
			}
			if (props.contains("fixedWeight"))    n->m_FixedWeight        = props["fixedWeight"].get<float>();
			if (props.contains("enableRotationTarget")) n->m_EnableRotationTarget = props["enableRotationTarget"].get<bool>();
			if (props.contains("rotationWeight")) n->m_RotationWeight = props["rotationWeight"].get<float>();
			if (props.contains("targetPositionSpace")) n->m_TargetPositionSpace = StringToIKSpace(props["targetPositionSpace"].get<std::string>());
			if (props.contains("targetRotationSpace")) n->m_TargetRotationSpace = StringToIKSpace(props["targetRotationSpace"].get<std::string>());
			if (props.contains("targetReferenceBone")) n->m_TargetReferenceBoneName = props["targetReferenceBone"].get<std::string>();
			if (props.contains("poleSpace")) n->m_PoleSpace = StringToIKSpace(props["poleSpace"].get<std::string>());
			if (props.contains("poleReferenceBone")) n->m_PoleReferenceBoneName = props["poleReferenceBone"].get<std::string>();
			if (props.contains("maintainEffectorGlobalRotation")) n->m_MaintainEffectorGlobalRotation = props["maintainEffectorGlobalRotation"].get<bool>();
			if (props.contains("allowStretch")) n->m_AllowStretch = props["allowStretch"].get<bool>();
			if (props.contains("startStretchRatio")) n->m_StartStretchRatio = props["startStretchRatio"].get<float>();
			if (props.contains("maxStretchScale")) n->m_MaxStretchScale = props["maxStretchScale"].get<float>();
			break;
		}
		case AnimGraphNodeType::LookAt:
		{
			auto* n = static_cast<AnimGraphLookAtNode*>(node);
			if (props.contains("bones"))   n->m_BoneNames   = props["bones"].get<std::vector<std::string>>();
			if (props.contains("weights")) n->m_BoneWeights = props["weights"].get<std::vector<float>>();
			if (props.contains("maxAngleDeg")) n->m_MaxAnglePerBoneDeg = props["maxAngleDeg"].get<float>();
			if (props.contains("forward") && props["forward"].is_array() && props["forward"].size() >= 3)
			{
				n->m_ForwardAxis.x = props["forward"][0].get<float>();
				n->m_ForwardAxis.y = props["forward"][1].get<float>();
				n->m_ForwardAxis.z = props["forward"][2].get<float>();
			}
			if (props.contains("worldForward") && props["worldForward"].is_array() && props["worldForward"].size() >= 3)
			{
				n->m_WorldForward.x = props["worldForward"][0].get<float>();
				n->m_WorldForward.y = props["worldForward"][1].get<float>();
				n->m_WorldForward.z = props["worldForward"][2].get<float>();
			}
			if (props.contains("modelUp") && props["modelUp"].is_array() && props["modelUp"].size() >= 3)
			{
				n->m_ModelUp.x = props["modelUp"][0].get<float>();
				n->m_ModelUp.y = props["modelUp"][1].get<float>();
				n->m_ModelUp.z = props["modelUp"][2].get<float>();
			}
			if (props.contains("upWeight")) n->m_UpWeight = props["upWeight"].get<float>();
			if (props.contains("targetPosParam")) n->m_TargetPosParamName = props["targetPosParam"].get<std::string>();
			if (props.contains("weightParam"))    n->m_WeightParamName    = props["weightParam"].get<std::string>();
			if (props.contains("useFixed"))       n->m_UseFixedTarget     = props["useFixed"].get<bool>();
			if (props.contains("fixedPos") && props["fixedPos"].is_array() && props["fixedPos"].size() >= 3)
			{
				n->m_FixedTargetPos.x = props["fixedPos"][0].get<float>();
				n->m_FixedTargetPos.y = props["fixedPos"][1].get<float>();
				n->m_FixedTargetPos.z = props["fixedPos"][2].get<float>();
			}
			if (props.contains("fixedWeight"))    n->m_FixedWeight        = props["fixedWeight"].get<float>();
			if (props.contains("targetPositionSpace")) n->m_TargetPositionSpace = StringToIKSpace(props["targetPositionSpace"].get<std::string>());
			if (props.contains("targetReferenceBone")) n->m_TargetReferenceBoneName = props["targetReferenceBone"].get<std::string>();
			break;
		}
		case AnimGraphNodeType::FootPlacement:
		{
			auto* n = static_cast<AnimGraphFootPlacementNode*>(node);
			FootPlacementSettings& s = n->m_Settings;
			auto readFloat = [&](const char* key, float& value) { if (props.contains(key)) value = props[key].get<float>(); };
			auto readBool = [&](const char* key, bool& value) { if (props.contains(key)) value = props[key].get<bool>(); };
			auto readString = [&](const char* key, std::string& value) { if (props.contains(key)) value = props[key].get<std::string>(); };
			readBool("enabled", s.enabled);
			readFloat("probeOriginHeight", s.probeOriginHeight); readFloat("probeLength", s.probeLength);
			readFloat("footHalfLength", s.footHalfLength); readFloat("footHalfWidth", s.footHalfWidth);
			readFloat("ankleHeight", s.ankleHeight); readFloat("fullContactHeight", s.fullContactHeight);
			readFloat("contactFadeHeight", s.contactFadeHeight); readFloat("maxStepUp", s.maxStepUp);
			readFloat("maxStepDown", s.maxStepDown); readFloat("maxSlopeDeg", s.maxSlopeDeg);
			readFloat("pelvisMaxDrop", s.pelvisMaxDrop); readFloat("pelvisSmoothTime", s.pelvisSmoothTime);
			readFloat("offsetSmoothTime", s.offsetSmoothTime); readFloat("normalSmoothTime", s.normalSmoothTime);
			readFloat("weightSmoothTime", s.weightSmoothTime); readFloat("globalWeightSmoothTime", s.globalWeightSmoothTime);
			readFloat("ikWeight", s.ikWeight); readFloat("rotationWeight", s.rotationWeight);
			readFloat("maxLegExtensionRatio", s.maxLegExtensionRatio); readFloat("poleSmoothTime", s.poleSmoothTime);
			readFloat("kneePoleModelWeight", s.kneePoleModelWeight); readBool("debugVisualization", s.debugVisualization);
			if (props.contains("collisionMask")) s.collisionMask = props["collisionMask"].get<uint32_t>();
			readString("airborneParameter", s.airborneParameter);
			if (props.contains("kneePoleModelDir") && props["kneePoleModelDir"].is_array() && props["kneePoleModelDir"].size() >= 3)
				s.kneePoleModelDir = glm::vec3(props["kneePoleModelDir"][0].get<float>(), props["kneePoleModelDir"][1].get<float>(), props["kneePoleModelDir"][2].get<float>());
			if (props.contains("bones") && props["bones"].is_object())
			{
				const auto& bones = props["bones"];
				s.bones.pelvis = bones.value("pelvis", s.bones.pelvis); s.bones.leftHip = bones.value("leftHip", s.bones.leftHip);
				s.bones.leftKnee = bones.value("leftKnee", s.bones.leftKnee); s.bones.leftFoot = bones.value("leftFoot", s.bones.leftFoot);
				s.bones.rightHip = bones.value("rightHip", s.bones.rightHip); s.bones.rightKnee = bones.value("rightKnee", s.bones.rightKnee);
				s.bones.rightFoot = bones.value("rightFoot", s.bones.rightFoot);
			}
			break;
		}
		default:
			break;
		}
	}

	void VansAnimGraph::SerializeToJsonObject(AnimGraphJson& outJson) const
	{
		outJson = nlohmann::json::object();

		// 节点数组
		nlohmann::json nodesJson = nlohmann::json::array();
		std::vector<const VansAnimGraphNode*> sortedNodes;
		sortedNodes.reserve(m_Nodes.size());
		for (const auto& [id, node] : m_Nodes)
			sortedNodes.push_back(node.get());
		std::sort(sortedNodes.begin(), sortedNodes.end(),
			[](const VansAnimGraphNode* lhs, const VansAnimGraphNode* rhs)
			{
				return lhs->GetNodeId() < rhs->GetNodeId();
			});
		for (const VansAnimGraphNode* node : sortedNodes)
		{
			nlohmann::json nj;
			nj["id"]       = node->GetNodeId();
			nj["type"]     = VansAnimGraphNode::TypeToString(node->GetType());
			nj["name"]     = node->GetName();
			nj["posX"]     = node->m_EditorPosX;
			nj["posY"]     = node->m_EditorPosY;
			nj["properties"] = SerializeNodeProperties(node);
			nodesJson.push_back(nj);
		}
		outJson["nodes"] = nodesJson;

		// 连线数组
		nlohmann::json linksJson = nlohmann::json::array();
		std::vector<AnimGraphLink> sortedLinks = m_Links;
		std::sort(sortedLinks.begin(), sortedLinks.end(),
			[](const AnimGraphLink& lhs, const AnimGraphLink& rhs)
			{
				if (lhs.toNodeId != rhs.toNodeId) return lhs.toNodeId < rhs.toNodeId;
				if (lhs.toPinIndex != rhs.toPinIndex) return lhs.toPinIndex < rhs.toPinIndex;
				if (lhs.fromNodeId != rhs.fromNodeId) return lhs.fromNodeId < rhs.fromNodeId;
				if (lhs.fromPinIndex != rhs.fromPinIndex) return lhs.fromPinIndex < rhs.fromPinIndex;
				return lhs.linkId < rhs.linkId;
			});
		for (const auto& link : sortedLinks)
		{
			linksJson.push_back({
				{ "id",       link.linkId },
				{ "fromNode", link.fromNodeId },
				{ "fromPin",  link.fromPinIndex },
				{ "toNode",   link.toNodeId },
				{ "toPin",    link.toPinIndex }
			});
		}
		outJson["links"] = linksJson;
	}

	std::unique_ptr<VansAnimGraph> VansAnimGraph::DeserializeFromJsonObject(const AnimGraphJson& j)
	{
		if (!j.is_object() || !j.contains("nodes") || !j["nodes"].is_array()
		    || !j.contains("links") || !j["links"].is_array())
			return nullptr;
		for (const auto& item : j.items())
		{
			if (item.key() != "nodes" && item.key() != "links")
				return nullptr;
		}

		auto graph = std::make_unique<VansAnimGraph>();
		int maxNodeId = 0;
		int maxLinkId = 0;
		int outputCount = 0;
		int entryCount = 0;
		std::unordered_set<int> nodeIds;
		std::unordered_set<int> linkIds;

		try
		{
			for (const auto& nj : j["nodes"])
			{
				if (!nj.is_object() || !nj.contains("id") || !nj["id"].is_number_integer()
				    || !nj.contains("type") || !nj["type"].is_string()
				    || !nj.contains("properties") || !nj["properties"].is_object())
					return nullptr;

				const int nodeId = nj["id"].get<int>();
				if (nodeId <= 0 || !nodeIds.insert(nodeId).second)
					return nullptr;

				auto node = CreateNodeByTypeName(nj["type"].get<std::string>());
				if (!node)
					return nullptr;
				node->m_NodeId = nodeId;
				if (nj.contains("name")) node->SetName(nj["name"].get<std::string>());
				if (nj.contains("posX")) node->m_EditorPosX = nj["posX"].get<float>();
				if (nj.contains("posY")) node->m_EditorPosY = nj["posY"].get<float>();
				DeserializeNodeProperties(node.get(), nj["properties"]);

				if (node->GetType() == AnimGraphNodeType::Entry)
				{
					++entryCount;
					graph->m_EntryNodeId = nodeId;
				}
				else if (node->GetType() == AnimGraphNodeType::Output)
				{
					++outputCount;
					graph->m_OutputNodeId = nodeId;
				}

				maxNodeId = (std::max)(maxNodeId, nodeId);
				graph->m_Nodes.emplace(nodeId, std::move(node));
			}
			if (outputCount != 1 || entryCount > 1)
				return nullptr;

			for (const auto& lj : j["links"])
			{
				if (!lj.is_object()
				    || !lj.contains("id") || !lj["id"].is_number_integer()
				    || !lj.contains("fromNode") || !lj["fromNode"].is_number_integer()
				    || !lj.contains("fromPin") || !lj["fromPin"].is_number_integer()
				    || !lj.contains("toNode") || !lj["toNode"].is_number_integer()
				    || !lj.contains("toPin") || !lj["toPin"].is_number_integer())
					return nullptr;

				const int linkId = lj["id"].get<int>();
				if (linkId <= 0 || !linkIds.insert(linkId).second)
					return nullptr;
				const int addedLinkId = graph->AddLink(
					lj["fromNode"].get<int>(), lj["fromPin"].get<int>(),
					lj["toNode"].get<int>(), lj["toPin"].get<int>());
				if (addedLinkId < 0)
					return nullptr;
				graph->m_Links.back().linkId = linkId;
				maxLinkId = (std::max)(maxLinkId, linkId);
			}

			if (!graph->GetInputNode(graph->m_OutputNodeId, 0))
				return nullptr;
		}
		catch (const nlohmann::json::exception&)
		{
			return nullptr;
		}

		graph->m_NextNodeId = maxNodeId + 1;
		graph->m_NextLinkId = maxLinkId + 1;

		return graph;
	}

	// ═════════════════════════════════════════════════════════════
	//  IK Node 实现
	// ═════════════════════════════════════════════════════════════

	// 在输入 Pose 上构建临时全局变换（拓扑顺序）
	static void BuildTempGlobals(
		const std::vector<glm::mat4>& localMatrices,
		const Skeleton&      skeleton,
		std::vector<glm::mat4>& outGlobals)
	{
		outGlobals.resize(localMatrices.size(), glm::mat4(1.0f));
		auto updateBone = [&](int boneIndex)
		{
			if (boneIndex < 0 || boneIndex >= static_cast<int>(localMatrices.size()))
				return;
			const int parentIndex = skeleton.bones[static_cast<std::size_t>(boneIndex)].parentIndex;
			outGlobals[static_cast<std::size_t>(boneIndex)] = parentIndex >= 0
				&& parentIndex < static_cast<int>(outGlobals.size())
				? outGlobals[static_cast<std::size_t>(parentIndex)]
					* localMatrices[static_cast<std::size_t>(boneIndex)]
				: localMatrices[static_cast<std::size_t>(boneIndex)];
		};
		if (!skeleton.topologicalOrder.empty())
			for (int boneIndex : skeleton.topologicalOrder) updateBone(boneIndex);
		else
			for (std::size_t boneIndex = 0; boneIndex < localMatrices.size(); ++boneIndex)
				updateBone(static_cast<int>(boneIndex));
	}

	static int ResolveGraphBoneIndex(const Skeleton& skeleton, const std::string& boneName)
	{
		if (boneName.empty()) return -1;
		const auto found = skeleton.boneNameToIndex.find(boneName);
		return found == skeleton.boneNameToIndex.end() ? -1 : found->second;
	}

	// 从 ctx.parameters 读取 Vector3 参数
	AnimGraphMotionMatchingNode::AnimGraphMotionMatchingNode()
	{
		m_Type = AnimGraphNodeType::MotionMatching;
		m_Name = "MotionMatching";
	}

	std::vector<AnimGraphPin> AnimGraphMotionMatchingNode::GetPins() const
	{
		return {
			{ 0, "FallbackPose", AnimGraphPinType::Pose, AnimGraphPinKind::Input  },
			{ 0, "OutPose",      AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphMotionMatchingNode::Evaluate(const AnimGraphContext& ctx,
	                                                   VansAnimGraphInstance& instance) const
	{
		if (ctx.motionMatching && ctx.skeleton && ctx.clips && ctx.parameters)
		{
			AnimGraphPose pose;
			std::vector<glm::mat4> sampledMatrices;
			if (ctx.motionMatching->Update(ctx.deltaTime,
			                               *ctx.skeleton,
			                               *ctx.clips,
			                               *ctx.parameters,
			                               ctx.ownerWorldTransform,
			                               sampledMatrices)
			    && VansPoseMath::FromMatrices(sampledMatrices, pose.localPose))
			{
				pose.valid = pose.localPose.size() == ctx.skeleton->bones.size();
				if (pose.valid)
					return pose;
			}
		}

		if (!m_EnableFallbackInput)
			return {};

		return instance.EvaluateInput(m_NodeId, 0, ctx);
	}

	AnimGraphSlotNode::AnimGraphSlotNode()
	{
		m_Type = AnimGraphNodeType::Slot;
		m_Name = "Slot";
	}

	std::vector<AnimGraphPin> AnimGraphSlotNode::GetPins() const
	{
		return {
			{ 0, "FallbackPose", AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphSlotNode::Evaluate(
		const AnimGraphContext& ctx,
		VansAnimGraphInstance& instance) const
	{
		AnimGraphPose fallback;
		if (m_EnableFallbackInput)
			fallback = instance.EvaluateInput(m_NodeId, 0, ctx);
		if (!ctx.slotPayloads)
			return fallback;
		auto slot = ctx.slotPayloads->find(m_SlotId);
		if (slot == ctx.slotPayloads->end() || !slot->second.valid)
			return fallback;
		if (!fallback.valid)
			return slot->second;

		const float sourceWeight = std::clamp(slot->second.sourceWeight, 0.0f, 1.0f);
		if (ctx.skeleton && (slot->second.sourceAdditive || !slot->second.sourceBoneMask.empty()))
		{
			VansCompiledBoneMask mask;
			mask.weights.assign(ctx.skeleton->bones.size(), 1.0f);
			if (slot->second.sourceBoneMask.size() == ctx.skeleton->bones.size())
				mask.weights.assign(slot->second.sourceBoneMask.begin(), slot->second.sourceBoneMask.end());
			mask.activeBones.reserve(mask.weights.size());
			for (std::size_t index = 0; index < mask.weights.size(); ++index)
				if (mask.weights[index] > 0.0f) mask.activeBones.push_back(static_cast<std::uint32_t>(index));
			mask.allZero = mask.activeBones.empty();
			mask.allOne = std::all_of(mask.weights.begin(), mask.weights.end(), [](float value) { return value >= 0.9999f; });
			mask.rootWeight = mask.weights.empty() ? 0.0f : mask.weights.front();
			mask.valid = true;
			VansAnimationLayerDefinition definition;
			definition.id = "TimelineSlot";
			definition.blendMode = slot->second.sourceAdditive ? VansLayerBlendMode::Additive : VansLayerBlendMode::Override;
			definition.rootMotion = VansLayerRootMotionMode::Override;
			definition.nodeTracks = VansLayerNodeTrackMode::Override;
			VansAnimationFrameVector<VansBoneTransform> referencePose;
			VansAnimationLayerMixer::BuildBindPose(*ctx.skeleton, referencePose);
			AnimGraphPose result = VansAnimationLayerMixer::ApplyLayer(
				fallback, slot->second, definition, mask, *ctx.skeleton, referencePose, 1.0f);
			result.sourceWeight = fallback.sourceWeight;
			return result;
		}
		AnimGraphPose result = VansPosePayloadMixer::BlendOverride(fallback, slot->second, sourceWeight);
		result.sourceWeight = fallback.sourceWeight;
		return result;
	}

	AnimGraphTargetPoseInputNode::AnimGraphTargetPoseInputNode()
	{
		m_Type = AnimGraphNodeType::TargetPoseInput;
		m_Name = "Target Pose Input";
	}

	std::vector<AnimGraphPin> AnimGraphTargetPoseInputNode::GetPins() const
	{
		return { { 0, "TargetPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output } };
	}

	AnimGraphPose AnimGraphTargetPoseInputNode::Evaluate(
		const AnimGraphContext& ctx,
		VansAnimGraphInstance&) const
	{
		return ctx.targetPoseInput ? *ctx.targetPoseInput : AnimGraphPose{};
	}

	static void ResolveIKChainBoneIndices(IKChainDefinition& chain, const Skeleton& skeleton)
	{
		for (IKBoneLink& link : chain.bones)
		{
			if (link.boneIndex >= 0 || link.boneName.empty())
				continue;

			auto it = skeleton.boneNameToIndex.find(link.boneName);
			if (it != skeleton.boneNameToIndex.end())
				link.boneIndex = it->second;
		}
	}

	static glm::vec3 ReadVec3Param(const AnimGraphContext& ctx,
	                               const std::string& name,
	                               const glm::vec3& def)
	{
		if (!ctx.parameters || name.empty()) return def;
		auto it = ctx.parameters->find(name);
		if (it == ctx.parameters->end()) return def;
		if (it->second.type != AnimatorParamType::Vector3) return def;
		return it->second.vec3Val;
	}

	static glm::quat ReadQuatParam(const AnimGraphContext& ctx,
	                               const std::string& name,
	                               const glm::quat& def)
	{
		if (!ctx.parameters || name.empty()) return def;
		auto it = ctx.parameters->find(name);
		if (it == ctx.parameters->end()) return def;
		if (it->second.type != AnimatorParamType::Quaternion) return def;
		return it->second.quatVal;
	}

	static float ReadFloatParam(const AnimGraphContext& ctx,
	                            const std::string& name,
	                            float def)
	{
		if (!ctx.parameters || name.empty()) return def;
		auto it = ctx.parameters->find(name);
		if (it == ctx.parameters->end()) return def;
		if (it->second.type != AnimatorParamType::Float) return def;
		return it->second.floatVal;
	}

	// ─── AnimGraphIKNode ─────────────────────────────────────────

	AnimGraphIKNode::AnimGraphIKNode()
	{
		m_Type = AnimGraphNodeType::IK;
		m_Name = "IK";
	}

	AnimGraphIKNode::~AnimGraphIKNode() = default;

	std::vector<AnimGraphPin> AnimGraphIKNode::GetPins() const
	{
		return {
			{ 0, "InPose",  AnimGraphPinType::Pose, AnimGraphPinKind::Input  },
			{ 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphIKNode::Evaluate(const AnimGraphContext& ctx,
	                                       VansAnimGraphInstance& instance) const
	{
		// 1. 拉取上游 Pose
		AnimGraphPose pose = instance.EvaluateInput(m_NodeId, 0, ctx);
		if (!pose.valid || !ctx.skeleton) return pose;
		if (m_Chain.bones.size() < 2) return pose;
		auto& runtime = instance.GetIKRuntimeState(m_NodeId);
		if (runtime.skeleton != ctx.skeleton)
		{
			runtime.chain = m_Chain;
			ResolveIKChainBoneIndices(runtime.chain, *ctx.skeleton);
			runtime.targetReferenceBoneIndex = ResolveGraphBoneIndex(
				*ctx.skeleton, m_TargetReferenceBoneName);
			runtime.skeleton = ctx.skeleton;
		}
		const IKChainDefinition& resolvedChain = runtime.chain;

		// 2. 读取目标
		float weight = m_UseFixedTarget
			? m_FixedWeight
			: ReadFloatParam(ctx, m_WeightParamName, m_FixedWeight);
		if (weight < 1e-4f) return pose;

		IKTarget target;
		target.position = m_UseFixedTarget
			? m_FixedTargetPos
			: ReadVec3Param(ctx, m_TargetPosParamName, m_FixedTargetPos);
		target.rotation = m_UseFixedTarget
			? m_FixedTargetRot
			: ReadQuatParam(ctx, m_TargetRotParamName, m_FixedTargetRot);
		target.positionWeight = weight;
		target.rotationWeight = m_Chain.enableRotationTarget ? weight * m_Chain.rotationWeight : 0.0f;
		target.positionSpace = m_TargetPositionSpace;
		target.rotationSpace = m_TargetRotationSpace;
		target.referenceBoneIndex = runtime.targetReferenceBoneIndex;

		// 3. 构建 tempGlobals
		VansPoseMath::ToMatrices(pose.localPose, runtime.localMatrices);
		BuildTempGlobals(runtime.localMatrices, *ctx.skeleton, runtime.globalMatrices);

		// 4. 求解
		if (VansIKSolver* solver = instance.GetIKSolver(m_NodeId, m_Chain.solverType))
		{
			IKSolveContext solveContext;
			solveContext.deltaTime = ctx.deltaTime;
			solveContext.ownerWorldTransform = ctx.ownerWorldTransform;
			solver->Solve(runtime.localMatrices, runtime.globalMatrices,
			              *ctx.skeleton, resolvedChain, target, solveContext);
			if (!VansPoseMath::FromMatrices(runtime.localMatrices, pose.localPose))
				return {};
		}

		return pose;
	}

	// ─── AnimGraphTwoBoneIKNode ──────────────────────────────────

	AnimGraphTwoBoneIKNode::AnimGraphTwoBoneIKNode()
	{
		m_Type = AnimGraphNodeType::TwoBoneIK;
		m_Name = "TwoBoneIK";
	}

	AnimGraphTwoBoneIKNode::~AnimGraphTwoBoneIKNode() = default;

	std::vector<AnimGraphPin> AnimGraphTwoBoneIKNode::GetPins() const
	{
		return {
			{ 0, "InPose",  AnimGraphPinType::Pose, AnimGraphPinKind::Input  },
			{ 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphTwoBoneIKNode::Evaluate(const AnimGraphContext& ctx,
	                                              VansAnimGraphInstance& instance) const
	{
		AnimGraphPose pose = instance.EvaluateInput(m_NodeId, 0, ctx);
		if (!pose.valid || !ctx.skeleton) return pose;

		auto& runtime = instance.GetIKRuntimeState(m_NodeId);
		if (runtime.skeleton != ctx.skeleton)
		{
			runtime.chain = m_UseLegProfile
				? VansIKChainBuilder::BuildHumanoidLeg(
					*ctx.skeleton, m_RootBoneName, m_MidBoneName, m_TipBoneName, m_IsRightSide)
				: VansIKChainBuilder::BuildHumanoidArm(
					*ctx.skeleton, m_RootBoneName, m_MidBoneName, m_TipBoneName, m_IsRightSide);
			runtime.chain.solverType = IKSolverType::TwoBone;
			if (runtime.chain.bones.size() >= 2)
			{
				runtime.chain.bones[0].constraint.coneAngleDeg = m_ConeAngle;
				runtime.chain.bones[1].constraint.minAngleY = m_HingeMinAngle;
				runtime.chain.bones[1].constraint.maxAngleY = m_HingeMaxAngle;
			}
			if (m_UsePoleVector)
			{
				runtime.chain.poleVector = m_PoleVector;
				runtime.chain.poleWeight = m_PoleWeight;
			}
			runtime.chain.poleSpace = m_PoleSpace;
			runtime.chain.poleReferenceBoneName = m_PoleReferenceBoneName;
			runtime.chain.enableRotationTarget = m_EnableRotationTarget;
			runtime.chain.rotationWeight = m_RotationWeight;
			runtime.chain.maintainEffectorGlobalRotation = m_MaintainEffectorGlobalRotation;
			runtime.chain.allowStretch = m_AllowStretch;
			runtime.chain.startStretchRatio = m_StartStretchRatio;
			runtime.chain.maxStretchScale = m_MaxStretchScale;
			runtime.targetReferenceBoneIndex = ResolveGraphBoneIndex(
				*ctx.skeleton, m_TargetReferenceBoneName);
			runtime.skeleton = ctx.skeleton;
		}
		IKChainDefinition& chain = runtime.chain;
		if (chain.bones.size() != 3 || !IK_ValidateChain(*ctx.skeleton, chain, true)) return pose;

		float weight = m_UseFixedTarget
			? m_FixedWeight
			: ReadFloatParam(ctx, m_WeightParamName, m_FixedWeight);
		if (weight < 1e-4f) return pose;

		IKTarget target;
		target.position = m_UseFixedTarget
			? m_FixedTargetPos
			: ReadVec3Param(ctx, m_TargetPosParamName, m_FixedTargetPos);
		target.rotation = m_UseFixedTarget
			? m_FixedTargetRot
			: ReadQuatParam(ctx, m_TargetRotParamName, m_FixedTargetRot);
		target.positionWeight = weight;
		target.rotationWeight = m_EnableRotationTarget ? weight * m_RotationWeight : 0.0f;
		target.positionSpace = m_TargetPositionSpace;
		target.rotationSpace = m_TargetRotationSpace;
		target.referenceBoneIndex = runtime.targetReferenceBoneIndex;

		VansPoseMath::ToMatrices(pose.localPose, runtime.localMatrices);
		BuildTempGlobals(runtime.localMatrices, *ctx.skeleton, runtime.globalMatrices);

		IKSolveContext solveContext;
		solveContext.deltaTime = ctx.deltaTime;
		solveContext.ownerWorldTransform = ctx.ownerWorldTransform;
		if (VansIKSolver* solver = instance.GetIKSolver(m_NodeId, IKSolverType::TwoBone))
			solver->Solve(runtime.localMatrices, runtime.globalMatrices, *ctx.skeleton,
				chain, target, solveContext);
		if (!VansPoseMath::FromMatrices(runtime.localMatrices, pose.localPose))
			return {};
		return pose;
	}

	// ─── AnimGraphLookAtNode ─────────────────────────────────────

	AnimGraphLookAtNode::AnimGraphLookAtNode()
	{
		m_Type = AnimGraphNodeType::LookAt;
		m_Name = "LookAt";
	}

	AnimGraphLookAtNode::~AnimGraphLookAtNode() = default;

	std::vector<AnimGraphPin> AnimGraphLookAtNode::GetPins() const
	{
		return {
			{ 0, "InPose",  AnimGraphPinType::Pose, AnimGraphPinKind::Input  },
			{ 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphLookAtNode::Evaluate(const AnimGraphContext& ctx,
	                                           VansAnimGraphInstance& instance) const
	{
		AnimGraphPose pose = instance.EvaluateInput(m_NodeId, 0, ctx);
		if (!pose.valid || !ctx.skeleton) return pose;
		if (m_BoneNames.empty()) return pose;

		auto& runtime = instance.GetIKRuntimeState(m_NodeId);
		if (runtime.skeleton != ctx.skeleton)
		{
			runtime.chain = VansIKChainBuilder::BuildLookAt(
				*ctx.skeleton, m_BoneNames, m_BoneWeights);
			runtime.targetReferenceBoneIndex = ResolveGraphBoneIndex(
				*ctx.skeleton, m_TargetReferenceBoneName);
			runtime.skeleton = ctx.skeleton;
		}
		const IKChainDefinition& chain = runtime.chain;
		if (chain.bones.empty()) return pose;

		float weight = m_UseFixedTarget
			? m_FixedWeight
			: ReadFloatParam(ctx, m_WeightParamName, m_FixedWeight);
		if (weight < 1e-4f) return pose;

		IKTarget target;
		target.position = m_UseFixedTarget
			? m_FixedTargetPos
			: ReadVec3Param(ctx, m_TargetPosParamName, m_FixedTargetPos);
		target.positionWeight = weight;
		target.positionSpace = m_TargetPositionSpace;
		target.referenceBoneIndex = runtime.targetReferenceBoneIndex;

		VansPoseMath::ToMatrices(pose.localPose, runtime.localMatrices);
		BuildTempGlobals(runtime.localMatrices, *ctx.skeleton, runtime.globalMatrices);

		auto* solver = static_cast<VansLookAtSolver*>(
			instance.GetIKSolver(m_NodeId, IKSolverType::LookAt));
		if (!solver) return pose;
		solver->m_ForwardAxis = m_ForwardAxis;
		solver->m_WorldForward = m_WorldForward;
		solver->m_ModelUp = m_ModelUp;
		solver->m_UpWeight = m_UpWeight;
		solver->m_MaxAnglePerBoneDeg = m_MaxAnglePerBoneDeg;
		IKSolveContext solveContext;
		solveContext.deltaTime = ctx.deltaTime;
		solveContext.ownerWorldTransform = ctx.ownerWorldTransform;
		solver->Solve(runtime.localMatrices, runtime.globalMatrices,
		             *ctx.skeleton, chain, target, solveContext);
		if (!VansPoseMath::FromMatrices(runtime.localMatrices, pose.localPose))
			return {};
		return pose;
	}

	// ─── AnimGraphFootPlacementNode ───────────────────────────────

	AnimGraphFootPlacementNode::AnimGraphFootPlacementNode()
	{
		m_Type = AnimGraphNodeType::FootPlacement;
		m_Name = "FootPlacement";
	}

	std::vector<AnimGraphPin> AnimGraphFootPlacementNode::GetPins() const
	{
		return {
			{ 0, "InPose",  AnimGraphPinType::Pose, AnimGraphPinKind::Input  },
			{ 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphFootPlacementNode::Evaluate(const AnimGraphContext& ctx,
	                                                  VansAnimGraphInstance& instance) const
	{
		AnimGraphPose pose = instance.EvaluateInput(m_NodeId, 0, ctx);
		if (!pose.valid)
			return pose;
		pose.footPlacement.valid = true;
		pose.footPlacement.sourceNodeId = m_NodeId;
		pose.footPlacement.settings = &m_Settings;
		return pose;
	}

}  // namespace VansGraphics
