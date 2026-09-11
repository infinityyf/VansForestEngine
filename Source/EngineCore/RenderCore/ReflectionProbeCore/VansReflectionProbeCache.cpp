#include "VansReflectionProbeCache.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include <array>
#include <cstring>
#include <system_error>
#include <utility>

namespace VansGraphics
{
    namespace
    {
        constexpr size_t HeaderBytes = 148u;
        constexpr uint64_t MaxFileBytes = HeaderBytes + 512ull * 512ull * 8ull * 6ull * 4ull / 3ull;
        uint32_t Read32(std::string_view bytes, size_t word)
        {
            const auto* p = reinterpret_cast<const uint8_t*>(bytes.data() + word * 4u);
            return uint32_t(p[0]) | (uint32_t(p[1]) << 8u) | (uint32_t(p[2]) << 16u) | (uint32_t(p[3]) << 24u);
        }
        bool FiniteHalfData(std::string_view bytes)
        {
            for (size_t i = 0; i < bytes.size(); i += 2u)
                if ((uint8_t(bytes[i + 1u]) & 0x7cu) == 0x7cu) return false;
            return true;
        }
    }

    bool VansReflectionProbeCache::Describe(uint32_t resolution,
        std::vector<VansReflectionProbeCacheSurface>& surfaces, uint64_t& byteCount,
        uint32_t& mipCount, std::string& error)
    {
        error.clear();
        if (!resolution || resolution > 512u || (resolution & (resolution - 1u)))
        { error = "Reflection cache resolution must be a power of two in [1,512]"; return false; }
        std::vector<VansReflectionProbeCacheSurface> pending;
        uint32_t mips = 0; for (uint32_t size = resolution; size; size >>= 1u) ++mips;
        uint64_t offset = 0;
        for (uint32_t face = 0; face < 6u; ++face)
        for (uint32_t mip = 0; mip < mips; ++mip)
        {
            const uint32_t size = resolution >> mip;
            const uint64_t bytes = uint64_t(size) * size * 8u;
            pending.push_back({face, mip, size, offset, bytes});
            offset += bytes;
        }
        surfaces = std::move(pending); byteCount = offset; mipCount = mips;
        return true;
    }

    bool VansReflectionProbeCache::Decode(std::string_view bytes, VansReflectionProbeCacheData& result, std::string& error)
    {
        error.clear();
        if (bytes.size() < HeaderBytes || bytes.size() > MaxFileBytes || Read32(bytes, 0) != 0x20534444u ||
            Read32(bytes, 1) != 124u || Read32(bytes, 19) != 32u || Read32(bytes, 20) != 4u ||
            Read32(bytes, 21) != 0x30315844u || Read32(bytes, 32) != 10u || Read32(bytes, 33) != 3u ||
            Read32(bytes, 34) != 4u || Read32(bytes, 35) != 1u || Read32(bytes, 3) != Read32(bytes, 4) ||
            Read32(bytes, 6) != 0u || (Read32(bytes, 28) & 0xfe00u) != 0xfe00u)
        { error = "Reflection cache requires one complete DDS DX10 RGBA16F cubemap"; return false; }
        VansReflectionProbeCacheData pending;
        pending.resolution = Read32(bytes, 4);
        std::vector<VansReflectionProbeCacheSurface> surfaces; uint64_t size = 0;
        if (!Describe(pending.resolution, surfaces, size, pending.mipCount, error)) return false;
        if (Read32(bytes, 7) != pending.mipCount || bytes.size() != HeaderBytes + size ||
            Read32(bytes, 5) != pending.resolution * 8u || !FiniteHalfData(bytes.substr(HeaderBytes)))
        { error = "Reflection cache mip chain is incomplete, mis-sized or non-finite"; return false; }
        pending.texels.assign(bytes.begin() + HeaderBytes, bytes.end());
        result = std::move(pending);
        return true;
    }

    bool VansReflectionProbeCache::Encode(const VansReflectionProbeCacheData& data, std::string& bytes, std::string& error)
    {
        std::vector<VansReflectionProbeCacheSurface> surfaces; uint64_t size = 0; uint32_t mips = 0;
        if (!Describe(data.resolution, surfaces, size, mips, error)) return false;
        if (data.mipCount != mips || data.texels.size() != size ||
            !FiniteHalfData({reinterpret_cast<const char*>(data.texels.data()), data.texels.size()}))
        { error = "Reflection cache requires finite RGBA16F texels for every face and mip"; return false; }
        std::array<uint32_t, 37> header{};
        header[0] = 0x20534444u; header[1] = 124u; header[2] = 0x2100fu;
        header[3] = header[4] = data.resolution; header[5] = data.resolution * 8u; header[7] = mips;
        header[19] = 32u; header[20] = 4u; header[21] = 0x30315844u;
        header[27] = 0x401008u; header[28] = 0xfe00u;
        header[32] = 10u; header[33] = 3u; header[34] = 4u; header[35] = 1u;
        std::string pending(HeaderBytes + size, '\0');
        for (size_t i = 0; i < header.size(); ++i)
            for (uint32_t byte = 0; byte < 4u; ++byte) pending[i * 4u + byte] = char(header[i] >> (byte * 8u));
        std::memcpy(pending.data() + HeaderBytes, data.texels.data(), data.texels.size());
        bytes = std::move(pending);
        return true;
    }

    bool VansReflectionProbeCache::Load(const std::filesystem::path& file, VansReflectionProbeCacheData& result, std::string& error)
    {
        std::error_code ec; const auto size = std::filesystem::file_size(file, ec);
        if (ec || size < HeaderBytes || size > MaxFileBytes)
        { error = "Reflection cache missing or file size is invalid"; return false; }
        Vans::VansScopedIOContext context(Vans::VansIODomain::Derived, "ReflectionProbeCache.Load");
        std::string bytes;
        return Vans::VansFileStorage::ReadAllBytes(file, bytes, error) && Decode(bytes, result, error);
    }

    bool VansReflectionProbeCache::Save(const std::filesystem::path& file, const VansReflectionProbeCacheData& data, std::string& error)
    {
        std::string bytes;
        if (!Encode(data, bytes, error)) return false;
        Vans::VansScopedIOContext context(Vans::VansIODomain::Derived, "ReflectionProbeCache.Save");
        return Vans::VansFileStorage::WriteAtomicBytes(file, bytes, error);
    }
}
