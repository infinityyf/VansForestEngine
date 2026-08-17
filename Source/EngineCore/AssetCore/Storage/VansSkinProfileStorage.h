#pragma once

#include "../VansSkinProfile.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansSkinProfileStorage
{
public:
	static bool Load(const std::filesystem::path& filePath, VansSkinProfile& profile, std::string& error);
	static bool SaveAtomic(const std::filesystem::path& filePath, const VansSkinProfile& profile, std::string& error);
};
}
