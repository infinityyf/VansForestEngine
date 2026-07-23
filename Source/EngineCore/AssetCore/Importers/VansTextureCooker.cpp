#include "VansTextureCooker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>

namespace Vans
{
namespace
{
constexpr std::array<char, 8> kArtifactMagic = { 'V', 'A', 'N', 'S', 'T', 'E', 'X', '\0' };
constexpr std::uint32_t kFormatBC3 = static_cast<std::uint32_t>(VansCookedTextureFormat::BC3);
constexpr std::uint64_t kMaxArtifactBytes = 2ull * 1024ull * 1024ull * 1024ull;

struct TextureArtifactHeader
{
    char magic[8]{};
    std::uint32_t version = 0;
    std::uint32_t format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipCount = 0;
    std::uint32_t reserved = 0;
    std::uint64_t sourceSize = 0;
    std::int64_t sourceWriteTime = 0;
    std::uint64_t metaSize = 0;
    std::int64_t metaWriteTime = 0;
    std::uint64_t tableOffset = 0;
    std::uint64_t dataOffset = 0;
    std::uint64_t dataSize = 0;
};

struct TextureArtifactMip
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

static_assert(std::is_trivially_copyable_v<TextureArtifactHeader>);
static_assert(std::is_trivially_copyable_v<TextureArtifactMip>);

struct FileStamp
{
    std::uint64_t size = 0;
    std::int64_t writeTime = 0;
};

bool GetFileStamp(const std::filesystem::path& path, FileStamp& result, std::string& error)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
    {
        error = "Cannot query file size: " + path.string() + " (" + ec.message() + ")";
        return false;
    }
    const auto writeTime = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
        error = "Cannot query file timestamp: " + path.string() + " (" + ec.message() + ")";
        return false;
    }
    result.size = static_cast<std::uint64_t>(size);
    result.writeTime = static_cast<std::int64_t>(writeTime.time_since_epoch().count());
    return true;
}

bool ReadBoolSetting(const nlohmann::ordered_json& settings, const char* primary, const char* legacy, bool fallback)
{
    if (const auto it = settings.find(primary); it != settings.end() && it->is_boolean())
        return it->get<bool>();
    if (legacy)
    {
        if (const auto it = settings.find(legacy); it != settings.end() && it->is_boolean())
            return it->get<bool>();
    }
    return fallback;
}

std::string ReadStringSetting(const nlohmann::ordered_json& settings, const char* key, const std::string& fallback)
{
    const auto it = settings.find(key);
    return it != settings.end() && it->is_string() ? it->get<std::string>() : fallback;
}

int ReadIntSetting(const nlohmann::ordered_json& settings, const char* key, int fallback)
{
    const auto it = settings.find(key);
    return it != settings.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

std::wstring LowerExtension(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return extension;
}

int CalculateMipCount(int width, int height)
{
    int count = 1;
    while (width > 1 || height > 1)
    {
        width = std::max(1, width / 2);
        height = std::max(1, height / 2);
        ++count;
    }
    return count;
}

std::vector<std::uint8_t> DownsampleRGBA8(const std::uint8_t* source, int width, int height)
{
    const int outputWidth = std::max(1, width / 2);
    const int outputHeight = std::max(1, height / 2);
    std::vector<std::uint8_t> result(
        static_cast<std::size_t>(outputWidth) * static_cast<std::size_t>(outputHeight) * 4u);

    auto sample = [&](int x, int y) -> const std::uint8_t* {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return source + (static_cast<std::size_t>(y) * width + x) * 4u;
    };

    for (int y = 0; y < outputHeight; ++y)
    {
        for (int x = 0; x < outputWidth; ++x)
        {
            const std::uint8_t* p00 = sample(x * 2, y * 2);
            const std::uint8_t* p10 = sample(x * 2 + 1, y * 2);
            const std::uint8_t* p01 = sample(x * 2, y * 2 + 1);
            const std::uint8_t* p11 = sample(x * 2 + 1, y * 2 + 1);
            std::uint8_t* output = result.data() + (static_cast<std::size_t>(y) * outputWidth + x) * 4u;
            for (int channel = 0; channel < 4; ++channel)
            {
                output[channel] = static_cast<std::uint8_t>(
                    (static_cast<unsigned>(p00[channel]) + p10[channel] + p01[channel] + p11[channel]) >> 2);
            }
        }
    }
    return result;
}

std::vector<std::uint8_t> CompressBC3(const std::uint8_t* source, int width, int height)
{
    const int blocksX = (width + 3) / 4;
    const int blocksY = (height + 3) / 4;
    std::vector<std::uint8_t> result(
        static_cast<std::size_t>(blocksX) * static_cast<std::size_t>(blocksY) * 16u);

    auto sample = [&](int x, int y) -> const std::uint8_t* {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return source + (static_cast<std::size_t>(y) * width + x) * 4u;
    };

    std::uint8_t block[16 * 4]{};
    std::uint8_t* output = result.data();
    for (int blockY = 0; blockY < blocksY; ++blockY)
    {
        for (int blockX = 0; blockX < blocksX; ++blockX)
        {
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 4; ++x)
                {
                    const std::uint8_t* pixel = sample(blockX * 4 + x, blockY * 4 + y);
                    std::memcpy(block + (y * 4 + x) * 4, pixel, 4);
                }
            }
            stb_compress_dxt_block(output, block, 1, 0);
            output += 16;
        }
    }
    return result;
}

