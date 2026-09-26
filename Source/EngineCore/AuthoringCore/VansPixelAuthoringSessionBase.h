#pragma once

#include "VansAssetDocumentRegistry.h"
#include "../AssetCore/VansAssetDatabase.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Vans
{
class VansAssetObjectRepository;

class VansPixelAuthoringSessionBase : public IVansAssetDocumentCompanion
{
protected:
	template <typename TSession, typename Compatible>
	static bool ResolveExistingSession(
		const std::shared_ptr<VansOpenAssetDocument>& document,
		Compatible&& compatible,
		const char* conflictMessage,
		std::shared_ptr<TSession>& session,
		std::string& error)
	{
		session.reset();
		if (!document) return true;
		if (const auto existing = document->companion.lock())
		{
			session = std::dynamic_pointer_cast<TSession>(existing);
			if (!session || !compatible(*session))
			{
				session.reset();
				error = conflictMessage;
				return false;
			}
		}
		return true;
	}

	static std::shared_ptr<VansOpenAssetDocument> OpenDocument(
		const VansAssetRecord& record,
		const char* openError,
		std::string& error);

	bool InitializePixelSession(
		const VansAssetRecord& record,
		VansAssetObjectRepository& repository,
		std::shared_ptr<VansOpenAssetDocument> document,
		std::vector<std::filesystem::path> pixelPaths,
		bool allowMissingPixels,
		const char* missingPixelsError,
		bool appendMissingPath,
		std::string& error);

	void AttachCompanion(const std::shared_ptr<IVansAssetDocumentCompanion>& companion);
	bool ValidatePixelSourcesUnchanged(const char* changedError, std::string& error) const;
	bool ObservePublishedPixelSources(
		const char* missingError,
		bool appendMissingPath,
		std::string& error);
	void AdoptObservedPixelSources();
	std::uint64_t MixDocumentState(std::uint64_t contentHash) const;

	VansAssetRecord m_Record;
	VansAssetObjectRepository* m_Repository = nullptr;
	std::shared_ptr<VansOpenAssetDocument> m_Document;
	std::vector<std::filesystem::path> m_PixelPaths;
	std::vector<VansAssetFileFingerprint> m_LoadedPixelFingerprints;
	std::vector<VansAssetFileFingerprint> m_ObservedPixelFingerprints;
};
}
