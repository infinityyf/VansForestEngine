#pragma once

#include "../Public/VansUIComponentConfig.h"

#include <string>
#include <vector>

namespace Vans
{
    struct VansSerializedValue;
}

namespace VansRuntime
{
    class VansUIComponentConfigReader
    {
    public:
        static bool Read(
            const Vans::VansSerializedValue& root,
            VansUIComponentConfig& config,
            std::vector<std::string>& diagnostics);
    };
}
