#pragma once

#include "VansUIVariant.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansRuntime
{
    struct VansUIComponentPropertyConfig
    {
        std::string name;
        std::string type;
        std::string binding;
        VansUIVariant defaultValue;
    };

    struct VansUIComponentEventConfig
    {
        std::string source;
        std::string name;
        std::string action;
        std::string actionParam;
        VansUIVariantMap params;
    };

    struct VansUIComponentStateConfig
    {
        std::string name;
        bool isDefault = false;
    };

    struct VansUIComponentConfig
    {
        std::uint32_t schemaVersion = 1;
        std::string guid;
        std::string name;
        std::string xamlAssetGuid;
        std::vector<VansUIComponentPropertyConfig> publicProperties;
        std::vector<VansUIComponentEventConfig> events;
        std::vector<VansUIComponentStateConfig> states;
        std::unordered_map<std::string, std::string> animations;
        std::string sourceAssetGuid;
    };
}
