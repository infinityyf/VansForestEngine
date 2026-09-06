#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace VansEngine
{
struct AudioMixConfig;

class VansAudioMixConfigJsonCodec
{
public:
	static bool Decode(
		const nlohmann::json& root,
		AudioMixConfig& config,
		std::string& error);
	static nlohmann::json Encode(const AudioMixConfig& config);
};
}
