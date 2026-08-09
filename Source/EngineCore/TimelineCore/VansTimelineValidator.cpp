#include "VansTimelineValidator.h"

#include "VansTimelineSerialization.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace Vans
{
namespace
{
void AddDiagnostic(
	VansTimelineDiagnostics& diagnostics,
	VansTimelineDiagnosticSeverity severity,
	const std::string& objectId,
	const std::string& propertyPath,
	std::string message)
{
	diagnostics.push_back({ severity, objectId, propertyPath, std::move(message) });
}

void Error(
	VansTimelineDiagnostics& diagnostics,
	const std::string& objectId,
	const std::string& propertyPath,
	std::string message)
{
	AddDiagnostic(diagnostics, VansTimelineDiagnosticSeverity::Error, objectId, propertyPath, std::move(message));
}

void Warning(
	VansTimelineDiagnostics& diagnostics,
	const std::string& objectId,
	const std::string& propertyPath,
	std::string message)
{
	AddDiagnostic(diagnostics, VansTimelineDiagnosticSeverity::Warning, objectId, propertyPath, std::move(message));
}

bool RegisterStableId(
	const VansTimelineId& id,
	const char* kind,
	std::unordered_map<VansTimelineId, std::string>& ids,
	VansTimelineDiagnostics& diagnostics)
{
	if (id.empty())
	{
		Error(diagnostics, {}, "id", std::string(kind) + " requires a stable non-empty id");
		return false;
	}
	const auto [existing, inserted] = ids.emplace(id, kind);
	if (!inserted)
	{
		Error(diagnostics, id, "id", std::string(kind) + " id collides with " + existing->second);
		return false;
	}
	return true;
}

bool HasAssetReference(const std::string& guid, const std::string& path)
{
	return !guid.empty() || !path.empty();
}

bool IsOneOf(const std::string& value, std::initializer_list<const char*> allowed)
{
	return std::any_of(allowed.begin(), allowed.end(), [&](const char* item) { return value == item; });
}

std::string LowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
	{
		return static_cast<char>(std::tolower(character));
	});
	return value;
}

bool KeyValueMatches(VansTimelineChannelType type, const VansTimelineKeyValue& value)
{
	switch (type)
	{
	case VansTimelineChannelType::Bool: return std::holds_alternative<bool>(value);
	case VansTimelineChannelType::Int32: return std::holds_alternative<std::int32_t>(value);
	case VansTimelineChannelType::Int64: return std::holds_alternative<std::int64_t>(value);
	case VansTimelineChannelType::Float: return std::holds_alternative<float>(value) || std::holds_alternative<double>(value);
	case VansTimelineChannelType::Double: return std::holds_alternative<double>(value) || std::holds_alternative<float>(value);
	case VansTimelineChannelType::Enum:
	case VansTimelineChannelType::String: return std::holds_alternative<std::string>(value);
	case VansTimelineChannelType::Vec2: return std::holds_alternative<VansTimelineVec2>(value);
	case VansTimelineChannelType::Vec3: return std::holds_alternative<VansTimelineVec3>(value);
	case VansTimelineChannelType::Vec4: return std::holds_alternative<VansTimelineVec4>(value);
	case VansTimelineChannelType::Quaternion: return std::holds_alternative<VansTimelineQuaternion>(value);
	case VansTimelineChannelType::ColorLinear: return std::holds_alternative<VansTimelineColorLinear>(value);
	case VansTimelineChannelType::ColorSrgb: return std::holds_alternative<VansTimelineColorSrgb>(value);
	case VansTimelineChannelType::ObjectReference: return std::holds_alternative<VansTimelineObjectReference>(value);
	case VansTimelineChannelType::EventPayload: return std::holds_alternative<VansTimelineEventPayload>(value);
	}
	return false;
}

bool SupportsCurves(VansTimelineChannelType type)
{
	return type == VansTimelineChannelType::Float || type == VansTimelineChannelType::Double ||
		type == VansTimelineChannelType::Vec2 || type == VansTimelineChannelType::Vec3 ||
		type == VansTimelineChannelType::Vec4 || type == VansTimelineChannelType::Quaternion ||
		type == VansTimelineChannelType::ColorLinear || type == VansTimelineChannelType::ColorSrgb;
}

bool ConfigMatches(VansTimelineTrackType type, const VansTimelineTrackConfig& config)
{
	switch (type)
	{
	case VansTimelineTrackType::Transform: return std::holds_alternative<VansTimelineTransformTrackConfig>(config);
	case VansTimelineTrackType::Property: return std::holds_alternative<VansTimelinePropertyTrackConfig>(config);
	case VansTimelineTrackType::Activation: return std::holds_alternative<VansTimelineActivationTrackConfig>(config);
	case VansTimelineTrackType::Constraint: return std::holds_alternative<VansTimelineConstraintTrackConfig>(config);
	case VansTimelineTrackType::AnimationClip: return std::holds_alternative<VansTimelineAnimationTrackConfig>(config);
	case VansTimelineTrackType::AnimatorParameter: return std::holds_alternative<VansTimelineAnimatorParameterTrackConfig>(config);
	case VansTimelineTrackType::BoneOverride: return std::holds_alternative<VansTimelineBoneOverrideTrackConfig>(config);
	case VansTimelineTrackType::Audio: return std::holds_alternative<VansTimelineAudioTrackConfig>(config);
	case VansTimelineTrackType::Media: return std::holds_alternative<VansTimelineMediaTrackConfig>(config);
	case VansTimelineTrackType::Particle: return std::holds_alternative<VansTimelineParticleTrackConfig>(config);
	case VansTimelineTrackType::CameraCut: return std::holds_alternative<VansTimelineCameraCutTrackConfig>(config);
	case VansTimelineTrackType::CameraProperty: return std::holds_alternative<VansTimelineCameraPropertyTrackConfig>(config);
	case VansTimelineTrackType::CameraShake: return std::holds_alternative<VansTimelineCameraShakeTrackConfig>(config);
	case VansTimelineTrackType::FadePostProcess: return std::holds_alternative<VansTimelineFadePostProcessTrackConfig>(config);
	case VansTimelineTrackType::Light: return std::holds_alternative<VansTimelineLightTrackConfig>(config);
	case VansTimelineTrackType::MaterialParameter: return std::holds_alternative<VansTimelineMaterialParameterTrackConfig>(config);
	case VansTimelineTrackType::MaterialSwitch: return std::holds_alternative<VansTimelineMaterialSwitchTrackConfig>(config);
	case VansTimelineTrackType::UIState: return std::holds_alternative<VansTimelineUIStateTrackConfig>(config);
	case VansTimelineTrackType::EventSignal: return std::holds_alternative<VansTimelineEventTrackConfig>(config);
	case VansTimelineTrackType::SubTimeline: return std::holds_alternative<VansTimelineSubTimelineTrackConfig>(config);
	case VansTimelineTrackType::Spawnable: return std::holds_alternative<VansTimelineSpawnableTrackConfig>(config);
	case VansTimelineTrackType::TimeScale: return std::holds_alternative<VansTimelineTimeScaleTrackConfig>(config);
	case VansTimelineTrackType::SceneState: return std::holds_alternative<VansTimelineSceneStateTrackConfig>(config);
	case VansTimelineTrackType::Custom: return std::holds_alternative<VansTimelineCustomTrackConfig>(config);
	}
	return false;
}

