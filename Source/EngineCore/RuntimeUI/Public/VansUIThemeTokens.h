#pragma once

#include "VansUIVariant.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace VansRuntime
{
	struct VansUIThemeTokensConfig
	{
		std::uint32_t schemaVersion = 1;
		std::string name;
		VansUIVariantMap colors;
		VansUIVariantMap font;
		VansUIVariantMap spacing;
		VansUIVariantMap motion;
		std::string sourceConfigPath;
	};
}
