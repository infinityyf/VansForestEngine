#pragma once

#include "../../AssetCore/Serialization/VansSerializedValue.h"

#include <filesystem>
#include <string>

namespace VansRuntime
{
	struct VansUIAssetDocument
	{
		Vans::VansSerializedValue root = Vans::VansSerializedValue::Object({});
		std::filesystem::path sourcePath;
		std::uint64_t contentHash = 0;
	};
}
