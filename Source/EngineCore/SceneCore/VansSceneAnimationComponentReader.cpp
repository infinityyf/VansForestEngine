#include "VansSceneAnimationComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cstdint>

namespace Vans
{
namespace
{
using VansGraphics::FootPlacementSettings;
using VansGraphics::MotionMatchingClipMetadata;
using VansGraphics::MotionMatchingSearchGroup;
using VansGraphics::MotionMatchingSettings;

const VansSerializedValue* ReadObjectField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
}

const VansSerializedValue* ReadArrayField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Array ? field : nullptr;
}

bool HasObjectField(const VansSerializedValue& object, const char* key)
{
	return FindObjectField(object, key) != nullptr;
}

float ReadFloatField(const VansSerializedValue& object, const char* key, float fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && (field->kind == VansSerializedValue::Kind::Float ||
		field->kind == VansSerializedValue::Kind::Int)
		? static_cast<float>(ReadSerializedNumber(*field))
		: fallback;
}

int ReadIntField(const VansSerializedValue& object, const char* key, int fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Int
		? static_cast<int>(field->intValue)
		: fallback;
}

std::uint32_t ReadUInt32Field(const VansSerializedValue& object, const char* key, std::uint32_t fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field || field->kind != VansSerializedValue::Kind::Int || field->intValue < 0)
		return fallback;
	return static_cast<std::uint32_t>(field->intValue);
}

bool ReadBoolField(const VansSerializedValue& object, const char* key, bool fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Bool ? field->boolValue : fallback;
}

glm::vec3 ReadVec3Field(const VansSerializedValue& object, const char* key, const glm::vec3& fallback)
{
	const VansSerializedValue* value = FindObjectField(object, key);
	if (!value)
		return fallback;
	if (value->kind == VansSerializedValue::Kind::Array && value->arrayItems.size() >= 3)
	{
		for (size_t index = 0; index < 3; ++index)
		{
			const VansSerializedValue& item = value->arrayItems[index];
			if (item.kind != VansSerializedValue::Kind::Float &&
				item.kind != VansSerializedValue::Kind::Int)
			{
				return fallback;
			}
		}
		return glm::vec3(
			static_cast<float>(ReadSerializedNumber(value->arrayItems[0])),
			static_cast<float>(ReadSerializedNumber(value->arrayItems[1])),
			static_cast<float>(ReadSerializedNumber(value->arrayItems[2])));
	}
	if (value->kind == VansSerializedValue::Kind::Object)
	{
		return glm::vec3(
			ReadFloatField(*value, "x", fallback.x),
			ReadFloatField(*value, "y", fallback.y),
			ReadFloatField(*value, "z", fallback.z));
	}
	return fallback;
}

void ReadStringArray(const VansSerializedValue& object, const char* key, std::vector<std::string>& out)
{
	const VansSerializedValue* array = ReadArrayField(object, key);
	if (!array)
		return;
	for (const VansSerializedValue& item : array->arrayItems)
		if (item.kind == VansSerializedValue::Kind::String)
			out.push_back(item.stringValue);
}

std::string ReadAssetReferenceField(
	const VansSerializedValue& object,
	const char* key,
	const std::string& fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (!field)
		return fallback;
	if (field->kind == VansSerializedValue::Kind::String)
		return field->stringValue;
	if (field->kind == VansSerializedValue::Kind::Object)
		return ReadSerializedStringField(*field, "guid", fallback);
	return fallback;
}

void ReadIntArray(const VansSerializedValue& object, const char* key, std::vector<int>& out)
{
	const VansSerializedValue* array = ReadArrayField(object, key);
	if (!array)
		return;
	for (const VansSerializedValue& item : array->arrayItems)
		if (item.kind == VansSerializedValue::Kind::Int)
			out.push_back(static_cast<int>(item.intValue));
}

void ReadReplacingIntArray(const VansSerializedValue& object, const char* key, std::vector<int>& out)
{
	if (!ReadArrayField(object, key))
		return;
	out.clear();
	ReadIntArray(object, key, out);
}