bool BindingRequired(VansTimelineTrackType type)
{
	return type != VansTimelineTrackType::CameraCut && type != VansTimelineTrackType::FadePostProcess &&
		type != VansTimelineTrackType::EventSignal && type != VansTimelineTrackType::SubTimeline &&
		type != VansTimelineTrackType::Spawnable && type != VansTimelineTrackType::TimeScale &&
		type != VansTimelineTrackType::SceneState && type != VansTimelineTrackType::Custom;
}

bool IsEdgeTrack(VansTimelineTrackType type)
{
	return type == VansTimelineTrackType::Activation || type == VansTimelineTrackType::Audio ||
		type == VansTimelineTrackType::Media || type == VansTimelineTrackType::Particle ||
		type == VansTimelineTrackType::CameraCut || type == VansTimelineTrackType::MaterialSwitch ||
		type == VansTimelineTrackType::UIState || type == VansTimelineTrackType::EventSignal ||
		type == VansTimelineTrackType::SubTimeline || type == VansTimelineTrackType::Spawnable ||
		type == VansTimelineTrackType::SceneState;
}

void ValidateCapability(
	const VansTimelineTrack& track,
	const VansTimelineValidationContext& context,
	VansTimelineDiagnostics& diagnostics)
{
	auto unsupported = [&](const std::string& propertyPath, std::string message)
	{
		if (context.runtimeValidation) Error(diagnostics, track.id, propertyPath, std::move(message));
		else Warning(diagnostics, track.id, propertyPath, std::move(message));
	};
	if (track.type == VansTimelineTrackType::SceneState && !context.Supports(VansTimelineCapability::AdditiveScene))
		unsupported("type", "Scene State track requires the Additive Scene capability");
	if (track.type == VansTimelineTrackType::Spawnable && !context.Supports(VansTimelineCapability::SpawnTemplate))
		unsupported("type", "Spawnable track requires the stable Spawn Template capability");
	if (track.type == VansTimelineTrackType::TimeScale)
	{
		const auto* config = std::get_if<VansTimelineTimeScaleTrackConfig>(&track.config);
		if (config && config->scope != "LocalTimeWarp" && !context.Supports(VansTimelineCapability::GlobalTimeScale))
			unsupported("config.scope", "Global time scale is unavailable; use LocalTimeWarp");
	}
	if (track.type == VansTimelineTrackType::Custom)
	{
		const auto* config = std::get_if<VansTimelineCustomTrackConfig>(&track.config);
		if (!config || config->customTypeId.empty())
			Error(diagnostics, track.id, "config.customTypeId", "Custom track requires a registered customTypeId");
		else if (!context.supportsCustomTrack || !context.supportsCustomTrack(config->customTypeId))
			unsupported("config.customTypeId", "Custom track type is not registered: " + config->customTypeId);
	}
}