bool ReadHeaderAndTable(
    const std::filesystem::path& artifactPath,
    TextureArtifactHeader& header,
    std::vector<TextureArtifactMip>& mips,
    std::string& error)
{
    std::ifstream input(artifactPath, std::ios::binary);
    if (!input)
    {
        error = "Cannot open cooked texture: " + artifactPath.string();
        return false;
    }

    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!input || std::memcmp(header.magic, kArtifactMagic.data(), kArtifactMagic.size()) != 0)
    {
        error = "Cooked texture has an invalid header: " + artifactPath.string();
        return false;
    }
    if (header.version != VansTextureCooker::ArtifactVersion || header.format != kFormatBC3 ||
        header.width == 0 || header.height == 0 || header.mipCount == 0 || header.mipCount > 32 ||
        header.dataSize == 0 || header.dataSize > kMaxArtifactBytes)
    {
        error = "Cooked texture header is unsupported or corrupt: " + artifactPath.string();
        return false;
    }

    const std::uint64_t expectedTableSize = static_cast<std::uint64_t>(header.mipCount) * sizeof(TextureArtifactMip);
    if (header.tableOffset < sizeof(TextureArtifactHeader) ||
        header.dataOffset < header.tableOffset + expectedTableSize)
    {
        error = "Cooked texture offsets are invalid: " + artifactPath.string();
        return false;
    }

    std::error_code ec;
    const std::uint64_t fileSize = static_cast<std::uint64_t>(std::filesystem::file_size(artifactPath, ec));
    if (ec || header.dataOffset > fileSize || header.dataSize > fileSize - header.dataOffset)
    {
        error = "Cooked texture payload is truncated: " + artifactPath.string();
        return false;
    }

    input.seekg(static_cast<std::streamoff>(header.tableOffset), std::ios::beg);
    mips.resize(header.mipCount);
    input.read(reinterpret_cast<char*>(mips.data()), static_cast<std::streamsize>(expectedTableSize));
    if (!input)
    {
        error = "Cannot read cooked texture mip table: " + artifactPath.string();
        return false;
    }

    std::uint32_t expectedWidth = header.width;
    std::uint32_t expectedHeight = header.height;
    std::uint64_t expectedOffset = 0;
    for (const TextureArtifactMip& mip : mips)
    {
        const std::uint64_t blocksX = (static_cast<std::uint64_t>(expectedWidth) + 3u) / 4u;
        const std::uint64_t blocksY = (static_cast<std::uint64_t>(expectedHeight) + 3u) / 4u;
        if (blocksX > kMaxArtifactBytes / 16u ||
            blocksY > kMaxArtifactBytes / (blocksX * 16u))
        {
            error = "Cooked texture mip dimensions overflow: " + artifactPath.string();
            return false;
        }
        const std::uint64_t expectedSize = blocksX * blocksY * 16u;
        if (mip.width != expectedWidth || mip.height != expectedHeight ||
            mip.offset != expectedOffset || mip.size != expectedSize ||
            expectedOffset > header.dataSize || expectedSize > header.dataSize - expectedOffset)
        {
            error = "Cooked texture mip table is corrupt: " + artifactPath.string();
            return false;
        }
        expectedOffset += expectedSize;
        expectedWidth = std::max(1u, expectedWidth / 2u);
        expectedHeight = std::max(1u, expectedHeight / 2u);
    }
    if (expectedOffset != header.dataSize)
    {
        error = "Cooked texture payload size does not match its mip table: " + artifactPath.string();
        return false;
    }
    return true;
}

