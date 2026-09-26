#include "VansSceneTimelineComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/VansAssetReference.h"
#include "VansComponentTypeCatalog.h"

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

bool ParsePlayOn(const std::string& value, VansTimelinePlayOn& out)
{
	if (value == "Manual") out = VansTimelinePlayOn::Manual;
	else if (value == "Awake") out = VansTimelinePlayOn::Awake;
	else if (value == "Enable") out = VansTimelinePlayOn::Enable;
	else if (value == "Signal") out = VansTimelinePlayOn::Signal;
	else return false;
	return true;
}

bool ParseBindingRootMode(const std::string& value, VansTimelineBindingRootMode& out)
{
	if (value == "OwnerRelative") out = VansTimelineBindingRootMode::OwnerRelative;
	else if (value == "World") out = VansTimelineBindingRootMode::World;
	else return false;
	return true;
}

bool ParseLoopMode(const std::string& value, VansTimelineLoopMode& out)
{
	if (value == "None") out = VansTimelineLoopMode::None;
	else if (value == "Loop") out = VansTimelineLoopMode::Loop;
	else if (value == "PingPong") out = VansTimelineLoopMode::PingPong;
	else return false;
	return true;
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

bool VansSceneTimelineComponentReader::ReadFromAuthoringEntity(
	const VansSerializedValue& entity,
	std::optional<VansSceneTimelineComponentConfig>& outConfig,
	std::string& error)
{
	outConfig.reset();
	const VansSerializedValue* component = FindTimelineComponent(entity);
	if (!component) return true;
	VansSceneTimelineComponentConfig config;
	if (!ReadAuthoringComponent(*component, config, error)) return false;
	outConfig = std::move(config);
	return true;
}

bool VansSceneTimelineComponentReader::ReadAuthoringComponent(
	const VansSerializedValue& component,
	VansSceneTimelineComponentConfig& outConfig,
	std::string& error)
{
	VansSceneTimelineComponentConfig config;
	config.enabled = ReadSerializedBoolField(component, "enabled", true);
	const VansSerializedValue* data = ObjectField(component, "data");
	if (!data) { outConfig = std::move(config); return true; }
	const VansSerializedValue* timeline = FindObjectField(*data, "timeline");
	if (!timeline) { outConfig = std::move(config); return true; }
	std::optional<VansAssetGuid> timelineGuid;
	if (!TryReadOptionalAssetGuidReference(*timeline, timelineGuid))
	{
		error = "Timeline.timeline must be an object containing exactly one guid";
		return false;
	}
	if (timelineGuid)
		config.timelineAssetGuid = timelineGuid->ToString();
	const std::string playOn = ReadSerializedStringField(*data, "playOn", "Manual");
	if (!ParsePlayOn(playOn, config.instance.playOn))
	{
		error = "Timeline.playOn has invalid value '" + playOn + "'";
		return false;
	}
	const std::string bindingRootMode = ReadSerializedStringField(*data, "bindingRootMode", "OwnerRelative");
	if (!ParseBindingRootMode(bindingRootMode, config.instance.bindingRootMode))
	{
		error = "Timeline.bindingRootMode has invalid value '" + bindingRootMode + "'";
		return false;
	}
	const std::string loopMode = ReadSerializedStringField(*data, "loopMode", "None");
	if (!ParseLoopMode(loopMode, config.instance.loopMode))
	{
		error = "Timeline.loopMode has invalid value '" + loopMode + "'";
		return false;
	}
	config.instance.loopCount = static_cast<std::int32_t>(ReadSerializedIntField(*data, "loopCount", 1));
	if (const VansSerializedValue* playbackSpeed = FindObjectField(*data, "playbackSpeed"))
		config.instance.playbackSpeed = ReadSerializedNumber(*playbackSpeed, 1.0);
	config.instance.restoreStateOnStop = ReadSerializedBoolField(*data, "restoreStateOnStop", true);

	if (const VansSerializedValue* overrides = ArrayField(*data, "bindingOverrides"))
	{
		for (const auto& source : overrides->arrayItems)
		{
			if (source.kind != VansSerializedValue::Kind::Object) continue;
			if (FindObjectField(source, "targetComponentTypeId"))
			{
				error = "Timeline.bindingOverrides uses obsolete integer targetComponentTypeId";
				return false;
			}
			VansTimelineBindingOverride overrideValue;
			const std::string bindingName = ReadSerializedStringField(source, "bindingId");
			overrideValue.bindingId = VansMakeStableId<VansTimelineBindingTag>(bindingName);
			const std::string entity = ReadSerializedStringField(source, "targetEntity");
			overrideValue.useOwner = entity == "owner";
			if (!overrideValue.useOwner) overrideValue.targetEntityGuid = entity;
			overrideValue.targetComponentGuid = ReadSerializedStringField(source, "targetComponent");
			const std::string componentType = ReadSerializedStringField(source, "targetComponentType");
			if (!componentType.empty())
			{
				const VansComponentTypeDescriptor* descriptor = VansComponentTypeCatalog::Find(componentType);
				if (!descriptor || descriptor->runtimeTypeId == VansInvalidComponentTypeId)
				{
					error = "Timeline.bindingOverrides has unknown targetComponentType '" + componentType + "'";
					return false;
				}
				overrideValue.targetComponentType = std::string(descriptor->runtimeKey);
			}
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
	outConfig = std::move(config);
	return true;
}
}
