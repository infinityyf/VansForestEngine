#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Vans
{
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

private:
    std::filesystem::path m_Path;
    Json m_Root;
    bool m_Loaded = false;
    bool m_Dirty = false;
};
}