void ValidateTrackConfig(
	const VansTimelineTrack& track,
	const VansTimelineValidationContext& context,
	VansTimelineDiagnostics& diagnostics)
{
	if (!ConfigMatches(track.type, track.config))
	{
		Error(diagnostics, track.id, "config", "Track config does not match track type " +
			std::string(VansTimelineSerialization::TrackTypeName(track.type)));
		return;
	}
	if (const auto* config = std::get_if<VansTimelinePropertyTrackConfig>(&track.config))
	{
		if (config->componentTypeId == 0 || config->descriptorId.empty())
			Error(diagnostics, track.id, "config.descriptorId", "Property track requires componentTypeId and stable descriptorId");
		else if (context.supportsPropertyDescriptor)
		{
			if (!context.supportsPropertyDescriptor(
				config->componentTypeId, config->descriptorId, config->valueType))
				Error(diagnostics, track.id, "config.descriptorId",
					"Property descriptor is not registered for the configured component and value type");
		}
		else if (context.runtimeValidation)
			Error(diagnostics, track.id, "config.descriptorId",
				"Runtime Property validation requires the scene property registry");
		if (!(config->minimum <= config->maximum) || config->step <= 0.0)
			Error(diagnostics, track.id, "config", "Property range and step are invalid");
	}
	if (const auto* config = std::get_if<VansTimelineTransformTrackConfig>(&track.config))
	{
		if (!IsOneOf(config->space, { "Local", "World", "OwnerRelative" }))
			Error(diagnostics, track.id, "config.space", "Transform space must be Local, World or OwnerRelative");
		if (!IsOneOf(config->rotationMode, { "EulerShortestPath", "QuaternionSlerp" }))
			Error(diagnostics, track.id, "config.rotationMode", "Transform rotation mode is invalid");
		if (!IsOneOf(config->trajectoryDisplay, { "Never", "Selected", "Always" }))
			Error(diagnostics, track.id, "config.trajectoryDisplay", "Transform trajectory display mode is invalid");
		if (config->physicsPolicy != "RejectDynamicBody")
			Error(diagnostics, track.id, "config.physicsPolicy",
				"Only RejectDynamicBody is executable until PhysicsCore exposes deterministic kinematic commands");
		if (config->channels == 0 || (config->channels & ~0x3FFu) != 0)
			Error(diagnostics, track.id, "config.channels", "Transform channel mask contains no channels or unknown bits");
	}
	if (const auto* config = std::get_if<VansTimelineActivationTrackConfig>(&track.config))
	{
		if (!IsOneOf(config->scope, { "EntityActive", "ComponentEnabled", "RenderVisibility" }))
			Error(diagnostics, track.id, "config.scope", "Activation scope is invalid");
		if (!IsOneOf(config->stateBefore, { "Restore", "Active", "Inactive", "DoNothing" }) ||
			!IsOneOf(config->stateAfter, { "Restore", "Active", "Inactive", "DoNothing" }))
			Error(diagnostics, track.id, "config", "Activation stateBefore/stateAfter is invalid");
		if (!config->useCommandBuffer)
			Error(diagnostics, track.id, "config.useCommandBuffer", "Activation changes must use RuntimeWorld CommandBuffer");
	}
	if (const auto* config = std::get_if<VansTimelineAnimationTrackConfig>(&track.config))
	{
		if (config->slot.empty())
			Error(diagnostics, track.id, "config.slot", "Animation Clip track must target an Animation Layer slot");
		if (!std::isfinite(config->weight) || config->weight < 0.0)
			Error(diagnostics, track.id, "config.weight", "Animation weight must be finite and non-negative");
		if (!IsOneOf(config->rootMotionPolicy, { "Ignore", "ApplyToOwner", "ExtractOnly" }))
			Error(diagnostics, track.id, "config.rootMotionPolicy", "Animation root-motion policy is invalid");
		if (config->avatarMaskGuid.empty() != config->avatarMaskPath.empty())
			Error(diagnostics, track.id, "config.avatarMaskGuid",
				"Animation avatar masks require both indexed GUID and authoring path metadata");
		if (config->markerSync && config->syncGroup.empty())
			Error(diagnostics, track.id, "config.syncGroup", "Marker-synchronized animation requires a sync group");
	}
	if (const auto* config = std::get_if<VansTimelineAnimatorParameterTrackConfig>(&track.config))
	{
		if (config->parameterName.empty()) Error(diagnostics, track.id, "config.parameterName", "Animator parameter name is required");
		if (!IsOneOf(config->parameterType, { "Float", "Int", "Bool", "Trigger", "Vector3", "Quaternion" }))
			Error(diagnostics, track.id, "config.parameterType", "Animator parameter type is invalid");
		if (!IsOneOf(config->firePolicy, { "Forward", "Reverse", "Both" }) ||
			!IsOneOf(config->seekPolicy, { "Never", "Crossed", "ExactTick" }))
			Error(diagnostics, track.id, "config", "Animator Trigger edge policy is invalid");
		if (!IsOneOf(config->missingParameterPolicy, { "Error", "WarningAndSkip" }))
			Error(diagnostics, track.id, "config.missingParameterPolicy", "Missing parameter policy is invalid");
	}
	if (const auto* config = std::get_if<VansTimelineConstraintTrackConfig>(&track.config))
	{
		if (config->sourceBindingId.empty() || config->targetBindingId.empty())
			Error(diagnostics, track.id, "config", "Constraint requires source and target bindings");
		if (!std::isfinite(config->weight) || config->weight < 0.0 || config->weight > 1.0)
			Error(diagnostics, track.id, "config.weight", "Constraint weight must be in [0, 1]");
		if (!IsOneOf(config->constraintType, { "Parent", "Position", "Point", "Rotation", "Orient", "Scale", "Aim", "LookAt" }))
			Error(diagnostics, track.id, "config.constraintType", "Constraint type is unsupported by the runtime adapter");
		if (config->sourceBindingId == config->targetBindingId && !config->sourceBindingId.empty())
			Error(diagnostics, track.id, "config.targetBindingId", "Constraint cannot target its own source binding");
		if ((config->axisMask & ~0x7u) != 0 || config->axisMask == 0)
			Error(diagnostics, track.id, "config.axisMask", "Constraint axis mask is invalid");
		if (!IsOneOf(config->upAxis, { "X", "Y", "Z" }) || !IsOneOf(config->aimAxis, { "X", "Y", "Z" }))
			Error(diagnostics, track.id, "config", "Constraint aim/up axis is invalid");
	}
	if (const auto* config = std::get_if<VansTimelineBoneOverrideTrackConfig>(&track.config))
	{
		if (config->bone.empty() && config->boneId.empty())
			Error(diagnostics, track.id, "config.boneId", "Bone Override requires a bone name or stable bone ID");
		if (!IsOneOf(config->space, { "Local" }))
			Error(diagnostics, track.id, "config.space", "Only Local bone overrides are executable");
		if (config->weight < 0.0 || config->weight > 1.0 || config->positionWeight < 0.0 ||
			config->positionWeight > 1.0 || config->rotationWeight < 0.0 || config->rotationWeight > 1.0)
			Error(diagnostics, track.id, "config.weight", "Bone Override weights must be in [0, 1]");
		if (!config->clearOnExit)
			Error(diagnostics, track.id, "config.clearOnExit",
				"Bone Override clearOnExit must remain enabled; KeepState is expressed through CompletionMode");
	}
	if (const auto* config = std::get_if<VansTimelineAudioTrackConfig>(&track.config))
	{
		if (!std::isfinite(config->pitch) || config->pitch <= 0.0 || !std::isfinite(config->volume) ||
			config->volume < 0.0 || config->stereoPan < -1.0 || config->stereoPan > 1.0 ||
			config->spatialBlend < 0.0 || config->spatialBlend > 1.0 || config->fadeInSeconds < 0.0 ||
			config->fadeOutSeconds < 0.0 || config->reverbSend < 0.0 || config->reverbSend > 1.0 ||
			config->referenceDistance <= 0.0 || config->maxDistance < config->referenceDistance || config->rolloff < 0.0)
			Error(diagnostics, track.id, "config", "Audio pitch and spatial blend are invalid");
		if (!IsOneOf(config->seekPolicy, { "Exact", "NearestSupported", "RestartAndFastForward", "Disabled" }))
			Error(diagnostics, track.id, "config.seekPolicy", "Audio seek policy is invalid");
		if (!IsOneOf(config->onSectionEnd, { "Stop", "PlayToCompletion", "FadeOut" }))
			Error(diagnostics, track.id, "config.onSectionEnd", "Audio section-end policy is invalid");
	}
	if (const auto* config = std::get_if<VansTimelineMediaTrackConfig>(&track.config))
	{
		if (!IsOneOf(config->syncMode, { "TimelineClock", "AudioClock", "FreeRun" }))
			Error(diagnostics, track.id, "config.syncMode", "Media sync mode is invalid");
		if (config->syncMode == "AudioClock")
			AddDiagnostic(diagnostics, context.runtimeValidation ? VansTimelineDiagnosticSeverity::Error : VansTimelineDiagnosticSeverity::Warning,
				track.id, "config.syncMode", "AudioClock Media sync is unavailable without an explicit clock binding");
		if (!IsOneOf(config->targetKind, { "VideoComponent", "MaterialSlot", "UIElement" }))
			Error(diagnostics, track.id, "config.targetKind", "Media target kind is invalid");
		if (!IsOneOf(config->onSectionEnd, { "Stop", "PauseLastFrame", "Clear" }))
			Error(diagnostics, track.id, "config.onSectionEnd", "Media section-end policy is invalid");
		if (!IsOneOf(config->colorSpace, { "Linear", "Srgb" }))
			Error(diagnostics, track.id, "config.colorSpace", "Media color space is invalid");
		if (config->targetKind != "VideoComponent")
			AddDiagnostic(diagnostics, context.runtimeValidation ? VansTimelineDiagnosticSeverity::Error : VansTimelineDiagnosticSeverity::Warning,
				track.id, "config.targetKind", "Media material-slot and UI targets require registered RenderCore or RuntimeUI video setters");
		if (config->outputAudio)
			AddDiagnostic(diagnostics, context.runtimeValidation ? VansTimelineDiagnosticSeverity::Error : VansTimelineDiagnosticSeverity::Warning,
				track.id, "config.outputAudio", "Media audio output is unavailable in the current Video backend");
	}
	if (const auto* config = std::get_if<VansTimelineParticleTrackConfig>(&track.config))
	{
		if (!IsOneOf(config->action, { "Play", "Stop", "Restart", "Pause", "Burst" }))
			Error(diagnostics, track.id, "config.action", "Particle action is invalid");
		if (!std::isfinite(config->simulationRate) || config->simulationRate <= 0.0 || config->prewarmTicks < 0)
			Error(diagnostics, track.id, "config", "Particle simulation rate and prewarm range are invalid");
		if (!IsOneOf(config->seekPolicy, { "DeterministicResimulate", "RestartOnly" }))
			Error(diagnostics, track.id, "config.seekPolicy", "Particle SnapshotCache is unavailable; use deterministic resimulation or restart-only");
	}
	if (const auto* config = std::get_if<VansTimelineMaterialParameterTrackConfig>(&track.config))
	{
		if (config->materialSlotId.empty() || config->parameterName.empty())
			Error(diagnostics, track.id, "config", "Material parameter track requires slot id and parameter name");
		if (!IsOneOf(config->instancePolicy, { "PerEntityRuntimeInstance", "ExistingInstance" }))
			Error(diagnostics, track.id, "config.instancePolicy", "Material instance policy is invalid");
		if (config->instancePolicy == "ExistingInstance")
			Error(diagnostics, track.id, "config.instancePolicy",
				"ExistingInstance is unavailable because shared material assets cannot be mutated by Timeline");
	}
	if (const auto* config = std::get_if<VansTimelineMaterialSwitchTrackConfig>(&track.config))
		if (config->materialSlotId.empty())
			Error(diagnostics, track.id, "config.materialSlotId", "Material Switch requires a stable material slot ID");
	if (const auto* config = std::get_if<VansTimelineCameraCutTrackConfig>(&track.config))
	{
		if (!IsOneOf(config->cutMode, { "Cut", "Blend" }) || config->blendDurationTicks < 0)
			Error(diagnostics, track.id, "config", "Camera Cut mode or blend duration is invalid");
		if (!IsOneOf(config->aspectPolicy, { "Preserve", "MatchViewport", "Letterbox" }))
			Error(diagnostics, track.id, "config.aspectPolicy", "Camera aspect policy is invalid");
		if (config->viewport.empty())
			Error(diagnostics, track.id, "config.viewport", "Camera Cut requires a viewport ID");
		if (config->aspectPolicy == "Letterbox")
			AddDiagnostic(diagnostics, context.runtimeValidation ? VansTimelineDiagnosticSeverity::Error : VansTimelineDiagnosticSeverity::Warning,
				track.id, "config.aspectPolicy", "Letterbox requires a registered viewport composition adapter");
	}
	if (const auto* config = std::get_if<VansTimelineCameraPropertyTrackConfig>(&track.config))
		if (!config->fieldOfView && !config->nearClip && !config->farClip && !config->transform)
			Error(diagnostics, track.id, "config", "Camera Property must expose at least one runtime camera property");
	if (const auto* config = std::get_if<VansTimelineCameraShakeTrackConfig>(&track.config))
	{
		if (!config->position && !config->rotation)
			Error(diagnostics, track.id, "config", "Camera Shake must expose position or rotation offsets");
		if (!IsOneOf(config->space, { "CameraLocal", "World" }))
			Error(diagnostics, track.id, "config.space", "Camera Shake space must be CameraLocal or World");
		if (!std::isfinite(config->amplitudeScale) || config->amplitudeScale < 0.0)
			Error(diagnostics, track.id, "config.amplitudeScale", "Camera Shake amplitude scale must be non-negative");
	}
	if (const auto* config = std::get_if<VansTimelineFadePostProcessTrackConfig>(&track.config))
	{
		if (!IsOneOf(config->mode, { "Fade", "PostProcess" }))
			Error(diagnostics, track.id, "config.mode", "Fade/PostProcess mode is invalid");
		if (!std::isfinite(config->blendWeight) || config->blendWeight < 0.0 || config->blendWeight > 1.0)
			Error(diagnostics, track.id, "config.blendWeight", "PostProcess blend weight must be in [0, 1]");
		if (config->mode == "PostProcess" &&
			(context.runtimeValidation ? config->profileGuid.empty() : !HasAssetReference(config->profileGuid, config->profilePath)))
			Error(diagnostics, track.id, "config.profileGuid", context.runtimeValidation
				? "Runtime PostProcess tracks require an indexed Profile GUID"
				: "PostProcess mode requires a Profile asset reference");
	}
	if (const auto* config = std::get_if<VansTimelineUIStateTrackConfig>(&track.config))
	{
		if (config->screen.empty())
			Error(diagnostics, track.id, "config.screen", "UI State requires a stable Screen name or GUID");
		if (!IsOneOf(config->targetKind, { "Screen", "Element", "ViewModel", "Action" }))
			Error(diagnostics, track.id, "config.targetKind", "UI State target kind is invalid");
		if (config->targetKind == "Action" && (config->setterId != 200 || config->action.empty()))
			Error(diagnostics, track.id, "config.action", "UI Action requires setterId 200 and an action name");
		if (config->targetKind == "Screen" &&
			(config->setterId != 300 || config->descriptorId != "Screen.Visible"))
			Error(diagnostics, track.id, "config.descriptorId", "Current UI Screen adapter exposes Screen.Visible through setterId 300");
		if (config->targetKind == "Element" && (config->element.empty() || config->setterId == 0 || config->descriptorId.empty()))
			Error(diagnostics, track.id, "config.element", "UI Element requires an element path and registered setter");
		if (config->targetKind == "ViewModel" && (config->setterId != 100 || config->descriptorId.empty()))
			Error(diagnostics, track.id, "config.descriptorId", "UI ViewModel requires setterId 100 and a property name");
	}
	if (const auto* config = std::get_if<VansTimelineTimeScaleTrackConfig>(&track.config))
	{
		if (config->minimum < 0.0 || config->maximum < config->minimum)
			Error(diagnostics, track.id, "config", "Time scale range is invalid");
		if (config->scope != "LocalTimeWarp")
			Error(diagnostics, track.id, "config.scope", "Only LocalTimeWarp is available until RuntimeClock exists");
	}
	if (const auto* config = std::get_if<VansTimelineEventTrackConfig>(&track.config))
	{
		if (config->signalId.empty())
			Error(diagnostics, track.id, "config.signalId", "Event track requires a stable signalId");
		if (!IsOneOf(config->firePolicy, { "Forward", "Reverse", "Both" }))
			Error(diagnostics, track.id, "config.firePolicy", "Event firePolicy must be Forward, Reverse or Both");
		if (!IsOneOf(config->seekPolicy, { "Never", "Crossed", "ExactTick" }))
			Error(diagnostics, track.id, "config.seekPolicy", "Event seekPolicy must be Never, Crossed or ExactTick");
		if (!IsOneOf(config->loopPolicy, { "EveryLoop", "FirstLoopOnly" }))
			Error(diagnostics, track.id, "config.loopPolicy", "Event loopPolicy must be EveryLoop or FirstLoopOnly");
		if (!IsOneOf(config->eventLane, { "MainThread", "Script", "GameLogic", "Diagnostics" }))
			Error(diagnostics, track.id, "config.eventLane", "Event lane is not registered");
	}
}

