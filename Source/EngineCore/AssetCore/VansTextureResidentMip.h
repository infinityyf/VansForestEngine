#pragma once

#include "VansAssetMeta.h"
#include "Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace Vans
{
inline bool ReadTextureMaxResidentDimension(
    const VansAssetMeta& meta, std::uint32_t& result, std::string& error)
{
    result = 0;
    const VansSerializedValue settings = meta.SerializedSettingsSnapshot();
    const VansSerializedValue* value = FindObjectField(settings, "maxResidentDimension");
    if (!value)
        return true;
    if (value->kind != VansSerializedValue::Kind::Int || value->intValue < 0 ||
        value->intValue > (std::numeric_limits<int>::max)())
    {
        error = "Texture maxResidentDimension must be a nonnegative integer no greater than INT_MAX";
        return false;
    }
    result = static_cast<std::uint32_t>(value->intValue);
    return true;
}

inline std::uint32_t SelectTextureResidentMip(
    std::uint32_t width, std::uint32_t height, std::uint32_t maxResidentDimension)
{
    std::uint32_t mip = 0;
    if (maxResidentDimension == 0)
        return mip;
    while (width > maxResidentDimension || height > maxResidentDimension)
    {
        width = std::max(1u, width / 2u);
        height = std::max(1u, height / 2u);
        ++mip;
    }
    return mip;
}
}
