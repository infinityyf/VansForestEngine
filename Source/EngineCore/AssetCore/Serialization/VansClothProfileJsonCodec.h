#pragma once

#include "../VansClothProfile.h"
#include "../VansClothProfileJson.h"

#include <filesystem>
#include <string>

namespace VansEngine
{
class VansClothProfileJsonCodec
{
public:
    static ClothProfileJson Encode(const VansClothProfile& profile);
    static bool Decode(
        const ClothProfileJson& root,
        const std::filesystem::path& filePath,
        VansClothProfile& profile,
        std::string& error);
};
}