void ValidateSectionConfig(
	const VansTimelineTrack& track,
	const VansTimelineSection& section,
	const VansTimelineValidationContext& context,
	VansTimelineDiagnostics& diagnostics)
{
	if (!std::holds_alternative<std::monostate>(section.config) && !ConfigMatches(track.type, section.config))
		Error(diagnostics, section.id, "config", "Section override config does not match its track type");
	else if (!std::holds_alternative<std::monostate>(section.config))
	{
		VansTimelineTrack sectionTrack = track;
		sectionTrack.id = section.id;
		sectionTrack.config = section.config;
		ValidateTrackConfig(sectionTrack, context, diagnostics);
	}
	const VansTimelineTrackConfig& effective = std::holds_alternative<std::monostate>(section.config)
		? track.config : section.config;
	const auto* audio = std::get_if<VansTimelineAudioTrackConfig>(&effective);
	const bool requiresAsset = track.type == VansTimelineTrackType::AnimationClip ||
		(track.type == VansTimelineTrackType::Audio && (!audio || !audio->useBoundSource)) ||
		track.type == VansTimelineTrackType::MaterialSwitch || track.type == VansTimelineTrackType::SubTimeline;
	if (requiresAsset &&
		(context.runtimeValidation ? section.assetGuid.empty() : !HasAssetReference(section.assetGuid, section.assetPath)))
	{
		Error(diagnostics, section.id, "assetGuid", context.runtimeValidation
			? "Runtime sections require an indexed asset GUID; paths are authoring hints only"
			: "Section requires an asset reference");
	}
	if (section.sourceInTick < 0 || (section.sourceOutTick >= 0 && section.sourceOutTick <= section.sourceInTick))
		Error(diagnostics, section.id, "sourceOutTick", "Section source range is invalid");
	if (!IsOneOf(section.blendIn.shape, { "Linear", "EaseIn", "EaseOut", "EaseInOut", "SmoothStep" }) ||
		!IsOneOf(section.blendOut.shape, { "Linear", "EaseIn", "EaseOut", "EaseInOut", "SmoothStep" }) ||
		section.blendIn.exponent <= 0.0 || section.blendOut.exponent <= 0.0)
		Error(diagnostics, section.id, "blendIn", "Section blend curve is invalid");
	if (track.type == VansTimelineTrackType::Media && section.reverse)
		Error(diagnostics, section.id, "reverse", "Media reverse playback is unavailable in the current Video backend");
	if (track.type == VansTimelineTrackType::CameraCut)
	{
		const auto* sectionConfig = std::get_if<VansTimelineCameraCutTrackConfig>(&section.config);
		const auto* trackConfig = std::get_if<VansTimelineCameraCutTrackConfig>(&track.config);
		const auto* config = sectionConfig ? sectionConfig : trackConfig;
		if (!config || config->cameraBindingId.empty())
			Error(diagnostics, section.id, "config.cameraBindingId", "Camera Cut section requires a camera binding");
	}
	if (track.type == VansTimelineTrackType::CameraProperty)
	{
		const auto* config = std::get_if<VansTimelineCameraPropertyTrackConfig>(&effective);
		if (config)
		{
			for (const VansTimelineChannel& channel : section.channels)
			{
				const std::string name = LowerAscii(channel.name);
				const bool numeric = channel.type == VansTimelineChannelType::Float ||
					channel.type == VansTimelineChannelType::Double;
				if ((name == "fieldofview" || name == "fov") && (!config->fieldOfView || !numeric))
					Error(diagnostics, channel.id, "name", "Camera FOV channel is disabled or has a non-numeric type");
				else if (name == "nearclip" && (!config->nearClip || !numeric))
					Error(diagnostics, channel.id, "name", "Camera Near Clip channel is disabled or has a non-numeric type");
				else if (name == "farclip" && (!config->farClip || !numeric))
					Error(diagnostics, channel.id, "name", "Camera Far Clip channel is disabled or has a non-numeric type");
				else if ((name == "position" || name == "transform.position") &&
					(!config->transform || channel.type != VansTimelineChannelType::Vec3))
					Error(diagnostics, channel.id, "name", "Camera Position requires Transform enabled and a Vec3 channel");
				else if ((name == "rotation" || name == "transform.rotation") &&
					(!config->transform || channel.type != VansTimelineChannelType::Quaternion))
					Error(diagnostics, channel.id, "name", "Camera Rotation requires Transform enabled and a Quaternion channel");
				else if ((name == "scale" || name == "transform.scale") &&
					(!config->transform || channel.type != VansTimelineChannelType::Vec3))
					Error(diagnostics, channel.id, "name", "Camera Scale requires Transform enabled and a Vec3 channel");
				else if (name != "fieldofview" && name != "fov" && name != "nearclip" && name != "farclip" &&
					name != "position" && name != "transform.position" && name != "rotation" &&
					name != "transform.rotation" && name != "scale" && name != "transform.scale")
					Error(diagnostics, channel.id, "name", "Camera property is not exposed by the runtime Camera adapter");
			}
		}
	}
	if (track.type == VansTimelineTrackType::CameraShake)
	{
		const auto* config = std::get_if<VansTimelineCameraShakeTrackConfig>(&effective);
		if (config)
		{
			for (const VansTimelineChannel& channel : section.channels)
			{
				const std::string name = LowerAscii(channel.name);
				if ((name == "positionoffset" || name == "position" || name == "offset") &&
					(!config->position || channel.type != VansTimelineChannelType::Vec3))
					Error(diagnostics, channel.id, "name", "Camera Shake position offset requires position enabled and a Vec3 channel");
				else if ((name == "rotationoffset" || name == "rotation" || name == "euler") &&
					(!config->rotation || channel.type != VansTimelineChannelType::Vec3))
					Error(diagnostics, channel.id, "name", "Camera Shake rotation offset requires rotation enabled and a Vec3 channel");
				else if (name != "positionoffset" && name != "position" && name != "offset" &&
					name != "rotationoffset" && name != "rotation" && name != "euler")
					Error(diagnostics, channel.id, "name", "Camera Shake channel must be positionOffset or rotationOffset");
			}
		}
	}
}

