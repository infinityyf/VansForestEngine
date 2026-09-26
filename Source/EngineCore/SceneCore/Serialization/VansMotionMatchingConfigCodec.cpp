#include "VansMotionMatchingConfigCodec.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <utility>
#include <vector>

namespace Vans
{
namespace
{
using VansGraphics::MotionMatchingContactChannel;
using VansGraphics::MotionMatchingContactSource;
using VansGraphics::MotionMatchingDatabase;
using VansGraphics::MotionMatchingDatabaseClip;
using VansGraphics::MotionMatchingSelectorRow;
using VansGraphics::MotionMatchingSettings;

const VansSerializedValue* ReadObject(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
}

const VansSerializedValue* ReadArray(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Array ? field : nullptr;
}

float ReadFloat(const VansSerializedValue& object, const char* key, float fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && (field->kind == VansSerializedValue::Kind::Float ||
		field->kind == VansSerializedValue::Kind::Int)
		? static_cast<float>(ReadSerializedNumber(*field))
		: fallback;
}

int ReadInt(const VansSerializedValue& object, const char* key, int fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Int
		? static_cast<int>(field->intValue)
		: fallback;
}

bool ReadBool(const VansSerializedValue& object, const char* key, bool fallback)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Bool ? field->boolValue : fallback;
}

glm::vec3 ReadVec3(const VansSerializedValue& object, const char* key, const glm::vec3& fallback)
{
	const VansSerializedValue* value = FindObjectField(object, key);
	if (!value || value->kind != VansSerializedValue::Kind::Array || value->arrayItems.size() != 3)
		return fallback;
	for (const VansSerializedValue& item : value->arrayItems)
		if (item.kind != VansSerializedValue::Kind::Float && item.kind != VansSerializedValue::Kind::Int)
			return fallback;
	return glm::vec3(
		static_cast<float>(ReadSerializedNumber(value->arrayItems[0])),
		static_cast<float>(ReadSerializedNumber(value->arrayItems[1])),
		static_cast<float>(ReadSerializedNumber(value->arrayItems[2])));
}

void ReadStrings(const VansSerializedValue& object, const char* key, std::vector<std::string>& values)
{
	const VansSerializedValue* array = ReadArray(object, key);
	if (!array) return;
	values.clear();
	for (const VansSerializedValue& item : array->arrayItems)
		if (item.kind == VansSerializedValue::Kind::String)
			values.push_back(item.stringValue);
}

void ReadInts(const VansSerializedValue& object, const char* key, std::vector<int>& values)
{
	const VansSerializedValue* array = ReadArray(object, key);
	if (!array) return;
	values.clear();
	for (const VansSerializedValue& item : array->arrayItems)
		if (item.kind == VansSerializedValue::Kind::Int)
			values.push_back(static_cast<int>(item.intValue));
}

VansSerializedValue EncodeInts(const std::vector<int>& values)
{
	std::vector<VansSerializedValue> items;
	items.reserve(values.size());
	for (int value : values) items.push_back(VansSerializedValue::Int(value));
	return VansSerializedValue::Array(std::move(items));
}

VansSerializedValue EncodeStrings(const std::vector<std::string>& values)
{
	std::vector<VansSerializedValue> items;
	items.reserve(values.size());
	for (const std::string& value : values) items.push_back(VansSerializedValue::String(value));
	return VansSerializedValue::Array(std::move(items));
}

VansSerializedValue EncodeVec3(const glm::vec3& value)
{
	return VansSerializedValue::Array({
		VansSerializedValue::Float(value.x),
		VansSerializedValue::Float(value.y),
		VansSerializedValue::Float(value.z)
	});
}

MotionMatchingDatabaseClip DecodeClip(
	const VansSerializedValue& value,
	const std::string& databasePhase)
{
	MotionMatchingDatabaseClip clip;
	clip.phase = databasePhase.empty() ? clip.phase : databasePhase;
	if (value.kind == VansSerializedValue::Kind::String)
	{
		clip.name = value.stringValue;
		return clip;
	}
	if (value.kind != VansSerializedValue::Kind::Object) return clip;
	clip.name = ReadSerializedStringField(value, "name");
	clip.loop = ReadBool(value, "loop", clip.loop);
	clip.phase = ReadSerializedStringField(value, "phase", clip.phase);
	clip.sourceMoveState = ReadInt(value, "source_move_state", clip.sourceMoveState);
	clip.targetMoveState = ReadInt(value, "target_move_state", clip.targetMoveState);
	clip.sourceDirectionBucket = ReadInt(
		value, "source_direction_bucket", clip.sourceDirectionBucket);
	clip.directionBucket = ReadInt(value, "direction_bucket", clip.directionBucket);
	clip.turnDirectionSign = ReadInt(value, "turn_direction_sign", clip.turnDirectionSign);
	clip.turnBucketDelta = ReadInt(value, "turn_bucket_delta", clip.turnBucketDelta);
	clip.samplingStart = ReadFloat(value, "sampling_start", clip.samplingStart);
	clip.samplingEnd = ReadFloat(value, "sampling_end", clip.samplingEnd);
	return clip;
}

MotionMatchingDatabase DecodeDatabase(const VansSerializedValue& value)
{
	MotionMatchingDatabase database;
	database.name = ReadSerializedStringField(value, "name");
	database.schema = ReadSerializedStringField(value, "schema", database.schema);
	database.normalizationSet = ReadSerializedStringField(
		value, "normalization_set", database.normalizationSet);
	database.stance = ReadSerializedStringField(value, "stance", database.stance);
	database.phase = ReadSerializedStringField(value, "phase", database.phase);
	database.enabled = ReadBool(value, "enabled", database.enabled);
	ReadInts(value, "move_states", database.moveStates);
	ReadStrings(value, "include_tokens", database.includeTokens);
	ReadStrings(value, "exclude_tokens", database.excludeTokens);
	if (const VansSerializedValue* clips = ReadArray(value, "clips"))
		for (const VansSerializedValue& clipValue : clips->arrayItems)
		{
			MotionMatchingDatabaseClip clip = DecodeClip(clipValue, database.phase);
			if (!clip.name.empty()) database.clips.push_back(std::move(clip));
		}
	return database;
}

MotionMatchingSelectorRow DecodeSelector(const VansSerializedValue& value)
{
	MotionMatchingSelectorRow row;
	row.name = ReadSerializedStringField(value, "name");
	row.stance = ReadSerializedStringField(value, "stance", row.stance);
	row.phase = ReadSerializedStringField(value, "phase", row.phase);
	ReadInts(value, "move_states", row.moveStates);
	ReadStrings(value, "databases", row.databases);
	return row;
}

VansSerializedValue EncodeClip(const MotionMatchingDatabaseClip& clip)
{
	std::vector<std::pair<std::string, VansSerializedValue>> fields = {
		{ "name", VansSerializedValue::String(clip.name) },
		{ "phase", VansSerializedValue::String(clip.phase) },
		{ "loop", VansSerializedValue::Bool(clip.loop) },
		{ "source_move_state", VansSerializedValue::Int(clip.sourceMoveState) },
		{ "target_move_state", VansSerializedValue::Int(clip.targetMoveState) },
		{ "direction_bucket", VansSerializedValue::Int(clip.directionBucket) },
		{ "turn_direction_sign", VansSerializedValue::Int(clip.turnDirectionSign) },
		{ "turn_bucket_delta", VansSerializedValue::Int(clip.turnBucketDelta) }
	};
	if (clip.sourceDirectionBucket != -1)
		fields.emplace_back("source_direction_bucket", VansSerializedValue::Int(clip.sourceDirectionBucket));
	if (clip.samplingStart != 0.0f)
		fields.emplace_back("sampling_start", VansSerializedValue::Float(clip.samplingStart));
	if (clip.samplingEnd >= 0.0f)
		fields.emplace_back("sampling_end", VansSerializedValue::Float(clip.samplingEnd));
	return VansSerializedValue::Object(std::move(fields));
}

VansSerializedValue EncodeDatabase(const MotionMatchingDatabase& database)
{
	std::vector<std::pair<std::string, VansSerializedValue>> fields = {
		{ "name", VansSerializedValue::String(database.name) },
		{ "schema", VansSerializedValue::String(database.schema) },
		{ "normalization_set", VansSerializedValue::String(database.normalizationSet) },
		{ "stance", VansSerializedValue::String(database.stance) },
		{ "phase", VansSerializedValue::String(database.phase) },
		{ "move_states", EncodeInts(database.moveStates) }
	};
	if (!database.enabled)
		fields.emplace_back("enabled", VansSerializedValue::Bool(false));
	if (!database.includeTokens.empty())
		fields.emplace_back("include_tokens", EncodeStrings(database.includeTokens));
	if (!database.excludeTokens.empty())
		fields.emplace_back("exclude_tokens", EncodeStrings(database.excludeTokens));
	if (!database.clips.empty())
	{
		std::vector<VansSerializedValue> clips;
		clips.reserve(database.clips.size());
		for (const MotionMatchingDatabaseClip& clip : database.clips)
			clips.push_back(EncodeClip(clip));
		fields.emplace_back("clips", VansSerializedValue::Array(std::move(clips)));
	}
	return VansSerializedValue::Object(std::move(fields));
}

VansSerializedValue EncodeSelector(const MotionMatchingSelectorRow& row)
{
	return VansSerializedValue::Object({
		{ "name", VansSerializedValue::String(row.name) },
		{ "stance", VansSerializedValue::String(row.stance) },
		{ "phase", VansSerializedValue::String(row.phase) },
		{ "move_states", EncodeInts(row.moveStates) },
		{ "databases", EncodeStrings(row.databases) }
	});
}

const char* DriveModeName(VansLocomotionDriveMode mode)
{
	switch (mode)
	{
	case VansLocomotionDriveMode::Capsule: return "capsule";
	case VansLocomotionDriveMode::RootMotion: return "root_motion";
	case VansLocomotionDriveMode::Hybrid: return "hybrid";
	}
	return nullptr;
}

VansSerializedValue EncodeMotionModel(const VansCharacterMotionSettings& settings)
{
	return VansSerializedValue::Object({
		{ "drive_mode", VansSerializedValue::String(DriveModeName(settings.driveMode)) },
		{ "velocity_half_life", VansSerializedValue::Float(settings.velocityHalfLife) },
		{ "facing_half_life", VansSerializedValue::Float(settings.facingHalfLife) },
		{ "facing_velocity_half_life", VansSerializedValue::Float(settings.facingVelocityHalfLife) },
		{ "movement_reference_yaw_rate_half_life", VansSerializedValue::Float(settings.movementReferenceYawRateHalfLife) },
		{ "max_facing_yaw_rate", VansSerializedValue::Float(settings.maxFacingYawRate) },
		{ "max_acceleration", VansSerializedValue::Float(settings.maxAcceleration) },
		{ "max_deceleration", VansSerializedValue::Float(settings.maxDeceleration) },
		{ "actual_velocity_feedback_half_life", VansSerializedValue::Float(settings.actualVelocityFeedbackHalfLife) },
		{ "prediction_step", VansSerializedValue::Float(settings.predictionStep) },
		{ "root_motion_to_world_scale", VansSerializedValue::Float(settings.rootMotionToWorldScale) },
		{ "loop_root_motion_weight", VansSerializedValue::Float(settings.loopRootMotionWeight) },
		{ "transition_root_motion_weight", VansSerializedValue::Float(settings.transitionRootMotionWeight) },
		{ "root_rotation_weight", VansSerializedValue::Float(settings.rootRotationWeight) }
	});
}
}

