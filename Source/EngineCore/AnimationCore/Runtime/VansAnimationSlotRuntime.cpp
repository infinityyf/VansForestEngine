#include "VansAnimationSlotRuntime.h"

#include <../../GLM/glm.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace VansGraphics;

namespace
{
	bool IsFiniteNonNegative(float value)
	{
		return std::isfinite(value) && value >= 0.0f;
	}

	float BlendAlpha(float alpha, VansSlotBlendOption option)
	{
		alpha = std::clamp(alpha, 0.0f, 1.0f);
		// UE FAlphaBlend::AlphaToBlendOption: zero-tangent CubicInterp and
		// SmoothStep (HermiteCubic) both reduce to 3t^2 - 2t^3.
		if (option == VansSlotBlendOption::Cubic
			|| option == VansSlotBlendOption::HermiteCubic)
			return alpha * alpha * (3.0f - 2.0f * alpha);
		return alpha;
	}
}

bool VansAnimationSlotRuntime::Configure(
	std::vector<VansAnimationSlotDefinition> definitions,
	std::string& error)
{
	error.clear();
	std::unordered_set<std::string> ids;
	for (const VansAnimationSlotDefinition& definition : definitions)
	{
		if (definition.id.empty() || definition.name.empty() || definition.layerId.empty()
			|| !ids.insert(definition.id).second)
		{
			error = "Slot definitions require unique IDs and non-empty names/layers";
			return false;
		}
		if (!IsFiniteNonNegative(definition.defaultBlendIn)
			|| !IsFiniteNonNegative(definition.defaultBlendOut))
		{
			error = "Slot blend times must be finite and non-negative";
			return false;
		}
	}

	m_Definitions = std::move(definitions);
	m_DefinitionById.clear();
	for (std::size_t index = 0; index < m_Definitions.size(); ++index)
		m_DefinitionById.emplace(m_Definitions[index].id, index);
	m_States.clear();
	m_States.resize(m_Definitions.size());
	m_Statuses.clear();
	m_LifecycleEvents.clear();
	m_PendingLifecycleEvents.clear();
	m_NextHandle = 1;
	m_RootMotionHandle = {};
	m_QueuedNotifyStates.clear();
	m_ActiveNotifyStates.clear();
	m_EndedNotifyStates.clear();
	m_NotifyStateEvents.clear();
	return true;
}

VansSlotPlaybackHandle VansAnimationSlotRuntime::Play(
	const std::string& slotId,
	const VansSlotPlayRequest& request,
	const std::unordered_map<std::string, VansAnimationClip>& clips)
{
	auto definitionIt = m_DefinitionById.find(slotId);
	if (definitionIt == m_DefinitionById.end() || request.clipName.empty()
		|| !std::isfinite(request.playRate) || request.playRate == 0.0f
		|| !IsFiniteNonNegative(request.startTime) || !std::isfinite(request.blendOutTriggerTime) || request.loopCount <= 0
		|| !std::isfinite(request.weight) || request.weight < 0.0f
		|| (request.blendIn && !IsFiniteNonNegative(*request.blendIn))
		|| (request.blendOut && !IsFiniteNonNegative(*request.blendOut)))
		return {};
	const auto clip=clips.find(request.clipName);
	if (clip==clips.end() || !std::isfinite(clip->second.duration) || clip->second.duration<=0.0f) return {};
	if (!request.curveOverrideClipName.empty() && clips.find(request.curveOverrideClipName)==clips.end()) return {};
	std::unordered_set<std::string> notifyIds;
	for (const auto& notify : request.notifyStates)
		if (notify.id.empty() || notify.name.empty() || !notifyIds.insert(notify.id).second ||
			!std::isfinite(notify.startTime) || !std::isfinite(notify.endTime) ||
			notify.endTime <= notify.startTime || !IsFiniteNonNegative(notify.minimumWeight)) return {};

	const std::size_t slotIndex = definitionIt->second;
	const VansAnimationSlotDefinition& definition = m_Definitions[slotIndex];
	SlotState& state = m_States[slotIndex];
	RequestRuntime runtime;
	runtime.handle.value = m_NextHandle++;
	runtime.request = request;
	if (!runtime.request.notifyStates.empty())
		runtime.notifyStates = std::make_shared<const std::vector<VansSlotNotifyStateDefinition>>(
			std::move(runtime.request.notifyStates));
	runtime.request.startTime = std::clamp(request.startTime,0.0f,clip->second.duration);
	runtime.previousTime = runtime.request.startTime;
	runtime.currentTime = runtime.request.startTime;
	runtime.hasRootMotion = clip->second.rootMotion.enabled;
	runtime.blendIn = request.blendIn.value_or(definition.defaultBlendIn);
	runtime.blendOut = request.blendOut.value_or(definition.defaultBlendOut);
	runtime.weight = runtime.blendIn <= 0.0f ? 1.0f : 0.0f;

	VansSlotPlaybackStatus status;
	status.slotId = definition.id;
	status.clipName = request.clipName;
	status.tag = request.tag;
	status.playbackTime = runtime.currentTime;
	status.weight = runtime.weight;

	if (!state.active)
	{
		m_Statuses[runtime.handle.value] = status;
		StartRequest(slotIndex, std::move(runtime));
		return state.active->handle;
	}

	const int activePriority = state.active->request.priority;
	const bool higherPriority = request.priority > activePriority;
	const bool equalPriority = request.priority == activePriority;
	const bool replace = higherPriority
		|| (equalPriority && definition.concurrency == VansSlotConcurrency::Replace);
	if (replace && definition.interruptible)
	{
		m_Statuses[runtime.handle.value] = status;
		StartRequest(slotIndex, std::move(runtime));
		return state.active->handle;
	}

	const bool canQueue = definition.concurrency == VansSlotConcurrency::Queue
		&& state.queue.size() < definition.maxQueueDepth;
	if (canQueue)
	{
		status.state = VansSlotPlaybackState::Queued;
		m_Statuses[runtime.handle.value] = status;
		state.queue.push_back(std::move(runtime));
		return state.queue.back().handle;
	}

	status.state = VansSlotPlaybackState::Rejected;
	m_Statuses[runtime.handle.value] = status;
	PublishLifecycle(slotIndex, runtime, VansSlotLifecycleEventType::Rejected);
	return runtime.handle;
}