void ValidateConstraintReferencesAndCycles(
	const VansTimelineAsset& asset,
	const std::unordered_set<VansTimelineId>& bindingIds,
	VansTimelineDiagnostics& diagnostics)
{
	struct Edge { VansTimelineId source; VansTimelineId target; VansTimelineId trackId; };
	std::vector<Edge> edges;
	for (const VansTimelineTrack& track : asset.tracks)
	{
		if (track.type != VansTimelineTrackType::Constraint) continue;
		auto collect = [&](const VansTimelineTrackConfig& value)
		{
			const auto* config = std::get_if<VansTimelineConstraintTrackConfig>(&value);
			if (!config) return;
			if (bindingIds.find(config->sourceBindingId) == bindingIds.end())
				Error(diagnostics, track.id, "config.sourceBindingId", "Constraint source binding does not exist");
			if (bindingIds.find(config->targetBindingId) == bindingIds.end())
				Error(diagnostics, track.id, "config.targetBindingId", "Constraint target binding does not exist");
			if (!config->sourceBindingId.empty() && !config->targetBindingId.empty())
				edges.push_back({ config->sourceBindingId, config->targetBindingId, track.id });
		};
		collect(track.config);
		for (const VansTimelineSection& section : track.sections)
			if (!std::holds_alternative<std::monostate>(section.config)) collect(section.config);
	}
	for (const Edge& edge : edges)
	{
		std::vector<VansTimelineId> pending{ edge.target };
		std::unordered_set<VansTimelineId> visited;
		while (!pending.empty())
		{
			VansTimelineId current = std::move(pending.back());
			pending.pop_back();
			if (!visited.insert(current).second) continue;
			if (current == edge.source)
			{
				Error(diagnostics, edge.trackId, "config.targetBindingId", "Constraint dependency graph contains a cycle");
				break;
			}
			for (const Edge& candidate : edges)
				if (candidate.source == current) pending.push_back(candidate.target);
		}
	}
}

