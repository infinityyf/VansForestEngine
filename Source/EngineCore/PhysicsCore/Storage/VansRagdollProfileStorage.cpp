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
        error = "Cannot read .ragdoll file " + filePath.string() + ": " + error;
        return false;
    }

    if (!VansRagdollProfileJsonCodec::Decode(root, profile, error))
    {
        error = "Invalid ragdoll profile " + filePath.string() + ": " + error;
        return false;
    }

    return true;
}
}
