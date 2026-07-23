#pragma once

#include "../VansAssetMeta.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
enum class VansCookedTextureFormat : std::uint32_t
{
    Unknown = 0,
    BC3 = 1
};

struct VansCookedTextureMip
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct VansCookedTextureData
{
    VansCookedTextureFormat format = VansCookedTextureFormat::Unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<VansCookedTextureMip> mips;
    std::vector<std::uint8_t> data;
};

enum class VansTextureCookStatus
{
    NotEligible,
    UpToDate,
    Cooked,
    Failed
};

struct VansTextureCookResult
{
    VansTextureCookStatus status = VansTextureCookStatus::NotEligible;
    std::filesystem::path artifactPath;
    std::string error;
};

class VansTextureCooker
{
public:
    static constexpr std::uint32_t ArtifactVersion = 1;

    static VansTextureCookResult CookIfNeeded(
        const std::filesystem::path& sourcePath,
        const std::filesystem::path& metaPath,
        const VansAssetMeta& meta,
        const std::filesystem::path& artifactRoot);

    static bool LoadArtifact(
        const std::filesystem::path& artifactPath,
        VansCookedTextureData& result,
        std::string& error);

private:
    static bool IsEligible(const std::filesystem::path& sourcePath, const VansAssetMeta& meta);
};
}
