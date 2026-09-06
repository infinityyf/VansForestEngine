#pragma once

#include "../VansAITypes.h"

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace Vans
{
class VansAIBehaviorJsonCodec
{
public:
	static bool Decode(
		const nlohmann::json& root,
		VansAIBehaviorAsset& asset,
		std::string& error);
	static bool Encode(
		const VansAIBehaviorAsset& asset,
		nlohmann::json& root,
		std::string& error);
};
}
