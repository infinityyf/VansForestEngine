#include "VansAnimGraph.h"
#include "VansAnimationController.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace VansGraphics
{
	// ═════════════════════════════════════════════════════════════
	//  工具函数
	// ═════════════════════════════════════════════════════════════

	// Pose 线性混合辅助（对每根骨骼的 4x4 矩阵做分量 lerp）
	static AnimGraphPose BlendPoses(const AnimGraphPose& a, const AnimGraphPose& b, float alpha)
	{
		AnimGraphPose result;
		if (!a.valid || !b.valid) return a.valid ? a : b;

		size_t count = std::min(a.localTransforms.size(), b.localTransforms.size());
		result.localTransforms.resize(count);

		float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
		for (size_t i = 0; i < count; ++i)
		{
			// 对每个分量做线性插值
			for (int col = 0; col < 4; ++col)
				for (int row = 0; row < 4; ++row)
					result.localTransforms[i][col][row] =
						a.localTransforms[i][col][row] * (1.0f - clampedAlpha) +
						b.localTransforms[i][col][row] * clampedAlpha;
		}
		result.valid = true;
		return result;
	}

	// Pose 叠加混合辅助：result = base + (additive - identity) * weight
	static AnimGraphPose AdditivePoses(const AnimGraphPose& base, const AnimGraphPose& additive, float weight)
	{
		AnimGraphPose result;
		if (!base.valid) return base;
		if (!additive.valid) return base;

		size_t count = std::min(base.localTransforms.size(), additive.localTransforms.size());
		result.localTransforms.resize(count);

		glm::mat4 identity(1.0f);
		float w = std::clamp(weight, 0.0f, 1.0f);

		for (size_t i = 0; i < count; ++i)
		{
			// additiveDelta = additive - identity
			// result = base + additiveDelta * weight
			for (int col = 0; col < 4; ++col)
				for (int row = 0; row < 4; ++row)
				{
					float addDelta = additive.localTransforms[i][col][row] - identity[col][row];
					result.localTransforms[i][col][row] =
						base.localTransforms[i][col][row] + addDelta * w;
				}
		}
		result.valid = true;
		return result;
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
		}
		return "Unknown";
	}

	AnimGraphNodeType VansAnimGraphNode::StringToType(const std::string& str)
	{
		if (str == "Entry")          return AnimGraphNodeType::Entry;
		if (str == "Output")         return AnimGraphNodeType::Output;
		if (str == "Clip")           return AnimGraphNodeType::Clip;
		if (str == "Blend")          return AnimGraphNodeType::Blend;
		if (str == "Blend1D")        return AnimGraphNodeType::Blend1D;
		if (str == "IfCondition")    return AnimGraphNodeType::IfCondition;
		if (str == "Switch")         return AnimGraphNodeType::Switch;
		if (str == "AdditiveBlend")  return AnimGraphNodeType::AdditiveBlend;
		if (str == "SpeedScale")     return AnimGraphNodeType::SpeedScale;
		if (str == "StateMachine")   return AnimGraphNodeType::StateMachine;
		return AnimGraphNodeType::Entry;  // fallback
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
	                                           VansAnimGraph& graph)
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
	                                            VansAnimGraph& graph)
	{
		// 拉取输入连接的 Pose
		VansAnimGraphNode* inputNode = graph.GetInputNode(m_NodeId, 0);
		if (inputNode)
			return inputNode->Evaluate(ctx, graph);
		return {};
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

	void AnimGraphClipNode::AdvanceTime(float dt)
	{
		m_CurrentTime += dt * m_Speed;
	}

	void AnimGraphClipNode::Reset()
	{
		m_CurrentTime = 0.0f;
	}

	AnimGraphPose AnimGraphClipNode::Evaluate(const AnimGraphContext& ctx,
	                                          VansAnimGraph& graph)
	{
		AnimGraphPose pose;
		if (!ctx.clips || !ctx.skeleton) return pose;

		// 查找 Clip
		auto it = ctx.clips->find(m_ClipName);
		if (it == ctx.clips->end()) return pose;

		const VansAnimationClip& clip = it->second;
		const Skeleton& skel = *ctx.skeleton;

		// 处理 loop / clamp
		float duration = clip.duration;
		if (duration <= 0.0f) return pose;

		float sampleTime = m_CurrentTime;
		if (m_Loop)
		{
			sampleTime = std::fmod(sampleTime, duration);
			if (sampleTime < 0.0f) sampleTime += duration;
		}
		else
		{
			sampleTime = std::clamp(sampleTime, 0.0f, duration);
		}

		// 采样每根骨骼的关键帧
		size_t boneCount = skel.bones.size();
		pose.localTransforms.resize(boneCount, glm::mat4(1.0f));

		for (size_t bi = 0; bi < boneCount && bi < clip.boneKeyframes.size(); ++bi)
		{
			const auto& keyframes = clip.boneKeyframes[bi];
			if (keyframes.empty())
			{
				pose.localTransforms[bi] = glm::mat4(1.0f);
				continue;
			}

			glm::vec3 pos, scl;
			glm::quat rot;
			InterpolateKeyframes(keyframes, sampleTime, pos, rot, scl);

			glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
			glm::mat4 R = glm::toMat4(rot);
			glm::mat4 S = glm::scale(glm::mat4(1.0f), scl);
			pose.localTransforms[bi] = T * R * S;
		}

		pose.valid = true;
		return pose;
	}

	void AnimGraphClipNode::InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
	                                             float time,
	                                             glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale)
	{
		if (keyframes.size() == 1)
		{
			outPos   = keyframes[0].position;
			outRot   = keyframes[0].rotation;
			outScale = keyframes[0].scale;
			return;
		}

		// 查找 time 所在的两个关键帧之间
		size_t idx = 0;
		for (size_t i = 0; i < keyframes.size() - 1; ++i)
		{
			if (time < keyframes[i + 1].time)
			{
				idx = i;
				break;
			}
			idx = i;
		}

		size_t next = std::min(idx + 1, keyframes.size() - 1);
		if (idx == next)
		{
			outPos   = keyframes[idx].position;
			outRot   = keyframes[idx].rotation;
			outScale = keyframes[idx].scale;
			return;
		}

		float dt = keyframes[next].time - keyframes[idx].time;
		float factor = (dt > 0.0001f) ? (time - keyframes[idx].time) / dt : 0.0f;
		factor = std::clamp(factor, 0.0f, 1.0f);

		outPos   = glm::mix(keyframes[idx].position, keyframes[next].position, factor);
		outRot   = glm::slerp(keyframes[idx].rotation, keyframes[next].rotation, factor);
		outScale = glm::mix(keyframes[idx].scale, keyframes[next].scale, factor);
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
	                                           VansAnimGraph& graph)
	{
		// 拉取两路输入
		VansAnimGraphNode* nodeA = graph.GetInputNode(m_NodeId, 0);
		VansAnimGraphNode* nodeB = graph.GetInputNode(m_NodeId, 1);

		AnimGraphPose poseA, poseB;
		if (nodeA) poseA = nodeA->Evaluate(ctx, graph);
		if (nodeB) poseB = nodeB->Evaluate(ctx, graph);

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

		return BlendPoses(poseA, poseB, alpha);
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
	                                             VansAnimGraph& graph)
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
			VansAnimGraphNode* node = graph.GetInputNode(m_NodeId, 0);
			return node ? node->Evaluate(ctx, graph) : AnimGraphPose{};
		}

		// 找到 paramValue 落在哪两个阈值之间
		if (paramValue <= m_Thresholds.front())
		{
			VansAnimGraphNode* node = graph.GetInputNode(m_NodeId, 0);
			return node ? node->Evaluate(ctx, graph) : AnimGraphPose{};
		}
		if (paramValue >= m_Thresholds.back())
		{
			VansAnimGraphNode* node = graph.GetInputNode(m_NodeId, count - 1);
			return node ? node->Evaluate(ctx, graph) : AnimGraphPose{};
		}

		for (int i = 0; i < count - 1; ++i)
		{
			if (paramValue >= m_Thresholds[i] && paramValue <= m_Thresholds[i + 1])
			{
				float range = m_Thresholds[i + 1] - m_Thresholds[i];
				float alpha = (range > 0.0001f)
					? (paramValue - m_Thresholds[i]) / range
					: 0.0f;

				VansAnimGraphNode* nodeA = graph.GetInputNode(m_NodeId, i);
				VansAnimGraphNode* nodeB = graph.GetInputNode(m_NodeId, i + 1);

				AnimGraphPose poseA = nodeA ? nodeA->Evaluate(ctx, graph) : AnimGraphPose{};
				AnimGraphPose poseB = nodeB ? nodeB->Evaluate(ctx, graph) : AnimGraphPose{};

				if (!poseA.valid) return poseB;
				if (!poseB.valid) return poseA;

				return BlendPoses(poseA, poseB, alpha);
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
	                                                 VansAnimGraph& graph)
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
		VansAnimGraphNode* inputNode = graph.GetInputNode(m_NodeId, pinIndex);
		return inputNode ? inputNode->Evaluate(ctx, graph) : AnimGraphPose{};
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
	                                            VansAnimGraph& graph)
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

		VansAnimGraphNode* inputNode = graph.GetInputNode(m_NodeId, selectedCase);
		return inputNode ? inputNode->Evaluate(ctx, graph) : AnimGraphPose{};
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
	                                                   VansAnimGraph& graph)
	{
		VansAnimGraphNode* baseNode     = graph.GetInputNode(m_NodeId, 0);
		VansAnimGraphNode* additiveNode = graph.GetInputNode(m_NodeId, 1);

		AnimGraphPose basePose     = baseNode     ? baseNode->Evaluate(ctx, graph)     : AnimGraphPose{};
		AnimGraphPose additivePose = additiveNode ? additiveNode->Evaluate(ctx, graph) : AnimGraphPose{};

		if (!basePose.valid) return basePose;
		if (!additivePose.valid) return basePose;

		float weight = m_FixedWeight;
		if (m_UseParam && ctx.parameters)
		{
			auto it = ctx.parameters->find(m_ParamName);
			if (it != ctx.parameters->end() && it->second.type == AnimatorParamType::Float)
				weight = it->second.floatVal;
		}

		return AdditivePoses(basePose, additivePose, weight);
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
	                                                VansAnimGraph& graph)
	{
		// SpeedScale 节点的效果在 AdvanceTime 阶段已经作用于下游 ClipNode。
		// Evaluate 阶段直接透传输入 Pose。
		VansAnimGraphNode* inputNode = graph.GetInputNode(m_NodeId, 0);
		return inputNode ? inputNode->Evaluate(ctx, graph) : AnimGraphPose{};
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

	void AnimGraphStateMachineNode::AdvanceTime(float dt)
	{
		// 推进当前状态的播放时间
		AnimatorState* current = GetState(m_CurrentStateName);
		if (current)
		{
			current->currentTime += dt * current->speed;
			if (current->loop && current->clip && current->clip->duration > 0.0f)
				current->currentTime = std::fmod(current->currentTime, current->clip->duration);
		}

		// 如果正在混合，也推进前一个状态
		if (m_BlendState == ControllerBlendState::Blending)
		{
			AnimatorState* prev = GetState(m_PrevStateName);
			if (prev)
			{
				prev->currentTime += dt * prev->speed;
				if (prev->loop && prev->clip && prev->clip->duration > 0.0f)
					prev->currentTime = std::fmod(prev->currentTime, prev->clip->duration);
			}
		}
	}

	void AnimGraphStateMachineNode::Reset()
	{
		m_CurrentStateName = m_DefaultStateName;
		m_PrevStateName.clear();
		m_BlendAlpha = 0.0f;
		m_BlendState = ControllerBlendState::Idle;

		for (auto& state : m_States)
			state.currentTime = 0.0f;
	}

	AnimGraphPose AnimGraphStateMachineNode::Evaluate(const AnimGraphContext& ctx,
	                                                  VansAnimGraph& graph)
	{
		// 绑定 clip 指针（延迟绑定）
		if (ctx.clips)
		{
			for (auto& state : m_States)
			{
				if (!state.clip)
				{
					auto it = ctx.clips->find(state.clipName);
					if (it != ctx.clips->end())
						state.clip = const_cast<VansAnimationClip*>(&it->second);
				}
			}
		}

		// 初始化
		if (m_CurrentStateName.empty())
		{
			m_CurrentStateName = m_DefaultStateName;
			AnimatorState* s = GetState(m_CurrentStateName);
			if (s) s->currentTime = 0.0f;
		}

		// 求值 Transition
		EvaluateTransitions(ctx);

		// 采样当前状态的 Pose
		AnimatorState* current = GetState(m_CurrentStateName);
		if (!current) return {};

		AnimGraphPose currentPose = ComputeStatePose(*current, ctx);

		// 如果正在混合
		if (m_BlendState == ControllerBlendState::Blending)
		{
			AnimatorState* prev = GetState(m_PrevStateName);
			if (prev)
			{
				AnimGraphPose prevPose = ComputeStatePose(*prev, ctx);
				m_BlendAlpha += ctx.deltaTime / m_BlendDuration;
				if (m_BlendAlpha >= 1.0f)
				{
					m_BlendAlpha = 1.0f;
					m_BlendState = ControllerBlendState::Idle;
				}
				return BlendPoses(prevPose, currentPose, m_BlendAlpha);
			}
		}

		return currentPose;
	}

	void AnimGraphStateMachineNode::EvaluateTransitions(const AnimGraphContext& ctx)
	{
		// 检查 AnyState 过渡
		for (const auto& trans : m_Transitions)
		{
			if (trans.fromState == "*" && trans.toState != m_CurrentStateName)
			{
				if (CheckConditions(trans, ctx))
				{
					StartTransition(trans);
					return;
				}
			}
		}

		// 检查当前状态的出边
		for (const auto& trans : m_Transitions)
		{
			if (trans.fromState == m_CurrentStateName)
			{
				// exitTime 检查
				if (trans.hasExitTime)
				{
					AnimatorState* current = GetState(m_CurrentStateName);
					if (current && current->clip && current->clip->duration > 0.0f)
					{
						float normalizedTime = current->currentTime / current->clip->duration;
						if (normalizedTime < trans.exitTime)
							continue;
					}
				}

				if (CheckConditions(trans, ctx))
				{
					StartTransition(trans);
					return;
				}
			}
		}
	}

	bool AnimGraphStateMachineNode::CheckConditions(const AnimatorTransition& trans,
	                                                const AnimGraphContext& ctx) const
	{
		if (!ctx.parameters) return trans.conditions.empty();

		for (const auto& cond : trans.conditions)
		{
			auto it = ctx.parameters->find(cond.paramName);
			if (it == ctx.parameters->end()) return false;

			const AnimatorParameter& param = it->second;
			bool satisfied = false;

			switch (param.type)
			{
			case AnimatorParamType::Float:
				switch (cond.op)
				{
				case CompareOp::Greater:      satisfied = param.floatVal >  cond.floatVal; break;
				case CompareOp::Less:         satisfied = param.floatVal <  cond.floatVal; break;
				case CompareOp::Equal:        satisfied = std::abs(param.floatVal - cond.floatVal) < 0.0001f; break;
				case CompareOp::NotEqual:     satisfied = std::abs(param.floatVal - cond.floatVal) >= 0.0001f; break;
				case CompareOp::GreaterEqual: satisfied = param.floatVal >= cond.floatVal; break;
				case CompareOp::LessEqual:    satisfied = param.floatVal <= cond.floatVal; break;
				}
				break;
			case AnimatorParamType::Bool:
			case AnimatorParamType::Trigger:
				satisfied = (cond.op == CompareOp::Equal)
					? (param.boolVal == cond.boolVal)
					: (param.boolVal != cond.boolVal);
				break;
			case AnimatorParamType::Int:
				switch (cond.op)
				{
				case CompareOp::Greater:      satisfied = param.intVal >  cond.intVal; break;
				case CompareOp::Less:         satisfied = param.intVal <  cond.intVal; break;
				case CompareOp::Equal:        satisfied = param.intVal == cond.intVal; break;
				case CompareOp::NotEqual:     satisfied = param.intVal != cond.intVal; break;
				case CompareOp::GreaterEqual: satisfied = param.intVal >= cond.intVal; break;
				case CompareOp::LessEqual:    satisfied = param.intVal <= cond.intVal; break;
				}
				break;
			}

			if (!satisfied) return false;
		}
		return true;
	}

	void AnimGraphStateMachineNode::StartTransition(const AnimatorTransition& trans)
	{
		m_PrevStateName    = m_CurrentStateName;
		m_CurrentStateName = trans.toState;
		m_BlendAlpha       = 0.0f;
		m_BlendDuration    = trans.blendDuration;
		m_BlendState       = ControllerBlendState::Blending;

		AnimatorState* newState = GetState(m_CurrentStateName);
		if (newState) newState->currentTime = 0.0f;
	}

	AnimGraphPose AnimGraphStateMachineNode::ComputeStatePose(const AnimatorState& state,
	                                                          const AnimGraphContext& ctx)
	{
		AnimGraphPose pose;
		if (!state.clip || !ctx.skeleton) return pose;

		const VansAnimationClip& clip = *state.clip;
		const Skeleton& skel = *ctx.skeleton;

		float duration = clip.duration;
		if (duration <= 0.0f) return pose;

		float sampleTime = state.currentTime;
		if (state.loop)
		{
			sampleTime = std::fmod(sampleTime, duration);
			if (sampleTime < 0.0f) sampleTime += duration;
		}
		else
		{
			sampleTime = std::clamp(sampleTime, 0.0f, duration);
		}

		size_t boneCount = skel.bones.size();
		pose.localTransforms.resize(boneCount, glm::mat4(1.0f));

		for (size_t bi = 0; bi < boneCount && bi < clip.boneKeyframes.size(); ++bi)
		{
			const auto& keyframes = clip.boneKeyframes[bi];
			if (keyframes.empty()) continue;

			glm::vec3 pos, scl;
			glm::quat rot;
			InterpolateKeyframes(keyframes, sampleTime, pos, rot, scl);

			glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
			glm::mat4 R = glm::toMat4(rot);
			glm::mat4 S = glm::scale(glm::mat4(1.0f), scl);
			pose.localTransforms[bi] = T * R * S;
		}

		pose.valid = true;
		return pose;
	}

	AnimatorState* AnimGraphStateMachineNode::GetState(const std::string& name)
	{
		for (auto& s : m_States)
			if (s.name == name)
				return &s;
		return nullptr;
	}

	void AnimGraphStateMachineNode::InterpolateKeyframes(const std::vector<BoneKeyframe>& keyframes,
	                                                     float time,
	                                                     glm::vec3& outPos, glm::quat& outRot, glm::vec3& outScale)
	{
		if (keyframes.size() == 1)
		{
			outPos   = keyframes[0].position;
			outRot   = keyframes[0].rotation;
			outScale = keyframes[0].scale;
			return;
		}

		size_t idx = 0;
		for (size_t i = 0; i < keyframes.size() - 1; ++i)
		{
			if (time < keyframes[i + 1].time) { idx = i; break; }
			idx = i;
		}

		size_t next = std::min(idx + 1, keyframes.size() - 1);
		if (idx == next)
		{
			outPos   = keyframes[idx].position;
			outRot   = keyframes[idx].rotation;
			outScale = keyframes[idx].scale;
			return;
		}

		float dt = keyframes[next].time - keyframes[idx].time;
		float factor = (dt > 0.0001f) ? (time - keyframes[idx].time) / dt : 0.0f;
		factor = std::clamp(factor, 0.0f, 1.0f);

		outPos   = glm::mix(keyframes[idx].position, keyframes[next].position, factor);
		outRot   = glm::slerp(keyframes[idx].rotation, keyframes[next].rotation, factor);
		outScale = glm::mix(keyframes[idx].scale, keyframes[next].scale, factor);
	}

	// ═════════════════════════════════════════════════════════════
	//  VansAnimGraph
	// ═════════════════════════════════════════════════════════════

	VansAnimGraph::VansAnimGraph()  = default;
	VansAnimGraph::~VansAnimGraph() = default;

	int VansAnimGraph::AddNode(std::unique_ptr<VansAnimGraphNode> node)
	{
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

	VansAnimGraphNode* VansAnimGraph::GetNode(int nodeId) const
	{
		auto it = m_Nodes.find(nodeId);
		return (it != m_Nodes.end()) ? it->second.get() : nullptr;
	}

	int VansAnimGraph::AddLink(int fromNodeId, int fromPinIndex, int toNodeId, int toPinIndex)
	{
		// 检查目标输入 Pin 是否已有连线（一个输入只能有一条连线）
		for (const auto& link : m_Links)
		{
			if (link.toNodeId == toNodeId && link.toPinIndex == toPinIndex)
				return -1;  // 已有连线，拒绝
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

	void VansAnimGraph::RemoveLink(int linkId)
	{
		m_Links.erase(
			std::remove_if(m_Links.begin(), m_Links.end(),
				[linkId](const AnimGraphLink& l) { return l.linkId == linkId; }),
			m_Links.end());
	}

	VansAnimGraphNode* VansAnimGraph::GetInputNode(int nodeId, int inputPinIndex) const
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

	AnimGraphPose VansAnimGraph::Evaluate(const AnimGraphContext& ctx)
	{
		if (m_OutputNodeId < 0) return {};

		VansAnimGraphNode* outputNode = GetNode(m_OutputNodeId);
		if (!outputNode) return {};

		return outputNode->Evaluate(ctx, *this);
	}

	void VansAnimGraph::AdvanceTime(float dt)
	{
		for (auto& [id, node] : m_Nodes)
			node->AdvanceTime(dt);
	}

	void VansAnimGraph::ResetAll()
	{
		for (auto& [id, node] : m_Nodes)
			node->Reset();
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
		}
		return nullptr;
	}

	std::unique_ptr<VansAnimGraphNode> VansAnimGraph::CreateNodeByTypeName(const std::string& typeName)
	{
		return CreateNodeByType(VansAnimGraphNode::StringToType(typeName));
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
		nlohmann::json props;
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
		default:
			break;
		}
	}

	void VansAnimGraph::SerializeToJsonObject(nlohmann::json& outJson) const
	{
		// 节点数组
		nlohmann::json nodesJson = nlohmann::json::array();
		for (const auto& [id, node] : m_Nodes)
		{
			nlohmann::json nj;
			nj["id"]       = node->GetNodeId();
			nj["type"]     = VansAnimGraphNode::TypeToString(node->GetType());
			nj["name"]     = node->GetName();
			nj["posX"]     = node->m_EditorPosX;
			nj["posY"]     = node->m_EditorPosY;
			nj["properties"] = SerializeNodeProperties(node.get());
			nodesJson.push_back(nj);
		}
		outJson["nodes"] = nodesJson;

		// 连线数组
		nlohmann::json linksJson = nlohmann::json::array();
		for (const auto& link : m_Links)
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

	std::string VansAnimGraph::SerializeToJson() const
	{
		nlohmann::json j;
		SerializeToJsonObject(j);
		return j.dump(4);
	}

	std::unique_ptr<VansAnimGraph> VansAnimGraph::DeserializeFromJsonObject(const nlohmann::json& j)
	{
		auto graph = std::make_unique<VansAnimGraph>();

		if (!j.contains("nodes") || !j.contains("links"))
			return graph;

		// 先确定最大 ID 以初始化计数器
		int maxNodeId = 0;
		int maxLinkId = 0;

		// 反序列化节点
		for (const auto& nj : j["nodes"])
		{
			std::string typeName = nj["type"].get<std::string>();
			auto node = CreateNodeByTypeName(typeName);
			if (!node) continue;

			int nodeId = nj["id"].get<int>();
			node->m_NodeId = nodeId;
			if (nj.contains("name")) node->SetName(nj["name"].get<std::string>());
			if (nj.contains("posX")) node->m_EditorPosX = nj["posX"].get<float>();
			if (nj.contains("posY")) node->m_EditorPosY = nj["posY"].get<float>();

			if (nj.contains("properties"))
				DeserializeNodeProperties(node.get(), nj["properties"]);

			// 记录 Entry/Output
			if (node->GetType() == AnimGraphNodeType::Entry)
				graph->m_EntryNodeId = nodeId;
			else if (node->GetType() == AnimGraphNodeType::Output)
				graph->m_OutputNodeId = nodeId;

			if (nodeId > maxNodeId) maxNodeId = nodeId;
			graph->m_Nodes[nodeId] = std::move(node);
		}

		// 反序列化连线
		for (const auto& lj : j["links"])
		{
			AnimGraphLink link;
			link.linkId       = lj["id"].get<int>();
			link.fromNodeId   = lj["fromNode"].get<int>();
			link.fromPinIndex = lj["fromPin"].get<int>();
			link.toNodeId     = lj["toNode"].get<int>();
			link.toPinIndex   = lj["toPin"].get<int>();
			graph->m_Links.push_back(link);

			if (link.linkId > maxLinkId) maxLinkId = link.linkId;
		}

		graph->m_NextNodeId = maxNodeId + 1;
		graph->m_NextLinkId = maxLinkId + 1;

		return graph;
	}

	std::unique_ptr<VansAnimGraph> VansAnimGraph::DeserializeFromJson(const std::string& jsonStr)
	{
		try
		{
			nlohmann::json j = nlohmann::json::parse(jsonStr);
			return DeserializeFromJsonObject(j);
		}
		catch (...)
		{
			return std::make_unique<VansAnimGraph>();
		}
	}

}  // namespace VansGraphics
