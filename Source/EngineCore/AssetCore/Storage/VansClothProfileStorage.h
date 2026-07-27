#pragma once

#include "../VansClothProfile.h"

#include <filesystem>
#include <string>

namespace VansEngine
{
class VansClothProfileStorage
{
public:
    static bool Load(const std::filesystem::path& filePath, VansClothProfile& profile, std::string& error);
    static bool SaveAtomic(const std::filesystem::path& filePath, const VansClothProfile& profile, std::string& error);
};
}
