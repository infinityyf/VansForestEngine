#include "VansAssetMetaJsonCodec.h"

#include "VansSerializedValueJsonAdapter.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace Vans
{
bool VansAssetMetaJsonCodec::Encode(
    const VansAssetMeta& meta,
    nlohmann::ordered_json& root,
    std::string& error)
{
    root = {};
    if (!meta.guid.IsValid() || meta.importer.empty())
    {
        error = "Asset meta requires a guid and importer";
        return false;
    }

    root = {
        { "guid", meta.guid.ToString() },
        { "importer", meta.importer },
        { "version", meta.version },
        { "settings", EncodeSerializedValueJson<nlohmann::ordered_json>(meta.SerializedSettingsSnapshot()) }
    };

    auto subAssetsJson = nlohmann::ordered_json::object();
    for (const auto& [fingerprint, id] : meta.subAssets)
        subAssetsJson[fingerprint] = id.ToString();
    root["subAssets"] = std::move(subAssetsJson);
    return true;
}

bool VansAssetMetaJsonCodec::Decode(
    const nlohmann::ordered_json& root,
    const std::filesystem::path& metaPath,
    VansAssetMeta& result,
    std::string& error)
{
    VansAssetGuid guid;
    if (!root.is_object() || !VansAssetGuid::TryParse(root.value("guid", ""), guid))
    {
        error = "Asset meta has an invalid guid: " + metaPath.string();
        return false;
    }

    result = {};
    result.guid = guid;
    result.importer = root.value("importer", "");
    result.version = root.value("version", 1u);
    result.SetSerializedSettings(DecodeSerializedValueJson(root.value("settings", nlohmann::ordered_json::object())));
    if (const auto it = root.find("subAssets"); it != root.end() && it->is_object())
    {
        for (auto entry = it->begin(); entry != it->end(); ++entry)
        {
            VansSubAssetId id;
            if (entry.value().is_string() && VansAssetGuid::TryParse(entry.value().get<std::string>(), id))
                result.subAssets.emplace(entry.key(), id);
        }
    }
    return true;
}
}
