#pragma once

#include "../Public/VansUIScreenConfig.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

#include <string>
#include <vector>

namespace VansRuntime
{
	class VansUIScreenConfigReader
	{
	public:
		static bool Read(const Vans::VansSerializedValue& root, VansUIScreenConfig& config, std::vector<std::string>& diagnostics);
	};
}
