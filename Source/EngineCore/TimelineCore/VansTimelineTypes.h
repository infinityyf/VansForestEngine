#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../RuntimeCore/VansStableIdentity.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Vans
{
using VansTimelineTick = std::int64_t;
using VansTimelineId = std::string;

struct VansTimelineTrackTypeTag;
struct VansTimelineOutputTypeTag;
struct VansTimelineParameterTag;
struct VansTimelineBindingTag;
struct VansTimelineClockTag;
struct VansTimelinePayloadTypeTag;
struct VansTimelineFieldTag;

using VansTimelineTrackTypeId = VansStableId<VansTimelineTrackTypeTag>;
using VansTimelineOutputTypeId = VansStableId<VansTimelineOutputTypeTag>;
using VansTimelineParameterId = VansStableId<VansTimelineParameterTag>;
using VansTimelineBindingId = VansStableId<VansTimelineBindingTag>;
using VansTimelineClockTypeId = VansStableId<VansTimelineClockTag>;
using VansTimelinePayloadTypeId = VansStableId<VansTimelinePayloadTypeTag>;
using VansTimelineFieldId = VansStableId<VansTimelineFieldTag>;

using VansTimelineSessionHandle = VansGenerationHandle;
using VansTimelineWriterHandle = VansGenerationHandle;
using VansTimelineRestoreHandle = VansGenerationHandle;
using VansTimelineClockHandle = VansGenerationHandle;

namespace TimelineNames
{
inline constexpr std::string_view Transform = "Timeline.Transform";
inline constexpr std::string_view Property = "Timeline.Property";
inline constexpr std::string_view Activation = "Timeline.Activation";
inline constexpr std::string_view Constraint = "Timeline.Constraint";
inline constexpr std::string_view AnimationClip = "Timeline.AnimationClip";
inline constexpr std::string_view AnimatorParameter = "Timeline.AnimatorParameter";
inline constexpr std::string_view BoneOverride = "Timeline.BoneOverride";
inline constexpr std::string_view Audio = "Timeline.Audio";
inline constexpr std::string_view Media = "Timeline.Media";
inline constexpr std::string_view Particle = "Timeline.Particle";
inline constexpr std::string_view CameraCut = "Timeline.CameraCut";
inline constexpr std::string_view CameraProperty = "Timeline.CameraProperty";
inline constexpr std::string_view CameraShake = "Timeline.CameraShake";
inline constexpr std::string_view FadePostProcess = "Timeline.FadePostProcess";
inline constexpr std::string_view Light = "Timeline.Light";
inline constexpr std::string_view MaterialParameter = "Timeline.MaterialParameter";
inline constexpr std::string_view MaterialSwitch = "Timeline.MaterialSwitch";
inline constexpr std::string_view UIState = "Timeline.UIState";
inline constexpr std::string_view EventSignal = "Timeline.EventSignal";
inline constexpr std::string_view SubTimeline = "Timeline.SubTimeline";
inline constexpr std::string_view TimeScale = "Timeline.TimeScale";
}

struct VansTimelineTimebase
{
	std::int64_t ticksPerSecond = 60000;
	std::int32_t displayRateNumerator = 60;
	std::int32_t displayRateDenominator = 1;
};

struct VansTimelineTickRange
{
	VansTimelineTick startTick = 0;
	VansTimelineTick endTick = 0;
};

enum class VansTimelineRoundingMode { Round, Floor, Ceil };
enum class VansTimelineBindingKind { SceneEntity, SceneComponent, RuntimeObject, Asset, UIComponent, External };
enum class VansTimelineValueType
{
	Null, Bool, Int32, Int64, Float, Double, Enum, String, Vec2, Vec3, Vec4,
	Quaternion, ColorLinear, ColorSrgb, ObjectReference, Struct
};
using VansTimelineChannelType = VansTimelineValueType;
enum class VansTimelineInterpolation { Constant, Linear, Auto, ClampedAuto, Cubic, Bezier, Slerp };
enum class VansTimelineTangentMode { Unified, Broken, Weighted };
enum class VansTimelineBlendMode { Override, Additive, Multiply, Relative };
enum class VansTimelineCompletionMode { ProjectDefault, RestoreState, KeepState };
enum class VansTimelineEvaluationMode { WithSubTimelines, Isolated };
enum class VansTimelineLoopMode { None, Loop, PingPong };
enum class VansTimelineExtrapolation { None, Hold, Linear, Loop, PingPong };
enum class VansTimelinePlayOn { Manual, Awake, Enable, Signal };
enum class VansTimelineUpdateMode { GameTime, UnscaledTime, Manual, FixedTick, External };
enum class VansTimelineBindingRootMode { OwnerRelative, World };
enum class VansTimelinePlayerState { Unloaded, Stopped, Playing, Paused, Completed, Error };
enum class VansTimelineEvaluationReason { Playback, Scrub, Jump, Step, LoopWrap, PreRoll, PostRoll, Restore, ClockCorrection };
enum class VansTimelineSeekPolicy { ContinuousOnly, SafeEdges, ExactTick, AllEdges, RebuildActive, NoEdges };
enum class VansTimelineDiagnosticSeverity { Info, Warning, Error };
enum class VansTimelineEvaluationPhase : std::uint8_t { PostScript = 1, Camera = 2 };
enum class VansTimelineSessionKind : std::uint8_t
{
	Component, Preview, External, SubTimeline, Replay, Action
};
enum class VansTimelineRangeEdge : std::uint8_t { Enter, Update, Exit };
enum class VansTimelineDispatchTiming : std::uint8_t { SameFrame, NextFrame };
enum class VansTimelineEndReason : std::uint8_t { Completed, Stopped, Failed, Released, WorldShutdown, Reloaded };

enum class VansTimelineTrackFlags : std::uint32_t
{
	None = 0,
	Continuous = 1u << 0,
	PointEdge = 1u << 1,
	RangeEdge = 1u << 2,
	SupportsSections = 1u << 3,
	SupportsChannels = 1u << 4,
	SupportsReverse = 1u << 5,
	SeekRebuildable = 1u << 6,
	Reversible = 1u << 7,
	Destructive = 1u << 8,
	PreviewSafeByDefault = 1u << 9,
	RequiresBinding = 1u << 10,
	ThreadSafeEvaluate = 1u << 11,
	Deterministic = 1u << 12,
	EditorOnly = 1u << 13
};

constexpr VansTimelineTrackFlags operator|(VansTimelineTrackFlags left, VansTimelineTrackFlags right)
{
	return static_cast<VansTimelineTrackFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
constexpr VansTimelineTrackFlags operator&(VansTimelineTrackFlags left, VansTimelineTrackFlags right)
{
	return static_cast<VansTimelineTrackFlags>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
constexpr bool VansHasTimelineFlag(VansTimelineTrackFlags value, VansTimelineTrackFlags flag)
{
	return (value & flag) != VansTimelineTrackFlags::None;
}

struct VansTimelineVec2 { std::array<double, 2> value{}; };
struct VansTimelineVec3 { std::array<double, 3> value{}; };
struct VansTimelineVec4 { std::array<double, 4> value{}; };
struct VansTimelineQuaternion { std::array<double, 4> value{ 0.0, 0.0, 0.0, 1.0 }; };
struct VansTimelineColorLinear { std::array<double, 4> value{ 0.0, 0.0, 0.0, 1.0 }; };
struct VansTimelineColorSrgb { std::array<double, 4> value{ 0.0, 0.0, 0.0, 1.0 }; };

struct VansTimelineObjectReference
{
	std::string guid;
	std::string path;
	std::string objectKind;
};

struct VansTimelineStructValue
{
	VansTimelinePayloadTypeId type;
	VansSerializedValue value = VansSerializedValue::Object({});
};

using VansTimelineValue = std::variant<
	std::monostate, bool, std::int32_t, std::int64_t, float, double, std::string,
	VansTimelineVec2, VansTimelineVec3, VansTimelineVec4, VansTimelineQuaternion,
	VansTimelineColorLinear, VansTimelineColorSrgb, VansTimelineObjectReference, VansTimelineStructValue>;
using VansTimelineKeyValue = VansTimelineValue;

VansTimelineValueType VansTimelineTypeOf(const VansTimelineValue& value);
bool VansTimelineValuesEqual(const VansTimelineValue& left, const VansTimelineValue& right);

struct VansTimelineDiagnostic
{
	VansTimelineDiagnosticSeverity severity = VansTimelineDiagnosticSeverity::Info;
	std::string code;
	std::string assetId;
	std::string objectId;
	std::string propertyPath;
	std::string message;
	VansTimelineSessionHandle session;
	VansTimelineTick tick = 0;
};
using VansTimelineDiagnostics = std::vector<VansTimelineDiagnostic>;

struct VansTimelineBindingOverride
{
	VansTimelineBindingId bindingId;
	std::string targetEntityGuid;
	std::string targetComponentGuid;
	std::uint16_t targetComponentTypeId = 0;
	bool useOwner = false;
};

struct VansTimelineParameterOverride
{
	VansTimelineParameterId parameterId;
	VansTimelineValue value;
};

struct VansTimelineRuntimeBinding
{
	VansTimelineBindingId bindingId;
	VansStableId<struct VansRuntimeObjectTypeTag> objectType;
	VansGenerationHandle object;
	std::uint64_t changeSerial = 0;
};

struct VansTimelineInstanceConfig
{
	VansTimelinePlayOn playOn = VansTimelinePlayOn::Manual;
	VansTimelineUpdateMode updateMode = VansTimelineUpdateMode::GameTime;
	VansTimelineBindingRootMode bindingRootMode = VansTimelineBindingRootMode::OwnerRelative;
	VansTimelineLoopMode loopMode = VansTimelineLoopMode::None;
	std::int32_t loopCount = 1;
	double playbackSpeed = 1.0;
	bool restoreStateOnStop = true;
	std::string clockType = "Timeline.Clock.GameTime";
	std::vector<VansTimelineBindingOverride> bindingOverrides;
	std::vector<VansTimelineParameterOverride> parameterOverrides;
};

class VansTimelineTime
{
public:
	static double TickToSeconds(VansTimelineTick tick, const VansTimelineTimebase& timebase);
	static VansTimelineTick SecondsToTick(double seconds, const VansTimelineTimebase& timebase,
		VansTimelineRoundingMode rounding = VansTimelineRoundingMode::Round);
	static VansTimelineTick FrameToTick(std::int64_t frame, const VansTimelineTimebase& timebase);
	static std::int64_t TickToFrame(VansTimelineTick tick, const VansTimelineTimebase& timebase,
		VansTimelineRoundingMode rounding = VansTimelineRoundingMode::Round);
	static std::string FormatTimecode(VansTimelineTick tick, const VansTimelineTimebase& timebase, bool dropFrame);
};

struct VansTimelineSectionTimeMap
{
	bool active = false;
	VansTimelineTick localTick = 0;
	std::int32_t loopIndex = 0;
	bool reversed = false;
};

class VansTimelineSectionTimeMapper
{
public:
	static VansTimelineSectionTimeMap Map(VansTimelineTick timelineTick, VansTimelineTick startTick,
		VansTimelineTick durationTicks, VansTimelineTick sourceInTick, VansTimelineTick sourceOutTick,
		double playRate, bool reverse, VansTimelineLoopMode loopMode, std::int32_t loopCount);
};

class VansTimelineEdgeCrossing
{
public:
	static bool Crossed(VansTimelineTick previousTick, VansTimelineTick currentTick,
		VansTimelineTick keyTick, bool exactSeek = false);
};
}
