#pragma once

#include "../VansAssetDocumentRegistry.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../TerrainCore/VansTerrainAsset.h"
#include "../../TerrainCore/VansTerrainBrush.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace Vans
{
class VansAssetObjectRepository;

struct VansTerrainPixelChange
{
	VansTerrainDirtyRect rect;
	bool height = false;
	bool splat0 = false;
	bool splat1 = false;
};

class VansTerrainAuthoringSession final
	: public IVansAssetDocumentCompanion
	, public std::enable_shared_from_this<VansTerrainAuthoringSession>
{
public:
	static std::shared_ptr<VansTerrainAuthoringSession> Open(
		const VansAssetRecord& record,
		std::shared_ptr<const VansTerrainAsset> asset,
		std::array<std::filesystem::path, 3> imagePaths,
		VansAssetObjectRepository& repository,
		std::string& error);

	const VansTerrainAsset& WorkingAsset() const { return m_Working; }
	const std::shared_ptr<VansOpenAssetDocument>& Document() const { return m_Document; }
	bool BeginStroke(VansTerrainBrushOperation operation, std::string& error);
	VansTerrainBrushResult ApplyDab(const VansTerrainBrushDab& dab);
	bool EndStroke(std::string& error);
	void CancelStroke();
	bool ApplyDefinition(const VansTerrainAssetSettings& settings, std::string& error);
	bool SyncDefinitionFromDocument(std::string& error);
	bool PublishWorkingSnapshot(std::string& error);
	std::optional<VansTerrainPixelChange> TakePendingPixelChange();
	bool Raycast(
		const std::array<float, 3>& origin,
		const std::array<float, 3>& direction,
		std::array<float, 3>& worldHit,
		float& pixelX,
		float& pixelY) const;

	bool IsDirty() const override;
	bool StageSave(std::vector<VansStagedFile>& files, std::string& error) override;
	bool ObservePublishedSave(std::string& error) override;
	void AdoptObservedSave() override;

private:
	struct PixelPatch
	{
		VansTerrainDirtyRect rect;
		bool height = false;
		std::vector<std::uint16_t> heightBefore;
		std::vector<std::uint16_t> heightAfter;
		std::array<std::vector<std::uint8_t>, 2> splatBefore;
		std::array<std::vector<std::uint8_t>, 2> splatAfter;
	};

	bool ApplyPatch(const PixelPatch& patch, bool useAfter);
	void RefreshPixelDirty();
	float SampleWorldHeight(float worldX, float worldZ) const;
	static std::uint64_t CombinedContentHash(
		const VansTerrainAsset& asset,
		VansAssetDocumentStateId documentState);

	VansAssetRecord m_Record;
	VansAssetObjectRepository* m_Repository = nullptr;
	std::shared_ptr<VansOpenAssetDocument> m_Document;
	VansTerrainAsset m_Working;
	std::vector<std::uint16_t> m_SavedHeights;
	std::array<std::vector<std::uint8_t>, 2> m_SavedSplats;
	std::array<std::filesystem::path, 3> m_ImagePaths;
	std::array<VansAssetFileFingerprint, 3> m_LoadedFingerprints;
	std::array<VansAssetFileFingerprint, 3> m_ObservedFingerprints;
	bool m_PixelDirty = false;
	bool m_StrokeActive = false;
	bool m_StrokeHeight = false;
	VansTerrainDirtyRect m_StrokeDirty;
	std::vector<std::uint16_t> m_StrokeHeightBefore;
	std::array<std::vector<std::uint8_t>, 2> m_StrokeSplatBefore;
	std::optional<VansTerrainPixelChange> m_PendingPixelChange;
};
}
