#include "VansAssetMeta.h"

#include "Serialization/VansSerializedValueAccess.h"

#include <utility>

namespace Vans
{
VansAssetMeta::VansAssetMeta()
    : m_Settings(std::make_unique<VansSerializedValue>(VansSerializedValue::Object({})))
{
}

VansAssetMeta::~VansAssetMeta() = default;

VansAssetMeta::VansAssetMeta(const VansAssetMeta& other)
    : guid(other.guid)
    , importer(other.importer)
    , version(other.version)
    , subAssets(other.subAssets)
    , m_Settings(std::make_unique<VansSerializedValue>(other.SerializedSettingsSnapshot()))
{
}

VansAssetMeta& VansAssetMeta::operator=(const VansAssetMeta& other)
{
    if (this == &other)
        return *this;
    guid = other.guid;
    importer = other.importer;
    version = other.version;
    subAssets = other.subAssets;
    SetSerializedSettings(other.SerializedSettingsSnapshot());
    return *this;
}

VansAssetMeta::VansAssetMeta(VansAssetMeta&& other) noexcept
    : guid(std::move(other.guid))
    , importer(std::move(other.importer))
    , version(other.version)
    , subAssets(std::move(other.subAssets))
    , m_Settings(std::move(other.m_Settings))
{
    if (!m_Settings)
        m_Settings = std::make_unique<VansSerializedValue>(VansSerializedValue::Object({}));
    if (!other.m_Settings)
        other.m_Settings = std::make_unique<VansSerializedValue>(VansSerializedValue::Object({}));
}

VansAssetMeta& VansAssetMeta::operator=(VansAssetMeta&& other) noexcept
{
    if (this == &other)
        return *this;
    guid = std::move(other.guid);
    importer = std::move(other.importer);
    version = other.version;
    subAssets = std::move(other.subAssets);
    m_Settings = std::move(other.m_Settings);
    if (!m_Settings)
        m_Settings = std::make_unique<VansSerializedValue>(VansSerializedValue::Object({}));
    if (!other.m_Settings)
        other.m_Settings = std::make_unique<VansSerializedValue>(VansSerializedValue::Object({}));
    return *this;
}

VansSerializedValue VansAssetMeta::SerializedSettingsSnapshot() const
{
    return m_Settings ? *m_Settings : VansSerializedValue::Object({});
}

void VansAssetMeta::SetSerializedSettings(VansSerializedValue settings)
{
    if (!m_Settings)
        m_Settings = std::make_unique<VansSerializedValue>(VansSerializedValue::Object({}));
    *m_Settings = std::move(settings);
}

bool VansAssetMeta::HasObjectSettings() const
{
    return m_Settings && m_Settings->kind == VansSerializedValue::Kind::Object;
}

std::string VansAssetMeta::ReadStringSetting(const std::string& key, const std::string& fallback) const
{
    if (!m_Settings)
        return fallback;
    const VansSerializedValue* value = FindObjectField(*m_Settings, key);
    return value != nullptr && value->kind == VansSerializedValue::Kind::String ? value->stringValue : fallback;
}

void VansAssetMeta::SetStringSetting(const std::string& key, std::string value)
{
    if (!m_Settings)
        m_Settings = std::make_unique<VansSerializedValue>(VansSerializedValue::Object({}));
    SetSerializedObjectField(*m_Settings, key, VansSerializedValue::String(std::move(value)));
}

bool VansAssetMeta::ReadBoolSetting(const std::string& key, bool fallback) const
{
    if (!m_Settings)
        return fallback;
    const VansSerializedValue* value = FindObjectField(*m_Settings, key);
    return value != nullptr && value->kind == VansSerializedValue::Kind::Bool ? value->boolValue : fallback;
}

bool VansAssetMeta::ReadBoolSetting(const std::string& key, const std::string& legacyKey, bool fallback) const
{
    if (!m_Settings)
        return fallback;
    const VansSerializedValue* value = FindObjectField(*m_Settings, key);
    if (value != nullptr && value->kind == VansSerializedValue::Kind::Bool)
        return value->boolValue;
    value = FindObjectField(*m_Settings, legacyKey);
    return value != nullptr && value->kind == VansSerializedValue::Kind::Bool ? value->boolValue : fallback;
}

int VansAssetMeta::ReadIntSetting(const std::string& key, int fallback) const
{
    if (!m_Settings)
        return fallback;
    const VansSerializedValue* value = FindObjectField(*m_Settings, key);
    return value != nullptr && value->kind == VansSerializedValue::Kind::Int
        ? static_cast<int>(value->intValue)
        : fallback;
}

float VansAssetMeta::ReadFloatSetting(const std::string& key, float fallback) const
{
    if (!m_Settings)
        return fallback;
    const VansSerializedValue* value = FindObjectField(*m_Settings, key);
    return value != nullptr &&
        (value->kind == VansSerializedValue::Kind::Float || value->kind == VansSerializedValue::Kind::Int)
        ? static_cast<float>(ReadSerializedNumber(*value, fallback))
        : fallback;
}

float VansAssetMeta::ReadFloatSetting(const std::string& key, const std::string& legacyKey, float fallback) const
{
    if (!m_Settings)
        return fallback;
    const VansSerializedValue* value = FindObjectField(*m_Settings, key);
    if (value != nullptr &&
        (value->kind == VansSerializedValue::Kind::Float || value->kind == VansSerializedValue::Kind::Int))
    {
        return static_cast<float>(ReadSerializedNumber(*value, fallback));
    }
    value = FindObjectField(*m_Settings, legacyKey);
    return value != nullptr &&
        (value->kind == VansSerializedValue::Kind::Float || value->kind == VansSerializedValue::Kind::Int)
        ? static_cast<float>(ReadSerializedNumber(*value, fallback))
        : fallback;
}

std::filesystem::path VansAssetMeta::MetaPathFor(const std::filesystem::path& sourcePath)
{
    return std::filesystem::path(sourcePath.native() + std::filesystem::path::string_type(L".meta"));
}
}