bool VansMotionMatchingConfigCodec::DecodeMotionModel(
	const VansSerializedValue& value,
	VansCharacterMotionSettings& settings,
	std::string& error)
{
	error.clear();
	if (value.kind != VansSerializedValue::Kind::Object)
	{
		error = "Motion model must be an object";
		return false;
	}
	const std::string driveMode = ReadSerializedStringField(value, "drive_mode", "hybrid");
	if (driveMode == "capsule") settings.driveMode = VansLocomotionDriveMode::Capsule;
	else if (driveMode == "root_motion") settings.driveMode = VansLocomotionDriveMode::RootMotion;
	else if (driveMode == "hybrid") settings.driveMode = VansLocomotionDriveMode::Hybrid;
	else
	{
		error = "Unknown motion model drive_mode '" + driveMode + "'";
		return false;
	}
	settings.velocityHalfLife = ReadFloat(value, "velocity_half_life", settings.velocityHalfLife);
	settings.facingHalfLife = ReadFloat(value, "facing_half_life", settings.facingHalfLife);
	settings.facingVelocityHalfLife = ReadFloat(
		value, "facing_velocity_half_life", settings.facingVelocityHalfLife);
	settings.movementReferenceYawRateHalfLife = ReadFloat(
		value, "movement_reference_yaw_rate_half_life", settings.movementReferenceYawRateHalfLife);
	settings.maxFacingYawRate = ReadFloat(value, "max_facing_yaw_rate", settings.maxFacingYawRate);
	settings.maxAcceleration = ReadFloat(value, "max_acceleration", settings.maxAcceleration);
	settings.maxDeceleration = ReadFloat(value, "max_deceleration", settings.maxDeceleration);
	settings.actualVelocityFeedbackHalfLife = ReadFloat(
		value, "actual_velocity_feedback_half_life", settings.actualVelocityFeedbackHalfLife);
	settings.predictionStep = ReadFloat(value, "prediction_step", settings.predictionStep);
	settings.rootMotionToWorldScale = ReadFloat(
		value, "root_motion_to_world_scale", settings.rootMotionToWorldScale);
	settings.loopRootMotionWeight = ReadFloat(
		value, "loop_root_motion_weight", settings.loopRootMotionWeight);
	settings.transitionRootMotionWeight = ReadFloat(
		value, "transition_root_motion_weight", settings.transitionRootMotionWeight);
	settings.rootRotationWeight = ReadFloat(
		value, "root_rotation_weight", settings.rootRotationWeight);
	return true;
}