void ReadOptionalBool(const VansSerializedValue& object, const char* key, bool& hasValue, bool& value)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (field && field->kind == VansSerializedValue::Kind::Bool)
	{
		hasValue = true;
		value = field->boolValue;
	}
}

void ReadOptionalInt(const VansSerializedValue& object, const char* key, bool& hasValue, int& value)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	if (field && field->kind == VansSerializedValue::Kind::Int)
	{
		hasValue = true;
		value = static_cast<int>(field->intValue);
	}
}

MotionMatchingSearchGroup DecodeSearchGroup(const VansSerializedValue& groupJson)
{
	MotionMatchingSearchGroup group;
	group.name = ReadSerializedStringField(groupJson, "name", "");
	group.stance = ReadSerializedStringField(groupJson, "stance", group.stance);
	group.phase = ReadSerializedStringField(groupJson, "phase", group.phase);
	ReadIntArray(groupJson, "move_states", group.moveStates);
	ReadIntArray(groupJson, "moveStates", group.moveStates);
	ReadStringArray(groupJson, "include", group.includeClipNameTokens);
	ReadStringArray(groupJson, "include_tokens", group.includeClipNameTokens);
	ReadStringArray(groupJson, "include_clip_tokens", group.includeClipNameTokens);
	ReadStringArray(groupJson, "exclude", group.excludeClipNameTokens);
	ReadStringArray(groupJson, "exclude_tokens", group.excludeClipNameTokens);
	ReadStringArray(groupJson, "exclude_clip_tokens", group.excludeClipNameTokens);
	return group;
}

MotionMatchingClipMetadata DecodeClipMetadata(const VansSerializedValue& itemJson)
{
	MotionMatchingClipMetadata metadata;
	metadata.name = ReadSerializedStringField(itemJson, "name", "");
	ReadStringArray(itemJson, "match", metadata.matchTokens);
	ReadStringArray(itemJson, "match_tokens", metadata.matchTokens);
	ReadStringArray(itemJson, "matchTokens", metadata.matchTokens);
	ReadOptionalBool(itemJson, "loop", metadata.hasLoopLike, metadata.loopLike);
	ReadOptionalBool(itemJson, "loop_like", metadata.hasLoopLike, metadata.loopLike);
	ReadOptionalBool(itemJson, "idle", metadata.hasIdleLike, metadata.idleLike);
	ReadOptionalBool(itemJson, "idle_like", metadata.hasIdleLike, metadata.idleLike);
	ReadOptionalBool(itemJson, "transition", metadata.hasTransitionLike, metadata.transitionLike);
	ReadOptionalBool(itemJson, "transition_like", metadata.hasTransitionLike, metadata.transitionLike);
	ReadOptionalBool(itemJson, "start", metadata.hasStartLike, metadata.startLike);
	ReadOptionalBool(itemJson, "start_like", metadata.hasStartLike, metadata.startLike);
	ReadOptionalBool(itemJson, "stop", metadata.hasStopLike, metadata.stopLike);
	ReadOptionalBool(itemJson, "stop_like", metadata.hasStopLike, metadata.stopLike);
	ReadOptionalBool(itemJson, "turn", metadata.hasTurnLike, metadata.turnLike);
	ReadOptionalBool(itemJson, "turn_like", metadata.hasTurnLike, metadata.turnLike);
	ReadOptionalBool(itemJson, "pace_transition", metadata.hasPaceTransitionLike, metadata.paceTransitionLike);
	ReadOptionalBool(itemJson, "pace_transition_like", metadata.hasPaceTransitionLike, metadata.paceTransitionLike);
	ReadOptionalInt(itemJson, "source_move_state", metadata.hasSourceMoveState, metadata.sourceMoveState);
	ReadOptionalInt(itemJson, "sourceMoveState", metadata.hasSourceMoveState, metadata.sourceMoveState);
	ReadOptionalInt(itemJson, "target_move_state", metadata.hasTargetMoveState, metadata.targetMoveState);
	ReadOptionalInt(itemJson, "targetMoveState", metadata.hasTargetMoveState, metadata.targetMoveState);
	ReadOptionalInt(itemJson, "direction_bucket", metadata.hasDirectionBucket, metadata.directionBucket);
	ReadOptionalInt(itemJson, "directionBucket", metadata.hasDirectionBucket, metadata.directionBucket);
	ReadOptionalInt(itemJson, "turn_direction_sign", metadata.hasTurnDirectionSign, metadata.turnDirectionSign);
	ReadOptionalInt(itemJson, "turnDirectionSign", metadata.hasTurnDirectionSign, metadata.turnDirectionSign);
	ReadOptionalInt(itemJson, "turn_bucket_delta", metadata.hasTurnBucketDelta, metadata.turnBucketDelta);
	ReadOptionalInt(itemJson, "turnBucketDelta", metadata.hasTurnBucketDelta, metadata.turnBucketDelta);
	return metadata;
}

