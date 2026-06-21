#include "VansAssetMeta.h"

#include <chrono>
#include <fstream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace Vans
{
std::filesystem::path VansAssetMeta::MetaPathFor(const std::filesystem::path& sourcePath)
{
    return std::filesystem::path(sourcePath.native() + std::filesystem::path::string_type(L".meta"));
}

bool VansAssetMeta::Load(const std::filesystem::path& metaPath, VansAssetMeta& result, std::string& error)
{
    try
    {
        std::ifstream input(metaPath, std::ios::binary);
        if (!input)
        {
            error = "Cannot open asset meta: " + metaPath.string();
            return false;
        }
        const nlohmann::ordered_json root = nlohmann::ordered_json::parse(input);
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
        result.settings = root.value("settings", nlohmann::ordered_json::object());
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
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

bool VansAssetMeta::SaveAtomic(const std::filesystem::path& metaPath, std::string& error) const
{
    if (!guid.IsValid() || importer.empty())
    {
        error = "Asset meta requires a guid and importer";
        return false;
    }

    nlohmann::ordered_json root = {
        { "guid", guid.ToString() },
        { "importer", importer },
        { "version", version },
        { "settings", settings }
    };
    auto subAssetsJson = nlohmann::ordered_json::object();
    for (const auto& [fingerprint, id] : subAssets)
        subAssetsJson[fingerprint] = id.ToString();
    root["subAssets"] = std::move(subAssetsJson);

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const std::filesystem::path temporary(metaPath.native() + L".tmp." + std::to_wstring(nonce));
#else
    const std::filesystem::path temporary(metaPath.native() + ".tmp." + std::to_string(nonce));
#endif
    try
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << root.dump(4) << '\n';
        output.flush();
        if (!output)
            throw std::runtime_error("Failed writing asset meta temporary file");
        output.close();

        VansAssetMeta verification;
        if (!Load(temporary, verification, error) || verification.guid != guid)
        {
            std::filesystem::remove(temporary);
            return false;
        }
        bool published = false;
#ifdef _WIN32
        published = MoveFileExW(temporary.c_str(), metaPath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
        std::error_code ec;
        std::filesystem::rename(temporary, metaPath, ec);
        published = !ec;
#endif
        if (!published)
        {
            std::filesystem::remove(temporary);
            error = "Failed atomically publishing asset meta";
            return false;
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        error = exception.what();
        return false;
    }
}
}
