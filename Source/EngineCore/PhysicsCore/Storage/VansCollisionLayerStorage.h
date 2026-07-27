#pragma once

#include <string>

namespace VansEngine
{
	struct VansCollisionLayerConfig;

	enum class VansCollisionLayerLoadStatus
	{
		Loaded,
		Missing,
		ReadFailed,
		DecodeFailed
	};

	class VansCollisionLayerStorage
	{
	public:
		static VansCollisionLayerLoadStatus Load(
			const std::string& path,
			VansCollisionLayerConfig& config,
			std::string& error);
	};
}