bool IsArtifactCurrent(
    const std::filesystem::path& artifactPath,
    const FileStamp& sourceStamp,
    const FileStamp& metaStamp)
{
    TextureArtifactHeader header{};
    std::vector<TextureArtifactMip> mips;
    std::string ignored;
    if (!ReadHeaderAndTable(artifactPath, header, mips, ignored))
        return false;
    return header.sourceSize == sourceStamp.size &&
        header.sourceWriteTime == sourceStamp.writeTime &&
        header.metaSize == metaStamp.size &&
        header.metaWriteTime == metaStamp.writeTime;
}

bool PublishArtifact(
    const std::filesystem::path& artifactPath,
    const TextureArtifactHeader& header,
    const std::vector<TextureArtifactMip>& mips,
    const std::vector<std::uint8_t>& data,
    std::string& error)
{
    std::error_code ec;
    std::filesystem::create_directories(artifactPath.parent_path(), ec);
    if (ec)
    {
        error = "Cannot create texture artifact directory: " + ec.message();
        return false;
    }

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const std::filesystem::path temporary(artifactPath.native() + L".tmp." + std::to_wstring(nonce));
#else
    const std::filesystem::path temporary(artifactPath.native() + ".tmp." + std::to_string(nonce));
#endif

    try
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Cannot create texture artifact temporary file");
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        output.write(reinterpret_cast<const char*>(mips.data()),
            static_cast<std::streamsize>(mips.size() * sizeof(TextureArtifactMip)));
        output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output)
            throw std::runtime_error("Failed writing texture artifact");
        output.close();

        TextureArtifactHeader verification{};
        std::vector<TextureArtifactMip> verificationMips;
        if (!ReadHeaderAndTable(temporary, verification, verificationMips, error))
        {
            std::filesystem::remove(temporary, ec);
            return false;
        }

        bool published = false;
#ifdef _WIN32
        published = MoveFileExW(temporary.c_str(), artifactPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
        std::filesystem::rename(temporary, artifactPath, ec);
        published = !ec;
#endif
        if (!published)
        {
            std::filesystem::remove(temporary, ec);
            error = "Failed atomically publishing cooked texture: " + artifactPath.string();
            return false;
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        std::filesystem::remove(temporary, ec);
        error = exception.what();
        return false;
    }
}
}

bool VansTextureCooker::IsEligible(const std::filesystem::path& sourcePath, const VansAssetMeta& meta)
{
    const std::wstring extension = LowerExtension(sourcePath);
    if (extension != L".png" && extension != L".jpg" && extension != L".jpeg" && extension != L".tga")
        return false;
    if (!ReadBoolSetting(meta.settings, "useCompress", "compress", true))
        return false;
    const std::string precision = ReadStringSetting(meta.settings, "precision", "low8");
    if (precision != "low8" && precision != "8" && precision != "rgba8")
        return false;
    return ReadIntSetting(meta.settings, "importChannel", 4) == 4;
}

