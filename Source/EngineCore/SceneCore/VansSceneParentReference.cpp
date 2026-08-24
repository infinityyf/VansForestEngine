#include "VansSceneParentReference.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <unordered_set>

namespace Vans
{
namespace
{
bool HasExactFields(
	const VansSerializedValue& value,
	std::initializer_list<const char*> required)
{
	if (value.kind != VansSerializedValue::Kind::Object
		|| value.objectFields.size() != required.size())
		return false;
	std::unordered_set<std::string> fields;
	for (const char* field : required)
		fields.emplace(field);
	for (const auto& [name, ignored] : value.objectFields)
		if (fields.erase(name) == 0)
			return false;
	return fields.empty();
}

bool ReadGuidField(const VansSerializedValue& value, const char* name, VansAssetGuid& guid)
{
	const VansSerializedValue* field = FindObjectField(value, name);
	return field && field->kind == VansSerializedValue::Kind::String
		&& VansAssetGuid::TryParse(field->stringValue, guid);
}
}

const char* SceneParentKindName(VansSceneParentKind kind)
{
	switch (kind)
	{
	case VansSceneParentKind::Entity: return "entity";
	case VansSceneParentKind::Bone: return "bone";
	case VansSceneParentKind::Socket: return "socket";
	}
	return nullptr;
}

bool VansSceneParentReference::IsValid() const
{
	return entityGuid.IsValid()
		&& (kind == VansSceneParentKind::Entity
			|| (animationComponentGuid.IsValid() && anchorGuid.IsValid()));
}

bool TryReadSceneParentReference(
	const VansSerializedValue& value,
	VansSceneParentReference& outReference,
	std::string& error)
{
	outReference = {};
	error.clear();
	const VansSerializedValue* kindField = FindObjectField(value, "kind");
	if (!kindField || kindField->kind != VansSerializedValue::Kind::String)
	{
		error = "Scene parent requires a string kind";
		return false;
	}
	if (kindField->stringValue == "entity")
		outReference.kind = VansSceneParentKind::Entity;
	else if (kindField->stringValue == "bone")
		outReference.kind = VansSceneParentKind::Bone;
	else if (kindField->stringValue == "socket")
		outReference.kind = VansSceneParentKind::Socket;
	else
	{
		error = "Scene parent kind must be entity, bone, or socket";
		return false;
	}

	if (outReference.kind == VansSceneParentKind::Entity)
	{
		if (!HasExactFields(value, { "kind", "entityGuid" })
			|| !ReadGuidField(value, "entityGuid", outReference.entityGuid))
		{
			error = "Entity parent requires exactly kind and entityGuid";
			return false;
		}
		return true;
	}

	if (!HasExactFields(value,
		{ "kind", "entityGuid", "animationComponentGuid", "anchorGuid" })
		|| !ReadGuidField(value, "entityGuid", outReference.entityGuid)
		|| !ReadGuidField(value, "animationComponentGuid", outReference.animationComponentGuid)
		|| !ReadGuidField(value, "anchorGuid", outReference.anchorGuid))
	{
		error = "Bone/socket parent requires exactly kind, entityGuid, animationComponentGuid, and anchorGuid";
		return false;
	}
	return true;
}

VansSerializedValue WriteSceneParentReference(const VansSceneParentReference& reference)
{
	std::vector<std::pair<std::string, VansSerializedValue>> fields = {
		{ "kind", VansSerializedValue::String(SceneParentKindName(reference.kind)) },
		{ "entityGuid", VansSerializedValue::String(reference.entityGuid.ToString()) }
	};
	if (reference.IsAnchor())
	{
		fields.emplace_back("animationComponentGuid",
			VansSerializedValue::String(reference.animationComponentGuid.ToString()));
		fields.emplace_back("anchorGuid",
			VansSerializedValue::String(reference.anchorGuid.ToString()));
	}
	return VansSerializedValue::Object(std::move(fields));
}

std::string ReadSceneParentEntityGuid(const VansSerializedValue& parentValue)
{
	if (parentValue.kind == VansSerializedValue::Kind::Null)
		return {};
	VansSceneParentReference parent;
	std::string error;
	return TryReadSceneParentReference(parentValue, parent, error)
		? parent.entityGuid.ToString() : std::string{};
}

VansSerializedValue WriteEntityParentReference(const std::string& entityGuid)
{
	if (entityGuid.empty())
		return VansSerializedValue::Null();
	VansSceneParentReference parent;
	if (!VansAssetGuid::TryParse(entityGuid, parent.entityGuid))
		return VansSerializedValue::Null();
	return WriteSceneParentReference(parent);
}
}