MotionMatchingSettings DecodeMotionMatching(const VansSerializedValue& mmJson)
{
	MotionMatchingSettings settings;
	settings.enabled = ReadBoolField(mmJson, "enabled", false);
	settings.autoBuild = ReadBoolField(mmJson, "auto_build", true);
	settings.externallyDriven = ReadBoolField(mmJson, "externally_driven", false);
	settings.sampleRate = ReadFloatField(mmJson, "sample_rate", 30.0f);
	settings.searchThrottle = ReadFloatField(mmJson, "search_throttle", 0.15f);
	const float legacyBlendDuration = ReadFloatField(mmJson, "blend_duration", 0.18f);
	settings.minSwitchCostImprovement = ReadFloatField(mmJson, "min_switch_cost_improvement", 0.02f);
	settings.minSwitchCostRatio = ReadFloatField(mmJson, "min_switch_cost_ratio", settings.minSwitchCostRatio);
	settings.minSwitchInterval = ReadFloatField(mmJson, "min_switch_interval", 0.25f);
	settings.blendInterruptFraction = ReadFloatField(mmJson, "blend_interrupt_fraction", 0.75f);
	settings.continuationBias = ReadFloatField(mmJson, "continuation_bias", 0.10f);
	settings.loopBias = ReadFloatField(mmJson, "loop_bias", 0.04f);
	settings.transitionBias = ReadFloatField(mmJson, "transition_bias", 0.08f);
	settings.desiredSpeedScale = ReadFloatField(mmJson, "desired_speed_scale", 650.0f);
	settings.worldToAnimationScale = ReadFloatField(mmJson, "world_to_animation_scale", 1.0f);
	settings.enableSpeedMatching = ReadBoolField(mmJson, "speed_matching_enabled", true);
	settings.minPlaybackRate = ReadFloatField(mmJson, "min_playback_rate", 0.75f);
	settings.maxPlaybackRate = ReadFloatField(mmJson, "max_playback_rate", 1.25f);
	settings.playbackRateSmoothing = ReadFloatField(mmJson, "playback_rate_smoothing", 12.0f);
	settings.trajectoryResponsiveness = ReadFloatField(mmJson, "trajectory_responsiveness", settings.trajectoryResponsiveness);
	settings.inertializationHalfLife = ReadFloatField(
		mmJson,
		"inertialization_half_life",
		(std::max)(0.01f, legacyBlendDuration * 0.55f));
	settings.inertializationMaxDuration = ReadFloatField(
		mmJson,
		"inertialization_max_duration",
		settings.inertializationMaxDuration);
	settings.trajectoryWeight = ReadFloatField(mmJson, "trajectory_weight", 1.0f);
	settings.poseWeight = ReadFloatField(mmJson, "pose_weight", 0.7f);
	settings.contactWeight = ReadFloatField(mmJson, "contact_weight", settings.contactWeight);
	settings.contactHeightFullFraction = ReadFloatField(
		mmJson,
		"contact_height_full_fraction",
		settings.contactHeightFullFraction);
	settings.contactHeightFadeFraction = ReadFloatField(
		mmJson,
		"contact_height_fade_fraction",
		settings.contactHeightFadeFraction);
	settings.contactVelocityConfidenceFloor = ReadFloatField(
		mmJson,
		"contact_velocity_confidence_floor",
		settings.contactVelocityConfidenceFloor);
	settings.topCandidateCount = ReadIntField(mmJson, "top_candidates", 8);
	settings.allowLegacyBoneDetection = ReadBoolField(mmJson, "allow_legacy_bone_detection", true);

	if (const VansSerializedValue* paramsJson = ReadObjectField(mmJson, "parameters"))
	{
		settings.parameters.enabled = ReadSerializedStringField(*paramsJson, "enabled", settings.parameters.enabled);
		settings.parameters.speed = ReadSerializedStringField(*paramsJson, "speed", settings.parameters.speed);
		settings.parameters.direction = ReadSerializedStringField(*paramsJson, "direction", settings.parameters.direction);
		settings.parameters.crouching = ReadSerializedStringField(*paramsJson, "crouching", settings.parameters.crouching);
		settings.parameters.airborne = ReadSerializedStringField(*paramsJson, "airborne", settings.parameters.airborne);
		settings.parameters.moveState = ReadSerializedStringField(*paramsJson, "move_state", settings.parameters.moveState);
		settings.parameters.moveState = ReadSerializedStringField(*paramsJson, "moveState", settings.parameters.moveState);
	}

	if (const VansSerializedValue* rigJson = ReadObjectField(mmJson, "rig"))
	{
		settings.rig.root = ReadSerializedStringField(*rigJson, "root", "");
		settings.rig.trajectoryRoot = ReadSerializedStringField(*rigJson, "trajectory_root", "");
		settings.rig.pelvis = ReadSerializedStringField(*rigJson, "pelvis", "");
		settings.rig.leftFoot = ReadSerializedStringField(*rigJson, "left_foot", "");
		settings.rig.rightFoot = ReadSerializedStringField(*rigJson, "right_foot", "");
		settings.rig.head = ReadSerializedStringField(*rigJson, "head", "");
		settings.rig.forwardAxis = ReadVec3Field(*rigJson, "forward_axis", settings.rig.forwardAxis);
	}

	if (const VansSerializedValue* schemaJson = ReadObjectField(mmJson, "schema"))
	{
		settings.trajectoryWeight = ReadFloatField(*schemaJson, "trajectory_weight", settings.trajectoryWeight);
		settings.poseWeight = ReadFloatField(*schemaJson, "pose_weight", settings.poseWeight);
		if (const VansSerializedValue* futureTimes = ReadArrayField(*schemaJson, "future_times"))
		{
			for (size_t i = 0; i < settings.schema.futureTimes.size() && i < futureTimes->arrayItems.size(); ++i)
			{
				const VansSerializedValue& item = futureTimes->arrayItems[i];
				if (item.kind == VansSerializedValue::Kind::Float || item.kind == VansSerializedValue::Kind::Int)
					settings.schema.futureTimes[i] = static_cast<float>(ReadSerializedNumber(item));
			}
		}
	}

	ReadStringArray(mmJson, "include_clip_tokens", settings.includeClipNameTokens);
	ReadStringArray(mmJson, "exclude_clip_tokens", settings.excludeClipNameTokens);

	if (const VansSerializedValue* statesJson = ReadObjectField(mmJson, "states"))
	{
		settings.states.idleState = ReadIntField(*statesJson, "idle", settings.states.idleState);
		settings.states.idleState = ReadIntField(*statesJson, "idle_state", settings.states.idleState);
		settings.states.crouchState = ReadIntField(*statesJson, "crouch", settings.states.crouchState);
		settings.states.crouchState = ReadIntField(*statesJson, "crouch_state", settings.states.crouchState);
		settings.states.idleSpeedThreshold = ReadFloatField(
			*statesJson,
			"idle_speed_threshold",
			settings.states.idleSpeedThreshold);
		ReadReplacingIntArray(*statesJson, "moving", settings.states.movingStates);
		ReadReplacingIntArray(*statesJson, "moving_states", settings.states.movingStates);
		ReadReplacingIntArray(*statesJson, "pace_transition", settings.states.paceTransitionStates);
		ReadReplacingIntArray(*statesJson, "pace_transition_states", settings.states.paceTransitionStates);
		ReadReplacingIntArray(*statesJson, "stance", settings.states.stanceStates);
		ReadReplacingIntArray(*statesJson, "stance_states", settings.states.stanceStates);
	}

	const VansSerializedValue* searchGroupsJson = ReadArrayField(mmJson, "search_groups");
	if (!searchGroupsJson)
		searchGroupsJson = ReadArrayField(mmJson, "searchGroups");
	if (searchGroupsJson)
	{
		for (const VansSerializedValue& groupJson : searchGroupsJson->arrayItems)
		{
			if (groupJson.kind != VansSerializedValue::Kind::Object)
				continue;
			MotionMatchingSearchGroup group = DecodeSearchGroup(groupJson);
			if (!group.name.empty() || !group.includeClipNameTokens.empty() ||
				!group.excludeClipNameTokens.empty() || !group.moveStates.empty())
			{
				settings.searchGroups.push_back(std::move(group));
			}
		}
	}

	const VansSerializedValue* clipMetadataJson = ReadArrayField(mmJson, "clip_metadata");
	if (!clipMetadataJson)
		clipMetadataJson = ReadArrayField(mmJson, "clipMetadata");
	if (clipMetadataJson)
	{
		for (const VansSerializedValue& itemJson : clipMetadataJson->arrayItems)
		{
			if (itemJson.kind != VansSerializedValue::Kind::Object)
				continue;
			MotionMatchingClipMetadata metadata = DecodeClipMetadata(itemJson);
			if (!metadata.name.empty() || !metadata.matchTokens.empty())
				settings.clipMetadata.push_back(std::move(metadata));
		}
	}

	return settings;
}