VansTextureCookResult VansTextureCooker::CookIfNeeded(
    const std::filesystem::path& sourcePath,
    const std::filesystem::path& metaPath,
    const VansAssetMeta& meta,
    const std::filesystem::path& artifactRoot)
{
    VansTextureCookResult result;
    if (artifactRoot.empty() || !IsEligible(sourcePath, meta))
        return result;

    result.artifactPath = artifactRoot / "Textures" / (meta.guid.ToString() + ".vtex");
    FileStamp sourceStamp{};
    FileStamp metaStamp{};
    if (!GetFileStamp(sourcePath, sourceStamp, result.error) || !GetFileStamp(metaPath, metaStamp, result.error))
    {
        result.status = VansTextureCookStatus::Failed;
        result.artifactPath.clear();
        return result;
    }
    if (IsArtifactCurrent(result.artifactPath, sourceStamp, metaStamp))
    {
        result.status = VansTextureCookStatus::UpToDate;
        return result;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    stbi_uc* loaded = stbi_load(sourcePath.string().c_str(), &width, &height, &sourceChannels, 4);
    if (!loaded || width <= 0 || height <= 0)
    {
        result.status = VansTextureCookStatus::Failed;
        result.error = "Cannot decode texture for offline compression: " + sourcePath.string();
        if (loaded)
            stbi_image_free(loaded);
        result.artifactPath.clear();
        return result;
    }

    const std::size_t baseBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (baseBytes > kMaxArtifactBytes)
    {
        stbi_image_free(loaded);
        result.status = VansTextureCookStatus::Failed;
        result.error = "Texture is too large for the offline cooker: " + sourcePath.string();
        result.artifactPath.clear();
        return result;
    }

    std::vector<std::uint8_t> rgba(loaded, loaded + baseBytes);
    stbi_image_free(loaded);

    const int mipCount = CalculateMipCount(width, height);
    std::vector<TextureArtifactMip> artifactMips;
    artifactMips.reserve(static_cast<std::size_t>(mipCount));
    std::vector<std::uint8_t> artifactData;
    artifactData.reserve(baseBytes / 3u + 256u);

    int mipWidth = width;
    int mipHeight = height;
    for (int mipIndex = 0; mipIndex < mipCount; ++mipIndex)
    {
        std::vector<std::uint8_t> compressed = CompressBC3(rgba.data(), mipWidth, mipHeight);
        TextureArtifactMip mip{};
        mip.width = static_cast<std::uint32_t>(mipWidth);
        mip.height = static_cast<std::uint32_t>(mipHeight);
        mip.offset = static_cast<std::uint64_t>(artifactData.size());
        mip.size = static_cast<std::uint64_t>(compressed.size());
        artifactMips.push_back(mip);
        artifactData.insert(artifactData.end(), compressed.begin(), compressed.end());

        if (mipIndex + 1 < mipCount)
        {
            rgba = DownsampleRGBA8(rgba.data(), mipWidth, mipHeight);
            mipWidth = std::max(1, mipWidth / 2);
            mipHeight = std::max(1, mipHeight / 2);
        }
    }

    TextureArtifactHeader header{};
    std::memcpy(header.magic, kArtifactMagic.data(), kArtifactMagic.size());
    header.version = ArtifactVersion;
    header.format = kFormatBC3;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.mipCount = static_cast<std::uint32_t>(artifactMips.size());
    header.sourceSize = sourceStamp.size;
    header.sourceWriteTime = sourceStamp.writeTime;
    header.metaSize = metaStamp.size;
    header.metaWriteTime = metaStamp.writeTime;
    header.tableOffset = sizeof(TextureArtifactHeader);
    header.dataOffset = header.tableOffset + artifactMips.size() * sizeof(TextureArtifactMip);
    header.dataSize = artifactData.size();

    if (!PublishArtifact(result.artifactPath, header, artifactMips, artifactData, result.error))
    {
        result.status = VansTextureCookStatus::Failed;
        result.artifactPath.clear();
        return result;
    }
    result.status = VansTextureCookStatus::Cooked;
    return result;
}

bool VansTextureCooker::LoadArtifact(
    const std::filesystem::path& artifactPath,
    VansCookedTextureData& result,
    std::string& error)
{
    TextureArtifactHeader header{};
    std::vector<TextureArtifactMip> artifactMips;
    if (!ReadHeaderAndTable(artifactPath, header, artifactMips, error))
        return false;

    std::ifstream input(artifactPath, std::ios::binary);
    if (!input)
    {
        error = "Cannot reopen cooked texture: " + artifactPath.string();
        return false;
    }
    input.seekg(static_cast<std::streamoff>(header.dataOffset), std::ios::beg);

    VansCookedTextureData loaded;
    loaded.format = static_cast<VansCookedTextureFormat>(header.format);
    loaded.width = header.width;
    loaded.height = header.height;
    loaded.data.resize(static_cast<std::size_t>(header.dataSize));
    input.read(reinterpret_cast<char*>(loaded.data.data()), static_cast<std::streamsize>(loaded.data.size()));
    if (!input)
    {
        error = "Cannot read cooked texture payload: " + artifactPath.string();
        return false;
    }
    loaded.mips.reserve(artifactMips.size());
    for (const TextureArtifactMip& mip : artifactMips)
        loaded.mips.push_back({ mip.width, mip.height, mip.offset, mip.size });

    result = std::move(loaded);
    return true;
}
}
