#include "VansRagdollProfileStorage.h"

#include "../Serialization/VansRagdollProfileJsonCodec.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace VansEngine
{
bool VansRagdollProfileStorage::Load(
    const std::filesystem::path& filePath,
    RagdollProfile& profile,
    std::string& error)
{
    RagdollJson root;
    if (!Vans::VansJsonFileStorage::Read(filePath, root, error))
    {
        error = "Cannot read .vragdoll file " + filePath.string() + ": " + error;
        return false;
    }

    if (!VansRagdollProfileJsonCodec::Decode(root, profile, error))
    {
        error = "Invalid ragdoll profile " + filePath.string() + ": " + error;
        return false;
    }

    return true;
}

bool VansRagdollProfileStorage::SaveAtomic(
    const std::filesystem::path& filePath,
    const RagdollProfile& profile,
    std::string& error)
{
    RagdollJson root;
    return VansRagdollProfileJsonCodec::Encode(profile, root, error)
        && Vans::VansJsonFileStorage::WriteAtomic(filePath, root, error);
}
}
