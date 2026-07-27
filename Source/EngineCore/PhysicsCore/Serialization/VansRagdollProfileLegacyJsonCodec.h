#pragma once

#include "../VansRagdollJson.h"
#include "../VansRagdollTypes.h"

#include <string>

namespace VansEngine
{
class VansRagdollProfileLegacyJsonCodec
{
public:
    static bool Decode(const RagdollJson& root, RagdollProfile& profile, std::string& error);
};
}
