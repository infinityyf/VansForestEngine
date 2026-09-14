#pragma once

#include "../VansAssetDocumentRegistry.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../PcgCore/VansPcgMaskAsset.h"

#include <memory>

namespace Vans
{
class VansAssetObjectRepository;

class VansPcgMaskAuthoringSession final
	: public IVansAssetDocumentCompanion
	, public std::enable_shared_from_this<VansPcgMaskAuthoringSession>
{
public:
	static std::shared_ptr<VansPcgMaskAuthoringSession> Open(const VansAssetRecord& record,
		std::shared_ptr<const VansPcgMaskAsset> asset, const std::filesystem::path& pixelPath,
		VansAssetObjectRepository& repository, std::string& error);

	const VansPcgMaskAsset& WorkingAsset() const { return m_Working; }
	const std::shared_ptr<VansOpenAssetDocument>& Document() const { return m_Document; }
	bool BeginStroke(const VansPcgMaskTarget& target, std::string& error);
	bool AddPoint(const VansPcgMaskTarget& target, float worldX, float worldZ, bool erase, std::string& error);
	void BreakSegment() { m_Stroke.BreakSegment(); }
	bool EndStroke(std::string& error);
	bool CancelStroke(std::string& error);
	bool StrokeActive() const { return m_Stroke.IsActive(); }
	bool SetBrush(const VansPcgBrushSettings& brush, std::string& error);
	bool SyncDefinitionFromDocument(std::string& error);
	bool PublishWorkingSnapshot(std::string& error);
	VansPcgPixelRect TakePendingPixelChange();
	bool ReplaceContent(VansPcgMaskAsset asset, std::string& error);
	bool TakeFullRefresh() { const bool value=m_FullRefresh; m_FullRefresh=false; return value; }

	bool IsDirty() const override;
	bool StageSave(std::vector<VansStagedFile>& files, std::string& error) override;
	bool ObservePublishedSave(std::string& error) override;
	void AdoptObservedSave() override;

private:
	bool ApplyEdit(const VansPcgMaskEdit& edit, bool redo);
	bool RestoreContent(const VansPcgMaskAsset& asset);
	void RefreshPixelDirty();
	VansAssetRecord m_Record;
	VansAssetObjectRepository* m_Repository = nullptr;
	std::shared_ptr<VansOpenAssetDocument> m_Document;
	VansPcgMaskAsset m_Working;
	VansPcgMaskStroke m_Stroke;
	VansPcgPixelRect m_StrokeDirty;
	VansPcgPixelRect m_PendingPixels;
	std::filesystem::path m_PixelPath;
	VansAssetFileFingerprint m_LoadedFingerprint;
	VansAssetFileFingerprint m_ObservedFingerprint;
	std::vector<std::uint16_t> m_SavedPixels;
	std::vector<std::uint16_t> m_StagedPixels;
	std::uint32_t m_SavedWidth=0, m_SavedHeight=0, m_StagedWidth=0, m_StagedHeight=0;
	bool m_FullRefresh=false;
	std::uint64_t m_StagedByteHash = 0;
	std::size_t m_StagedByteCount = 0;
	bool m_PixelDirty = false;
	bool m_HasStagedPixels = false;
};
}