FootPlacementSettings DecodeFootPlacement(const VansSerializedValue& fpJson)
{
	FootPlacementSettings settings;
	settings.enabled = ReadBoolField(fpJson, "enabled", false);
	settings.probeOriginHeight = ReadFloatField(fpJson, "probe_origin_height", settings.probeOriginHeight);
	settings.probeLength = ReadFloatField(fpJson, "probe_length", settings.probeLength);
	settings.footHalfLength = ReadFloatField(fpJson, "foot_half_length", settings.footHalfLength);
	settings.footHalfWidth = ReadFloatField(fpJson, "foot_half_width", settings.footHalfWidth);
	settings.ankleHeight = ReadFloatField(fpJson, "ankle_height", settings.ankleHeight);
	settings.fullContactHeight = ReadFloatField(fpJson, "full_contact_height", settings.fullContactHeight);
	settings.contactFadeHeight = ReadFloatField(fpJson, "contact_fade_height", settings.contactFadeHeight);
	settings.maxStepUp = ReadFloatField(fpJson, "max_step_up", settings.maxStepUp);
	settings.maxStepDown = ReadFloatField(fpJson, "max_step_down", settings.maxStepDown);
	settings.maxSlopeDeg = ReadFloatField(fpJson, "max_slope_deg", settings.maxSlopeDeg);
	settings.pelvisMaxDrop = ReadFloatField(fpJson, "pelvis_max_drop", settings.pelvisMaxDrop);
	settings.pelvisSmoothTime = ReadFloatField(fpJson, "pelvis_smooth_time", settings.pelvisSmoothTime);
	settings.offsetSmoothTime = ReadFloatField(fpJson, "offset_smooth_time", settings.offsetSmoothTime);
	settings.normalSmoothTime = ReadFloatField(fpJson, "normal_smooth_time", settings.normalSmoothTime);
	settings.weightSmoothTime = ReadFloatField(fpJson, "weight_smooth_time", settings.weightSmoothTime);
	settings.globalWeightSmoothTime = ReadFloatField(fpJson, "global_weight_smooth_time", settings.globalWeightSmoothTime);
	settings.ikWeight = ReadFloatField(fpJson, "ik_weight", settings.ikWeight);
	settings.rotationWeight = ReadFloatField(fpJson, "rotation_weight", settings.rotationWeight);
	settings.maxLegExtensionRatio = ReadFloatField(fpJson, "max_leg_extension_ratio", settings.maxLegExtensionRatio);
	settings.poleSmoothTime = ReadFloatField(fpJson, "pole_smooth_time", settings.poleSmoothTime);
	settings.kneePoleModelWeight = ReadFloatField(fpJson, "knee_pole_model_weight", settings.kneePoleModelWeight);
	settings.kneePoleModelDir = ReadVec3Field(fpJson, "knee_pole_model_dir", settings.kneePoleModelDir);
	settings.airborneParameter = ReadSerializedStringField(fpJson, "airborne_parameter", settings.airborneParameter);
	settings.debugVisualization = ReadBoolField(fpJson, "debug_visualization", settings.debugVisualization);
	settings.collisionMask = ReadUInt32Field(fpJson, "collision_mask", settings.collisionMask);

	if (const VansSerializedValue* bonesJson = ReadObjectField(fpJson, "bones"))
	{
		settings.bones.pelvis = ReadSerializedStringField(*bonesJson, "pelvis", settings.bones.pelvis);
		settings.bones.leftHip = ReadSerializedStringField(*bonesJson, "left_hip", settings.bones.leftHip);
		settings.bones.leftKnee = ReadSerializedStringField(*bonesJson, "left_knee", settings.bones.leftKnee);
		settings.bones.leftFoot = ReadSerializedStringField(*bonesJson, "left_foot", settings.bones.leftFoot);
		settings.bones.rightHip = ReadSerializedStringField(*bonesJson, "right_hip", settings.bones.rightHip);
		settings.bones.rightKnee = ReadSerializedStringField(*bonesJson, "right_knee", settings.bones.rightKnee);
		settings.bones.rightFoot = ReadSerializedStringField(*bonesJson, "right_foot", settings.bones.rightFoot);
	}
	return settings;
}

