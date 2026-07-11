#pragma once

#include "../AssetCore/VansAssetDocument.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansOpenAssetDocument
{
    std::filesystem::path sourcePath;
    std::filesystem::path metaPath;
    VansAssetDocument sourceDocument;
    VansAssetDocument metaDocument;
    std::string lastError;

    bool IsDirty() const { return sourceDocument.IsDirty() || metaDocument.IsDirty(); }
};

class VansAssetDocumentRegistry
{
public:
    static VansAssetDocumentRegistry& Get();

    std::shared_ptr<VansOpenAssetDocument> GetOrOpen(const std::filesystem::path& sourcePath);
    std::shared_ptr<VansOpenAssetDocument> Find(const std::filesystem::path& sourcePath) const;
    std::vector<std::shared_ptr<VansOpenAssetDocument>> DirtyDocuments() const;
    bool HasDirtyDocuments() const;
    std::size_t DirtyDocumentCount() const;
    void Clear();

private:
    static std::filesystem::path Normalize(const std::filesystem::path& path);
    static std::wstring PathKey(const std::filesystem::path& path);

    std::unordered_map<std::wstring, std::shared_ptr<VansOpenAssetDocument>> m_Documents;
};
}
