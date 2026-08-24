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
	return true;
}

VansSlotPlaybackHandle VansAnimationSlotRuntime::Play(
	const std::string& slotId,
	const VansSlotPlayRequest& request)
{
	auto definitionIt = m_DefinitionById.find(slotId);
	if (definitionIt == m_DefinitionById.end() || request.clipName.empty()
		|| !std::isfinite(request.playRate) || request.playRate == 0.0f
		|| !IsFiniteNonNegative(request.startTime) || request.loopCount <= 0
		|| !std::isfinite(request.weight) || request.weight < 0.0f
		|| (request.blendIn && !IsFiniteNonNegative(*request.blendIn))
		|| (request.blendOut && !IsFiniteNonNegative(*request.blendOut)))
		return {};

	const std::size_t slotIndex = definitionIt->second;
	const VansAnimationSlotDefinition& definition = m_Definitions[slotIndex];
	SlotState& state = m_States[slotIndex];
	RequestRuntime runtime;
	runtime.handle.value = m_NextHandle++;
	runtime.request = request;
	runtime.previousTime = request.startTime;
	runtime.currentTime = request.startTime;
	runtime.blendIn = request.blendIn.value_or(definition.defaultBlendIn);
	runtime.blendOut = request.blendOut.value_or(definition.defaultBlendOut);
	runtime.weight = runtime.blendIn <= 0.0f ? 1.0f : 0.0f;

	VansSlotPlaybackStatus status;
	status.slotId = definition.id;
	status.clipName = request.clipName;
	status.tag = request.tag;
	status.playbackTime = request.startTime;
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
		BeginBlendOut(slotIndex, VansSlotLifecycleEventType::Interrupted, state.active->blendOut);
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
	};
	for (SlotState& state : m_States)
	{
		if (state.active && state.active->handle == handle) { drive(*state.active); return true; }
		if (state.outgoing && state.outgoing->handle == handle) { drive(*state.outgoing); return true; }
		for (RequestRuntime& queued : state.queue)
			if (queued.handle == handle) { drive(queued); return true; }
	}
	return false;
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
		&& (m_States[found->second].active.has_value() || m_States[found->second].outgoing.has_value());
}

void VansAnimationSlotRuntime::Reset()
{
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
		if (clips.find(request.request.clipName) == clips.end())
		{
			interruptByReload(previousDefinition, request);
			return;
		}
		destination = request;
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
			if (previousState.outgoing) interruptByReload(previousDefinition, *previousState.outgoing);
			for (const RequestRuntime& request : previousState.queue)
				interruptByReload(previousDefinition, request);
			continue;
		}

		SlotState& destination = m_States[currentDefinition->second];
		if (previousState.active)
			transferRequest(previousDefinition, *previousState.active, destination.active);
		if (previousState.outgoing)
			transferRequest(previousDefinition, *previousState.outgoing, destination.outgoing);
		for (const RequestRuntime& request : previousState.queue)
		{
			if (clips.find(request.request.clipName) == clips.end())
			{
				interruptByReload(previousDefinition, request);
				continue;
			}
			destination.queue.push_back(request);
			const auto status = previous.m_Statuses.find(request.handle.value);
			if (status != previous.m_Statuses.end())
				m_Statuses[request.handle.value] = status->second;
		}
	}
}

void VansAnimationSlotRuntime::StartRequest(std::size_t slotIndex, RequestRuntime request)
{
	SlotState& state = m_States[slotIndex];
	state.active = std::move(request);
	VansSlotPlaybackStatus& status = m_Statuses[state.active->handle.value];
	status.state = state.active->blendIn > 0.0f
		? VansSlotPlaybackState::BlendingIn : VansSlotPlaybackState::Playing;
	PublishLifecycle(slotIndex, *state.active, VansSlotLifecycleEventType::Started);
}

