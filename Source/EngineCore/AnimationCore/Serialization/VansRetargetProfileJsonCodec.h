#pragma once

#include "../Retargeting/VansRetargetProfile.h"

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace VansGraphics
{
class VansRetargetProfileJsonCodec
{
public:
	static bool Decode(
		const nlohmann::json& root,
		VansRetargetProfileAsset& asset,
		std::string& error);
	static bool Encode(
		const VansRetargetProfileAsset& asset,
		nlohmann::json& root,
		std::string& error);
};
}
