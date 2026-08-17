#include "VansTimelineSerialization.h"

#include "VansTimelineSourceSchema.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace Vans
{
namespace
{
using Json = VansTimelineSerialization::Json;

template <typename Enum>
struct EnumName { Enum value; const char* name; };

template <typename Enum, std::size_t Count>
const char* EnumToName(Enum value, const EnumName<Enum>(&names)[Count], const char* fallback)
{
	for (const auto& item : names) if (item.value == value) return item.name;
	return fallback;
}

template <typename Enum, std::size_t Count>
bool ParseEnum(const std::string& value, const EnumName<Enum>(&names)[Count], Enum& result)
{
	for (const auto& item : names) if (value == item.name) { result = item.value; return true; }
	return false;
}

constexpr EnumName<VansTimelineValueType> ValueTypes[] = {
	{ VansTimelineValueType::Null, "Null" }, { VansTimelineValueType::Bool, "Bool" },
	{ VansTimelineValueType::Int32, "Int32" }, { VansTimelineValueType::Int64, "Int64" },
	{ VansTimelineValueType::Float, "Float" }, { VansTimelineValueType::Double, "Double" },
	{ VansTimelineValueType::Enum, "Enum" }, { VansTimelineValueType::String, "String" },
	{ VansTimelineValueType::Vec2, "Vec2" }, { VansTimelineValueType::Vec3, "Vec3" },
	{ VansTimelineValueType::Vec4, "Vec4" }, { VansTimelineValueType::Quaternion, "Quaternion" },
	{ VansTimelineValueType::ColorLinear, "ColorLinear" }, { VansTimelineValueType::ColorSrgb, "ColorSrgb" },
	{ VansTimelineValueType::ObjectReference, "ObjectReference" }, { VansTimelineValueType::Struct, "Struct" }
};
constexpr EnumName<VansTimelineBindingKind> BindingKinds[] = {
	{ VansTimelineBindingKind::SceneEntity, "SceneEntity" }, { VansTimelineBindingKind::SceneComponent, "SceneComponent" },
	{ VansTimelineBindingKind::RuntimeObject, "RuntimeObject" }, { VansTimelineBindingKind::Asset, "Asset" },
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
	{ VansTimelineCompletionMode::RestoreState, "RestoreState" }, { VansTimelineCompletionMode::KeepState, "KeepState" }
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

Json ValueJson(const VansTimelineValue& value)
{
	return EncodeSerializedValueJson<Json>(VansTimelineEncodeSourceValue(value));
}

bool ReadValue(const Json& source, VansTimelineValueType type, VansTimelineValue& value)
{
	return VansTimelineDecodeSourceValue(DecodeSerializedValueJson(source), type, value);
}

Json BlendCurveJson(const VansTimelineBlendCurve& curve)
{
	return { { "shape", curve.shape }, { "exponent", curve.exponent } };
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

Json BindingJson(const VansTimelineBinding& binding)
{
	return {
		{ "id", binding.id }, { "displayName", binding.displayName },
		{ "kind", EnumToName(binding.kind, BindingKinds, "SceneEntity") },
		{ "targetGuid", binding.targetGuid }, { "componentGuid", binding.componentGuid },
		{ "componentTypeId", binding.componentTypeId }, { "assetGuid", binding.assetGuid },
		{ "assetPath", binding.assetPath }, { "scenePathHint", binding.scenePathHint },
		{ "required", binding.required }
	};
}

bool ReadBinding(const Json& source, VansTimelineBinding& binding)
{
	if (!source.is_object()) return false;
	binding.id = source.value("id", "");
	binding.stableId = VansMakeStableId<VansTimelineBindingTag>(binding.id);
	binding.displayName = source.value("displayName", "");
	if (!ParseEnum(source.value("kind", "SceneEntity"), BindingKinds, binding.kind)) return false;
	binding.targetGuid = source.value("targetGuid", "");
	binding.componentGuid = source.value("componentGuid", "");
	binding.componentTypeId = source.value("componentTypeId", std::uint16_t{});
	binding.assetGuid = source.value("assetGuid", "");
	binding.assetPath = source.value("assetPath", "");
	binding.scenePathHint = source.value("scenePathHint", "");
	binding.required = source.value("required", true);
	return !binding.id.empty();
}

Json ParameterJson(const VansTimelineParameterDescriptor& parameter)
{
	return { { "id", parameter.id.value }, { "name", parameter.name },
		{ "type", VansTimelineSerialization::ValueTypeName(parameter.type) },
		{ "defaultValue", ValueJson(parameter.defaultValue) }, { "readOnly", parameter.readOnly } };
}

bool ReadParameter(const Json& source, VansTimelineParameterDescriptor& parameter)
{
	if (!source.is_object()) return false;
	parameter.name = source.value("name", "");
	parameter.id.value = source.value("id", std::uint64_t{});
	if (!parameter.id) parameter.id = VansMakeStableId<VansTimelineParameterTag>(parameter.name);
	if (!VansTimelineSerialization::TryParseValueType(source.value("type", "Null"), parameter.type)) return false;
	if (const auto value = source.find("defaultValue"); value != source.end())
		if (!ReadValue(*value, parameter.type, parameter.defaultValue)) return false;
	parameter.readOnly = source.value("readOnly", false);
	return parameter.id.IsValid() && !parameter.name.empty();
}

Json ChannelJson(const VansTimelineChannel& channel)
{
	Json keys = Json::array();
	for (const VansTimelineKey& key : channel.keys)
		keys.push_back({ { "id", key.id }, { "tick", key.tick }, { "value", ValueJson(key.value) },
			{ "interpolation", EnumToName(key.interpolation, Interpolations, "Auto") },
			{ "tangentMode", EnumToName(key.tangentMode, TangentModes, "Unified") },
			{ "arriveTangent", key.arriveTangent }, { "leaveTangent", key.leaveTangent },
			{ "arriveWeight", key.arriveWeight }, { "leaveWeight", key.leaveWeight } });
	return { { "id", channel.id }, { "name", channel.name },
		{ "type", VansTimelineSerialization::ValueTypeName(channel.type) },
		{ "preExtrapolation", EnumToName(channel.preExtrapolation, Extrapolations, "None") },
		{ "postExtrapolation", EnumToName(channel.postExtrapolation, Extrapolations, "None") },
		{ "keys", std::move(keys) } };
}

bool ReadChannel(const Json& source, VansTimelineChannel& channel)
{
	if (!source.is_object()) return false;
	channel.id = source.value("id", ""); channel.name = source.value("name", "");
	if (!VansTimelineSerialization::TryParseValueType(source.value("type", "Float"), channel.type)) return false;
	if (!ParseEnum(source.value("preExtrapolation", "None"), Extrapolations, channel.preExtrapolation) ||
		!ParseEnum(source.value("postExtrapolation", "None"), Extrapolations, channel.postExtrapolation)) return false;
	const auto keys = source.find("keys");
	if (keys != source.end() && keys->is_array()) for (const Json& item : *keys)
	{
		VansTimelineKey key;
		key.id = item.value("id", ""); key.tick = item.value("tick", VansTimelineTick{});
		if (!ParseEnum(item.value("interpolation", "Auto"), Interpolations, key.interpolation) ||
			!ParseEnum(item.value("tangentMode", "Unified"), TangentModes, key.tangentMode)) return false;
		key.arriveTangent = item.value("arriveTangent", 0.0); key.leaveTangent = item.value("leaveTangent", 0.0);
		key.arriveWeight = item.value("arriveWeight", 0.0); key.leaveWeight = item.value("leaveWeight", 0.0);
		const auto value = item.find("value");
		if (value == item.end() || !ReadValue(*value, channel.type, key.value)) return false;
		channel.keys.push_back(std::move(key));
	}
	return !channel.id.empty();
}

Json SectionJson(const VansTimelineSection& section)
{
	Json channels = Json::array(); for (const auto& channel : section.channels) channels.push_back(ChannelJson(channel));
	Json ranges = Json::array(); for (const auto& range : section.ranges)
		ranges.push_back({ { "id", range.id }, { "startTick", range.startTick }, { "endTick", range.endTick },
			{ "extensionData", EncodeSerializedValueJson<Json>(range.extensionData) } });
	Json result{
		{ "id", section.id }, { "name", section.name }, { "startTick", section.startTick },
		{ "durationTicks", section.durationTicks }, { "sourceInTick", section.sourceInTick },
		{ "sourceOutTick", section.sourceOutTick }, { "playRate", section.playRate },
		{ "reverse", section.reverse }, { "loopMode", EnumToName(section.loopMode, LoopModes, "None") },
		{ "loopCount", section.loopCount }, { "preRollTicks", section.preRollTicks },
		{ "postRollTicks", section.postRollTicks }, { "easeInTicks", section.easeInTicks },
		{ "easeOutTicks", section.easeOutTicks }, { "blendIn", BlendCurveJson(section.blendIn) },
		{ "blendOut", BlendCurveJson(section.blendOut) },
		{ "preExtrapolation", EnumToName(section.preExtrapolation, Extrapolations, "None") },
		{ "postExtrapolation", EnumToName(section.postExtrapolation, Extrapolations, "None") },
		{ "completionMode", EnumToName(section.completionMode, CompletionModes, "ProjectDefault") },
		{ "active", section.active }, { "locked", section.locked },
		{ "assetGuid", section.assetGuid }, { "assetPath", section.assetPath },
		{ "channels", std::move(channels) }, { "ranges", std::move(ranges) }
	};
	if (section.extensionData)
		result["extensionData"] = EncodeSerializedValueJson<Json>(*section.extensionData);
	return result;
}

bool ReadSection(const Json& source, VansTimelineSection& section)
{
	if (!source.is_object()) return false;
	section.id = source.value("id", ""); section.name = source.value("name", "");
	section.startTick = source.value("startTick", VansTimelineTick{});
	section.durationTicks = source.value("durationTicks", VansTimelineTick{ 1 });
	section.sourceInTick = source.value("sourceInTick", VansTimelineTick{});
	section.sourceOutTick = source.value("sourceOutTick", VansTimelineTick{ -1 });
	section.playRate = source.value("playRate", 1.0); section.reverse = source.value("reverse", false);
	if (!ParseEnum(source.value("loopMode", "None"), LoopModes, section.loopMode) ||
		!ParseEnum(source.value("preExtrapolation", "None"), Extrapolations, section.preExtrapolation) ||
		!ParseEnum(source.value("postExtrapolation", "None"), Extrapolations, section.postExtrapolation) ||
		!ParseEnum(source.value("completionMode", "ProjectDefault"), CompletionModes, section.completionMode)) return false;
	section.loopCount = source.value("loopCount", 1); section.preRollTicks = source.value("preRollTicks", VansTimelineTick{});
	section.postRollTicks = source.value("postRollTicks", VansTimelineTick{});
	section.easeInTicks = source.value("easeInTicks", VansTimelineTick{}); section.easeOutTicks = source.value("easeOutTicks", VansTimelineTick{});
	section.blendIn = ReadBlendCurve(source.value("blendIn", Json::object()));
	section.blendOut = ReadBlendCurve(source.value("blendOut", Json::object()));
	section.active = source.value("active", true); section.locked = source.value("locked", false);
	section.assetGuid = source.value("assetGuid", ""); section.assetPath = source.value("assetPath", "");
	if (const auto data = source.find("extensionData"); data != source.end()) section.extensionData = DecodeSerializedValueJson(*data);
	if (const auto channels = source.find("channels"); channels != source.end() && channels->is_array())
		for (const Json& item : *channels) { VansTimelineChannel channel; if (!ReadChannel(item, channel)) return false; section.channels.push_back(std::move(channel)); }
	if (const auto ranges = source.find("ranges"); ranges != source.end() && ranges->is_array())
		for (const Json& item : *ranges)
		{
			VansTimelineRange range; range.id = item.value("id", ""); range.startTick = item.value("startTick", VansTimelineTick{});
			range.endTick = item.value("endTick", VansTimelineTick{ 1 });
			if (const auto data = item.find("extensionData"); data != item.end()) range.extensionData = DecodeSerializedValueJson(*data);
			section.ranges.push_back(std::move(range));
		}
	return !section.id.empty();
}

Json TrackJson(const VansTimelineTrack& track)
{
	Json sections = Json::array(); for (const auto& section : track.sections) sections.push_back(SectionJson(section));
	return {
		{ "id", track.id }, { "type", track.type.stableName },
		{ "name", track.name }, { "bindingId", track.bindingId }, { "groupId", track.groupId },
		{ "enabled", track.enabled }, { "locked", track.locked },
		{ "order", track.order }, { "priority", track.priority },
		{ "blendMode", EnumToName(track.blendMode, BlendModes, "Override") },
		{ "completionMode", EnumToName(track.completionMode, CompletionModes, "ProjectDefault") },
		{ "condition", { { "parameterId", track.condition.parameterId.value },
			{ "expectedValue", ValueJson(track.condition.expectedValue) }, { "negate", track.condition.negate } } },
		{ "extensionData", EncodeSerializedValueJson<Json>(track.extensionData) },
		{ "display", { { "color", ColorJson(track.display.color) }, { "rowHeight", track.display.rowHeight },
			{ "note", track.display.note } } }, { "sections", std::move(sections) }
	};
}

bool ReadTrack(const Json& source, VansTimelineTrack& track)
{
	if (!source.is_object()) return false;
	track.id = source.value("id", "");
	track.type = VansTimelineTrackTypeRef::FromName(source.value("type", ""));
	track.name = source.value("name", ""); track.bindingId = source.value("bindingId", ""); track.groupId = source.value("groupId", "");
	track.enabled = source.value("enabled", true); track.locked = source.value("locked", false);
	track.order = source.value("order", 0); track.priority = source.value("priority", 0);
	if (!ParseEnum(source.value("blendMode", "Override"), BlendModes, track.blendMode) ||
		!ParseEnum(source.value("completionMode", "ProjectDefault"), CompletionModes, track.completionMode)) return false;
	if (const auto condition = source.find("condition"); condition != source.end() && condition->is_object())
	{
		track.condition.parameterId.value = condition->value("parameterId", std::uint64_t{});
		track.condition.negate = condition->value("negate", false);
		if (const auto value = condition->find("expectedValue"); value != condition->end())
		{
			if (value->is_boolean()) track.condition.expectedValue = value->get<bool>();
			else if (value->is_number_integer()) track.condition.expectedValue = value->get<std::int64_t>();
			else if (value->is_number()) track.condition.expectedValue = value->get<double>();
			else if (value->is_string()) track.condition.expectedValue = value->get<std::string>();
		}
	}
	const auto extension = source.find("extensionData");
	if (extension == source.end()) return false;
	track.extensionData = DecodeSerializedValueJson(*extension);
	if (const auto display = source.find("display"); display != source.end() && display->is_object())
	{
		track.display.color = ReadColor(display->value("color", Json::array()), track.display.color);
		track.display.rowHeight = display->value("rowHeight", track.display.rowHeight);
		track.display.note = display->value("note", "");
	}
	if (const auto sections = source.find("sections"); sections != source.end() && sections->is_array())
		for (const Json& item : *sections) { VansTimelineSection section; if (!ReadSection(item, section)) return false; track.sections.push_back(std::move(section)); }
	return !track.id.empty() && !track.type.stableName.empty();
}
}

const char* VansTimelineSerialization::ValueTypeName(VansTimelineValueType type)
{
	return EnumToName(type, ValueTypes, "Null");
}

bool VansTimelineSerialization::TryParseValueType(const std::string& value, VansTimelineValueType& type)
{
	return ParseEnum(value, ValueTypes, type);
}

bool VansTimelineSerialization::Decode(const Json& root, VansTimelineAsset& asset, std::string& error)
{
	error.clear();
	if (!root.is_object()) { error = "Timeline document root must be an object"; return false; }
	if (root.value("assetKind", "") != "Timeline") { error = "Timeline assetKind is missing or invalid"; return false; }
	const Json& current = root;
	VansTimelineAsset decoded;
	if (const auto timebase = current.find("timebase"); timebase != current.end() && timebase->is_object())
	{
		decoded.timebase.ticksPerSecond = timebase->value("ticksPerSecond", decoded.timebase.ticksPerSecond);
		decoded.timebase.displayRateNumerator = timebase->value("displayRateNumerator", decoded.timebase.displayRateNumerator);
		decoded.timebase.displayRateDenominator = timebase->value("displayRateDenominator", decoded.timebase.displayRateDenominator);
	}
	decoded.durationTicks = current.value("durationTicks", decoded.durationTicks);
	auto readRange = [](const Json& source, VansTimelineTickRange& range)
	{
		if (!source.is_object()) return;
		range.startTick = source.value("startTick", range.startTick); range.endTick = source.value("endTick", range.endTick);
	};
	if (const auto range = current.find("playbackRange"); range != current.end()) readRange(*range, decoded.playbackRange);
	if (const auto range = current.find("workRange"); range != current.end()) readRange(*range, decoded.workRange);
	if (!ParseEnum(current.value("defaultCompletionMode", "RestoreState"), CompletionModes, decoded.defaultCompletionMode) ||
		!ParseEnum(current.value("defaultEvaluationMode", "WithSubTimelines"), EvaluationModes, decoded.defaultEvaluationMode))
	{
		error = "Timeline document contains an invalid default mode"; return false;
	}
	if (const auto parameters = current.find("parameters"); parameters != current.end() && parameters->is_array())
		for (const Json& item : *parameters) { VansTimelineParameterDescriptor parameter; if (!ReadParameter(item, parameter)) { error = "Invalid Timeline parameter"; return false; } decoded.parameters.push_back(std::move(parameter)); }
	if (const auto bindings = current.find("bindings"); bindings != current.end() && bindings->is_array())
		for (const Json& item : *bindings) { VansTimelineBinding binding; if (!ReadBinding(item, binding)) { error = "Invalid Timeline binding"; return false; } decoded.bindings.push_back(std::move(binding)); }
	if (const auto groups = current.find("groups"); groups != current.end() && groups->is_array())
		for (const Json& item : *groups)
		{
			VansTimelineGroup group; group.id = item.value("id", ""); group.parentId = item.value("parentId", ""); group.name = item.value("name", "");
			group.color = ReadColor(item.value("color", Json::array()), group.color); group.order = item.value("order", 0); group.locked = item.value("locked", false);
			decoded.groups.push_back(std::move(group));
		}
	if (const auto tracks = current.find("tracks"); tracks != current.end() && tracks->is_array())
		for (const Json& item : *tracks) { VansTimelineTrack track; if (!ReadTrack(item, track)) { error = "Invalid Timeline track"; return false; } decoded.tracks.push_back(std::move(track)); }
	if (const auto markers = current.find("markers"); markers != current.end() && markers->is_array())
		for (const Json& item : *markers)
		{
			VansTimelineMarker marker; marker.id = item.value("id", ""); marker.tick = item.value("tick", VansTimelineTick{}); marker.label = item.value("label", "");
			marker.color = ReadColor(item.value("color", Json::array()), marker.color); marker.category = item.value("category", "");
			marker.determinismFence = item.value("determinismFence", false); marker.runtimeObservable = item.value("runtimeObservable", false);
			marker.editorSafe = item.value("editorSafe", false); marker.firePolicy = item.value("firePolicy", marker.firePolicy);
			marker.payloadType.value = item.value("payloadType", std::uint64_t{});
			if (const auto payload = item.find("payload"); payload != item.end()) marker.payload = DecodeSerializedValueJson(*payload);
			decoded.markers.push_back(std::move(marker));
		}
	if (const auto metadata = current.find("metadata"); metadata != current.end() && metadata->is_object())
	{
		decoded.metadata.displayName = metadata->value("displayName", ""); decoded.metadata.description = metadata->value("description", "");
		decoded.metadata.tags = metadata->value("tags", std::vector<std::string>{});
	}
	Normalize(decoded); asset = std::move(decoded); return true;
}

VansTimelineSerialization::Json VansTimelineSerialization::Encode(const VansTimelineAsset& source)
{
	VansTimelineAsset asset = source; Normalize(asset);
	Json root{
		{ "assetKind", "Timeline" },
		{ "timebase", { { "ticksPerSecond", asset.timebase.ticksPerSecond },
			{ "displayRateNumerator", asset.timebase.displayRateNumerator },
			{ "displayRateDenominator", asset.timebase.displayRateDenominator } } },
		{ "durationTicks", asset.durationTicks },
		{ "playbackRange", { { "startTick", asset.playbackRange.startTick }, { "endTick", asset.playbackRange.endTick } } },
		{ "workRange", { { "startTick", asset.workRange.startTick }, { "endTick", asset.workRange.endTick } } },
		{ "defaultCompletionMode", EnumToName(asset.defaultCompletionMode, CompletionModes, "RestoreState") },
		{ "defaultEvaluationMode", EnumToName(asset.defaultEvaluationMode, EvaluationModes, "WithSubTimelines") },
		{ "parameters", Json::array() }, { "bindings", Json::array() }, { "groups", Json::array() },
		{ "tracks", Json::array() }, { "markers", Json::array() },
		{ "metadata", { { "displayName", asset.metadata.displayName }, { "description", asset.metadata.description }, { "tags", asset.metadata.tags } } }
	};
	for (const auto& parameter : asset.parameters) root["parameters"].push_back(ParameterJson(parameter));
	for (const auto& binding : asset.bindings) root["bindings"].push_back(BindingJson(binding));
	for (const auto& group : asset.groups) root["groups"].push_back({ { "id", group.id }, { "parentId", group.parentId }, { "name", group.name }, { "color", ColorJson(group.color) }, { "order", group.order }, { "locked", group.locked } });
	for (const auto& track : asset.tracks) root["tracks"].push_back(TrackJson(track));
	for (const auto& marker : asset.markers) root["markers"].push_back({ { "id", marker.id }, { "tick", marker.tick }, { "label", marker.label },
		{ "color", ColorJson(marker.color) }, { "category", marker.category }, { "determinismFence", marker.determinismFence },
		{ "runtimeObservable", marker.runtimeObservable }, { "editorSafe", marker.editorSafe }, { "firePolicy", marker.firePolicy },
		{ "payloadType", marker.payloadType.value }, { "payload", EncodeSerializedValueJson<Json>(marker.payload) } });
	return root;
}

bool VansTimelineSerialization::Load(const std::filesystem::path& path, VansTimelineAsset& asset, std::string& error)
{
	Json root; if (!VansJsonFileStorage::Read(path, root, error)) return false; return Decode(root, asset, error);
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
	asset.timebase.displayRateNumerator = std::max(1, asset.timebase.displayRateNumerator);
	asset.timebase.displayRateDenominator = std::max(1, asset.timebase.displayRateDenominator);
	asset.durationTicks = std::max<VansTimelineTick>(1, asset.durationTicks);
	asset.playbackRange.startTick = std::clamp(asset.playbackRange.startTick, VansTimelineTick{}, asset.durationTicks);
	asset.playbackRange.endTick = std::clamp(asset.playbackRange.endTick, asset.playbackRange.startTick, asset.durationTicks);
	asset.workRange.startTick = std::clamp(asset.workRange.startTick, VansTimelineTick{}, asset.durationTicks);
	asset.workRange.endTick = std::clamp(asset.workRange.endTick, asset.workRange.startTick, asset.durationTicks);
	for (auto& binding : asset.bindings) binding.stableId = VansMakeStableId<VansTimelineBindingTag>(binding.id);
	for (auto& parameter : asset.parameters) if (!parameter.id) parameter.id = VansMakeStableId<VansTimelineParameterTag>(parameter.name);
	for (auto& track : asset.tracks)
	{
		track.type.typeId = VansMakeStableId<VansTimelineTrackTypeTag>(track.type.stableName);
		std::stable_sort(track.sections.begin(), track.sections.end(), [](const auto& left, const auto& right)
		{
			if (left.startTick != right.startTick) return left.startTick < right.startTick; return left.id < right.id;
		});
		for (auto& section : track.sections)
		{
			section.durationTicks = std::max<VansTimelineTick>(1, section.durationTicks);
			for (auto& channel : section.channels)
				std::stable_sort(channel.keys.begin(), channel.keys.end(), [](const auto& left, const auto& right)
				{ if (left.tick != right.tick) return left.tick < right.tick; return left.id < right.id; });
		}
	}
	std::stable_sort(asset.tracks.begin(), asset.tracks.end(), [](const auto& left, const auto& right)
	{ if (left.order != right.order) return left.order < right.order; return left.id < right.id; });
	std::stable_sort(asset.markers.begin(), asset.markers.end(), [](const auto& left, const auto& right)
	{ if (left.tick != right.tick) return left.tick < right.tick; return left.id < right.id; });
}
}
