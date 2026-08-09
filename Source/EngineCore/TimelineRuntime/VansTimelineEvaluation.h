#pragma once

#include "../SceneRuntime/VansRuntimeHandle.h"
#include "../TimelineCore/VansTimelineCompiler.h"

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace Vans
{
struct VansResolvedTimelineTarget
{
	VansTimelineId bindingId;
	VansTimelineBindingKind kind = VansTimelineBindingKind::SceneEntity;
	VansEntityHandle entity;
	VansEntityHandle rootOwner;
	VansComponentHandle component;
	std::string assetGuid;
	std::string assetPath;
	bool required = true;
	bool valid = false;
};

struct VansTimelineTransformOutput
{
	VansTimelineVec3 position;
	VansTimelineQuaternion rotation;
	VansTimelineVec3 scale{ { 1.0, 1.0, 1.0 } };
	std::uint32_t channels = 0;
	std::string space;
};

struct VansTimelinePropertyOutput
{
	std::uint16_t componentTypeId = 0;
	std::string descriptorId;
	std::string propertyPath;
	VansTimelineChannelType valueType = VansTimelineChannelType::Float;
	VansTimelineKeyValue value;
};

struct VansTimelineActivationOutput
{
	std::string scope;
	bool active = true;
};

struct VansTimelineConstraintOutput
{
	VansTimelineConstraintTrackConfig config;
	VansResolvedTimelineTarget sourceTarget;
	VansResolvedTimelineTarget constrainedTarget;
	double weight = 1.0;
};

struct VansTimelineAnimationOutput
{
	std::string assetGuid;
	std::string assetPath;
	std::string slot;
	std::string layer;
	VansTimelineTick localTick = 0;
	double localSeconds = 0.0;
	double weight = 1.0;
	double blendInSeconds = 0.0;
	double blendOutSeconds = 0.0;
	bool additive = false;
	std::string avatarMaskGuid;
	std::string avatarMaskPath;
	std::string syncGroup;
	bool markerSync = false;
	bool active = false;
	bool entered = false;
	bool exited = false;
	std::string rootMotionPolicy;
};

struct VansTimelineAnimatorParameterOutput
{
	std::string parameterName;
	std::string parameterType;
	VansTimelineKeyValue value;
	bool trigger = false;
	std::string missingParameterPolicy = "Error";
};

struct VansTimelineBoneOverrideOutput
{
	VansTimelineBoneOverrideTrackConfig config;
	VansTimelineTransformOutput transform;
	VansResolvedTimelineTarget ikTarget;
	VansResolvedTimelineTarget poleTarget;
};

struct VansTimelineAudioOutput
{
	std::string assetGuid;
	std::string assetPath;
	VansTimelineAudioTrackConfig config;
	double localSeconds = 0.0;
	double envelopeWeight = 1.0;
	bool loop = false;
	bool active = false;
	bool entered = false;
	bool exited = false;
};

struct VansTimelineMediaOutput
{
	std::string assetGuid;
	std::string assetPath;
	VansTimelineMediaTrackConfig config;
	double localSeconds = 0.0;
	double playbackRate = 1.0;
	bool reverse = false;
	bool active = false;
	bool entered = false;
	bool exited = false;
};

struct VansTimelineParticleOutput
{
	VansTimelineParticleTrackConfig config;
	double localSeconds = 0.0;
	double prewarmSeconds = 0.0;
	bool active = false;
	bool entered = false;
	bool exited = false;
};

struct VansTimelineCameraCutOutput
{
	VansTimelineId cameraBindingId;
	VansTimelineId targetCameraBindingId;
	VansResolvedTimelineTarget cameraTarget;
	VansResolvedTimelineTarget targetCameraTarget;
	VansTimelineCameraCutTrackConfig config;
	bool active = false;
	double blendAlpha = 1.0;
};

struct VansTimelineCameraPropertyOutput
{
	std::string property;
	VansTimelineKeyValue value;
};

struct VansTimelineCameraShakeOutput
{
	VansTimelineCameraShakeTrackConfig config;
	VansTimelineVec3 positionOffset;
	VansTimelineVec3 rotationOffset;
	double weight = 1.0;
	bool active = false;
};

struct VansTimelineFadePostProcessOutput
{
	VansTimelineFadePostProcessTrackConfig config;
	double value = 0.0;
};

struct VansTimelineLightOutput
{
	std::string property;
	VansTimelineKeyValue value;
};

struct VansTimelineMaterialParameterOutput
{
	std::string materialSlotId;
	std::string parameterName;
	VansTimelineKeyValue value;
	std::string instancePolicy;
};

struct VansTimelineMaterialSwitchOutput
{
	std::string materialSlotId;
	std::string materialGuid;
	std::string materialPath;
};

struct VansTimelineUIOutput
{
	VansTimelineUIStateTrackConfig config;
	VansTimelineKeyValue value;
	bool entered = false;
	bool exited = false;
};

struct VansTimelineEventOutput
{
	VansTimelineEventTrackConfig config;
	VansTimelineKeyValue payload;
	VansTimelineTick tick = 0;
	std::int32_t loopIteration = 0;
};

struct VansTimelineSubTimelineOutput
{
	std::string assetGuid;
	std::string assetPath;
	VansTimelineSubTimelineTrackConfig config;
	VansTimelineTick localTick = 0;
	bool active = false;
	bool entered = false;
	bool exited = false;
};

struct VansTimelineTimeScaleOutput
{
	double scale = 1.0;
	VansTimelineTimeScaleTrackConfig config;
};

using VansTimelineOutputValue = std::variant<
	VansTimelineTransformOutput,
	VansTimelinePropertyOutput,
	VansTimelineActivationOutput,
	VansTimelineConstraintOutput,
	VansTimelineAnimationOutput,
	VansTimelineAnimatorParameterOutput,
	VansTimelineBoneOverrideOutput,
	VansTimelineAudioOutput,
	VansTimelineMediaOutput,
	VansTimelineParticleOutput,
	VansTimelineCameraCutOutput,
	VansTimelineCameraPropertyOutput,
	VansTimelineCameraShakeOutput,
	VansTimelineFadePostProcessOutput,
	VansTimelineLightOutput,
	VansTimelineMaterialParameterOutput,
	VansTimelineMaterialSwitchOutput,
	VansTimelineUIOutput,
	VansTimelineEventOutput,
	VansTimelineSubTimelineOutput,
	VansTimelineTimeScaleOutput>;

struct VansTimelineEvaluationOutput
{
	VansResolvedTimelineTarget target;
	VansTimelineTrackType trackType = VansTimelineTrackType::Transform;
	VansTimelineOutputValue value = VansTimelineTransformOutput{};
	VansTimelineBlendMode blendMode = VansTimelineBlendMode::Override;
	std::int32_t hierarchicalBias = 0;
	std::int32_t priority = 0;
	std::int32_t trackOrder = 0;
	std::string sourceTrackId;
	std::string sourceSectionId;
	std::string propertyKey;
	std::string rootWriterId;
	std::string writerId;
	VansTimelineCompletionMode completionMode = VansTimelineCompletionMode::RestoreState;
	bool retainsPreAnimatedState = true;
};

struct VansTimelineEvaluationSegment
{
	VansTimelineTick previousTick = 0;
	VansTimelineTick currentTick = 0;
	VansTimelineEvaluationReason reason = VansTimelineEvaluationReason::Playback;
	VansTimelineSeekPolicy seekPolicy = VansTimelineSeekPolicy::ContinuousOnly;
	int playbackDirection = 1;
	std::int32_t loopIteration = 0;
};
}
