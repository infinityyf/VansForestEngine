#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Vans
{
class VansAssetGuid
{
public:
    VansAssetGuid() = default;

    static VansAssetGuid New();
    static bool TryParse(std::string_view text, VansAssetGuid& result);

    bool IsValid() const;
    std::string ToString() const;

    bool operator==(const VansAssetGuid& other) const { return m_High == other.m_High && m_Low == other.m_Low; }
    bool operator!=(const VansAssetGuid& other) const { return !(*this == other); }
    bool operator<(const VansAssetGuid& other) const { return m_High < other.m_High || (m_High == other.m_High && m_Low < other.m_Low); }

    std::uint64_t High() const { return m_High; }
    std::uint64_t Low() const { return m_Low; }

private:
    VansAssetGuid(std::uint64_t high, std::uint64_t low) : m_High(high), m_Low(low) {}

    std::uint64_t m_High = 0;
    std::uint64_t m_Low = 0;
};

using VansSubAssetId = VansAssetGuid;
using VansEntityGuid = VansAssetGuid;
using VansComponentGuid = VansAssetGuid;
}

namespace std
{
template <>
struct hash<Vans::VansAssetGuid>
{
    size_t operator()(const Vans::VansAssetGuid& guid) const noexcept
    {
        const size_t high = hash<std::uint64_t>{}(guid.High());
        const size_t low = hash<std::uint64_t>{}(guid.Low());
        return high ^ (low + 0x9e3779b97f4a7c15ull + (high << 6) + (high >> 2));
    }
};
}