bool VansAnimationSlotRuntime::Drive(
	VansSlotPlaybackHandle handle,
	float playbackTime,
	float weight)
{
	if (!handle || !IsFiniteNonNegative(playbackTime) || !IsFiniteNonNegative(weight))
		return false;
	auto drive = [playbackTime, weight](RequestRuntime& runtime)
	{
		runtime.previousTime = runtime.currentTime;
		runtime.currentTime = playbackTime;
		runtime.request.externallyDriven = true;
		runtime.request.weight = weight;
		runtime.weight = weight;
		runtime.notifyWeight = weight;
		runtime.notifyRangePending = true;
	};
	for (SlotState& state : m_States)
	{
		if (state.active && state.active->handle == handle) { drive(*state.active); return true; }
		for (auto& outgoing : state.outgoing)
			if (outgoing.handle == handle) { drive(outgoing); return true; }
		for (RequestRuntime& queued : state.queue)
			if (queued.handle == handle) { drive(queued); return true; }
	}
	return false;
}

bool VansAnimationSlotRuntime::StopSlot(const std::string& slotId, float blendOut, bool force)
{
	auto found = m_DefinitionById.find(slotId);
	if (found == m_DefinitionById.end()) return false;
	const auto& state = m_States[found->second];
	std::vector<VansSlotPlaybackHandle> handles;
	if (state.active) handles.push_back(state.active->handle);
	for (const auto& request : state.outgoing) handles.push_back(request.handle);
	for (const auto& request : state.queue) handles.push_back(request.handle);
	bool stopped = false;
	for (auto handle : handles) stopped = Stop(handle, blendOut, force) || stopped;
	return stopped;
}

bool VansAnimationSlotRuntime::Stop(VansSlotPlaybackHandle handle, float blendOut, bool force)
{
	if (!handle || !IsFiniteNonNegative(blendOut))
		return false;
	for (std::size_t slotIndex = 0; slotIndex < m_States.size(); ++slotIndex)
	{
		SlotState& state = m_States[slotIndex];
		if (state.active && state.active->handle == handle)
		{
			if (!force && !m_Definitions[slotIndex].interruptible)
				return false;
			BeginBlendOut(slotIndex, VansSlotLifecycleEventType::Interrupted, blendOut);
			return true;
		}
		for (auto& outgoing : state.outgoing)
		{
			if (!(outgoing.handle == handle)) continue;
			if (!force && !m_Definitions[slotIndex].interruptible) return false;
			RetargetBlendOut(slotIndex, outgoing, blendOut);
			return true;
		}
		for (auto it = state.queue.begin(); it != state.queue.end(); ++it)
		{
			if (it->handle == handle)
			{
				RequestRuntime request = *it;
				state.queue.erase(it);
				m_Statuses[handle.value].state = VansSlotPlaybackState::Interrupted;
				PublishLifecycle(slotIndex, request, VansSlotLifecycleEventType::Interrupted);
				return true;
			}
		}
	}
	return false;
}

