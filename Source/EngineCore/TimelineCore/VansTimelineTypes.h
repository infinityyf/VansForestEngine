#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Vans
{
using VansTimelineTick = std::int64_t;
using VansTimelineId = std::string;

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
enum class VansTimelineBindingKind { SceneEntity, SceneComponent, RuntimeObject, Asset, Spawnable, UIComponent, External };
enum class VansTimelineTrackType
{
	Transform,
	Property,
	Activation,
	Constraint,
	AnimationClip,
	AnimatorParameter,
	BoneOverride,
	Audio,
	Media,
	Particle,
	CameraCut,
	CameraProperty,
	CameraShake,
	FadePostProcess,
	Light,
	MaterialParameter,
	MaterialSwitch,
	UIState,
	EventSignal,
	SubTimeline,
	Spawnable,
	TimeScale,
	SceneState,
	Custom
};
enum class VansTimelineChannelType
{
	Bool,
	Int32,
	Int64,
	Float,
	Double,
	Enum,
	String,
	Vec2,
	Vec3,
	Vec4,
	Quaternion,
	ColorLinear,
	ColorSrgb,
	ObjectReference,
	EventPayload
};
enum class VansTimelineInterpolation { Constant, Linear, Auto, ClampedAuto, Cubic, Bezier, Slerp };
enum class VansTimelineTangentMode { Unified, Broken, Weighted };
enum class VansTimelineBlendMode { Override, Additive, Multiply, Relative };
enum class VansTimelineCompletionMode { ProjectDefault, RestoreState, KeepState };
enum class VansTimelineEvaluationMode { WithSubTimelines, Isolated };
enum class VansTimelineLoopMode { None, Loop, PingPong };
enum class VansTimelineExtrapolation { None, Hold, Linear, Loop, PingPong };
enum class VansTimelinePlayOn { Manual, Awake, Enable, Signal };
enum class VansTimelineUpdateMode { GameTime, UnscaledTime, Manual };
enum class VansTimelineBindingRootMode { OwnerRelative, World };
enum class VansTimelinePlayerState { Unloaded, Stopped, Playing, Paused, Completed, Error };
enum class VansTimelineEvaluationReason { Playback, Scrub, Jump, Step, LoopWrap, PreRoll, PostRoll, Restore };
enum class VansTimelineSeekPolicy { ContinuousOnly, SafeEdges, ExactTick, AllEdges };
enum class VansTimelineDiagnosticSeverity { Info, Warning, Error };
enum class VansTimelineCapability
{
	Runtime,
	Editor,
	GeometryCache,
	MorphTargets,
	ControlRig,
	GlobalTimeScale,
	AdditiveScene,
	PhysicsCommands,
	SpawnTemplate
};

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

struct VansTimelineEventPayload
{
	std::string payloadType;
	VansSerializedValue value = VansSerializedValue::Object({});
};

using VansTimelineKeyValue = std::variant<
	std::monostate,
	bool,
	std::int32_t,
	std::int64_t,
	float,
	double,
	std::string,
	VansTimelineVec2,
	VansTimelineVec3,
	VansTimelineVec4,
	VansTimelineQuaternion,
	VansTimelineColorLinear,
	VansTimelineColorSrgb,
	VansTimelineObjectReference,
	VansTimelineEventPayload>;

struct VansTimelineDiagnostic
{
	VansTimelineDiagnosticSeverity severity = VansTimelineDiagnosticSeverity::Info;
	std::string objectId;
	std::string propertyPath;
	std::string message;
};

using VansTimelineDiagnostics = std::vector<VansTimelineDiagnostic>;

struct VansTimelineBindingOverride
{
	VansTimelineId bindingId;
	std::string targetEntityGuid;
	std::string targetComponentGuid;
	std::uint16_t targetComponentTypeId = 0;
	bool useOwner = false;
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
	std::vector<VansTimelineBindingOverride> bindingOverrides;
	std::unordered_map<std::string, VansTimelineKeyValue> parameters;
};

class VansTimelineTime
{
public:
	static double TickToSeconds(VansTimelineTick tick, const VansTimelineTimebase& timebase);
	static VansTimelineTick SecondsToTick(
		double seconds,
		const VansTimelineTimebase& timebase,
		VansTimelineRoundingMode rounding = VansTimelineRoundingMode::Round);
	static VansTimelineTick FrameToTick(std::int64_t frame, const VansTimelineTimebase& timebase);
	static std::int64_t TickToFrame(
		VansTimelineTick tick,
		const VansTimelineTimebase& timebase,
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
	static VansTimelineSectionTimeMap Map(
		VansTimelineTick timelineTick,
		VansTimelineTick startTick,
		VansTimelineTick durationTicks,
		VansTimelineTick sourceInTick,
		VansTimelineTick sourceOutTick,
		double playRate,
		bool reverse,
		VansTimelineLoopMode loopMode,
		std::int32_t loopCount);
};

class VansTimelineEdgeCrossing
{
public:
	static bool Crossed(
		VansTimelineTick previousTick,
		VansTimelineTick currentTick,
		VansTimelineTick keyTick,
		bool exactSeek = false);
};
}
