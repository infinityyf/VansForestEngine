#include "VansTimelineSerialization.h"

#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <unordered_set>

namespace Vans
{
namespace
{
using Json = VansTimelineSerialization::Json;

template <typename Enum>
struct EnumName
{
	Enum value;
	const char* name;
};

template <typename Enum, std::size_t Count>
const char* EnumToName(Enum value, const EnumName<Enum> (&names)[Count], const char* fallback)
{
	for (const auto& item : names)
		if (item.value == value) return item.name;
	return fallback;
}

template <typename Enum, std::size_t Count>
bool ParseEnum(const std::string& value, const EnumName<Enum> (&names)[Count], Enum& result)
{
	for (const auto& item : names)
	{
		if (value == item.name)
		{
			result = item.value;
			return true;
		}
	}
	return false;
}

constexpr EnumName<VansTimelineTrackType> TrackTypes[] = {
	{ VansTimelineTrackType::Transform, "Transform" },
	{ VansTimelineTrackType::Property, "Property" },
	{ VansTimelineTrackType::Activation, "Activation" },
	{ VansTimelineTrackType::Constraint, "Constraint" },
	{ VansTimelineTrackType::AnimationClip, "AnimationClip" },
	{ VansTimelineTrackType::AnimatorParameter, "AnimatorParameter" },
	{ VansTimelineTrackType::BoneOverride, "BoneOverride" },
	{ VansTimelineTrackType::Audio, "Audio" },
	{ VansTimelineTrackType::Media, "Media" },
	{ VansTimelineTrackType::Particle, "Particle" },
	{ VansTimelineTrackType::CameraCut, "CameraCut" },
	{ VansTimelineTrackType::CameraProperty, "CameraProperty" },
	{ VansTimelineTrackType::CameraShake, "CameraShake" },
	{ VansTimelineTrackType::FadePostProcess, "FadePostProcess" },
	{ VansTimelineTrackType::Light, "Light" },
	{ VansTimelineTrackType::MaterialParameter, "MaterialParameter" },
	{ VansTimelineTrackType::MaterialSwitch, "MaterialSwitch" },
	{ VansTimelineTrackType::UIState, "UIState" },
	{ VansTimelineTrackType::EventSignal, "EventSignal" },
	{ VansTimelineTrackType::SubTimeline, "SubTimeline" },
	{ VansTimelineTrackType::Spawnable, "Spawnable" },
	{ VansTimelineTrackType::TimeScale, "TimeScale" },
	{ VansTimelineTrackType::SceneState, "SceneState" },
	{ VansTimelineTrackType::Custom, "Custom" }
};

constexpr EnumName<VansTimelineChannelType> ChannelTypes[] = {
	{ VansTimelineChannelType::Bool, "Bool" }, { VansTimelineChannelType::Int32, "Int32" },
	{ VansTimelineChannelType::Int64, "Int64" }, { VansTimelineChannelType::Float, "Float" },
	{ VansTimelineChannelType::Double, "Double" }, { VansTimelineChannelType::Enum, "Enum" },
	{ VansTimelineChannelType::String, "String" }, { VansTimelineChannelType::Vec2, "Vec2" },
	{ VansTimelineChannelType::Vec3, "Vec3" }, { VansTimelineChannelType::Vec4, "Vec4" },
	{ VansTimelineChannelType::Quaternion, "Quaternion" },
	{ VansTimelineChannelType::ColorLinear, "ColorLinear" },
	{ VansTimelineChannelType::ColorSrgb, "ColorSrgb" },
	{ VansTimelineChannelType::ObjectReference, "ObjectReference" },
	{ VansTimelineChannelType::EventPayload, "EventPayload" }
};

constexpr EnumName<VansTimelineBindingKind> BindingKinds[] = {
	{ VansTimelineBindingKind::SceneEntity, "SceneEntity" },
	{ VansTimelineBindingKind::SceneComponent, "SceneComponent" },
	{ VansTimelineBindingKind::RuntimeObject, "RuntimeObject" },
	{ VansTimelineBindingKind::Asset, "Asset" },
	{ VansTimelineBindingKind::Spawnable, "Spawnable" },
	{ VansTimelineBindingKind::UIComponent, "UIComponent" },
	{ VansTimelineBindingKind::External, "External" }
};
constexpr EnumName<VansTimelineInterpolation> Interpolations[] = {
	{ VansTimelineInterpolation::Constant, "Constant" }, { VansTimelineInterpolation::Linear, "Linear" },
	{ VansTimelineInterpolation::Auto, "Auto" }, { VansTimelineInterpolation::ClampedAuto, "ClampedAuto" },
	{ VansTimelineInterpolation::Cubic, "Cubic" }, { VansTimelineInterpolation::Bezier, "Bezier" },
	{ VansTimelineInterpolation::Slerp, "Slerp" }
};
constexpr EnumName<VansTimelineTangentMode> TangentModes[] = {
	{ VansTimelineTangentMode::Unified, "Unified" }, { VansTimelineTangentMode::Broken, "Broken" },
	{ VansTimelineTangentMode::Weighted, "Weighted" }
};
constexpr EnumName<VansTimelineBlendMode> BlendModes[] = {
	{ VansTimelineBlendMode::Override, "Override" }, { VansTimelineBlendMode::Additive, "Additive" },
	{ VansTimelineBlendMode::Multiply, "Multiply" }, { VansTimelineBlendMode::Relative, "Relative" }
};
constexpr EnumName<VansTimelineCompletionMode> CompletionModes[] = {
	{ VansTimelineCompletionMode::ProjectDefault, "ProjectDefault" },
	{ VansTimelineCompletionMode::RestoreState, "RestoreState" },
	{ VansTimelineCompletionMode::KeepState, "KeepState" }
};
constexpr EnumName<VansTimelineEvaluationMode> EvaluationModes[] = {
	{ VansTimelineEvaluationMode::WithSubTimelines, "WithSubTimelines" },
	{ VansTimelineEvaluationMode::Isolated, "Isolated" }
};
constexpr EnumName<VansTimelineLoopMode> LoopModes[] = {
	{ VansTimelineLoopMode::None, "None" }, { VansTimelineLoopMode::Loop, "Loop" },
	{ VansTimelineLoopMode::PingPong, "PingPong" }
};
constexpr EnumName<VansTimelineExtrapolation> Extrapolations[] = {
	{ VansTimelineExtrapolation::None, "None" }, { VansTimelineExtrapolation::Hold, "Hold" },
	{ VansTimelineExtrapolation::Linear, "Linear" }, { VansTimelineExtrapolation::Loop, "Loop" },
	{ VansTimelineExtrapolation::PingPong, "PingPong" }
};
constexpr EnumName<VansTimelinePlayOn> PlayOnNames[] = {
	{ VansTimelinePlayOn::Manual, "Manual" }, { VansTimelinePlayOn::Awake, "Awake" },
	{ VansTimelinePlayOn::Enable, "Enable" }, { VansTimelinePlayOn::Signal, "Signal" }
};
constexpr EnumName<VansTimelineUpdateMode> UpdateModes[] = {
	{ VansTimelineUpdateMode::GameTime, "GameTime" },
	{ VansTimelineUpdateMode::UnscaledTime, "UnscaledTime" },
	{ VansTimelineUpdateMode::Manual, "Manual" }
};
constexpr EnumName<VansTimelineBindingRootMode> BindingRootModes[] = {
	{ VansTimelineBindingRootMode::OwnerRelative, "OwnerRelative" },
	{ VansTimelineBindingRootMode::World, "World" }
};

template <std::size_t Count>
Json ArrayJson(const std::array<double, Count>& values)
{
	Json result = Json::array();
	for (double value : values) result.push_back(value);
	return result;
}

template <std::size_t Count>
std::array<double, Count> ReadArray(const Json& value, std::array<double, Count> fallback = {})
{
	if (!value.is_array() || value.size() != Count) return fallback;
	for (std::size_t index = 0; index < Count; ++index)
		fallback[index] = value[index].is_number() ? value[index].get<double>() : fallback[index];
	return fallback;
}

Json ColorJson(const std::array<float, 4>& color)
{
	return Json::array({ color[0], color[1], color[2], color[3] });
}

std::array<float, 4> ReadColor(const Json& value, std::array<float, 4> fallback)
{
	if (!value.is_array() || value.size() != 4) return fallback;
	for (std::size_t index = 0; index < 4; ++index)
		if (value[index].is_number()) fallback[index] = value[index].get<float>();
	return fallback;
}

Json KeyValueJson(const VansTimelineKeyValue& value)
{
	return std::visit([](const auto& item) -> Json
	{
		using T = std::decay_t<decltype(item)>;
		if constexpr (std::is_same_v<T, std::monostate>) return nullptr;
		else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::int32_t> ||
			std::is_same_v<T, std::int64_t> || std::is_same_v<T, float> ||
			std::is_same_v<T, double> || std::is_same_v<T, std::string>) return item;
		else if constexpr (std::is_same_v<T, VansTimelineVec2> || std::is_same_v<T, VansTimelineVec3> ||
			std::is_same_v<T, VansTimelineVec4> || std::is_same_v<T, VansTimelineQuaternion> ||
			std::is_same_v<T, VansTimelineColorLinear> || std::is_same_v<T, VansTimelineColorSrgb>)
			return ArrayJson(item.value);
		else if constexpr (std::is_same_v<T, VansTimelineObjectReference>)
			return Json{ { "guid", item.guid }, { "path", item.path }, { "objectKind", item.objectKind } };
		else
			return Json{ { "payloadType", item.payloadType },
				{ "value", EncodeSerializedValueJson<Json>(item.value) } };
	}, value);
}

bool ReadKeyValue(const Json& value, VansTimelineChannelType type, VansTimelineKeyValue& result)
{
	switch (type)
	{
	case VansTimelineChannelType::Bool:
		if (!value.is_boolean()) return false; result = value.get<bool>(); return true;
	case VansTimelineChannelType::Int32:
		if (!value.is_number_integer()) return false; result = value.get<std::int32_t>(); return true;
	case VansTimelineChannelType::Int64:
		if (!value.is_number_integer()) return false; result = value.get<std::int64_t>(); return true;
	case VansTimelineChannelType::Float:
		if (!value.is_number()) return false; result = value.get<float>(); return true;
	case VansTimelineChannelType::Double:
		if (!value.is_number()) return false; result = value.get<double>(); return true;
	case VansTimelineChannelType::Enum:
	case VansTimelineChannelType::String:
		if (!value.is_string()) return false; result = value.get<std::string>(); return true;
	case VansTimelineChannelType::Vec2:
		if (!value.is_array() || value.size() != 2) return false; result = VansTimelineVec2{ ReadArray<2>(value) }; return true;
	case VansTimelineChannelType::Vec3:
		if (!value.is_array() || value.size() != 3) return false; result = VansTimelineVec3{ ReadArray<3>(value) }; return true;
	case VansTimelineChannelType::Vec4:
		if (!value.is_array() || value.size() != 4) return false; result = VansTimelineVec4{ ReadArray<4>(value) }; return true;
	case VansTimelineChannelType::Quaternion:
		if (!value.is_array() || value.size() != 4) return false;
		result = VansTimelineQuaternion{ ReadArray<4>(value, { 0.0, 0.0, 0.0, 1.0 }) }; return true;
	case VansTimelineChannelType::ColorLinear:
		if (!value.is_array() || value.size() != 4) return false;
		result = VansTimelineColorLinear{ ReadArray<4>(value, { 0.0, 0.0, 0.0, 1.0 }) }; return true;
	case VansTimelineChannelType::ColorSrgb:
		if (!value.is_array() || value.size() != 4) return false;
		result = VansTimelineColorSrgb{ ReadArray<4>(value, { 0.0, 0.0, 0.0, 1.0 }) }; return true;
	case VansTimelineChannelType::ObjectReference:
		if (!value.is_object()) return false;
		result = VansTimelineObjectReference{ value.value("guid", ""), value.value("path", ""), value.value("objectKind", "") };
		return true;
	case VansTimelineChannelType::EventPayload:
		if (!value.is_object()) return false;
		result = VansTimelineEventPayload{ value.value("payloadType", ""),
			DecodeSerializedValueJson(value.value("value", Json::object())) };
		return true;
	}
	return false;
}

Json RangeJson(const VansTimelineTickRange& range)
{
	return Json{ { "startTick", range.startTick }, { "endTick", range.endTick } };
}

VansTimelineTickRange ReadRange(const Json& value, VansTimelineTickRange fallback)
{
	if (!value.is_object()) return fallback;
	fallback.startTick = value.value("startTick", fallback.startTick);
	fallback.endTick = value.value("endTick", fallback.endTick);
	return fallback;
}

Json BlendCurveJson(const VansTimelineBlendCurve& curve)
{
	return Json{ { "shape", curve.shape }, { "exponent", curve.exponent } };
}

VansTimelineBlendCurve ReadBlendCurve(const Json& value)
{
	VansTimelineBlendCurve result;
	if (value.is_object())
	{
		result.shape = value.value("shape", result.shape);
		result.exponent = value.value("exponent", result.exponent);
	}
	return result;
}

Json BindingOverrideJson(const VansTimelineBindingOverride& value)
{
	return Json{
		{ "bindingId", value.bindingId }, { "targetEntityGuid", value.targetEntityGuid },
		{ "targetComponentGuid", value.targetComponentGuid },
		{ "targetComponentTypeId", value.targetComponentTypeId }, { "useOwner", value.useOwner }
	};
}

VansTimelineBindingOverride ReadBindingOverride(const Json& value)
{
	VansTimelineBindingOverride result;
	if (!value.is_object()) return result;
	result.bindingId = value.value("bindingId", "");
	result.targetEntityGuid = value.value("targetEntityGuid", "");
	result.targetComponentGuid = value.value("targetComponentGuid", "");
	result.targetComponentTypeId = value.value("targetComponentTypeId", 0);
	result.useOwner = value.value("useOwner", false);
	return result;
}

Json ConfigJson(const VansTimelineTrackConfig& config)
{
	return std::visit([](const auto& item) -> Json
	{
		using T = std::decay_t<decltype(item)>;
		Json result = Json::object();
		if constexpr (std::is_same_v<T, std::monostate>) {}
		else if constexpr (std::is_same_v<T, VansTimelineTransformTrackConfig>)
			result = { { "space", item.space }, { "channels", item.channels }, { "rotationMode", item.rotationMode },
				{ "trajectoryDisplay", item.trajectoryDisplay }, { "physicsPolicy", item.physicsPolicy }, { "writeScale", item.writeScale } };
		else if constexpr (std::is_same_v<T, VansTimelinePropertyTrackConfig>)
			result = { { "componentTypeId", item.componentTypeId }, { "componentGuid", item.componentGuid },
				{ "descriptorId", item.descriptorId },
				{ "propertyPath", item.propertyPath }, { "valueType", VansTimelineSerialization::ChannelTypeName(item.valueType) },
				{ "unit", item.unit }, { "minimum", item.minimum }, { "maximum", item.maximum },
				{ "step", item.step }, { "colorSpace", item.colorSpace } };
		else if constexpr (std::is_same_v<T, VansTimelineActivationTrackConfig>)
			result = { { "scope", item.scope }, { "stateWhenInside", item.stateWhenInside },
				{ "stateBefore", item.stateBefore }, { "stateAfter", item.stateAfter }, { "useCommandBuffer", item.useCommandBuffer } };
		else if constexpr (std::is_same_v<T, VansTimelineConstraintTrackConfig>)
			result = { { "constraintType", item.constraintType }, { "sourceBindingId", item.sourceBindingId },
				{ "targetBindingId", item.targetBindingId }, { "maintainOffset", item.maintainOffset },
				{ "offsetPosition", ArrayJson(item.offsetPosition.value) }, { "offsetRotation", ArrayJson(item.offsetRotation.value) },
				{ "offsetScale", ArrayJson(item.offsetScale.value) }, { "axisMask", item.axisMask }, { "upAxis", item.upAxis },
				{ "aimAxis", item.aimAxis }, { "weight", item.weight } };
		else if constexpr (std::is_same_v<T, VansTimelineAnimationTrackConfig>)
			result = { { "slot", item.slot }, { "layer", item.layer }, { "weight", item.weight }, { "additive", item.additive },
				{ "avatarMaskGuid", item.avatarMaskGuid }, { "avatarMaskPath", item.avatarMaskPath },
				{ "rootMotionPolicy", item.rootMotionPolicy }, { "syncGroup", item.syncGroup }, { "markerSync", item.markerSync } };
		else if constexpr (std::is_same_v<T, VansTimelineAnimatorParameterTrackConfig>)
			result = { { "parameterName", item.parameterName }, { "parameterType", item.parameterType },
				{ "firePolicy", item.firePolicy }, { "seekPolicy", item.seekPolicy }, { "missingParameterPolicy", item.missingParameterPolicy } };
		else if constexpr (std::is_same_v<T, VansTimelineBoneOverrideTrackConfig>)
			result = { { "bone", item.bone }, { "boneId", item.boneId }, { "weight", item.weight }, { "additive", item.additive },
				{ "space", item.space }, { "ikTargetBindingId", item.ikTargetBindingId }, { "poleBindingId", item.poleBindingId },
				{ "positionWeight", item.positionWeight }, { "rotationWeight", item.rotationWeight }, { "clearOnExit", item.clearOnExit } };
		else if constexpr (std::is_same_v<T, VansTimelineAudioTrackConfig>)
			result = { { "useBoundSource", item.useBoundSource }, { "volume", item.volume }, { "pitch", item.pitch },
				{ "stereoPan", item.stereoPan }, { "spatialBlend", item.spatialBlend }, { "fadeInSeconds", item.fadeInSeconds },
				{ "fadeOutSeconds", item.fadeOutSeconds }, { "bus", item.bus }, { "reverbSend", item.reverbSend },
				{ "referenceDistance", item.referenceDistance }, { "maxDistance", item.maxDistance }, { "rolloff", item.rolloff },
				{ "seekPolicy", item.seekPolicy }, { "onSectionEnd", item.onSectionEnd } };
		else if constexpr (std::is_same_v<T, VansTimelineMediaTrackConfig>)
			result = { { "syncMode", item.syncMode }, { "frameHold", item.frameHold }, { "colorSpace", item.colorSpace },
				{ "outputAudio", item.outputAudio }, { "targetKind", item.targetKind }, { "materialSlot", item.materialSlot },
				{ "uiElement", item.uiElement }, { "onSectionEnd", item.onSectionEnd } };
		else if constexpr (std::is_same_v<T, VansTimelineParticleTrackConfig>)
			result = { { "action", item.action }, { "prewarmTicks", item.prewarmTicks }, { "simulationRate", item.simulationRate },
				{ "randomSeed", item.randomSeed }, { "resetOnEnter", item.resetOnEnter }, { "clearOnExit", item.clearOnExit },
				{ "loop", item.loop }, { "seekPolicy", item.seekPolicy } };
		else if constexpr (std::is_same_v<T, VansTimelineCameraCutTrackConfig>)
			result = { { "cameraBindingId", item.cameraBindingId }, { "targetCameraBindingId", item.targetCameraBindingId }, { "cutMode", item.cutMode },
				{ "blendDurationTicks", item.blendDurationTicks }, { "blendCurve", BlendCurveJson(item.blendCurve) },
				{ "priority", item.priority },
				{ "aspectPolicy", item.aspectPolicy }, { "viewport", item.viewport }, { "shotName", item.shotName },
				{ "shotColor", ColorJson(item.shotColor) }, { "thumbnailCacheKey", item.thumbnailCacheKey } };
		else if constexpr (std::is_same_v<T, VansTimelineCameraPropertyTrackConfig>)
			result = { { "fieldOfView", item.fieldOfView }, { "nearClip", item.nearClip },
				{ "farClip", item.farClip }, { "transform", item.transform } };
		else if constexpr (std::is_same_v<T, VansTimelineCameraShakeTrackConfig>)
			result = { { "position", item.position }, { "rotation", item.rotation },
				{ "space", item.space }, { "amplitudeScale", item.amplitudeScale },
				{ "priority", item.priority } };
		else if constexpr (std::is_same_v<T, VansTimelineFadePostProcessTrackConfig>)
			result = { { "mode", item.mode }, { "color", ArrayJson(item.color.value) }, { "gameViewOnly", item.gameViewOnly },
				{ "profileGuid", item.profileGuid }, { "profilePath", item.profilePath },
				{ "blendWeight", item.blendWeight }, { "priority", item.priority } };
		else if constexpr (std::is_same_v<T, VansTimelineLightTrackConfig>)
			result = { { "color", item.color }, { "temperature", item.temperature }, { "intensity", item.intensity },
				{ "range", item.range }, { "cone", item.cone }, { "rectSize", item.rectSize }, { "shadow", item.shadow } };
		else if constexpr (std::is_same_v<T, VansTimelineMaterialParameterTrackConfig>)
			result = { { "materialSlotId", item.materialSlotId }, { "parameterName", item.parameterName },
				{ "parameterType", VansTimelineSerialization::ChannelTypeName(item.parameterType) }, { "instancePolicy", item.instancePolicy } };
		else if constexpr (std::is_same_v<T, VansTimelineMaterialSwitchTrackConfig>)
			result = { { "materialSlotId", item.materialSlotId } };
		else if constexpr (std::is_same_v<T, VansTimelineUIStateTrackConfig>)
			result = { { "screen", item.screen }, { "targetKind", item.targetKind }, { "element", item.element },
				{ "descriptorId", item.descriptorId }, { "setterId", item.setterId }, { "action", item.action } };
		else if constexpr (std::is_same_v<T, VansTimelineEventTrackConfig>)
			result = { { "signalId", item.signalId }, { "displayName", item.displayName }, { "payloadType", item.payloadType },
				{ "eventLane", item.eventLane }, { "firePolicy", item.firePolicy }, { "seekPolicy", item.seekPolicy },
				{ "loopPolicy", item.loopPolicy }, { "editorSafe", item.editorSafe }, { "oncePerPlayback", item.oncePerPlayback } };
		else if constexpr (std::is_same_v<T, VansTimelineSubTimelineTrackConfig>)
		{
			result = { { "bindingRemap", Json::array() }, { "parameterOverrides", Json::object() },
				{ "hierarchicalBias", item.hierarchicalBias }, { "useGlobalTimeDisplay", item.useGlobalTimeDisplay },
				{ "originTransformOverride", item.originTransformOverride }, { "originPosition", ArrayJson(item.originPosition.value) },
				{ "originRotation", ArrayJson(item.originRotation.value) }, { "originScale", ArrayJson(item.originScale.value) },
				{ "timeWarpChannel", item.timeWarpChannel } };
			for (const auto& remap : item.bindingRemap) result["bindingRemap"].push_back(BindingOverrideJson(remap));
			for (const auto& [name, value] : item.parameterOverrides) result["parameterOverrides"][name] = KeyValueJson(value);
		}
		else if constexpr (std::is_same_v<T, VansTimelineSpawnableTrackConfig>)
			result = { { "spawnTemplateGuid", item.spawnTemplateGuid }, { "spawnTemplatePath", item.spawnTemplatePath },
				{ "parentBindingId", item.parentBindingId }, { "spawnPolicy", item.spawnPolicy }, { "destroyPolicy", item.destroyPolicy },
				{ "prewarmTicks", item.prewarmTicks }, { "exportBindingId", item.exportBindingId } };
		else if constexpr (std::is_same_v<T, VansTimelineTimeScaleTrackConfig>)
			result = { { "scope", item.scope }, { "affectAudio", item.affectAudio }, { "affectParticles", item.affectParticles },
				{ "minimum", item.minimum }, { "maximum", item.maximum }, { "pauseAtZero", item.pauseAtZero } };
		else if constexpr (std::is_same_v<T, VansTimelineSceneStateTrackConfig>)
			result = { { "sceneGuid", item.sceneGuid }, { "scenePath", item.scenePath }, { "action", item.action },
				{ "preload", item.preload }, { "asyncPolicy", item.asyncPolicy }, { "activationPriority", item.activationPriority } };
		else if constexpr (std::is_same_v<T, VansTimelineCustomTrackConfig>)
			result = { { "customTypeId", item.customTypeId }, { "payload", EncodeSerializedValueJson<Json>(item.payload) } };
		return result;
	}, config);
}

VansTimelineTrackConfig ReadConfig(VansTimelineTrackType type, const Json& value)
{
	const Json empty = Json::object();
	const Json& source = value.is_object() ? value : empty;
	switch (type)
	{
	case VansTimelineTrackType::Transform:
	{
		VansTimelineTransformTrackConfig c;
		c.space = source.value("space", c.space); c.channels = source.value("channels", c.channels);
		c.rotationMode = source.value("rotationMode", c.rotationMode); c.trajectoryDisplay = source.value("trajectoryDisplay", c.trajectoryDisplay);
		c.physicsPolicy = source.value("physicsPolicy", c.physicsPolicy); c.writeScale = source.value("writeScale", c.writeScale); return c;
	}
	case VansTimelineTrackType::Property:
	{
		VansTimelinePropertyTrackConfig c;
		c.componentTypeId = source.value("componentTypeId", c.componentTypeId); c.componentGuid = source.value("componentGuid", "");
		c.descriptorId = source.value("descriptorId", "");
		c.propertyPath = source.value("propertyPath", ""); VansTimelineSerialization::TryParseChannelType(source.value("valueType", "Float"), c.valueType);
		c.unit = source.value("unit", ""); c.minimum = source.value("minimum", c.minimum); c.maximum = source.value("maximum", c.maximum);
		c.step = source.value("step", c.step); c.colorSpace = source.value("colorSpace", c.colorSpace); return c;
	}
	case VansTimelineTrackType::Activation:
	{
		VansTimelineActivationTrackConfig c; c.scope = source.value("scope", c.scope); c.stateWhenInside = source.value("stateWhenInside", c.stateWhenInside);
		c.stateBefore = source.value("stateBefore", c.stateBefore); c.stateAfter = source.value("stateAfter", c.stateAfter);
		c.useCommandBuffer = source.value("useCommandBuffer", c.useCommandBuffer); return c;
	}
	case VansTimelineTrackType::Constraint:
	{
		VansTimelineConstraintTrackConfig c; c.constraintType = source.value("constraintType", c.constraintType);
		c.sourceBindingId = source.value("sourceBindingId", ""); c.targetBindingId = source.value("targetBindingId", "");
		c.maintainOffset = source.value("maintainOffset", c.maintainOffset); c.offsetPosition.value = ReadArray<3>(source.value("offsetPosition", Json::array()));
		c.offsetRotation.value = ReadArray<4>(source.value("offsetRotation", Json::array()), c.offsetRotation.value);
		c.offsetScale.value = ReadArray<3>(source.value("offsetScale", Json::array()), c.offsetScale.value);
		c.axisMask = source.value("axisMask", c.axisMask); c.upAxis = source.value("upAxis", c.upAxis); c.aimAxis = source.value("aimAxis", c.aimAxis);
		c.weight = source.value("weight", c.weight); return c;
	}
	case VansTimelineTrackType::AnimationClip:
	{
		VansTimelineAnimationTrackConfig c; c.slot = source.value("slot", ""); c.layer = source.value("layer", "");
		c.weight = source.value("weight", c.weight); c.additive = source.value("additive", c.additive);
		c.avatarMaskGuid = source.value("avatarMaskGuid", ""); c.avatarMaskPath = source.value("avatarMaskPath", "");
		c.rootMotionPolicy = source.value("rootMotionPolicy", c.rootMotionPolicy); c.syncGroup = source.value("syncGroup", "");
		c.markerSync = source.value("markerSync", c.markerSync); return c;
	}
	case VansTimelineTrackType::AnimatorParameter:
	{
		VansTimelineAnimatorParameterTrackConfig c; c.parameterName = source.value("parameterName", ""); c.parameterType = source.value("parameterType", c.parameterType);
		c.firePolicy = source.value("firePolicy", c.firePolicy); c.seekPolicy = source.value("seekPolicy", c.seekPolicy);
		c.missingParameterPolicy = source.value("missingParameterPolicy", c.missingParameterPolicy); return c;
	}
	case VansTimelineTrackType::BoneOverride:
	{
		VansTimelineBoneOverrideTrackConfig c; c.bone = source.value("bone", ""); c.boneId = source.value("boneId", ""); c.weight = source.value("weight", c.weight);
		c.additive = source.value("additive", c.additive); c.space = source.value("space", c.space); c.ikTargetBindingId = source.value("ikTargetBindingId", "");
		c.poleBindingId = source.value("poleBindingId", ""); c.positionWeight = source.value("positionWeight", c.positionWeight);
		c.rotationWeight = source.value("rotationWeight", c.rotationWeight); c.clearOnExit = source.value("clearOnExit", c.clearOnExit); return c;
	}
	case VansTimelineTrackType::Audio:
	{
		VansTimelineAudioTrackConfig c; c.useBoundSource = source.value("useBoundSource", c.useBoundSource); c.volume = source.value("volume", c.volume);
		c.pitch = source.value("pitch", c.pitch); c.stereoPan = source.value("stereoPan", c.stereoPan); c.spatialBlend = source.value("spatialBlend", c.spatialBlend);
		c.fadeInSeconds = source.value("fadeInSeconds", c.fadeInSeconds); c.fadeOutSeconds = source.value("fadeOutSeconds", c.fadeOutSeconds);
		c.bus = source.value("bus", c.bus); c.reverbSend = source.value("reverbSend", c.reverbSend); c.referenceDistance = source.value("referenceDistance", c.referenceDistance);
		c.maxDistance = source.value("maxDistance", c.maxDistance); c.rolloff = source.value("rolloff", c.rolloff); c.seekPolicy = source.value("seekPolicy", c.seekPolicy);
		c.onSectionEnd = source.value("onSectionEnd", c.onSectionEnd); return c;
	}
	case VansTimelineTrackType::Media:
	{
		VansTimelineMediaTrackConfig c; c.syncMode = source.value("syncMode", c.syncMode); c.frameHold = source.value("frameHold", c.frameHold);
		c.colorSpace = source.value("colorSpace", c.colorSpace); c.outputAudio = source.value("outputAudio", c.outputAudio); c.targetKind = source.value("targetKind", c.targetKind);
		c.materialSlot = source.value("materialSlot", ""); c.uiElement = source.value("uiElement", ""); c.onSectionEnd = source.value("onSectionEnd", c.onSectionEnd); return c;
	}
	case VansTimelineTrackType::Particle:
	{
		VansTimelineParticleTrackConfig c; c.action = source.value("action", c.action); c.prewarmTicks = source.value("prewarmTicks", c.prewarmTicks);
		c.simulationRate = source.value("simulationRate", c.simulationRate); c.randomSeed = source.value("randomSeed", c.randomSeed);
		c.resetOnEnter = source.value("resetOnEnter", c.resetOnEnter); c.clearOnExit = source.value("clearOnExit", c.clearOnExit);
		c.loop = source.value("loop", c.loop); c.seekPolicy = source.value("seekPolicy", c.seekPolicy); return c;
	}
	case VansTimelineTrackType::CameraCut:
	{
		VansTimelineCameraCutTrackConfig c; c.cameraBindingId = source.value("cameraBindingId", ""); c.targetCameraBindingId = source.value("targetCameraBindingId", "");
		c.cutMode = source.value("cutMode", c.cutMode);
		c.blendDurationTicks = source.value("blendDurationTicks", c.blendDurationTicks); c.blendCurve = ReadBlendCurve(source.value("blendCurve", Json::object()));
		c.priority = source.value("priority", c.priority);
		c.aspectPolicy = source.value("aspectPolicy", c.aspectPolicy); c.viewport = source.value("viewport", c.viewport); c.shotName = source.value("shotName", "");
		c.shotColor = ReadColor(source.value("shotColor", Json::array()), c.shotColor); c.thumbnailCacheKey = source.value("thumbnailCacheKey", ""); return c;
	}
	case VansTimelineTrackType::CameraProperty:
	{
		VansTimelineCameraPropertyTrackConfig c; c.fieldOfView = source.value("fieldOfView", c.fieldOfView); c.nearClip = source.value("nearClip", c.nearClip);
		c.farClip = source.value("farClip", c.farClip); c.transform = source.value("transform", c.transform); return c;
	}
	case VansTimelineTrackType::CameraShake:
	{
		VansTimelineCameraShakeTrackConfig c; c.position = source.value("position", c.position);
		c.rotation = source.value("rotation", c.rotation); c.space = source.value("space", c.space);
		c.amplitudeScale = source.value("amplitudeScale", c.amplitudeScale);
		c.priority = source.value("priority", c.priority); return c;
	}
	case VansTimelineTrackType::FadePostProcess:
	{
		VansTimelineFadePostProcessTrackConfig c; c.mode = source.value("mode", c.mode); c.color.value = ReadArray<4>(source.value("color", Json::array()), c.color.value);
		c.gameViewOnly = source.value("gameViewOnly", c.gameViewOnly); c.profileGuid = source.value("profileGuid", ""); c.profilePath = source.value("profilePath", "");
		c.blendWeight = source.value("blendWeight", c.blendWeight); c.priority = source.value("priority", c.priority); return c;
	}
	case VansTimelineTrackType::Light:
	{
		VansTimelineLightTrackConfig c; c.color = source.value("color", c.color); c.temperature = source.value("temperature", c.temperature);
		c.intensity = source.value("intensity", c.intensity); c.range = source.value("range", c.range); c.cone = source.value("cone", c.cone);
		c.rectSize = source.value("rectSize", c.rectSize); c.shadow = source.value("shadow", c.shadow); return c;
	}
	case VansTimelineTrackType::MaterialParameter:
	{
		VansTimelineMaterialParameterTrackConfig c; c.materialSlotId = source.value("materialSlotId", ""); c.parameterName = source.value("parameterName", "");
		VansTimelineSerialization::TryParseChannelType(source.value("parameterType", "Float"), c.parameterType);
		c.instancePolicy = source.value("instancePolicy", c.instancePolicy); return c;
	}
	case VansTimelineTrackType::MaterialSwitch:
	{
		VansTimelineMaterialSwitchTrackConfig c; c.materialSlotId = source.value("materialSlotId", ""); return c;
	}
	case VansTimelineTrackType::UIState:
	{
		VansTimelineUIStateTrackConfig c; c.screen = source.value("screen", ""); c.targetKind = source.value("targetKind", c.targetKind);
		c.element = source.value("element", ""); c.descriptorId = source.value("descriptorId", ""); c.setterId = source.value("setterId", c.setterId);
		c.action = source.value("action", ""); return c;
	}
	case VansTimelineTrackType::EventSignal:
	{
		VansTimelineEventTrackConfig c; c.signalId = source.value("signalId", ""); c.displayName = source.value("displayName", ""); c.payloadType = source.value("payloadType", "");
		c.eventLane = source.value("eventLane", c.eventLane); c.firePolicy = source.value("firePolicy", c.firePolicy); c.seekPolicy = source.value("seekPolicy", c.seekPolicy);
		c.loopPolicy = source.value("loopPolicy", c.loopPolicy); c.editorSafe = source.value("editorSafe", c.editorSafe); c.oncePerPlayback = source.value("oncePerPlayback", c.oncePerPlayback); return c;
	}
	case VansTimelineTrackType::SubTimeline:
	{
		VansTimelineSubTimelineTrackConfig c; c.hierarchicalBias = source.value("hierarchicalBias", c.hierarchicalBias);
		c.useGlobalTimeDisplay = source.value("useGlobalTimeDisplay", c.useGlobalTimeDisplay); c.originTransformOverride = source.value("originTransformOverride", c.originTransformOverride);
		c.originPosition.value = ReadArray<3>(source.value("originPosition", Json::array())); c.originRotation.value = ReadArray<4>(source.value("originRotation", Json::array()), c.originRotation.value);
		c.originScale.value = ReadArray<3>(source.value("originScale", Json::array()), c.originScale.value); c.timeWarpChannel = source.value("timeWarpChannel", "");
		if (const auto remap = source.find("bindingRemap"); remap != source.end() && remap->is_array())
			for (const auto& entry : *remap) c.bindingRemap.push_back(ReadBindingOverride(entry));
		if (const auto parameters = source.find("parameterOverrides"); parameters != source.end() && parameters->is_object())
			for (const auto& [name, item] : parameters->items())
			{
				VansTimelineKeyValue parameter;
				if (item.is_boolean()) parameter = item.get<bool>(); else if (item.is_number_integer()) parameter = item.get<std::int64_t>();
				else if (item.is_number()) parameter = item.get<double>(); else if (item.is_string()) parameter = item.get<std::string>(); else continue;
				c.parameterOverrides.emplace(name, std::move(parameter));
			}
		return c;
	}
	case VansTimelineTrackType::Spawnable:
	{
		VansTimelineSpawnableTrackConfig c; c.spawnTemplateGuid = source.value("spawnTemplateGuid", ""); c.spawnTemplatePath = source.value("spawnTemplatePath", "");
		c.parentBindingId = source.value("parentBindingId", ""); c.spawnPolicy = source.value("spawnPolicy", c.spawnPolicy); c.destroyPolicy = source.value("destroyPolicy", c.destroyPolicy);
		c.prewarmTicks = source.value("prewarmTicks", c.prewarmTicks); c.exportBindingId = source.value("exportBindingId", ""); return c;
	}
	case VansTimelineTrackType::TimeScale:
	{
		VansTimelineTimeScaleTrackConfig c; c.scope = source.value("scope", c.scope); c.affectAudio = source.value("affectAudio", c.affectAudio);
		c.affectParticles = source.value("affectParticles", c.affectParticles); c.minimum = source.value("minimum", c.minimum); c.maximum = source.value("maximum", c.maximum);
		c.pauseAtZero = source.value("pauseAtZero", c.pauseAtZero); return c;
	}
	case VansTimelineTrackType::SceneState:
	{
		VansTimelineSceneStateTrackConfig c; c.sceneGuid = source.value("sceneGuid", ""); c.scenePath = source.value("scenePath", ""); c.action = source.value("action", c.action);
		c.preload = source.value("preload", c.preload); c.asyncPolicy = source.value("asyncPolicy", c.asyncPolicy); c.activationPriority = source.value("activationPriority", c.activationPriority); return c;
	}
	case VansTimelineTrackType::Custom:
	{
		VansTimelineCustomTrackConfig c; c.customTypeId = source.value("customTypeId", ""); c.payload = DecodeSerializedValueJson(source.value("payload", Json::object())); return c;
	}
	}
	return {};
}

Json ChannelJson(const VansTimelineChannel& channel)
{
	Json result = {
		{ "id", channel.id }, { "name", channel.name },
		{ "type", VansTimelineSerialization::ChannelTypeName(channel.type) },
		{ "preExtrapolation", EnumToName(channel.preExtrapolation, Extrapolations, "None") },
		{ "postExtrapolation", EnumToName(channel.postExtrapolation, Extrapolations, "None") },
		{ "keys", Json::array() }
	};
	for (const VansTimelineKey& key : channel.keys)
	{
		result["keys"].push_back({
			{ "id", key.id }, { "tick", key.tick }, { "value", KeyValueJson(key.value) },
			{ "interpolation", EnumToName(key.interpolation, Interpolations, "Auto") },
			{ "tangentMode", EnumToName(key.tangentMode, TangentModes, "Unified") },
			{ "arriveTangent", key.arriveTangent }, { "leaveTangent", key.leaveTangent },
			{ "arriveWeight", key.arriveWeight }, { "leaveWeight", key.leaveWeight }
		});
	}
	return result;
}

bool ReadChannel(const Json& value, VansTimelineChannel& channel, std::string& error)
{
	if (!value.is_object()) { error = "Timeline channel must be an object"; return false; }
	channel.id = value.value("id", ""); channel.name = value.value("name", "");
	if (!VansTimelineSerialization::TryParseChannelType(value.value("type", ""), channel.type))
	{
		error = "Unknown Timeline channel type"; return false;
	}
	ParseEnum(value.value("preExtrapolation", "None"), Extrapolations, channel.preExtrapolation);
	ParseEnum(value.value("postExtrapolation", "None"), Extrapolations, channel.postExtrapolation);
	const Json keys = value.value("keys", Json::array());
	if (!keys.is_array()) { error = "Timeline channel keys must be an array"; return false; }
	for (const Json& keyValue : keys)
	{
		if (!keyValue.is_object()) { error = "Timeline key must be an object"; return false; }
		VansTimelineKey key; key.id = keyValue.value("id", ""); key.tick = keyValue.value("tick", 0ll);
		if (!ReadKeyValue(keyValue.value("value", Json()), channel.type, key.value))
		{
			error = "Timeline key value does not match its channel type"; return false;
		}
		if (!ParseEnum(keyValue.value("interpolation", "Auto"), Interpolations, key.interpolation))
		{
			error = "Unknown Timeline key interpolation"; return false;
		}
		if (!ParseEnum(keyValue.value("tangentMode", "Unified"), TangentModes, key.tangentMode))
		{
			error = "Unknown Timeline key tangent mode"; return false;
		}
		key.arriveTangent = keyValue.value("arriveTangent", 0.0); key.leaveTangent = keyValue.value("leaveTangent", 0.0);
		key.arriveWeight = keyValue.value("arriveWeight", 0.0); key.leaveWeight = keyValue.value("leaveWeight", 0.0);
		channel.keys.push_back(std::move(key));
	}
	return true;
}

Json SectionJson(const VansTimelineSection& section)
{
	Json result = {
		{ "id", section.id }, { "name", section.name }, { "startTick", section.startTick },
		{ "durationTicks", section.durationTicks }, { "sourceInTick", section.sourceInTick },
		{ "sourceOutTick", section.sourceOutTick }, { "playRate", section.playRate }, { "reverse", section.reverse },
		{ "loopMode", EnumToName(section.loopMode, LoopModes, "None") }, { "loopCount", section.loopCount },
		{ "preRollTicks", section.preRollTicks }, { "postRollTicks", section.postRollTicks },
		{ "easeInTicks", section.easeInTicks }, { "easeOutTicks", section.easeOutTicks },
		{ "blendIn", BlendCurveJson(section.blendIn) }, { "blendOut", BlendCurveJson(section.blendOut) },
		{ "preExtrapolation", EnumToName(section.preExtrapolation, Extrapolations, "None") },
		{ "postExtrapolation", EnumToName(section.postExtrapolation, Extrapolations, "None") },
		{ "completionMode", EnumToName(section.completionMode, CompletionModes, "ProjectDefault") },
		{ "active", section.active }, { "locked", section.locked }, { "assetGuid", section.assetGuid },
		{ "assetPath", section.assetPath }, { "config", ConfigJson(section.config) }, { "channels", Json::array() }
	};
	for (const auto& channel : section.channels) result["channels"].push_back(ChannelJson(channel));
	return result;
}

bool ReadSection(const Json& value, VansTimelineTrackType trackType, VansTimelineSection& section, std::string& error)
{
	if (!value.is_object()) { error = "Timeline section must be an object"; return false; }
	section.id = value.value("id", ""); section.name = value.value("name", ""); section.startTick = value.value("startTick", 0ll);
	section.durationTicks = value.value("durationTicks", 1ll); section.sourceInTick = value.value("sourceInTick", 0ll);
	section.sourceOutTick = value.value("sourceOutTick", -1ll); section.playRate = value.value("playRate", 1.0); section.reverse = value.value("reverse", false);
	if (!ParseEnum(value.value("loopMode", "None"), LoopModes, section.loopMode)) { error = "Unknown Timeline section loop mode"; return false; }
	section.loopCount = value.value("loopCount", 1); section.preRollTicks = value.value("preRollTicks", 0ll); section.postRollTicks = value.value("postRollTicks", 0ll);
	section.easeInTicks = value.value("easeInTicks", 0ll); section.easeOutTicks = value.value("easeOutTicks", 0ll);
	section.blendIn = ReadBlendCurve(value.value("blendIn", Json::object())); section.blendOut = ReadBlendCurve(value.value("blendOut", Json::object()));
	ParseEnum(value.value("preExtrapolation", "None"), Extrapolations, section.preExtrapolation);
	ParseEnum(value.value("postExtrapolation", "None"), Extrapolations, section.postExtrapolation);
	if (!ParseEnum(value.value("completionMode", "ProjectDefault"), CompletionModes, section.completionMode)) { error = "Unknown Timeline section completion mode"; return false; }
	section.active = value.value("active", true); section.locked = value.value("locked", false); section.assetGuid = value.value("assetGuid", ""); section.assetPath = value.value("assetPath", "");
	section.config = ReadConfig(trackType, value.value("config", Json::object()));
	const Json channels = value.value("channels", Json::array());
	if (!channels.is_array()) { error = "Timeline section channels must be an array"; return false; }
	for (const Json& item : channels) { VansTimelineChannel channel; if (!ReadChannel(item, channel, error)) return false; section.channels.push_back(std::move(channel)); }
	return true;
}
}

