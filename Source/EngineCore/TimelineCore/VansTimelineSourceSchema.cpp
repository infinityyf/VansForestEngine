#include "VansTimelineSourceSchema.h"

#include "VansTimelineSerialization.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <type_traits>

namespace Vans
{
const VansSerializedValue* VansTimelineFindSourceField(
	const VansSerializedValue& object,
	const std::string& name)
{
	return FindObjectField(object, name);
}

bool VansTimelineDecodeSourceValue(
	const VansSerializedValue& source,
	VansTimelineValueType type,
	VansTimelineValue& value)
{
	switch (type)
	{
	case VansTimelineValueType::Null:
		if (source.kind != VansSerializedValue::Kind::Null) return false;
		value = std::monostate{}; return true;
	case VansTimelineValueType::Bool:
		if (source.kind != VansSerializedValue::Kind::Bool) return false;
		value = source.boolValue; return true;
	case VansTimelineValueType::Int32:
		if (source.kind != VansSerializedValue::Kind::Int) return false;
		value = static_cast<std::int32_t>(source.intValue); return true;
	case VansTimelineValueType::Int64:
		if (source.kind != VansSerializedValue::Kind::Int) return false;
		value = source.intValue; return true;
	case VansTimelineValueType::Float:
		if (source.kind != VansSerializedValue::Kind::Float && source.kind != VansSerializedValue::Kind::Int) return false;
		value = static_cast<float>(ReadSerializedNumber(source)); return true;
	case VansTimelineValueType::Double:
		if (source.kind != VansSerializedValue::Kind::Float && source.kind != VansSerializedValue::Kind::Int) return false;
		value = ReadSerializedNumber(source); return true;
	case VansTimelineValueType::Enum:
	case VansTimelineValueType::String:
		if (source.kind != VansSerializedValue::Kind::String) return false;
		value = source.stringValue; return true;
	case VansTimelineValueType::Vec2:
	case VansTimelineValueType::Vec3:
	case VansTimelineValueType::Vec4:
	case VansTimelineValueType::Quaternion:
	case VansTimelineValueType::ColorLinear:
	case VansTimelineValueType::ColorSrgb:
	{
		if (source.kind != VansSerializedValue::Kind::Array) return false;
		const std::size_t count = type == VansTimelineValueType::Vec2 ? 2 :
			(type == VansTimelineValueType::Vec3 ? 3 : 4);
		if (source.arrayItems.size() != count) return false;
		std::array<double, 4> values{};
		if (type == VansTimelineValueType::Quaternion) values[3] = 1.0;
		if (type == VansTimelineValueType::ColorLinear || type == VansTimelineValueType::ColorSrgb) values[3] = 1.0;
		for (std::size_t index = 0; index < count; ++index)
		{
			const auto& item = source.arrayItems[index];
			if (item.kind != VansSerializedValue::Kind::Float && item.kind != VansSerializedValue::Kind::Int) return false;
			values[index] = ReadSerializedNumber(item);
		}
		if (type == VansTimelineValueType::Vec2) value = VansTimelineVec2{ { values[0], values[1] } };
		else if (type == VansTimelineValueType::Vec3) value = VansTimelineVec3{ { values[0], values[1], values[2] } };
		else if (type == VansTimelineValueType::Vec4) value = VansTimelineVec4{ values };
		else if (type == VansTimelineValueType::Quaternion) value = VansTimelineQuaternion{ values };
		else if (type == VansTimelineValueType::ColorLinear) value = VansTimelineColorLinear{ values };
		else value = VansTimelineColorSrgb{ values };
		return true;
	}
	case VansTimelineValueType::ObjectReference:
	{
		if (source.kind != VansSerializedValue::Kind::Object) return false;
		value = VansTimelineObjectReference{
			ReadSerializedStringField(source, "guid"),
			ReadSerializedStringField(source, "path"),
			ReadSerializedStringField(source, "objectKind") };
		return true;
	}
	case VansTimelineValueType::Struct:
		value = VansTimelineStructValue{ {}, source };
		return true;
	}
	return false;
}

VansSerializedValue VansTimelineEncodeSourceValue(const VansTimelineValue& value)
{
	return std::visit([](const auto& item) -> VansSerializedValue
	{
		using Type = std::decay_t<decltype(item)>;
		if constexpr (std::is_same_v<Type, std::monostate>) return VansSerializedValue::Null();
		else if constexpr (std::is_same_v<Type, bool>) return VansSerializedValue::Bool(item);
		else if constexpr (std::is_same_v<Type, std::int32_t> || std::is_same_v<Type, std::int64_t>)
			return VansSerializedValue::Int(item);
		else if constexpr (std::is_same_v<Type, float> || std::is_same_v<Type, double>)
			return VansSerializedValue::Float(item);
		else if constexpr (std::is_same_v<Type, std::string>) return VansSerializedValue::String(item);
		else if constexpr (std::is_same_v<Type, VansTimelineObjectReference>)
			return VansSerializedValue::Object({ { "guid", VansSerializedValue::String(item.guid) },
				{ "path", VansSerializedValue::String(item.path) },
				{ "objectKind", VansSerializedValue::String(item.objectKind) } });
		else if constexpr (std::is_same_v<Type, VansTimelineStructValue>) return item.value;
		else
		{
			std::vector<VansSerializedValue> values;
			for (double component : item.value) values.push_back(VansSerializedValue::Float(component));
			return VansSerializedValue::Array(std::move(values));
		}
	}, value);
}

VansTimelineValueType VansTimelineResolveChannelType(
	const VansTimelineChannelSchema& channel,
	const VansSerializedValue& extensionData)
{
	if (channel.typeField.empty()) return channel.type;
	const VansSerializedValue* source = VansTimelineFindSourceField(extensionData, channel.typeField);
	if (!source || source->kind != VansSerializedValue::Kind::String) return channel.type;
	for (const auto& [name, type] : channel.typeCases)
		if (name == source->stringValue) return type;
	VansTimelineValueType resolved = channel.type;
	return VansTimelineSerialization::TryParseValueType(source->stringValue, resolved)
		? resolved : channel.type;
}
}
