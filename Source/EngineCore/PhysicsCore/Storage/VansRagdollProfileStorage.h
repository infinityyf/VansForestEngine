#pragma once

#include "../VansRagdollTypes.h"

#include <filesystem>
#include <string>

namespace VansEngine
{
class VansRagdollProfileStorage
{
public:
    static bool Load(const std::filesystem::path& filePath, RagdollProfile& profile, std::string& error);
};
}
