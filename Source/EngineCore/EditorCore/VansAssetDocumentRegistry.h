#pragma once

#include "../AssetCore/VansAssetDocument.h"

#include <filesystem>
#include <functional>
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
	bool saveWithScene = false;

    bool IsDirty() const { return sourceDocument.IsDirty() || metaDocument.IsDirty(); }
};

class VansAssetDocumentRegistry
{
public:
	using WorkingCopyPublisher =
		std::function<bool(const VansOpenAssetDocument&, std::string&)>;

    static VansAssetDocumentRegistry& Get();

    std::shared_ptr<VansOpenAssetDocument> GetOrOpen(const std::filesystem::path& sourcePath);
	std::shared_ptr<VansOpenAssetDocument> CreateInMemory(
		const std::filesystem::path& sourcePath,
		VansSerializedValue sourceRoot,
		VansSerializedValue metaRoot,
		bool saveWithScene,
		std::string& error);
    std::shared_ptr<VansOpenAssetDocument> Find(const std::filesystem::path& sourcePath) const;
    std::vector<std::shared_ptr<VansOpenAssetDocument>> DirtyDocuments() const;
	std::vector<std::shared_ptr<VansOpenAssetDocument>> SceneOwnedDirtyDocuments() const;
    bool HasDirtyDocuments() const;
    std::size_t DirtyDocumentCount() const;
	void SetWorkingCopyPublisher(WorkingCopyPublisher publisher);
	void ClearWorkingCopyPublisher();
	bool PublishWorkingCopy(const VansAssetDocument& changedDocument);
    void Clear();

private:
    static std::filesystem::path Normalize(const std::filesystem::path& path);
    static std::wstring PathKey(const std::filesystem::path& path);

    std::unordered_map<std::wstring, std::shared_ptr<VansOpenAssetDocument>> m_Documents;
	WorkingCopyPublisher m_WorkingCopyPublisher;
};
}
