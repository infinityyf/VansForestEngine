#pragma once

#include "../VansPostProcessProfile.h"

#include <filesystem>
#include <string>

namespace VansGraphics
{
class VansPostProcessProfileStorage
{
public:
	static bool Load(const std::filesystem::path& filePath, VansPostProcessProfile& profile, std::string& error);
	static bool SaveAtomic(const std::filesystem::path& filePath, const VansPostProcessProfile& profile, std::string& error);
};
}
