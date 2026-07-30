#include "VansUIThemeTokensReader.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <utility>

namespace VansRuntime
{
	namespace
	{
		VansUIVariant ReadVariant(const Vans::VansSerializedValue& value)
		{
			switch (value.kind)
			{
			case Vans::VansSerializedValue::Kind::Bool:
				return VansUIVariant(value.boolValue);
			case Vans::VansSerializedValue::Kind::Int:
				return VansUIVariant(value.intValue);
			case Vans::VansSerializedValue::Kind::Float:
				return VansUIVariant(value.floatValue);
			case Vans::VansSerializedValue::Kind::String:
				return VansUIVariant(value.stringValue);
			default:
				return VansUIVariant();
			}
		}

		void ReadTokenGroup(
			const Vans::VansSerializedValue& root,
			const char* groupName,
			VansUIVariantMap& outGroup)
		{
			const Vans::VansSerializedValue* group = Vans::FindObjectField(root, groupName);
			if (!group || group->kind != Vans::VansSerializedValue::Kind::Object)
				return;

			for (const auto& [name, value] : group->objectFields)
				outGroup.emplace(name, ReadVariant(value));
		}
	}

	bool VansUIThemeTokensReader::Read(
		const Vans::VansSerializedValue& root,
		VansUIThemeTokensConfig& config,
		std::vector<std::string>& diagnostics)
	{
		if (root.kind != Vans::VansSerializedValue::Kind::Object)
		{
			diagnostics.push_back("UI theme tokens root must be an object.");
			return false;
		}

		config.schemaVersion = static_cast<std::uint32_t>(
			std::max<std::int64_t>(1, Vans::ReadSerializedIntField(root, "schemaVersion", 1)));
		config.name = Vans::ReadSerializedStringField(root, "name", "ThemeTokens");
		ReadTokenGroup(root, "colors", config.colors);
		ReadTokenGroup(root, "font", config.font);
		ReadTokenGroup(root, "spacing", config.spacing);
		ReadTokenGroup(root, "motion", config.motion);

		if (config.colors.empty() && config.font.empty() && config.spacing.empty() && config.motion.empty())
			diagnostics.push_back("UI theme tokens must contain at least one token group.");

		return diagnostics.empty();
	}
}
