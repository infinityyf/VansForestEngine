#include "VansAssetDocumentRegistry.h"

#include "../AssetCore/VansAssetMeta.h"

#include <algorithm>
#include <cwctype>

namespace Vans
{
VansAssetDocumentRegistry& VansAssetDocumentRegistry::Get()
{
    static VansAssetDocumentRegistry registry;
    return registry;
}

std::shared_ptr<VansOpenAssetDocument> VansAssetDocumentRegistry::GetOrOpen(const std::filesystem::path& sourcePath)
{
    const std::filesystem::path normalized = Normalize(sourcePath);
    const std::wstring key = PathKey(normalized);
    if (const auto found = m_Documents.find(key); found != m_Documents.end())
        return found->second;

    auto document = std::make_shared<VansOpenAssetDocument>();
    document->sourcePath = normalized;
    document->metaPath = VansAssetMeta::MetaPathFor(normalized);

    std::string sourceError;
    document->sourceDocument.Load(document->sourcePath, sourceError);
    document->metaDocument.Load(document->metaPath, document->lastError);
    if (!document->metaDocument.IsLoaded() && document->lastError.empty() && !sourceError.empty())
        document->lastError = std::move(sourceError);

    m_Documents.emplace(key, document);
    return document;
}

std::shared_ptr<VansOpenAssetDocument> VansAssetDocumentRegistry::Find(const std::filesystem::path& sourcePath) const
{
    const auto found = m_Documents.find(PathKey(Normalize(sourcePath)));
    return found == m_Documents.end() ? nullptr : found->second;
}

std::vector<std::shared_ptr<VansOpenAssetDocument>> VansAssetDocumentRegistry::DirtyDocuments() const
{
    std::vector<std::shared_ptr<VansOpenAssetDocument>> result;
    for (const auto& [path, document] : m_Documents)
        if (document && document->IsDirty())
            result.push_back(document);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left->sourcePath < right->sourcePath;
    });
    return result;
}

bool VansAssetDocumentRegistry::HasDirtyDocuments() const
{
    for (const auto& [path, document] : m_Documents)
        if (document && document->IsDirty())
            return true;
    return false;
}

std::size_t VansAssetDocumentRegistry::DirtyDocumentCount() const
{
    std::size_t count = 0;
    for (const auto& [path, document] : m_Documents)
        if (document && document->IsDirty())
            ++count;
    return count;
}

void VansAssetDocumentRegistry::Clear()
{
    m_Documents.clear();
}

std::filesystem::path VansAssetDocumentRegistry::Normalize(const std::filesystem::path& path)
{
    return std::filesystem::absolute(path).lexically_normal();
}

std::wstring VansAssetDocumentRegistry::PathKey(const std::filesystem::path& path)
{
    std::wstring key = path.generic_wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) { return std::towlower(value); });
    return key;
}
}