bool SectionsOverlap(const VansTimelineTrack& left, const VansTimelineTrack& right)
{
	for (const VansTimelineSection& a : left.sections)
		for (const VansTimelineSection& b : right.sections)
			if (a.startTick < b.startTick + b.durationTicks && b.startTick < a.startTick + a.durationTicks)
				return true;
	return false;
}

bool TrackTypeCanConflict(VansTimelineTrackType type)
{
	return type != VansTimelineTrackType::Audio && type != VansTimelineTrackType::Media &&
		type != VansTimelineTrackType::Particle && type != VansTimelineTrackType::EventSignal &&
		type != VansTimelineTrackType::SubTimeline && type != VansTimelineTrackType::TimeScale;
}

void ValidateEqualPriorityConflicts(const VansTimelineAsset& asset, VansTimelineDiagnostics& diagnostics)
{
	for (std::size_t left = 0; left < asset.tracks.size(); ++left)
	{
		const VansTimelineTrack& a = asset.tracks[left];
		if (!a.enabled || a.runtimeMuted || a.blendMode != VansTimelineBlendMode::Override ||
			!TrackTypeCanConflict(a.type)) continue;
		for (std::size_t right = left + 1; right < asset.tracks.size(); ++right)
		{
			const VansTimelineTrack& b = asset.tracks[right];
			if (!b.enabled || b.runtimeMuted || b.blendMode != VansTimelineBlendMode::Override ||
				a.type != b.type || a.bindingId != b.bindingId || a.priority != b.priority ||
				!SectionsOverlap(a, b)) continue;
			Warning(diagnostics, b.id, "priority",
				"Equal-priority Override tracks overlap the same target; stable track order chooses the final writer");
		}
	}
}

