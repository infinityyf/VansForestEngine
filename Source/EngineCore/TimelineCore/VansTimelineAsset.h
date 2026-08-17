#pragma once

#include "VansTimelineTypes.h"

#include <optional>
#include <string>
#include <vector>

namespace Vans
{
struct VansTimelineBinding
{
	VansTimelineId id;
	VansTimelineBindingId stableId;
	std::string displayName;
	VansTimelineBindingKind kind = VansTimelineBindingKind::SceneEntity;
	std::string targetGuid;
	std::string componentGuid;
	std::uint16_t componentTypeId = 0;
	std::string assetGuid;
	std::string assetPath;
	std::string scenePathHint;
	bool required = true;
};

struct VansTimelineParameterDescriptor
{
	VansTimelineParameterId id;
	std::string name;
	VansTimelineValueType type = VansTimelineValueType::Null;
	VansTimelineValue defaultValue;
	bool readOnly = false;
};

struct VansTimelineCondition
{
	VansTimelineParameterId parameterId;
	VansTimelineValue expectedValue;
	bool negate = false;
};

struct VansTimelineTrackTypeRef
{
	VansTimelineTrackTypeId typeId;
	std::string stableName;
	static VansTimelineTrackTypeRef FromName(std::string name)
	{
		VansTimelineTrackTypeRef result;
		result.typeId = VansMakeStableId<VansTimelineTrackTypeTag>(name);
		result.stableName = std::move(name);
		return result;
	}
};

struct VansTimelineTrackDisplay
{
	std::array<float, 4> color{ 0.24f, 0.55f, 0.92f, 1.0f };
	std::string rowHeight = "Normal";
	std::string note;
};

struct VansTimelineBlendCurve
{
	std::string shape = "Linear";
	double exponent = 1.0;
};

struct VansTimelineKey
{
	VansTimelineId id;
	VansTimelineTick tick = 0;
	VansTimelineValue value;
	VansTimelineInterpolation interpolation = VansTimelineInterpolation::Auto;
	VansTimelineTangentMode tangentMode = VansTimelineTangentMode::Unified;
	double arriveTangent = 0.0;
	double leaveTangent = 0.0;
	double arriveWeight = 0.0;
	double leaveWeight = 0.0;
};

struct VansTimelineChannel
{
	VansTimelineId id;
	std::string name;
	VansTimelineValueType type = VansTimelineValueType::Float;
	VansTimelineExtrapolation preExtrapolation = VansTimelineExtrapolation::None;
	VansTimelineExtrapolation postExtrapolation = VansTimelineExtrapolation::None;
	std::vector<VansTimelineKey> keys;
};

struct VansTimelineRange
{
	VansTimelineId id;
	VansTimelineTick startTick = 0;
	VansTimelineTick endTick = 1;
	VansSerializedValue extensionData = VansSerializedValue::Object({});
};

struct VansTimelineSection
{
	VansTimelineId id;
	std::string name;
	VansTimelineTick startTick = 0;
	VansTimelineTick durationTicks = 1;
	VansTimelineTick sourceInTick = 0;
	VansTimelineTick sourceOutTick = -1;
	double playRate = 1.0;
	bool reverse = false;
	VansTimelineLoopMode loopMode = VansTimelineLoopMode::None;
	std::int32_t loopCount = 1;
	VansTimelineTick preRollTicks = 0;
	VansTimelineTick postRollTicks = 0;
	VansTimelineTick easeInTicks = 0;
	VansTimelineTick easeOutTicks = 0;
	VansTimelineBlendCurve blendIn;
	VansTimelineBlendCurve blendOut;
	VansTimelineExtrapolation preExtrapolation = VansTimelineExtrapolation::None;
	VansTimelineExtrapolation postExtrapolation = VansTimelineExtrapolation::None;
	VansTimelineCompletionMode completionMode = VansTimelineCompletionMode::ProjectDefault;
	bool active = true;
	bool locked = false;
	std::string assetGuid;
	std::string assetPath;
	std::optional<VansSerializedValue> extensionData;
	std::vector<VansTimelineChannel> channels;
	std::vector<VansTimelineRange> ranges;
};

struct VansTimelineTrack
{
	VansTimelineId id;
	VansTimelineTrackTypeRef type = VansTimelineTrackTypeRef::FromName(std::string(TimelineNames::Transform));
	std::string name;
	VansTimelineId bindingId;
	VansTimelineId groupId;
	bool enabled = true;
	bool locked = false;
	std::int32_t order = 0;
	std::int32_t priority = 0;
	VansTimelineBlendMode blendMode = VansTimelineBlendMode::Override;
	VansTimelineCompletionMode completionMode = VansTimelineCompletionMode::ProjectDefault;
	VansTimelineCondition condition;
	VansSerializedValue extensionData = VansSerializedValue::Object({});
	VansTimelineTrackDisplay display;
	std::vector<VansTimelineSection> sections;
};

struct VansTimelineGroup
{
	VansTimelineId id;
	VansTimelineId parentId;
	std::string name;
	std::array<float, 4> color{ 0.31f, 0.34f, 0.4f, 1.0f };
	std::int32_t order = 0;
	bool locked = false;
};

struct VansTimelineMarker
{
	VansTimelineId id;
	VansTimelineTick tick = 0;
	std::string label;
	std::array<float, 4> color{ 0.96f, 0.72f, 0.24f, 1.0f };
	std::string category;
	bool determinismFence = false;
	bool runtimeObservable = false;
	bool editorSafe = false;
	std::string firePolicy = "EveryCrossing";
	VansTimelinePayloadTypeId payloadType;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

struct VansTimelineMetadata
{
	std::string displayName;
	std::string description;
	std::vector<std::string> tags;
};

struct VansTimelineAsset
{
	std::string assetKind = "Timeline";
	VansTimelineTimebase timebase;
	VansTimelineTick durationTicks = 60000;
	VansTimelineTickRange playbackRange{ 0, 60000 };
	VansTimelineTickRange workRange{ 0, 60000 };
	VansTimelineCompletionMode defaultCompletionMode = VansTimelineCompletionMode::RestoreState;
	VansTimelineEvaluationMode defaultEvaluationMode = VansTimelineEvaluationMode::WithSubTimelines;
	std::vector<VansTimelineParameterDescriptor> parameters;
	std::vector<VansTimelineBinding> bindings;
	std::vector<VansTimelineGroup> groups;
	std::vector<VansTimelineTrack> tracks;
	std::vector<VansTimelineMarker> markers;
	VansTimelineMetadata metadata;
};
}