const char* VansTimelineSerialization::TrackTypeName(VansTimelineTrackType type)
{
	return EnumToName(type, TrackTypes, "Transform");
}

bool VansTimelineSerialization::TryParseTrackType(const std::string& value, VansTimelineTrackType& type)
{
	return ParseEnum(value, TrackTypes, type);
}

const char* VansTimelineSerialization::ChannelTypeName(VansTimelineChannelType type)
{
	return EnumToName(type, ChannelTypes, "Float");
}

bool VansTimelineSerialization::TryParseChannelType(const std::string& value, VansTimelineChannelType& type)
{
	return ParseEnum(value, ChannelTypes, type);
}

bool VansTimelineSerialization::ContainsForbiddenFormatField(const Json& root, std::string& propertyPath)
{
	if (root.is_object())
	{
		for (const auto& [name, value] : root.items())
		{
			const std::string child = propertyPath + "/" + name;
			if (name == "version" || name == "schemaVersion" || name == "formatVersion")
			{
				propertyPath = child;
				return true;
			}
			std::string nested = child;
			if (ContainsForbiddenFormatField(value, nested)) { propertyPath = nested; return true; }
		}
	}
	else if (root.is_array())
	{
		for (std::size_t index = 0; index < root.size(); ++index)
		{
			std::string nested = propertyPath + "/" + std::to_string(index);
			if (ContainsForbiddenFormatField(root[index], nested)) { propertyPath = nested; return true; }
		}
	}
	return false;
}

