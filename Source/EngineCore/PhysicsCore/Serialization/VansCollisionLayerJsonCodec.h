#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace VansEngine
{
	struct VansCollisionLayerConfig;

	class VansCollisionLayerJsonCodec
	{
	public:
		static bool Decode(
			const nlohmann::json& root,
			VansCollisionLayerConfig& config,
			std::string& error);
		static nlohmann::json Encode(const VansCollisionLayerConfig& config);
	};
}
