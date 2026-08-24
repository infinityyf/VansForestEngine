#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/VansAssetGuid.h"

#include <string>

namespace Vans
{
enum class VansSceneParentKind : std::uint8_t
{
	Entity,
	Bone,
	Socket
};

struct VansSceneParentReference
{
	VansSceneParentKind kind = VansSceneParentKind::Entity;
	VansEntityGuid entityGuid;
	VansComponentGuid animationComponentGuid;
	VansAssetGuid anchorGuid;

	bool IsEntity() const { return kind == VansSceneParentKind::Entity; }
	bool IsAnchor() const { return !IsEntity(); }
	bool IsValid() const;
};

bool TryReadSceneParentReference(
	const VansSerializedValue& value,
	VansSceneParentReference& outReference,
	std::string& error);
VansSerializedValue WriteSceneParentReference(const VansSceneParentReference& reference);
std::string ReadSceneParentEntityGuid(const VansSerializedValue& parentValue);
VansSerializedValue WriteEntityParentReference(const std::string& entityGuid);
const char* SceneParentKindName(VansSceneParentKind kind);
}
