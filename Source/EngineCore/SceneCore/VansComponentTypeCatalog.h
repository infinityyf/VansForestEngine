#pragma once

#include "../RuntimeCore/VansRuntimeComponentTypeId.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Vans
{
struct VansSerializedValue;

using VansComponentDefaultDataFactory = VansSerializedValue (*)();

enum class VansComponentAssetReferenceStorage : std::uint8_t
{
	GuidString,
	GuidObject
};

struct VansComponentAssetReferenceRule
{
	std::string_view parentKey;
	std::string_view fieldKey;
	std::string_view assetType;
	VansComponentAssetReferenceStorage storage = VansComponentAssetReferenceStorage::GuidObject;
};

enum class VansComponentTypeTrait : std::uint32_t
{
	None = 0,
	Light = 1u << 0,
	CameraMedia = 1u << 1,
	Particle = 1u << 2,
	MultiMeshRoot = 1u << 3,
	Animation = 1u << 4,
};

struct VansComponentTypeDescriptor
{
	std::string_view authoringType;
	std::string_view runtimeKey;
	std::uint16_t runtimeTypeId = VansInvalidComponentTypeId;
	VansComponentTypeTrait traits = VansComponentTypeTrait::None;
	bool sceneAuthoringSupported = true;
	bool inspectorAddable = false;
	bool singleton = false;
	VansComponentDefaultDataFactory defaultDataFactory = nullptr;
	std::array<std::string_view, 3> aliases{};
	std::string_view sceneAuthoringAlias;
	std::array<std::string_view, 2> runtimeTypeAliases{};
	std::array<VansComponentAssetReferenceRule, 3> assetReferenceRules{};
	std::uint8_t assetReferenceRuleCount = 0;
};

class VansComponentTypeCatalog final
{
public:
	static const std::array<VansComponentTypeDescriptor, 27>& All();

	static const VansComponentTypeDescriptor* Find(std::string_view name)
	{
		for (const VansComponentTypeDescriptor& descriptor : All())
		{
			if (EqualsIgnoreCase(name, descriptor.authoringType) ||
				EqualsIgnoreCase(name, descriptor.runtimeKey) ||
				(!descriptor.sceneAuthoringAlias.empty() &&
				 EqualsIgnoreCase(name, descriptor.sceneAuthoringAlias)))
				return &descriptor;
			for (std::string_view alias : descriptor.aliases)
				if (!alias.empty() && EqualsIgnoreCase(name, alias))
					return &descriptor;
		}
		return nullptr;
	}

	static bool IsSceneAuthoringType(std::string_view name)
	{
		for (const VansComponentTypeDescriptor& descriptor : All())
		{
			if (!descriptor.sceneAuthoringSupported)
				continue;
			if (name == descriptor.authoringType ||
				(!descriptor.sceneAuthoringAlias.empty() && name == descriptor.sceneAuthoringAlias))
				return true;
		}
		return false;
	}

	static bool HasTrait(std::string_view name, VansComponentTypeTrait trait)
	{
		const VansComponentTypeDescriptor* descriptor = Find(name);
		return descriptor &&
			(static_cast<std::uint32_t>(descriptor->traits) &
			 static_cast<std::uint32_t>(trait)) != 0;
	}

	static std::string CanonicalRuntimeKey(std::string name)
	{
		if (const VansComponentTypeDescriptor* descriptor = Find(name))
			return std::string(descriptor->runtimeKey);
		std::transform(name.begin(), name.end(), name.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return name;
	}

	static std::uint16_t RuntimeTypeId(std::string_view name)
	{
		for (const VansComponentTypeDescriptor& descriptor : All())
		{
			if (name == descriptor.runtimeKey)
				return descriptor.runtimeTypeId;
			for (std::string_view alias : descriptor.runtimeTypeAliases)
				if (!alias.empty() && name == alias)
					return descriptor.runtimeTypeId;
		}
		return VansInvalidComponentTypeId;
	}

	static VansSerializedValue CreateDefaultData(std::string_view name);
	static const VansComponentAssetReferenceRule* FindAssetReferenceRule(
		std::string_view authoringType,
		std::string_view parentKey,
		std::string_view fieldKey);

private:
	static bool EqualsIgnoreCase(std::string_view left, std::string_view right)
	{
		return left.size() == right.size() &&
			std::equal(left.begin(), left.end(), right.begin(),
				[](unsigned char a, unsigned char b)
				{
					return std::tolower(a) == std::tolower(b);
				});
	}
};

inline std::string CanonicalRuntimeComponentKeyForName(std::string componentName)
{
	return VansComponentTypeCatalog::CanonicalRuntimeKey(std::move(componentName));
}

inline std::uint16_t VansRuntimeComponentTypeIdForKey(const std::string& key)
{
	return VansComponentTypeCatalog::RuntimeTypeId(key);
}
}