bool VansTimelineSerialization::Decode(const Json& root, VansTimelineAsset& asset, std::string& error)
{
	error.clear(); asset = {};
	if (!root.is_object()) { error = "Timeline document root must be an object"; return false; }
	std::string forbiddenPath;
	if (ContainsForbiddenFormatField(root, forbiddenPath))
	{
		error = "Timeline authoring format forbids version fields at " + forbiddenPath;
		return false;
	}
	if (root.value("assetKind", "") != "Timeline") { error = "Timeline document assetKind must be Timeline"; return false; }
	asset.assetKind = "Timeline";
	const Json timebase = root.value("timebase", Json::object());
	if (!timebase.is_object()) { error = "Timeline timebase must be an object"; return false; }
	asset.timebase.ticksPerSecond = timebase.value("ticksPerSecond", asset.timebase.ticksPerSecond);
	asset.timebase.displayRateNumerator = timebase.value("displayRateNumerator", asset.timebase.displayRateNumerator);
	asset.timebase.displayRateDenominator = timebase.value("displayRateDenominator", asset.timebase.displayRateDenominator);
	asset.durationTicks = root.value("durationTicks", asset.durationTicks);
	asset.playbackRange = ReadRange(root.value("playbackRange", Json::object()), asset.playbackRange);
	asset.workRange = ReadRange(root.value("workRange", Json::object()), asset.workRange);
	if (!ParseEnum(root.value("defaultCompletionMode", "RestoreState"), CompletionModes, asset.defaultCompletionMode) ||
		!ParseEnum(root.value("defaultEvaluationMode", "WithSubTimelines"), EvaluationModes, asset.defaultEvaluationMode))
	{
		error = "Timeline document has an unknown default mode"; return false;
	}
	const Json bindings = root.value("bindings", Json::array());
	const Json groups = root.value("groups", Json::array());
	const Json tracks = root.value("tracks", Json::array());
	const Json markers = root.value("markers", Json::array());
	if (!bindings.is_array() || !groups.is_array() || !tracks.is_array() || !markers.is_array())
	{
		error = "Timeline bindings, groups, tracks and markers must be arrays"; return false;
	}
	for (const Json& value : bindings)
	{
		if (!value.is_object()) { error = "Timeline binding must be an object"; return false; }
		VansTimelineBinding binding; binding.id = value.value("id", ""); binding.displayName = value.value("displayName", "");
		if (!ParseEnum(value.value("kind", "SceneEntity"), BindingKinds, binding.kind)) { error = "Unknown Timeline binding kind"; return false; }
		binding.targetGuid = value.value("targetGuid", ""); binding.componentGuid = value.value("componentGuid", "");
		binding.componentTypeId = value.value("componentTypeId", 0); binding.assetGuid = value.value("assetGuid", "");
		binding.assetPath = value.value("assetPath", ""); binding.scenePathHint = value.value("scenePathHint", ""); binding.required = value.value("required", true);
		asset.bindings.push_back(std::move(binding));
	}
	for (const Json& value : groups)
	{
		if (!value.is_object()) { error = "Timeline group must be an object"; return false; }
		VansTimelineGroup group; group.id = value.value("id", ""); group.parentId = value.value("parentId", ""); group.name = value.value("name", "");
		group.color = ReadColor(value.value("color", Json::array()), group.color); group.order = value.value("order", 0); group.locked = value.value("locked", false);
		asset.groups.push_back(std::move(group));
	}
	for (const Json& value : tracks)
	{
		if (!value.is_object()) { error = "Timeline track must be an object"; return false; }
		VansTimelineTrack track; track.id = value.value("id", ""); track.name = value.value("name", "");
		if (!TryParseTrackType(value.value("type", ""), track.type)) { error = "Unknown Timeline track type"; return false; }
		track.bindingId = value.value("bindingId", ""); track.groupId = value.value("groupId", ""); track.enabled = value.value("enabled", true);
		track.runtimeMuted = value.value("runtimeMuted", false); track.locked = value.value("locked", false); track.order = value.value("order", 0); track.priority = value.value("priority", 0);
		if (!ParseEnum(value.value("blendMode", "Override"), BlendModes, track.blendMode) ||
			!ParseEnum(value.value("completionMode", "ProjectDefault"), CompletionModes, track.completionMode))
		{
			error = "Timeline track has an unknown blend or completion mode"; return false;
		}
		const Json condition = value.value("condition", Json::object());
		if (condition.is_object())
		{
			track.condition.parameter = condition.value("parameter", ""); track.condition.negate = condition.value("negate", false);
			const Json expected = condition.value("expectedValue", Json());
			if (expected.is_boolean()) track.condition.expectedValue = expected.get<bool>();
			else if (expected.is_number_integer()) track.condition.expectedValue = expected.get<std::int64_t>();
			else if (expected.is_number()) track.condition.expectedValue = expected.get<double>();
			else if (expected.is_string()) track.condition.expectedValue = expected.get<std::string>();
		}
		track.config = ReadConfig(track.type, value.value("config", Json::object()));
		const Json display = value.value("display", Json::object());
		if (display.is_object())
		{
			track.display.color = ReadColor(display.value("color", Json::array()), track.display.color);
			track.display.rowHeight = display.value("rowHeight", track.display.rowHeight); track.display.note = display.value("note", "");
		}
		const Json sections = value.value("sections", Json::array());
		if (!sections.is_array()) { error = "Timeline track sections must be an array"; return false; }
		for (const Json& sectionValue : sections)
		{
			VansTimelineSection section; if (!ReadSection(sectionValue, track.type, section, error)) return false;
			track.sections.push_back(std::move(section));
		}
		asset.tracks.push_back(std::move(track));
	}
	for (const Json& value : markers)
	{
		if (!value.is_object()) { error = "Timeline marker must be an object"; return false; }
		VansTimelineMarker marker; marker.id = value.value("id", ""); marker.tick = value.value("tick", 0ll); marker.label = value.value("label", "");
		marker.color = ReadColor(value.value("color", Json::array()), marker.color); marker.category = value.value("category", ""); marker.determinismFence = value.value("determinismFence", false);
		asset.markers.push_back(std::move(marker));
	}
	const Json metadata = root.value("metadata", Json::object());
	if (metadata.is_object())
	{
		asset.metadata.displayName = metadata.value("displayName", ""); asset.metadata.description = metadata.value("description", "");
		if (const auto tags = metadata.find("tags"); tags != metadata.end() && tags->is_array())
			for (const Json& tag : *tags) if (tag.is_string()) asset.metadata.tags.push_back(tag.get<std::string>());
	}
	return true;
}

