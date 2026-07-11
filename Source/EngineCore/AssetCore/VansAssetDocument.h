#pragma once

#include <filesystem>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace Vans
{
struct VansAssetFileFingerprint
{
    bool exists = false;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type lastWriteTime{};
    std::uint64_t contentHash = 0;

    friend bool operator==(const VansAssetFileFingerprint& left, const VansAssetFileFingerprint& right)
    {
        return left.exists == right.exists &&
            left.size == right.size &&
            left.lastWriteTime == right.lastWriteTime &&
            left.contentHash == right.contentHash;
    }

    friend bool operator!=(const VansAssetFileFingerprint& left, const VansAssetFileFingerprint& right)
    {
        return !(left == right);
    }
};

class VansAssetDocument
{
public:
    using Json = nlohmann::ordered_json;

    bool Load(const std::filesystem::path& path, std::string& error);
    bool Save(std::string& error);

    const std::filesystem::path& Path() const { return m_Path; }
    Json& Root() { return m_Root; }
    const Json& Root() const { return m_Root; }
    bool IsLoaded() const { return m_Loaded; }
    bool IsDirty() const { return m_Dirty; }
    void MarkDirty() { if (m_Loaded) m_Dirty = true; }
    void Reset();

    static VansAssetFileFingerprint Fingerprint(const std::filesystem::path& path, std::string& error);

private:
    std::filesystem::path m_Path;
    Json m_Root;
    VansAssetFileFingerprint m_LoadedFingerprint;
    bool m_Loaded = false;
    bool m_Dirty = false;
};
}
