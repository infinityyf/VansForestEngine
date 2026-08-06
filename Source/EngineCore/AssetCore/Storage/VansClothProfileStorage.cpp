#include "VansClothProfileStorage.h"

#include "../Serialization/VansClothProfileJsonCodec.h"
#include "VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace VansEngine
{
bool VansClothProfileStorage::Load(
    const std::filesystem::path& filePath,
    VansClothProfile& profile,
    std::string& error)
{
    ClothProfileJson root;
    if (!Vans::VansJsonFileStorage::Read(filePath, root, error))
        return false;
    return VansClothProfileJsonCodec::Decode(root, filePath, profile, error);
}

bool VansClothProfileStorage::SaveAtomic(
    const std::filesystem::path& filePath,
    const VansClothProfile& profile,
    std::string& error)
{
    return Vans::VansJsonFileStorage::WriteAtomic(
        filePath,
        VansClothProfileJsonCodec::Encode(profile),
        error);
}
}
