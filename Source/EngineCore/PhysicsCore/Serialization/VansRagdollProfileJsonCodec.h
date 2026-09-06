#pragma once

#include "../VansRagdollJson.h"
#include "../VansRagdollTypes.h"

#include <string>

namespace VansEngine
{
class VansRagdollProfileJsonCodec
{
public:
    static bool Decode(const RagdollJson& root, RagdollProfile& profile, std::string& error);
    static bool Encode(const RagdollProfile& profile, RagdollJson& root, std::string& error);
};
}
