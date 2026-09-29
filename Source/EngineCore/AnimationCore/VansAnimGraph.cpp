#include <set>
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
	namespace
	{
		AnimGraphPose PlaybackPlaceholder() { AnimGraphPose pose; pose.valid = true; return pose; }
		float EvaluateTransitionAlpha(const std::vector<AnimatorTransitionCurveKey>& curve, float progress)
		{
			if (curve.empty()) return std::clamp(progress, 0.0f, 1.0f);
			if (progress <= curve.front().time) return std::clamp(curve.front().value, 0.0f, 1.0f);
			if (progress >= curve.back().time) return std::clamp(curve.back().value, 0.0f, 1.0f);
			for (size_t i = 1; i < curve.size(); ++i)
			{
				const auto& a = curve[i - 1]; const auto& b = curve[i];
				if (progress > b.time) continue;
				const float span = b.time - a.time, t = (progress - a.time) / span;
				return std::clamp((2*t*t*t - 3*t*t + 1)*a.value + (t*t*t - 2*t*t + t)*span*a.leaveTangent
					+ (-2*t*t*t + 3*t*t)*b.value + (t*t*t - t*t)*span*b.arriveTangent, 0.0f, 1.0f);
			}
			return progress;
		}
		bool RequiresStagedPlayback(const VansAnimGraphNode& node)
		{
			if (node.GetType() == VansAnimGraphNodeType::Inertialization) return true;
			if (node.GetType() == VansAnimGraphNodeType::Clip) return !static_cast<const AnimGraphClipNode&>(node).m_SyncGroup.empty();
			if (node.GetType() == VansAnimGraphNodeType::Blend) return static_cast<const AnimGraphBlendNode&>(node).m_InterpolateAlpha;
			if (node.GetType() == VansAnimGraphNodeType::BlendSpace2D) return !static_cast<const AnimGraphBlendSpace2DNode&>(node).m_SyncGroup.empty();
			if (node.GetType() == VansAnimGraphNodeType::StateMachine)
			{
				const auto& machine = static_cast<const AnimGraphStateMachineNode&>(node);
				return std::any_of(machine.m_States.begin(), machine.m_States.end(), [](const auto& state){return state.conduit;})
					|| machine.m_MaxTransitionsPerFrame > 1 || machine.m_SkipFirstUpdateTransition
					|| std::any_of(machine.m_States.begin(),machine.m_States.end(),[](const auto& s){return s.alwaysResetOnEntry;})
					|| std::any_of(machine.m_Transitions.begin(),machine.m_Transitions.end(),[](const auto& t){return t.requireRelevantClipFinished || t.automaticRemainingTime || t.inertialization
						|| std::any_of(t.conditions.begin(),t.conditions.end(),[](const auto& c){return c.source!=AnimatorConditionSource::Parameter;});});
			}
			return false;
		}
	}
	float VansAnimGraphStateMachineRuntimeState::GetStateWeight(const std::string& name) const
	{
		if (activeTransitions.empty()) return currentStateName == name ? 1.0f : 0.0f;
		float weight = 0;
		for (size_t i = 0; i < activeTransitions.size(); ++i)
		{
			const auto& transition = activeTransitions[i];
			if (i > 0) weight *= 1 - transition.alpha;
			else if (transition.previousStateName == name) weight += 1 - transition.alpha;
			if (transition.nextStateName == name) weight += transition.alpha;
		}
		return std::clamp(weight, 0.0f, 1.0f);
	}
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
		const AnimGraphContext& ctx, float weight)
	{
		return instance.EvaluateInput(nodeId, inputPinIndex, ctx, weight);
	}

	AnimGraphPose VansAnimGraphNode::EvaluateNodePose(
		VansAnimGraphInstance& instance,
		int nodeId,
		const AnimGraphContext& ctx, int parentId, float weight)
	{
		return instance.EvaluateWeightedNode(parentId, nodeId, weight, ctx);
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
		case VansAnimGraphNodeType::MultiWayBlend:  return "MultiWayBlend";
		case VansAnimGraphNodeType::ModifyCurve: return "ModifyCurve";
		case VansAnimGraphNodeType::ComponentBoneScale: return "ComponentBoneScale";
		case VansAnimGraphNodeType::ComponentBoneTransform: return "ComponentBoneTransform";
		case VansAnimGraphNodeType::IfCondition:    return "IfCondition";
		case VansAnimGraphNodeType::Switch:         return "Switch";
		case VansAnimGraphNodeType::AdditiveBlend:  return "AdditiveBlend";
		case VansAnimGraphNodeType::Inertialization: return "Inertialization";
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

	bool AnimGraphClipNode::ResolveLoop(const AnimGraphContext& ctx) const
	{
		if (!m_LoopParameter.empty() && ctx.parameters)
		{
			const auto found = ctx.parameters->find(m_LoopParameter);
			if (found != ctx.parameters->end() && found->second.type == AnimatorParamType::Bool)
				return found->second.boolVal;
		}
		return m_Loop;
	}

	void AnimGraphClipNode::AdvancePlayback(VansAnimGraphClipRuntimeState& clock, float deltaTime,
	                                       const AnimGraphContext& ctx, bool normalizeTime) const
	{
		const bool loop = ResolveLoop(ctx);
		float duration = 0;
		if (ctx.clips && (normalizeTime || !m_LoopParameter.empty() || clock.startPositionPending))
		{
			const auto found = ctx.clips->find(m_ClipName);
			if (found != ctx.clips->end()) duration = found->second.duration;
		}
		if (clock.startPositionPending)
		{
			clock.currentTime = std::clamp(m_StartPosition, 0.0f, duration);
			clock.previousTime = clock.currentTime;
			clock.startPositionPending = false;
		}
		// 上一轮可跨越多个循环；先按上一帧循环语义恢复相位，再应用本帧开关。
		if ((normalizeTime || !m_LoopParameter.empty()) && duration > 0
			&& (m_LoopParameter.empty() || clock.currentTime < 0 || clock.currentTime > duration))
			clock.currentTime = VansAnimationSampler::ResolveSampleTime(clock.currentTime, 0, duration,
				m_LoopParameter.empty() ? loop : clock.looping);
		clock.previousTime = clock.currentTime;
		clock.currentTime += deltaTime * m_Speed;
		if ((normalizeTime || !m_LoopParameter.empty()) && !loop && duration > 0)
			clock.currentTime = std::clamp(clock.currentTime, 0.0f, duration);
		clock.looping = loop;
	}

	AnimGraphPose AnimGraphClipNode::Evaluate(const AnimGraphContext& ctx,
	                                          VansAnimGraphInstance& instance) const
	{
		if (ctx.preparePlayback) return PlaybackPlaceholder();
		AnimGraphPose pose;
		if (!ctx.clips || !ctx.skeleton) return pose;
		auto it = ctx.clips->find(m_ClipName);
		if (it == ctx.clips->end()) return pose;
		VansAnimGraphClipRuntimeState& runtime = ResolveClipState(instance, m_NodeId);
		if (runtime.startPositionPending) AdvancePlayback(runtime, 0, ctx);
		VansAnimationSampleRequest request;
		request.previousTime = runtime.previousTime;
		request.currentTime = runtime.currentTime;
		request.loop = ResolveLoop(ctx);
		// 可切换循环播放器保留恰好到达末帧的姿态；仅越过端点才回绕。
		if (!m_LoopParameter.empty() && request.currentTime == it->second.duration)
			request.loop = false;
		if (HasExplicitSampleTime())
		{
			float sampleTime = m_SampleTime;
			if (!m_SampleTimeParameter.empty())
			{
				if (!ctx.parameters) return pose;
				const auto parameter = ctx.parameters->find(m_SampleTimeParameter);
				if (parameter == ctx.parameters->end()
					|| parameter->second.type != AnimatorParamType::Float
					|| !std::isfinite(parameter->second.floatVal)) return pose;
				sampleTime = parameter->second.floatVal;
			}
			request.currentTime = std::clamp(sampleTime, 0.0f, it->second.duration);
			request.previousTime = request.currentTime;
			request.loop = false;
			runtime.previousTime = runtime.currentTime = request.currentTime;
		}
		request.sourceNodeId = static_cast<std::uint64_t>(m_NodeId);
		VansAnimationSampler::Sample(it->second, *ctx.skeleton, request, pose);
		if (m_AdditiveReferenceTime >= 0)
		{
			const auto referenceClip = m_AdditiveReferenceClip.empty() ? it : ctx.clips->find(m_AdditiveReferenceClip);
			if (referenceClip == ctx.clips->end()) return {};
			AnimGraphPose reference; VansAnimationSampleRequest referenceRequest;
			referenceRequest.previousTime=referenceRequest.currentTime=std::clamp(m_AdditiveReferenceTime,0.0f,referenceClip->second.duration);
			referenceRequest.loop=false;
			VansAnimationSampler::Sample(referenceClip->second,*ctx.skeleton,referenceRequest,reference);
			if (!pose.valid || !reference.valid || !VansPoseMath::MakeAdditiveDeltaPose(pose.localPose,reference.localPose,*ctx.skeleton,m_AdditiveMode)) return {};
			for (const auto& curve : reference.curves) if (curve.present)
			{
				auto found=std::find_if(pose.curves.begin(),pose.curves.end(),[&](const auto& c){return c.id==curve.id;});
				if (found==pose.curves.end()) pose.curves.push_back({curve.id,curve.name,-curve.value,true});
				else found->value-=curve.value;
			}
		}
		if (!m_RootMotion || HasExplicitSampleTime())
			pose.rootMotion = {};
		if (HasExplicitSampleTime())
			pose.events.clear();
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

		// 确定 alpha
		float alpha = m_FixedAlpha;
		if (m_UseParam && ctx.parameters)
		{
			auto it = ctx.parameters->find(m_ParamName);
			if (it != ctx.parameters->end() && it->second.type == AnimatorParamType::Float)
				alpha = it->second.floatVal;
		}

		if (m_MapAlpha)
			alpha = m_AlphaOutMin + (alpha-m_AlphaInMin)/(m_AlphaInMax-m_AlphaInMin)*(m_AlphaOutMax-m_AlphaOutMin);
		if (m_InterpolateAlpha)
		{
			auto& history = instance.m_BlendAlphaStates[m_NodeId];
			if (!ctx.stagedPlayback || ctx.preparePlayback)
			{
				// UE先保存未钳制的映射/插值结果，最终权重才钳制；首次更新直接采用输入。
				if (history.initialized)
				{
					const float speed = alpha >= history.value ? m_AlphaSpeedIncreasing : m_AlphaSpeedDecreasing;
					const float distance = alpha-history.value;
					const float dt = ctx.inputDeltaTime >= 0 ? ctx.inputDeltaTime : ctx.deltaTime;
					if (speed > 0 && distance*distance >= 1.e-8f)
						alpha = history.value + distance*std::clamp(dt*speed, 0.0f, 1.0f);
				}
				history = {true, alpha};
			}
			alpha = history.value;
		}
		alpha = std::clamp(alpha, 0.0f, 1.0f);
		AnimGraphPose poseA = EvaluateInputPose(instance, m_NodeId, 0, ctx, 1-alpha);
		AnimGraphPose poseB = EvaluateInputPose(instance, m_NodeId, 1, ctx, std::clamp(alpha, 0.0f, 1.0f));
		if (ctx.preparePlayback) return PlaybackPlaceholder();
		if (!poseA.valid) return poseB;
		if (!poseB.valid) return poseA;
		if (m_LinearRotationBlend)
			return VansPosePayloadMixer::BlendWeighted({poseA, poseB}, {1-alpha, alpha});
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

				AnimGraphPose poseA = EvaluateInputPose(instance, m_NodeId, i, ctx, 1-alpha);
				AnimGraphPose poseB = EvaluateInputPose(instance, m_NodeId, i + 1, ctx, alpha);
				if (ctx.preparePlayback) return PlaybackPlaceholder();

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

	float VansAnimGraphInputFilterState::Update(float input, float deltaTime, float duration)
	{
		if (window != duration) { *this = {}; window = duration; }
		if (window <= 0) return output = input;
		if (deltaTime <= 0.0001f) return output;
		time += deltaTime;
		if (samples.empty()) samples.resize(10);
		for (auto& sample : samples) if (time - sample.time > window) sample.time = 0;
		std::size_t slot = writeIndex;
		if (samples[slot].time > 0)
		{
			for (std::size_t offset = 0; offset < samples.size(); ++offset)
			{
				const auto candidate = (writeIndex + offset) % samples.size();
				if (samples[candidate].time <= 0) { slot = candidate; break; }
			}
			if (samples[slot].time > 0) { slot = samples.size(); samples.resize(slot + 5); }
		}
		samples[slot] = { input, time };
		writeIndex = (slot + 1) % samples.size();
		float sum = 0, total = 0;
		for (const auto& sample : samples)
		{
			if (sample.time <= 0) continue;
			const float age = (time - sample.time) / window;
			const float weight = 1 - age*age*age;
			if (weight > 0) { sum += weight*sample.value; total += weight; }
		}
		return output = total > 0 ? sum/total : 0;
	}

	AnimGraphBlendSpace2DNode::AnimGraphBlendSpace2DNode()
	{
		m_Type = VansAnimGraphNodeType::BlendSpace2D;
		m_Name = "BlendSpace2D";
	}
	bool AnimGraphBlendSpace2DNode::ValidateSampleGrid(std::string& error) const
	{
		const auto& grid=m_SampleGrid;
		if(!grid.IsEnabled())return true;
		bool valid=!m_BilinearGrid && grid.columns>=2 && grid.rows>=2 && grid.columns<=128 && grid.rows<=128
			&& grid.cells.size()==size_t(grid.columns)*size_t(grid.rows)
			&& std::isfinite(grid.minX) && std::isfinite(grid.maxX) && grid.maxX>grid.minX
			&& std::isfinite(grid.minY) && std::isfinite(grid.maxY) && grid.maxY>grid.minY;
		for(const auto& cell:grid.cells)
		{
			float sum=0;std::unordered_set<int> indices;
			valid=valid && !cell.empty() && cell.size()<=3;
			for(const auto& influence:cell)
			{
				valid=valid && influence.sampleIndex>=0 && influence.sampleIndex<static_cast<int>(m_Samples.size())
					&& std::isfinite(influence.weight) && influence.weight>=0 && indices.insert(influence.sampleIndex).second;
				sum+=influence.weight;
			}
			valid=valid && std::abs(sum-1)<=1e-4f;
		}
		if(!valid)error="BlendSpace sample grid requires finite bounds, 2..128 rows/columns and normalized cells of at most three distinct samples";
		return valid;
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

	namespace
	{
		// 映射整个循环区间而非两个独立的取模时间，保留跨零点、多圈及反向事件区间。
		bool MapLoopMarkerTime(const VansAnimationClip& source, const VansAnimationClip& target,
			float rawTime, float& mapped)
		{
			if (source.duration <= 0 || target.duration <= 0 || source.syncMarkers.empty()
				|| source.syncMarkers.size() != target.syncMarkers.size()) return false;
			VansAnimationFrameVector<const AnimationSyncMarker*> a, b;
			for (const auto& marker : source.syncMarkers) a.push_back(&marker);
			for (const auto& marker : target.syncMarkers) b.push_back(&marker);
			const auto less = [](const auto* x, const auto* y) { return x->time < y->time; };
			std::stable_sort(a.begin(), a.end(), less); std::stable_sort(b.begin(), b.end(), less);
			const auto id = [](const auto* marker) { return marker->id ? marker->id : VansAnimationStableId(marker->name); };
			const size_t count = a.size();
			for (size_t i = 0; i < count; ++i)
				if (!std::isfinite(a[i]->time) || a[i]->time < 0 || a[i]->time >= source.duration
					|| !std::isfinite(b[i]->time) || b[i]->time < 0 || b[i]->time >= target.duration
					|| (i && (a[i]->time <= a[i-1]->time || b[i]->time <= b[i-1]->time))) return false;
			size_t offset = count;
			for (size_t j = 0; j < count; ++j)
			{
				bool matches = true;
				for (size_t i = 0; i < count; ++i) matches = matches && id(a[i]) == id(b[(j+i)%count]);
				if (matches) { offset = j; break; }
			}
			if (offset == count) return false;
			const float cycle = std::floor((rawTime - a[0]->time) / source.duration);
			const float local = rawTime - cycle * source.duration;
			size_t index = 0;
			while (index + 1 < count && a[index+1]->time <= local) ++index;
			const float end = index + 1 < count ? a[index+1]->time : a[0]->time + source.duration;
			const size_t targetIndex = (offset + index) % count;
			const float targetStart = b[targetIndex]->time + (offset + index >= count ? target.duration : 0);
			const float targetRange = (targetIndex + 1 < count ? b[targetIndex+1]->time
				: b[0]->time + target.duration) - b[targetIndex]->time;
			mapped = cycle * target.duration + targetStart + (local-a[index]->time)/(end-a[index]->time)*targetRange;
			return true;
		}
	}

	bool AnimGraphBlendSpace2DNode::SynchronizeSampleTimes(const AnimGraphContext& ctx,
		VansAnimGraphInstance& instance, const VansAnimationFrameVector<float>& weights, bool allowMarkers) const
	{
		if (!ctx.clips || !std::isfinite(ctx.deltaTime)) return false;
		auto& runtime = instance.m_BlendSpaceStates[m_NodeId];
		VansAnimationFrameVector<const AnimGraphClipNode*> nodes;
		VansAnimationFrameVector<const VansAnimationClip*> clips;
		int leader = -1, firstMarked = -1;
		bool compatibleMarkers = true;
		float length = 0;
		for (int i = 0; i < static_cast<int>(weights.size()); ++i)
		{
			const auto* input = instance.m_Definition.GetInputNode(m_NodeId, i);
			if (!input || input->GetType() != VansAnimGraphNodeType::Clip) return false;
			const auto* node = static_cast<const AnimGraphClipNode*>(input);
			const auto found = ctx.clips->find(node->m_ClipName);
			if (found == ctx.clips->end() || !std::isfinite(found->second.duration) || found->second.duration <= 0) return false;
			nodes.push_back(node); clips.push_back(&found->second);
			length += weights[i] * found->second.duration / (node->m_Speed != 0 ? node->m_Speed : 1);
			if (!found->second.syncMarkers.empty())
			{
				if (firstMarked < 0) firstMarked = i;
				float ignored;
				compatibleMarkers = compatibleMarkers && MapLoopMarkerTime(*clips[firstMarked], found->second, 0, ignored);
				if (weights[i] > 0 && (leader < 0 || weights[i] > weights[leader])) leader = i;
			}
		}
		if (!compatibleMarkers || !allowMarkers) leader = -1;
		if (!runtime.playbackInitialized)
		{
			runtime.normalizedTime = m_StartPosition;
			runtime.playbackInitialized = true;
		}
		const float previousPhase = runtime.normalizedTime;
		float previous = previousPhase, current = previousPhase;
		if (leader >= 0)
		{
			const int leaderId = nodes[leader]->GetNodeId();
			previous = previousPhase * clips[leader]->duration;
			if (runtime.markerLeader >= 0 && runtime.markerLeader < static_cast<int>(nodes.size()))
			{
				if (std::find(runtime.activeSamples.begin(), runtime.activeSamples.end(), leader) != runtime.activeSamples.end())
					previous = instance.GetClipState(leaderId).currentTime;
				else
				{
					const int old = runtime.markerLeader;
					if (!MapLoopMarkerTime(*clips[old], *clips[leader], instance.GetClipState(nodes[old]->GetNodeId()).currentTime, previous)) return false;
				}
			}
			previous = VansAnimationSampler::ResolveSampleTime(previous, 0, clips[leader]->duration, true);
			current = previous + ctx.deltaTime * nodes[leader]->m_Speed;
			runtime.normalizedTime = VansAnimationSampler::ResolveSampleTime(current, 0, clips[leader]->duration, true) / clips[leader]->duration;
		}
		else
		{
			if (length <= 0) return false;
			current = previous + ctx.deltaTime / length;
			runtime.normalizedTime = VansAnimationSampler::ResolveSampleTime(current, 0, 1, true);
		}
		runtime.activeSamples.clear();
		for (int i = 0; i < static_cast<int>(weights.size()); ++i)
		{
			if (weights[i] <= 0) continue;
			auto& clock = instance.GetClipState(nodes[i]->GetNodeId());
			if (leader >= 0 && !clips[i]->syncMarkers.empty())
			{
				if (!MapLoopMarkerTime(*clips[leader], *clips[i], previous, clock.previousTime)
					|| !MapLoopMarkerTime(*clips[leader], *clips[i], current, clock.currentTime)) return false;
			}
			else
			{
				clock.previousTime = (leader >= 0 ? previousPhase : previous) * clips[i]->duration;
				clock.currentTime = (leader >= 0 ? current/clips[leader]->duration : current) * clips[i]->duration;
			}
			clock.startPositionPending = false;
			runtime.activeSamples.push_back(i);
		}
		runtime.markerLeader = leader;
		return true;
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
		if (m_CubicFilterWindowX > 0 || m_CubicFilterWindowY > 0)
		{
			auto& filter = instance.m_BlendSpaceStates[m_NodeId];
			const float dt = ctx.inputDeltaTime >= 0 ? ctx.inputDeltaTime : ctx.deltaTime;
			x = filter.x.Update(x, ctx.stagedPlayback && !ctx.preparePlayback ? 0 : dt, m_CubicFilterWindowX);
			y = filter.y.Update(y, ctx.stagedPlayback && !ctx.preparePlayback ? 0 : dt, m_CubicFilterWindowY);
		}
		if (m_BilinearGrid || m_SampleGrid.IsEnabled())
		{
			VansAnimationFrameVector<AnimGraphPose> poses;
			VansAnimationFrameVector<float> weights;
			VansAnimationFrameVector<float> sampleWeights(static_cast<size_t>(count),0);
			VansAnimationFrameVector<int> order;
			if(m_SampleGrid.IsEnabled())
			{
				const auto& grid=m_SampleGrid;
				if(!std::isfinite(x)||!std::isfinite(y))return {};
				const float gx=(std::clamp(x,grid.minX,grid.maxX)-grid.minX)/(grid.maxX-grid.minX)*(grid.columns-1);
				const float gy=(std::clamp(y,grid.minY,grid.maxY)-grid.minY)/(grid.maxY-grid.minY)*(grid.rows-1);
				const int ix=static_cast<int>(gx),iy=static_cast<int>(gy);
				const float tx=gx-ix,ty=gy-iy;
				// 行优先读取左下/右下/左上/右上，零权重条目也保留首次出现顺序。
				for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
				{
					if(ix+dx>=grid.columns || iy+dy>=grid.rows)continue;
					const float w=(dx?tx:1-tx)*(dy?ty:1-ty);
					for(const auto& entry:grid.cells[size_t(iy+dy)*grid.columns+ix+dx])
					{
						if(std::find(order.begin(),order.end(),entry.sampleIndex)==order.end())order.push_back(entry.sampleIndex);
						sampleWeights[entry.sampleIndex]+=entry.weight*w;
					}
				}
				// 明确的选择排序保证相等权重的交换顺序；四个三角网格点至多产生十二个候选。
				for(int end=static_cast<int>(order.size())-1;end>0;--end)
				{
					int smallest=0;
					for(int i=1;i<=end;++i)if(sampleWeights[order[smallest]]>sampleWeights[order[i]])smallest=i;
					std::swap(order[smallest],order[end]);
				}
				float sum=0;
				for(int i:order){if(sampleWeights[i]<.00001f)sampleWeights[i]=0;sum+=sampleWeights[i];}
				if(sum<=0)return {};
				for(float& w:sampleWeights)w/=sum;
			}
			else
			{
			if (count != 4) return {};
			float minX = m_Samples[0].x, maxX = minX, minY = m_Samples[0].y, maxY = minY;
			for (const auto& sample : m_Samples)
			{
				minX = std::min(minX, sample.x); maxX = std::max(maxX, sample.x);
				minY = std::min(minY, sample.y); maxY = std::max(maxY, sample.y);
			}
			if (maxX <= minX || maxY <= minY) return {};
			x = std::clamp((x - minX)/(maxX - minX), 0.0f, 1.0f);
			y = std::clamp((y - minY)/(maxY - minY), 0.0f, 1.0f);
			for (int i = 0; i < count; ++i)
			{
				const float weight = (m_Samples[i].x == minX ? 1-x : x) * (m_Samples[i].y == minY ? 1-y : y);
				sampleWeights[i]=weight > 0.00001f ? weight : 0;
				order.push_back(i);
			}
			}
			if (m_SynchronizeSamples)
			{
				const float sum = std::accumulate(sampleWeights.begin(), sampleWeights.end(), 0.0f);
				if (sum <= 0) return {};
				if(!m_SampleGrid.IsEnabled())for (float& weight : sampleWeights) weight /= sum;
				if (ctx.preparePlayback)
				{
					instance.m_PreparedSampleWeights[m_NodeId].assign(sampleWeights.begin(), sampleWeights.end());
					return PlaybackPlaceholder();
				}
				if (!ctx.stagedPlayback && !SynchronizeSampleTimes(ctx, instance, sampleWeights)) return {};
				if(!m_SampleGrid.IsEnabled())std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return sampleWeights[a] > sampleWeights[b]; });
			}
			for (int i : order)
			{
				const float weight = sampleWeights[i];
				if (weight <= 0) continue;
				auto pose = EvaluateInputPose(instance, m_NodeId, i, ctx, weight);
				if (!pose.valid) return {};
				// 同步资产播放器只从最高权重样本生成通知，避免四个样本重复触发脚步。
				if (m_SynchronizeSamples)
				{
					if (i != order.front()) pose.events.clear();
					else for (auto& event : pose.events) event.weight /= weight;
				}
				poses.push_back(std::move(pose)); weights.push_back(weight);
			}
			return ctx.preparePlayback ? PlaybackPlaceholder() : VansPosePayloadMixer::BlendWeighted(poses, weights);
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
			AnimGraphPose pose = EvaluateInputPose(instance, m_NodeId, index, ctx, weights[index]/totalWeight);
			if (!pose.valid)
				continue;
			const float normalizedWeight = weights[static_cast<size_t>(index)] / totalWeight;
			if (ctx.preparePlayback) { result = PlaybackPlaceholder(); continue; }
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

	AnimGraphModifyCurveNode::AnimGraphModifyCurveNode()
	{
		m_Type = VansAnimGraphNodeType::ModifyCurve;
		m_Name = "ModifyCurve";
	}

	std::vector<AnimGraphPin> AnimGraphModifyCurveNode::GetPins() const
	{
		return {{0,"Pose",AnimGraphPinType::Pose,AnimGraphPinKind::Input},
			{0,"Result",AnimGraphPinType::Pose,AnimGraphPinKind::Output}};
	}

	AnimGraphPose AnimGraphModifyCurveNode::Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		auto pose=EvaluateInputPose(instance,m_NodeId,0,ctx);
		if (ctx.preparePlayback || !pose.valid) return pose;
		auto read=[&](const std::string& parameter,float fixed,float& value)
		{
			value=fixed;
			if (!parameter.empty())
			{
				if (!ctx.parameters) return false;
				const auto found=ctx.parameters->find(parameter);
				if (found==ctx.parameters->end() || found->second.type!=AnimatorParamType::Float) return false;
				value=found->second.floatVal;
			}
			return std::isfinite(value);
		};
		float alpha;
		if (!read(m_AlphaParameter,m_Alpha,alpha)) return {};
		alpha=std::clamp(alpha,0.0f,1.0f);
		for (const auto& curve:m_Curves)
		{
			float value;
			if (!read(curve.parameter,curve.value,value)) return {};
			const auto id=VansAnimationStableId(curve.name);
			auto found=std::find_if(pose.curves.begin(),pose.curves.end(),[&](const auto& sample){return sample.id==id;});
			const float current=found!=pose.curves.end() && found->present ? found->value : 0;
			if (m_Mode==VansCurveModifyMode::Scale) value*=current;
			else if (m_Mode!=VansCurveModifyMode::Blend) return {};
			const float result=current+(value-current)*alpha;
			if (found==pose.curves.end()) pose.curves.push_back({id,curve.name,result,true});
			else {found->value=result;found->present=true;}
		}
		return pose;
	}

	AnimGraphComponentBoneScaleNode::AnimGraphComponentBoneScaleNode()
	{
		m_Type = VansAnimGraphNodeType::ComponentBoneScale;
		m_Name = "ComponentBoneScale";
	}

	AnimGraphComponentBoneTransformNode::AnimGraphComponentBoneTransformNode()
	{
		m_Type = VansAnimGraphNodeType::ComponentBoneTransform;
		m_Name = "ComponentBoneTransform";
	}

	std::vector<AnimGraphPin> AnimGraphComponentBoneTransformNode::GetPins() const
	{
		return {{0,"Pose",AnimGraphPinType::Pose,AnimGraphPinKind::Input},
			{0,"Result",AnimGraphPinType::Pose,AnimGraphPinKind::Output}};
	}

	AnimGraphPose AnimGraphComponentBoneTransformNode::Evaluate(const AnimGraphContext& ctx,
		VansAnimGraphInstance& instance) const
	{
		return EvaluateInputPose(instance,m_NodeId,0,ctx);
	}

	std::vector<AnimGraphPin> AnimGraphComponentBoneScaleNode::GetPins() const
	{
		return {{0,"Pose",AnimGraphPinType::Pose,AnimGraphPinKind::Input},
			{0,"Result",AnimGraphPinType::Pose,AnimGraphPinKind::Output}};
	}

	AnimGraphPose AnimGraphComponentBoneScaleNode::Evaluate(const AnimGraphContext& ctx,
		VansAnimGraphInstance& instance) const
	{
		auto pose=EvaluateInputPose(instance,m_NodeId,0,ctx);
		if(ctx.preparePlayback || !pose.valid) return pose;
		if(!ctx.skeleton || pose.localPose.size()!=ctx.skeleton->bones.size())return {};
		const auto bone=ctx.skeleton->boneNameToIndex.find(m_BoneName);
		if(bone==ctx.skeleton->boneNameToIndex.end())return pose;
		float alpha=m_Alpha;
		if(!m_AlphaParameter.empty())
		{
			if(!ctx.parameters)return {};
			const auto parameter=ctx.parameters->find(m_AlphaParameter);
			if(parameter==ctx.parameters->end() || parameter->second.type!=AnimatorParamType::Float)return {};
			alpha=parameter->second.floatVal;
		}
		if(!std::isfinite(alpha))return {};
		alpha=std::clamp(alpha,0.0f,1.0f);
		if(alpha<=0)return pose;
		VansAnimationFrameVector<glm::mat4> component;
		if(!VansPoseMath::BuildModelTransforms(pose.localPose,*ctx.skeleton,component))return {};
		const size_t index=static_cast<size_t>(bone->second);
		VansBoneTransform original;
		if(!VansPoseMath::TryDecompose(component[index],original))return {};
		auto modified=original;
		modified.scale*=m_Scale;
		const glm::mat4 blended=VansPoseMath::BlendTransforms(component[index],
			VansPoseMath::Compose(modified),alpha);
		const int parent=ctx.skeleton->bones[index].parentIndex;
		const glm::mat4 local=parent<0?blended:glm::inverse(component[static_cast<size_t>(parent)])*blended;
		VansBoneTransform result;
		if(!VansPoseMath::TryDecompose(local,result))return {};
		pose.localPose[index]=result;
		return pose;
	}

	AnimGraphMultiWayBlendNode::AnimGraphMultiWayBlendNode()
	{
		m_Type = VansAnimGraphNodeType::MultiWayBlend;
		m_Name = "MultiWayBlend";
	}

	std::vector<AnimGraphPin> AnimGraphMultiWayBlendNode::GetPins() const
	{
		std::vector<AnimGraphPin> pins;
		for (int i = 0; i < static_cast<int>(m_WeightParameters.size()); ++i)
			pins.push_back({ i, "Pose " + std::to_string(i), AnimGraphPinType::Pose, AnimGraphPinKind::Input });
		pins.push_back({ 0, "Result", AnimGraphPinType::Pose, AnimGraphPinKind::Output });
		return pins;
	}

	AnimGraphPose AnimGraphMultiWayBlendNode::Evaluate(const AnimGraphContext& ctx,
		VansAnimGraphInstance& instance) const
	{
		VansAnimationFrameVector<float> desired(m_WeightParameters.size(), 0.0f);
		float total = 0.0f;
		for (size_t i = 0; i < desired.size(); ++i)
		{
			if (!ctx.parameters) continue;
			auto found = ctx.parameters->find(m_WeightParameters[i]);
			if (found != ctx.parameters->end() && found->second.type == AnimatorParamType::Float
				&& std::isfinite(found->second.floatVal))
				desired[i] = std::max(0.0f, found->second.floatVal);
			total += desired[i];
		}
		constexpr float relevantWeight = 0.00001f;
		if (total <= relevantWeight)
		{
			if (ctx.preparePlayback) return PlaybackPlaceholder();
			AnimGraphPose reference;
			if (ctx.skeleton)
			{
				VansAnimationLayerMixer::BuildBindPose(*ctx.skeleton, reference.localPose);
				reference.valid = true;
			}
			return reference;
		}
		VansAnimationFrameVector<AnimGraphPose> poses;
		VansAnimationFrameVector<float> weights;
		poses.reserve(desired.size()); weights.reserve(desired.size());
		for (size_t i = 0; i < desired.size(); ++i)
		{
			const float weight = desired[i] / total;
			if (weight <= relevantWeight) continue;
			auto pose = EvaluateInputPose(instance, m_NodeId, static_cast<int>(i), ctx, weight);
			if (!pose.valid) return {};
			poses.push_back(std::move(pose)); weights.push_back(weight);
		}
		return ctx.preparePlayback ? PlaybackPlaceholder() : VansPosePayloadMixer::BlendWeighted(poses, weights);
	}

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

		float weight = m_FixedWeight;
		if (m_UseParam && ctx.parameters)
		{
			auto it = ctx.parameters->find(m_ParamName);
			if (it != ctx.parameters->end() && it->second.type == AnimatorParamType::Float)
				weight = it->second.floatVal;
		}

		AnimGraphPose basePose = EvaluateInputPose(instance, m_NodeId, 0, ctx);
		AnimGraphPose additivePose = EvaluateInputPose(instance, m_NodeId, 1, ctx, std::clamp(weight, 0.0f, 1.0f));
		if (ctx.preparePlayback) return PlaybackPlaceholder();
		if (!basePose.valid || !additivePose.valid) return basePose;
		auto result = VansPosePayloadMixer::ApplyAdditive(basePose, additivePose, weight);
		if (m_AdditiveMode!=VansAdditivePoseMode::LocalSpherical && (!ctx.skeleton || !VansPoseMath::ApplyAdditiveDeltaPose(basePose.localPose,
			additivePose.localPose,weight,*ctx.skeleton,result.localPose,m_AdditiveMode))) return {};
		return result;
	}

	// ═════════════════════════════════════════════════════════════
	//  SpeedScaleNode
	// ═════════════════════════════════════════════════════════════

	AnimGraphInertializationNode::AnimGraphInertializationNode()
	{
		m_Type = VansAnimGraphNodeType::Inertialization;
		m_Name = "Inertialization";
	}

	std::vector<AnimGraphPin> AnimGraphInertializationNode::GetPins() const
	{
		return {{0, "Pose", AnimGraphPinType::Pose, AnimGraphPinKind::Input},
			{0, "Result", AnimGraphPinType::Pose, AnimGraphPinKind::Output}};
	}

	AnimGraphPose AnimGraphInertializationNode::Evaluate(const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		auto pose = EvaluateInputPose(instance, m_NodeId, 0, ctx);
		auto& state = instance.m_InertializationStates[m_NodeId];
		if (ctx.preparePlayback) state.deltaTime += std::max(0.0f, ctx.inputDeltaTime);
		else state.Evaluate(pose, ctx.ownerWorldTransform, m_TeleportDistance);
		return pose;
	}

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
		auto conditionsPass = [&](const AnimatorTransition& transition)
		{
			for (const TransitionCondition& condition : transition.conditions)
			{
				AnimatorParameter parameter;
				if (condition.source == AnimatorConditionSource::Parameter)
				{
					if (!ctx.parameters) { if (transition.matchAnyCondition) continue; return false; }
					const auto found = ctx.parameters->find(condition.paramName);
					if (found == ctx.parameters->end()) { if (transition.matchAnyCondition) continue; return false; }
					parameter = found->second;
				}
				else
				{
					const int id = condition.machineNodeId < 0 ? m_NodeId : condition.machineNodeId;
					const auto found = instance.m_StateMachineStates.find(id);
					parameter.type = AnimatorParamType::Float;
					parameter.floatVal = found == instance.m_StateMachineStates.end() ? 0.0f :
						(condition.source == AnimatorConditionSource::MachineWeight ? found->second.recordedWeight : found->second.currentStateElapsedTime);
				}
				bool satisfied = false;
				switch (parameter.type)
				{
				case AnimatorParamType::Float:
					switch (condition.op)
					{
					case CompareOp::Greater: satisfied = parameter.floatVal > condition.floatVal; break;
					case CompareOp::Less: satisfied = parameter.floatVal < condition.floatVal; break;
					case CompareOp::Equal: satisfied = condition.source == AnimatorConditionSource::Parameter
						? std::abs(parameter.floatVal - condition.floatVal) < 0.0001f : parameter.floatVal == condition.floatVal; break;
					case CompareOp::NotEqual: satisfied = condition.source == AnimatorConditionSource::Parameter
						? std::abs(parameter.floatVal - condition.floatVal) >= 0.0001f : parameter.floatVal != condition.floatVal; break;
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
				if (transition.matchAnyCondition && satisfied) return true;
				if (!transition.matchAnyCondition && !satisfied) return false;
			}
			return transition.conditions.empty() || !transition.matchAnyCondition;
		};
		auto startTransition = [&](const AnimatorTransition& transition, float timeAdjustment)
		{
			if (!runtime.activeTransitions.empty())
			{
				const int index = runtime.activeTransitions.back().definitionIndex;
				if (index >= 0 && index < static_cast<int>(m_Transitions.size()))
					instance.QueueStateMachineEvent(m_NodeId, m_Transitions[index].interruptEvent);
			}
			if (const auto* previous = findState(runtime.currentStateName))
				instance.QueueStateMachineEvent(m_NodeId, previous->leftEvent);
			if (const auto* next = findState(transition.toState))
				instance.QueueStateMachineEvent(m_NodeId, next->enteredEvent);
			const float targetWeight = runtime.GetStateWeight(transition.toState);
			VansAnimGraphTransitionRuntimeState active;
			active.definitionIndex = static_cast<int>(&transition - m_Transitions.data());
			active.previousStateName = runtime.currentStateName;
			active.nextStateName = transition.toState;
			active.duration = transition.inertialization ? 0.0f : std::max(0.0f, transition.blendDuration - timeAdjustment) * EvaluateTransitionAlpha(transition.durationScaleCurve, 1.0f - targetWeight);
			active.linearRotationBlend = m_LinearRotationBlend;
			active.blendCurve = transition.blendCurve;
			active.boneBlendFactors = transition.boneBlendFactors;
			if (const auto* previous = findState(runtime.currentStateName); previous && !previous->conduit)
			{
				if (transition.inertialization) runtime.inertialRequests.push_back(std::max(0.0f, transition.blendDuration));
				runtime.activeTransitions.push_back(std::move(active));
				if (!runtime.firstUpdate || !m_SkipFirstUpdateTransition)
					instance.QueueStateMachineEvent(m_NodeId, transition.startEvent);
			}
			runtime.currentStateName = transition.toState;
			runtime.currentStateElapsedTime = 0.0f;
			if (const AnimatorState* target = findState(runtime.currentStateName))
			{
				if (targetWeight <= .00001f || target->alwaysResetOnEntry)
				{
					runtime.stateTimes[target->name] = target->startTime;
					runtime.previousStateTimes[target->name] = target->startTime;
					runtime.relevantClipRemaining.erase(target->name);
					if (ctx.stagedPlayback && target->poseNodeId >= 0)
						instance.InitializeSubgraph(target->poseNodeId);
				}
			}
			if (ctx.parameters)
				for (const TransitionCondition& condition : transition.conditions)
				{
					auto parameter = ctx.parameters->find(condition.paramName);
					if (parameter != ctx.parameters->end() && parameter->second.type == AnimatorParamType::Trigger)
						parameter->second.boolVal = false;
				}
		};

		if (!ctx.synchronizedStateFollower && (!ctx.stagedPlayback || ctx.preparePlayback))
		{
			runtime.inertialRequests.clear();
			if (runtime.firstUpdate)
				if (const auto* initial = findState(runtime.currentStateName))
					instance.QueueStateMachineEvent(m_NodeId, initial->enteredEvent);
			for (int step = 0; step < m_MaxTransitionsPerFrame; ++step)
			{
				std::unordered_set<std::string> visited;
				std::vector<const AnimatorTransition*> path;
				float selectedAdjustment = 0;
				auto select = [&](auto&& self, const std::string& sourceName) -> const AnimatorTransition*
				{
					if (!visited.insert(sourceName).second) return nullptr;
					const auto* source = findState(sourceName);
					if (!source) return nullptr;
					if (!source->entryConditionParameter.empty())
					{
						if (!ctx.parameters) return nullptr;
						const auto parameter = ctx.parameters->find(source->entryConditionParameter);
						if (parameter == ctx.parameters->end() || parameter->second.type != AnimatorParamType::Bool || !parameter->second.boolVal) return nullptr;
					}
					for (const auto& transition : m_Transitions)
					{
						if (transition.fromState != sourceName && (transition.fromState != "*" || source->conduit)) continue;
						const auto* target = findState(transition.toState);
						if (!target) continue;
					if (transition.requireSourceFullyBlended && runtime.GetStateWeight(sourceName) < 1.0f) continue;
						float adjustment = 0;
						if (transition.requireRelevantClipFinished || transition.automaticRemainingTime)
						{
							auto remaining = runtime.relevantClipRemaining.find(sourceName);
							if (remaining == runtime.relevantClipRemaining.end()) continue;
							const float triggerTime = transition.automaticRemainingTime && !target->conduit ? transition.blendDuration : 0;
							if (remaining->second > triggerTime) continue;
							if (transition.automaticRemainingTime) adjustment = triggerTime - remaining->second;
						}
						if (transition.hasExitTime)
						{
							const auto clip = ctx.clips->find(source->clipName);
							if (clip != ctx.clips->end() && clip->second.duration > 0
								&& runtime.stateTimes[sourceName] / clip->second.duration < transition.exitTime) continue;
						}
						if (!transition.automaticRemainingTime && !conditionsPass(transition)) continue;
						if (target->conduit)
						{
							if (const auto* result = self(self, target->name)) { path.push_back(&transition); return result; }
						}
						else { selectedAdjustment = adjustment; path.push_back(&transition); return &transition; }
					}
					return nullptr;
				};
				const auto* selected = select(select, runtime.currentStateName);
				// 找到自身即结束本帧选择；不能继续尝试较低优先级分支。
				if (!selected || selected->toState == runtime.currentStateName) break;
				startTransition(*selected, selectedAdjustment);
				if (ctx.parameters) for (const auto* edge : path) for (const auto& condition : edge->conditions)
					if (auto parameter = ctx.parameters->find(condition.paramName); parameter != ctx.parameters->end() && parameter->second.type == AnimatorParamType::Trigger)
						parameter->second.boolVal = false;
			}
		}

		// UE先选择全部过渡，再依次推进过渡并按当时的累计权重更新尚未更新的状态。
		std::vector<std::pair<std::string, float>> updates;
		if (!ctx.stagedPlayback || ctx.preparePlayback)
		{
			if (runtime.firstUpdate && m_SkipFirstUpdateTransition) runtime.activeTransitions.clear();
			runtime.firstUpdate = false;
			if (!ctx.synchronizedStateFollower)
				runtime.currentStateElapsedTime += std::max(0.0f, ctx.inputDeltaTime >= 0 ? ctx.inputDeltaTime : ctx.deltaTime);
			auto schedule = [&](const std::string& name)
			{
				if (std::none_of(updates.begin(), updates.end(), [&](const auto& item) { return item.first == name; }))
					updates.emplace_back(name, runtime.GetStateWeight(name));
			};
			int lastFinished = -1;
			for (size_t i = 0; i < runtime.activeTransitions.size(); ++i)
			{
				auto& active = runtime.activeTransitions[i];
				if (!ctx.synchronizedStateFollower)
				{
					active.elapsedTime += std::max(0.0f, ctx.inputDeltaTime >= 0 ? ctx.inputDeltaTime : ctx.deltaTime);
					const float progress = active.duration <= 0 ? 1 : std::min(1.0f, active.elapsedTime / active.duration);
					active.alpha = EvaluateTransitionAlpha(active.blendCurve, progress);
				}
				if (active.duration <= 0 || active.elapsedTime >= active.duration)
				{
					lastFinished = static_cast<int>(i);
					if (!ctx.synchronizedStateFollower && i + 1 == runtime.activeTransitions.size())
					{
						if (active.definitionIndex >= 0 && active.definitionIndex < static_cast<int>(m_Transitions.size()))
							instance.QueueStateMachineEvent(m_NodeId, m_Transitions[active.definitionIndex].endEvent);
						if (const auto* current = findState(runtime.currentStateName))
							instance.QueueStateMachineEvent(m_NodeId, current->fullyBlendedEvent);
					}
				}
				else { schedule(active.previousStateName); schedule(active.nextStateName); }
			}
			if (lastFinished >= 0) runtime.activeTransitions.erase(runtime.activeTransitions.begin(), runtime.activeTransitions.begin() + lastFinished + 1);
			if (runtime.activeTransitions.empty()) schedule(runtime.currentStateName);
		}
		auto sampleState = [&](const AnimatorState& state, float weight) -> AnimGraphPose
		{
			if (state.conduit)
			{
				if (ctx.preparePlayback) return PlaybackPlaceholder();
				AnimGraphPose pose; VansAnimationLayerMixer::BuildBindPose(*ctx.skeleton, pose.localPose); pose.valid = true; return pose;
			}
			if (ctx.stagedPlayback && weight <= .00001f)
				return ctx.preparePlayback ? PlaybackPlaceholder() : AnimGraphPose{};
			if (state.poseNodeId >= 0)
			{
				// A state may own a complete pose subgraph (BlendSpace,
				// layered pose, or another generic node).  The state machine
				// still owns transition selection/blending; the referenced
				// node owns pose evaluation and curve output.
				AnimGraphContext stateContext = ctx;
				float speed = state.speed;
				if (ctx.parameters && !state.speedParameter.empty())
				{
					auto parameter = ctx.parameters->find(state.speedParameter);
					if (parameter != ctx.parameters->end() && parameter->second.type == AnimatorParamType::Float)
						speed *= parameter->second.floatVal;
				}
				stateContext.deltaTime *= speed;
				return EvaluateNodePose(instance, state.poseNodeId, stateContext, m_NodeId, weight);
			}
			if (ctx.preparePlayback)
			{
				if (!ctx.synchronizedStateFollower)
				{
					auto clip = ctx.clips->find(state.clipName);
					if (clip != ctx.clips->end())
					{
						float speed = state.speed;
						if (ctx.parameters && !state.speedParameter.empty())
						{
							auto parameter = ctx.parameters->find(state.speedParameter);
							if (parameter != ctx.parameters->end() && parameter->second.type == AnimatorParamType::Float)
								speed *= parameter->second.floatVal;
						}
						float& time = runtime.stateTimes[state.name];
						runtime.previousStateTimes[state.name] = time;
						const float end = state.endTime < 0 ? clip->second.duration : state.endTime;
						if (end <= state.startTime) time = runtime.previousStateTimes[state.name] = state.startTime;
						else
						{
							time += ctx.deltaTime * speed;
							if (!state.loop) time = std::clamp(time, state.startTime, end);
						}
					}
				}
				return PlaybackPlaceholder();
			}
			AnimGraphPose pose;
			if (state.clipName.empty())
			{
				VansAnimationLayerMixer::BuildBindPose(*ctx.skeleton, pose.localPose);
				pose.valid = true;
				return pose;
			}
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

		if (ctx.preparePlayback)
		{
			for (const auto& update : updates)
				if (const auto* state = findState(update.first)) sampleState(*state, update.second);
			return PlaybackPlaceholder();
		}
		std::unordered_map<std::string, AnimGraphPose> poses;
		auto poseFor = [&](const std::string& name) -> AnimGraphPose
		{
			auto cached = poses.find(name);
			if (cached != poses.end()) return cached->second;
			const auto* state = findState(name);
			auto pose = state ? sampleState(*state, runtime.GetStateWeight(name)) : AnimGraphPose{};
			poses.emplace(name, pose);
			return pose;
		};
		if (runtime.activeTransitions.empty()) return poseFor(runtime.currentStateName);
		AnimGraphPose accumulated;
		// Once a later transition has reached full weight, every earlier pose in
		// the stack is mathematically discarded.  Staged playback also stops
		// updating those zero-weight paths, so do not sample them for the blend.
		size_t firstRelevant = 0;
		for (size_t i = 0; i < runtime.activeTransitions.size(); ++i)
			if (runtime.activeTransitions[i].alpha >= .99999f) firstRelevant = i;
		if (runtime.activeTransitions[firstRelevant].alpha >= .99999f)
		{
			accumulated = poseFor(runtime.activeTransitions[firstRelevant].nextStateName);
			++firstRelevant;
		}
		for (size_t i = firstRelevant; i < runtime.activeTransitions.size(); ++i)
		{
			const auto& active = runtime.activeTransitions[i];
			if (!accumulated.valid) accumulated = poseFor(active.previousStateName);
			const float alpha = active.alpha;
			if (alpha <= .00001f) continue;
			const auto incomingPose = poseFor(active.nextStateName);
			if (alpha >= .99999f) { accumulated = incomingPose; continue; }
			// UE在整个过渡栈完成后才归一化旋转；中间四元数保持加权累积结果。
			auto blended = active.linearRotationBlend
				? VansPosePayloadMixer::BlendWeighted({ accumulated, incomingPose }, { 1.0f - alpha, alpha })
				: VansPosePayloadMixer::BlendOverride(accumulated, incomingPose, alpha);
			if (!blended.valid || !ctx.skeleton) return blended;
			for (size_t bone = 0; active.linearRotationBlend && bone < blended.localPose.size(); ++bone)
			{
				const auto& a = accumulated.localPose[bone].rotation; const auto& b = incomingPose.localPose[bone].rotation;
				blended.localPose[bone].rotation = a * (1 - alpha) + b * (glm::dot(a, b) < 0 ? -alpha : alpha);
			}
			for (const auto& factor : active.boneBlendFactors)
			{
				auto bone = ctx.skeleton->boneNameToIndex.find(factor.first);
				if (bone == ctx.skeleton->boneNameToIndex.end() || bone->second < 0
					|| static_cast<size_t>(bone->second) >= blended.localPose.size()) continue;
				const float incoming = std::max(alpha * factor.second, .00001f);
				const float outgoing = std::max((1.0f - alpha) / factor.second, .00001f);
				const float weight = incoming / (incoming + outgoing);
				const auto& a = accumulated.localPose[bone->second]; const auto& b = incomingPose.localPose[bone->second];
				auto& target = blended.localPose[bone->second];
				target.translation = glm::mix(a.translation, b.translation, weight);
				target.scale = glm::mix(a.scale, b.scale, weight);
				target.rotation = active.linearRotationBlend
					? a.rotation * (1.0f - weight) + b.rotation * (glm::dot(a.rotation, b.rotation) < 0.0f ? -weight : weight)
					: glm::normalize(glm::slerp(a.rotation, b.rotation, weight));
			}
			accumulated = std::move(blended);
		}
		for (auto& bone : accumulated.localPose) bone.rotation = glm::normalize(bone.rotation);
		return accumulated;
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
			case VansAnimGraphNodeType::MultiWayBlend: return CloneNodeAs<AnimGraphMultiWayBlendNode>(source);
			case VansAnimGraphNodeType::ModifyCurve: return CloneNodeAs<AnimGraphModifyCurveNode>(source);
			case VansAnimGraphNodeType::ComponentBoneScale: return CloneNodeAs<AnimGraphComponentBoneScaleNode>(source);
			case VansAnimGraphNodeType::ComponentBoneTransform: return CloneNodeAs<AnimGraphComponentBoneTransformNode>(source);
			case VansAnimGraphNodeType::IfCondition: return CloneNodeAs<AnimGraphIfConditionNode>(source);
			case VansAnimGraphNodeType::Switch: return CloneNodeAs<AnimGraphSwitchNode>(source);
			case VansAnimGraphNodeType::AdditiveBlend: return CloneNodeAs<AnimGraphAdditiveBlendNode>(source);
			case VansAnimGraphNodeType::Inertialization: return CloneNodeAs<AnimGraphInertializationNode>(source);
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
		const bool stagedGraph = std::any_of(m_Nodes.begin(), m_Nodes.end(), [](const auto& pair)
		{
			return RequiresStagedPlayback(*pair.second);
		});
		auto cacheWriter = [&](const VansAnimGraphNode* node) -> int
		{
			if (node->GetType() != VansAnimGraphNodeType::UseCachedPose) return -1;
			const auto& name = static_cast<const AnimGraphUseCachedPoseNode*>(node)->m_CacheName;
			for (const auto& [id, candidate] : m_Nodes)
				if (candidate->GetType() == VansAnimGraphNodeType::SaveCachedPose
					&& static_cast<const AnimGraphSaveCachedPoseNode*>(candidate.get())->m_CacheName == name) return id;
			return -1;
		};
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
			if(node->GetType()==VansAnimGraphNodeType::BlendSpace2D
				&& !static_cast<const AnimGraphBlendSpace2DNode*>(node)->ValidateSampleGrid(outError))return false;
			if (const int writer = cacheWriter(node); writer >= 0 && !visit(writer)) return false;
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
				if (stateMachine->m_MaxTransitionsPerFrame < 1)
				{ outError = "State machine requires a positive transition limit"; return false; }
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

		// 每条路径都必须有接收器，不能把惯性化静默降级成普通交叉混合。
		std::set<std::pair<int, bool>> inertialPaths;
		auto validateInertia = [&](auto&& self, int id, bool hasReceiver) -> bool
		{
			if (!inertialPaths.emplace(id, hasReceiver).second) return true;
			const auto* node = GetNode(id);
			if (node->GetType() == VansAnimGraphNodeType::Inertialization)
			{
				const float distance = static_cast<const AnimGraphInertializationNode*>(node)->m_TeleportDistance;
				if (!std::isfinite(distance) || distance < 0) { outError = "Invalid inertialization teleport distance"; return false; }
				hasReceiver = true;
			}
			if (node->GetType() == VansAnimGraphNodeType::StateMachine)
			{
				const auto* machine = static_cast<const AnimGraphStateMachineNode*>(node);
				for (const auto& transition : machine->m_Transitions)
					if (transition.inertialization && (!hasReceiver || !transition.boneBlendFactors.empty()))
					{ outError = "Inertial transition requires a downstream Inertialization node and no unsupported bone duration profile"; return false; }
				for (const auto& state : machine->m_States)
					if (state.poseNodeId >= 0 && !self(self, state.poseNodeId, hasReceiver)) return false;
			}
			if (const int writer = cacheWriter(node); writer >= 0 && !self(self, writer, hasReceiver)) return false;
			for (const auto& link : m_Links)
				if (link.toNodeId == id && !self(self, link.fromNodeId, hasReceiver)) return false;
			return true;
		};
		if (!validateInertia(validateInertia, m_OutputNodeId, false)) return false;

		// 同步空间独占其样本时钟，禁止一个 Clip 同时成为普通播放器或其他空间的样本。
		for (const auto& [id, node] : m_Nodes)
		{
			if (node->GetType() == VansAnimGraphNodeType::LayeredBlendPerBone)
			{
				const auto& layer = *static_cast<const AnimGraphLayeredBlendPerBoneNode*>(node.get());
				if (layer.m_MeshSpaceRotationOnly && (layer.m_RotationSpace != VansRotationBlendSpace::Mesh
					|| layer.m_BlendMode != VansLayerBlendMode::Override || layer.m_ApplyAdditiveInput))
				{ outError = "Mesh rotation-only blend requires an override layer without additive input"; outPlan.clear(); return false; }
			}
			if (node->GetType() == VansAnimGraphNodeType::Blend)
			{
				const auto& blend = *static_cast<const AnimGraphBlendNode*>(node.get());
				if ((blend.m_MapAlpha && (!std::isfinite(blend.m_AlphaInMin) || !std::isfinite(blend.m_AlphaInMax)
					|| !std::isfinite(blend.m_AlphaOutMin) || !std::isfinite(blend.m_AlphaOutMax) || blend.m_AlphaInMin == blend.m_AlphaInMax))
					|| (blend.m_InterpolateAlpha && (!std::isfinite(blend.m_AlphaSpeedIncreasing) || !std::isfinite(blend.m_AlphaSpeedDecreasing)
						|| blend.m_AlphaSpeedIncreasing < 0 || blend.m_AlphaSpeedDecreasing < 0)))
				{ outError = "Blend alpha mapping/interpolation values invalid"; outPlan.clear(); return false; }
			}
			if (node->GetType() == VansAnimGraphNodeType::Clip)
			{
				const auto* clip = static_cast<const AnimGraphClipNode*>(node.get());
				if (!std::isfinite(clip->m_SampleTime) || (clip->m_SampleTime < 0 && clip->m_SampleTime != -1))
				{ outError = "Explicit sample time must be nonnegative or disabled (-1)"; outPlan.clear(); return false; }
				if (!clip->m_LoopParameter.empty() && clip->HasExplicitSampleTime())
				{ outError = "Explicit sample Clip cannot bind a loop parameter"; outPlan.clear(); return false; }
				if (clip->m_SyncGroup.empty()) continue;
				if (clip->HasExplicitSampleTime() || !std::isfinite(clip->m_Speed) || clip->m_Speed < 0)
				{ outError = "Sync group Clip must have nonnegative speed and no explicit sample time"; outPlan.clear(); return false; }
			}
			else if (node->GetType() == VansAnimGraphNodeType::BlendSpace2D)
			{
				const auto* blend = static_cast<const AnimGraphBlendSpace2DNode*>(node.get());
				if (blend->m_SyncGroup.empty()) continue;
				if (!blend->m_SynchronizeSamples)
				{ outError = "Sync group BlendSpace must own its sample clocks"; outPlan.clear(); return false; }
			}
		}
		if (stagedGraph)
			for (const auto& [id, node] : m_Nodes)
				if (node->GetType() == VansAnimGraphNodeType::MotionMatching)
				{ outError = "MotionMatching cannot participate in a staged sync-group graph"; outPlan.clear(); return false; }
		for (const auto& [nodeId, node] : m_Nodes)
		{
			if (!node || node->GetType() != VansAnimGraphNodeType::BlendSpace2D) continue;
			const auto* blend = static_cast<const AnimGraphBlendSpace2DNode*>(node.get());
			if (!std::isfinite(blend->m_StartPosition) || blend->m_StartPosition < 0 || blend->m_StartPosition > 1
				|| (blend->m_SynchronizeSamples && (!blend->m_SampleGrid.IsEnabled() && (!blend->m_BilinearGrid || blend->m_Samples.size() != 4))))
			{ outError = "BlendSpace start position or synchronized grid is invalid"; outPlan.clear(); return false; }
			if (!blend->m_SynchronizeSamples) continue;
			for (int i = 0; i < static_cast<int>(blend->m_Samples.size()); ++i)
			{
				const auto* input = GetInputNode(nodeId, i);
				if (!input || input->GetType() != VansAnimGraphNodeType::Clip)
				{ outError = "Synchronized BlendSpace requires direct Clip inputs"; outPlan.clear(); return false; }
				const auto* clip = static_cast<const AnimGraphClipNode*>(input);
				bool exclusive = std::count_if(m_Links.begin(), m_Links.end(), [&](const auto& link)
					{ return link.fromNodeId == input->GetNodeId(); }) == 1;
				for (const auto& [otherId, other] : m_Nodes)
					if (other && other->GetType() == VansAnimGraphNodeType::StateMachine)
						for (const auto& state : static_cast<const AnimGraphStateMachineNode*>(other.get())->m_States)
							exclusive = exclusive && state.poseNodeId != input->GetNodeId();
				if (!exclusive || !clip->m_Loop || !clip->m_LoopParameter.empty() || clip->HasExplicitSampleTime() || !clip->m_SyncGroup.empty()
					|| !std::isfinite(clip->m_Speed) || clip->m_Speed < 0)
				{ outError = "Synchronized BlendSpace requires exclusive looping Clips with nonnegative speed"; outPlan.clear(); return false; }
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
			if (const int writer = cacheWriter(node); writer >= 0 && !validateSpeedPath(writer, speedPath)) return false;
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
			const auto* node = m_Definition.GetNode(nodeId);
			m_UsesStagedPlayback = m_UsesStagedPlayback || (node && RequiresStagedPlayback(*node));
			if (node && node->GetType() == VansAnimGraphNodeType::BlendSpace2D)
			{
				const auto* blend = static_cast<const AnimGraphBlendSpace2DNode*>(node);
				if (blend->m_SynchronizeSamples)
					for (int i = 0; i < static_cast<int>(blend->m_Samples.size()); ++i)
						if (const auto* input = m_Definition.GetInputNode(nodeId, i))
							m_SynchronizedSampleOwners.emplace(input->GetNodeId(), nodeId);
			}
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

	AnimGraphPose VansAnimGraphInstance::Evaluate(const AnimGraphContext& inputContext)
	{
		m_StateMachineEvents.clear();
		AnimGraphContext ctx = inputContext;
		if (ctx.inputDeltaTime < 0) ctx.inputDeltaTime = ctx.deltaTime;
		if (!IsCompiled())
			return {};
		ctx.stagedPlayback = m_UsesStagedPlayback;
		if (m_UsesStagedPlayback)
		{
			m_InitializedThisFrame.clear();
			m_PlaybackEdges.clear(); m_PlaybackWeights.clear(); m_PlayerDeltaTimes.clear();
			m_PreparedSampleWeights.clear(); m_SyncFollowers.clear(); m_CachedPoses.clear();
			for (int nodeId : m_ExecutionPlan) { m_EvaluatedNodes[nodeId] = false; m_EvaluatingNodes[nodeId] = false; }
			if (!m_InitializedNodes.count(m_Definition.GetOutputNodeId()))
				InitializeSubgraph(m_Definition.GetOutputNodeId());
			ctx.preparePlayback = true;
			EvaluateNode(m_Definition.GetOutputNodeId(), ctx);
			RouteInertializationRequests();
			TickSyncGroups(ctx);
			ctx.preparePlayback = false;
		}
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
			if (!m_UsesStagedPlayback && node && node->GetType() == VansAnimGraphNodeType::SaveCachedPose)
				EvaluateNode(nodeId, ctx);
		}
		AnimGraphPose result = EvaluateNode(m_Definition.GetOutputNodeId(), ctx);
		// State lifecycle notifications belong to the update, not to blended poses.
		// Publish once, after evaluation, in update order and before sampled clip events.
		result.events.insert(result.events.begin(), m_StateMachineEvents.begin(), m_StateMachineEvents.end());
		for (int nodeId : m_ExecutionPlan)
			m_PreviousActiveNodes[nodeId] = m_EvaluatedNodes[nodeId];
		return result;
	}

	AnimGraphPose VansAnimGraphInstance::EvaluateFrame(const AnimGraphContext& ctx)
	{
		AdvanceTime(ctx.deltaTime, ctx);
		return Evaluate(ctx);
	}

	void VansAnimGraphInstance::QueueStateMachineEvent(int nodeId, std::string_view name)
	{
		if (name.empty()) return;
		VansAnimationEventSample event;
		event.id = VansAnimationStableId(name);
		event.name = name; // immutable graph definition owns this string
		event.sourceNodeId = static_cast<std::uint64_t>(nodeId);
		m_StateMachineEvents.push_back(event);
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
		if (ctx.preparePlayback && node)
		{
			m_PlayerDeltaTimes[nodeId] = ctx.deltaTime;
			// 权重恢复本身不初始化播放器；状态机重新相关时才重新进入默认状态。
			if (node->GetType() == VansAnimGraphNodeType::StateMachine
				&& m_UpdatedNodes.count(nodeId) && !m_PreviousActiveNodes[nodeId]
				&& !m_InitializedThisFrame.count(nodeId)) InitializeSubgraph(nodeId);
			if (node->GetType() == VansAnimGraphNodeType::Clip)
			{
				const auto& clip = *static_cast<const AnimGraphClipNode*>(node);
				if (clip.m_SyncGroup.empty() && !clip.HasExplicitSampleTime() && !m_SynchronizedSampleOwners.count(nodeId))
				{
					auto& clock = GetClipState(nodeId);
					clip.AdvancePlayback(clock, ctx.deltaTime, ctx);
				}
			}
		}
		AnimGraphPose result = node ? node->Evaluate(ctx, *this) : AnimGraphPose{};
		if (ctx.preparePlayback) m_UpdatedNodes.insert(nodeId);
		if (!ctx.preparePlayback && m_SyncFollowers.count(nodeId)) result.events.clear();
		m_EvaluatingNodes[nodeId] = false;
		m_EvaluationCache.at(nodeId) = result;
		m_EvaluatedNodes[nodeId] = true;
		return m_EvaluationCache.at(nodeId);
	}

	AnimGraphPose VansAnimGraphInstance::EvaluateInput(
		int nodeId, int inputPinIndex, const AnimGraphContext& ctx, float weight)
	{
		const VansAnimGraphNode* input = m_Definition.GetInputNode(nodeId, inputPinIndex);
		return input ? EvaluateWeightedNode(nodeId, input->GetNodeId(), weight, ctx) : AnimGraphPose{};
	}

	AnimGraphPose VansAnimGraphInstance::EvaluateWeightedNode(int parentId, int nodeId, float weight, const AnimGraphContext& ctx)
	{
		if (ctx.stagedPlayback && (!std::isfinite(weight) || weight <= .00001f))
			return ctx.preparePlayback ? PlaybackPlaceholder() : AnimGraphPose{};
		if (ctx.preparePlayback && parentId >= 0) m_PlaybackEdges[parentId].push_back({nodeId, weight});
		return EvaluateNode(nodeId, ctx);
	}

	void VansAnimGraphInstance::InitializeSubgraph(int nodeId)
	{
		if (!m_UsesStagedPlayback) return;
		std::unordered_set<int> visited;
		auto initialize = [&](auto&& self, int id) -> void
		{
			const auto* node = m_Definition.GetNode(id);
			if (!node || !visited.insert(id).second) return;
			// UE SaveCachedPose: 初始化计数一致且尚未更新过/仍相关时，拦截状态进入的重置。
			if (node->GetType() == VansAnimGraphNodeType::SaveCachedPose && m_InitializedNodes.count(id)
				&& (!m_UpdatedNodes.count(id) || m_PreviousActiveNodes[id] || m_EvaluatedNodes[id])) return;
			m_InitializedNodes.insert(id);
			m_InitializedThisFrame.insert(id);
			m_ClipStates.erase(id);
			m_BlendSpaceStates.erase(id);
			m_BlendAlphaStates.erase(id);
			m_InertializationStates.erase(id);
			if (node->GetType() == VansAnimGraphNodeType::StateMachine)
			{
				const auto previous = m_StateMachineStates.find(id);
				const float recordedWeight = previous == m_StateMachineStates.end() ? 0.0f : previous->second.recordedWeight;
				m_StateMachineStates.erase(id);
				const auto& machine = *static_cast<const AnimGraphStateMachineNode*>(node);
				GetStateMachineState(id, machine).recordedWeight = recordedWeight;
				for (const auto& state : machine.m_States)
					if (state.name == machine.m_DefaultStateName && state.poseNodeId >= 0) self(self, state.poseNodeId);
				return;
			}
			if (node->GetType() == VansAnimGraphNodeType::UseCachedPose)
			{
				const auto& name = static_cast<const AnimGraphUseCachedPoseNode*>(node)->m_CacheName;
				for (const auto& [writerId, writer] : m_Definition.GetNodes())
					if (writer->GetType() == VansAnimGraphNodeType::SaveCachedPose
						&& static_cast<const AnimGraphSaveCachedPoseNode*>(writer.get())->m_CacheName == name)
					{ self(self, writerId); break; }
			}
			for (const auto& link : m_Definition.GetLinks())
				if (link.toNodeId == id) self(self, link.fromNodeId);
		};
		initialize(initialize, nodeId);
	}

	void VansAnimGraphInstance::RouteInertializationRequests()
	{
		// 沿本帧有效路径查找最近的下游接收器；缓存读分支也转发被跳过更新的请求。
		std::set<std::pair<int,int>> visited;
		auto route = [&](auto&& self, int id, int receiver) -> void
		{
			if (!visited.emplace(id, receiver).second) return;
			const auto* node = m_Definition.GetNode(id);
			if (node && node->GetType() == VansAnimGraphNodeType::Inertialization) receiver = id;
			if (receiver >= 0)
				if (const auto machine = m_StateMachineStates.find(id); machine != m_StateMachineStates.end())
				{
					auto& requests = m_InertializationStates[receiver].requests;
					requests.insert(requests.end(), machine->second.inertialRequests.begin(), machine->second.inertialRequests.end());
				}
			for (const auto& edge : m_PlaybackEdges[id]) self(self, edge.child, receiver);
		};
		route(route, m_Definition.GetOutputNodeId(), -1);
	}

	void VansAnimGraphInstance::TickSyncGroups(const AnimGraphContext& ctx)
	{
		if (!ctx.clips) return;
		for (int id : m_ExecutionPlan)
		{
			const auto* node = m_Definition.GetNode(id);
			if (!m_EvaluatedNodes[id] || !node || node->GetType() != VansAnimGraphNodeType::Clip) continue;
			auto& clock = GetClipState(id);
			if (clock.startPositionPending) static_cast<const AnimGraphClipNode*>(node)->AdvancePlayback(clock, 0, ctx);
		}
		// 缓存子图取最高有效路径权重；重复引用不能重复推进播放器。
		auto propagate = [&](auto&& self, int id, float weight) -> void
		{
			if (weight <= .00001f || weight <= m_PlaybackWeights[id]) return;
			m_PlaybackWeights[id] = weight;
			for (const auto& edge : m_PlaybackEdges[id]) self(self, edge.child, weight * edge.weight);
		};
		propagate(propagate, m_Definition.GetOutputNodeId(), 1);
		// 与 UE 的权重双缓冲一致：本帧条件读取旧记录，更新后才发布本帧权重。
		for (auto& [id, machine] : m_StateMachineStates)
		{
			const auto weight = m_PlaybackWeights.find(id);
			machine.recordedWeight = weight == m_PlaybackWeights.end() ? 0.0f : weight->second;
		}
		std::unordered_map<std::string, std::vector<int>> groups;
		auto groupName = [&](int id) -> std::string
		{
			const auto* node = m_Definition.GetNode(id);
			if (node->GetType() == VansAnimGraphNodeType::Clip) return static_cast<const AnimGraphClipNode*>(node)->m_SyncGroup;
			if (node->GetType() == VansAnimGraphNodeType::BlendSpace2D) return static_cast<const AnimGraphBlendSpace2DNode*>(node)->m_SyncGroup;
			return {};
		};
		auto sampleNode = [&](int id, bool markedOnly) -> const AnimGraphClipNode*
		{
			const auto* node = m_Definition.GetNode(id);
			if (node->GetType() == VansAnimGraphNodeType::Clip) return static_cast<const AnimGraphClipNode*>(node);
			const auto found = m_PreparedSampleWeights.find(id);
			if (found == m_PreparedSampleWeights.end()) return nullptr;
			const AnimGraphClipNode* best = nullptr;
			float weight = 0;
			for (int i = 0; i < static_cast<int>(found->second.size()); ++i)
			{
				const auto* clip = static_cast<const AnimGraphClipNode*>(m_Definition.GetInputNode(id, i));
				const auto asset = ctx.clips->find(clip->m_ClipName);
				if (asset == ctx.clips->end() || (markedOnly && asset->second.syncMarkers.empty())) continue;
				if (found->second[i] > weight) { weight = found->second[i]; best = clip; }
			}
			return best;
		};
		for (int id : m_ExecutionPlan)
		{
			if (m_PlaybackWeights[id] <= .00001f) continue;
			const auto name = groupName(id);
			if (!name.empty()) groups[name].push_back(id);
			else if (m_PreparedSampleWeights.count(id))
			{
				AnimGraphContext playerContext = ctx; playerContext.deltaTime = m_PlayerDeltaTimes[id];
				const auto& weights = m_PreparedSampleWeights.at(id);
				VansAnimationFrameVector<float> frameWeights; frameWeights.assign(weights.begin(), weights.end());
				static_cast<const AnimGraphBlendSpace2DNode*>(m_Definition.GetNode(id))->SynchronizeSampleTimes(
					playerContext, *this, frameWeights);
			}
		}
		std::unordered_map<std::string, VansAnimGraphSyncGroupState> nextGroups;
		for (auto& [name, players] : groups)
		{
			std::stable_sort(players.begin(), players.end(), [&](int a, int b) { return m_PlaybackWeights[a] > m_PlaybackWeights[b]; });
			const auto external = m_ExternalSyncGroups.find(name);
			if (external != m_ExternalSyncGroups.end())
			{
				const auto source = ctx.clips->find(external->second.leaderClip);
				if (source != ctx.clips->end() && source->second.duration > 0)
				{
					const auto& leaderClip = source->second;
					bool markerSync = !leaderClip.syncMarkers.empty();
					for (int player : players)
					{
						const auto* sample = sampleNode(player, true);
						if (!sample || !sample->ResolveLoop(ctx)) { markerSync = false; break; }
						const auto asset = ctx.clips->find(sample->m_ClipName);
						float mapped = 0;
						if (asset == ctx.clips->end() || asset->second.syncMarkers.empty()
							|| !MapLoopMarkerTime(leaderClip, asset->second, 0, mapped))
						{ markerSync = false; break; }
					}
					auto follow = [&](const AnimGraphClipNode& sample)
					{
						const auto asset = ctx.clips->find(sample.m_ClipName);
						if (asset == ctx.clips->end()) return;
						auto& clock = GetClipState(sample.GetNodeId());
						if (!markerSync
							|| !MapLoopMarkerTime(leaderClip, asset->second,
								external->second.previousTime, clock.previousTime)
							|| !MapLoopMarkerTime(leaderClip, asset->second,
								external->second.currentTime, clock.currentTime))
						{
							clock.previousTime = external->second.previousTime / leaderClip.duration * asset->second.duration;
							clock.currentTime = external->second.currentTime / leaderClip.duration * asset->second.duration;
						}
						clock.startPositionPending = false;
						clock.looping = sample.ResolveLoop(ctx);
						if (!clock.looping)
						{
							clock.previousTime = std::clamp(clock.previousTime, 0.0f, asset->second.duration);
							clock.currentTime = std::clamp(clock.currentTime, 0.0f, asset->second.duration);
						}
					};
					for (int player : players)
					{
						m_SyncFollowers[player] = true;
						if (m_PreparedSampleWeights.count(player))
						{
							auto& state = m_BlendSpaceStates[player];
							state.playbackInitialized = true; state.activeSamples.clear(); state.markerLeader = -1;
							const auto* highest = sampleNode(player, false);
							const auto* marked = sampleNode(player, true);
							for (int i = 0; i < static_cast<int>(m_PreparedSampleWeights[player].size()); ++i)
							{
								if (m_PreparedSampleWeights[player][i] <= 0) continue;
								const auto* sample = static_cast<const AnimGraphClipNode*>(m_Definition.GetInputNode(player, i));
								follow(*sample); state.activeSamples.push_back(i);
								if (markerSync && sample == marked) state.markerLeader = i;
								if (sample == highest)
								{
									const float duration = ctx.clips->at(sample->m_ClipName).duration;
									state.normalizedTime = VansAnimationSampler::ResolveSampleTime(
										GetClipState(sample->GetNodeId()).currentTime, 0, duration, true) / duration;
								}
							}
						}
						else if (const auto* sample = sampleNode(player, false)) follow(*sample);
					}
					nextGroups[name] = {players.front(), external->second.leaderClip,
						external->second.previousTime, external->second.currentTime};
					continue;
				}
			}
			const int leader = players.front();
			const auto* leaderSample = sampleNode(leader, true);
			if (!leaderSample) leaderSample = sampleNode(leader, false);
			if (!leaderSample) continue;
			const auto leaderAsset = ctx.clips->find(leaderSample->m_ClipName);
			if (leaderAsset == ctx.clips->end() || leaderAsset->second.duration <= 0) continue;
			const auto& leaderClip = leaderAsset->second;
			bool markerSync = leaderSample->ResolveLoop(ctx) && !leaderClip.syncMarkers.empty();
			for (int player : players)
			{
				const auto* sample = sampleNode(player, true);
				if (!sample) continue;
				markerSync = markerSync && sample->ResolveLoop(ctx);
				const auto asset = ctx.clips->find(sample->m_ClipName);
				float ignored;
				if (asset != ctx.clips->end() && !asset->second.syncMarkers.empty())
					markerSync = markerSync && MapLoopMarkerTime(leaderClip, asset->second, 0, ignored);
			}
			const auto previousGroup = m_SyncGroups.find(name);
			if (markerSync && previousGroup != m_SyncGroups.end())
			{
				const auto source = ctx.clips->find(previousGroup->second.leaderClip);
				float mapped;
				if (source != ctx.clips->end() && MapLoopMarkerTime(source->second, leaderClip, previousGroup->second.currentTime, mapped))
				{
					auto& leaderClock = GetClipState(leaderSample->GetNodeId());
					leaderClock.currentTime = mapped;
					leaderClock.startPositionPending = false;
					if (m_PreparedSampleWeights.count(leader))
					{
						auto& state = m_BlendSpaceStates[leader];
						state.playbackInitialized = true;
						state.normalizedTime = VansAnimationSampler::ResolveSampleTime(mapped, 0, leaderClip.duration, true)/leaderClip.duration;
						for (int i = 0; i < static_cast<int>(m_PreparedSampleWeights[leader].size()); ++i)
							if (m_Definition.GetInputNode(leader, i) == leaderSample)
							{ state.markerLeader = i; state.activeSamples.push_back(i); break; }
					}
				}
			}
			if (m_PreparedSampleWeights.count(leader))
			{
				AnimGraphContext playerContext = ctx; playerContext.deltaTime = m_PlayerDeltaTimes[leader];
				const auto& weights = m_PreparedSampleWeights.at(leader);
				VansAnimationFrameVector<float> frameWeights; frameWeights.assign(weights.begin(), weights.end());
				static_cast<const AnimGraphBlendSpace2DNode*>(m_Definition.GetNode(leader))->SynchronizeSampleTimes(
					playerContext, *this, frameWeights, markerSync);
			}
			else
			{
				auto& clock = GetClipState(leader);
				leaderSample->AdvancePlayback(clock, m_PlayerDeltaTimes[leader], ctx, true);
			}
			const auto leaderClock = GetClipState(leaderSample->GetNodeId());
			nextGroups[name] = {leader, leaderSample->m_ClipName, leaderClock.previousTime, leaderClock.currentTime};
			auto follow = [&](const AnimGraphClipNode& sample)
			{
				const auto asset = ctx.clips->find(sample.m_ClipName);
				if (asset == ctx.clips->end()) return;
				auto& clock = GetClipState(sample.GetNodeId());
				if (!markerSync || !MapLoopMarkerTime(leaderClip, asset->second, leaderClock.previousTime, clock.previousTime)
					|| !MapLoopMarkerTime(leaderClip, asset->second, leaderClock.currentTime, clock.currentTime))
				{
					clock.previousTime = leaderClock.previousTime / leaderClip.duration * asset->second.duration;
					clock.currentTime = leaderClock.currentTime / leaderClip.duration * asset->second.duration;
				}
				clock.startPositionPending = false;
				clock.looping = sample.ResolveLoop(ctx);
				if (!clock.looping)
				{
					clock.previousTime = std::clamp(clock.previousTime, 0.0f, asset->second.duration);
					clock.currentTime = std::clamp(clock.currentTime, 0.0f, asset->second.duration);
				}
			};
			for (size_t p = 1; p < players.size(); ++p)
			{
				const int player = players[p]; m_SyncFollowers[player] = true;
				if (m_PreparedSampleWeights.count(player))
				{
					auto& state = m_BlendSpaceStates[player];
					state.playbackInitialized = true; state.activeSamples.clear(); state.markerLeader = -1;
					const auto* highest = sampleNode(player, false);
					const auto* marked = sampleNode(player, true);
					for (int i = 0; i < static_cast<int>(m_PreparedSampleWeights[player].size()); ++i)
					{
						if (m_PreparedSampleWeights[player][i] <= 0) continue;
						const auto* sample = static_cast<const AnimGraphClipNode*>(m_Definition.GetInputNode(player, i));
						follow(*sample); state.activeSamples.push_back(i);
						if (markerSync && sample == marked) state.markerLeader = i;
						if (sample == highest)
						{
							const float duration = ctx.clips->at(sample->m_ClipName).duration;
							state.normalizedTime = VansAnimationSampler::ResolveSampleTime(GetClipState(sample->GetNodeId()).currentTime, 0, duration, true)/duration;
						}
					}
				}
				else if (const auto* sample = sampleNode(player, false)) follow(*sample);
			}
		}
		m_SyncGroups = std::move(nextGroups);
		// 下一帧过渡规则读取本帧实际更新的最高权重Clip，而非状态经过时间。
		for (auto& [id, runtime] : m_StateMachineStates)
		{
			if (!m_EvaluatedNodes[id]) continue;
			const auto& machine = *static_cast<const AnimGraphStateMachineNode*>(m_Definition.GetNode(id));
			for (const auto& state : machine.m_States)
			{
				if (state.conduit) continue;
				if (std::none_of(machine.m_Transitions.begin(), machine.m_Transitions.end(), [&](const auto& transition)
					{ return (transition.requireRelevantClipFinished || transition.automaticRemainingTime) && (transition.fromState == state.name || transition.fromState == "*"); })) continue;
				if (state.poseNodeId < 0)
				{
					const auto clip = ctx.clips->find(state.clipName);
					if (clip != ctx.clips->end() && runtime.stateTimes.count(state.name))
						runtime.relevantClipRemaining[state.name] = std::max(0.0f, clip->second.duration - VansAnimationSampler::ResolveSampleTime(runtime.stateTimes.at(state.name),0,clip->second.duration,state.loop));
					continue;
				}
				float bestWeight = 0, remaining = 0;
				auto find = [&](auto&& self, int nodeId, float weight) -> void
				{
					if (weight <= .00001f) return;
					const auto* node = m_Definition.GetNode(nodeId);
					// 状态外缓存播放器不属于该状态的 Relevant Asset Player 集合。
					if (node && (node->GetType() == VansAnimGraphNodeType::SaveCachedPose || node->GetType() == VansAnimGraphNodeType::UseCachedPose)) return;
					if (node && node->GetType() == VansAnimGraphNodeType::Clip && weight > bestWeight)
					{
						const auto& clipNode = *static_cast<const AnimGraphClipNode*>(node);
						const auto clip = ctx.clips->find(clipNode.m_ClipName);
						if (clip != ctx.clips->end())
						{
							float sampleTime = 0.0f;
							if (clipNode.HasExplicitSampleTime())
							{
								float explicitTime = clipNode.m_SampleTime;
								if (!clipNode.m_SampleTimeParameter.empty())
								{
								// 显式采样播放器同样参加状态相关性；不推进时钟，但保留源资产剩余时间。
								if (!ctx.parameters) return;
								const auto parameter = ctx.parameters->find(clipNode.m_SampleTimeParameter);
								if (parameter == ctx.parameters->end() || parameter->second.type != AnimatorParamType::Float
									|| !std::isfinite(parameter->second.floatVal)) return;
								explicitTime = parameter->second.floatVal;
								}
								sampleTime = std::clamp(explicitTime, 0.0f, clip->second.duration);
							}
							else
							{
								if (!m_ClipStates.count(nodeId)) return;
								sampleTime = VansAnimationSampler::ResolveSampleTime(
									m_ClipStates.at(nodeId).currentTime, 0, clip->second.duration, clipNode.ResolveLoop(ctx));
								if (!clipNode.m_LoopParameter.empty() && m_ClipStates.at(nodeId).currentTime == clip->second.duration)
									sampleTime = clip->second.duration;
							}
							bestWeight = weight;
							remaining = std::max(0.0f, clip->second.duration - sampleTime);
						}
					}
					for (const auto& edge : m_PlaybackEdges[nodeId]) self(self, edge.child, weight*edge.weight);
				};
				find(find, state.poseNodeId, 1);
				if (bestWeight > 0) runtime.relevantClipRemaining[state.name] = remaining;
			}
		}
	}

	void VansAnimGraphInstance::AdvanceTime(float deltaTime, const AnimGraphContext& ctx)
	{
		// 分阶段图只推进本帧真正参与更新的播放器；不能提前推进上一帧已退出的节点。
		if (!IsCompiled() || m_UsesStagedPlayback)
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
				for (const auto& active : runtime.activeTransitions)
				{
					propagateStatePose(active.previousStateName);
					propagateStatePose(active.nextStateName);
				}
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
				if (clipNode->HasExplicitSampleTime() || !clipNode->m_SyncGroup.empty() || m_SynchronizedSampleOwners.count(nodeId))
					continue;
				VansAnimGraphClipRuntimeState& state = GetClipState(nodeId);
				const float timeScale = m_HasActiveTimeScale[nodeId]
					? m_ActiveTimeScales[nodeId] : 1.0f;
				clipNode->AdvancePlayback(state, deltaTime * timeScale, ctx);
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
			std::unordered_set<std::string> advanced;
			auto advanceOnce = [&](const std::string& name)
			{
				if (advanced.insert(name).second) advanceState(findState(name));
			};
			advanceOnce(runtime.currentStateName);
			for (const auto& active : runtime.activeTransitions)
			{
				advanceOnce(active.previousStateName);
				advanceOnce(active.nextStateName);
			}
		}
	}

	void VansAnimGraphInstance::Reset()
	{
		m_InitializedNodes.clear(); m_UpdatedNodes.clear(); m_InitializedThisFrame.clear();
		m_ClipStates.clear();
		m_StateMachineStates.clear();
		m_BlendSpaceStates.clear();
		m_BlendAlphaStates.clear();
		m_InertializationStates.clear();
		m_SyncGroups.clear(); m_ExternalSyncGroups.clear(); m_SyncFollowers.clear();
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
		if (m_UsesStagedPlayback && !m_InitializedNodes.count(m_Definition.GetOutputNodeId()))
			InitializeSubgraph(m_Definition.GetOutputNodeId());
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
				runtime.currentStateElapsedTime = 0.0f;
				runtime.activeTransitions.clear();
					runtime.stateTimes[stateName] = state.startTime;
					runtime.previousStateTimes[stateName] = state.startTime;
					m_PreviousActiveNodes[nodeId] = true;
					if (state.poseNodeId >= 0) InitializeSubgraph(state.poseNodeId);
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
			for (auto active = runtimeIt->second.activeTransitions.rbegin(); active != runtimeIt->second.activeTransitions.rend(); ++active)
			{
				path += "<-";
				path += active->previousStateName;
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
		// A Graph Set may hand off phase before its incoming graph has evaluated.
		// Initialize the staged graph first; otherwise its first Evaluate clears
		// the clip clock that receives the handoff time below.
		if (m_UsesStagedPlayback
			&& !m_InitializedNodes.count(m_Definition.GetOutputNodeId()))
			InitializeSubgraph(m_Definition.GetOutputNodeId());
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
					runtime.currentStateElapsedTime = 0.0f;
					runtime.activeTransitions.clear();
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
				runtime.startPositionPending = false;
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
		for (const auto& active : leaderRuntime->activeTransitions)
			if (!findState(*followerDefinition, active.previousStateName) || !findState(*followerDefinition, active.nextStateName))
				return false;

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

		// 外部状态同步必须在首次图初始化之后提交，否则首帧会被默认状态覆盖。
		if (m_UsesStagedPlayback && !m_InitializedNodes.count(m_Definition.GetOutputNodeId()))
			InitializeSubgraph(m_Definition.GetOutputNodeId());
		VansAnimGraphStateMachineRuntimeState& followerRuntime =
			GetStateMachineState(followerNodeId, *followerDefinition);
		followerRuntime.currentStateName = leaderRuntime->currentStateName;
		followerRuntime.activeTransitions = leaderRuntime->activeTransitions;
		followerRuntime.inertialRequests = leaderRuntime->inertialRequests;
		followerRuntime.currentStateElapsedTime = leaderRuntime->currentStateElapsedTime;
		followerRuntime.firstUpdate = false;
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
		for (const auto& active : leaderRuntime->activeTransitions)
			for (const auto& name : {active.previousStateName, active.nextStateName})
				if (const auto* source = findState(*leaderDefinition, name))
					if (const auto* target = findState(*followerDefinition, name)) synchronizeTimes(*source, *target);
		m_PreviousActiveNodes[followerNodeId] = true;
		return true;
	}

	VansAnimGraphRuntimeStateSnapshot VansAnimGraphInstance::CaptureRuntimeState() const
	{
		VansAnimGraphRuntimeStateSnapshot snapshot;
		snapshot.clipStates = m_ClipStates;
		snapshot.stateMachineStates = m_StateMachineStates;
		snapshot.blendSpaceStates = m_BlendSpaceStates;
		snapshot.blendAlphaStates = m_BlendAlphaStates;
		snapshot.inertializationStates = m_InertializationStates;
		snapshot.activeNodes = m_PreviousActiveNodes;
		snapshot.initializedNodes = m_InitializedNodes;
		snapshot.updatedNodes = m_UpdatedNodes;
		snapshot.syncGroups = m_SyncGroups;
		return snapshot;
	}

	bool VansAnimGraphInstance::RestoreRuntimeState(
		const VansAnimGraphRuntimeStateSnapshot& snapshot)
	{
		bool fullyCompatible = true;
		m_BlendAlphaStates.clear();
		m_InertializationStates.clear();
		for (const auto& [id, state] : snapshot.inertializationStates)
			if (const auto* node = m_Definition.GetNode(id); node && node->GetType() == VansAnimGraphNodeType::Inertialization && state.IsFinite())
				m_InertializationStates.emplace(id, state);
			else fullyCompatible = false;
		for (const auto& [id, state] : snapshot.blendAlphaStates)
			if (const auto* node = m_Definition.GetNode(id); node && node->GetType() == VansAnimGraphNodeType::Blend && std::isfinite(state.value))
				m_BlendAlphaStates.emplace(id, state);
			else fullyCompatible = false;
		m_InitializedNodes.clear(); m_UpdatedNodes.clear(); m_InitializedThisFrame.clear();
		for (int id : snapshot.initializedNodes)
			if (m_Definition.GetNode(id)) m_InitializedNodes.insert(id); else fullyCompatible = false;
		for (int id : snapshot.updatedNodes)
			if (m_Definition.GetNode(id)) m_UpdatedNodes.insert(id); else fullyCompatible = false;
		m_ClipStates.clear();
		m_StateMachineStates.clear();
		m_BlendSpaceStates.clear();
		m_SyncGroups.clear();
		m_ExternalSyncGroups.clear();
		for (const auto& [name, group] : snapshot.syncGroups)
			if (m_Definition.GetNode(group.leaderNode) && std::isfinite(group.previousTime) && std::isfinite(group.currentTime))
				m_SyncGroups.emplace(name, group);
			else fullyCompatible = false;
		for (const auto& [id, state] : snapshot.blendSpaceStates)
		{
			const auto* node = m_Definition.GetNode(id);
			if (!node || node->GetType() != VansAnimGraphNodeType::BlendSpace2D) { fullyCompatible = false; continue; }
			const auto valid = [](const auto& filter)
			{
				return std::isfinite(filter.window) && filter.window >= 0 && std::isfinite(filter.time)
					&& std::isfinite(filter.output) && (filter.samples.empty() || filter.writeIndex < filter.samples.size())
					&& std::all_of(filter.samples.begin(), filter.samples.end(), [](const auto& sample)
					{ return std::isfinite(sample.value) && std::isfinite(sample.time); });
			};
			const auto* blend = static_cast<const AnimGraphBlendSpace2DNode*>(node);
			if (!valid(state.x) || !valid(state.y) || !std::isfinite(state.normalizedTime)
				|| state.normalizedTime < 0 || state.normalizedTime > 1 || state.markerLeader < -1
				|| state.markerLeader >= static_cast<int>(blend->m_Samples.size())
				|| std::any_of(state.activeSamples.begin(), state.activeSamples.end(), [&](int sample)
					{ return sample < 0 || sample >= static_cast<int>(blend->m_Samples.size()); }))
			{ fullyCompatible = false; continue; }
			m_BlendSpaceStates[id] = state;
		}
		for (int nodeId : m_ExecutionPlan)
		{
			m_EvaluationCache[nodeId] = {};
			m_EvaluatedNodes[nodeId] = false;
			m_EvaluatingNodes[nodeId] = false;
			const auto active = snapshot.activeNodes.find(nodeId);
			m_PreviousActiveNodes[nodeId] = active != snapshot.activeNodes.end() && active->second;
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
			bool validTransitions = true;
			for (const auto& active : restored.activeTransitions)
			{
				if (!stateNames.count(active.previousStateName) || !stateNames.count(active.nextStateName)
					|| !std::isfinite(active.elapsedTime) || active.elapsedTime < 0
					|| !std::isfinite(active.duration) || active.duration < 0
					|| !std::isfinite(active.alpha) || active.alpha < 0 || active.alpha > 1) validTransitions = false;
			}
			if (!validTransitions) { restored.activeTransitions.clear(); fullyCompatible = false; }
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
			retainValidTimes(restored.relevantClipRemaining);
			for (const AnimatorState& state : definition->m_States)
			{
				restored.stateTimes.try_emplace(state.name, state.startTime);
				restored.previousStateTimes.try_emplace(state.name, state.startTime);
			}
			m_StateMachineStates.emplace(nodeId, std::move(restored));
		}
		return fullyCompatible;
	}

	VansAnimGraphClipRuntimeState& VansAnimGraphInstance::GetClipState(int nodeId)
	{
		auto [it, inserted] = m_ClipStates.try_emplace(nodeId);
		if (inserted)
		{
			const auto* node = m_Definition.GetNode(nodeId);
			if (node && node->GetType() == VansAnimGraphNodeType::Clip)
			{
				it->second.previousTime = it->second.currentTime = static_cast<const AnimGraphClipNode*>(node)->m_StartPosition;
				it->second.startPositionPending = true;
			}
		}
		return it->second;
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
		case VansAnimGraphNodeType::MultiWayBlend: return std::make_unique<AnimGraphMultiWayBlendNode>();
		case VansAnimGraphNodeType::ModifyCurve: return std::make_unique<AnimGraphModifyCurveNode>();
		case VansAnimGraphNodeType::ComponentBoneScale: return std::make_unique<AnimGraphComponentBoneScaleNode>();
		case VansAnimGraphNodeType::ComponentBoneTransform: return std::make_unique<AnimGraphComponentBoneTransformNode>();
		case VansAnimGraphNodeType::IfCondition:   return std::make_unique<AnimGraphIfConditionNode>();
		case VansAnimGraphNodeType::Switch:        return std::make_unique<AnimGraphSwitchNode>();
		case VansAnimGraphNodeType::AdditiveBlend: return std::make_unique<AnimGraphAdditiveBlendNode>();
		case VansAnimGraphNodeType::Inertialization: return std::make_unique<AnimGraphInertializationNode>();
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
		if (typeName == "MultiWayBlend")  return CreateNodeByType(VansAnimGraphNodeType::MultiWayBlend);
		if (typeName == "ModifyCurve") return CreateNodeByType(VansAnimGraphNodeType::ModifyCurve);
		if (typeName == "ComponentBoneScale") return CreateNodeByType(VansAnimGraphNodeType::ComponentBoneScale);
		if (typeName == "ComponentBoneTransform") return CreateNodeByType(VansAnimGraphNodeType::ComponentBoneTransform);
		if (typeName == "IfCondition")    return CreateNodeByType(VansAnimGraphNodeType::IfCondition);
		if (typeName == "Switch")         return CreateNodeByType(VansAnimGraphNodeType::Switch);
		if (typeName == "AdditiveBlend")  return CreateNodeByType(VansAnimGraphNodeType::AdditiveBlend);
		if (typeName == "Inertialization") return CreateNodeByType(VansAnimGraphNodeType::Inertialization);
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
		case VansGraphGoalSource::PoseBone: return "poseBone";
		}
		throw std::invalid_argument("Invalid procedural Goal source enum");
	}

	static VansGraphGoalSource StringToGoalSource(const std::string& source)
	{
		if (source == "binding") return VansGraphGoalSource::Binding;
		if (source == "parameters") return VansGraphGoalSource::Parameters;
		if (source == "fixed") return VansGraphGoalSource::Fixed;
		if (source == "poseBone") return VansGraphGoalSource::PoseBone;
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
			{ "binding", goal.binding }, { "boneName", goal.boneName },
			{ "poleBoneName", goal.poleBoneName },
			{ "poleOffsetLocal", { goal.poleOffsetLocal.x, goal.poleOffsetLocal.y, goal.poleOffsetLocal.z } },
			{ "positionParameter", goal.positionParameter },
			{ "rotationParameter", goal.rotationParameter }, { "weightParameter", goal.weightParameter },
			{ "weightCurve", goal.weightCurve },
			{ "offsetWeightCurve", goal.offsetWeightCurve },
			{ "fixedPositionModel", { goal.fixedPositionModel.x, goal.fixedPositionModel.y, goal.fixedPositionModel.z } },
			{ "fixedRotationModel", { goal.fixedRotationModel.x, goal.fixedRotationModel.y,
				goal.fixedRotationModel.z, goal.fixedRotationModel.w } },
			{ "fixedPositionWeight", goal.fixedPositionWeight },
			{ "fixedRotationWeight", goal.fixedRotationWeight }
		};
	}

	static void DeserializeGoal(const nlohmann::json& value, VansGraphGoalDefinition& goal)
	{
		RequireOnlyFields(value, { "id", "source", "binding", "boneName", "poleBoneName", "poleOffsetLocal", "positionParameter",
			"rotationParameter", "weightParameter", "weightCurve", "offsetWeightCurve", "fixedPositionModel",
			"fixedRotationModel", "fixedPositionWeight", "fixedRotationWeight" });
		goal.goalId = value.at("id").get<std::string>();
		goal.source = StringToGoalSource(value.at("source").get<std::string>());
		goal.binding = value.at("binding").get<std::string>();
		goal.boneName = value.value("boneName", std::string{});
		goal.poleBoneName = value.value("poleBoneName", std::string{});
		if (value.contains("poleOffsetLocal"))
		{
			const auto& offset = value.at("poleOffsetLocal");
			goal.poleOffsetLocal = { offset.at(0).get<float>(), offset.at(1).get<float>(), offset.at(2).get<float>() };
		}
		goal.positionParameter = value.at("positionParameter").get<std::string>();
		goal.rotationParameter = value.at("rotationParameter").get<std::string>();
		goal.weightParameter = value.at("weightParameter").get<std::string>();
		goal.weightCurve = value.value("weightCurve", std::string{});
		goal.offsetWeightCurve = value.value("offsetWeightCurve", std::string{});
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

	static const char* AdditiveModeToString(VansAdditivePoseMode mode)
	{
		switch(mode)
		{
		case VansAdditivePoseMode::LocalSpherical:return "LocalSpherical";
		case VansAdditivePoseMode::LocalLinear:return "LocalLinear";
		case VansAdditivePoseMode::MeshRotationLinear:return "MeshRotationLinear";
		}
		throw std::invalid_argument("Invalid additive mode");
	}
	static VansAdditivePoseMode StringToAdditiveMode(const std::string& mode)
	{
		if(mode=="LocalSpherical")return VansAdditivePoseMode::LocalSpherical;
		if(mode=="LocalLinear")return VansAdditivePoseMode::LocalLinear;
		if(mode=="MeshRotationLinear")return VansAdditivePoseMode::MeshRotationLinear;
		throw std::invalid_argument("Unknown additive mode: "+mode);
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
			if (!n->m_SyncGroup.empty()) props["syncGroup"] = n->m_SyncGroup;
			props["speed"]    = n->m_Speed;
			if (n->m_StartPosition != 0) props["startPosition"] = n->m_StartPosition;
			if (!n->m_SampleTimeParameter.empty())
				props["sampleTimeParameter"] = n->m_SampleTimeParameter;
			if (n->m_SampleTime >= 0) props["sampleTime"] = n->m_SampleTime;
			if (n->m_AdditiveReferenceTime >= 0) props["additiveReferenceTime"] = n->m_AdditiveReferenceTime;
			if (!n->m_AdditiveReferenceClip.empty()) props["additiveReferenceClip"] = n->m_AdditiveReferenceClip;
			if(n->m_AdditiveMode!=VansAdditivePoseMode::LocalSpherical) props["additiveMode"]=AdditiveModeToString(n->m_AdditiveMode);
			props["loop"]     = n->m_Loop;
			if (!n->m_LoopParameter.empty()) props["loopParameter"] = n->m_LoopParameter;
			props["rootMotion"] = n->m_RootMotion;
			break;
		}
		case VansAnimGraphNodeType::Blend:
		{
			auto* n = static_cast<const AnimGraphBlendNode*>(node);
			if (n->m_LinearRotationBlend) props["linearRotationBlend"] = true;
			props["paramName"]  = n->m_ParamName;
			props["fixedAlpha"] = n->m_FixedAlpha;
			if (n->m_MapAlpha) props["alphaMap"] = {n->m_AlphaInMin,n->m_AlphaInMax,n->m_AlphaOutMin,n->m_AlphaOutMax};
			if (n->m_InterpolateAlpha) props["alphaInterp"] = {n->m_AlphaSpeedIncreasing,n->m_AlphaSpeedDecreasing};
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
		case VansAnimGraphNodeType::MultiWayBlend:
		{
			props["weightParameters"] = static_cast<const AnimGraphMultiWayBlendNode*>(node)->m_WeightParameters;
			break;
		}
		case VansAnimGraphNodeType::ModifyCurve:
		{
			const auto* n=static_cast<const AnimGraphModifyCurveNode*>(node);
			if (n->m_Mode!=VansCurveModifyMode::Blend && n->m_Mode!=VansCurveModifyMode::Scale)
				throw std::runtime_error("Invalid ModifyCurve mode");
			props["mode"]=n->m_Mode==VansCurveModifyMode::Scale ? "Scale" : "Blend";
			props["alpha"]=n->m_Alpha;
			props["alphaParameter"]=n->m_AlphaParameter;
			props["curves"]=nlohmann::json::array();
			for (const auto& c:n->m_Curves) props["curves"].push_back({{"name",c.name},{"value",c.value},{"parameter",c.parameter}});
			break;
		}
		case VansAnimGraphNodeType::ComponentBoneScale:
		{
			const auto* n=static_cast<const AnimGraphComponentBoneScaleNode*>(node);
			props["boneName"]=n->m_BoneName;
			props["scale"]={n->m_Scale.x,n->m_Scale.y,n->m_Scale.z};
			props["alpha"]=n->m_Alpha;
			props["alphaParameter"]=n->m_AlphaParameter;
			break;
		}
		case VansAnimGraphNodeType::ComponentBoneTransform:
		{
			const auto* n=static_cast<const AnimGraphComponentBoneTransformNode*>(node);
			props["boneName"]=n->m_BoneName;
			props["positionParameter"]=n->m_PositionParameter;
			props["rotationParameter"]=n->m_RotationParameter;
			props["alphaParameter"]=n->m_AlphaParameter;
			props["alpha"]=n->m_Alpha;
			props["positionAdditive"]=n->m_PositionAdditive;
			props["rotationAdditive"]=n->m_RotationAdditive;
			props["worldSpace"]=n->m_WorldSpace;
			break;
		}
		case VansAnimGraphNodeType::BlendSpace2D:
		{
			auto* n = static_cast<const AnimGraphBlendSpace2DNode*>(node);
			props["xParamName"] = n->m_XParamName;
			props["yParamName"] = n->m_YParamName;
			if (n->m_BilinearGrid) props["bilinearGrid"] = true;
			if(n->m_SampleGrid.IsEnabled())
			{
				const auto& g=n->m_SampleGrid;
				auto& j=props["sampleGrid"];
				j={{"columns",g.columns},{"rows",g.rows},{"minX",g.minX},{"maxX",g.maxX},{"minY",g.minY},{"maxY",g.maxY},{"cells",nlohmann::json::array()}};
				for(const auto& cell:g.cells)
				{
					auto entries=nlohmann::json::array();
					for(const auto& entry:cell)entries.push_back({{"sampleIndex",entry.sampleIndex},{"weight",entry.weight}});
					j["cells"].push_back(std::move(entries));
				}
			}
			if (n->m_CubicFilterWindowX > 0) props["cubicFilterWindowX"] = n->m_CubicFilterWindowX;
			if (n->m_CubicFilterWindowY > 0) props["cubicFilterWindowY"] = n->m_CubicFilterWindowY;
			if (n->m_SynchronizeSamples) props["synchronizeSamples"] = true;
			if (!n->m_SyncGroup.empty()) props["syncGroup"] = n->m_SyncGroup;
			if (n->m_StartPosition != 0) props["startPosition"] = n->m_StartPosition;
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
			if(n->m_AdditiveMode!=VansAdditivePoseMode::LocalSpherical) props["additiveMode"]=AdditiveModeToString(n->m_AdditiveMode);
			props["paramName"]   = n->m_ParamName;
			props["fixedWeight"] = n->m_FixedWeight;
			props["useParam"]    = n->m_UseParam;
			break;
		}
		case VansAnimGraphNodeType::Inertialization:
			props["teleportDistance"] = static_cast<const AnimGraphInertializationNode*>(node)->m_TeleportDistance;
			break;
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
			if (n->m_SkipFirstUpdateTransition) props["skipFirstUpdateTransition"] = true;
			if (n->m_MaxTransitionsPerFrame != 1) props["maxTransitionsPerFrame"] = n->m_MaxTransitionsPerFrame;
			if (n->m_LinearRotationBlend) props["linearRotationBlend"] = true;

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
				if (s.alwaysResetOnEntry) statesJson.back()["alwaysResetOnEntry"] = true;
				if (s.conduit) statesJson.back()["conduit"] = true;
				if (!s.entryConditionParameter.empty()) statesJson.back()["entryConditionParameter"] = s.entryConditionParameter;
				if (!s.enteredEvent.empty()) statesJson.back()["enteredEvent"] = s.enteredEvent;
				if (!s.leftEvent.empty()) statesJson.back()["leftEvent"] = s.leftEvent;
				if (!s.fullyBlendedEvent.empty()) statesJson.back()["fullyBlendedEvent"] = s.fullyBlendedEvent;
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
				if (!t.startEvent.empty()) tj["startEvent"] = t.startEvent;
				if (!t.endEvent.empty()) tj["endEvent"] = t.endEvent;
				if (!t.interruptEvent.empty()) tj["interruptEvent"] = t.interruptEvent;
				if (t.requireSourceFullyBlended) tj["requireSourceFullyBlended"] = true;
				if (t.requireRelevantClipFinished) tj["requireRelevantClipFinished"] = true;
				if (t.inertialization) tj["inertialization"] = true;
				if (t.automaticRemainingTime) tj["automaticRemainingTime"] = true;
				if (t.matchAnyCondition) tj["matchAnyCondition"] = true;
				if (!t.boneBlendFactors.empty()) tj["boneBlendFactors"] = t.boneBlendFactors;
				if (!t.durationScaleCurve.empty())
				{
					tj["durationScaleCurve"] = nlohmann::json::array();
					for (const auto& key : t.durationScaleCurve)
						tj["durationScaleCurve"].push_back({ { "time", key.time }, { "value", key.value },
							{ "arriveTangent", key.arriveTangent }, { "leaveTangent", key.leaveTangent } });
				}
				if (!t.blendCurve.empty())
				{
					tj["blendCurve"] = nlohmann::json::array();
					for (const auto& key : t.blendCurve)
						tj["blendCurve"].push_back({ { "time", key.time }, { "value", key.value },
							{ "arriveTangent", key.arriveTangent }, { "leaveTangent", key.leaveTangent } });
				}

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
					if (c.source != AnimatorConditionSource::Parameter)
					{
						condsJson.back()["source"] = c.source == AnimatorConditionSource::MachineWeight ? "machineWeight" : "stateElapsedTime";
						condsJson.back()["machineNodeId"] = c.machineNodeId;
					}
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
			if (n->m_LinearRotationBlend) props["linearRotationBlend"] = true;
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
			if (n->m_MeshSpaceRotationOnly) props["meshSpaceRotationOnly"] = true;
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
			RequireOnlyFields(props, { "clipName", "speed", "loop", "loopParameter", "rootMotion", "sampleTimeParameter", "sampleTime", "startPosition", "additiveReferenceTime", "additiveReferenceClip", "additiveMode", "syncGroup" });
			auto* n = static_cast<AnimGraphClipNode*>(node);
			if (props.contains("clipName")) n->m_ClipName = props["clipName"].get<std::string>();
			n->m_SyncGroup = props.value("syncGroup", std::string{});
			if (props.contains("speed"))    n->m_Speed    = props["speed"].get<float>();
			n->m_StartPosition = props.value("startPosition", 0.0f);
			if (props.contains("sampleTimeParameter")) n->m_SampleTimeParameter = props["sampleTimeParameter"].get<std::string>();
			if (props.contains("sampleTime")) n->m_SampleTime = props["sampleTime"].get<float>();
			n->m_AdditiveReferenceTime = props.value("additiveReferenceTime", -1.0f);
			n->m_AdditiveReferenceClip = props.value("additiveReferenceClip", std::string{});
			n->m_AdditiveMode=StringToAdditiveMode(props.value("additiveMode",std::string("LocalSpherical")));
			if (props.contains("loop"))     n->m_Loop     = props["loop"].get<bool>();
			if (props.contains("loopParameter")) n->m_LoopParameter = props["loopParameter"].get<std::string>();
			if (props.contains("rootMotion")) n->m_RootMotion = props["rootMotion"].get<bool>();
			break;
		}
		case VansAnimGraphNodeType::Blend:
		{
			RequireOnlyFields(props, { "paramName", "fixedAlpha", "useParam", "alphaMap", "alphaInterp", "linearRotationBlend" });
			auto* n = static_cast<AnimGraphBlendNode*>(node);
			n->m_LinearRotationBlend = props.value("linearRotationBlend", false);
			if (props.contains("alphaMap"))
			{
				const auto& map = props.at("alphaMap");
				if (!map.is_array() || map.size()!=4) throw std::runtime_error("alphaMap requires four range values");
				n->m_MapAlpha = true;
				n->m_AlphaInMin=map.at(0).get<float>(); n->m_AlphaInMax=map.at(1).get<float>();
				n->m_AlphaOutMin=map.at(2).get<float>(); n->m_AlphaOutMax=map.at(3).get<float>();
			}
			if (props.contains("alphaInterp"))
			{
				const auto& speed = props.at("alphaInterp");
				if (!speed.is_array() || speed.size()!=2) throw std::runtime_error("alphaInterp requires increasing/decreasing speeds");
				n->m_InterpolateAlpha = true;
				n->m_AlphaSpeedIncreasing=speed.at(0).get<float>(); n->m_AlphaSpeedDecreasing=speed.at(1).get<float>();
			}
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
		case VansAnimGraphNodeType::MultiWayBlend:
		{
			RequireOnlyFields(props, { "weightParameters" });
			static_cast<AnimGraphMultiWayBlendNode*>(node)->m_WeightParameters =
				props.at("weightParameters").get<std::vector<std::string>>();
			break;
		}
		case VansAnimGraphNodeType::ModifyCurve:
		{
			RequireOnlyFields(props,{"mode","alpha","alphaParameter","curves"});
			auto* n=static_cast<AnimGraphModifyCurveNode*>(node);
			const auto mode=props.at("mode").get<std::string>();
			if (mode!="Blend" && mode!="Scale") throw std::runtime_error("Invalid ModifyCurve mode");
			n->m_Mode=mode=="Scale" ? VansCurveModifyMode::Scale : VansCurveModifyMode::Blend;
			n->m_Alpha=props.at("alpha").get<float>();
			n->m_AlphaParameter=props.at("alphaParameter").get<std::string>();
			n->m_Curves.clear();
			for (const auto& c:props.at("curves"))
			{
				RequireOnlyFields(c,{"name","value","parameter"});
				n->m_Curves.push_back({c.at("name").get<std::string>(),c.at("value").get<float>(),c.at("parameter").get<std::string>()});
			}
			break;
		}
		case VansAnimGraphNodeType::ComponentBoneScale:
		{
			RequireOnlyFields(props,{"boneName","scale","alpha","alphaParameter"});
			auto* n=static_cast<AnimGraphComponentBoneScaleNode*>(node);
			n->m_BoneName=props.at("boneName").get<std::string>();
			const auto scale=props.at("scale").get<std::vector<float>>();
			if(scale.size()!=3)throw std::runtime_error("ComponentBoneScale requires three scale components");
			n->m_Scale={scale[0],scale[1],scale[2]};
			n->m_Alpha=props.at("alpha").get<float>();
			n->m_AlphaParameter=props.at("alphaParameter").get<std::string>();
			break;
		}
		case VansAnimGraphNodeType::ComponentBoneTransform:
		{
			RequireOnlyFields(props,{"boneName","positionParameter","rotationParameter",
				"alphaParameter","alpha","positionAdditive","rotationAdditive","worldSpace"});
			auto* n=static_cast<AnimGraphComponentBoneTransformNode*>(node);
			n->m_BoneName=props.at("boneName").get<std::string>();
			n->m_PositionParameter=props.at("positionParameter").get<std::string>();
			n->m_RotationParameter=props.at("rotationParameter").get<std::string>();
			n->m_AlphaParameter=props.at("alphaParameter").get<std::string>();
			n->m_Alpha=props.at("alpha").get<float>();
			n->m_PositionAdditive=props.at("positionAdditive").get<bool>();
			n->m_RotationAdditive=props.at("rotationAdditive").get<bool>();
			n->m_WorldSpace=props.at("worldSpace").get<bool>();
			break;
		}
		case VansAnimGraphNodeType::BlendSpace2D:
		{
			auto* n = static_cast<AnimGraphBlendSpace2DNode*>(node);
			RequireOnlyFields(props, { "xParamName", "yParamName", "samples", "bilinearGrid", "sampleGrid", "cubicFilterWindowX", "cubicFilterWindowY", "synchronizeSamples", "startPosition", "syncGroup" });
			n->m_XParamName = props.at("xParamName").get<std::string>();
			n->m_YParamName = props.at("yParamName").get<std::string>();
			n->m_BilinearGrid = props.value("bilinearGrid", false);
			n->m_SampleGrid={};
			if(props.contains("sampleGrid"))
			{
				const auto& j=props.at("sampleGrid");
				RequireOnlyFields(j,{"columns","rows","minX","maxX","minY","maxY","cells"});
				auto& g=n->m_SampleGrid;
				g.columns=j.at("columns").get<int>();g.rows=j.at("rows").get<int>();
				g.minX=j.at("minX").get<float>();g.maxX=j.at("maxX").get<float>();
				g.minY=j.at("minY").get<float>();g.maxY=j.at("maxY").get<float>();
				if(!j.at("cells").is_array())throw std::runtime_error("sampleGrid cells must be an array");
				for(const auto& cell:j.at("cells"))
				{
					if(!cell.is_array())throw std::runtime_error("sampleGrid cell must be an array");
					g.cells.emplace_back();
					for(const auto& entry:cell)
					{
						RequireOnlyFields(entry,{"sampleIndex","weight"});
						g.cells.back().push_back({entry.at("sampleIndex").get<int>(),entry.at("weight").get<float>()});
					}
				}
			}
			n->m_CubicFilterWindowX = props.value("cubicFilterWindowX", 0.0f);
			n->m_CubicFilterWindowY = props.value("cubicFilterWindowY", 0.0f);
			n->m_SynchronizeSamples = props.value("synchronizeSamples", false);
			n->m_SyncGroup = props.value("syncGroup", std::string{});
			n->m_StartPosition = props.value("startPosition", 0.0f);
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
			RequireOnlyFields(props, { "paramName", "fixedWeight", "useParam", "additiveMode" });
			auto* n = static_cast<AnimGraphAdditiveBlendNode*>(node);
			n->m_AdditiveMode=StringToAdditiveMode(props.value("additiveMode",std::string("LocalSpherical")));
			if (props.contains("paramName"))   n->m_ParamName   = props["paramName"].get<std::string>();
			if (props.contains("fixedWeight")) n->m_FixedWeight = props["fixedWeight"].get<float>();
			if (props.contains("useParam"))    n->m_UseParam    = props["useParam"].get<bool>();
			break;
		}
		case VansAnimGraphNodeType::Inertialization:
			RequireOnlyFields(props, {"teleportDistance"});
			static_cast<AnimGraphInertializationNode*>(node)->m_TeleportDistance = props.value("teleportDistance", 3.0f);
			break;
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
			RequireOnlyFields(props, { "defaultState", "states", "transitions", "skipFirstUpdateTransition", "maxTransitionsPerFrame", "linearRotationBlend" });
			auto* n = static_cast<AnimGraphStateMachineNode*>(node);
			n->m_SkipFirstUpdateTransition = props.value("skipFirstUpdateTransition", false);
			n->m_MaxTransitionsPerFrame = props.value("maxTransitionsPerFrame", 1);
			n->m_LinearRotationBlend = props.value("linearRotationBlend", false);
			if (props.contains("defaultState"))
				n->m_DefaultStateName = props["defaultState"].get<std::string>();

			if (props.contains("states"))
			{
				for (const auto& sj : props["states"])
				{
					RequireOnlyFields(sj, { "name", "clip", "poseNodeId", "speed",
						"speedParameter", "loop", "rootMotion", "startTime", "endTime", "alwaysResetOnEntry", "conduit", "entryConditionParameter", "enteredEvent", "leftEvent", "fullyBlendedEvent" });
					AnimatorState s;
					s.enteredEvent = sj.value("enteredEvent", std::string{});
					s.leftEvent = sj.value("leftEvent", std::string{});
					s.fullyBlendedEvent = sj.value("fullyBlendedEvent", std::string{});
					s.alwaysResetOnEntry = sj.value("alwaysResetOnEntry", false);
					s.conduit = sj.value("conduit", false);
					s.entryConditionParameter = sj.value("entryConditionParameter", std::string{});
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
						"exitTime", "conditions", "blendCurve", "durationScaleCurve", "requireSourceFullyBlended", "requireRelevantClipFinished", "inertialization", "automaticRemainingTime", "matchAnyCondition", "boneBlendFactors", "startEvent", "endEvent", "interruptEvent" });
					AnimatorTransition t;
					t.startEvent = tj.value("startEvent", std::string{});
					t.endEvent = tj.value("endEvent", std::string{});
					t.interruptEvent = tj.value("interruptEvent", std::string{});
					if (tj.contains("from"))          t.fromState     = tj["from"].get<std::string>();
					if (tj.contains("to"))            t.toState       = tj["to"].get<std::string>();
					if (tj.contains("blendDuration")) t.blendDuration = tj["blendDuration"].get<float>();
					if (tj.contains("hasExitTime"))   t.hasExitTime   = tj["hasExitTime"].get<bool>();
					if (tj.contains("exitTime"))      t.exitTime      = tj["exitTime"].get<float>();
					t.requireSourceFullyBlended = tj.value("requireSourceFullyBlended", false);
					t.requireRelevantClipFinished = tj.value("requireRelevantClipFinished", false);
					t.inertialization = tj.value("inertialization", false);
					t.automaticRemainingTime = tj.value("automaticRemainingTime", false);
					t.matchAnyCondition = tj.value("matchAnyCondition", false);
					if (tj.contains("boneBlendFactors")) t.boneBlendFactors = tj["boneBlendFactors"].get<std::unordered_map<std::string, float>>();
					if (tj.contains("durationScaleCurve")) for (const auto& key : tj["durationScaleCurve"])
					{
						RequireOnlyFields(key, { "time", "value", "arriveTangent", "leaveTangent" });
						t.durationScaleCurve.push_back({ key.at("time").get<float>(), key.at("value").get<float>(),
							key.at("arriveTangent").get<float>(), key.at("leaveTangent").get<float>() });
					}
					if (tj.contains("blendCurve")) for (const auto& key : tj["blendCurve"])
					{
						RequireOnlyFields(key, { "time", "value", "arriveTangent", "leaveTangent" });
						t.blendCurve.push_back({ key.at("time").get<float>(), key.at("value").get<float>(),
							key.at("arriveTangent").get<float>(), key.at("leaveTangent").get<float>() });
					}

					if (tj.contains("conditions"))
					{
						for (const auto& cj : tj["conditions"])
						{
							RequireOnlyFields(cj, { "param", "op", "floatVal", "boolVal", "intVal", "source", "machineNodeId" });
							TransitionCondition cond;
							const auto source = cj.value("source", std::string("parameter"));
							if (source == "machineWeight") cond.source = AnimatorConditionSource::MachineWeight;
							else if (source == "stateElapsedTime") cond.source = AnimatorConditionSource::StateElapsedTime;
							else if (source != "parameter") throw std::invalid_argument("Unknown transition condition source: " + source);
							cond.machineNodeId = cj.value("machineNodeId", -1);
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
			RequireOnlyFields(props, { "slotId", "enableFallbackInput", "linearRotationBlend" });
			auto* n = static_cast<AnimGraphSlotNode*>(node);
			n->m_LinearRotationBlend = props.value("linearRotationBlend", false);
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
				"fixedWeight", "useWeightParameter", "applyAdditiveInput", "meshSpaceRotationOnly", "mask" });
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
			n->m_MeshSpaceRotationOnly = props.value("meshSpaceRotationOnly", false);
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
		const AnimGraphContext& ctx, VansAnimGraphInstance& instance) const
	{
		if (ctx.stagedPlayback)
			for (const auto& [id, node] : instance.GetDefinition().GetNodes())
				if (node->GetType() == VansAnimGraphNodeType::SaveCachedPose
					&& static_cast<const AnimGraphSaveCachedPoseNode*>(node.get())->m_CacheName == m_CacheName)
					return EvaluateNodePose(instance, id, ctx, m_NodeId);
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
		const float weight = m_UseWeightParameter && ctx.parameters
			? [&]() { auto it = ctx.parameters->find(m_WeightParameter);
				return it != ctx.parameters->end() && it->second.type == AnimatorParamType::Float
					? it->second.floatVal : 0.0f; }()
			: m_FixedWeight;
		AnimGraphPose base = EvaluateInputPose(instance, GetNodeId(), 0, ctx);
		AnimGraphPose overlay = EvaluateInputPose(instance, GetNodeId(), 1, ctx, std::clamp(weight, 0.0f, 1.0f));
		if (ctx.preparePlayback)
		{
			if (m_ApplyAdditiveInput) EvaluateInputPose(instance, GetNodeId(), 2, ctx, std::clamp(weight, 0.0f, 1.0f));
			return PlaybackPlaceholder();
		}
		if (!base.valid || !overlay.valid || !ctx.skeleton) return base;
		const auto& runtime = instance.ResolveLayeredBlendRuntime(
			GetNodeId(), m_Mask, *ctx.skeleton);
		VansAnimationLayerDefinition definition;
		definition.id = "graph-layered-blend-per-bone";
		definition.kind = VansAnimationLayerKind::Overlay;
		definition.blendMode = m_BlendMode;
		definition.rotationSpace = m_MeshSpaceRotationOnly ? VansRotationBlendSpace::Local : m_RotationSpace;
		definition.rootMotion = VansLayerRootMotionMode::Ignore;
		AnimGraphPose result = VansAnimationLayerMixer::ApplyLayer(
			base, overlay, definition, runtime.mask, *ctx.skeleton, runtime.bindPose, weight);
		if (m_MeshSpaceRotationOnly)
		{
			// 分开累积旋转，避免模型矩阵混合把父骨平移、缩放或剪切带入局部平移。
			const auto count = ctx.skeleton->bones.size();
			if (base.localPose.size() != count || overlay.localPose.size() != count || runtime.mask.weights.size() != count)
				return base;
			const float effectiveWeight = std::clamp(weight * overlay.sourceWeight, 0.0f, 1.0f);
			VansAnimationFrameVector<glm::quat> baseRotations(count), overlayRotations(count), mixedRotations(count);
			for (int bone : ctx.skeleton->topologicalOrder)
			{
				const int parent = ctx.skeleton->bones[bone].parentIndex;
				baseRotations[bone] = glm::normalize(parent < 0 ? base.localPose[bone].rotation
					: baseRotations[parent] * base.localPose[bone].rotation);
				overlayRotations[bone] = glm::normalize(parent < 0 ? overlay.localPose[bone].rotation
					: overlayRotations[parent] * overlay.localPose[bone].rotation);
				const float alpha = std::clamp(effectiveWeight * runtime.mask.weights[bone], 0.0f, 1.0f);
				const auto target = glm::dot(baseRotations[bone], overlayRotations[bone]) < 0
					? -overlayRotations[bone] : overlayRotations[bone];
				mixedRotations[bone] = glm::normalize(baseRotations[bone]*(1-alpha) + target*alpha);
				result.localPose[bone].rotation = glm::normalize(parent < 0 ? mixedRotations[bone]
					: glm::inverse(mixedRotations[parent]) * mixedRotations[bone]);
			}
		}
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
		const VansSlotPoseInputs* inputs = nullptr;
		if (ctx.slotPayloads)
		{
			const auto slot = ctx.slotPayloads->find(m_SlotId);
			if (slot != ctx.slotPayloads->end()) inputs = &slot->second;
		}
		float totalWeight = 0;
		bool unmaskedOverride = true;
		if (inputs) for (const auto& pose : inputs->poses)
		{
			if (!pose.valid) continue;
			if (!std::isfinite(pose.sourceWeight) || pose.sourceWeight < 0) return {};
			totalWeight += pose.sourceWeight;
			unmaskedOverride &= !pose.sourceAdditive && pose.sourceBoneMask.empty();
		}
		const float fallbackWeight = unmaskedOverride ? std::max(0.0f, 1-totalWeight) : 1.0f;
		AnimGraphPose fallback;
		if (m_EnableFallbackInput)
			fallback = EvaluateInputPose(instance, m_NodeId, 0, ctx, fallbackWeight);
		if (ctx.preparePlayback) return PlaybackPlaceholder();
		return BlendInputs(fallback, inputs, ctx.skeleton, m_LinearRotationBlend);
	}

	AnimGraphPose AnimGraphSlotNode::BlendInputs(
		const AnimGraphPose& fallback, const VansSlotPoseInputs* inputs,
		const Skeleton* skeleton, bool linearRotationBlend)
	{
		if (!inputs || inputs->poses.empty()) return fallback;
		float totalWeight = 0.0f;
		bool unmaskedOverride = true;
		for (const auto& pose : inputs->poses)
		{
			if (!pose.valid) continue;
			if (!std::isfinite(pose.sourceWeight) || pose.sourceWeight < 0.0f) return {};
			totalWeight += pose.sourceWeight;
			unmaskedOverride &= !pose.sourceAdditive && pose.sourceBoneMask.empty();
		}
		const float fallbackWeight = unmaskedOverride ? std::max(0.0f, 1.0f-totalWeight) : 1.0f;
		if (linearRotationBlend && unmaskedOverride)
		{
			constexpr float weightThreshold = .00001f;
			if (totalWeight <= weightThreshold) return fallback;
			VansAnimationFrameVector<VansPosePayload> poses;
			VansAnimationFrameVector<float> weights;
			const float denominator = !fallback.valid ? totalWeight
				: (totalWeight > 1+weightThreshold ? totalWeight : 1);
			for (const auto& pose : inputs->poses) if (pose.valid)
			{
				poses.push_back(pose); weights.push_back(pose.sourceWeight/denominator);
			}
			// 保留播放实例的顺序，最后加入基础姿态；不提前归一化中间四元数。
			if (fallback.valid && fallbackWeight > weightThreshold)
			{
				poses.push_back(fallback); weights.push_back(fallbackWeight);
			}
			auto result = VansPosePayloadMixer::BlendWeighted(poses, weights);
			result.sourceWeight = fallback.valid ? fallback.sourceWeight : std::clamp(totalWeight, 0.0f, 1.0f);
			return result;
		}
		// 球面插值模式保留逐片段混合；播放器不再拥有第二套混合结果。
		AnimGraphPose combined;
		float accumulatedWeight = 0;
		for (const auto& pose : inputs->poses) if (pose.valid)
		{
			const float sum = accumulatedWeight+pose.sourceWeight;
			combined = combined.valid ? VansPosePayloadMixer::BlendOverride(combined, pose,
				sum > 0 ? pose.sourceWeight/sum : 1) : pose;
			accumulatedWeight = sum;
		}
		if (!combined.valid) return fallback;
		combined.sourceWeight = std::clamp(accumulatedWeight, 0.0f, 1.0f);
		if (!fallback.valid) return combined;
		if (skeleton && (combined.sourceAdditive || !combined.sourceBoneMask.empty()))
		{
			VansCompiledBoneMask mask;
			mask.weights.assign(skeleton->bones.size(), 1.0f);
			if (combined.sourceBoneMask.size() == skeleton->bones.size())
				mask.weights.assign(combined.sourceBoneMask.begin(), combined.sourceBoneMask.end());
			mask.activeBones.reserve(mask.weights.size());
			for (std::size_t index = 0; index < mask.weights.size(); ++index)
				if (mask.weights[index] > 0.0f) mask.activeBones.push_back(static_cast<std::uint32_t>(index));
			mask.allZero = mask.activeBones.empty();
			mask.allOne = std::all_of(mask.weights.begin(), mask.weights.end(), [](float value) { return value >= 0.9999f; });
			mask.rootWeight = mask.weights.empty() ? 0.0f : mask.weights.front();
			mask.valid = true;
			VansAnimationLayerDefinition definition;
			definition.id = "TimelineSlot";
			definition.blendMode = combined.sourceAdditive ? VansLayerBlendMode::Additive : VansLayerBlendMode::Override;
			definition.rootMotion = VansLayerRootMotionMode::Override;
			definition.nodeTracks = VansLayerNodeTrackMode::Override;
			VansAnimationFrameVector<VansBoneTransform> referencePose;
			VansAnimationLayerMixer::BuildBindPose(*skeleton, referencePose);
			AnimGraphPose result = VansAnimationLayerMixer::ApplyLayer(
				fallback, combined, definition, mask, *skeleton, referencePose, 1.0f);
			result.sourceWeight = fallback.sourceWeight;
			return result;
		}
		auto result = VansPosePayloadMixer::BlendOverride(fallback, combined, combined.sourceWeight);
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
