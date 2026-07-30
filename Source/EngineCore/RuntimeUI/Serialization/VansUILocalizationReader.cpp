#include "VansUILocalizationReader.h"

#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>

namespace VansRuntime
{
	bool VansUILocalizationReader::Read(
		const Vans::VansSerializedValue& root,
		VansUILocalizationConfig& config,
		std::vector<std::string>& diagnostics)
	{
		if (root.kind != Vans::VansSerializedValue::Kind::Object)
		{
			diagnostics.push_back("UI localization root must be an object.");
			return false;
		}

		config.schemaVersion = static_cast<std::uint32_t>(
			std::max<std::int64_t>(1, Vans::ReadSerializedIntField(root, "schemaVersion", 1)));
		config.locale = Vans::ReadSerializedStringField(root, "locale", "default");

		const Vans::VansSerializedValue* strings = Vans::FindObjectField(root, "strings");
		if (strings && strings->kind == Vans::VansSerializedValue::Kind::Object)
		{
			for (const auto& [key, value] : strings->objectFields)
			{
				if (value.kind == Vans::VansSerializedValue::Kind::String)
					config.strings.emplace(key, value.stringValue);
			}
		}

		if (config.locale.empty())
			diagnostics.push_back("UI localization locale must not be empty.");
		if (config.strings.empty())
			diagnostics.push_back("UI localization strings must not be empty.");
		return diagnostics.empty();
	}
}
