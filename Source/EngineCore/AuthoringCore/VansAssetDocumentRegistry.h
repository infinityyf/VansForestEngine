#pragma once

#include "../AssetCore/VansAssetDocument.h"
#include "../AssetCore/Storage/VansStagedFileTransaction.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
class VansAssetDocumentEditHistory;
class VansAssetDocumentEditAccess;
class VansAssetDocumentEditService;

class IVansAssetDocumentCompanion
{
public:
	virtual ~IVansAssetDocumentCompanion() = default;
	virtual bool IsDirty() const = 0;
	virtual bool StageSave(std::vector<VansStagedFile>& files, std::string& error) = 0;
	virtual bool ObservePublishedSave(std::string& error) = 0;
	virtual void AdoptObservedSave() = 0;
};

struct VansOpenAssetDocument
{
    VansOpenAssetDocument();
    ~VansOpenAssetDocument();

    std::filesystem::path sourcePath;
    std::filesystem::path metaPath;
    VansAssetDocument sourceDocument;
    VansAssetDocument metaDocument;
    std::string lastError;
	bool saveWithScene = false;
	std::weak_ptr<IVansAssetDocumentCompanion> companion;

    bool IsDirty() const
	{
		const auto sidecar = companion.lock();
		return sourceDocument.IsDirty() || metaDocument.IsDirty() ||
			(sidecar && sidecar->IsDirty());
	}

private:
	friend class VansAssetDocumentEditAccess;
	friend class VansAssetDocumentEditService;
	std::unique_ptr<VansAssetDocumentEditHistory> m_SourceEditHistory;
	std::unique_ptr<VansAssetDocumentEditHistory> m_MetaEditHistory;
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
	friend class VansAssetDocumentEditAccess;
	friend class VansAssetDocumentEditService;
    static std::filesystem::path Normalize(const std::filesystem::path& path);
    static std::wstring PathKey(const std::filesystem::path& path);

    std::unordered_map<std::wstring, std::shared_ptr<VansOpenAssetDocument>> m_Documents;
	WorkingCopyPublisher m_WorkingCopyPublisher;
};
}
