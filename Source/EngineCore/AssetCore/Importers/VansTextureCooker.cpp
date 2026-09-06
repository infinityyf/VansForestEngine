#include "VansTextureCooker.h"

#include "../Storage/VansFileStorage.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

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
constexpr std::uint32_t kFormatRGBA8 = static_cast<std::uint32_t>(VansCookedTextureFormat::RGBA8);
constexpr std::uint32_t kFormatR8 = static_cast<std::uint32_t>(VansCookedTextureFormat::R8);
constexpr std::uint32_t kFormatRG8 = static_cast<std::uint32_t>(VansCookedTextureFormat::RG8);
constexpr std::uint64_t kMaxArtifactBytes = 2ull * 1024ull * 1024ull * 1024ull;
constexpr std::uint64_t kBufferCopyOffsetAlignment = 4u;

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

std::vector<std::uint8_t> PackUncompressed(
    const std::uint8_t* source,
    int width,
    int height,
    int channelCount)
{
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> result(pixelCount * static_cast<std::size_t>(channelCount));
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        for (int channel = 0; channel < channelCount; ++channel)
            result[pixel * channelCount + channel] = source[pixel * 4u + channel];
    }
    return result;
}

bool TryCalculateMipSize(
    std::uint32_t format,
    std::uint32_t width,
    std::uint32_t height,
    std::uint64_t& result)
{
    if (format == kFormatBC3)
    {
        const std::uint64_t blocksX = (static_cast<std::uint64_t>(width) + 3u) / 4u;
        const std::uint64_t blocksY = (static_cast<std::uint64_t>(height) + 3u) / 4u;
        if (blocksX > kMaxArtifactBytes / 16u ||
            blocksY > kMaxArtifactBytes / (blocksX * 16u))
        {
            return false;
        }
        result = blocksX * blocksY * 16u;
        return true;
    }
    std::uint64_t bytesPerTexel = 0;
    if (format == kFormatR8)
        bytesPerTexel = 1u;
    else if (format == kFormatRG8)
        bytesPerTexel = 2u;
    else if (format == kFormatRGBA8)
        bytesPerTexel = 4u;
    if (bytesPerTexel != 0)
    {
        if (height == 0 || width > kMaxArtifactBytes / bytesPerTexel / height)
            return false;
        result = static_cast<std::uint64_t>(width) * height * bytesPerTexel;
        return true;
    }
    return false;
}

std::uint64_t AlignBufferCopyOffset(std::uint64_t value)
{
    return (value + kBufferCopyOffsetAlignment - 1u) &
        ~(kBufferCopyOffsetAlignment - 1u);
}

