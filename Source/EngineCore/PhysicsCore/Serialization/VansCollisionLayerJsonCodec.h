#pragma once

#include <nlohmann/json_fwd.hpp>

namespace VansEngine
{
	struct VansCollisionLayerConfig;

	class VansCollisionLayerJsonCodec
	{
	public:
		static bool Decode(const nlohmann::json& root, VansCollisionLayerConfig& config);
	};
}
