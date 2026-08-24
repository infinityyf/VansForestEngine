#include "VansSceneAnimationComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cstdint>

namespace Vans
{
namespace
{
using VansGraphics::MotionMatchingDatabase;
using VansGraphics::MotionMatchingDatabaseClip;
using VansGraphics::MotionMatchingSelectorRow;
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

MotionMatchingDatabaseClip DecodeMotionDatabaseClip(const VansSerializedValue& clipJson, const std::string& databasePhase)
{
	MotionMatchingDatabaseClip clip;
	clip.phase = databasePhase.empty() ? clip.phase : databasePhase;
	if (clipJson.kind == VansSerializedValue::Kind::String)
	{
		clip.name = clipJson.stringValue;
		return clip;
	}
	if (clipJson.kind != VansSerializedValue::Kind::Object)
		return clip;
	clip.name = ReadSerializedStringField(clipJson, "name", "");
	clip.loop = ReadBoolField(clipJson, "loop", clip.loop);
	clip.phase = ReadSerializedStringField(clipJson, "phase", clip.phase);
	clip.sourceMoveState = ReadIntField(clipJson, "source_move_state", clip.sourceMoveState);
	clip.targetMoveState = ReadIntField(clipJson, "target_move_state", clip.targetMoveState);
	clip.sourceDirectionBucket = ReadIntField(
		clipJson, "source_direction_bucket", clip.sourceDirectionBucket);
	clip.directionBucket = ReadIntField(clipJson, "direction_bucket", clip.directionBucket);
	clip.turnDirectionSign = ReadIntField(clipJson, "turn_direction_sign", clip.turnDirectionSign);
	clip.turnBucketDelta = ReadIntField(clipJson, "turn_bucket_delta", clip.turnBucketDelta);
	clip.samplingStart = ReadFloatField(clipJson, "sampling_start", clip.samplingStart);
	clip.samplingEnd = ReadFloatField(clipJson, "sampling_end", clip.samplingEnd);
	return clip;
}

MotionMatchingDatabase DecodeMotionDatabase(const VansSerializedValue& databaseJson)
{
	MotionMatchingDatabase database;
	database.name = ReadSerializedStringField(databaseJson, "name", "");
	database.schema = ReadSerializedStringField(databaseJson, "schema", database.schema);
	database.normalizationSet = ReadSerializedStringField(databaseJson, "normalization_set", database.normalizationSet);
	database.stance = ReadSerializedStringField(databaseJson, "stance", database.stance);
	database.phase = ReadSerializedStringField(databaseJson, "phase", database.phase);
	database.enabled = ReadBoolField(databaseJson, "enabled", database.enabled);
	ReadIntArray(databaseJson, "move_states", database.moveStates);
	ReadStringArray(databaseJson, "include_tokens", database.includeTokens);
	ReadStringArray(databaseJson, "exclude_tokens", database.excludeTokens);
	if (const VansSerializedValue* clipsJson = ReadArrayField(databaseJson, "clips"))
	{
		for (const VansSerializedValue& clipJson : clipsJson->arrayItems)
		{
			MotionMatchingDatabaseClip clip = DecodeMotionDatabaseClip(clipJson, database.phase);
			if (!clip.name.empty())
				database.clips.push_back(std::move(clip));
		}
	}
	return database;
}

MotionMatchingSelectorRow DecodeMotionSelectorRow(const VansSerializedValue& rowJson)
{
	MotionMatchingSelectorRow row;
	row.name = ReadSerializedStringField(rowJson, "name", "");
	row.stance = ReadSerializedStringField(rowJson, "stance", row.stance);
	row.phase = ReadSerializedStringField(rowJson, "phase", row.phase);
	ReadIntArray(rowJson, "move_states", row.moveStates);
	ReadStringArray(rowJson, "databases", row.databases);
	return row;
}

bool DecodeMotionSearchGroup(const VansSerializedValue& groupJson,
                             MotionMatchingDatabase& database,
                             MotionMatchingSelectorRow& row)
{
	if (groupJson.kind != VansSerializedValue::Kind::Object)
		return false;

	database.name = ReadSerializedStringField(groupJson, "name", "");
	if (database.name.empty())
		return false;

	database.schema = ReadSerializedStringField(groupJson, "schema", database.schema);
	database.normalizationSet = ReadSerializedStringField(groupJson, "normalization_set", database.normalizationSet);
	database.stance = ReadSerializedStringField(groupJson, "stance", "Any");
	database.phase = ReadSerializedStringField(groupJson, "phase", "Any");
	database.enabled = ReadBoolField(groupJson, "enabled", database.enabled);
	ReadIntArray(groupJson, "move_states", database.moveStates);
	ReadStringArray(groupJson, "include", database.includeTokens);
	ReadStringArray(groupJson, "exclude", database.excludeTokens);

	row.name = database.name;
	row.stance = database.stance;
	row.phase = database.phase;
	row.moveStates = database.moveStates;
	row.databases.push_back(database.name);
	return true;
}

VansSceneAnimationRetargetConfig DecodeRetarget(const VansSerializedValue& retargetJson)
{
	VansSceneAnimationRetargetConfig config;
	config.enabled = ReadBoolField(retargetJson, "enabled", false);
	config.profile = ReadSerializedStringField(retargetJson, "profile", "");
	config.sourceModel = ReadAssetReferenceField(retargetJson, "source_model", "");
	config.sourceAnimator = ReadAssetReferenceField(retargetJson, "source_animator", "");
	config.debugDraw = ReadBoolField(retargetJson, "debug_draw", false);
	return config;
}

bool DecodeMotionMatching(const VansSerializedValue& mmJson, MotionMatchingSettings& settings)
{
	settings = {};
	settings.enabled = ReadBoolField(mmJson, "enabled", false);
	settings.autoBuild = ReadBoolField(mmJson, "auto_build", true);
	if (const VansSerializedValue* motionModel = ReadObjectField(mmJson, "motion_model"))
	{
		const std::string driveMode = ReadSerializedStringField(*motionModel, "drive_mode", "hybrid");
		if (driveMode == "capsule")
			settings.motionModel.driveMode = VansLocomotionDriveMode::Capsule;
		else if (driveMode == "root_motion")
			settings.motionModel.driveMode = VansLocomotionDriveMode::RootMotion;
		else if (driveMode == "hybrid")
			settings.motionModel.driveMode = VansLocomotionDriveMode::Hybrid;
		else
			return false;
		settings.motionModel.velocityHalfLife = ReadFloatField(
			*motionModel, "velocity_half_life", settings.motionModel.velocityHalfLife);
		settings.motionModel.facingHalfLife = ReadFloatField(
			*motionModel, "facing_half_life", settings.motionModel.facingHalfLife);
		settings.motionModel.facingVelocityHalfLife = ReadFloatField(
			*motionModel, "facing_velocity_half_life", settings.motionModel.facingVelocityHalfLife);
		settings.motionModel.movementReferenceYawRateHalfLife = ReadFloatField(
			*motionModel,
			"movement_reference_yaw_rate_half_life",
			settings.motionModel.movementReferenceYawRateHalfLife);
		settings.motionModel.maxFacingYawRate = ReadFloatField(
			*motionModel, "max_facing_yaw_rate", settings.motionModel.maxFacingYawRate);
		settings.motionModel.maxAcceleration = ReadFloatField(
			*motionModel, "max_acceleration", settings.motionModel.maxAcceleration);
		settings.motionModel.maxDeceleration = ReadFloatField(
			*motionModel, "max_deceleration", settings.motionModel.maxDeceleration);
		settings.motionModel.actualVelocityFeedbackHalfLife = ReadFloatField(
			*motionModel,
			"actual_velocity_feedback_half_life",
			settings.motionModel.actualVelocityFeedbackHalfLife);
		settings.motionModel.predictionStep = ReadFloatField(
			*motionModel, "prediction_step", settings.motionModel.predictionStep);
		settings.motionModel.rootMotionToWorldScale = ReadFloatField(
			*motionModel, "root_motion_to_world_scale", settings.motionModel.rootMotionToWorldScale);
		settings.motionModel.loopRootMotionWeight = ReadFloatField(
			*motionModel, "loop_root_motion_weight", settings.motionModel.loopRootMotionWeight);
		settings.motionModel.transitionRootMotionWeight = ReadFloatField(
			*motionModel, "transition_root_motion_weight", settings.motionModel.transitionRootMotionWeight);
		settings.motionModel.rootRotationWeight = ReadFloatField(
			*motionModel, "root_rotation_weight", settings.motionModel.rootRotationWeight);
	}
	settings.sampleRate = ReadFloatField(mmJson, "sample_rate", 30.0f);
	settings.nonLoopSamplingEndMargin = ReadFloatField(
		mmJson, "non_loop_sampling_end_margin", settings.nonLoopSamplingEndMargin);
	settings.searchThrottle = ReadFloatField(mmJson, "search_throttle", 0.15f);
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
	if (const VansSerializedValue* steering = ReadObjectField(mmJson, "root_motion_steering"))
	{
		settings.steering.enabled = ReadBoolField(
			*steering, "enabled", settings.steering.enabled);
		settings.steering.predictionTime = ReadFloatField(
			*steering, "prediction_time", settings.steering.predictionTime);
		settings.steering.correctionHalfLife = ReadFloatField(
			*steering, "correction_half_life", settings.steering.correctionHalfLife);
		settings.steering.maxCorrectionAngleDegrees = ReadFloatField(
			*steering,
			"max_correction_angle_degrees",
			settings.steering.maxCorrectionAngleDegrees);
		settings.steering.maxCorrectionYawRateDegreesPerSecond = ReadFloatField(
			*steering,
			"max_correction_yaw_rate_degrees_per_second",
			settings.steering.maxCorrectionYawRateDegreesPerSecond);
		settings.steering.minMovementSpeed = ReadFloatField(
			*steering, "min_movement_speed", settings.steering.minMovementSpeed);
	}
	if (const VansSerializedValue* reconciliation = ReadObjectField(mmJson, "root_motion_reconciliation"))
	{
		settings.rootMotionReconciliation.enabled = ReadBoolField(
			*reconciliation, "enabled", settings.rootMotionReconciliation.enabled);
		settings.rootMotionReconciliation.linearVelocityHalfLife = ReadFloatField(
			*reconciliation, "linear_velocity_half_life",
			settings.rootMotionReconciliation.linearVelocityHalfLife);
		settings.rootMotionReconciliation.angularVelocityHalfLife = ReadFloatField(
			*reconciliation, "angular_velocity_half_life",
			settings.rootMotionReconciliation.angularVelocityHalfLife);
		settings.rootMotionReconciliation.maxDuration = ReadFloatField(
			*reconciliation, "max_duration", settings.rootMotionReconciliation.maxDuration);
		settings.rootMotionReconciliation.maxLinearVelocityCorrection = ReadFloatField(
			*reconciliation, "max_linear_velocity_correction",
			settings.rootMotionReconciliation.maxLinearVelocityCorrection);
		settings.rootMotionReconciliation.maxAngularVelocityCorrectionDegreesPerSecond = ReadFloatField(
			*reconciliation, "max_angular_velocity_correction_degrees_per_second",
			settings.rootMotionReconciliation.maxAngularVelocityCorrectionDegreesPerSecond);
	}
	settings.facingTurnEnterThresholdDegrees = ReadFloatField(
		mmJson, "facing_turn_enter_threshold_degrees", settings.facingTurnEnterThresholdDegrees);
	settings.facingTurnExitThresholdDegrees = ReadFloatField(
		mmJson, "facing_turn_exit_threshold_degrees", settings.facingTurnExitThresholdDegrees);
	settings.facingTurnExitYawRateDegreesPerSecond = ReadFloatField(
		mmJson,
		"facing_turn_exit_yaw_rate_degrees_per_second",
		settings.facingTurnExitYawRateDegreesPerSecond);
	settings.inertializationHalfLife = ReadFloatField(
		mmJson,
		"inertialization_half_life",
		settings.inertializationHalfLife);
	settings.inertializationMaxDuration = ReadFloatField(
		mmJson,
		"inertialization_max_duration",
		settings.inertializationMaxDuration);
	settings.trajectoryWeight = ReadFloatField(mmJson, "trajectory_weight", 1.0f);
	settings.trajectoryPositionWeight = ReadFloatField(
		mmJson, "trajectory_position_weight", settings.trajectoryPositionWeight);
	settings.trajectoryVelocityWeight = ReadFloatField(
		mmJson, "trajectory_velocity_weight", settings.trajectoryVelocityWeight);
	settings.trajectoryFacingWeight = ReadFloatField(
		mmJson, "trajectory_facing_weight", settings.trajectoryFacingWeight);
	settings.poseWeight = ReadFloatField(mmJson, "pose_weight", 0.7f);
	settings.contactWeight = ReadFloatField(mmJson, "contact_weight", settings.contactWeight);
	settings.pivotEnterAngleDegrees = ReadFloatField(
		mmJson, "pivot_enter_angle_degrees", settings.pivotEnterAngleDegrees);
	settings.pivotExitAngleDegrees = ReadFloatField(
		mmJson, "pivot_exit_angle_degrees", settings.pivotExitAngleDegrees);
	settings.pivotMinSpeed = ReadFloatField(
		mmJson, "pivot_min_speed", settings.pivotMinSpeed);
	settings.pivotPredictionLeadTime = ReadFloatField(
		mmJson, "pivot_prediction_lead_time", settings.pivotPredictionLeadTime);
	settings.pivotUrgentPredictionTime = ReadFloatField(
		mmJson, "pivot_urgent_prediction_time", settings.pivotUrgentPredictionTime);
	settings.pivotMinimumPlaybackTime = ReadFloatField(
		mmJson, "pivot_minimum_playback_time", settings.pivotMinimumPlaybackTime);
	settings.urgentDirectionChangeDegrees = ReadFloatField(
		mmJson, "urgent_direction_change_degrees", settings.urgentDirectionChangeDegrees);
	settings.directionBucketTolerance = ReadIntField(
		mmJson, "direction_bucket_tolerance", settings.directionBucketTolerance);
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

	if (const VansSerializedValue* paramsJson = ReadObjectField(mmJson, "parameters"))
	{
		settings.parameters.enabled = ReadSerializedStringField(*paramsJson, "enabled", settings.parameters.enabled);
		settings.parameters.speed = ReadSerializedStringField(*paramsJson, "speed", settings.parameters.speed);
		settings.parameters.direction = ReadSerializedStringField(*paramsJson, "direction", settings.parameters.direction);
		settings.parameters.crouching = ReadSerializedStringField(*paramsJson, "crouching", settings.parameters.crouching);
		settings.parameters.airborne = ReadSerializedStringField(*paramsJson, "airborne", settings.parameters.airborne);
		settings.parameters.moveState = ReadSerializedStringField(*paramsJson, "move_state", settings.parameters.moveState);
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

	if (const VansSerializedValue* contactsField = FindObjectField(mmJson, "contacts"))
	{
		if (contactsField->kind != VansSerializedValue::Kind::Object)
			return false;
		const VansSerializedValue* contactsJson = contactsField;
		settings.contactProvider = ReadSerializedStringField(*contactsJson, "provider", "");
		const VansSerializedValue* channelsField = FindObjectField(*contactsJson, "channels");
		if (settings.contactProvider.empty() || !channelsField ||
			channelsField->kind != VansSerializedValue::Kind::Array ||
			channelsField->arrayItems.empty())
		{
			return false;
		}
		for (const VansSerializedValue& channelJson : channelsField->arrayItems)
		{
			if (channelJson.kind != VansSerializedValue::Kind::Object)
				return false;
			VansGraphics::MotionMatchingContactChannel channel;
			channel.id = ReadSerializedStringField(channelJson, "id", "");
			const std::string source = ReadSerializedStringField(channelJson, "source", "");
			if (channel.id.empty())
				return false;
			if (source == "leftFoot")
				channel.source = VansGraphics::MotionMatchingContactSource::LeftFoot;
			else if (source == "rightFoot")
				channel.source = VansGraphics::MotionMatchingContactSource::RightFoot;
			else
				return false;
			settings.contactChannels.push_back(std::move(channel));
		}
	}

	if (const VansSerializedValue* schemaJson = ReadObjectField(mmJson, "schema"))
	{
		settings.trajectoryWeight = ReadFloatField(*schemaJson, "trajectory_weight", settings.trajectoryWeight);
		settings.trajectoryPositionWeight = ReadFloatField(
			*schemaJson, "position_weight", settings.trajectoryPositionWeight);
		settings.trajectoryVelocityWeight = ReadFloatField(
			*schemaJson, "velocity_weight", settings.trajectoryVelocityWeight);
		settings.trajectoryFacingWeight = ReadFloatField(
			*schemaJson, "facing_weight", settings.trajectoryFacingWeight);
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

	if (const VansSerializedValue* statesJson = ReadObjectField(mmJson, "states"))
	{
		settings.states.idleState = ReadIntField(*statesJson, "idle_state", settings.states.idleState);
		settings.states.crouchState = ReadIntField(*statesJson, "crouch_state", settings.states.crouchState);
		settings.states.idleSpeedThreshold = ReadFloatField(
			*statesJson,
			"idle_speed_threshold",
			settings.states.idleSpeedThreshold);
		ReadReplacingIntArray(*statesJson, "moving_states", settings.states.movingStates);
		ReadReplacingIntArray(*statesJson, "pace_transition_states", settings.states.paceTransitionStates);
		ReadReplacingIntArray(*statesJson, "stance_states", settings.states.stanceStates);
	}

	if (const VansSerializedValue* databasesJson = ReadArrayField(mmJson, "databases"))
	{
		for (const VansSerializedValue& databaseJson : databasesJson->arrayItems)
		{
			if (databaseJson.kind != VansSerializedValue::Kind::Object)
				continue;
			MotionMatchingDatabase database = DecodeMotionDatabase(databaseJson);
			if (!database.name.empty() && (!database.clips.empty() || !database.includeTokens.empty()))
				settings.databases.push_back(std::move(database));
		}
	}

	ReadStringArray(mmJson, "include_clip_tokens", settings.includeClipTokens);
	ReadStringArray(mmJson, "exclude_clip_tokens", settings.excludeClipTokens);

	const VansSerializedValue* selectorJson = ReadArrayField(mmJson, "selector");
	if (selectorJson)
	{
		for (const VansSerializedValue& rowJson : selectorJson->arrayItems)
		{
			if (rowJson.kind != VansSerializedValue::Kind::Object)
				continue;
			MotionMatchingSelectorRow row = DecodeMotionSelectorRow(rowJson);
			if (!row.databases.empty())
				settings.selectorRows.push_back(std::move(row));
		}
	}

	if (const VansSerializedValue* searchGroupsJson = ReadArrayField(mmJson, "search_groups"))
	{
		for (const VansSerializedValue& groupJson : searchGroupsJson->arrayItems)
		{
			MotionMatchingDatabase database;
			MotionMatchingSelectorRow row;
			if (DecodeMotionSearchGroup(groupJson, database, row))
			{
				settings.databases.push_back(std::move(database));
				settings.selectorRows.push_back(std::move(row));
			}
		}
	}

	return true;
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
	config.rig = ReadAssetReferenceField(animationNode, "rig", "");
	config.externClips = ReadSerializedStringField(animationNode, "extern_clips", "");
	config.rootMotion = ReadBoolField(animationNode, "root_motion", false);
	config.autoPlay = ReadBoolField(animationNode, "auto_play", true);
	config.loop = ReadBoolField(animationNode, "loop", true);
	config.rootBone = ReadSerializedStringField(animationNode, "root_bone", "");
	config.name = ReadSerializedStringField(animationNode, "name", "");

	if (const VansSerializedValue* motionMatchingField = FindObjectField(animationNode, "motion_matching"))
	{
		if (motionMatchingField->kind != VansSerializedValue::Kind::Object)
		{
			config.valid = false;
			return config;
		}
		MotionMatchingSettings settings;
		if (!DecodeMotionMatching(*motionMatchingField, settings))
		{
			config.valid = false;
			return config;
		}
		config.motionMatching = std::move(settings);
	}
	if (const VansSerializedValue* retarget = ReadObjectField(animationNode, "retarget"))
		config.retarget = DecodeRetarget(*retarget);
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