VansSlotPlaybackStatus VansAnimationSlotRuntime::GetStatus(VansSlotPlaybackHandle handle) const
{
	auto found = m_Statuses.find(handle.value);
	return found == m_Statuses.end() ? VansSlotPlaybackStatus{} : found->second;
}

bool VansAnimationSlotRuntime::IsSlotActive(const std::string& slotId) const
{
	auto found = m_DefinitionById.find(slotId);
	return found != m_DefinitionById.end()
		&& (m_States[found->second].active.has_value() || !m_States[found->second].outgoing.empty());
}

bool VansAnimationSlotRuntime::HasRootMotionPlayback(const std::string& slotId,
	const std::unordered_map<std::string, VansAnimationClip>& clips) const
{
	const auto found = m_DefinitionById.find(slotId);
	if (found == m_DefinitionById.end()) return false;
	const auto extractsRoot = [&](const RequestRuntime& runtime)
	{
		const auto clip = clips.find(runtime.request.clipName);
		return !runtime.reachedEnd && !runtime.request.suppressRootMotion &&
			clip != clips.end() && clip->second.rootMotion.enabled;
	};
	const auto& state = m_States[found->second];
	// 刚提交的请求尚未推进淡入，也必须在本帧移动前提取根运动。
	return (state.active && state.active->handle==m_RootMotionHandle && extractsRoot(*state.active)) ||
		(!state.active && state.outgoing.empty() && !state.queue.empty() && extractsRoot(state.queue.front()));
}

VansSlotRootMotionFrame VansAnimationSlotRuntime::GetRootMotionFrame(const std::string& slotId) const
{
	const auto found=m_DefinitionById.find(slotId);
	return found==m_DefinitionById.end() ? VansSlotRootMotionFrame{} : m_States[found->second].rootMotionFrame;
}

void VansAnimationSlotRuntime::Reset()
{
	m_RootMotionHandle = {};
	m_QueuedNotifyStates.clear();
	m_ActiveNotifyStates.clear();
	m_EndedNotifyStates.clear();
	m_NotifyStateEvents.clear();
	for (SlotState& state : m_States)
		state = {};
	m_Statuses.clear();
	m_LifecycleEvents.clear();
	m_PendingLifecycleEvents.clear();
}

void VansAnimationSlotRuntime::TransferRuntimeStateFrom(
	const VansAnimationSlotRuntime& previous,
	const std::unordered_map<std::string, VansAnimationClip>& clips)
{
	m_PendingLifecycleEvents.clear();
	m_NextHandle = std::max(m_NextHandle, previous.m_NextHandle);
	m_QueuedNotifyStates.clear();
	m_EndedNotifyStates.clear();
	m_NotifyStateEvents.clear();
	m_ActiveNotifyStates = previous.m_ActiveNotifyStates;
	auto interruptByReload = [&](const VansAnimationSlotDefinition& definition,
		const RequestRuntime& request)
	{
		VansSlotPlaybackStatus status;
		status.state = VansSlotPlaybackState::Interrupted;
		status.slotId = definition.id;
		status.clipName = request.request.clipName;
		status.tag = request.request.tag;
		status.playbackTime = request.currentTime;
		status.weight = 0.0f;
		m_Statuses[request.handle.value] = std::move(status);
		m_PendingLifecycleEvents.push_back({ VansSlotLifecycleEventType::InterruptedByReload,
			request.handle, definition.id, request.request.clipName, request.request.tag });
	};
	auto transferRequest = [&](const VansAnimationSlotDefinition& previousDefinition,
		const RequestRuntime& request, std::optional<RequestRuntime>& destination)
	{
		if (clips.find(request.request.clipName) == clips.end() ||
			(!request.request.curveOverrideClipName.empty() && clips.find(request.request.curveOverrideClipName)==clips.end()))
		{
			interruptByReload(previousDefinition, request);
			return;
		}
		destination = request;
		destination->hasRootMotion = clips.at(request.request.clipName).rootMotion.enabled;
		const auto status = previous.m_Statuses.find(request.handle.value);
		if (status != previous.m_Statuses.end())
			m_Statuses[request.handle.value] = status->second;
	};

	for (std::size_t previousIndex = 0; previousIndex < previous.m_Definitions.size(); ++previousIndex)
	{
		const VansAnimationSlotDefinition& previousDefinition = previous.m_Definitions[previousIndex];
		const SlotState& previousState = previous.m_States[previousIndex];
		const auto currentDefinition = m_DefinitionById.find(previousDefinition.id);
		if (currentDefinition == m_DefinitionById.end())
		{
			if (previousState.active) interruptByReload(previousDefinition, *previousState.active);
			for (const auto& outgoing : previousState.outgoing) interruptByReload(previousDefinition, outgoing);
			for (const RequestRuntime& request : previousState.queue)
				interruptByReload(previousDefinition, request);
			continue;
		}

		SlotState& destination = m_States[currentDefinition->second];
		destination.active.reset();
		destination.queue.clear();
		destination.outgoing.clear();
		if (previousState.active)
			transferRequest(previousDefinition, *previousState.active, destination.active);
		for (const auto& outgoing : previousState.outgoing)
		{
			std::optional<RequestRuntime> transferred;
			transferRequest(previousDefinition, outgoing, transferred);
			if (transferred) destination.outgoing.push_back(std::move(*transferred));
		}
		for (const RequestRuntime& request : previousState.queue)
		{
			if (clips.find(request.request.clipName) == clips.end())
			{
				interruptByReload(previousDefinition, request);
				continue;
			}
			destination.queue.push_back(request);
			destination.queue.back().hasRootMotion=clips.at(request.request.clipName).rootMotion.enabled;
			const auto status = previous.m_Statuses.find(request.handle.value);
			if (status != previous.m_Statuses.end())
				m_Statuses[request.handle.value] = status->second;
		}
	}
	m_RootMotionHandle={};
	for (const auto& state:m_States)
		if (state.active && state.active->handle==previous.m_RootMotionHandle && state.active->hasRootMotion)
			m_RootMotionHandle=state.active->handle;
}