VansSceneAnimationBoneBindingConfig DecodeBoneBinding(const VansSerializedValue& bindJson)
{
	VansSceneAnimationBoneBindingConfig config;
	config.boneName = ReadSerializedStringField(bindJson, "bone_name", "");
	config.physicsObjectName = ReadSerializedStringField(bindJson, "physics_object", "");
	config.offsetPosition = ReadVec3Field(bindJson, "offset_position", config.offsetPosition);
	config.offsetRotation = ReadVec3Field(bindJson, "offset_rotation", config.offsetRotation);
	config.offsetScale = ReadVec3Field(bindJson, "offset_scale", config.offsetScale);
	config.syncRotation = ReadBoolField(bindJson, "sync_rotation", true);
	config.syncScale = ReadBoolField(bindJson, "sync_scale", false);
	config.layerName = ReadSerializedStringField(bindJson, "layer", "Default");
	config.isTrigger = ReadBoolField(bindJson, "is_trigger", false);
	config.enabled = ReadBoolField(bindJson, "enabled", true);
	config.autoCreateNode = ReadBoolField(bindJson, "auto_create_node", false);
	config.shapeExtents = ReadVec3Field(bindJson, "shape_extents", config.shapeExtents);
	config.shapeType = ReadSerializedStringField(bindJson, "shape_type", "capsule");
	return config;
}

