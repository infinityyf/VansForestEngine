#include "VansAssetDatabase.h"

#include <algorithm>
#include <cwctype>
#include <mutex>

namespace Vans
{
namespace
{
std::wstring LowerExtension(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) { return std::towlower(value); });
    return extension;
}
}

VansAssetDatabase::VansAssetDatabase(std::filesystem::path assetsRoot)
    : m_AssetsRoot(std::filesystem::absolute(std::move(assetsRoot)).lexically_normal())
{
}

VansAssetScanResult VansAssetDatabase::Scan()
{
    VansAssetScanResult result;
    std::error_code ec;
    if (!std::filesystem::is_directory(m_AssetsRoot, ec))
    {
        result.errors.push_back("Assets root is not a directory: " + m_AssetsRoot.string());
        return result;
    }

    {
        std::unique_lock lock(m_Mutex);
        m_ByPath.clear();
        for (auto& [guid, record] : m_ByGuid)
            record.state = VansAssetState::Missing;
    }

    for (std::filesystem::recursive_directory_iterator it(m_AssetsRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec))
    {
        if (ec)
        {
            result.errors.push_back(ec.message());
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || LowerExtension(it->path()) == L".meta")
            continue;
        if (Classify(it->path()) == VansAssetType::Unknown)
            continue;
        const bool hadMeta = std::filesystem::exists(VansAssetMeta::MetaPathFor(it->path()));
        std::string error;
        if (RegisterOrRefresh(it->path(), true, error))
        {
            ++result.registered;
            if (!hadMeta)
                ++result.generatedMeta;
        }
        else
            result.errors.push_back(std::move(error));
    }
    return result;
}

bool VansAssetDatabase::RegisterOrRefresh(const std::filesystem::path& sourcePath, bool createMeta, std::string& error)
{
    const std::filesystem::path normalized = Normalize(sourcePath);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(normalized, ec))
    {
        error = "Asset source does not exist: " + normalized.string();
        return false;
    }
    const VansAssetType type = Classify(normalized);
    if (type == VansAssetType::Unknown)
    {
        error = "Unsupported asset type: " + normalized.string();
        return false;
    }

    const std::filesystem::path metaPath = VansAssetMeta::MetaPathFor(normalized);
    VansAssetMeta meta;
    if (std::filesystem::exists(metaPath, ec))
    {
        if (!VansAssetMeta::Load(metaPath, meta, error))
            return false;
    }
    else
    {
        if (!createMeta)
        {
            error = "Asset has no meta: " + normalized.string();
            return false;
        }
        meta.guid = VansAssetGuid::New();
        meta.importer = ImporterFor(type);
        if (!meta.SaveAtomic(metaPath, error))
            return false;
    }
    if (meta.importer != ImporterFor(type))
    {
        error = "Asset importer does not match source extension: " + normalized.string();
        return false;
    }

    std::unique_lock lock(m_Mutex);
    const std::wstring key = PathKey(normalized);
    if (const auto existing = m_ByGuid.find(meta.guid); existing != m_ByGuid.end() &&
        existing->second.state != VansAssetState::Missing && PathKey(existing->second.sourcePath) != key)
    {
        error = "Duplicate asset guid in " + normalized.string() + " and " + existing->second.sourcePath.string();
        return false;
    }
    if (const auto existing = m_ByPath.find(key); existing != m_ByPath.end() && existing->second != meta.guid)
    {
        error = "Case-insensitive asset path collision: " + normalized.string();
        return false;
    }

    VansAssetRecord& record = m_ByGuid[meta.guid];
    record.guid = meta.guid;
    record.type = type;
    record.sourcePath = normalized;
    record.metaPath = metaPath;
    record.state = VansAssetState::Discovered;
    ++record.generation;
    record.error.clear();
    m_ByPath[key] = meta.guid;
    return true;
}

bool VansAssetDatabase::RemovePath(const std::filesystem::path& sourcePath)
{
    std::unique_lock lock(m_Mutex);
    const auto path = m_ByPath.find(PathKey(Normalize(sourcePath)));
    if (path == m_ByPath.end())
        return false;
    if (auto record = m_ByGuid.find(path->second); record != m_ByGuid.end())
    {
        record->second.state = VansAssetState::Missing;
        ++record->second.generation;
    }
    m_ByPath.erase(path);
    return true;
}

std::optional<VansAssetRecord> VansAssetDatabase::Find(VansAssetGuid guid) const
{
    std::shared_lock lock(m_Mutex);
    const auto result = m_ByGuid.find(guid);
    return result == m_ByGuid.end() ? std::nullopt : std::optional<VansAssetRecord>(result->second);
}

std::optional<VansAssetRecord> VansAssetDatabase::Find(const std::filesystem::path& sourcePath) const
{
    std::shared_lock lock(m_Mutex);
    const auto path = m_ByPath.find(PathKey(Normalize(sourcePath)));
    if (path == m_ByPath.end())
        return std::nullopt;
    const auto record = m_ByGuid.find(path->second);
    return record == m_ByGuid.end() ? std::nullopt : std::optional<VansAssetRecord>(record->second);
}

std::vector<VansAssetRecord> VansAssetDatabase::All() const
{
    std::shared_lock lock(m_Mutex);
    std::vector<VansAssetRecord> result;
    result.reserve(m_ByGuid.size());
    for (const auto& [guid, record] : m_ByGuid)
        result.push_back(record);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.sourcePath < right.sourcePath; });
    return result;
}

VansAssetType VansAssetDatabase::Classify(const std::filesystem::path& sourcePath)
{
    const std::wstring extension = LowerExtension(sourcePath);
    if (extension == L".fbx" || extension == L".obj" || extension == L".gltf" || extension == L".glb") return VansAssetType::Model;
	if (extension == L".png" || extension == L".jpg" || extension == L".jpeg" || extension == L".tga" || extension == L".hdr" || extension == L".exr" || extension == L".cubemap") return VansAssetType::Texture;
    if (extension == L".mat") return VansAssetType::Material;
	if (extension == L".wav" || extension == L".mp3" || extension == L".ogg" || extension == L".flac") return VansAssetType::Audio;
	if (extension == L".mp4" || extension == L".mkv" || extension == L".avi" || extension == L".mov" || extension == L".webm") return VansAssetType::Video;
    if (extension == L".scene" || extension == L".vscene") return VansAssetType::Scene;
    return VansAssetType::Unknown;
}

std::string VansAssetDatabase::ImporterFor(VansAssetType type)
{
    switch (type)
    {
    case VansAssetType::Model: return "ModelImporter";
    case VansAssetType::Texture: return "TextureImporter";
    case VansAssetType::Material: return "MaterialImporter";
	case VansAssetType::Audio: return "AudioImporter";
	case VansAssetType::Video: return "VideoImporter";
    case VansAssetType::Scene: return "SceneImporter";
    default: return {};
    }
}

std::filesystem::path VansAssetDatabase::Normalize(const std::filesystem::path& path) const
{
    return std::filesystem::absolute(path).lexically_normal();
}

std::wstring VansAssetDatabase::PathKey(const std::filesystem::path& path)
{
    std::wstring key = path.generic_wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) { return std::towlower(value); });
    return key;
}
}
