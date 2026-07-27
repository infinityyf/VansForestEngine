#pragma once

#include <nlohmann/json_fwd.hpp>

namespace VansEngine
{
	struct VansCollisionLayerConfig;

	class VansCollisionLayerLegacyJsonCodec
	{
	public:
		static bool Decode(const nlohmann::json& root, VansCollisionLayerConfig& config);
	};
}