bool VansMotionMatchingConfigCodec::Decode(
	const VansSerializedValue& value,
	MotionMatchingSettings& settings,
	std::string& error)
{
	error.clear();
	if (value.kind != VansSerializedValue::Kind::Object)
	{
		error = "Motion Matching configuration must be an object";
		return false;
	}
	for (const char* obsolete : {
		"search_groups", "trajectory_weight", "trajectory_position_weight",
		"trajectory_velocity_weight", "trajectory_facing_weight", "pose_weight" })
	{
		if (FindObjectField(value, obsolete))
		{
			error = std::string("Motion Matching field '") + obsolete +
				"' is not part of the current schema";
			return false;
		}
	}

	settings = {};
	settings.enabled = ReadBool(value, "enabled", settings.enabled);
	settings.autoBuild = ReadBool(value, "auto_build", settings.autoBuild);
	if (const VansSerializedValue* motionModel = ReadObject(value, "motion_model"))
		if (!DecodeMotionModel(*motionModel, settings.motionModel, error)) return false;
	settings.sampleRate = ReadFloat(value, "sample_rate", settings.sampleRate);
	settings.nonLoopSamplingEndMargin = ReadFloat(
		value, "non_loop_sampling_end_margin", settings.nonLoopSamplingEndMargin);
	settings.searchThrottle = ReadFloat(value, "search_throttle", settings.searchThrottle);
	settings.minSwitchCostImprovement = ReadFloat(
		value, "min_switch_cost_improvement", settings.minSwitchCostImprovement);
	settings.minSwitchCostRatio = ReadFloat(
		value, "min_switch_cost_ratio", settings.minSwitchCostRatio);
	settings.minSwitchInterval = ReadFloat(value, "min_switch_interval", settings.minSwitchInterval);
	settings.blendInterruptFraction = ReadFloat(
		value, "blend_interrupt_fraction", settings.blendInterruptFraction);
	settings.continuationBias = ReadFloat(value, "continuation_bias", settings.continuationBias);
	settings.loopBias = ReadFloat(value, "loop_bias", settings.loopBias);
	settings.transitionBias = ReadFloat(value, "transition_bias", settings.transitionBias);
	settings.desiredSpeedScale = ReadFloat(value, "desired_speed_scale", settings.desiredSpeedScale);
	settings.worldToAnimationScale = ReadFloat(
		value, "world_to_animation_scale", settings.worldToAnimationScale);
	settings.enableSpeedMatching = ReadBool(
		value, "speed_matching_enabled", settings.enableSpeedMatching);
	settings.minPlaybackRate = ReadFloat(value, "min_playback_rate", settings.minPlaybackRate);
	settings.maxPlaybackRate = ReadFloat(value, "max_playback_rate", settings.maxPlaybackRate);
	settings.playbackRateSmoothing = ReadFloat(
		value, "playback_rate_smoothing", settings.playbackRateSmoothing);
	settings.trajectoryResponsiveness = ReadFloat(
		value, "trajectory_responsiveness", settings.trajectoryResponsiveness);

	if (const VansSerializedValue* steering = ReadObject(value, "root_motion_steering"))
	{
		settings.steering.enabled = ReadBool(*steering, "enabled", settings.steering.enabled);
		settings.steering.predictionTime = ReadFloat(
			*steering, "prediction_time", settings.steering.predictionTime);
		settings.steering.correctionHalfLife = ReadFloat(
			*steering, "correction_half_life", settings.steering.correctionHalfLife);
		settings.steering.maxCorrectionAngleDegrees = ReadFloat(
			*steering, "max_correction_angle_degrees", settings.steering.maxCorrectionAngleDegrees);
		settings.steering.maxCorrectionYawRateDegreesPerSecond = ReadFloat(
			*steering, "max_correction_yaw_rate_degrees_per_second",
			settings.steering.maxCorrectionYawRateDegreesPerSecond);
		settings.steering.minMovementSpeed = ReadFloat(
			*steering, "min_movement_speed", settings.steering.minMovementSpeed);
	}
	if (const VansSerializedValue* warping = ReadObject(value, "turn_in_place_warping"))
	{
		settings.turnInPlaceWarping.enabled = ReadBool(
			*warping, "enabled", settings.turnInPlaceWarping.enabled);
		settings.turnInPlaceWarping.maxTargetPredictionTime = ReadFloat(
			*warping, "max_target_prediction_time", settings.turnInPlaceWarping.maxTargetPredictionTime);
		settings.turnInPlaceWarping.proceduralTargetTime = ReadFloat(
			*warping, "procedural_target_time", settings.turnInPlaceWarping.proceduralTargetTime);
		settings.turnInPlaceWarping.correctionHalfLife = ReadFloat(
			*warping, "correction_half_life", settings.turnInPlaceWarping.correctionHalfLife);
		settings.turnInPlaceWarping.minRootYawScaleRatio = ReadFloat(
			*warping, "min_root_yaw_scale_ratio", settings.turnInPlaceWarping.minRootYawScaleRatio);
		settings.turnInPlaceWarping.maxRootYawScaleRatio = ReadFloat(
			*warping, "max_root_yaw_scale_ratio", settings.turnInPlaceWarping.maxRootYawScaleRatio);
		settings.turnInPlaceWarping.rootYawThresholdDegrees = ReadFloat(
			*warping, "root_yaw_threshold_degrees", settings.turnInPlaceWarping.rootYawThresholdDegrees);
		settings.turnInPlaceWarping.maxAdditiveCorrectionDegrees = ReadFloat(
			*warping, "max_additive_correction_degrees", settings.turnInPlaceWarping.maxAdditiveCorrectionDegrees);
		settings.turnInPlaceWarping.maxAdditiveYawRateDegreesPerSecond = ReadFloat(
			*warping, "max_additive_yaw_rate_degrees_per_second",
			settings.turnInPlaceWarping.maxAdditiveYawRateDegreesPerSecond);
		settings.turnInPlaceWarping.finalToleranceDegrees = ReadFloat(
			*warping, "final_tolerance_degrees", settings.turnInPlaceWarping.finalToleranceDegrees);
		settings.turnInPlaceWarping.endpointScaleCostWeight = ReadFloat(
			*warping, "endpoint_scale_cost_weight", settings.turnInPlaceWarping.endpointScaleCostWeight);
		settings.turnInPlaceWarping.endpointResidualCostWeight = ReadFloat(
			*warping, "endpoint_residual_cost_weight", settings.turnInPlaceWarping.endpointResidualCostWeight);
	}
	if (const VansSerializedValue* reconciliation = ReadObject(value, "root_motion_reconciliation"))
	{
		settings.rootMotionReconciliation.enabled = ReadBool(
			*reconciliation, "enabled", settings.rootMotionReconciliation.enabled);
		settings.rootMotionReconciliation.linearVelocityHalfLife = ReadFloat(
			*reconciliation, "linear_velocity_half_life",
			settings.rootMotionReconciliation.linearVelocityHalfLife);
		settings.rootMotionReconciliation.angularVelocityHalfLife = ReadFloat(
			*reconciliation, "angular_velocity_half_life",
			settings.rootMotionReconciliation.angularVelocityHalfLife);
		settings.rootMotionReconciliation.maxDuration = ReadFloat(
			*reconciliation, "max_duration", settings.rootMotionReconciliation.maxDuration);
		settings.rootMotionReconciliation.maxLinearVelocityCorrection = ReadFloat(
			*reconciliation, "max_linear_velocity_correction",
			settings.rootMotionReconciliation.maxLinearVelocityCorrection);
		settings.rootMotionReconciliation.maxAngularVelocityCorrectionDegreesPerSecond = ReadFloat(
			*reconciliation, "max_angular_velocity_correction_degrees_per_second",
			settings.rootMotionReconciliation.maxAngularVelocityCorrectionDegreesPerSecond);
	}

	settings.facingTurnEnterThresholdDegrees = ReadFloat(
		value, "facing_turn_enter_threshold_degrees", settings.facingTurnEnterThresholdDegrees);
	settings.facingTurnExitThresholdDegrees = ReadFloat(
		value, "facing_turn_exit_threshold_degrees", settings.facingTurnExitThresholdDegrees);
	settings.facingTurnExitYawRateDegreesPerSecond = ReadFloat(
		value, "facing_turn_exit_yaw_rate_degrees_per_second",
		settings.facingTurnExitYawRateDegreesPerSecond);
	settings.inertializationHalfLife = ReadFloat(
		value, "inertialization_half_life", settings.inertializationHalfLife);
	settings.inertializationMaxDuration = ReadFloat(
		value, "inertialization_max_duration", settings.inertializationMaxDuration);
	settings.contactWeight = ReadFloat(value, "contact_weight", settings.contactWeight);
	settings.pivotEnterAngleDegrees = ReadFloat(
		value, "pivot_enter_angle_degrees", settings.pivotEnterAngleDegrees);
	settings.pivotExitAngleDegrees = ReadFloat(
		value, "pivot_exit_angle_degrees", settings.pivotExitAngleDegrees);
	settings.pivotMinSpeed = ReadFloat(value, "pivot_min_speed", settings.pivotMinSpeed);
	settings.pivotPredictionLeadTime = ReadFloat(
		value, "pivot_prediction_lead_time", settings.pivotPredictionLeadTime);
	settings.pivotUrgentPredictionTime = ReadFloat(
		value, "pivot_urgent_prediction_time", settings.pivotUrgentPredictionTime);
	settings.pivotMinimumPlaybackTime = ReadFloat(
		value, "pivot_minimum_playback_time", settings.pivotMinimumPlaybackTime);
	settings.urgentDirectionChangeDegrees = ReadFloat(
		value, "urgent_direction_change_degrees", settings.urgentDirectionChangeDegrees);
	settings.directionBucketTolerance = ReadInt(
		value, "direction_bucket_tolerance", settings.directionBucketTolerance);
	settings.contactHeightFullFraction = ReadFloat(
		value, "contact_height_full_fraction", settings.contactHeightFullFraction);
	settings.contactHeightFadeFraction = ReadFloat(
		value, "contact_height_fade_fraction", settings.contactHeightFadeFraction);
	settings.contactVelocityConfidenceFloor = ReadFloat(
		value, "contact_velocity_confidence_floor", settings.contactVelocityConfidenceFloor);
	settings.topCandidateCount = ReadInt(value, "top_candidates", settings.topCandidateCount);

	if (const VansSerializedValue* parameters = ReadObject(value, "parameters"))
	{
		settings.parameters.enabled = ReadSerializedStringField(
			*parameters, "enabled", settings.parameters.enabled);
		settings.parameters.speed = ReadSerializedStringField(
			*parameters, "speed", settings.parameters.speed);
		settings.parameters.direction = ReadSerializedStringField(
			*parameters, "direction", settings.parameters.direction);
		settings.parameters.crouching = ReadSerializedStringField(
			*parameters, "crouching", settings.parameters.crouching);
		settings.parameters.airborne = ReadSerializedStringField(
			*parameters, "airborne", settings.parameters.airborne);
		settings.parameters.moveState = ReadSerializedStringField(
			*parameters, "move_state", settings.parameters.moveState);
	}
	if (const VansSerializedValue* rig = ReadObject(value, "rig"))
	{
		settings.rig.root = ReadSerializedStringField(*rig, "root", settings.rig.root);
		settings.rig.trajectoryRoot = ReadSerializedStringField(
			*rig, "trajectory_root", settings.rig.trajectoryRoot);
		settings.rig.pelvis = ReadSerializedStringField(*rig, "pelvis", settings.rig.pelvis);
		settings.rig.leftFoot = ReadSerializedStringField(*rig, "left_foot", settings.rig.leftFoot);
		settings.rig.rightFoot = ReadSerializedStringField(*rig, "right_foot", settings.rig.rightFoot);
		settings.rig.head = ReadSerializedStringField(*rig, "head", settings.rig.head);
		settings.rig.forwardAxis = ReadVec3(*rig, "forward_axis", settings.rig.forwardAxis);
	}
	if (const VansSerializedValue* contactsField = FindObjectField(value, "contacts"))
	{
		if (contactsField->kind != VansSerializedValue::Kind::Object)
		{
			error = "Motion Matching contacts must be an object";
			return false;
		}
		const VansSerializedValue* contacts = contactsField;
		settings.contactProvider = ReadSerializedStringField(*contacts, "provider");
		const VansSerializedValue* channels = ReadArray(*contacts, "channels");
		if (settings.contactProvider.empty() || !channels || channels->arrayItems.empty())
		{
			error = "Motion Matching contacts require a provider and at least one channel";
			return false;
		}
		for (const VansSerializedValue& channelValue : channels->arrayItems)
		{
			if (channelValue.kind != VansSerializedValue::Kind::Object)
			{
				error = "Motion Matching contact channel must be an object";
				return false;
			}
			MotionMatchingContactChannel channel;
			channel.id = ReadSerializedStringField(channelValue, "id");
			const std::string source = ReadSerializedStringField(channelValue, "source");
			if (channel.id.empty() || (source != "leftFoot" && source != "rightFoot"))
			{
				error = "Motion Matching contact channel requires a valid id and source";
				return false;
			}
			channel.source = source == "leftFoot"
				? MotionMatchingContactSource::LeftFoot
				: MotionMatchingContactSource::RightFoot;
			settings.contactChannels.push_back(std::move(channel));
		}
	}
	if (const VansSerializedValue* schema = ReadObject(value, "schema"))
	{
		if (FindObjectField(*schema, "name") || FindObjectField(*schema, "sample_rate") ||
			FindObjectField(*schema, "channels"))
		{
			error = "Motion Matching schema contains obsolete descriptive fields";
			return false;
		}
		settings.trajectoryWeight = ReadFloat(
			*schema, "trajectory_weight", settings.trajectoryWeight);
		settings.trajectoryPositionWeight = ReadFloat(
			*schema, "position_weight", settings.trajectoryPositionWeight);
		settings.trajectoryVelocityWeight = ReadFloat(
			*schema, "velocity_weight", settings.trajectoryVelocityWeight);
		settings.trajectoryFacingWeight = ReadFloat(
			*schema, "facing_weight", settings.trajectoryFacingWeight);
		settings.poseWeight = ReadFloat(*schema, "pose_weight", settings.poseWeight);
		if (const VansSerializedValue* futureTimes = ReadArray(*schema, "future_times"))
			for (std::size_t index = 0;
				index < settings.schema.futureTimes.size() && index < futureTimes->arrayItems.size();
				++index)
			{
				const VansSerializedValue& item = futureTimes->arrayItems[index];
				if (item.kind == VansSerializedValue::Kind::Float || item.kind == VansSerializedValue::Kind::Int)
					settings.schema.futureTimes[index] = static_cast<float>(ReadSerializedNumber(item));
			}
	}
	if (const VansSerializedValue* states = ReadObject(value, "states"))
	{
		settings.states.idleState = ReadInt(*states, "idle_state", settings.states.idleState);
		settings.states.crouchState = ReadInt(*states, "crouch_state", settings.states.crouchState);
		settings.states.airborneState = ReadInt(
			*states, "airborne_state", settings.states.airborneState);
		settings.states.idleSpeedThreshold = ReadFloat(
			*states, "idle_speed_threshold", settings.states.idleSpeedThreshold);
		ReadInts(*states, "moving_states", settings.states.movingStates);
		ReadInts(*states, "pace_transition_states", settings.states.paceTransitionStates);
		ReadInts(*states, "stance_states", settings.states.stanceStates);
	}
	if (const VansSerializedValue* databases = ReadArray(value, "databases"))
		for (const VansSerializedValue& databaseValue : databases->arrayItems)
		{
			if (databaseValue.kind != VansSerializedValue::Kind::Object) continue;
			MotionMatchingDatabase database = DecodeDatabase(databaseValue);
			if (!database.name.empty() && (!database.clips.empty() || !database.includeTokens.empty()))
				settings.databases.push_back(std::move(database));
		}
	ReadStrings(value, "include_clip_tokens", settings.includeClipTokens);
	ReadStrings(value, "exclude_clip_tokens", settings.excludeClipTokens);
	if (const VansSerializedValue* selector = ReadArray(value, "selector"))
		for (const VansSerializedValue& rowValue : selector->arrayItems)
		{
			if (rowValue.kind != VansSerializedValue::Kind::Object) continue;
			MotionMatchingSelectorRow row = DecodeSelector(rowValue);
			if (!row.databases.empty()) settings.selectorRows.push_back(std::move(row));
		}
	return true;
}

