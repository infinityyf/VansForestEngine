#pragma once

#include "VansAssetMeta.h"

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansAssetType
{
    Unknown,
    Model,
    Texture,
    Material,
	Audio,
	Video,
    Scene
};

enum class VansAssetState
{
    Discovered,
    Importing,
    CpuReady,
    GpuReady,
    Failed,
    Missing
};

struct VansAssetRecord
{
    VansAssetGuid guid;
    VansAssetType type = VansAssetType::Unknown;
    VansAssetState state = VansAssetState::Discovered;
    std::filesystem::path sourcePath;
    std::filesystem::path metaPath;
    std::filesystem::path artifactPath;
    std::uint64_t generation = 0;
    std::string error;
};

struct VansAssetScanResult
{
    std::size_t registered = 0;
    std::size_t generatedMeta = 0;
    std::vector<std::string> errors;

    explicit operator bool() const { return errors.empty(); }
};

class VansAssetDatabase
{
public:
    explicit VansAssetDatabase(std::filesystem::path assetsRoot);

    VansAssetScanResult Scan();
    bool RegisterOrRefresh(const std::filesystem::path& sourcePath, bool createMeta, std::string& error);
    bool RemovePath(const std::filesystem::path& sourcePath);
    std::optional<VansAssetRecord> Find(VansAssetGuid guid) const;
    std::optional<VansAssetRecord> Find(const std::filesystem::path& sourcePath) const;
    std::vector<VansAssetRecord> All() const;
    const std::filesystem::path& AssetsRoot() const { return m_AssetsRoot; }

    static VansAssetType Classify(const std::filesystem::path& sourcePath);
    static std::string ImporterFor(VansAssetType type);

private:
    std::filesystem::path Normalize(const std::filesystem::path& path) const;
    static std::wstring PathKey(const std::filesystem::path& path);

    std::filesystem::path m_AssetsRoot;
    mutable std::shared_mutex m_Mutex;
    std::unordered_map<VansAssetGuid, VansAssetRecord> m_ByGuid;
    std::unordered_map<std::wstring, VansAssetGuid> m_ByPath;
};
}