VansSceneRagdollComponentConfig DecodeRagdollConfig(const VansSerializedValue& ragdollJson)
{
	VansSceneRagdollComponentConfig config;
	config.profile = ReadAssetReferenceField(ragdollJson, "profile", "");
	config.driveMode = ReadSerializedStringField(ragdollJson, "drive_mode", "animation");
	config.blendWeight = ReadFloatField(ragdollJson, "blend_weight", 0.0f);
	return config;
}

const VansSerializedValue* FindAuthoringComponent(const VansSerializedValue& entity, const char* type)
{
	const VansSerializedValue* components = ReadArrayField(entity, "components");
	if (!components)
		return nullptr;

	for (const VansSerializedValue& component : components->arrayItems)
	{
		if (ReadSerializedStringField(component, "type") == type)
			return &component;
	}
	return nullptr;
}
}

std::optional<VansSceneAnimationComponentConfig>
VansSceneAnimationComponentReader::ReadFromComponents(const VansSerializedValue& components)
{
	const VansSerializedValue* animation = ReadObjectField(components, "animation");
	if (!animation)
		return std::nullopt;
	return ReadAnimation(*animation);
}

std::optional<VansSceneAnimationComponentConfig>
VansSceneAnimationComponentReader::ReadFromAuthoringEntity(const VansSerializedValue& entity)
{
	const VansSerializedValue* animationComponent = FindAuthoringComponent(entity, "Animation");
	if (!animationComponent)
		return std::nullopt;
	return ReadAuthoringAnimationComponent(*animationComponent);
}