VansTimelineSerialization::Json VansTimelineSerialization::Encode(const VansTimelineAsset& source)
{
	VansTimelineAsset asset = source;
	Normalize(asset);
	Json root = {
		{ "assetKind", "Timeline" },
		{ "timebase", { { "ticksPerSecond", asset.timebase.ticksPerSecond },
			{ "displayRateNumerator", asset.timebase.displayRateNumerator },
			{ "displayRateDenominator", asset.timebase.displayRateDenominator } } },
		{ "durationTicks", asset.durationTicks }, { "playbackRange", RangeJson(asset.playbackRange) },
		{ "workRange", RangeJson(asset.workRange) },
		{ "defaultCompletionMode", EnumToName(asset.defaultCompletionMode, CompletionModes, "RestoreState") },
		{ "defaultEvaluationMode", EnumToName(asset.defaultEvaluationMode, EvaluationModes, "WithSubTimelines") },
		{ "bindings", Json::array() }, { "groups", Json::array() }, { "tracks", Json::array() }, { "markers", Json::array() },
		{ "metadata", { { "displayName", asset.metadata.displayName }, { "description", asset.metadata.description }, { "tags", asset.metadata.tags } } }
	};
	for (const auto& binding : asset.bindings)
		root["bindings"].push_back({ { "id", binding.id }, { "displayName", binding.displayName },
			{ "kind", EnumToName(binding.kind, BindingKinds, "SceneEntity") }, { "targetGuid", binding.targetGuid },
			{ "componentGuid", binding.componentGuid }, { "componentTypeId", binding.componentTypeId },
			{ "assetGuid", binding.assetGuid }, { "assetPath", binding.assetPath },
			{ "scenePathHint", binding.scenePathHint }, { "required", binding.required } });
	for (const auto& group : asset.groups)
		root["groups"].push_back({ { "id", group.id }, { "parentId", group.parentId }, { "name", group.name },
			{ "color", ColorJson(group.color) }, { "order", group.order }, { "locked", group.locked } });
	for (const auto& track : asset.tracks)
	{
		Json value = { { "id", track.id }, { "type", TrackTypeName(track.type) }, { "name", track.name },
			{ "bindingId", track.bindingId }, { "groupId", track.groupId }, { "enabled", track.enabled },
			{ "runtimeMuted", track.runtimeMuted }, { "locked", track.locked }, { "order", track.order },
			{ "priority", track.priority }, { "blendMode", EnumToName(track.blendMode, BlendModes, "Override") },
			{ "completionMode", EnumToName(track.completionMode, CompletionModes, "ProjectDefault") },
			{ "condition", { { "parameter", track.condition.parameter }, { "expectedValue", KeyValueJson(track.condition.expectedValue) }, { "negate", track.condition.negate } } },
			{ "config", ConfigJson(track.config) },
			{ "display", { { "color", ColorJson(track.display.color) }, { "rowHeight", track.display.rowHeight }, { "note", track.display.note } } },
			{ "sections", Json::array() } };
		for (const auto& section : track.sections) value["sections"].push_back(SectionJson(section));
		root["tracks"].push_back(std::move(value));
	}
	for (const auto& marker : asset.markers)
		root["markers"].push_back({ { "id", marker.id }, { "tick", marker.tick }, { "label", marker.label },
			{ "color", ColorJson(marker.color) }, { "category", marker.category }, { "determinismFence", marker.determinismFence } });
	return root;
}