bool ReadHeaderAndTable(
    const std::filesystem::path& artifactPath,
    TextureArtifactHeader& header,
    std::vector<TextureArtifactMip>& mips,
    std::string& error)
{
    std::string headerBytes;
    if (!VansFileStorage::ReadByteRange(artifactPath, 0, sizeof(header), headerBytes, error))
    {
        error = "Cannot open cooked texture: " + artifactPath.string();
        return false;
    }

    std::memcpy(&header, headerBytes.data(), sizeof(header));
    if (std::memcmp(header.magic, kArtifactMagic.data(), kArtifactMagic.size()) != 0)
    {
        error = "Cooked texture has an invalid header: " + artifactPath.string();
        return false;
    }
    if (header.version != VansTextureCooker::ArtifactVersion ||
        (header.format != kFormatBC3 && header.format != kFormatRGBA8 &&
            header.format != kFormatR8 && header.format != kFormatRG8) ||
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

    std::string tableBytes;
    if (!VansFileStorage::ReadByteRange(artifactPath, header.tableOffset, expectedTableSize, tableBytes, error))
    {
        error = "Cannot read cooked texture mip table: " + artifactPath.string();
        return false;
    }

    mips.resize(header.mipCount);
    std::memcpy(mips.data(), tableBytes.data(), static_cast<std::size_t>(expectedTableSize));

    std::uint32_t expectedWidth = header.width;
    std::uint32_t expectedHeight = header.height;
    std::uint64_t expectedOffset = 0;
    for (const TextureArtifactMip& mip : mips)
    {
		expectedOffset = AlignBufferCopyOffset(expectedOffset);
        std::uint64_t expectedSize = 0;
        if (!TryCalculateMipSize(header.format, expectedWidth, expectedHeight, expectedSize))
        {
            error = "Cooked texture mip dimensions overflow: " + artifactPath.string();
            return false;
        }
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

bool HeaderMatches(const TextureArtifactHeader& actual, const TextureArtifactHeader& expected)
{
    return std::memcmp(actual.magic, expected.magic, sizeof(actual.magic)) == 0 &&
        actual.version == expected.version &&
        actual.format == expected.format &&
        actual.width == expected.width &&
        actual.height == expected.height &&
        actual.mipCount == expected.mipCount &&
        actual.sourceSize == expected.sourceSize &&
        actual.sourceWriteTime == expected.sourceWriteTime &&
        actual.metaSize == expected.metaSize &&
        actual.metaWriteTime == expected.metaWriteTime &&
        actual.tableOffset == expected.tableOffset &&
        actual.dataOffset == expected.dataOffset &&
        actual.dataSize == expected.dataSize;
}

bool MipTableMatches(
    const std::vector<TextureArtifactMip>& actual,
    const std::vector<TextureArtifactMip>& expected)
{
    if (actual.size() != expected.size())
        return false;
    for (std::size_t index = 0; index < actual.size(); ++index)
    {
        if (actual[index].width != expected[index].width ||
            actual[index].height != expected[index].height ||
            actual[index].offset != expected[index].offset ||
            actual[index].size != expected[index].size)
        {
            return false;
        }
    }
    return true;
}

bool PublishArtifact(
    const std::filesystem::path& artifactPath,
    const TextureArtifactHeader& header,
    const std::vector<TextureArtifactMip>& mips,
    const std::vector<std::uint8_t>& data,
    std::string& error)
{
    try
    {
        std::string bytes;
        bytes.reserve(sizeof(header) + mips.size() * sizeof(TextureArtifactMip) + data.size());
        bytes.append(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!mips.empty())
            bytes.append(reinterpret_cast<const char*>(mips.data()), mips.size() * sizeof(TextureArtifactMip));
        if (!data.empty())
            bytes.append(reinterpret_cast<const char*>(data.data()), data.size());

        VansStagedFile stage;
        if (!VansFileStorage::StageWriteBytes(artifactPath, bytes, stage, error))
            return false;
        const std::filesystem::path stagedPath = stage.temporaryPath;

        VansStagedFileTransaction transaction;
        transaction.Add(std::move(stage));

        TextureArtifactHeader verification{};
        std::vector<TextureArtifactMip> verificationMips;
        if (!ReadHeaderAndTable(stagedPath, verification, verificationMips, error))
        {
            return false;
        }
        if (!HeaderMatches(verification, header) || !MipTableMatches(verificationMips, mips))
        {
            error = "Cooked texture staged verification did not match source data: " + artifactPath.string();
            return false;
        }

        if (!transaction.Publish(error))
        {
            error = "Failed atomically publishing cooked texture: " + artifactPath.string() + " (" + error + ")";
            return false;
        }
        return true;
    }
    catch (const std::exception& exception)
    {
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
    const std::string precision = meta.ReadStringSetting("precision", "low8");
    if (precision != "low8" && precision != "8" && precision != "rgba8")
        return false;
    const int importChannel = meta.ReadIntSetting("importChannel", 4);
    const bool useCompress = meta.ReadBoolSetting("useCompress", "compress", true);
    return useCompress ? importChannel == 4
        : importChannel == 1 || importChannel == 2 || importChannel == 4;
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
        result.error = "Cannot decode texture for offline cooking: " + sourcePath.string();
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

    const bool useCompress = meta.ReadBoolSetting("useCompress", "compress", true);
    const int importChannel = meta.ReadIntSetting("importChannel", 4);
    const std::uint32_t cookedFormat = useCompress
        ? kFormatBC3
        : importChannel == 1 ? kFormatR8
        : importChannel == 2 ? kFormatRG8
        : kFormatRGBA8;
    const int mipCount = CalculateMipCount(width, height);
    std::vector<TextureArtifactMip> artifactMips;
    artifactMips.reserve(static_cast<std::size_t>(mipCount));
    std::vector<std::uint8_t> artifactData;
    artifactData.reserve(useCompress ? baseBytes / 3u + 256u : baseBytes);

    int mipWidth = width;
    int mipHeight = height;
    for (int mipIndex = 0; mipIndex < mipCount; ++mipIndex)
    {
        std::vector<std::uint8_t> mipPayload = useCompress
            ? CompressBC3(rgba.data(), mipWidth, mipHeight)
            : PackUncompressed(rgba.data(), mipWidth, mipHeight, importChannel);
        const std::size_t alignedOffset = static_cast<std::size_t>(
			AlignBufferCopyOffset(artifactData.size()));
		if (alignedOffset > kMaxArtifactBytes ||
			mipPayload.size() > kMaxArtifactBytes - alignedOffset)
        {
            result.status = VansTextureCookStatus::Failed;
            result.error = "Cooked texture mip chain exceeds the artifact size limit: " + sourcePath.string();
            result.artifactPath.clear();
            return result;
        }
		artifactData.resize(alignedOffset, 0u);
        TextureArtifactMip mip{};
        mip.width = static_cast<std::uint32_t>(mipWidth);
        mip.height = static_cast<std::uint32_t>(mipHeight);
        mip.offset = static_cast<std::uint64_t>(artifactData.size());
        mip.size = static_cast<std::uint64_t>(mipPayload.size());
        artifactMips.push_back(mip);
        artifactData.insert(artifactData.end(), mipPayload.begin(), mipPayload.end());

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
    header.format = cookedFormat;
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

    VansCookedTextureData loaded;
    loaded.format = static_cast<VansCookedTextureFormat>(header.format);
    loaded.width = header.width;
    loaded.height = header.height;
    std::string payloadBytes;
    if (!VansFileStorage::ReadByteRange(artifactPath, header.dataOffset, header.dataSize, payloadBytes, error))
    {
        error = "Cannot read cooked texture payload: " + artifactPath.string();
        return false;
    }
    loaded.data.resize(payloadBytes.size());
    std::memcpy(loaded.data.data(), payloadBytes.data(), payloadBytes.size());
    loaded.mips.reserve(artifactMips.size());
    for (const TextureArtifactMip& mip : artifactMips)
        loaded.mips.push_back({ mip.width, mip.height, mip.offset, mip.size });

    result = std::move(loaded);
    return true;
}
}
