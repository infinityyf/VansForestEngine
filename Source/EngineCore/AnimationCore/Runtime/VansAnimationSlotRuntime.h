#pragma once

#include "../VansAnimationSampler.h"
#include "../VansPosePayloadMixer.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	enum class VansSlotConcurrency { Replace, Queue, Reject };
	enum class VansSlotPlaybackState { Invalid, Queued, BlendingIn, Playing, BlendingOut, Completed, Interrupted, Rejected };
	enum class VansSlotLifecycleEventType
	{
		Started,
		BlendingOut,
		Completed,
		Interrupted,
		InterruptedByReload,
		Rejected
	};

	struct VansAnimationSlotDefinition
	{
		std::string id;
		std::string name;
		std::string layerId;
		int slotNodeId = -1;
		VansSlotConcurrency concurrency = VansSlotConcurrency::Replace;
		std::uint32_t maxQueueDepth = 4;
		float defaultBlendIn = 0.08f;
		float defaultBlendOut = 0.12f;
		bool interruptible = true;
	};

	struct VansSlotPlayRequest
	{
		std::string clipName;
		float playRate = 1.0f;
		float startTime = 0.0f;
		int loopCount = 1;
		int priority = 0;
		std::optional<float> blendIn;
		std::optional<float> blendOut;
		float weight = 1.0f;
		bool externallyDriven = false;
		bool suppressRootMotion = false;
		bool additive = false;
		std::vector<float> boneMaskWeights;
		std::string syncGroup;
		bool markerSync = false;
		std::string tag;
	};

	struct VansSlotPlaybackHandle
	{
		std::uint64_t value = 0;
		explicit operator bool() const { return value != 0; }
		friend bool operator==(VansSlotPlaybackHandle lhs, VansSlotPlaybackHandle rhs) { return lhs.value == rhs.value; }
	};

	struct VansSlotPlaybackStatus
	{
		VansSlotPlaybackState state = VansSlotPlaybackState::Invalid;
		std::string slotId;
		std::string clipName;
		std::string tag;
		float playbackTime = 0.0f;
		float weight = 0.0f;
	};

	struct VansSlotLifecycleEvent
	{
		VansSlotLifecycleEventType type = VansSlotLifecycleEventType::Started;
		VansSlotPlaybackHandle handle;
		std::string slotId;
		std::string clipName;
		std::string tag;
	};

	class VansAnimationSlotRuntime
	{
	public:
		bool Configure(std::vector<VansAnimationSlotDefinition> definitions, std::string& error);
		VansSlotPlaybackHandle Play(const std::string& slotId, const VansSlotPlayRequest& request);
		bool Stop(VansSlotPlaybackHandle handle, float blendOut, bool force = false);
		bool Drive(VansSlotPlaybackHandle handle, float playbackTime, float weight);
		VansSlotPlaybackStatus GetStatus(VansSlotPlaybackHandle handle) const;
		bool IsSlotActive(const std::string& slotId) const;
		void Reset();
		void TransferRuntimeStateFrom(
			const VansAnimationSlotRuntime& previous,
			const std::unordered_map<std::string, VansAnimationClip>& clips);

		void Update(float deltaTime,
		            const std::unordered_map<std::string, VansAnimationClip>& clips,
		            const Skeleton& skeleton,
		            std::unordered_map<std::string, VansPosePayload>& outSlotPayloads);

		const std::vector<VansSlotLifecycleEvent>& GetLifecycleEvents() const { return m_LifecycleEvents; }
		const std::vector<VansAnimationSlotDefinition>& GetDefinitions() const { return m_Definitions; }

	private:
		struct RequestRuntime
		{
			VansSlotPlaybackHandle handle;
			VansSlotPlayRequest request;
			float previousTime = 0.0f;
			float currentTime = 0.0f;
			float blendIn = 0.0f;
			float blendOut = 0.0f;
			float fadeElapsed = 0.0f;
			float fadeDuration = 0.0f;
			float fadeStartWeight = 1.0f;
			float weight = 0.0f;
			bool stopped = false;
		};

		struct SlotState
		{
			std::optional<RequestRuntime> active;
			std::optional<RequestRuntime> outgoing;
			std::deque<RequestRuntime> queue;
		};

		std::vector<VansAnimationSlotDefinition> m_Definitions;
		std::unordered_map<std::string, std::size_t> m_DefinitionById;
		std::vector<SlotState> m_States;
		std::unordered_map<std::uint64_t, VansSlotPlaybackStatus> m_Statuses;
		std::vector<VansSlotLifecycleEvent> m_LifecycleEvents;
		std::vector<VansSlotLifecycleEvent> m_PendingLifecycleEvents;
		std::uint64_t m_NextHandle = 1;

		void StartRequest(std::size_t slotIndex, RequestRuntime request);
		void BeginBlendOut(std::size_t slotIndex, VansSlotLifecycleEventType reason,
		                   float duration);
		void PublishLifecycle(std::size_t slotIndex, const RequestRuntime& request,
		                      VansSlotLifecycleEventType type);
		bool SampleRequest(RequestRuntime& runtime, const VansAnimationClip& clip,
		                   const Skeleton& skeleton, VansPosePayload& payload) const;
	};
}