void VansAnimationSlotRuntime::BeginBlendOut(
	std::size_t slotIndex,
	VansSlotLifecycleEventType reason,
	float duration)
{
	SlotState& state = m_States[slotIndex];
	if (!state.active)
		return;
	state.outgoing = std::move(state.active);
	state.active.reset();
	state.outgoing->fadeElapsed = 0.0f;
	state.outgoing->fadeDuration = duration;
	state.outgoing->fadeStartWeight = state.outgoing->weight;
	state.outgoing->stopped = true;
	m_Statuses[state.outgoing->handle.value].state = VansSlotPlaybackState::BlendingOut;
	PublishLifecycle(slotIndex, *state.outgoing, VansSlotLifecycleEventType::BlendingOut);
	if (reason == VansSlotLifecycleEventType::Interrupted)
		PublishLifecycle(slotIndex, *state.outgoing, VansSlotLifecycleEventType::Interrupted);
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
	if (!VansAnimationSampler::Sample(clip, skeleton, request, payload))
		return false;
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

void VansAnimationSlotRuntime::Update(
	float deltaTime,
	const std::unordered_map<std::string, VansAnimationClip>& clips,
	const Skeleton& skeleton,
	std::unordered_map<std::string, VansPosePayload>& outSlotPayloads)
{
	m_LifecycleEvents = std::move(m_PendingLifecycleEvents);
	m_PendingLifecycleEvents.clear();
	for (auto& [slotId, payload] : outSlotPayloads)
		payload = {};
	deltaTime = std::max(0.0f, deltaTime);
	for (std::size_t slotIndex = 0; slotIndex < m_States.size(); ++slotIndex)
	{
		SlotState& state = m_States[slotIndex];
		if (!state.active && !state.queue.empty())
		{
			RequestRuntime next = std::move(state.queue.front());
			state.queue.pop_front();
			StartRequest(slotIndex, std::move(next));
		}

		auto advance = [&](RequestRuntime& runtime, bool outgoing)
		{
			if (!runtime.request.externallyDriven)
			{
				runtime.previousTime = runtime.currentTime;
				runtime.currentTime += deltaTime * runtime.request.playRate;
			}
			if (outgoing)
			{
				runtime.fadeElapsed += deltaTime;
				runtime.weight = runtime.fadeDuration <= 0.0f ? 0.0f
					: runtime.fadeStartWeight * std::max(0.0f, 1.0f - runtime.fadeElapsed / runtime.fadeDuration);
				return;
			}
			if (runtime.request.externallyDriven)
			{
				runtime.weight = runtime.request.weight;
				return;
			}
			const auto clip = clips.find(runtime.request.clipName);
			const float totalDuration = clip == clips.end() ? 0.0f
				: clip->second.duration * static_cast<float>(runtime.request.loopCount);
			const float elapsed = std::abs(runtime.currentTime - runtime.request.startTime);
			const float fadeInWeight = runtime.blendIn <= 0.0f ? 1.0f : std::min(1.0f, elapsed / runtime.blendIn);
			const float remaining = std::max(0.0f, totalDuration - elapsed);
			const float fadeOutWeight = runtime.blendOut <= 0.0f ? 1.0f : std::min(1.0f, remaining / runtime.blendOut);
			runtime.weight = std::min(fadeInWeight, fadeOutWeight);
		};

		if (state.outgoing)
		{
			advance(*state.outgoing, true);
			VansSlotPlaybackStatus& status = m_Statuses[state.outgoing->handle.value];
			status.playbackTime = state.outgoing->currentTime;
			status.weight = state.outgoing->weight;
			if (state.outgoing->weight <= 0.0f)
			{
				status.state = VansSlotPlaybackState::Interrupted;
				state.outgoing.reset();
			}
		}

		if (state.active)
		{
			advance(*state.active, false);
			auto clip = clips.find(state.active->request.clipName);
			VansSlotPlaybackStatus& status = m_Statuses[state.active->handle.value];
			status.playbackTime = state.active->currentTime;
			status.weight = state.active->weight;
			status.state = state.active->weight < 1.0f
				? (state.active->currentTime - state.active->request.startTime < state.active->blendIn
					? VansSlotPlaybackState::BlendingIn : VansSlotPlaybackState::BlendingOut)
				: VansSlotPlaybackState::Playing;
			const float totalDuration = clip == clips.end() ? 0.0f
				: clip->second.duration * static_cast<float>(state.active->request.loopCount);
			if (clip == clips.end() || (!state.active->request.externallyDriven &&
				std::abs(state.active->currentTime - state.active->request.startTime) >= totalDuration))
			{
				status.state = VansSlotPlaybackState::Completed;
				status.weight = 0.0f;
				PublishLifecycle(slotIndex, *state.active, VansSlotLifecycleEventType::Completed);
				state.active.reset();
			}
		}

		VansPosePayload activePayload;
		VansPosePayload outgoingPayload;
		bool hasActive = false;
		bool hasOutgoing = false;
		if (state.active)
		{
			auto clip = clips.find(state.active->request.clipName);
			hasActive = clip != clips.end() && SampleRequest(*state.active, clip->second, skeleton, activePayload);
		}
		if (state.outgoing)
		{
			auto clip = clips.find(state.outgoing->request.clipName);
			hasOutgoing = clip != clips.end() && SampleRequest(*state.outgoing, clip->second, skeleton, outgoingPayload);
		}
		if (!hasActive && !hasOutgoing)
			continue;
		VansPosePayload result = hasActive ? activePayload : outgoingPayload;
		if (hasActive && hasOutgoing)
		{
			const float sum = activePayload.sourceWeight + outgoingPayload.sourceWeight;
			const float alpha = sum > 0.0f ? activePayload.sourceWeight / sum : 1.0f;
			result = VansPosePayloadMixer::BlendOverride(outgoingPayload, activePayload, alpha);
			result.sourceWeight = std::clamp(sum, 0.0f, 1.0f);
		}
		auto destination = outSlotPayloads.find(m_Definitions[slotIndex].id);
		if (destination != outSlotPayloads.end())
			destination->second = std::move(result);
	}
}
