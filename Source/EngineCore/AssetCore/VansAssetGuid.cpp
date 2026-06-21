#include "VansAssetGuid.h"

#include <array>
#include <charconv>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

namespace Vans
{
namespace
{
bool ParseHalf(std::string_view text, std::uint64_t& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
}

VansAssetGuid VansAssetGuid::New()
{
    std::random_device source;
    std::mt19937_64 generator((static_cast<std::uint64_t>(source()) << 32) ^ source());
    std::uint64_t high = generator();
    std::uint64_t low = generator();
    high = (high & 0xffffffffffff0fffull) | 0x0000000000004000ull;
    low = (low & 0x3fffffffffffffffull) | 0x8000000000000000ull;
    return VansAssetGuid(high, low);
}

bool VansAssetGuid::TryParse(std::string_view text, VansAssetGuid& result)
{
    std::array<char, 32> compact{};
    std::size_t cursor = 0;
    for (const char character : text)
    {
        if (character == '-')
            continue;
        if (cursor == compact.size())
            return false;
        compact[cursor++] = character;
    }
    if (cursor != compact.size())
        return false;

    std::uint64_t high = 0;
    std::uint64_t low = 0;
    const std::string_view view(compact.data(), compact.size());
    if (!ParseHalf(view.substr(0, 16), high) || !ParseHalf(view.substr(16), low) || (high == 0 && low == 0))
        return false;
    result = VansAssetGuid(high, low);
    return true;
}

bool VansAssetGuid::IsValid() const
{
    return m_High != 0 || m_Low != 0;
}

std::string VansAssetGuid::ToString() const
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
           << std::setw(8) << static_cast<std::uint32_t>(m_High >> 32) << '-'
           << std::setw(4) << static_cast<std::uint16_t>(m_High >> 16) << '-'
           << std::setw(4) << static_cast<std::uint16_t>(m_High) << '-'
           << std::setw(4) << static_cast<std::uint16_t>(m_Low >> 48) << '-'
           << std::setw(12) << (m_Low & 0x0000ffffffffffffull);
    return stream.str();
}
}