void VansAnimationSlotRuntime::StartRequest(std::size_t slotIndex, RequestRuntime request)
{
	// 接受并实际启动后才替换同组实例；排队或被拒绝的请求不打断其他 Slot。
	const auto& definition = m_Definitions[slotIndex];
	for (std::size_t index = 0; index < m_States.size(); ++index)
	{
		if (index != slotIndex && (definition.group.empty()
			|| definition.group != m_Definitions[index].group)) continue;
		auto& other = m_States[index];
		for (auto& outgoing : other.outgoing)
			RetargetBlendOut(index, outgoing, request.blendIn, request.request.blendInOption);
		if (other.active)
			BeginBlendOut(index, VansSlotLifecycleEventType::Interrupted, request.blendIn,
				request.request.blendInOption);
		if (index != slotIndex)
		{
			for (const auto& queued : other.queue)
			{
				m_Statuses[queued.handle.value].state = VansSlotPlaybackState::Interrupted;
				PublishLifecycle(index, queued, VansSlotLifecycleEventType::Interrupted);
			}
			other.queue.clear();
		}
	}
	SlotState& state = m_States[slotIndex];
	state.active = std::move(request);
	// 原始根运动有明确实例身份；停止新实例后不能回退到仍在播放的旧组。
	if (state.active->hasRootMotion) m_RootMotionHandle=state.active->handle;
	VansSlotPlaybackStatus& status = m_Statuses[state.active->handle.value];
	status.state = state.active->blendIn > 0.0f
		? VansSlotPlaybackState::BlendingIn : VansSlotPlaybackState::Playing;
	PublishLifecycle(slotIndex, *state.active, VansSlotLifecycleEventType::Started);
}

void VansAnimationSlotRuntime::BeginBlendOut(
	std::size_t slotIndex,
	VansSlotLifecycleEventType reason,
	float duration,
	std::optional<VansSlotBlendOption> option)
{
	SlotState& state = m_States[slotIndex];
	if (!state.active)
		return;
	if (state.active->handle==m_RootMotionHandle) m_RootMotionHandle={};
	state.outgoing.push_back(std::move(*state.active));
	auto& outgoing = state.outgoing.back();
	state.active.reset();
	outgoing.fadeElapsed = 0.0f;
	outgoing.fadeDuration = duration;
	outgoing.fadeStartWeight = outgoing.weight;
	outgoing.fadeOption = option.value_or(outgoing.request.blendOutOption);
	outgoing.interrupted = reason == VansSlotLifecycleEventType::Interrupted;
	if (duration <= 0.0f) outgoing.weight = 0.0f;
	m_Statuses[outgoing.handle.value].state = VansSlotPlaybackState::BlendingOut;
	m_Statuses[outgoing.handle.value].weight = outgoing.weight;
	PublishLifecycle(slotIndex, outgoing, VansSlotLifecycleEventType::BlendingOut);
	if (reason == VansSlotLifecycleEventType::Interrupted)
		PublishLifecycle(slotIndex, outgoing, VansSlotLifecycleEventType::Interrupted);
}

