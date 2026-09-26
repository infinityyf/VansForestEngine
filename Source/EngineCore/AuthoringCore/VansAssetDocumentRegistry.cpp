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

std::shared_ptr<VansOpenAssetDocument> VansAssetDocumentRegistry::CreateInMemory(
	const std::filesystem::path& sourcePath,
	VansSerializedValue sourceRoot,
	VansSerializedValue metaRoot,
	bool saveWithScene,
	std::string& error)
{
	error.clear();
	const std::filesystem::path normalized = Normalize(sourcePath);
	const std::wstring key = PathKey(normalized);
	if (const auto found = m_Documents.find(key); found != m_Documents.end())
		return found->second;

	auto document = std::make_shared<VansOpenAssetDocument>();
	document->sourcePath = normalized;
	document->metaPath = VansAssetMeta::MetaPathFor(normalized);
	document->saveWithScene = saveWithScene;
	if (!document->sourceDocument.InitializeNew(
			document->sourcePath, std::move(sourceRoot), error) ||
		!document->metaDocument.InitializeNew(
			document->metaPath, std::move(metaRoot), error))
	{
		return nullptr;
	}
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

std::vector<std::shared_ptr<VansOpenAssetDocument>>
VansAssetDocumentRegistry::SceneOwnedDirtyDocuments() const
{
	std::vector<std::shared_ptr<VansOpenAssetDocument>> result;
	for (const auto& [path, document] : m_Documents)
		if (document && document->saveWithScene && document->IsDirty())
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

void VansAssetDocumentRegistry::SetWorkingCopyPublisher(WorkingCopyPublisher publisher)
{
	m_WorkingCopyPublisher = std::move(publisher);
}

void VansAssetDocumentRegistry::ClearWorkingCopyPublisher()
{
	m_WorkingCopyPublisher = {};
}

bool VansAssetDocumentRegistry::PublishWorkingCopy(const VansAssetDocument& changedDocument)
{
	for (const auto& [path, document] : m_Documents)
	{
		(void)path;
		if (!document ||
			(&document->sourceDocument != &changedDocument &&
			 &document->metaDocument != &changedDocument))
			continue;
		if (!m_WorkingCopyPublisher)
			return true;
		std::string error;
		if (!m_WorkingCopyPublisher(*document, error))
		{
			document->lastError = error.empty()
				? "Asset working copy could not publish a runtime preview snapshot"
				: std::move(error);
			return false;
		}
		document->lastError.clear();
		return true;
	}
	return true;
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
