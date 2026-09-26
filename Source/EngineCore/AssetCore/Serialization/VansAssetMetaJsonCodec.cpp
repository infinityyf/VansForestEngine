#include "VansAssetMetaJsonCodec.h"

#include "VansSerializedValueJsonAdapter.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace Vans
{
bool VansAssetMetaJsonCodec::Encode(
    const VansAssetMeta& meta,
    VansSerializedValue& serialized,
    std::string& error)
{
    serialized = VansSerializedValue::Object({});
    if (!meta.guid.IsValid() || meta.importer.empty())
    {
        error = "Asset meta requires a guid and importer";
        return false;
    }

    nlohmann::ordered_json root = {
        { "guid", meta.guid.ToString() },
        { "importer", meta.importer },
        { "version", meta.version },
        { "settings", EncodeSerializedValueJson<nlohmann::ordered_json>(meta.SerializedSettingsSnapshot()) }
    };

    auto subAssetsJson = nlohmann::ordered_json::object();
    for (const auto& [fingerprint, id] : meta.subAssets)
        subAssetsJson[fingerprint] = id.ToString();
    root["subAssets"] = std::move(subAssetsJson);
    serialized = DecodeSerializedValueJson(root);
    return true;
}

bool VansAssetMetaJsonCodec::Decode(
    const VansSerializedValue& serialized,
    const std::filesystem::path& metaPath,
    VansAssetMeta& result,
    std::string& error)
{
    const nlohmann::ordered_json root =
        EncodeSerializedValueJson<nlohmann::ordered_json>(serialized);
    VansAssetGuid guid;
    if (!root.is_object() || !VansAssetGuid::TryParse(root.value("guid", ""), guid))
    {
        error = "Asset meta has an invalid guid: " + metaPath.string();
        return false;
    }

    const auto settings = root.value("settings", nlohmann::ordered_json::object());
    if (settings.is_object() &&
        (settings.contains("compress") || settings.contains("generateMip") ||
            settings.contains("scale")))
    {
        error = "Asset meta contains removed settings keys: " + metaPath.string();
        return false;
    }

    result = {};
    result.guid = guid;
    result.importer = root.value("importer", "");
    result.version = root.value("version", 1u);
    result.SetSerializedSettings(DecodeSerializedValueJson(settings));
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
