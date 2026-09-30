#pragma once

#include "../VansAnimationSampler.h"
#include "../VansPosePayloadMixer.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansGraphics
{
	enum class VansSlotConcurrency { Replace, Queue, Reject };
	enum class VansSlotBlendOption { Linear, Cubic, HermiteCubic };
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
		// Slots in the same group follow UE's AnimSlot group rule: starting a
		// new request interrupts the active request in that group while slots in
		// different groups continue to blend independently.
		std::string group;
		VansSlotConcurrency concurrency = VansSlotConcurrency::Replace;
		std::uint32_t maxQueueDepth = 4;
		float defaultBlendIn = 0.08f;
		float defaultBlendOut = 0.12f;
		bool interruptible = true;
	};

	// 范围身份由作者数据提供；同一身份在多次播放/循环中只保留一个活动状态。
	struct VansSlotNotifyStateDefinition
	{
		std::string id;
		std::string name;
		float startTime = 0.0f;
		float endTime = 0.0f;
		float minimumWeight = 0.0f;
		VansAnimationEventValue payload;
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
		VansSlotBlendOption blendInOption = VansSlotBlendOption::Linear;
		VansSlotBlendOption blendOutOption = VansSlotBlendOption::Linear;
		// 负值按淡出时长提前触发；非负值表示距离播放结束的真实秒数，0 表示结束时触发。
		float blendOutTriggerTime = -1.0f;
		float weight = 1.0f;
		bool externallyDriven = false;
		bool suppressRootMotion = false;
		bool additive = false;
		std::vector<float> boneMaskWeights;
		std::string syncGroup;
		bool markerSync = false;
		std::string tag;
		std::vector<VansSlotNotifyStateDefinition> notifyStates;
		// 在实例时间采样该 Clip 的曲线并覆盖姿态 Clip 的同名曲线。
		std::string curveOverrideClipName;
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

	// 最近一次实际推进的请求原始根增量，独立于该 Slot 的姿态混合权重。
	struct VansSlotRootMotionFrame
	{
		VansRootMotionDelta delta;
		VansSlotPlaybackHandle handle;
	};

	class VansAnimationSlotRuntime
	{
	public:
		bool Configure(std::vector<VansAnimationSlotDefinition> definitions, std::string& error);
		VansSlotPlaybackHandle Play(const std::string& slotId, const VansSlotPlayRequest& request,
		                           const std::unordered_map<std::string, VansAnimationClip>& clips);
		bool Stop(VansSlotPlaybackHandle handle, float blendOut, bool force = false);
		bool StopSlot(const std::string& slotId, float blendOut, bool force = false);
		bool Drive(VansSlotPlaybackHandle handle, float playbackTime, float weight);
		VansSlotPlaybackStatus GetStatus(VansSlotPlaybackHandle handle) const;
		bool IsSlotActive(const std::string& slotId) const;
		bool HasRootMotionPlayback(const std::string& slotId,
			const std::unordered_map<std::string, VansAnimationClip>& clips) const;
		VansSlotRootMotionFrame GetRootMotionFrame(const std::string& slotId) const;
		void Reset();
		void TransferRuntimeStateFrom(
			const VansAnimationSlotRuntime& previous,
			const std::unordered_map<std::string, VansAnimationClip>& clips);

		void Update(float deltaTime,
		            const std::unordered_map<std::string, VansAnimationClip>& clips,
		            const Skeleton& skeleton,
		            std::unordered_map<std::string, VansSlotPoseInputs>& outSlotPayloads);

		const std::vector<VansSlotLifecycleEvent>& GetLifecycleEvents() const { return m_LifecycleEvents; }
		const std::vector<VansAnimationSlotDefinition>& GetDefinitions() const { return m_Definitions; }
		const std::vector<VansAnimationEventSample>& GetNotifyStateEvents() const { return m_NotifyStateEvents; }

	private:
		struct RequestRuntime
		{
			VansSlotPlaybackHandle handle;
			VansSlotPlayRequest request;
			float previousTime = 0.0f;
			float currentTime = 0.0f;
			float blendIn = 0.0f;
			float blendOut = 0.0f;
			float blendElapsed = 0.0f;
			float fadeElapsed = 0.0f;
			float fadeDuration = 0.0f;
			float fadeStartWeight = 1.0f;
			VansSlotBlendOption fadeOption = VansSlotBlendOption::Linear;
			float weight = 0.0f;
			bool interrupted = false;
			bool reachedEnd = false;
			bool hasRootMotion = false;
			float notifyWeight = 0.0f;
			bool notifyRangePending = false;
			std::shared_ptr<const std::vector<VansSlotNotifyStateDefinition>> notifyStates;
		};
		struct NotifyStateReference
		{
			std::shared_ptr<const std::vector<VansSlotNotifyStateDefinition>> definitions;
			std::size_t index = 0;
			VansAnimationEventSample sample;
			const VansSlotNotifyStateDefinition& Definition() const { return (*definitions)[index]; }
		};

		struct SlotState
		{
			VansSlotRootMotionFrame rootMotionFrame;
			std::optional<RequestRuntime> active;
			std::vector<RequestRuntime> outgoing;
			std::deque<RequestRuntime> queue;
		};

		std::vector<VansAnimationSlotDefinition> m_Definitions;
		std::unordered_map<std::string, std::size_t> m_DefinitionById;
		std::vector<SlotState> m_States;
		std::unordered_map<std::uint64_t, VansSlotPlaybackStatus> m_Statuses;
		std::vector<VansSlotLifecycleEvent> m_LifecycleEvents;
		std::vector<VansSlotLifecycleEvent> m_PendingLifecycleEvents;
		std::uint64_t m_NextHandle = 1;
		VansSlotPlaybackHandle m_RootMotionHandle;
		std::vector<NotifyStateReference> m_QueuedNotifyStates;
		std::vector<NotifyStateReference> m_ActiveNotifyStates;
		// 本帧 End 输出仍引用作者数据；保留其共享所有权直到下一次求值。
		std::vector<NotifyStateReference> m_EndedNotifyStates;
		std::vector<VansAnimationEventSample> m_NotifyStateEvents;

		void StartRequest(std::size_t slotIndex, RequestRuntime request);
		void BeginBlendOut(std::size_t slotIndex, VansSlotLifecycleEventType reason,
		                   float duration, std::optional<VansSlotBlendOption> option = std::nullopt);
		void RetargetBlendOut(std::size_t slotIndex, RequestRuntime& request, float duration,
		                      std::optional<VansSlotBlendOption> option = std::nullopt);
		void PublishLifecycle(std::size_t slotIndex, const RequestRuntime& request,
		                      VansSlotLifecycleEventType type);
		bool SampleRequest(RequestRuntime& runtime, const VansAnimationClip& clip,
		                   const Skeleton& skeleton, VansPosePayload& payload) const;
		void CollectNotifyStates(const RequestRuntime& runtime, const VansAnimationClip& clip);
		void FinalizeNotifyStates(float deltaTime);
	};
}