bool VansTimelineSerialization::Load(const std::filesystem::path& path, VansTimelineAsset& asset, std::string& error)
{
	Json root;
	return VansJsonFileStorage::Read(path, root, error) && Decode(root, asset, error);
}

bool VansTimelineSerialization::SaveAtomic(const std::filesystem::path& path, const VansTimelineAsset& asset, std::string& error)
{
	return VansJsonFileStorage::WriteAtomic(path, Encode(asset), error);
}

VansSerializedValue VansTimelineSerialization::EncodeSerialized(const VansTimelineAsset& asset)
{
	return DecodeSerializedValueJson(Encode(asset));
}

bool VansTimelineSerialization::DecodeSerialized(const VansSerializedValue& root, VansTimelineAsset& asset, std::string& error)
{
	return Decode(EncodeSerializedValueJson<Json>(root), asset, error);
}

void VansTimelineSerialization::Normalize(VansTimelineAsset& asset)
{
	asset.assetKind = "Timeline";
	asset.timebase.ticksPerSecond = std::max<std::int64_t>(1, asset.timebase.ticksPerSecond);
	asset.timebase.displayRateNumerator = std::max<std::int32_t>(1, asset.timebase.displayRateNumerator);
	asset.timebase.displayRateDenominator = std::max<std::int32_t>(1, asset.timebase.displayRateDenominator);
	asset.playbackRange.endTick = std::max(asset.playbackRange.startTick, asset.playbackRange.endTick);
	asset.workRange.endTick = std::max(asset.workRange.startTick, asset.workRange.endTick);
	asset.durationTicks = std::max({ asset.durationTicks, asset.playbackRange.endTick, asset.workRange.endTick, VansTimelineTick{ 1 } });
	for (auto& track : asset.tracks)
	{
		std::stable_sort(track.sections.begin(), track.sections.end(), [](const auto& left, const auto& right)
		{
			if (left.startTick != right.startTick) return left.startTick < right.startTick;
			return left.id < right.id;
		});
		for (auto& section : track.sections)
		{
			if (section.playRate < 0.0) { section.playRate = -section.playRate; section.reverse = !section.reverse; }
			for (auto& channel : section.channels)
				std::stable_sort(channel.keys.begin(), channel.keys.end(), [](const auto& left, const auto& right)
				{
					if (left.tick != right.tick) return left.tick < right.tick;
					return left.id < right.id;
				});
		}
	}
	std::stable_sort(asset.tracks.begin(), asset.tracks.end(), [](const auto& left, const auto& right)
	{
		if (left.order != right.order) return left.order < right.order;
		return left.id < right.id;
	});
	std::stable_sort(asset.markers.begin(), asset.markers.end(), [](const auto& left, const auto& right)
	{
		if (left.tick != right.tick) return left.tick < right.tick;
		return left.id < right.id;
	});
}
}
