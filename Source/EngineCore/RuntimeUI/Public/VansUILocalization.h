#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace VansRuntime
{
	struct VansUILocalizationConfig
	{
		std::uint32_t schemaVersion = 1;
		std::string locale;
		std::unordered_map<std::string, std::string> strings;
		std::string sourceConfigPath;
	};
}
