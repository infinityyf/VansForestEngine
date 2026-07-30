#pragma once

#include "../Public/VansUILocalization.h"

#include <string>
#include <vector>

namespace Vans
{
struct VansSerializedValue;
}

namespace VansRuntime
{
	class VansUILocalizationReader
	{
	public:
		static bool Read(
			const Vans::VansSerializedValue& root,
			VansUILocalizationConfig& config,
			std::vector<std::string>& diagnostics);
	};
}
