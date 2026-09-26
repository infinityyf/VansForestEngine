#include "VansPcgSplineAuthoringSession.h"

#include "../VansAssetDocumentEditService.h"
#include "../VansAssetDocumentRegistry.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../PcgCore/Serialization/VansPcgSplineAssetCodec.h"
#include "../../TerrainCore/VansTerrainAsset.h"

namespace Vans
{
bool VansPcgSplineAuthoringSession::MatchesContext(
	const std::string& projectRoot,
	std::uint64_t sceneRevision) const
{
	return m_ProjectRoot == projectRoot && m_SceneRevision == sceneRevision;
}

void VansPcgSplineAuthoringSession::ResetContext(
	std::string projectRoot,
	std::uint64_t sceneRevision)
{
	*this = VansPcgSplineAuthoringSession{};
	m_ProjectRoot = std::move(projectRoot);
	m_SceneRevision = sceneRevision;
}

bool VansPcgSplineAuthoringSession::BindAsset(
	const std::string& guid,
	VansAssetDatabase* database,
	const VansAssetObjectRepository& repository,
	VansAssetGuid unboundTerrain)
{
	if (guid == m_Guid) return false;
	++m_RequestId;
	m_Guid = guid;
	m_Document.reset();
	m_Dragging = false;
	m_NeedsBuild = false;
	m_ObservedDocumentState = 0;
	ClearSelection();
	m_ToolEnabled = false;
	m_Message.clear();
	m_Working = {};
	m_Terrain.reset();

	VansAssetGuid assetGuid;
	VansAssetGuid::TryParse(guid, assetGuid);
	const auto record = database ? database->Find(assetGuid) : std::nullopt;
	if (record && record->type == VansAssetType::PcgSpline)
	{
		m_Document = VansAssetDocumentRegistry::Get().GetOrOpen(record->sourcePath);
		if (m_Document) m_Document->saveWithScene = true;
	}
	if (guid.empty() && unboundTerrain.IsValid())
	{
		m_Working.name = "Unbound splines";
		m_Working.terrain = unboundTerrain;
		m_Terrain = repository.ResolveLatest<VansTerrainAsset>(unboundTerrain);
		m_NeedsBuild = bool(m_Terrain);
	}
	return true;
}

bool VansPcgSplineAuthoringSession::SynchronizeDocument(
	const VansAssetObjectRepository& repository,
	bool editMode)
{
	if (!editMode)
	{
		m_ToolEnabled = false;
		return false;
	}
	if (!m_Document || m_Dragging ||
		m_ObservedDocumentState == m_Document->sourceDocument.CurrentStateId())
		return false;

	m_ObservedDocumentState = m_Document->sourceDocument.CurrentStateId();
	if (!VansPcgSplineAssetCodec::Decode(
			m_Document->sourceDocument.SerializedRootSnapshot(), m_Working, m_Message))
		return false;
	m_Terrain = repository.ResolveLatest<VansTerrainAsset>(m_Working.terrain);
	m_NeedsBuild = bool(m_Terrain);
	++m_RequestId;
	return true;
}

bool VansPcgSplineAuthoringSession::RequestPreview(
	const VansAssetObjectRepository& repository,
	std::shared_ptr<const VansTerrainAsset> terrainOverride,
	std::string& error)
{
	if (!m_Dragging && m_Document &&
		!VansPcgSplineAssetCodec::Decode(
			m_Document->sourceDocument.SerializedRootSnapshot(), m_Working, m_Message))
	{
		error = m_Message;
		return false;
	}
	m_Terrain = repository.ResolveLatest<VansTerrainAsset>(m_Working.terrain);
	if (terrainOverride) m_Terrain = std::move(terrainOverride);
	if (!m_Terrain)
	{
		error = "Spline terrain is unavailable.";
		return false;
	}
	m_NeedsBuild = true;
	++m_RequestId;
	if (m_Document)
		m_ObservedDocumentState = m_Document->sourceDocument.CurrentStateId();
	error.clear();
	return true;
}

void VansPcgSplineAuthoringSession::NotifyTerrainHeightChanged(
	VansPcgTerrainPreviewScheduler::Clock::time_point now)
{
	m_TerrainPreviewScheduler.NotifyHeightChanged(now);
}

bool VansPcgSplineAuthoringSession::IsTerrainPreviewRequestDue(
	bool buildInFlight,
	VansPcgTerrainPreviewScheduler::Clock::time_point now) const
{
	return m_TerrainPreviewScheduler.IsRequestDue(buildInFlight, now);
}

std::uint32_t VansPcgSplineAuthoringSession::RequestPendingTerrainPreview(
	const VansAssetObjectRepository& repository,
	std::shared_ptr<const VansTerrainAsset> terrainOverride,
	bool buildInFlight,
	std::string& error,
	VansPcgTerrainPreviewScheduler::Clock::time_point now)
{
	if (!m_TerrainPreviewScheduler.IsRequestDue(buildInFlight, now))
	{
		error.clear();
		return 0;
	}
	if (!RequestPreview(repository, std::move(terrainOverride), error))
		return 0;
	return m_TerrainPreviewScheduler.MarkRequestSubmitted(now);
}

VansPcgSplineBuildRequest VansPcgSplineAuthoringSession::TakeBuildRequest()
{
	if (!m_NeedsBuild || !m_Terrain) return {};
	m_NeedsBuild = false;
	return {m_Working, m_Terrain, m_RequestId};
}

bool VansPcgSplineAuthoringSession::BeginEdit(std::string& error)
{
	if (m_Dragging)
	{
		error = "A spline edit is already active.";
		return false;
	}
	m_Dragging = true;
	error.clear();
	return true;
}

void VansPcgSplineAuthoringSession::CancelEdit()
{
	m_Dragging = false;
}

bool VansPcgSplineAuthoringSession::PublishAsset(
	VansPcgSplineAsset asset,
	bool commitDocument,
	std::string& error)
{
	if (!m_Document)
	{
		error = "Open an editable spline asset.";
		return false;
	}
	VansSerializedValue root;
	if (!VansPcgSplineAssetCodec::Encode(asset, root, error)) return false;
	if (commitDocument)
	{
		const auto edit = VansAssetDocumentEditService::ReplaceRoot(
			m_Document->sourceDocument, std::move(root));
		if (!edit)
		{
			error = edit.message;
			return false;
		}
		m_ObservedDocumentState = m_Document->sourceDocument.CurrentStateId();
		m_Dragging = false;
	}
	m_Working = std::move(asset);
	error.clear();
	return true;
}

bool VansPcgSplineAuthoringSession::ReplaceDraft(VansPcgSplineAsset asset, std::string& error)
{
	if (!m_Dragging)
	{
		error = "Begin an edit before updating it.";
		return false;
	}
	return PublishAsset(std::move(asset), false, error);
}

bool VansPcgSplineAuthoringSession::CommitAsset(VansPcgSplineAsset asset, std::string& error)
{
	return PublishAsset(std::move(asset), true, error);
}

bool VansPcgSplineAuthoringSession::FinishEdit(std::string& error)
{
	if (m_Dragging && !CommitAsset(m_Working, error)) return false;
	m_ToolEnabled = false;
	error.clear();
	return true;
}
}