void VansAnimationSlotRuntime::RetargetBlendOut(
	std::size_t slotIndex, RequestRuntime& request, float duration,
	std::optional<VansSlotBlendOption> option)
{
	if (!request.interrupted)
	{
		request.interrupted = true;
		PublishLifecycle(slotIndex, request, VansSlotLifecycleEventType::Interrupted);
	}
	// 已停止实例只允许缩短其原淡出时长，从当前权重重新开始，不能延长。
	if (duration < request.fadeDuration)
	{
		request.fadeElapsed = 0.0f;
		request.fadeStartWeight = request.weight;
		request.fadeDuration = duration;
		request.fadeOption = option.value_or(request.request.blendOutOption);
		if (duration <= 0.0f) request.weight = 0.0f;
		m_Statuses[request.handle.value].weight = request.weight;
	}
}

void VansAnimationSlotRuntime::PublishLifecycle(
	std::size_t slotIndex,
	const RequestRuntime& request,
	VansSlotLifecycleEventType type)
{
	m_LifecycleEvents.push_back({ type, request.handle, m_Definitions[slotIndex].id,
		request.request.clipName, request.request.tag });
}

bool VansAnimationSlotRuntime::SampleRequest(
	RequestRuntime& runtime,
	const VansAnimationClip& clip,
	const Skeleton& skeleton,
	VansPosePayload& payload) const
{
	VansAnimationSampleRequest request;
	request.previousTime = runtime.previousTime;
	request.currentTime = runtime.currentTime;
	request.loop = runtime.request.loopCount > 1;
	request.sourceNodeId = 0;
	request.extractRootMotion = !runtime.request.suppressRootMotion;
	if (!VansAnimationSampler::Sample(clip, skeleton, request, payload))
		return false;
	// 主动打断后，源 Montage 不再收集自身或序列通知；自然淡出仍可收集。
	if (runtime.interrupted) payload.events.clear();
	if (runtime.request.suppressRootMotion)
		payload.rootMotion.valid = false;
	payload.sourceWeight = runtime.weight;
	payload.sourceAdditive = runtime.request.additive;
	payload.sourceBoneMask.assign(runtime.request.boneMaskWeights.begin(), runtime.request.boneMaskWeights.end());
	if (!runtime.request.syncGroup.empty())
	{
		payload.sync.groupId = VansAnimationStableId(runtime.request.syncGroup);
		payload.sync.valid = true;
		if (!runtime.request.markerSync)
		{
			payload.sync.markerId = 0;
			payload.sync.nextMarkerId = 0;
		}
	}
	return true;
}

void VansAnimationSlotRuntime::CollectNotifyStates(
	const RequestRuntime& runtime, const VansAnimationClip& clip)
{
	if (!runtime.notifyStates || runtime.interrupted || !runtime.notifyRangePending) return;
	for (std::size_t index = 0; index < runtime.notifyStates->size(); ++index)
	{
		const auto& definition = (*runtime.notifyStates)[index];
		if (runtime.notifyWeight < definition.minimumWeight) continue;
		const bool forward = runtime.currentTime >= runtime.previousTime;
		bool intersects = forward
			? definition.startTime <= runtime.currentTime && definition.endTime > runtime.previousTime
			: definition.startTime < runtime.previousTime && definition.endTime >= runtime.currentTime;
		std::int64_t loopIndex = 0;
		if (runtime.request.loopCount > 1)
		{
			// 只求是否存在交叠周期，不按大帧跨越的循环数分配/遍历。
			const double duration = clip.duration;
			const double period = forward
				? std::floor((double(runtime.previousTime) - definition.endTime) / duration) + 1.0
				: std::ceil((double(runtime.currentTime) - definition.endTime) / duration);
			const double start = definition.startTime + period * duration;
			intersects = forward ? start <= runtime.currentTime : start < runtime.previousTime;
			loopIndex = static_cast<std::int64_t>(period);
		}
		if (!intersects) continue;
		NotifyStateReference reference;
		reference.definitions = runtime.notifyStates;
		reference.index = index;
		reference.sample.id = VansAnimationStableId(definition.id);
		reference.sample.clipId = clip.stableId ? clip.stableId : VansAnimationStableId(clip.clipName);
		reference.sample.name = definition.name;
		reference.sample.sourceTime = runtime.currentTime;
		reference.sample.loopIndex = loopIndex;
		reference.sample.weight = runtime.notifyWeight;
		reference.sample.forward = forward;
		reference.sample.payload = definition.payload;
		reference.sample.sourceInstanceId = runtime.handle.value;
		m_QueuedNotifyStates.push_back(std::move(reference));
	}
}

