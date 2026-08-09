#pragma once

#include "VansTimelineTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace Vans
{
struct VansTimelineBinding
{
	VansTimelineId id;
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

struct VansTimelineCondition
{
	std::string parameter;
	VansTimelineKeyValue expectedValue;
	bool negate = false;
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

struct VansTimelineTransformTrackConfig
{
	std::string space = "Local";
	std::uint32_t channels = 0x3FFu;
	std::string rotationMode = "QuaternionSlerp";
	std::string trajectoryDisplay = "Selected";
	std::string physicsPolicy = "RejectDynamicBody";
	bool writeScale = true;
};

struct VansTimelinePropertyTrackConfig
{
	std::uint16_t componentTypeId = 0;
	std::string componentGuid;
	std::string descriptorId;
	std::string propertyPath;
	VansTimelineChannelType valueType = VansTimelineChannelType::Float;
	std::string unit;
	double minimum = 0.0;
	double maximum = 1.0;
	double step = 0.01;
	std::string colorSpace = "Linear";
};

struct VansTimelineActivationTrackConfig
{
	std::string scope = "EntityActive";
	bool stateWhenInside = true;
	std::string stateBefore = "Restore";
	std::string stateAfter = "Restore";
	bool useCommandBuffer = true;
};

struct VansTimelineConstraintTrackConfig
{
	std::string constraintType = "Parent";
	VansTimelineId sourceBindingId;
	VansTimelineId targetBindingId;
	bool maintainOffset = true;
	VansTimelineVec3 offsetPosition;
	VansTimelineQuaternion offsetRotation;
	VansTimelineVec3 offsetScale{ { 1.0, 1.0, 1.0 } };
	std::uint32_t axisMask = 0x7u;
	std::string upAxis = "Y";
	std::string aimAxis = "Z";
	double weight = 1.0;
};

struct VansTimelineAnimationTrackConfig
{
	std::string slot;
	std::string layer;
	double weight = 1.0;
	bool additive = false;
	std::string avatarMaskGuid;
	std::string avatarMaskPath;
	std::string rootMotionPolicy = "Ignore";
	std::string syncGroup;
	bool markerSync = false;
};

struct VansTimelineAnimatorParameterTrackConfig
{
	std::string parameterName;
	std::string parameterType = "Float";
	std::string firePolicy = "Forward";
	std::string seekPolicy = "Never";
	std::string missingParameterPolicy = "Error";
};

struct VansTimelineBoneOverrideTrackConfig
{
	std::string bone;
	std::string boneId;
	double weight = 1.0;
	bool additive = false;
	std::string space = "Local";
	VansTimelineId ikTargetBindingId;
	VansTimelineId poleBindingId;
	double positionWeight = 1.0;
	double rotationWeight = 1.0;
	bool clearOnExit = true;
};

struct VansTimelineAudioTrackConfig
{
	bool useBoundSource = false;
	double volume = 1.0;
	double pitch = 1.0;
	double stereoPan = 0.0;
	double spatialBlend = 0.0;
	double fadeInSeconds = 0.0;
	double fadeOutSeconds = 0.0;
	std::string bus = "Master";
	double reverbSend = 0.0;
	double referenceDistance = 1.0;
	double maxDistance = 100.0;
	double rolloff = 1.0;
	std::string seekPolicy = "Exact";
	std::string onSectionEnd = "Stop";
};

struct VansTimelineMediaTrackConfig
{
	std::string syncMode = "TimelineClock";
	bool frameHold = true;
	std::string colorSpace = "Srgb";
	bool outputAudio = false;
	std::string targetKind = "VideoComponent";
	std::string materialSlot;
	std::string uiElement;
	std::string onSectionEnd = "Stop";
};

struct VansTimelineParticleTrackConfig
{
	std::string action = "Play";
	VansTimelineTick prewarmTicks = 0;
	double simulationRate = 1.0;
	std::uint32_t randomSeed = 0;
	bool resetOnEnter = true;
	bool clearOnExit = true;
	bool loop = false;
	std::string seekPolicy = "DeterministicResimulate";
};

struct VansTimelineCameraCutTrackConfig
{
	VansTimelineId cameraBindingId;
	VansTimelineId targetCameraBindingId;
	std::string cutMode = "Cut";
	VansTimelineTick blendDurationTicks = 0;
	VansTimelineBlendCurve blendCurve;
	std::int32_t priority = 0;
	std::string aspectPolicy = "Preserve";
	std::string viewport = "Main";
	std::string shotName;
	std::array<float, 4> shotColor{ 0.22f, 0.62f, 0.86f, 1.0f };
	std::string thumbnailCacheKey;
};

struct VansTimelineCameraPropertyTrackConfig
{
	bool fieldOfView = true;
	bool nearClip = false;
	bool farClip = false;
	bool transform = false;
};

struct VansTimelineCameraShakeTrackConfig
{
	bool position = true;
	bool rotation = true;
	std::string space = "CameraLocal";
	double amplitudeScale = 1.0;
	std::int32_t priority = 0;
};

struct VansTimelineFadePostProcessTrackConfig
{
	std::string mode = "Fade";
	VansTimelineColorLinear color;
	bool gameViewOnly = true;
	std::string profileGuid;
	std::string profilePath;
	double blendWeight = 1.0;
	std::int32_t priority = 0;
};

struct VansTimelineLightTrackConfig
{
	bool color = true;
	bool temperature = false;
	bool intensity = true;
	bool range = false;
	bool cone = false;
	bool rectSize = false;
	bool shadow = false;
};

struct VansTimelineMaterialParameterTrackConfig
{
	std::string materialSlotId;
	std::string parameterName;
	VansTimelineChannelType parameterType = VansTimelineChannelType::Float;
	std::string instancePolicy = "PerEntityRuntimeInstance";
};

struct VansTimelineMaterialSwitchTrackConfig
{
	std::string materialSlotId;
};

struct VansTimelineUIStateTrackConfig
{
	std::string screen;
	std::string targetKind = "Screen";
	std::string element;
	std::string descriptorId;
	std::uint32_t setterId = 0;
	std::string action;
};

struct VansTimelineEventTrackConfig
{
	std::string signalId;
	std::string displayName;
	std::string payloadType;
	std::string eventLane = "MainThread";
	std::string firePolicy = "Forward";
	std::string seekPolicy = "Never";
	std::string loopPolicy = "EveryLoop";
	bool editorSafe = false;
	bool oncePerPlayback = false;
};

struct VansTimelineSubTimelineTrackConfig
{
	std::vector<VansTimelineBindingOverride> bindingRemap;
	std::unordered_map<std::string, VansTimelineKeyValue> parameterOverrides;
	std::int32_t hierarchicalBias = 0;
	bool useGlobalTimeDisplay = false;
	bool originTransformOverride = false;
	VansTimelineVec3 originPosition;
	VansTimelineQuaternion originRotation;
	VansTimelineVec3 originScale{ { 1.0, 1.0, 1.0 } };
	std::string timeWarpChannel;
};

struct VansTimelineSpawnableTrackConfig
{
	std::string spawnTemplateGuid;
	std::string spawnTemplatePath;
	VansTimelineId parentBindingId;
	std::string spawnPolicy = "OnEnter";
	std::string destroyPolicy = "OnExit";
	VansTimelineTick prewarmTicks = 0;
	std::string exportBindingId;
};

struct VansTimelineTimeScaleTrackConfig
{
	std::string scope = "LocalTimeWarp";
	bool affectAudio = false;
	bool affectParticles = true;
	double minimum = 0.0;
	double maximum = 8.0;
	bool pauseAtZero = true;
};

struct VansTimelineSceneStateTrackConfig
{
	std::string sceneGuid;
	std::string scenePath;
	std::string action = "Load";
	bool preload = false;
	std::string asyncPolicy = "NonBlocking";
	std::int32_t activationPriority = 0;
};

struct VansTimelineCustomTrackConfig
{
	std::string customTypeId;
	VansSerializedValue payload = VansSerializedValue::Object({});
};

using VansTimelineTrackConfig = std::variant<
	std::monostate,
	VansTimelineTransformTrackConfig,
	VansTimelinePropertyTrackConfig,
	VansTimelineActivationTrackConfig,
	VansTimelineConstraintTrackConfig,
	VansTimelineAnimationTrackConfig,
	VansTimelineAnimatorParameterTrackConfig,
	VansTimelineBoneOverrideTrackConfig,
	VansTimelineAudioTrackConfig,
	VansTimelineMediaTrackConfig,
	VansTimelineParticleTrackConfig,
	VansTimelineCameraCutTrackConfig,
	VansTimelineCameraPropertyTrackConfig,
	VansTimelineCameraShakeTrackConfig,
	VansTimelineFadePostProcessTrackConfig,
	VansTimelineLightTrackConfig,
	VansTimelineMaterialParameterTrackConfig,
	VansTimelineMaterialSwitchTrackConfig,
	VansTimelineUIStateTrackConfig,
	VansTimelineEventTrackConfig,
	VansTimelineSubTimelineTrackConfig,
	VansTimelineSpawnableTrackConfig,
	VansTimelineTimeScaleTrackConfig,
	VansTimelineSceneStateTrackConfig,
	VansTimelineCustomTrackConfig>;

struct VansTimelineKey
{
	VansTimelineId id;
	VansTimelineTick tick = 0;
	VansTimelineKeyValue value;
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
	VansTimelineChannelType type = VansTimelineChannelType::Float;
	VansTimelineExtrapolation preExtrapolation = VansTimelineExtrapolation::None;
	VansTimelineExtrapolation postExtrapolation = VansTimelineExtrapolation::None;
	std::vector<VansTimelineKey> keys;
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
	VansTimelineTrackConfig config;
	std::vector<VansTimelineChannel> channels;
};

struct VansTimelineTrack
{
	VansTimelineId id;
	VansTimelineTrackType type = VansTimelineTrackType::Transform;
	std::string name;
	VansTimelineId bindingId;
	VansTimelineId groupId;
	bool enabled = true;
	bool runtimeMuted = false;
	bool locked = false;
	std::int32_t order = 0;
	std::int32_t priority = 0;
	VansTimelineBlendMode blendMode = VansTimelineBlendMode::Override;
	VansTimelineCompletionMode completionMode = VansTimelineCompletionMode::ProjectDefault;
	VansTimelineCondition condition;
	VansTimelineTrackConfig config;
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
	std::vector<VansTimelineBinding> bindings;
	std::vector<VansTimelineGroup> groups;
	std::vector<VansTimelineTrack> tracks;
	std::vector<VansTimelineMarker> markers;
	VansTimelineMetadata metadata;
};
}
