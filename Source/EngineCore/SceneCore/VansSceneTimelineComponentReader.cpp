#include "VansSceneTimelineComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>

namespace Vans
{
namespace
{
const VansSerializedValue* ObjectField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
}

const VansSerializedValue* ArrayField(const VansSerializedValue& object, const char* key)
{
	const VansSerializedValue* field = FindObjectField(object, key);
	return field && field->kind == VansSerializedValue::Kind::Array ? field : nullptr;
}

const VansSerializedValue* FindTimelineComponent(const VansSerializedValue& entity)
{
	const VansSerializedValue* components = ArrayField(entity, "components");
	if (!components) return nullptr;
	const auto found = std::find_if(components->arrayItems.begin(), components->arrayItems.end(), [](const auto& component)
	{
		return ReadSerializedStringField(component, "type") == "Timeline";
	});
	return found == components->arrayItems.end() ? nullptr : &*found;
}

VansTimelinePlayOn ParsePlayOn(const std::string& value)
{
	if (value == "Awake") return VansTimelinePlayOn::Awake;
	if (value == "Enable") return VansTimelinePlayOn::Enable;
	if (value == "Signal") return VansTimelinePlayOn::Signal;
	return VansTimelinePlayOn::Manual;
}

VansTimelineUpdateMode ParseUpdateMode(const std::string& value)
{
	if (value == "UnscaledTime") return VansTimelineUpdateMode::UnscaledTime;
	if (value == "Manual") return VansTimelineUpdateMode::Manual;
	return VansTimelineUpdateMode::GameTime;
}

VansTimelineBindingRootMode ParseBindingRootMode(const std::string& value)
{
	return value == "World" ? VansTimelineBindingRootMode::World : VansTimelineBindingRootMode::OwnerRelative;
}

VansTimelineLoopMode ParseLoopMode(const std::string& value)
{
	if (value == "Loop") return VansTimelineLoopMode::Loop;
	if (value == "PingPong") return VansTimelineLoopMode::PingPong;
	return VansTimelineLoopMode::None;
}

VansTimelineKeyValue DecodeParameterValue(const VansSerializedValue& value)
{
	switch (value.kind)
	{
	case VansSerializedValue::Kind::Bool: return value.boolValue;
	case VansSerializedValue::Kind::Int: return value.intValue;
	case VansSerializedValue::Kind::Float: return value.floatValue;
	case VansSerializedValue::Kind::String: return value.stringValue;
	case VansSerializedValue::Kind::Object:
	{
		const std::string guid = ReadSerializedStringField(value, "guid");
		const std::string path = ReadSerializedStringField(value, "path");
		if (!guid.empty() || !path.empty())
			return VansTimelineObjectReference{ guid, path, ReadSerializedStringField(value, "objectKind") };
		return std::monostate{};
	}
	default: return std::monostate{};
	}
}
}

std::optional<VansSceneTimelineComponentConfig>
VansSceneTimelineComponentReader::ReadFromAuthoringEntity(const VansSerializedValue& entity)
{
	const VansSerializedValue* component = FindTimelineComponent(entity);
	if (!component) return std::nullopt;
	return ReadAuthoringComponent(*component);
}

VansSceneTimelineComponentConfig VansSceneTimelineComponentReader::ReadAuthoringComponent(
	const VansSerializedValue& component)
{
	VansSceneTimelineComponentConfig config;
	config.enabled = ReadSerializedBoolField(component, "enabled", true);
	const VansSerializedValue* data = ObjectField(component, "data");
	if (!data) return config;
	const VansSerializedValue* timeline = ObjectField(*data, "timeline");
	if (!timeline) return config;
	config.timelineAssetGuid = ReadSerializedStringField(*timeline, "guid");
	config.timelineAssetPath = ReadSerializedStringField(*timeline, "path");
	config.instance.playOn = ParsePlayOn(ReadSerializedStringField(*data, "playOn", "Manual"));
	config.instance.updateMode = ParseUpdateMode(ReadSerializedStringField(*data, "updateMode", "GameTime"));
	config.instance.bindingRootMode = ParseBindingRootMode(ReadSerializedStringField(*data, "bindingRootMode", "OwnerRelative"));
	config.instance.loopMode = ParseLoopMode(ReadSerializedStringField(*data, "loopMode", "None"));
	config.instance.loopCount = static_cast<std::int32_t>(ReadSerializedIntField(*data, "loopCount", 1));
	if (const VansSerializedValue* playbackSpeed = FindObjectField(*data, "playbackSpeed"))
		config.instance.playbackSpeed = ReadSerializedNumber(*playbackSpeed, 1.0);
	config.instance.restoreStateOnStop = ReadSerializedBoolField(*data, "restoreStateOnStop", true);

	if (const VansSerializedValue* overrides = ArrayField(*data, "bindingOverrides"))
	{
		for (const auto& source : overrides->arrayItems)
		{
			if (source.kind != VansSerializedValue::Kind::Object) continue;
			VansTimelineBindingOverride overrideValue;
			const std::string bindingName = ReadSerializedStringField(source, "bindingId");
			overrideValue.bindingId = VansMakeStableId<VansTimelineBindingTag>(bindingName);
			const std::string entity = ReadSerializedStringField(source, "targetEntity");
			overrideValue.useOwner = entity == "owner";
			if (!overrideValue.useOwner) overrideValue.targetEntityGuid = entity;
			overrideValue.targetComponentGuid = ReadSerializedStringField(source, "targetComponent");
			overrideValue.targetComponentTypeId = static_cast<std::uint16_t>(
				ReadSerializedIntField(source, "targetComponentTypeId", 0));
			if (overrideValue.bindingId) config.instance.bindingOverrides.push_back(std::move(overrideValue));
		}
	}
	if (const VansSerializedValue* parameters = ObjectField(*data, "parameters"))
	{
		for (const auto& [name, value] : parameters->objectFields)
			config.instance.parameterOverrides.push_back({
				VansMakeStableId<VansTimelineParameterTag>(name), DecodeParameterValue(value) });
	}
	config.valid = !config.timelineAssetGuid.empty();
	return config;
}
}
