#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace VansEngine
{
struct VansAudioMixConfig;

class VansAudioMixConfigJsonCodec
{
public:
	static bool Decode(
		const nlohmann::json& root,
		VansAudioMixConfig& config,
		std::string& error);
	static nlohmann::json Encode(const VansAudioMixConfig& config);
};
}
