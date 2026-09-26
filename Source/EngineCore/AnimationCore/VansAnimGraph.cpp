#include "VansAnimGraph.h"
#include "VansAnimationController.h"
#include "VansAnimationSampler.h"
#include "VansAnimationLayer.h"
#include "VansPoseMath.h"
#include "VansPosePayloadMixer.h"
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
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace VansGraphics
{
	bool VansAnimGraphMotionMatchingPort::Evaluate(
		float deltaTime,
		const Skeleton& skeleton,
		const std::unordered_map<std::string, VansAnimationClip>& clips,
		const std::unordered_map<std::string, AnimatorParameter>& parameters,
		const Vans::VansCharacterTrajectory* trajectory,
		VansPosePayload& outPayload) const
	{
		return m_Runtime && m_Runtime->Update(
			deltaTime, skeleton, clips, parameters, trajectory, outPayload);
	}

	AnimGraphPose VansAnimGraphNode::EvaluateInputPose(
		VansAnimGraphInstance& instance,
		int nodeId,
		int inputPinIndex,
		const AnimGraphContext& ctx)
	{
		return instance.EvaluateInput(nodeId, inputPinIndex, ctx);
	}

	AnimGraphPose VansAnimGraphNode::EvaluateNodePose(
		VansAnimGraphInstance& instance,
		int nodeId,
		const AnimGraphContext& ctx)
	{
		return instance.EvaluateNode(nodeId, ctx);
	}

	VansAnimGraphClipRuntimeState& VansAnimGraphNode::ResolveClipState(
		VansAnimGraphInstance& instance, int nodeId)
	{
		return instance.GetClipState(nodeId);
	}

	VansAnimGraphStateMachineRuntimeState&
	VansAnimGraphNode::ResolveStateMachineState(
		VansAnimGraphInstance& instance,
		int nodeId,
		const AnimGraphStateMachineNode& definition)
	{
		return instance.GetStateMachineState(nodeId, definition);
	}

	void VansAnimGraphNode::StoreCachedPose(
		VansAnimGraphInstance& instance,
		const std::string& name,
		const AnimGraphPose& pose)
	{
		instance.SetCachedPose(name, pose);
	}

	const AnimGraphPose* VansAnimGraphNode::ResolveCachedPose(
		const VansAnimGraphInstance& instance,
		const std::string& name)
	{
		return instance.FindCachedPose(name);
	}

	AnimGraphPose VansAnimGraphNode::AppendProceduralPose(
		VansAnimGraphInstance& instance,
		int nodeId,
		const AnimGraphContext& ctx)
	{
		AnimGraphPose pose = instance.EvaluateInput(nodeId, 0, ctx);
		if (pose.valid)
			pose.proceduralNodeIds.push_back(nodeId);
		return pose;
	}

	// ═════════════════════════════════════════════════════════════
	//  工具函数
	// ═════════════════════════════════════════════════════════════

	// ═════════════════════════════════════════════════════════════
	//  节点类型名称映射
	// ═════════════════════════════════════════════════════════════

	const char* VansAnimGraphNode::TypeToString(VansAnimGraphNodeType type)
	{
		switch (type)
		{
		case VansAnimGraphNodeType::Entry:          return "Entry";
		case VansAnimGraphNodeType::Output:         return "Output";
		case VansAnimGraphNodeType::Clip:           return "Clip";
		case VansAnimGraphNodeType::Blend:          return "Blend";
		case VansAnimGraphNodeType::Blend1D:        return "Blend1D";
		case VansAnimGraphNodeType::BlendSpace2D:   return "BlendSpace2D";
		case VansAnimGraphNodeType::IfCondition:    return "IfCondition";
		case VansAnimGraphNodeType::Switch:         return "Switch";
		case VansAnimGraphNodeType::AdditiveBlend:  return "AdditiveBlend";
		case VansAnimGraphNodeType::SpeedScale:     return "SpeedScale";
		case VansAnimGraphNodeType::StateMachine:   return "StateMachine";
		case VansAnimGraphNodeType::MotionMatching: return "MotionMatching";
		case VansAnimGraphNodeType::Slot:           return "Slot";
		case VansAnimGraphNodeType::TargetPoseInput:return "TargetPoseInput";
		case VansAnimGraphNodeType::Goal:           return "Goal";
		case VansAnimGraphNodeType::AimConstraint:  return "AimConstraint";
		case VansAnimGraphNodeType::Grounding:      return "Grounding";
		case VansAnimGraphNodeType::LimbIK:         return "LimbIK";
		case VansAnimGraphNodeType::ChainIK:        return "ChainIK";
		case VansAnimGraphNodeType::RotationDistribution: return "RotationDistribution";
		case VansAnimGraphNodeType::PoseCheckpoint: return "PoseCheckpoint";
		case VansAnimGraphNodeType::SaveCachedPose: return "SaveCachedPose";
		case VansAnimGraphNodeType::UseCachedPose: return "UseCachedPose";
		case VansAnimGraphNodeType::LayeredBlendPerBone: return "LayeredBlendPerBone";
		}
		return "Unknown";
	}

	// ═════════════════════════════════════════════════════════════
	//  EntryNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphEntryNode::AnimGraphEntryNode()
	{
		m_Type = VansAnimGraphNodeType::Entry;
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
		m_Type = VansAnimGraphNodeType::Output;
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
		return EvaluateInputPose(instance, m_NodeId, 0, ctx);
	}

	// ═════════════════════════════════════════════════════════════
	//  ClipNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphClipNode::AnimGraphClipNode()
	{
		m_Type = VansAnimGraphNodeType::Clip;
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
		const VansAnimGraphClipRuntimeState& runtime = ResolveClipState(instance, m_NodeId);
		VansAnimationSampleRequest request;
		request.previousTime = runtime.previousTime;
		request.currentTime = runtime.currentTime;
		request.loop = m_Loop;
		request.sourceNodeId = static_cast<std::uint64_t>(m_NodeId);
		VansAnimationSampler::Sample(it->second, *ctx.skeleton, request, pose);
		if (!m_RootMotion)
			pose.rootMotion = {};
		return pose;
	}

	// ═════════════════════════════════════════════════════════════
	//  BlendNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphBlendNode::AnimGraphBlendNode()
	{
		m_Type = VansAnimGraphNodeType::Blend;
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
		AnimGraphPose poseA = EvaluateInputPose(instance, m_NodeId, 0, ctx);
		AnimGraphPose poseB = EvaluateInputPose(instance, m_NodeId, 1, ctx);

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
		m_Type = VansAnimGraphNodeType::Blend1D;
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
			return EvaluateInputPose(instance, m_NodeId, 0, ctx);
		}

		// 找到 paramValue 落在哪两个阈值之间
		if (paramValue <= m_Thresholds.front())
		{
			return EvaluateInputPose(instance, m_NodeId, 0, ctx);
		}
		if (paramValue >= m_Thresholds.back())
		{
			return EvaluateInputPose(instance, m_NodeId, count - 1, ctx);
		}

		for (int i = 0; i < count - 1; ++i)
		{
			if (paramValue >= m_Thresholds[i] && paramValue <= m_Thresholds[i + 1])
			{
				float range = m_Thresholds[i + 1] - m_Thresholds[i];
				float alpha = (range > 0.0001f)
					? (paramValue - m_Thresholds[i]) / range
					: 0.0f;

				AnimGraphPose poseA = EvaluateInputPose(instance, m_NodeId, i, ctx);
				AnimGraphPose poseB = EvaluateInputPose(instance, m_NodeId, i + 1, ctx);

				if (!poseA.valid) return poseB;
				if (!poseB.valid) return poseA;

				return VansPosePayloadMixer::BlendOverride(poseA, poseB, alpha);
			}
		}

		return {};
	}

	// ═════════════════════════════════════════════════════════════
	//  BlendSpace2DNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphBlendSpace2DNode::AnimGraphBlendSpace2DNode()
	{
		m_Type = VansAnimGraphNodeType::BlendSpace2D;
		m_Name = "BlendSpace2D";
	}

	std::vector<AnimGraphPin> AnimGraphBlendSpace2DNode::GetPins() const
	{
		std::vector<AnimGraphPin> pins;
		for (int index = 0; index < static_cast<int>(m_Samples.size()); ++index)
			pins.push_back({ index, "Pose " + std::to_string(index),
				AnimGraphPinType::Pose, AnimGraphPinKind::Input });
		pins.push_back({ 0, "Result", AnimGraphPinType::Pose, AnimGraphPinKind::Output });
		return pins;
	}

	AnimGraphPose AnimGraphBlendSpace2DNode::Evaluate(const AnimGraphContext& ctx,
	                                                  VansAnimGraphInstance& instance) const
	{
		const int count = static_cast<int>(m_Samples.size());
		if (count <= 0)
			return {};
		float x = 0.0f;
		float y = 0.0f;
		if (ctx.parameters)
		{
			auto readFloat = [&](const std::string& name, float& value)
			{
				auto found = ctx.parameters->find(name);
				if (found != ctx.parameters->end() && found->second.type == AnimatorParamType::Float)
					value = found->second.floatVal;
			};
			readFloat(m_XParamName, x);
			readFloat(m_YParamName, y);
		}

		std::vector<float> weights(static_cast<size_t>(count), 0.0f);
		constexpr float epsilon = 1.0e-5f;
		for (int i = 0; i < count; ++i)
		{
			const float dx = x - m_Samples[static_cast<size_t>(i)].x;
			const float dy = y - m_Samples[static_cast<size_t>(i)].y;
			if (dx * dx + dy * dy <= epsilon * epsilon)
			{
				weights[static_cast<size_t>(i)] = 1.0f;
				break;
			}
		}

		bool exact = false;
		for (float weight : weights)
			if (weight > 0.0f) { exact = true; break; }
		if (!exact)
		{
			// 先找包含当前参数的采样三角形，顺序稳定且与资产保存顺序无关。
			for (int a = 0; a < count && !exact; ++a)
				for (int b = a + 1; b < count && !exact; ++b)
					for (int c = b + 1; c < count && !exact; ++c)
					{
						const auto& pa = m_Samples[static_cast<size_t>(a)];
						const auto& pb = m_Samples[static_cast<size_t>(b)];
						const auto& pc = m_Samples[static_cast<size_t>(c)];
						const float denominator = (pb.y - pc.y) * (pa.x - pc.x)
							+ (pc.x - pb.x) * (pa.y - pc.y);
						if (std::abs(denominator) <= epsilon)
							continue;
						const float wa = ((pb.y - pc.y) * (x - pc.x)
							+ (pc.x - pb.x) * (y - pc.y)) / denominator;
						const float wb = ((pc.y - pa.y) * (x - pc.x)
							+ (pa.x - pc.x) * (y - pc.y)) / denominator;
						const float wc = 1.0f - wa - wb;
						if (wa >= -epsilon && wb >= -epsilon && wc >= -epsilon)
						{
							weights[static_cast<size_t>(a)] = std::max(0.0f, wa);
							weights[static_cast<size_t>(b)] = std::max(0.0f, wb);
							weights[static_cast<size_t>(c)] = std::max(0.0f, wc);
							exact = true;
						}
					}
		}
		if (!exact)
		{
			// 参数在网格外：取最近的最多三个点，使用反距离权重，避免突然跳到单个样本。
			std::vector<int> order(static_cast<size_t>(count));
			std::iota(order.begin(), order.end(), 0);
			std::stable_sort(order.begin(), order.end(), [&](int lhs, int rhs)
			{
				const auto& a = m_Samples[static_cast<size_t>(lhs)];
				const auto& b = m_Samples[static_cast<size_t>(rhs)];
				const float da = (x - a.x) * (x - a.x) + (y - a.y) * (y - a.y);
				const float db = (x - b.x) * (x - b.x) + (y - b.y) * (y - b.y);
				return da < db;
			});
			const int selected = std::min(3, count);
			for (int rank = 0; rank < selected; ++rank)
			{
				const auto& sample = m_Samples[static_cast<size_t>(order[static_cast<size_t>(rank)])];
				const float distanceSquared = (x - sample.x) * (x - sample.x)
					+ (y - sample.y) * (y - sample.y);
				weights[static_cast<size_t>(order[static_cast<size_t>(rank)])] =
					1.0f / std::max(distanceSquared, epsilon * epsilon);
			}
		}

		float totalWeight = 0.0f;
		for (float weight : weights) totalWeight += weight;
		if (totalWeight <= epsilon)
			return {};
		AnimGraphPose result;
		float accumulated = 0.0f;
		for (int index = 0; index < count; ++index)
		{
			if (weights[static_cast<size_t>(index)] <= epsilon)
				continue;
			AnimGraphPose pose = EvaluateInputPose(instance, m_NodeId, index, ctx);
			if (!pose.valid)
				continue;
			const float normalizedWeight = weights[static_cast<size_t>(index)] / totalWeight;
			if (!result.valid)
			{
				result = std::move(pose);
				accumulated = normalizedWeight;
				continue;
			}
			const float alpha = normalizedWeight / std::max(accumulated + normalizedWeight, epsilon);
			result = VansPosePayloadMixer::BlendOverride(result, pose, alpha);
			accumulated += normalizedWeight;
		}
		return result;
	}

	// ═════════════════════════════════════════════════════════════
	//  IfConditionNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphIfConditionNode::AnimGraphIfConditionNode()
	{
		m_Type = VansAnimGraphNodeType::IfCondition;
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
					case CompareOp::AbsGreaterEqual: condResult = std::abs(a) >= b; break;
					case CompareOp::AbsLess:          condResult = std::abs(a) < b; break;
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
		return EvaluateInputPose(instance, m_NodeId, pinIndex, ctx);
	}

	// ═════════════════════════════════════════════════════════════
	//  SwitchNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphSwitchNode::AnimGraphSwitchNode()
	{
		m_Type = VansAnimGraphNodeType::Switch;
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

		return EvaluateInputPose(instance, m_NodeId, selectedCase, ctx);
	}

	// ═════════════════════════════════════════════════════════════
	//  AdditiveBlendNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphAdditiveBlendNode::AnimGraphAdditiveBlendNode()
	{
		m_Type = VansAnimGraphNodeType::AdditiveBlend;
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
		AnimGraphPose basePose = EvaluateInputPose(instance, m_NodeId, 0, ctx);
		AnimGraphPose additivePose = EvaluateInputPose(instance, m_NodeId, 1, ctx);

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
		m_Type = VansAnimGraphNodeType::SpeedScale;
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
		return EvaluateInputPose(instance, m_NodeId, 0, scaledContext);
	}

	// ═════════════════════════════════════════════════════════════
	//  StateMachineNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphStateMachineNode::AnimGraphStateMachineNode()
	{
		m_Type = VansAnimGraphNodeType::StateMachine;
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
			ResolveStateMachineState(instance, m_NodeId, *this);
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
					case CompareOp::AbsGreaterEqual: satisfied = std::abs(parameter.floatVal) >= condition.floatVal; break;
					case CompareOp::AbsLess: satisfied = std::abs(parameter.floatVal) < condition.floatVal; break;
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
				if (state.poseNodeId >= 0)
				{
					// A state may own a complete pose subgraph (BlendSpace,
					// layered pose, or another generic node).  The state machine
					// still owns transition selection/blending; the referenced
					// node owns pose evaluation and curve output.
					return EvaluateNodePose(instance, state.poseNodeId, ctx);
				}
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
				if (!state.rootMotion)
					pose.rootMotion = {};
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

	namespace
	{
		template<typename Node>
		std::unique_ptr<VansAnimGraphNode> CloneNodeAs(const VansAnimGraphNode& source)
		{
			return std::make_unique<Node>(static_cast<const Node&>(source));
		}

		std::unique_ptr<VansAnimGraphNode> CloneNodeDefinition(const VansAnimGraphNode& source)
		{
			switch (source.GetType())
			{
			case VansAnimGraphNodeType::Entry: return CloneNodeAs<AnimGraphEntryNode>(source);
			case VansAnimGraphNodeType::Output: return CloneNodeAs<AnimGraphOutputNode>(source);
			case VansAnimGraphNodeType::Clip: return CloneNodeAs<AnimGraphClipNode>(source);
			case VansAnimGraphNodeType::Blend: return CloneNodeAs<AnimGraphBlendNode>(source);
			case VansAnimGraphNodeType::Blend1D: return CloneNodeAs<AnimGraphBlend1DNode>(source);
			case VansAnimGraphNodeType::BlendSpace2D: return CloneNodeAs<AnimGraphBlendSpace2DNode>(source);
			case VansAnimGraphNodeType::IfCondition: return CloneNodeAs<AnimGraphIfConditionNode>(source);
			case VansAnimGraphNodeType::Switch: return CloneNodeAs<AnimGraphSwitchNode>(source);
			case VansAnimGraphNodeType::AdditiveBlend: return CloneNodeAs<AnimGraphAdditiveBlendNode>(source);
			case VansAnimGraphNodeType::SpeedScale: return CloneNodeAs<AnimGraphSpeedScaleNode>(source);
			case VansAnimGraphNodeType::StateMachine: return CloneNodeAs<AnimGraphStateMachineNode>(source);
			case VansAnimGraphNodeType::MotionMatching: return CloneNodeAs<AnimGraphMotionMatchingNode>(source);
			case VansAnimGraphNodeType::Slot: return CloneNodeAs<AnimGraphSlotNode>(source);
			case VansAnimGraphNodeType::TargetPoseInput: return CloneNodeAs<AnimGraphTargetPoseInputNode>(source);
			case VansAnimGraphNodeType::Goal: return CloneNodeAs<AnimGraphGoalNode>(source);
			case VansAnimGraphNodeType::AimConstraint: return CloneNodeAs<AnimGraphAimConstraintNode>(source);
			case VansAnimGraphNodeType::Grounding: return CloneNodeAs<AnimGraphGroundingNode>(source);
			case VansAnimGraphNodeType::LimbIK: return CloneNodeAs<AnimGraphLimbIKNode>(source);
			case VansAnimGraphNodeType::ChainIK: return CloneNodeAs<AnimGraphChainIKNode>(source);
			case VansAnimGraphNodeType::PoseCheckpoint: return CloneNodeAs<AnimGraphPoseCheckpointNode>(source);
			case VansAnimGraphNodeType::RotationDistribution: return CloneNodeAs<AnimGraphRotationDistributionNode>(source);
			case VansAnimGraphNodeType::SaveCachedPose: return CloneNodeAs<AnimGraphSaveCachedPoseNode>(source);
			case VansAnimGraphNodeType::UseCachedPose: return CloneNodeAs<AnimGraphUseCachedPoseNode>(source);
			case VansAnimGraphNodeType::LayeredBlendPerBone: return CloneNodeAs<AnimGraphLayeredBlendPerBoneNode>(source);
			}
			return nullptr;
		}
	}

	std::unique_ptr<VansAnimGraph> VansAnimGraph::Clone() const
	{
		auto clone = std::make_unique<VansAnimGraph>();
		clone->m_Nodes.reserve(m_Nodes.size());
		for (const auto& [nodeId, node] : m_Nodes)
		{
			if (!node)
				return nullptr;
			auto clonedNode = CloneNodeDefinition(*node);
			if (!clonedNode)
				return nullptr;
			clone->m_Nodes.emplace(nodeId, std::move(clonedNode));
		}
		clone->m_Links = m_Links;
		clone->m_EntryNodeId = m_EntryNodeId;
		clone->m_OutputNodeId = m_OutputNodeId;
		clone->m_NextNodeId = m_NextNodeId;
		clone->m_NextLinkId = m_NextLinkId;
		return clone;
	}

	int VansAnimGraph::AddNode(std::unique_ptr<VansAnimGraphNode> node)
	{
		if (!node)
			return -1;
		if (node->GetType() == VansAnimGraphNodeType::Entry && m_EntryNodeId >= 0)
			return -1;
		if (node->GetType() == VansAnimGraphNodeType::Output && m_OutputNodeId >= 0)
			return -1;

		int id = m_NextNodeId++;
		node->m_NodeId = id;

		// 自动记录 Entry / Output 节点
		if (node->GetType() == VansAnimGraphNodeType::Entry)
			m_EntryNodeId = id;
		else if (node->GetType() == VansAnimGraphNodeType::Output)
			m_OutputNodeId = id;

		m_Nodes[id] = std::move(node);
		return id;
	}

	bool VansAnimGraph::AddNodeWithId(std::unique_ptr<VansAnimGraphNode> node, int nodeId)
	{
		if (!node || nodeId <= 0 || m_Nodes.find(nodeId) != m_Nodes.end())
			return false;
		if (node->GetType() == VansAnimGraphNodeType::Entry && m_EntryNodeId >= 0)
			return false;
		if (node->GetType() == VansAnimGraphNodeType::Output && m_OutputNodeId >= 0)
			return false;

		node->m_NodeId = nodeId;
		if (node->GetType() == VansAnimGraphNodeType::Entry)
			m_EntryNodeId = nodeId;
		else if (node->GetType() == VansAnimGraphNodeType::Output)
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
			int& visitMark = visitState[nodeId];
			if (visitMark == 2)
				return true;
			if (visitMark == 1)
			{
				outError = "Animation graph contains a directed cycle at node " + std::to_string(nodeId);
				return false;
			}
			const VansAnimGraphNode* node = GetNode(nodeId);
			if (!node)
			{
				outError = "Animation graph execution plan references missing node " + std::to_string(nodeId);
				return false;
			}

			visitMark = 1;
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
			// A state may reference a complete pose subgraph (for example a
			// BlendSpace or layered pose) instead of a single clip.  These
			// references are part of the graph's dependency tree even though they
			// are not represented as ordinary input links on the StateMachine
			// node.  Visit them before publishing the StateMachine so the plan is
			// topologically ordered and so unreferenced pose nodes are rejected.
			if (node->GetType() == VansAnimGraphNodeType::StateMachine)
			{
				const auto* stateMachine = static_cast<const AnimGraphStateMachineNode*>(node);
				for (const AnimatorState& animatorState : stateMachine->m_States)
				{
					if (animatorState.poseNodeId >= 0 && !visit(animatorState.poseNodeId))
						return false;
				}
			}
			visitMark = 2;
			outPlan.push_back(nodeId);
			return true;
		};

		if (!visit(m_OutputNodeId))
			return false;
		for (const auto& [nodeId, node] : m_Nodes)
		{
			if (node && node->GetType() != VansAnimGraphNodeType::Entry
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
			if (node->GetType() == VansAnimGraphNodeType::SpeedScale)
				speedPath.push_back(nodeId);
			const bool statefulSource = node->GetType() == VansAnimGraphNodeType::Clip
				|| node->GetType() == VansAnimGraphNodeType::StateMachine
				|| node->GetType() == VansAnimGraphNodeType::MotionMatching;
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
			if (node->GetType() == VansAnimGraphNodeType::StateMachine)
			{
				const auto* stateMachine = static_cast<const AnimGraphStateMachineNode*>(node);
				for (const AnimatorState& state : stateMachine->m_States)
					if (state.poseNodeId >= 0
						&& !validateSpeedPath(state.poseNodeId, speedPath))
						return false;
			}
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

	const VansAnimGraphInstance::VansLayeredBlendRuntimeState&
	VansAnimGraphInstance::ResolveLayeredBlendRuntime(
		int nodeId, const VansBoneMaskAsset& mask, const Skeleton& skeleton)
	{
		auto found = m_LayeredBlendRuntimes.try_emplace(nodeId).first;
		VansLayeredBlendRuntimeState& runtime = found->second;
		const std::uint64_t signature = skeleton.signature != 0
			? skeleton.signature
			: skeleton.ComputeSignature();
		if (!runtime.initialized || runtime.skeletonSignature != signature)
		{
			VansCompiledBoneMask compiledMask = VansBoneMaskCompiler::Compile(mask, skeleton);
			VansAnimationFrameVector<VansBoneTransform> bindPose{
				std::pmr::new_delete_resource() };
			VansAnimationLayerMixer::BuildBindPose(skeleton, bindPose);

			runtime.skeletonSignature = signature;
			runtime.mask = std::move(compiledMask);
			runtime.bindPose = std::move(bindPose);
			runtime.initialized = true;
		}
		return runtime;
	}

	AnimGraphPose VansAnimGraphInstance::Evaluate(const AnimGraphContext& ctx)
	{
		if (!IsCompiled())
			return {};
		m_CachedPoses.clear();
		for (int nodeId : m_ExecutionPlan)
		{
			m_EvaluatedNodes[nodeId] = false;
			m_EvaluatingNodes[nodeId] = false;
		}
		// Resolve named cache writers before the output pull so an independent
		// UseCachedPose branch observes the same frame's pose.
		for (int nodeId : m_ExecutionPlan)
		{
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (node && node->GetType() == VansAnimGraphNodeType::SaveCachedPose)
				EvaluateNode(nodeId, ctx);
		}
		AnimGraphPose result = EvaluateNode(m_Definition.GetOutputNodeId(), ctx);
		for (int nodeId : m_ExecutionPlan)
			m_PreviousActiveNodes[nodeId] = m_EvaluatedNodes[nodeId];
		return result;
	}

	AnimGraphPose VansAnimGraphInstance::EvaluateFrame(const AnimGraphContext& ctx)
	{
		AdvanceTime(ctx.deltaTime, ctx);
		return Evaluate(ctx);
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
			if (node->GetType() == VansAnimGraphNodeType::SpeedScale)
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
			if (node->GetType() == VansAnimGraphNodeType::StateMachine)
			{
				const auto* stateMachine = static_cast<const AnimGraphStateMachineNode*>(node);
				const auto& runtime = GetStateMachineState(nodeId, *stateMachine);
				auto propagateStatePose = [&](const std::string& stateName)
				{
					for (const AnimatorState& state : stateMachine->m_States)
					{
						if (state.name != stateName || state.poseNodeId < 0)
							continue;
						float stateScale = state.speed;
						if (!state.speedParameter.empty() && ctx.parameters)
						{
							auto parameter = ctx.parameters->find(state.speedParameter);
							if (parameter != ctx.parameters->end()
								&& parameter->second.type == AnimatorParamType::Float)
								stateScale *= parameter->second.floatVal;
						}
						self(self, state.poseNodeId, inputScale * stateScale);
						break;
					}
				};
				propagateStatePose(runtime.currentStateName);
				if (runtime.blendState == ControllerBlendState::Blending)
					propagateStatePose(runtime.previousStateName);
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
			if (node->GetType() == VansAnimGraphNodeType::Clip)
			{
				const auto* clipNode = static_cast<const AnimGraphClipNode*>(node);
				VansAnimGraphClipRuntimeState& state = GetClipState(nodeId);
				state.previousTime = state.currentTime;
				const float timeScale = m_HasActiveTimeScale[nodeId]
					? m_ActiveTimeScales[nodeId] : 1.0f;
				state.currentTime += deltaTime * timeScale * clipNode->m_Speed;
				continue;
			}
			if (node->GetType() != VansAnimGraphNodeType::StateMachine || !ctx.clips)
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
					float stateSpeed = state->speed;
					if (!state->speedParameter.empty() && ctx.parameters)
					{
						auto parameter = ctx.parameters->find(state->speedParameter);
						if (parameter != ctx.parameters->end()
						    && parameter->second.type == AnimatorParamType::Float)
							stateSpeed *= parameter->second.floatVal;
					}
					time += deltaTime * timeScale * stateSpeed;
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
		m_CachedPoses.clear();
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
			if (!node || node->GetType() != VansAnimGraphNodeType::StateMachine)
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
					m_PreviousActiveNodes[nodeId] = true;
					return true;
			}
		}
		return false;
	}

	void VansAnimGraphInstance::SetCachedPose(const std::string& name,
	                                           const AnimGraphPose& pose)
	{
		if (!name.empty())
			m_CachedPoses[name] = pose;
	}

	const AnimGraphPose* VansAnimGraphInstance::FindCachedPose(const std::string& name) const
	{
		auto it = m_CachedPoses.find(name);
		return it == m_CachedPoses.end() ? nullptr : &it->second;
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

	std::string VansAnimGraphInstance::GetActiveStatePath() const
	{
		std::string path;
		for (int nodeId : m_ExecutionPlan)
		{
			const VansAnimGraphNode* node = m_Definition.GetNode(nodeId);
			if (!node || node->GetType() != VansAnimGraphNodeType::StateMachine)
				continue;
			auto runtimeIt = m_StateMachineStates.find(nodeId);
			if (runtimeIt == m_StateMachineStates.end()
				|| runtimeIt->second.currentStateName.empty())
				continue;
			if (!path.empty())
				path += " | ";
			path += node->GetName();
			path += ":";
			path += runtimeIt->second.currentStateName;
			if (runtimeIt->second.blendState == ControllerBlendState::Blending
				&& !runtimeIt->second.previousStateName.empty())
			{
				path += "<-";
				path += runtimeIt->second.previousStateName;
			}
		}
		return path;
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
			if (node->GetType() == VansAnimGraphNodeType::StateMachine)
			{
				auto runtimeIt = m_StateMachineStates.find(nodeId);
				if (runtimeIt == m_StateMachineStates.end())
					continue;
				const auto* stateMachine = static_cast<const AnimGraphStateMachineNode*>(node);
				for (const AnimatorState& state : stateMachine->m_States)
					if (state.name == runtimeIt->second.currentStateName)
						return state.clipName;
			}
			if (node->GetType() == VansAnimGraphNodeType::Clip
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
			if (node->GetType() == VansAnimGraphNodeType::StateMachine)
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
				m_PreviousActiveNodes[nodeId] = true;
				return true;
			}
			if (node->GetType() == VansAnimGraphNodeType::Clip)
			{
				VansAnimGraphClipRuntimeState& runtime = GetClipState(nodeId);
				runtime.previousTime = runtime.currentTime;
				runtime.currentTime = time;
				m_PreviousActiveNodes[nodeId] = true;
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
			if (node && node->GetType() == VansAnimGraphNodeType::StateMachine
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
			if (node && node->GetType() == VansAnimGraphNodeType::StateMachine)
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
		m_PreviousActiveNodes[followerNodeId] = true;
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
			if (!node || node->GetType() != VansAnimGraphNodeType::Clip
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
			if (!node || node->GetType() != VansAnimGraphNodeType::StateMachine)
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

	// ─── 节点工厂 ──────────────────────────────────────────────

	std::unique_ptr<VansAnimGraphNode> VansAnimGraph::CreateNodeByType(VansAnimGraphNodeType type)
	{
		switch (type)
		{
		case VansAnimGraphNodeType::Entry:         return std::make_unique<AnimGraphEntryNode>();
		case VansAnimGraphNodeType::Output:        return std::make_unique<AnimGraphOutputNode>();
		case VansAnimGraphNodeType::Clip:          return std::make_unique<AnimGraphClipNode>();
		case VansAnimGraphNodeType::Blend:         return std::make_unique<AnimGraphBlendNode>();
		case VansAnimGraphNodeType::Blend1D:       return std::make_unique<AnimGraphBlend1DNode>();
		case VansAnimGraphNodeType::BlendSpace2D:  return std::make_unique<AnimGraphBlendSpace2DNode>();
		case VansAnimGraphNodeType::IfCondition:   return std::make_unique<AnimGraphIfConditionNode>();
		case VansAnimGraphNodeType::Switch:        return std::make_unique<AnimGraphSwitchNode>();
		case VansAnimGraphNodeType::AdditiveBlend: return std::make_unique<AnimGraphAdditiveBlendNode>();
		case VansAnimGraphNodeType::SpeedScale:    return std::make_unique<AnimGraphSpeedScaleNode>();
		case VansAnimGraphNodeType::StateMachine:  return std::make_unique<AnimGraphStateMachineNode>();
		case VansAnimGraphNodeType::MotionMatching:return std::make_unique<AnimGraphMotionMatchingNode>();
		case VansAnimGraphNodeType::Slot:          return std::make_unique<AnimGraphSlotNode>();
		case VansAnimGraphNodeType::TargetPoseInput:return std::make_unique<AnimGraphTargetPoseInputNode>();
		case VansAnimGraphNodeType::Goal:          return std::make_unique<AnimGraphGoalNode>();
		case VansAnimGraphNodeType::AimConstraint: return std::make_unique<AnimGraphAimConstraintNode>();
		case VansAnimGraphNodeType::Grounding:     return std::make_unique<AnimGraphGroundingNode>();
		case VansAnimGraphNodeType::LimbIK:        return std::make_unique<AnimGraphLimbIKNode>();
		case VansAnimGraphNodeType::ChainIK:       return std::make_unique<AnimGraphChainIKNode>();
		case VansAnimGraphNodeType::RotationDistribution: return std::make_unique<AnimGraphRotationDistributionNode>();
		case VansAnimGraphNodeType::PoseCheckpoint: return std::make_unique<AnimGraphPoseCheckpointNode>();
		case VansAnimGraphNodeType::SaveCachedPose: return std::make_unique<AnimGraphSaveCachedPoseNode>();
		case VansAnimGraphNodeType::UseCachedPose: return std::make_unique<AnimGraphUseCachedPoseNode>();
		case VansAnimGraphNodeType::LayeredBlendPerBone: return std::make_unique<AnimGraphLayeredBlendPerBoneNode>();
		}
		return nullptr;
	}

	std::unique_ptr<VansAnimGraphNode> VansAnimGraph::CreateNodeByTypeName(const std::string& typeName)
	{
		if (typeName == "Entry")          return CreateNodeByType(VansAnimGraphNodeType::Entry);
		if (typeName == "Output")         return CreateNodeByType(VansAnimGraphNodeType::Output);
		if (typeName == "Clip")           return CreateNodeByType(VansAnimGraphNodeType::Clip);
		if (typeName == "Blend")          return CreateNodeByType(VansAnimGraphNodeType::Blend);
		if (typeName == "Blend1D")        return CreateNodeByType(VansAnimGraphNodeType::Blend1D);
		if (typeName == "BlendSpace2D")   return CreateNodeByType(VansAnimGraphNodeType::BlendSpace2D);
		if (typeName == "IfCondition")    return CreateNodeByType(VansAnimGraphNodeType::IfCondition);
		if (typeName == "Switch")         return CreateNodeByType(VansAnimGraphNodeType::Switch);
		if (typeName == "AdditiveBlend")  return CreateNodeByType(VansAnimGraphNodeType::AdditiveBlend);
		if (typeName == "SpeedScale")     return CreateNodeByType(VansAnimGraphNodeType::SpeedScale);
		if (typeName == "StateMachine")   return CreateNodeByType(VansAnimGraphNodeType::StateMachine);
		if (typeName == "MotionMatching") return CreateNodeByType(VansAnimGraphNodeType::MotionMatching);
		if (typeName == "Slot")           return CreateNodeByType(VansAnimGraphNodeType::Slot);
		if (typeName == "TargetPoseInput")return CreateNodeByType(VansAnimGraphNodeType::TargetPoseInput);
		if (typeName == "Goal")           return CreateNodeByType(VansAnimGraphNodeType::Goal);
		if (typeName == "AimConstraint")  return CreateNodeByType(VansAnimGraphNodeType::AimConstraint);
		if (typeName == "Grounding")      return CreateNodeByType(VansAnimGraphNodeType::Grounding);
		if (typeName == "LimbIK")         return CreateNodeByType(VansAnimGraphNodeType::LimbIK);
		if (typeName == "ChainIK")        return CreateNodeByType(VansAnimGraphNodeType::ChainIK);
		if (typeName == "RotationDistribution") return CreateNodeByType(VansAnimGraphNodeType::RotationDistribution);
		if (typeName == "PoseCheckpoint") return CreateNodeByType(VansAnimGraphNodeType::PoseCheckpoint);
		if (typeName == "SaveCachedPose") return CreateNodeByType(VansAnimGraphNodeType::SaveCachedPose);
		if (typeName == "UseCachedPose") return CreateNodeByType(VansAnimGraphNodeType::UseCachedPose);
		if (typeName == "LayeredBlendPerBone") return CreateNodeByType(VansAnimGraphNodeType::LayeredBlendPerBone);
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
		case CompareOp::AbsGreaterEqual: return "abs>=";
		case CompareOp::AbsLess:          return "abs<";
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
		if (s == "abs>=") return CompareOp::AbsGreaterEqual;
		if (s == "abs<") return CompareOp::AbsLess;
		return CompareOp::Equal;
	}

	// 序列化单个节点的特有属性
	static const char* GoalSourceToString(VansGraphGoalSource source)
	{
		switch (source)
		{
		case VansGraphGoalSource::Binding: return "binding";
		case VansGraphGoalSource::Parameters: return "parameters";
		case VansGraphGoalSource::Fixed: return "fixed";
		}
		throw std::invalid_argument("Invalid procedural Goal source enum");
	}

	static VansGraphGoalSource StringToGoalSource(const std::string& source)
	{
		if (source == "binding") return VansGraphGoalSource::Binding;
		if (source == "parameters") return VansGraphGoalSource::Parameters;
		if (source == "fixed") return VansGraphGoalSource::Fixed;
		throw std::invalid_argument("Unknown procedural goal source: " + source);
	}

	static void RequireOnlyFields(const nlohmann::json& value,
		std::initializer_list<const char*> allowed)
	{
		if (!value.is_object())
			throw std::invalid_argument("Animation graph value must be an object");
		for (const auto& item : value.items())
		{
			const bool known = std::any_of(allowed.begin(), allowed.end(),
				[&](const char* field) { return item.key() == field; });
			if (!known)
				throw std::invalid_argument("Unknown animation graph field: " + item.key());
		}
	}

	static nlohmann::json SerializeGoal(const VansGraphGoalDefinition& goal)
	{
		return {
			{ "id", goal.goalId }, { "source", GoalSourceToString(goal.source) },
			{ "binding", goal.binding }, { "positionParameter", goal.positionParameter },
			{ "rotationParameter", goal.rotationParameter }, { "weightParameter", goal.weightParameter },
			{ "fixedPositionModel", { goal.fixedPositionModel.x, goal.fixedPositionModel.y, goal.fixedPositionModel.z } },
			{ "fixedRotationModel", { goal.fixedRotationModel.x, goal.fixedRotationModel.y,
				goal.fixedRotationModel.z, goal.fixedRotationModel.w } },
			{ "fixedPositionWeight", goal.fixedPositionWeight },
			{ "fixedRotationWeight", goal.fixedRotationWeight }
		};
	}

	static void DeserializeGoal(const nlohmann::json& value, VansGraphGoalDefinition& goal)
	{
		RequireOnlyFields(value, { "id", "source", "binding", "positionParameter",
			"rotationParameter", "weightParameter", "fixedPositionModel",
			"fixedRotationModel", "fixedPositionWeight", "fixedRotationWeight" });
		goal.goalId = value.at("id").get<std::string>();
		goal.source = StringToGoalSource(value.at("source").get<std::string>());
		goal.binding = value.at("binding").get<std::string>();
		goal.positionParameter = value.at("positionParameter").get<std::string>();
		goal.rotationParameter = value.at("rotationParameter").get<std::string>();
		goal.weightParameter = value.at("weightParameter").get<std::string>();
		const auto& position = value.at("fixedPositionModel");
		goal.fixedPositionModel = { position.at(0).get<float>(), position.at(1).get<float>(), position.at(2).get<float>() };
		const auto& rotation = value.at("fixedRotationModel");
		goal.fixedRotationModel = { rotation.at(3).get<float>(), rotation.at(0).get<float>(),
			rotation.at(1).get<float>(), rotation.at(2).get<float>() };
		goal.fixedPositionWeight = value.at("fixedPositionWeight").get<float>();
		goal.fixedRotationWeight = value.at("fixedRotationWeight").get<float>();
	}

	static const char* PlantPivotToString(VansPlantPivot pivot)
	{
		switch (pivot)
		{
		case VansPlantPivot::Heel: return "heel";
		case VansPlantPivot::Ball: return "ball";
		case VansPlantPivot::Ankle: return "ankle";
		}
		throw std::invalid_argument("Invalid Grounding plant pivot enum");
	}

	static VansPlantPivot StringToPlantPivot(const std::string& pivot)
	{
		if (pivot == "heel") return VansPlantPivot::Heel;
		if (pivot == "ball") return VansPlantPivot::Ball;
		if (pivot == "ankle") return VansPlantPivot::Ankle;
		throw std::invalid_argument("Unknown grounding plant pivot: " + pivot);
	}

	static nlohmann::json SerializeNodeProperties(const VansAnimGraphNode* node)
	{
		nlohmann::json props = nlohmann::json::object();
		switch (node->GetType())
		{
		case VansAnimGraphNodeType::Clip:
		{
			auto* n = static_cast<const AnimGraphClipNode*>(node);
			props["clipName"] = n->m_ClipName;
			props["speed"]    = n->m_Speed;
			props["loop"]     = n->m_Loop;
			props["rootMotion"] = n->m_RootMotion;
			break;
		}
		case VansAnimGraphNodeType::Blend:
		{
			auto* n = static_cast<const AnimGraphBlendNode*>(node);
			props["paramName"]  = n->m_ParamName;
			props["fixedAlpha"] = n->m_FixedAlpha;
			props["useParam"]   = n->m_UseParam;
			break;
		}
		case VansAnimGraphNodeType::Blend1D:
		{
			auto* n = static_cast<const AnimGraphBlend1DNode*>(node);
			props["paramName"]  = n->m_ParamName;
			props["thresholds"] = n->m_Thresholds;
			break;
		}
		case VansAnimGraphNodeType::BlendSpace2D:
		{
			auto* n = static_cast<const AnimGraphBlendSpace2DNode*>(node);
			props["xParamName"] = n->m_XParamName;
			props["yParamName"] = n->m_YParamName;
			props["samples"] = nlohmann::json::array();
			for (const auto& sample : n->m_Samples)
				props["samples"].push_back({ { "x", sample.x }, { "y", sample.y } });
			break;
		}
		case VansAnimGraphNodeType::IfCondition:
		{
			auto* n = static_cast<const AnimGraphIfConditionNode*>(node);
			props["paramName"] = n->m_ParamName;
			props["op"]        = CompareOpToString(n->m_CompareOp);
			props["floatVal"]  = n->m_FloatVal;
			props["boolVal"]   = n->m_BoolVal;
			props["intVal"]    = n->m_IntVal;
			break;
		}
		case VansAnimGraphNodeType::Switch:
		{
			auto* n = static_cast<const AnimGraphSwitchNode*>(node);
			props["paramName"] = n->m_ParamName;
			props["caseCount"] = n->m_CaseCount;
			break;
		}
		case VansAnimGraphNodeType::AdditiveBlend:
		{
			auto* n = static_cast<const AnimGraphAdditiveBlendNode*>(node);
			props["paramName"]   = n->m_ParamName;
			props["fixedWeight"] = n->m_FixedWeight;
			props["useParam"]    = n->m_UseParam;
			break;
		}
		case VansAnimGraphNodeType::SpeedScale:
		{
			auto* n = static_cast<const AnimGraphSpeedScaleNode*>(node);
			props["paramName"]  = n->m_ParamName;
			props["fixedSpeed"] = n->m_FixedSpeed;
			props["useParam"]   = n->m_UseParam;
			break;
		}
		case VansAnimGraphNodeType::StateMachine:
		{
			auto* n = static_cast<const AnimGraphStateMachineNode*>(node);
			props["defaultState"] = n->m_DefaultStateName;

			nlohmann::json statesJson = nlohmann::json::array();
			for (const auto& s : n->m_States)
			{
				statesJson.push_back({
					{ "name", s.name },
					{ "clip", s.clipName },
					{ "poseNodeId", s.poseNodeId },
					{ "speed", s.speed },
					{ "speedParameter", s.speedParameter },
					{ "loop", s.loop },
					{ "rootMotion", s.rootMotion },
					{ "startTime", s.startTime },
					{ "endTime", s.endTime }
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
		case VansAnimGraphNodeType::MotionMatching:
		{
			auto* n = static_cast<const AnimGraphMotionMatchingNode*>(node);
			props["enableFallbackInput"] = n->m_EnableFallbackInput;
			break;
		}
		case VansAnimGraphNodeType::Slot:
		{
			auto* n = static_cast<const AnimGraphSlotNode*>(node);
			props["slotId"] = n->m_SlotId;
			props["enableFallbackInput"] = n->m_EnableFallbackInput;
			break;
		}
		case VansAnimGraphNodeType::SaveCachedPose:
			props["cacheName"] = static_cast<const AnimGraphSaveCachedPoseNode*>(node)->m_CacheName;
			break;
		case VansAnimGraphNodeType::UseCachedPose:
			props["cacheName"] = static_cast<const AnimGraphUseCachedPoseNode*>(node)->m_CacheName;
			break;
		case VansAnimGraphNodeType::LayeredBlendPerBone:
		{
			const auto* n = static_cast<const AnimGraphLayeredBlendPerBoneNode*>(node);
			props["blendMode"] = n->m_BlendMode == VansLayerBlendMode::Additive ? "additive" : "override";
			props["rotationSpace"] = n->m_RotationSpace == VansRotationBlendSpace::Mesh ? "mesh" : "local";
			props["weightParameter"] = n->m_WeightParameter;
			props["fixedWeight"] = n->m_FixedWeight;
			props["useWeightParameter"] = n->m_UseWeightParameter;
			props["applyAdditiveInput"] = n->m_ApplyAdditiveInput;
			props["mask"] = {
				{ "id", n->m_Mask.id }, { "name", n->m_Mask.name },
				{ "defaultWeight", n->m_Mask.defaultWeight },
				{ "branchRules", nlohmann::json::array() },
				{ "explicitWeights", n->m_Mask.explicitWeights }
			};
			for (const auto& rule : n->m_Mask.branchRules)
				props["mask"]["branchRules"].push_back({
					{ "id", rule.id },
					{ "mode", rule.mode == VansBoneMaskRuleMode::Exclude ? "exclude" : "include" },
					{ "rootBone", rule.rootBone }, { "includeDescendants", rule.includeDescendants },
					{ "maxDepth", rule.maxDepth }, { "rootWeight", rule.rootWeight },
					{ "endWeight", rule.endWeight },
					{ "falloff", rule.falloff == VansBoneMaskFalloff::SmoothStep ? "smoothStep" :
						rule.falloff == VansBoneMaskFalloff::Constant ? "constant" : "linear" }
				});
			break;
		}
		case VansAnimGraphNodeType::PoseCheckpoint:
		{
			const auto* n = static_cast<const AnimGraphPoseCheckpointNode*>(node);
			props = { { "checkpoint", n->m_CheckpointId }, { "bones", n->m_Bones } };
			break;
		}
		case VansAnimGraphNodeType::TargetPoseInput:
			break;
		case VansAnimGraphNodeType::Goal:
			props["goal"] = SerializeGoal(static_cast<const AnimGraphGoalNode*>(node)->m_Goal); break;
		case VansAnimGraphNodeType::AimConstraint:
		{
			const auto* n = static_cast<const AnimGraphAimConstraintNode*>(node);
			props = { { "chain", n->m_ChainId }, { "target", SerializeGoal(n->m_Target) },
				{ "mode", n->m_Settings.mode == VansAimConstraintMode::LookAtPoint ? "lookAtPoint" :
					n->m_Settings.mode == VansAimConstraintMode::LookAtDirection ? "lookAtDirection" : "pitchOffset" },
				{ "directionParameter", n->m_DirectionParameter },
				{ "directionWeightParameter", n->m_DirectionWeightParameter },
				{ "directionIsWorldSpace", n->m_DirectionIsWorldSpace },
				{ "pivotBone", n->m_PivotBone },
				{ "yawLimitDegrees", { n->m_Settings.yawLimitDegrees.x, n->m_Settings.yawLimitDegrees.y } },
				{ "pitchLimitDegrees", { n->m_Settings.pitchLimitDegrees.x, n->m_Settings.pitchLimitDegrees.y } },
				{ "targetHalfLife", n->m_TargetHalfLife },
				{ "maxAngularSpeedDegrees", n->m_Settings.maxAngularSpeedDegrees },
				{ "weight", n->m_Settings.weight } };
			break;
		}
		case VansAnimGraphNodeType::Grounding:
		{
			const auto& s = static_cast<const AnimGraphGroundingNode*>(node)->m_Settings;
			props = { { "contacts", s.contacts }, { "plantSignal", s.plantSignal }, { "weight", s.weight },
				{ "query", { { "profile", s.query.profile },
					{ "startDistanceAgainstApproach", s.query.startDistanceAgainstApproach },
					{ "endDistanceAlongApproach", s.query.endDistanceAlongApproach },
					{ "maxStepUp", s.query.maxStepUp }, { "maxStepDown", s.query.maxStepDown },
					{ "maxSlopeDegrees", s.query.maxSlopeDegrees },
					{ "maxPlaneResidual", s.query.maxPlaneResidual },
					{ "maxNormalDeviationDegrees", s.query.maxNormalDeviationDegrees } } },
				{ "plant", { { "lockEnabled", s.plant.lockEnabled },
					{ "enterPhase", s.plant.enterPhase }, { "exitPhase", s.plant.exitPhase },
					{ "unplantDistance", s.plant.unplantDistance }, { "replantDistance", s.plant.replantDistance },
					{ "unplantAngleDegrees", s.plant.unplantAngleDegrees },
					{ "replantAngleDegrees", s.plant.replantAngleDegrees },
					{ "pivot", PlantPivotToString(s.plant.pivot) },
					{ "weightHalfLife", s.plant.weightHalfLife } } },
				{ "alignment", { { "fullContactHeight", s.alignment.fullContactHeight },
					{ "contactFadeHeight", s.alignment.contactFadeHeight },
					{ "normalHalfLife", s.alignment.normalHalfLife },
					{ "rotationWeight", s.alignment.rotationWeight } } },
				{ "pelvis", { { "maxUpOffset", s.pelvis.maxUpOffset },
					{ "maxDownOffset", s.pelvis.maxDownOffset },
					{ "maxHorizontalOffset", s.pelvis.maxHorizontalOffset },
					{ "halfLife", s.pelvis.halfLife } } } };
			break;
		}
		case VansAnimGraphNodeType::LimbIK:
		{
			const auto* n = static_cast<const AnimGraphLimbIKNode*>(node);
			const char* rotationMode = nullptr;
			switch (n->m_Settings.tipRotationMode)
			{
			case VansLimbTipRotationMode::PreserveInput: rotationMode = "preserveInput"; break;
			case VansLimbTipRotationMode::MatchGoal: rotationMode = "matchGoal"; break;
			case VansLimbTipRotationMode::FollowChain: rotationMode = "followChain"; break;
			default: throw std::invalid_argument("Invalid Limb IK tip rotation enum");
			}
			props = { { "chains", n->m_ChainIds }, { "tipRotationMode", rotationMode },
				{ "positionTolerance", n->m_Settings.positionTolerance }, { "weight", n->m_Settings.weight },
				{ "commitClampedPose", n->m_Settings.commitClampedPose } };
			break;
		}
		case VansAnimGraphNodeType::RotationDistribution:
			props = {{"profile", static_cast<const AnimGraphRotationDistributionNode*>(node)->m_RotationProfileId}};
			break;
		case VansAnimGraphNodeType::ChainIK:
		{
			const auto* n = static_cast<const AnimGraphChainIKNode*>(node);
			props = { { "chains", n->m_ChainIds }, { "maxIterations", n->m_Settings.maxIterations },
				{ "positionTolerance", n->m_Settings.positionTolerance }, { "weight", n->m_Settings.weight },
				{ "commitClampedPose", n->m_Settings.commitClampedPose } };
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
		case VansAnimGraphNodeType::Entry:
		case VansAnimGraphNodeType::Output:
			RequireOnlyFields(props, {});
			break;
		case VansAnimGraphNodeType::Clip:
		{
			RequireOnlyFields(props, { "clipName", "speed", "loop", "rootMotion" });
			auto* n = static_cast<AnimGraphClipNode*>(node);
			if (props.contains("clipName")) n->m_ClipName = props["clipName"].get<std::string>();
			if (props.contains("speed"))    n->m_Speed    = props["speed"].get<float>();
			if (props.contains("loop"))     n->m_Loop     = props["loop"].get<bool>();
			if (props.contains("rootMotion")) n->m_RootMotion = props["rootMotion"].get<bool>();
			break;
		}
		case VansAnimGraphNodeType::Blend:
		{
			RequireOnlyFields(props, { "paramName", "fixedAlpha", "useParam" });
			auto* n = static_cast<AnimGraphBlendNode*>(node);
			if (props.contains("paramName"))  n->m_ParamName  = props["paramName"].get<std::string>();
			if (props.contains("fixedAlpha")) n->m_FixedAlpha = props["fixedAlpha"].get<float>();
			if (props.contains("useParam"))   n->m_UseParam   = props["useParam"].get<bool>();
			break;
		}
		case VansAnimGraphNodeType::Blend1D:
		{
			RequireOnlyFields(props, { "paramName", "thresholds" });
			auto* n = static_cast<AnimGraphBlend1DNode*>(node);
			if (props.contains("paramName"))  n->m_ParamName  = props["paramName"].get<std::string>();
			if (props.contains("thresholds")) n->m_Thresholds = props["thresholds"].get<std::vector<float>>();
			break;
		}
		case VansAnimGraphNodeType::BlendSpace2D:
		{
			auto* n = static_cast<AnimGraphBlendSpace2DNode*>(node);
			RequireOnlyFields(props, { "xParamName", "yParamName", "samples" });
			n->m_XParamName = props.at("xParamName").get<std::string>();
			n->m_YParamName = props.at("yParamName").get<std::string>();
			n->m_Samples.clear();
			for (const auto& sample : props.at("samples"))
			{
				RequireOnlyFields(sample, { "x", "y" });
				n->m_Samples.push_back({ sample.at("x").get<float>(), sample.at("y").get<float>() });
			}
			break;
		}
		case VansAnimGraphNodeType::IfCondition:
		{
			RequireOnlyFields(props, { "paramName", "op", "floatVal", "boolVal", "intVal" });
			auto* n = static_cast<AnimGraphIfConditionNode*>(node);
			if (props.contains("paramName")) n->m_ParamName = props["paramName"].get<std::string>();
			if (props.contains("op"))        n->m_CompareOp = StringToCompareOp(props["op"].get<std::string>());
			if (props.contains("floatVal"))  n->m_FloatVal  = props["floatVal"].get<float>();
			if (props.contains("boolVal"))   n->m_BoolVal   = props["boolVal"].get<bool>();
			if (props.contains("intVal"))    n->m_IntVal    = props["intVal"].get<int>();
			break;
		}
		case VansAnimGraphNodeType::Switch:
		{
			RequireOnlyFields(props, { "paramName", "caseCount" });
			auto* n = static_cast<AnimGraphSwitchNode*>(node);
			if (props.contains("paramName")) n->m_ParamName = props["paramName"].get<std::string>();
			if (props.contains("caseCount")) n->m_CaseCount = props["caseCount"].get<int>();
			break;
		}
		case VansAnimGraphNodeType::AdditiveBlend:
		{
			RequireOnlyFields(props, { "paramName", "fixedWeight", "useParam" });
			auto* n = static_cast<AnimGraphAdditiveBlendNode*>(node);
			if (props.contains("paramName"))   n->m_ParamName   = props["paramName"].get<std::string>();
			if (props.contains("fixedWeight")) n->m_FixedWeight = props["fixedWeight"].get<float>();
			if (props.contains("useParam"))    n->m_UseParam    = props["useParam"].get<bool>();
			break;
		}
		case VansAnimGraphNodeType::SpeedScale:
		{
			RequireOnlyFields(props, { "paramName", "fixedSpeed", "useParam" });
			auto* n = static_cast<AnimGraphSpeedScaleNode*>(node);
			if (props.contains("paramName"))  n->m_ParamName  = props["paramName"].get<std::string>();
			if (props.contains("fixedSpeed")) n->m_FixedSpeed = props["fixedSpeed"].get<float>();
			if (props.contains("useParam"))   n->m_UseParam   = props["useParam"].get<bool>();
			break;
		}
		case VansAnimGraphNodeType::StateMachine:
		{
			RequireOnlyFields(props, { "defaultState", "states", "transitions" });
			auto* n = static_cast<AnimGraphStateMachineNode*>(node);
			if (props.contains("defaultState"))
				n->m_DefaultStateName = props["defaultState"].get<std::string>();

			if (props.contains("states"))
			{
				for (const auto& sj : props["states"])
				{
					RequireOnlyFields(sj, { "name", "clip", "poseNodeId", "speed",
						"speedParameter", "loop", "rootMotion", "startTime", "endTime" });
					AnimatorState s;
					if (sj.contains("name"))       s.name       = sj["name"].get<std::string>();
					if (sj.contains("clip"))        s.clipName   = sj["clip"].get<std::string>();
					if (sj.contains("poseNodeId")) s.poseNodeId  = sj["poseNodeId"].get<int>();
					if (sj.contains("speed"))       s.speed      = sj["speed"].get<float>();
					if (sj.contains("speedParameter")) s.speedParameter = sj["speedParameter"].get<std::string>();
					if (sj.contains("loop"))        s.loop       = sj["loop"].get<bool>();
					if (sj.contains("rootMotion"))  s.rootMotion = sj["rootMotion"].get<bool>();
					if (sj.contains("startTime"))   s.startTime  = sj["startTime"].get<float>();
					if (sj.contains("endTime"))     s.endTime    = sj["endTime"].get<float>();
					n->m_States.push_back(s);
				}
			}

			if (props.contains("transitions"))
			{
				for (const auto& tj : props["transitions"])
				{
					RequireOnlyFields(tj, { "from", "to", "blendDuration", "hasExitTime",
						"exitTime", "conditions" });
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
							RequireOnlyFields(cj, { "param", "op", "floatVal", "boolVal", "intVal" });
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
		case VansAnimGraphNodeType::MotionMatching:
		{
			RequireOnlyFields(props, { "enableFallbackInput" });
			auto* n = static_cast<AnimGraphMotionMatchingNode*>(node);
			if (props.contains("enableFallbackInput"))
				n->m_EnableFallbackInput = props["enableFallbackInput"].get<bool>();
			break;
		}
		case VansAnimGraphNodeType::Slot:
		{
			RequireOnlyFields(props, { "slotId", "enableFallbackInput" });
			auto* n = static_cast<AnimGraphSlotNode*>(node);
			if (props.contains("slotId")) n->m_SlotId = props["slotId"].get<std::string>();
			if (props.contains("enableFallbackInput"))
				n->m_EnableFallbackInput = props["enableFallbackInput"].get<bool>();
			break;
		}
		case VansAnimGraphNodeType::SaveCachedPose:
		{
			RequireOnlyFields(props, { "cacheName" });
			auto* n = static_cast<AnimGraphSaveCachedPoseNode*>(node);
			if (props.contains("cacheName")) n->m_CacheName = props["cacheName"].get<std::string>();
			break;
		}
		case VansAnimGraphNodeType::UseCachedPose:
		{
			RequireOnlyFields(props, { "cacheName" });
			auto* n = static_cast<AnimGraphUseCachedPoseNode*>(node);
			if (props.contains("cacheName")) n->m_CacheName = props["cacheName"].get<std::string>();
			break;
		}
		case VansAnimGraphNodeType::LayeredBlendPerBone:
		{
			RequireOnlyFields(props, { "blendMode", "rotationSpace", "weightParameter",
				"fixedWeight", "useWeightParameter", "applyAdditiveInput", "mask" });
			auto* n = static_cast<AnimGraphLayeredBlendPerBoneNode*>(node);
			if (props.contains("blendMode"))
			{
				const std::string value = props["blendMode"].get<std::string>();
				if (value == "additive") n->m_BlendMode = VansLayerBlendMode::Additive;
				else if (value == "override") n->m_BlendMode = VansLayerBlendMode::Override;
				else throw std::invalid_argument("Unknown layered blend mode: " + value);
			}
			if (props.contains("rotationSpace"))
			{
				const std::string value = props["rotationSpace"].get<std::string>();
				if (value == "local") n->m_RotationSpace = VansRotationBlendSpace::Local;
				else if (value == "mesh") n->m_RotationSpace = VansRotationBlendSpace::Mesh;
				else throw std::invalid_argument("Unknown layered blend rotation space: " + value);
			}
			if (props.contains("weightParameter")) n->m_WeightParameter = props["weightParameter"].get<std::string>();
			if (props.contains("fixedWeight")) n->m_FixedWeight = props["fixedWeight"].get<float>();
			if (props.contains("useWeightParameter")) n->m_UseWeightParameter = props["useWeightParameter"].get<bool>();
			if (props.contains("applyAdditiveInput")) n->m_ApplyAdditiveInput = props["applyAdditiveInput"].get<bool>();
			if (props.contains("mask"))
			{
				const auto& mask = props["mask"];
				RequireOnlyFields(mask, { "id", "name", "defaultWeight", "branchRules", "explicitWeights" });
				n->m_Mask.id = mask.value("id", "inline-layer-mask");
				n->m_Mask.name = mask.value("name", "Inline Layer Mask");
				n->m_Mask.defaultWeight = mask.value("defaultWeight", 0.0f);
				n->m_Mask.explicitWeights = mask.value("explicitWeights", std::unordered_map<std::string, float>{});
				if (mask.contains("branchRules"))
					for (const auto& r : mask["branchRules"])
					{
						RequireOnlyFields(r, { "id", "mode", "rootBone", "includeDescendants",
							"maxDepth", "rootWeight", "endWeight", "falloff" });
						VansBoneMaskBranchRule rule;
						rule.id = r.value("id", "inline-rule");
						const std::string mode = r.value("mode", "include");
						if (mode == "exclude") rule.mode = VansBoneMaskRuleMode::Exclude;
						else if (mode == "include") rule.mode = VansBoneMaskRuleMode::Include;
						else throw std::invalid_argument("Unknown inline Bone Mask rule mode: " + mode);
						rule.rootBone = r.value("rootBone", "");
						rule.includeDescendants = r.value("includeDescendants", true);
						rule.maxDepth = r.value("maxDepth", -1);
						rule.rootWeight = r.value("rootWeight", 1.0f);
						rule.endWeight = r.value("endWeight", 1.0f);
						const std::string falloff = r.value("falloff", "constant");
						if (falloff == "smoothStep") rule.falloff = VansBoneMaskFalloff::SmoothStep;
						else if (falloff == "linear") rule.falloff = VansBoneMaskFalloff::Linear;
						else if (falloff == "constant") rule.falloff = VansBoneMaskFalloff::Constant;
						else throw std::invalid_argument("Unknown inline Bone Mask falloff: " + falloff);
						n->m_Mask.branchRules.push_back(std::move(rule));
					}
			}
			break;
		}
		case VansAnimGraphNodeType::PoseCheckpoint:
		{
			RequireOnlyFields(props, { "checkpoint", "bones" });
			auto* n = static_cast<AnimGraphPoseCheckpointNode*>(node);
			n->m_CheckpointId = props.at("checkpoint").get<std::string>();
			n->m_Bones = props.at("bones").get<std::vector<std::string>>();
			break;
		}
		case VansAnimGraphNodeType::TargetPoseInput:
			RequireOnlyFields(props, {});
			break;
		case VansAnimGraphNodeType::Goal:
			RequireOnlyFields(props, { "goal" });
			DeserializeGoal(props.at("goal"), static_cast<AnimGraphGoalNode*>(node)->m_Goal); break;
		case VansAnimGraphNodeType::AimConstraint:
		{
			RequireOnlyFields(props, { "chain", "target", "yawLimitDegrees",
				"pitchLimitDegrees", "targetHalfLife", "maxAngularSpeedDegrees", "weight",
				"mode", "directionParameter", "directionWeightParameter", "directionIsWorldSpace", "pivotBone" });
			auto* n = static_cast<AnimGraphAimConstraintNode*>(node);
			const auto mode = props.at("mode").get<std::string>();
			if (mode == "lookAtPoint") n->m_Settings.mode = VansAimConstraintMode::LookAtPoint;
			else if (mode == "lookAtDirection") n->m_Settings.mode = VansAimConstraintMode::LookAtDirection;
			else if (mode == "pitchOffset") n->m_Settings.mode = VansAimConstraintMode::PitchOffset;
			else throw std::invalid_argument("Invalid Aim Constraint mode");
			n->m_DirectionParameter = props.at("directionParameter").get<std::string>();
			n->m_DirectionWeightParameter = props.at("directionWeightParameter").get<std::string>();
			n->m_DirectionIsWorldSpace = props.at("directionIsWorldSpace").get<bool>();
			n->m_PivotBone = props.at("pivotBone").get<std::string>();
			n->m_ChainId = props.at("chain").get<std::string>();
			DeserializeGoal(props.at("target"), n->m_Target);
			const auto& yaw = props.at("yawLimitDegrees");
			const auto& pitch = props.at("pitchLimitDegrees");
			n->m_Settings.yawLimitDegrees = { yaw.at(0).get<float>(), yaw.at(1).get<float>() };
			n->m_Settings.pitchLimitDegrees = { pitch.at(0).get<float>(), pitch.at(1).get<float>() };
			n->m_TargetHalfLife = props.at("targetHalfLife").get<float>();
			n->m_Settings.maxAngularSpeedDegrees = props.at("maxAngularSpeedDegrees").get<float>();
			n->m_Settings.weight = props.at("weight").get<float>();
			break;
		}
		case VansAnimGraphNodeType::Grounding:
		{
			RequireOnlyFields(props, { "contacts", "plantSignal", "weight", "query", "plant", "alignment", "pelvis" });
			auto& s = static_cast<AnimGraphGroundingNode*>(node)->m_Settings;
			s.contacts = props.at("contacts").get<std::vector<std::string>>();
			s.plantSignal = props.at("plantSignal").get<std::string>();
			s.weight = props.at("weight").get<float>();
			const auto& query = props.at("query");
			RequireOnlyFields(query, { "profile", "startDistanceAgainstApproach",
				"endDistanceAlongApproach", "maxStepUp", "maxStepDown", "maxSlopeDegrees",
				"maxPlaneResidual", "maxNormalDeviationDegrees" });
			s.query.profile = query.at("profile").get<std::string>();
			s.query.startDistanceAgainstApproach = query.at("startDistanceAgainstApproach").get<float>();
			s.query.endDistanceAlongApproach = query.at("endDistanceAlongApproach").get<float>();
			s.query.maxStepUp = query.at("maxStepUp").get<float>();
			s.query.maxStepDown = query.at("maxStepDown").get<float>();
			s.query.maxSlopeDegrees = query.at("maxSlopeDegrees").get<float>();
			s.query.maxPlaneResidual = query.at("maxPlaneResidual").get<float>();
			s.query.maxNormalDeviationDegrees = query.at("maxNormalDeviationDegrees").get<float>();
			const auto& plant = props.at("plant");
			RequireOnlyFields(plant, { "lockEnabled", "enterPhase", "exitPhase",
				"unplantDistance", "replantDistance", "unplantAngleDegrees",
				"replantAngleDegrees", "pivot", "weightHalfLife" });
			s.plant.lockEnabled = plant.at("lockEnabled").get<bool>();
			s.plant.enterPhase = plant.at("enterPhase").get<float>();
			s.plant.exitPhase = plant.at("exitPhase").get<float>();
			s.plant.unplantDistance = plant.at("unplantDistance").get<float>();
			s.plant.replantDistance = plant.at("replantDistance").get<float>();
			s.plant.unplantAngleDegrees = plant.at("unplantAngleDegrees").get<float>();
			s.plant.replantAngleDegrees = plant.at("replantAngleDegrees").get<float>();
			s.plant.pivot = StringToPlantPivot(plant.at("pivot").get<std::string>());
			s.plant.weightHalfLife = plant.at("weightHalfLife").get<float>();
			const auto& alignment = props.at("alignment");
			RequireOnlyFields(alignment, { "fullContactHeight", "contactFadeHeight",
				"normalHalfLife", "rotationWeight" });
			s.alignment.fullContactHeight = alignment.at("fullContactHeight").get<float>();
			s.alignment.contactFadeHeight = alignment.at("contactFadeHeight").get<float>();
			s.alignment.normalHalfLife = alignment.at("normalHalfLife").get<float>();
			s.alignment.rotationWeight = alignment.at("rotationWeight").get<float>();
			const auto& pelvis = props.at("pelvis");
			RequireOnlyFields(pelvis, { "maxUpOffset", "maxDownOffset", "maxHorizontalOffset", "halfLife" });
			s.pelvis.maxUpOffset = pelvis.at("maxUpOffset").get<float>();
			s.pelvis.maxDownOffset = pelvis.at("maxDownOffset").get<float>();
			s.pelvis.maxHorizontalOffset = pelvis.at("maxHorizontalOffset").get<float>();
			s.pelvis.halfLife = pelvis.at("halfLife").get<float>();
			break;
		}
		case VansAnimGraphNodeType::LimbIK:
		{
			RequireOnlyFields(props, { "chains", "tipRotationMode", "positionTolerance",
				"weight", "commitClampedPose" });
			auto* n = static_cast<AnimGraphLimbIKNode*>(node);
			n->m_ChainIds = props.at("chains").get<std::vector<std::string>>();
			const std::string mode = props.at("tipRotationMode").get<std::string>();
			if (mode == "preserveInput")
				n->m_Settings.tipRotationMode = VansLimbTipRotationMode::PreserveInput;
			else if (mode == "followChain")
				n->m_Settings.tipRotationMode = VansLimbTipRotationMode::FollowChain;
			else if (mode == "matchGoal")
				n->m_Settings.tipRotationMode = VansLimbTipRotationMode::MatchGoal;
			else
				throw std::invalid_argument("Unknown Limb IK tip rotation mode: " + mode);
			n->m_Settings.positionTolerance = props.at("positionTolerance").get<float>();
			n->m_Settings.weight = props.at("weight").get<float>();
			n->m_Settings.commitClampedPose = props.at("commitClampedPose").get<bool>();
			break;
		}
		case VansAnimGraphNodeType::RotationDistribution:
			RequireOnlyFields(props, {"profile"});
			static_cast<AnimGraphRotationDistributionNode*>(node)->m_RotationProfileId = props.at("profile").get<std::string>();
			break;
		case VansAnimGraphNodeType::ChainIK:
		{
			RequireOnlyFields(props, { "chains", "maxIterations", "positionTolerance",
				"weight", "commitClampedPose" });
			auto* n = static_cast<AnimGraphChainIKNode*>(node);
			n->m_ChainIds = props.at("chains").get<std::vector<std::string>>();
			n->m_Settings.maxIterations = props.at("maxIterations").get<int>();
			n->m_Settings.positionTolerance = props.at("positionTolerance").get<float>();
			n->m_Settings.weight = props.at("weight").get<float>();
			n->m_Settings.commitClampedPose = props.at("commitClampedPose").get<bool>();
			break;
		}
		default:
			throw std::invalid_argument("Unknown animation graph node type");
		}
	}

	void VansAnimGraph::SerializeToJsonObject(nlohmann::json& outJson) const
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
			nj["posX"]     = node->m_EditorLayout.x;
			nj["posY"]     = node->m_EditorLayout.y;
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

	std::unique_ptr<VansAnimGraph> VansAnimGraph::DeserializeFromJsonObject(const nlohmann::json& j)
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
				RequireOnlyFields(nj, { "id", "type", "name", "posX", "posY", "properties" });

				const int nodeId = nj["id"].get<int>();
				if (nodeId <= 0 || !nodeIds.insert(nodeId).second)
					return nullptr;

				auto node = CreateNodeByTypeName(nj["type"].get<std::string>());
				if (!node)
					return nullptr;
				node->m_NodeId = nodeId;
				if (nj.contains("name")) node->SetName(nj["name"].get<std::string>());
				if (nj.contains("posX")) node->m_EditorLayout.x = nj["posX"].get<float>();
				if (nj.contains("posY")) node->m_EditorLayout.y = nj["posY"].get<float>();
				DeserializeNodeProperties(node.get(), nj["properties"]);

				if (node->GetType() == VansAnimGraphNodeType::Entry)
				{
					++entryCount;
					graph->m_EntryNodeId = nodeId;
				}
				else if (node->GetType() == VansAnimGraphNodeType::Output)
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
				RequireOnlyFields(lj, { "id", "fromNode", "fromPin", "toNode", "toPin" });

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
		catch (const std::exception&)
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

	// 从 ctx.parameters 读取 Vector3 参数
	AnimGraphMotionMatchingNode::AnimGraphMotionMatchingNode()
	{
		m_Type = VansAnimGraphNodeType::MotionMatching;
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
			if (ctx.motionMatching->Evaluate(ctx.deltaTime,
			                               *ctx.skeleton,
			                               *ctx.clips,
			                               *ctx.parameters,
			                               ctx.characterTrajectory,
			                               pose))
			{
				pose.valid = pose.localPose.size() == ctx.skeleton->bones.size();
				if (pose.valid)
					return pose;
			}
		}

		if (!m_EnableFallbackInput)
			return {};

		return EvaluateInputPose(instance, m_NodeId, 0, ctx);
	}

	AnimGraphSlotNode::AnimGraphSlotNode()
	{
		m_Type = VansAnimGraphNodeType::Slot;
		m_Name = "Slot";
	}

	AnimGraphSaveCachedPoseNode::AnimGraphSaveCachedPoseNode()
	{
		m_Type = VansAnimGraphNodeType::SaveCachedPose;
		m_Name = "Save Cached Pose";
	}

	std::vector<AnimGraphPin> AnimGraphSaveCachedPoseNode::GetPins() const
	{
		return {
			{ 0, "InputPose", AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphSaveCachedPoseNode::Evaluate(
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		AnimGraphPose pose = EvaluateInputPose(instance, GetNodeId(), 0, ctx);
		if (!m_CacheName.empty())
			StoreCachedPose(instance, m_CacheName, pose);
		return pose;
	}

	AnimGraphUseCachedPoseNode::AnimGraphUseCachedPoseNode()
	{
		m_Type = VansAnimGraphNodeType::UseCachedPose;
		m_Name = "Use Cached Pose";
	}

	std::vector<AnimGraphPin> AnimGraphUseCachedPoseNode::GetPins() const
	{
		return { { 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output } };
	}

	AnimGraphPose AnimGraphUseCachedPoseNode::Evaluate(
		const AnimGraphContext&, VansAnimGraphInstance& instance) const
	{
		const AnimGraphPose* pose = ResolveCachedPose(instance, m_CacheName);
		return pose ? *pose : AnimGraphPose{};
	}

	AnimGraphLayeredBlendPerBoneNode::AnimGraphLayeredBlendPerBoneNode()
	{
		m_Type = VansAnimGraphNodeType::LayeredBlendPerBone;
		m_Name = "Layered Blend Per Bone";
		m_Mask.id = "inline-layer-mask";
		m_Mask.defaultWeight = 0.0f;
	}

	std::vector<AnimGraphPin> AnimGraphLayeredBlendPerBoneNode::GetPins() const
	{
		return {
			{ 0, "BasePose", AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 1, "OverlayPose", AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 2, "AdditivePoseMS", AnimGraphPinType::Pose, AnimGraphPinKind::Input },
			{ 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
		};
	}

	AnimGraphPose AnimGraphLayeredBlendPerBoneNode::Evaluate(
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		AnimGraphPose base = EvaluateInputPose(instance, GetNodeId(), 0, ctx);
		AnimGraphPose overlay = EvaluateInputPose(instance, GetNodeId(), 1, ctx);
		if (!base.valid || !overlay.valid || !ctx.skeleton)
			return base;
		const float weight = m_UseWeightParameter && ctx.parameters
			? [&]() { auto it = ctx.parameters->find(m_WeightParameter);
				return it != ctx.parameters->end() && it->second.type == AnimatorParamType::Float
					? it->second.floatVal : 0.0f; }()
			: m_FixedWeight;
		const auto& runtime = instance.ResolveLayeredBlendRuntime(
			GetNodeId(), m_Mask, *ctx.skeleton);
		VansAnimationLayerDefinition definition;
		definition.id = "graph-layered-blend-per-bone";
		definition.kind = VansAnimationLayerKind::Overlay;
		definition.blendMode = m_BlendMode;
		definition.rotationSpace = m_RotationSpace;
		definition.rootMotion = VansLayerRootMotionMode::Ignore;
		AnimGraphPose result = VansAnimationLayerMixer::ApplyLayer(
			base, overlay, definition, runtime.mask, *ctx.skeleton, runtime.bindPose, weight);
		if (m_ApplyAdditiveInput)
		{
			AnimGraphPose additive = EvaluateInputPose(instance, GetNodeId(), 2, ctx);
			if (additive.valid)
			{
				VansAnimationLayerDefinition additiveDefinition = definition;
				additiveDefinition.blendMode = VansLayerBlendMode::Additive;
				additiveDefinition.rotationSpace = VansRotationBlendSpace::Mesh;
				result = VansAnimationLayerMixer::ApplyLayer(
					result, additive, additiveDefinition, runtime.mask,
					*ctx.skeleton, runtime.bindPose, weight);
			}
		}
		return result;
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
			fallback = EvaluateInputPose(instance, m_NodeId, 0, ctx);
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
		m_Type = VansAnimGraphNodeType::TargetPoseInput;
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


	namespace
	{
		std::vector<AnimGraphPin> ProceduralPosePins()
		{
			return {
				{ 0, "InPose", AnimGraphPinType::Pose, AnimGraphPinKind::Input },
				{ 0, "OutPose", AnimGraphPinType::Pose, AnimGraphPinKind::Output }
			};
		}

	}

	AnimGraphPoseCheckpointNode::AnimGraphPoseCheckpointNode()
	{
		m_Type = VansAnimGraphNodeType::PoseCheckpoint;
		m_Name = "Pose Checkpoint";
	}
	std::vector<AnimGraphPin> AnimGraphPoseCheckpointNode::GetPins() const { return ProceduralPosePins(); }
	AnimGraphPose AnimGraphPoseCheckpointNode::Evaluate(
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		return AppendProceduralPose(instance, m_NodeId, ctx);
	}

	AnimGraphGoalNode::AnimGraphGoalNode()
	{
		m_Type = VansAnimGraphNodeType::Goal;
		m_Name = "Goal";
	}

	std::vector<AnimGraphPin> AnimGraphGoalNode::GetPins() const { return ProceduralPosePins(); }

	AnimGraphPose AnimGraphGoalNode::Evaluate(
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		return AppendProceduralPose(instance, m_NodeId, ctx);
	}

	AnimGraphAimConstraintNode::AnimGraphAimConstraintNode()
	{
		m_Type = VansAnimGraphNodeType::AimConstraint;
		m_Name = "Aim Constraint";
	}

	std::vector<AnimGraphPin> AnimGraphAimConstraintNode::GetPins() const { return ProceduralPosePins(); }

	AnimGraphPose AnimGraphAimConstraintNode::Evaluate(
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		return AppendProceduralPose(instance, m_NodeId, ctx);
	}

	AnimGraphGroundingNode::AnimGraphGroundingNode()
	{
		m_Type = VansAnimGraphNodeType::Grounding;
		m_Name = "Grounding";
	}

	std::vector<AnimGraphPin> AnimGraphGroundingNode::GetPins() const { return ProceduralPosePins(); }

	AnimGraphPose AnimGraphGroundingNode::Evaluate(
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		return AppendProceduralPose(instance, m_NodeId, ctx);
	}

	AnimGraphLimbIKNode::AnimGraphLimbIKNode()
	{
		m_Type = VansAnimGraphNodeType::LimbIK;
		m_Name = "Limb IK";
	}

	std::vector<AnimGraphPin> AnimGraphLimbIKNode::GetPins() const { return ProceduralPosePins(); }

	AnimGraphPose AnimGraphLimbIKNode::Evaluate(
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		return AppendProceduralPose(instance, m_NodeId, ctx);
	}

	AnimGraphRotationDistributionNode::AnimGraphRotationDistributionNode()
	{
		m_Type = VansAnimGraphNodeType::RotationDistribution;
		m_Name = "Rotation Distribution";
	}

	std::vector<AnimGraphPin> AnimGraphRotationDistributionNode::GetPins() const { return ProceduralPosePins(); }
	AnimGraphPose AnimGraphRotationDistributionNode::Evaluate(
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		return AppendProceduralPose(instance, m_NodeId, ctx);
	}

	AnimGraphChainIKNode::AnimGraphChainIKNode()
	{
		m_Type = VansAnimGraphNodeType::ChainIK;
		m_Name = "Chain IK";
	}

	std::vector<AnimGraphPin> AnimGraphChainIKNode::GetPins() const { return ProceduralPosePins(); }

	AnimGraphPose AnimGraphChainIKNode::Evaluate(
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		return AppendProceduralPose(instance, m_NodeId, ctx);
	}

}  // namespace VansGraphics