void VansAnimationSlotRuntime::FinalizeNotifyStates(float deltaTime)
{
	std::sort(m_QueuedNotifyStates.begin(), m_QueuedNotifyStates.end(),
		[](const auto& lhs, const auto& rhs)
		{
			return lhs.sample.sourceInstanceId != rhs.sample.sourceInstanceId
				? lhs.sample.sourceInstanceId < rhs.sample.sourceInstanceId : lhs.index < rhs.index;
		});
	// 同一作者通知对象在多实例/多循环中只进入一次队列。
	for (std::size_t index = 0; index < m_QueuedNotifyStates.size(); ++index)
		for (std::size_t next = index + 1; next < m_QueuedNotifyStates.size();)
			if (m_QueuedNotifyStates[index].Definition().id == m_QueuedNotifyStates[next].Definition().id)
				m_QueuedNotifyStates.erase(m_QueuedNotifyStates.begin() + next);
			else ++next;
	for (auto& reference : m_QueuedNotifyStates)
	{
		const auto existing = std::find_if(m_ActiveNotifyStates.begin(), m_ActiveNotifyStates.end(),
			[&](const auto& active) { return active.Definition().id == reference.Definition().id; });
		if (existing == m_ActiveNotifyStates.end()) reference.sample.phase = VansAnimationEventPhase::Begin;
		else
		{
			reference.sample.phase = VansAnimationEventPhase::Tick;
			// 保持源 RemoveAtSwap 的未匹配 End 顺序。
			*existing = std::move(m_ActiveNotifyStates.back());
			m_ActiveNotifyStates.pop_back();
		}
	}
	m_EndedNotifyStates.swap(m_ActiveNotifyStates);
	for (const auto& ended : m_EndedNotifyStates)
	{
		auto event = ended.sample;
		event.phase = VansAnimationEventPhase::End;
		event.deltaTime = 0.0f;
		m_NotifyStateEvents.push_back(std::move(event));
	}
	for (const auto& begun : m_QueuedNotifyStates)
		if (begun.sample.phase == VansAnimationEventPhase::Begin)
			m_NotifyStateEvents.push_back(begun.sample);
	m_ActiveNotifyStates.swap(m_QueuedNotifyStates);
	for (const auto& active : m_ActiveNotifyStates)
	{
		auto event = active.sample;
		event.phase = VansAnimationEventPhase::Tick;
		event.deltaTime = deltaTime;
		m_NotifyStateEvents.push_back(std::move(event));
	}
	// 完成事件在普通通知之后派发；零时长自动淡出可能刚收集过范围。
	// 按实例创建顺序处理终止，同一实例的残余范围从后向前 End/RemoveAtSwap。
	for (;;)
	{
		std::uint64_t completedInstance = std::numeric_limits<std::uint64_t>::max();
		for (const auto& active : m_ActiveNotifyStates)
		{
			const auto status = m_Statuses.find(active.sample.sourceInstanceId);
			if (status != m_Statuses.end() && status->second.state == VansSlotPlaybackState::Completed)
				completedInstance = std::min(completedInstance, active.sample.sourceInstanceId);
		}
		if (completedInstance == std::numeric_limits<std::uint64_t>::max()) break;
		for (std::size_t remaining = m_ActiveNotifyStates.size(); remaining > 0; --remaining)
		{
			const auto index = remaining - 1;
			if (m_ActiveNotifyStates[index].sample.sourceInstanceId != completedInstance) continue;
			m_EndedNotifyStates.push_back(std::move(m_ActiveNotifyStates[index]));
			auto event = m_EndedNotifyStates.back().sample;
			event.phase = VansAnimationEventPhase::End;event.deltaTime = 0.0f;
			m_NotifyStateEvents.push_back(std::move(event));
			m_ActiveNotifyStates[index] = std::move(m_ActiveNotifyStates.back());
			m_ActiveNotifyStates.pop_back();
		}
	}
}

