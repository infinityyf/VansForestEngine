#pragma once

#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "VansGameplaySchemaTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansGameplayPropertyKind : std::uint8_t
{
	Bool,
	Int,
	Float,
	String,
	Enum,
	Object,
	Array,
	Tag,
	TagQuery,
	AssetReference,
	Payload,
	Graph,
	Vec2,
	Vec3,
	Vec4,
	Quaternion,
	Color,
	Map,
	EntityBinding,
	ComponentBinding
};

struct VansGameplayPropertySchema
{
	VansActionFieldId fieldId;
	std::string path;
	std::string displayName;
	std::string group;
	std::string description;
	std::string unit;
	VansGameplayPropertyKind kind = VansGameplayPropertyKind::String;
	VansSerializedValue defaultValue;
	bool required = false;
	bool cook = true;
	bool deprecated = false;
	bool readOnly = false;
	double minimum = 0.0;
	double maximum = 0.0;
	double step = 0.0;
	bool hasMinimum = false;
	bool hasMaximum = false;
	bool hasStep = false;
	std::vector<std::string> enumValues;
	std::vector<VansAssetType> allowedAssetTypes;
	bool hasArrayElement = false;
	VansGameplayPropertyKind arrayElementKind = VansGameplayPropertyKind::Object;
	VansSerializedValue arrayElementDefault = VansSerializedValue::Object({});
	// Object children and object-array element members use a local JSON member
	// name in path. Top-level fields continue to use absolute JSON Pointers.
	std::vector<VansGameplayPropertySchema> children;
	std::string visibleWhenPath;
	VansSerializedValue visibleWhenValue;
};

struct VansGameplayAssetSchemaDescriptor
{
	VansAssetType assetType = VansAssetType::Unknown;
	std::string assetKind;
	std::string extension;
	std::uint32_t schemaVersion = 1;
	bool editorOnly = false;
	std::vector<VansGameplayPropertySchema> fields;
};

class VansGameplayAssetSchemaRegistry
{
public:
	bool Register(VansGameplayAssetSchemaDescriptor descriptor, std::string& error);
	bool Seal(std::string& error);
	const VansGameplayAssetSchemaDescriptor* Resolve(VansAssetType type) const;
	const VansGameplayAssetSchemaDescriptor* ResolveKind(std::string_view assetKind) const;
	VansGameplayDiagnostics Validate(VansAssetType type, const VansSerializedValue& root) const;
	std::vector<std::string> CollectDependencies(VansAssetType type, const VansSerializedValue& root) const;
	VansSerializedValue CreateDefault(VansAssetType type) const;
	bool IsSealed() const { return m_Sealed; }
	static const VansGameplayAssetSchemaRegistry& BuiltIns();
	static bool IsGameplayAssetType(VansAssetType type);

private:
	bool m_Sealed = false;
	std::unordered_map<VansAssetType, VansGameplayAssetSchemaDescriptor> m_ByType;
	std::unordered_map<std::string, VansAssetType> m_ByKind;
};
}