bool VansMotionMatchingConfigCodec::Encode(
	const MotionMatchingSettings& settings,
	VansSerializedValue& value,
	std::string& error)
{
	error.clear();
	if (!DriveModeName(settings.motionModel.driveMode))
	{
		error = "Motion Matching has an invalid motion model drive mode";
		return false;
	}
	if (settings.contactProvider.empty() != settings.contactChannels.empty())
	{
		error = "Motion Matching contact provider and channels must be configured together";
		return false;
	}

	std::vector<VansSerializedValue> contactChannels;
	contactChannels.reserve(settings.contactChannels.size());
	for (const MotionMatchingContactChannel& channel : settings.contactChannels)
	{
		const char* source = nullptr;
		switch (channel.source)
		{
		case MotionMatchingContactSource::LeftFoot: source = "leftFoot"; break;
		case MotionMatchingContactSource::RightFoot: source = "rightFoot"; break;
		}
		if (channel.id.empty() || !source)
		{
			error = "Motion Matching has an invalid contact channel";
			return false;
		}
		contactChannels.push_back(VansSerializedValue::Object({
			{ "id", VansSerializedValue::String(channel.id) },
			{ "source", VansSerializedValue::String(source) }
		}));
	}

	std::vector<VansSerializedValue> futureTimes;
	for (float time : settings.schema.futureTimes)
		futureTimes.push_back(VansSerializedValue::Float(time));
	std::vector<VansSerializedValue> databases;
	databases.reserve(settings.databases.size());
	for (const MotionMatchingDatabase& database : settings.databases)
		databases.push_back(EncodeDatabase(database));
	std::vector<VansSerializedValue> selector;
	selector.reserve(settings.selectorRows.size());
	for (const MotionMatchingSelectorRow& row : settings.selectorRows)
		selector.push_back(EncodeSelector(row));

	value = VansSerializedValue::Object({
		{ "enabled", VansSerializedValue::Bool(settings.enabled) },
		{ "auto_build", VansSerializedValue::Bool(settings.autoBuild) },
		{ "motion_model", EncodeMotionModel(settings.motionModel) },
		{ "sample_rate", VansSerializedValue::Float(settings.sampleRate) },
		{ "non_loop_sampling_end_margin", VansSerializedValue::Float(settings.nonLoopSamplingEndMargin) },
		{ "search_throttle", VansSerializedValue::Float(settings.searchThrottle) },
		{ "min_switch_cost_improvement", VansSerializedValue::Float(settings.minSwitchCostImprovement) },
		{ "min_switch_cost_ratio", VansSerializedValue::Float(settings.minSwitchCostRatio) },
		{ "min_switch_interval", VansSerializedValue::Float(settings.minSwitchInterval) },
		{ "blend_interrupt_fraction", VansSerializedValue::Float(settings.blendInterruptFraction) },
		{ "continuation_bias", VansSerializedValue::Float(settings.continuationBias) },
		{ "loop_bias", VansSerializedValue::Float(settings.loopBias) },
		{ "transition_bias", VansSerializedValue::Float(settings.transitionBias) },
		{ "desired_speed_scale", VansSerializedValue::Float(settings.desiredSpeedScale) },
		{ "world_to_animation_scale", VansSerializedValue::Float(settings.worldToAnimationScale) },
		{ "speed_matching_enabled", VansSerializedValue::Bool(settings.enableSpeedMatching) },
		{ "min_playback_rate", VansSerializedValue::Float(settings.minPlaybackRate) },
		{ "max_playback_rate", VansSerializedValue::Float(settings.maxPlaybackRate) },
		{ "playback_rate_smoothing", VansSerializedValue::Float(settings.playbackRateSmoothing) },
		{ "trajectory_responsiveness", VansSerializedValue::Float(settings.trajectoryResponsiveness) },
		{ "root_motion_steering", VansSerializedValue::Object({
			{ "enabled", VansSerializedValue::Bool(settings.steering.enabled) },
			{ "prediction_time", VansSerializedValue::Float(settings.steering.predictionTime) },
			{ "correction_half_life", VansSerializedValue::Float(settings.steering.correctionHalfLife) },
			{ "max_correction_angle_degrees", VansSerializedValue::Float(settings.steering.maxCorrectionAngleDegrees) },
			{ "max_correction_yaw_rate_degrees_per_second", VansSerializedValue::Float(settings.steering.maxCorrectionYawRateDegreesPerSecond) },
			{ "min_movement_speed", VansSerializedValue::Float(settings.steering.minMovementSpeed) }
		}) },
		{ "turn_in_place_warping", VansSerializedValue::Object({
			{ "enabled", VansSerializedValue::Bool(settings.turnInPlaceWarping.enabled) },
			{ "max_target_prediction_time", VansSerializedValue::Float(settings.turnInPlaceWarping.maxTargetPredictionTime) },
			{ "procedural_target_time", VansSerializedValue::Float(settings.turnInPlaceWarping.proceduralTargetTime) },
			{ "correction_half_life", VansSerializedValue::Float(settings.turnInPlaceWarping.correctionHalfLife) },
			{ "min_root_yaw_scale_ratio", VansSerializedValue::Float(settings.turnInPlaceWarping.minRootYawScaleRatio) },
			{ "max_root_yaw_scale_ratio", VansSerializedValue::Float(settings.turnInPlaceWarping.maxRootYawScaleRatio) },
			{ "root_yaw_threshold_degrees", VansSerializedValue::Float(settings.turnInPlaceWarping.rootYawThresholdDegrees) },
			{ "max_additive_correction_degrees", VansSerializedValue::Float(settings.turnInPlaceWarping.maxAdditiveCorrectionDegrees) },
			{ "max_additive_yaw_rate_degrees_per_second", VansSerializedValue::Float(settings.turnInPlaceWarping.maxAdditiveYawRateDegreesPerSecond) },
			{ "final_tolerance_degrees", VansSerializedValue::Float(settings.turnInPlaceWarping.finalToleranceDegrees) },
			{ "endpoint_scale_cost_weight", VansSerializedValue::Float(settings.turnInPlaceWarping.endpointScaleCostWeight) },
			{ "endpoint_residual_cost_weight", VansSerializedValue::Float(settings.turnInPlaceWarping.endpointResidualCostWeight) }
		}) },
		{ "root_motion_reconciliation", VansSerializedValue::Object({
			{ "enabled", VansSerializedValue::Bool(settings.rootMotionReconciliation.enabled) },
			{ "linear_velocity_half_life", VansSerializedValue::Float(settings.rootMotionReconciliation.linearVelocityHalfLife) },
			{ "angular_velocity_half_life", VansSerializedValue::Float(settings.rootMotionReconciliation.angularVelocityHalfLife) },
			{ "max_duration", VansSerializedValue::Float(settings.rootMotionReconciliation.maxDuration) },
			{ "max_linear_velocity_correction", VansSerializedValue::Float(settings.rootMotionReconciliation.maxLinearVelocityCorrection) },
			{ "max_angular_velocity_correction_degrees_per_second", VansSerializedValue::Float(settings.rootMotionReconciliation.maxAngularVelocityCorrectionDegreesPerSecond) }
		}) },
		{ "facing_turn_enter_threshold_degrees", VansSerializedValue::Float(settings.facingTurnEnterThresholdDegrees) },
		{ "facing_turn_exit_threshold_degrees", VansSerializedValue::Float(settings.facingTurnExitThresholdDegrees) },
		{ "facing_turn_exit_yaw_rate_degrees_per_second", VansSerializedValue::Float(settings.facingTurnExitYawRateDegreesPerSecond) },
		{ "inertialization_half_life", VansSerializedValue::Float(settings.inertializationHalfLife) },
		{ "inertialization_max_duration", VansSerializedValue::Float(settings.inertializationMaxDuration) },
		{ "contact_weight", VansSerializedValue::Float(settings.contactWeight) },
		{ "pivot_enter_angle_degrees", VansSerializedValue::Float(settings.pivotEnterAngleDegrees) },
		{ "pivot_exit_angle_degrees", VansSerializedValue::Float(settings.pivotExitAngleDegrees) },
		{ "pivot_min_speed", VansSerializedValue::Float(settings.pivotMinSpeed) },
		{ "pivot_prediction_lead_time", VansSerializedValue::Float(settings.pivotPredictionLeadTime) },
		{ "pivot_urgent_prediction_time", VansSerializedValue::Float(settings.pivotUrgentPredictionTime) },
		{ "pivot_minimum_playback_time", VansSerializedValue::Float(settings.pivotMinimumPlaybackTime) },
		{ "urgent_direction_change_degrees", VansSerializedValue::Float(settings.urgentDirectionChangeDegrees) },
		{ "direction_bucket_tolerance", VansSerializedValue::Int(settings.directionBucketTolerance) },
		{ "contact_height_full_fraction", VansSerializedValue::Float(settings.contactHeightFullFraction) },
		{ "contact_height_fade_fraction", VansSerializedValue::Float(settings.contactHeightFadeFraction) },
		{ "contact_velocity_confidence_floor", VansSerializedValue::Float(settings.contactVelocityConfidenceFloor) },
		{ "top_candidates", VansSerializedValue::Int(settings.topCandidateCount) },
		{ "parameters", VansSerializedValue::Object({
			{ "enabled", VansSerializedValue::String(settings.parameters.enabled) },
			{ "speed", VansSerializedValue::String(settings.parameters.speed) },
			{ "direction", VansSerializedValue::String(settings.parameters.direction) },
			{ "crouching", VansSerializedValue::String(settings.parameters.crouching) },
			{ "airborne", VansSerializedValue::String(settings.parameters.airborne) },
			{ "move_state", VansSerializedValue::String(settings.parameters.moveState) }
		}) },
		{ "rig", VansSerializedValue::Object({
			{ "root", VansSerializedValue::String(settings.rig.root) },
			{ "trajectory_root", VansSerializedValue::String(settings.rig.trajectoryRoot) },
			{ "pelvis", VansSerializedValue::String(settings.rig.pelvis) },
			{ "left_foot", VansSerializedValue::String(settings.rig.leftFoot) },
			{ "right_foot", VansSerializedValue::String(settings.rig.rightFoot) },
			{ "head", VansSerializedValue::String(settings.rig.head) },
			{ "forward_axis", EncodeVec3(settings.rig.forwardAxis) }
		}) },
		{ "schema", VansSerializedValue::Object({
			{ "future_times", VansSerializedValue::Array(std::move(futureTimes)) },
			{ "trajectory_weight", VansSerializedValue::Float(settings.trajectoryWeight) },
			{ "position_weight", VansSerializedValue::Float(settings.trajectoryPositionWeight) },
			{ "velocity_weight", VansSerializedValue::Float(settings.trajectoryVelocityWeight) },
			{ "facing_weight", VansSerializedValue::Float(settings.trajectoryFacingWeight) },
			{ "pose_weight", VansSerializedValue::Float(settings.poseWeight) }
		}) },
		{ "states", VansSerializedValue::Object({
			{ "idle_state", VansSerializedValue::Int(settings.states.idleState) },
			{ "crouch_state", VansSerializedValue::Int(settings.states.crouchState) },
			{ "airborne_state", VansSerializedValue::Int(settings.states.airborneState) },
			{ "idle_speed_threshold", VansSerializedValue::Float(settings.states.idleSpeedThreshold) },
			{ "moving_states", EncodeInts(settings.states.movingStates) },
			{ "pace_transition_states", EncodeInts(settings.states.paceTransitionStates) },
			{ "stance_states", EncodeInts(settings.states.stanceStates) }
		}) },
		{ "contacts", VansSerializedValue::Object({
			{ "provider", VansSerializedValue::String(settings.contactProvider) },
			{ "channels", VansSerializedValue::Array(std::move(contactChannels)) }
		}) },
		{ "include_clip_tokens", EncodeStrings(settings.includeClipTokens) },
		{ "exclude_clip_tokens", EncodeStrings(settings.excludeClipTokens) },
		{ "databases", VansSerializedValue::Array(std::move(databases)) },
		{ "selector", VansSerializedValue::Array(std::move(selector)) }
	});
	if (settings.contactProvider.empty())
		EraseSerializedObjectField(value, "contacts");
	return true;
}
}
