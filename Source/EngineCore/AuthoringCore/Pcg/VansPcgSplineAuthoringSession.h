#pragma once

#include "../../PcgCore/VansPcgSplineAsset.h"
#include "VansPcgTerrainPreviewScheduler.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Vans
{
class VansAssetDatabase;
class VansAssetObjectRepository;
struct VansOpenAssetDocument;
struct VansTerrainAsset;

struct VansPcgSplineBuildRequest
{
	VansPcgSplineAsset asset;
	std::shared_ptr<const VansTerrainAsset> terrain;
	std::uint64_t requestId = 0;
	explicit operator bool() const { return terrain != nullptr; }
};

class VansPcgSplineAuthoringSession
{
public:
	bool MatchesContext(const std::string& projectRoot, std::uint64_t sceneRevision) const;
	void ResetContext(std::string projectRoot, std::uint64_t sceneRevision);
	bool BindAsset(
		const std::string& guid,
		VansAssetDatabase* database,
		const VansAssetObjectRepository& repository,
		VansAssetGuid unboundTerrain);
	bool SynchronizeDocument(const VansAssetObjectRepository& repository, bool editMode);
	bool RequestPreview(
		const VansAssetObjectRepository& repository,
		std::shared_ptr<const VansTerrainAsset> terrainOverride,
		std::string& error);
	void NotifyTerrainHeightChanged(
		VansPcgTerrainPreviewScheduler::Clock::time_point now =
			VansPcgTerrainPreviewScheduler::Clock::now());
	bool IsTerrainPreviewRequestDue(
		bool buildInFlight,
		VansPcgTerrainPreviewScheduler::Clock::time_point now =
			VansPcgTerrainPreviewScheduler::Clock::now()) const;
	std::uint32_t RequestPendingTerrainPreview(
		const VansAssetObjectRepository& repository,
		std::shared_ptr<const VansTerrainAsset> terrainOverride,
		bool buildInFlight,
		std::string& error,
		VansPcgTerrainPreviewScheduler::Clock::time_point now =
			VansPcgTerrainPreviewScheduler::Clock::now());
	VansPcgSplineBuildRequest TakeBuildRequest();

	bool BeginEdit(std::string& error);
	void CancelEdit();
	bool ReplaceDraft(VansPcgSplineAsset asset, std::string& error);
	bool CommitAsset(VansPcgSplineAsset asset, std::string& error);
	bool FinishEdit(std::string& error);

	const std::string& ProjectRoot() const { return m_ProjectRoot; }
	const std::string& Guid() const { return m_Guid; }
	const std::string& SelectedSpline() const { return m_SelectedSpline; }
	const std::string& SelectedPoint() const { return m_SelectedPoint; }
	const std::string& Message() const { return m_Message; }
	std::uint64_t RequestId() const { return m_RequestId; }
	std::shared_ptr<VansOpenAssetDocument> Document() const { return m_Document; }
	const VansPcgSplineAsset& WorkingAsset() const { return m_Working; }
	bool ToolEnabled() const { return m_ToolEnabled; }
	bool Dragging() const { return m_Dragging; }
	bool NeedsBuild() const { return m_NeedsBuild; }

	void SetSelection(std::string spline, std::string point)
	{
		m_SelectedSpline = std::move(spline);
		m_SelectedPoint = std::move(point);
	}
	void SetSelectedPoint(std::string point) { m_SelectedPoint = std::move(point); }
	void ClearSelection() { m_SelectedSpline.clear(); m_SelectedPoint.clear(); }
	void SetToolEnabled(bool enabled) { m_ToolEnabled = enabled; }
	void SetMessage(std::string message) { m_Message = std::move(message); }

private:
	bool PublishAsset(VansPcgSplineAsset asset, bool commitDocument, std::string& error);

	std::string m_ProjectRoot;
	std::uint64_t m_SceneRevision = 0;
	std::string m_Guid;
	std::string m_SelectedSpline;
	std::string m_SelectedPoint;
	std::string m_Message;
	std::uint64_t m_RequestId = 0;
	std::uint64_t m_ObservedDocumentState = 0;
	std::shared_ptr<VansOpenAssetDocument> m_Document;
	bool m_ToolEnabled = false;
	bool m_Dragging = false;
	bool m_NeedsBuild = false;
	VansPcgSplineAsset m_Working;
	std::shared_ptr<const VansTerrainAsset> m_Terrain;
	VansPcgTerrainPreviewScheduler m_TerrainPreviewScheduler;
};
}
