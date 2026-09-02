#pragma once

#include "VansAITypes.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansAIBehaviorAssetStorage
{
public:
	static bool Load(const std::filesystem::path& path,
		VansAIBehaviorAsset& outAsset,
		std::string& error);
};
}