VansSceneAnimationComponentConfig VansSceneAnimationComponentReader::ReadAuthoringAnimationComponent(
	const VansSerializedValue& animationComponent)
{
	VansSceneAnimationComponentConfig config;
	if (const VansSerializedValue* data = ReadObjectField(animationComponent, "data"))
		config = ReadAnimation(*data);
	else
		config.valid = true;

	config.enabled = ReadSerializedBoolField(animationComponent, "enabled", true);
	return config;
}

VansSceneAnimationComponentConfig VansSceneAnimationComponentReader::ReadAnimation(
	const VansSerializedValue& animationNode)
{
	VansSceneAnimationComponentConfig config;
	if (animationNode.kind != VansSerializedValue::Kind::Object)
		return config;

	config.valid = true;
	config.enabled = ReadBoolField(animationNode, "enabled", true);
	config.meshGroup = ReadSerializedStringField(animationNode, "mesh_group", "");
	config.animator = ReadAssetReferenceField(animationNode, "animator", "");
	config.externClips = ReadSerializedStringField(animationNode, "extern_clips", "");
	config.rootMotion = ReadBoolField(animationNode, "root_motion", false);
	config.rootBone = ReadSerializedStringField(animationNode, "root_bone", "");
	config.name = ReadSerializedStringField(animationNode, "name", "");

	if (const VansSerializedValue* motionMatching = ReadObjectField(animationNode, "motion_matching"))
		config.motionMatching = DecodeMotionMatching(*motionMatching);
	if (const VansSerializedValue* footPlacement = ReadObjectField(animationNode, "foot_placement"))
		config.footPlacement = DecodeFootPlacement(*footPlacement);
	if (const VansSerializedValue* boneBindings = ReadArrayField(animationNode, "bone_bindings"))
	{
		config.boneBindings.reserve(boneBindings->arrayItems.size());
		for (const VansSerializedValue& binding : boneBindings->arrayItems)
			if (binding.kind == VansSerializedValue::Kind::Object)
				config.boneBindings.push_back(DecodeBoneBinding(binding));
	}
	if (const VansSerializedValue* ragdoll = ReadObjectField(animationNode, "ragdoll"))
		config.ragdoll = DecodeRagdollConfig(*ragdoll);

	return config;
}

VansSceneRagdollComponentConfig VansSceneAnimationComponentReader::ReadRagdoll(
	const VansSerializedValue& ragdollNode)
{
	if (ragdollNode.kind != VansSerializedValue::Kind::Object)
		return {};
	return DecodeRagdollConfig(ragdollNode);
}
}
