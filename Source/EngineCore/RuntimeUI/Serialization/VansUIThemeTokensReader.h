#pragma once

#include "../Public/VansUIThemeTokens.h"

#include <string>
#include <vector>

namespace Vans
{
struct VansSerializedValue;
}

namespace VansRuntime
{
	class VansUIThemeTokensReader
	{
	public:
		static bool Read(
			const Vans::VansSerializedValue& root,
			VansUIThemeTokensConfig& config,
			std::vector<std::string>& diagnostics);
	};
}
