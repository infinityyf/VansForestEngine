#include "VansCollisionLayerStorage.h"

#include "../Serialization/VansCollisionLayerJsonCodec.h"
#include "../VansCollisionLayerConfig.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace VansEngine
{
	VansCollisionLayerLoadStatus VansCollisionLayerStorage::Load(
		const std::string& path,
		VansCollisionLayerConfig& config,
		std::string& error)
	{
		if (!std::filesystem::exists(path))
			return VansCollisionLayerLoadStatus::Missing;

		nlohmann::json root;
		if (!Vans::VansJsonFileStorage::Read(path, root, error))
			return VansCollisionLayerLoadStatus::ReadFailed;

		if (!VansCollisionLayerJsonCodec::Decode(root, config, error))
		{
			return VansCollisionLayerLoadStatus::DecodeFailed;
		}

		return VansCollisionLayerLoadStatus::Loaded;
	}

	bool VansCollisionLayerStorage::SaveAtomic(
		const std::string& path,
		const VansCollisionLayerConfig& config,
		std::string& error)
	{
		return Vans::VansJsonFileStorage::WriteAtomic(
			path, VansCollisionLayerJsonCodec::Encode(config), error);
	}
}