void VansAnimationSlotRuntime::Update(
	float deltaTime,
	const std::unordered_map<std::string, VansAnimationClip>& clips,
	const Skeleton& skeleton,
	std::unordered_map<std::string, VansSlotPoseInputs>& outSlotPayloads)
{
	m_LifecycleEvents = std::move(m_PendingLifecycleEvents);
	m_PendingLifecycleEvents.clear();
	m_NotifyStateEvents.clear();
	m_EndedNotifyStates.clear();
	m_QueuedNotifyStates.clear();
	for (auto& [slotId, payload] : outSlotPayloads)
		payload.poses.clear();
	deltaTime = std::max(0.0f, deltaTime);
	const auto playbackDuration = [](const VansAnimationClip& clip,
		const RequestRuntime& runtime)
	{
		// startTime is an absolute clip sample position.  A non-looping request
		// therefore owns only the remaining part of the clip; using the full
		// duration here would leave a montage with a non-zero start time alive
		// past its authored end and would delay its blend-out.
		const float start = std::clamp(runtime.request.startTime, 0.0f, clip.duration);
		const float firstSpan = runtime.request.playRate >= 0.0f
			? std::max(0.0f, clip.duration - start)
			: start;
		if (runtime.request.loopCount <= 1)
			return firstSpan;
		return firstSpan + clip.duration * static_cast<float>(runtime.request.loopCount - 1);
	};
	for (std::size_t slotIndex = 0; slotIndex < m_States.size(); ++slotIndex)
	{
		SlotState& state = m_States[slotIndex];
		if (!state.active && state.outgoing.empty() && !state.queue.empty())
		{
			RequestRuntime next = std::move(state.queue.front());
			state.queue.pop_front();
			StartRequest(slotIndex, std::move(next));
		}

		// UE 顺序：先按帧秒数更新权重，再推进片段时间并触发自动淡出。
		state.rootMotionFrame={};
		// 本帧推进过程中触发的淡出从下一次权重更新开始，不能追回本帧 DeltaTime。
		auto advancePosition = [&](RequestRuntime& runtime, const VansAnimationClip& clip, bool extractRawRoot)
		{
			if (runtime.request.externallyDriven) return;
			runtime.previousTime = runtime.currentTime;
			runtime.notifyRangePending = !runtime.reachedEnd && deltaTime > 0.0f;
			if (runtime.reachedEnd) return;
			const bool forward = runtime.request.playRate > 0.0f;
			const float start = std::clamp(runtime.request.startTime, 0.0f, clip.duration);
			const float end = start + (forward ? 1.0f : -1.0f) * playbackDuration(clip, runtime);
			runtime.currentTime += deltaTime * runtime.request.playRate;
			if ((forward && runtime.currentTime >= end) || (!forward && runtime.currentTime <= end))
			{
				runtime.reachedEnd = true;
				// UE 最后一段前向停在 End - KINDA_SMALL_NUMBER/2，避免跨过末端通知。
				runtime.currentTime = forward ? std::max(start, end - 0.00005f) : end;
			}
			// 在终点触发零时长淡出并销毁实例之前提取本帧最后的根运动区间。
			if (extractRawRoot && !runtime.request.suppressRootMotion && runtime.weight > 0.0f &&
				runtime.previousTime != runtime.currentTime && runtime.handle.value > state.rootMotionFrame.handle.value)
			{
				VansAnimationSampleRequest rootRequest;
				rootRequest.previousTime=runtime.previousTime;rootRequest.currentTime=runtime.currentTime;
				rootRequest.loop=runtime.request.loopCount>1;
				const auto root=VansAnimationSampler::ExtractRootMotion(clip,skeleton,rootRequest);
				if (root.valid) state.rootMotionFrame={root,runtime.handle};
			}
		};
		auto retireOutgoing = [&]()
		{
			for (auto it = state.outgoing.begin(); it != state.outgoing.end();)
			{
				auto& status = m_Statuses[it->handle.value];
				status.playbackTime = it->currentTime;
				status.weight = it->weight;
				if (it->weight > 0.0f) { ++it; continue; }
				status.state = it->interrupted ? VansSlotPlaybackState::Interrupted : VansSlotPlaybackState::Completed;
				if (!it->interrupted) PublishLifecycle(slotIndex, *it, VansSlotLifecycleEventType::Completed);
				it = state.outgoing.erase(it);
			}
		};
		for (auto& outgoing : state.outgoing)
		{
			const float previousWeight = outgoing.weight;
			outgoing.fadeElapsed += deltaTime;
			outgoing.weight = outgoing.fadeDuration <= 0.0f ? 0.0f
				: outgoing.fadeStartWeight * (1.0f - BlendAlpha(
					outgoing.fadeElapsed / outgoing.fadeDuration, outgoing.fadeOption));
			outgoing.notifyWeight = std::max(previousWeight, outgoing.weight);
			const auto clip = clips.find(outgoing.request.clipName);
			if (clip != clips.end() && outgoing.fadeDuration > 0.0f)
			{
				advancePosition(outgoing, clip->second,false);
				if (outgoing.weight > 0.0f) CollectNotifyStates(outgoing, clip->second);
			}
		}
		retireOutgoing();

		if (state.active)
		{
			auto& active = *state.active;
			const auto clip = clips.find(active.request.clipName);
			auto& status = m_Statuses[active.handle.value];
			if (clip == clips.end())
			{
				if (active.handle==m_RootMotionHandle) m_RootMotionHandle={};
				status.state = VansSlotPlaybackState::Completed;
				status.weight = 0.0f;
				PublishLifecycle(slotIndex, active, VansSlotLifecycleEventType::Completed);
				state.active.reset();
			}
			else
			{
				active.blendElapsed += deltaTime;
				const float previousWeight = active.weight;
				active.weight = active.request.externallyDriven ? active.request.weight
					: (active.blendIn <= 0.0f ? 1.0f : BlendAlpha(
						active.blendElapsed / active.blendIn, active.request.blendInOption));
				active.notifyWeight = std::max(previousWeight, active.weight);
				advancePosition(active, clip->second,active.handle==m_RootMotionHandle);
				CollectNotifyStates(active, clip->second);
				status.playbackTime = active.currentTime;
				status.weight = active.weight;
				status.state = !active.request.externallyDriven && active.blendElapsed < active.blendIn
					? VansSlotPlaybackState::BlendingIn : VansSlotPlaybackState::Playing;
				if (!active.request.externallyDriven && deltaTime > 0.0f)
				{
					const float elapsed = std::abs(active.currentTime - active.request.startTime);
					const float remaining = active.reachedEnd ? 0.0f :
						std::max(0.0f, playbackDuration(clip->second, active) - elapsed) / std::abs(active.request.playRate);
					const bool custom = active.request.blendOutTriggerTime >= 0.0f;
					const float trigger = custom ? active.request.blendOutTriggerTime : active.blendOut;
					if (remaining <= std::max(trigger, 0.0001f))
					{
						BeginBlendOut(slotIndex, VansSlotLifecycleEventType::Completed,
							custom ? active.blendOut : remaining);
						retireOutgoing();
					}
				}
			}
		}

		auto destination = outSlotPayloads.find(m_Definitions[slotIndex].id);
		auto sample = [&](RequestRuntime& request)
		{
			const auto clip = clips.find(request.request.clipName);
			VansPosePayload payload;
			if (destination != outSlotPayloads.end() && clip != clips.end() &&
				SampleRequest(request, clip->second, skeleton, payload))
			{
				const auto curveClip=clips.find(request.request.curveOverrideClipName);
				if (curveClip!=clips.end())
				{
					const float time=VansAnimationSampler::ResolveSampleTime(request.currentTime,0.f,
						curveClip->second.duration,request.request.loopCount>1);
					for (const auto& curve:curveClip->second.curves)
					{
						if(curve.keys.empty())continue;
						const auto id=curve.id?curve.id:VansAnimationStableId(curve.name);
						const float value=VansAnimationSampler::SampleCurve(curve,time);
						const auto existing=std::find_if(payload.curves.begin(),payload.curves.end(),
							[id](const auto& sample){return sample.id==id;});
						if(existing!=payload.curves.end())existing->value=value;
						else payload.curves.push_back({id,curve.name,value,true});
					}
				}
				if (state.active && request.handle==m_RootMotionHandle && request.request.externallyDriven &&
					payload.rootMotion.valid && request.previousTime != request.currentTime &&
					request.handle.value > state.rootMotionFrame.handle.value)
					state.rootMotionFrame={payload.rootMotion,request.handle};
				destination->second.poses.push_back(std::move(payload));
			}
			// 通知区间的消费不依赖图是否接收该 Slot 姿态或采样是否成功。
			if (request.request.externallyDriven)
			{
				request.previousTime=request.currentTime;
				request.notifyRangePending = false;
			}
		};
		// 请求按创建顺序传给图，淡出片段不提前归一化，也不被后续替换丢弃。
		for (auto& outgoing : state.outgoing) sample(outgoing);
		if (state.active) sample(*state.active);
	}
	FinalizeNotifyStates(deltaTime);
}
