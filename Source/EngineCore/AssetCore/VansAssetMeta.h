#pragma once

#include "Serialization/VansSerializedValue.h"
#include "VansAssetGuid.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace Vans
{
struct VansAssetMeta
{
    VansAssetMeta();
    ~VansAssetMeta();
    VansAssetMeta(const VansAssetMeta& other);
    VansAssetMeta& operator=(const VansAssetMeta& other);
    VansAssetMeta(VansAssetMeta&& other) noexcept;
    VansAssetMeta& operator=(VansAssetMeta&& other) noexcept;

    VansAssetGuid guid;
    std::string importer;
    std::uint32_t version = 1;
    std::map<std::string, VansSubAssetId> subAssets;

    VansSerializedValue SerializedSettingsSnapshot() const;
    void SetSerializedSettings(VansSerializedValue settings);

    bool HasObjectSettings() const;
    std::string ReadStringSetting(const std::string& key, const std::string& fallback = {}) const;
    void SetStringSetting(const std::string& key, std::string value);
    bool ReadBoolSetting(const std::string& key, bool fallback) const;
    bool ReadBoolSetting(const std::string& key, const std::string& legacyKey, bool fallback) const;
    int ReadIntSetting(const std::string& key, int fallback) const;
    float ReadFloatSetting(const std::string& key, float fallback) const;
    float ReadFloatSetting(const std::string& key, const std::string& legacyKey, float fallback) const;

    static std::filesystem::path MetaPathFor(const std::filesystem::path& sourcePath);

private:
    std::unique_ptr<VansSerializedValue> m_Settings;
};
}