void ValidateGroupCycles(
	const VansTimelineAsset& asset,
	const std::unordered_set<VansTimelineId>& groupIds,
	VansTimelineDiagnostics& diagnostics)
{
	std::unordered_map<VansTimelineId, VansTimelineId> parents;
	for (const auto& group : asset.groups)
	{
		if (!group.parentId.empty())
		{
			if (groupIds.find(group.parentId) == groupIds.end())
				Error(diagnostics, group.id, "parentId", "Group parent does not exist");
			parents[group.id] = group.parentId;
		}
	}
	for (const auto& group : asset.groups)
	{
		std::unordered_set<VansTimelineId> path;
		VansTimelineId current = group.id;
		while (!current.empty())
		{
			if (!path.insert(current).second)
			{
				Error(diagnostics, group.id, "parentId", "Group hierarchy contains a cycle");
				break;
			}
			const auto parent = parents.find(current);
			if (parent == parents.end()) break;
			current = parent->second;
		}
	}
}
}

VansTimelineDiagnostics VansTimelineValidator::Validate(
	const VansTimelineAsset& asset,
	const VansTimelineValidationContext& context)
{
	VansTimelineDiagnostics diagnostics;
	if (asset.assetKind != "Timeline")
		Error(diagnostics, {}, "assetKind", "assetKind must be Timeline");
	if (asset.timebase.ticksPerSecond <= 0 || asset.timebase.displayRateNumerator <= 0 ||
		asset.timebase.displayRateDenominator <= 0)
		Error(diagnostics, {}, "timebase", "Timeline timebase values must be positive");
	if (asset.durationTicks <= 0)
		Error(diagnostics, {}, "durationTicks", "Timeline duration must be positive");
	if (asset.playbackRange.startTick < 0 || asset.playbackRange.endTick <= asset.playbackRange.startTick ||
		asset.playbackRange.endTick > asset.durationTicks)
		Error(diagnostics, {}, "playbackRange", "Playback range must be non-empty and contained by the duration");
	if (asset.workRange.endTick < asset.workRange.startTick)
		Error(diagnostics, {}, "workRange", "Work range end must not precede its start");

	std::unordered_map<VansTimelineId, std::string> allIds;
	std::unordered_set<VansTimelineId> bindingIds;
	std::unordered_set<VansTimelineId> groupIds;
	for (const auto& binding : asset.bindings)
	{
		RegisterStableId(binding.id, "Binding", allIds, diagnostics);
		bindingIds.insert(binding.id);
		if (binding.required && binding.kind != VansTimelineBindingKind::RuntimeObject &&
			binding.kind != VansTimelineBindingKind::External && binding.targetGuid.empty() &&
			binding.assetGuid.empty() && binding.assetPath.empty())
			Warning(diagnostics, binding.id, "targetGuid", "Required binding has no default target and must be overridden by an instance");
		if (context.runtimeValidation && binding.kind == VansTimelineBindingKind::Asset && binding.assetGuid.empty())
			Error(diagnostics, binding.id, "assetGuid", "Runtime asset bindings require an indexed asset GUID");
	}
	for (const auto& group : asset.groups)
	{
		RegisterStableId(group.id, "Group", allIds, diagnostics);
		groupIds.insert(group.id);
	}
	ValidateGroupCycles(asset, groupIds, diagnostics);
	ValidateConstraintReferencesAndCycles(asset, bindingIds, diagnostics);
	ValidateEqualPriorityConflicts(asset, diagnostics);

	for (const auto& track : asset.tracks)
	{
		RegisterStableId(track.id, "Track", allIds, diagnostics);
		if (BindingRequired(track.type) && (track.bindingId.empty() || bindingIds.find(track.bindingId) == bindingIds.end()))
			Error(diagnostics, track.id, "bindingId", "Track requires an existing binding");
		if (!track.groupId.empty() && groupIds.find(track.groupId) == groupIds.end())
			Error(diagnostics, track.id, "groupId", "Track group does not exist");
		ValidateTrackConfig(track, context, diagnostics);
		ValidateCapability(track, context, diagnostics);
		if (const auto* camera = std::get_if<VansTimelineCameraCutTrackConfig>(&track.config))
		{
			if (bindingIds.find(camera->cameraBindingId) == bindingIds.end())
				Error(diagnostics, track.id, "config.cameraBindingId", "Camera Cut binding does not exist");
			if (!camera->targetCameraBindingId.empty() && bindingIds.find(camera->targetCameraBindingId) == bindingIds.end())
				Error(diagnostics, track.id, "config.targetCameraBindingId", "Camera Cut target camera binding does not exist");
		}

		VansTimelineTick previousSectionStart = std::numeric_limits<VansTimelineTick>::min();
		for (const auto& section : track.sections)
		{
			RegisterStableId(section.id, "Section", allIds, diagnostics);
			if (section.startTick < previousSectionStart)
				Error(diagnostics, section.id, "startTick", "Sections must be stored in deterministic start-tick order");
			previousSectionStart = section.startTick;
			if (section.durationTicks <= 0 || section.startTick < 0 ||
				section.startTick > asset.durationTicks || section.durationTicks > asset.durationTicks - section.startTick)
				Error(diagnostics, section.id, "durationTicks", "Section range must be positive and contained by the Timeline duration");
			if (!std::isfinite(section.playRate) || section.playRate <= 0.0)
				Error(diagnostics, section.id, "playRate", "Section play rate must be finite and positive");
			if (section.loopCount <= 0)
				Error(diagnostics, section.id, "loopCount", "Section loop count must be positive");
			if (section.preRollTicks < 0 || section.postRollTicks < 0 || section.easeInTicks < 0 || section.easeOutTicks < 0 ||
				section.easeInTicks + section.easeOutTicks > section.durationTicks)
				Error(diagnostics, section.id, "easeInTicks", "Section preroll, postroll and easing values are invalid");
			ValidateSectionConfig(track, section, context, diagnostics);
			const VansTimelineTrackConfig& effectiveConfig =
				std::holds_alternative<std::monostate>(section.config) ? track.config : section.config;
			if (const auto* camera = std::get_if<VansTimelineCameraCutTrackConfig>(&effectiveConfig))
			{
				if (bindingIds.find(camera->cameraBindingId) == bindingIds.end())
					Error(diagnostics, section.id, "config.cameraBindingId", "Camera Cut binding does not exist");
				if (!camera->targetCameraBindingId.empty() && bindingIds.find(camera->targetCameraBindingId) == bindingIds.end())
					Error(diagnostics, section.id, "config.targetCameraBindingId", "Camera Cut target camera binding does not exist");
			}
			if (const auto* property = std::get_if<VansTimelinePropertyTrackConfig>(&effectiveConfig))
			{
				if (section.channels.size() != 1)
					Error(diagnostics, section.id, "channels", "Property sections require exactly one channel");
				else if (section.channels.front().type != property->valueType)
					Error(diagnostics, section.channels.front().id, "type",
						"Property channel type does not match the registered descriptor value type");
			}

			for (const auto& channel : section.channels)
			{
				RegisterStableId(channel.id, "Channel", allIds, diagnostics);
				VansTimelineTick previousTick = std::numeric_limits<VansTimelineTick>::min();
				for (const auto& key : channel.keys)
				{
					RegisterStableId(key.id, "Key", allIds, diagnostics);
					if (key.tick < previousTick)
						Error(diagnostics, key.id, "tick", "Keys must be stored in deterministic tick order");
					if (key.tick == previousTick && !IsEdgeTrack(track.type))
						Error(diagnostics, key.id, "tick", "Continuous channels cannot contain duplicate key ticks");
					previousTick = key.tick;
					if (key.tick < 0)
						Error(diagnostics, key.id, "tick", "Section-local key ticks must not be negative");
					if (!KeyValueMatches(channel.type, key.value))
						Error(diagnostics, key.id, "value", "Key value does not match its channel type");
					if (!SupportsCurves(channel.type) && key.interpolation != VansTimelineInterpolation::Constant)
						Error(diagnostics, key.id, "interpolation", "Discrete channels only support Constant interpolation");
					if (key.interpolation == VansTimelineInterpolation::Slerp && channel.type != VansTimelineChannelType::Quaternion)
						Error(diagnostics, key.id, "interpolation", "Slerp is only valid for quaternion channels");
				}
			}
		}
	}
	for (const auto& marker : asset.markers)
	{
		RegisterStableId(marker.id, "Marker", allIds, diagnostics);
		if (marker.tick < 0 || marker.tick > asset.durationTicks)
			Error(diagnostics, marker.id, "tick", "Marker must be contained by the Timeline duration");
	}
	return diagnostics;
}

bool VansTimelineValidator::HasErrors(const VansTimelineDiagnostics& diagnostics)
{
	return std::any_of(diagnostics.begin(), diagnostics.end(), [](const VansTimelineDiagnostic& diagnostic)
	{
		return diagnostic.severity == VansTimelineDiagnosticSeverity::Error;
	});
}
}
