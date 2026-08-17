#pragma once

#include "../SceneRuntime/VansRuntimeHandle.h"
#include "../TimelineCore/VansTimelineCompiler.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace Vans
{
struct VansResolvedTimelineTarget
{
	VansTimelineBindingId bindingId;
	VansTimelineBindingKind kind = VansTimelineBindingKind::SceneEntity;
	VansStableId<struct VansRuntimeObjectTypeTag> objectType;
	VansGenerationHandle object;
	std::uint64_t changeSerial = 0;
	VansEntityHandle entity;
	VansEntityHandle rootOwner;
	VansComponentHandle component;
	std::string assetGuid;
	std::string assetPath;
	bool required = true;
	bool valid = false;
};

struct VansTimelineStableOrder
{
	std::int32_t hierarchicalBias = 0;
	std::int32_t priority = 0;
	std::int32_t trackOrder = 0;
	std::uint64_t sequence = 0;
};

struct VansTimelineOutputPayloadView
{
	const std::byte* data = nullptr;
	std::uint32_t size = 0;
	std::uint32_t alignment = 1;

	template <typename Value>
	const Value* As() const
	{
		return size == sizeof(Value) && alignment == alignof(Value) &&
			reinterpret_cast<std::uintptr_t>(data) % alignof(Value) == 0
			? reinterpret_cast<const Value*>(data) : nullptr;
	}
};

struct VansTimelineEvaluationOutput
{
	VansTimelineOutputTypeId typeId;
	std::uint32_t applierSlot = UINT32_MAX;
	VansTimelineOutputPayloadView payload;
	VansResolvedTimelineTarget target;
	VansTimelineWriterHandle writer;
	VansTimelineStableOrder order;
	VansTimelineBlendMode blendMode = VansTimelineBlendMode::Override;
	VansTimelineCompletionMode completion = VansTimelineCompletionMode::RestoreState;
	VansTimelineSessionHandle session;
	VansTimelineSessionHandle root;
	VansTimelineSessionKind sessionKind = VansTimelineSessionKind::External;
	VansTimelineEvaluationPhase phase = VansTimelineEvaluationPhase::PostScript;
	std::uint32_t trackIndex = UINT32_MAX;
	std::uint32_t sectionIndex = UINT32_MAX;
	VansTimelineId sourceTrackId;
	VansTimelineId sourceElementId;
	bool retainsPreAnimatedState = true;
};

class VansTimelineOutputArena
{
public:
	VansTimelineOutputArena();
	VansTimelineOutputArena(const VansTimelineOutputArena&) = delete;
	VansTimelineOutputArena& operator=(const VansTimelineOutputArena&) = delete;
	VansTimelineOutputArena(VansTimelineOutputArena&&) noexcept = default;
	VansTimelineOutputArena& operator=(VansTimelineOutputArena&&) noexcept = default;
	void Reset();

	template <typename Value>
	VansTimelineOutputPayloadView Write(const Value& value)
	{
		static_assert(std::is_trivially_copyable_v<Value>,
			"Timeline output payloads must be trivially copyable");
		std::byte* destination = Allocate(sizeof(Value), alignof(Value));
		std::memcpy(destination, &value, sizeof(Value));
		return { destination, static_cast<std::uint32_t>(sizeof(Value)),
			static_cast<std::uint32_t>(alignof(Value)) };
	}

	std::size_t HighWaterBytes() const { return m_HighWater; }
	std::size_t FrameBytes() const { return m_FrameBytes; }

private:
	struct Block
	{
		std::unique_ptr<std::byte[]> bytes;
		std::size_t capacity = 0;
		std::size_t used = 0;
	};
	std::byte* Allocate(std::size_t size, std::size_t alignment);
	std::vector<Block> m_Blocks;
	std::size_t m_CurrentBlock = 0;
	std::size_t m_FrameBytes = 0;
	std::size_t m_HighWater = 0;
};

struct VansTimelineTraversalSegment
{
	VansTimelineTick previousTick = 0;
	VansTimelineTick currentTick = 0;
	VansTimelineEvaluationReason reason = VansTimelineEvaluationReason::Playback;
	VansTimelineSeekPolicy seekPolicy = VansTimelineSeekPolicy::ContinuousOnly;
	int playbackDirection = 1;
	std::int32_t loopIteration = 0;
	std::uint64_t clockSerial = 0;
	bool discontinuity = false;
	// True only for the first segment of a newly-started traversal or a repeat-loop
	// wrap. Point/range edges at previousTick belong to that traversal exactly once.
	bool includesPreviousEndpoint = false;
};
using VansTimelineEvaluationSegment = VansTimelineTraversalSegment;

struct VansTimelineRangeCrossing
{
	VansTimelineId rangeId;
	VansTimelineRangeEdge edge = VansTimelineRangeEdge::Update;
	VansTimelineTick tick = 0;
};

// Built-in integrations consume this POD and read immutable compiled data from ApplyContext.
// Third-party extensions may emit their own registered POD payload instead.
struct VansTimelineSampleOutput
{
	VansTimelineTick timelineTick = 0;
	VansTimelineTick localTick = 0;
	double weight = 1.0;
	std::int32_t loopIteration = 0;
	std::int8_t direction = 1;
	bool active = false;
	bool entered = false;
	bool exited = false;
	bool rebuild = false;
};
}
