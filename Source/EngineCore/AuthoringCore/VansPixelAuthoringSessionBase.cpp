#include "VansPixelAuthoringSessionBase.h"

namespace Vans
{
std::shared_ptr<VansOpenAssetDocument> VansPixelAuthoringSessionBase::OpenDocument(
	const VansAssetRecord& record,
	const char* openError,
	std::string& error)
{
	auto document = VansAssetDocumentRegistry::Get().GetOrOpen(record.sourcePath);
	if (!document || !document->sourceDocument.IsLoaded())
	{
		error = document ? document->lastError : openError;
		return {};
	}
	return document;
}

bool VansPixelAuthoringSessionBase::InitializePixelSession(
	const VansAssetRecord& record,
	VansAssetObjectRepository& repository,
	std::shared_ptr<VansOpenAssetDocument> document,
	std::vector<std::filesystem::path> pixelPaths,
	bool allowMissingPixels,
	const char* missingPixelsError,
	bool appendMissingPath,
	std::string& error)
{
	m_Record = record;
	m_Repository = &repository;
	m_Document = std::move(document);
	m_PixelPaths = std::move(pixelPaths);
	m_LoadedPixelFingerprints.clear();
	m_LoadedPixelFingerprints.reserve(m_PixelPaths.size());
	for (const auto& path : m_PixelPaths)
	{
		auto fingerprint = VansAssetDocument::Fingerprint(path, error);
		if (!error.empty()) return false;
		if (!fingerprint.exists && !allowMissingPixels)
		{
			error = missingPixelsError;
			if (appendMissingPath) error += ": " + path.string();
			return false;
		}
		m_LoadedPixelFingerprints.push_back(fingerprint);
	}
	m_ObservedPixelFingerprints.assign(m_PixelPaths.size(), {});
	return true;
}

void VansPixelAuthoringSessionBase::AttachCompanion(
	const std::shared_ptr<IVansAssetDocumentCompanion>& companion)
{
	m_Document->companion = companion;
}

bool VansPixelAuthoringSessionBase::ValidatePixelSourcesUnchanged(
	const char* changedError,
	std::string& error) const
{
	for (std::size_t index = 0; index < m_PixelPaths.size(); ++index)
	{
		const auto current = VansAssetDocument::Fingerprint(m_PixelPaths[index], error);
		if (!error.empty()) return false;
		if (current != m_LoadedPixelFingerprints[index])
		{
			error = std::string(changedError) + ": " + m_PixelPaths[index].string();
			return false;
		}
	}
	return true;
}

bool VansPixelAuthoringSessionBase::ObservePublishedPixelSources(
	const char* missingError,
	bool appendMissingPath,
	std::string& error)
{
	for (std::size_t index = 0; index < m_PixelPaths.size(); ++index)
	{
		m_ObservedPixelFingerprints[index] = VansAssetDocument::Fingerprint(m_PixelPaths[index], error);
		if (!error.empty()) return false;
		if (!m_ObservedPixelFingerprints[index].exists)
		{
			error = missingError;
			if (appendMissingPath) error += ": " + m_PixelPaths[index].string();
			return false;
		}
	}
	return true;
}

void VansPixelAuthoringSessionBase::AdoptObservedPixelSources()
{
	m_LoadedPixelFingerprints = m_ObservedPixelFingerprints;
}

std::uint64_t VansPixelAuthoringSessionBase::MixDocumentState(std::uint64_t contentHash) const
{
	contentHash ^= m_Document->sourceDocument.CurrentStateId() + 0x9e3779b97f4a7c15ull +
		(contentHash << 6u) + (contentHash >> 2u);
	return contentHash;
}
}
