#include "VansTerrainAuthoringSession.h"

#include "../VansAssetDocumentEditService.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../TerrainCore/Serialization/VansTerrainAssetCodec.h"
#include "../../TerrainCore/Serialization/VansTerrainImageCodec.h"
#include "../../TerrainCore/VansTerrainSurfaceQuery.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <system_error>

namespace Vans
{
namespace
{
bool IsHeightOperation(VansTerrainBrushOperation operation)
{
	return operation == VansTerrainBrushOperation::Raise ||
		operation == VansTerrainBrushOperation::Lower ||
		operation == VansTerrainBrushOperation::SmoothHeight ||
		operation == VansTerrainBrushOperation::Flatten ||
		operation == VansTerrainBrushOperation::Noise;
}

template <typename T>
std::vector<T> ExtractRect(
	const std::vector<T>& source,
	std::uint32_t sourceWidth,
	const VansTerrainDirtyRect& rect,
	std::size_t components)
{
	std::vector<T> result(static_cast<std::size_t>(rect.Width()) * rect.Height() * components);
	for (std::uint32_t row = 0; row < rect.Height(); ++row)
	{
		const std::size_t sourceOffset =
			(static_cast<std::size_t>(rect.minY + row) * sourceWidth + rect.minX) * components;
		const std::size_t destinationOffset = static_cast<std::size_t>(row) * rect.Width() * components;
		std::copy_n(source.data() + sourceOffset,
			static_cast<std::size_t>(rect.Width()) * components,
			result.data() + destinationOffset);
	}
	return result;
}

template <typename T>
void WriteRect(
	std::vector<T>& destination,
	std::uint32_t destinationWidth,
	const VansTerrainDirtyRect& rect,
	std::size_t components,
	const std::vector<T>& source)
{
	for (std::uint32_t row = 0; row < rect.Height(); ++row)
	{
		const std::size_t destinationOffset =
			(static_cast<std::size_t>(rect.minY + row) * destinationWidth + rect.minX) * components;
		const std::size_t sourceOffset = static_cast<std::size_t>(row) * rect.Width() * components;
		std::copy_n(source.data() + sourceOffset,
			static_cast<std::size_t>(rect.Width()) * components,
			destination.data() + destinationOffset);
	}
}

template <typename T>
std::vector<T> MergeCapturedRect(
	const std::vector<T>& current,
	std::uint32_t currentWidth,
	std::size_t components,
	const VansTerrainDirtyRect& oldRect,
	const std::vector<T>& oldBefore,
	const VansTerrainDirtyRect& newRect,
	const std::vector<T>& newBefore,
	const VansTerrainDirtyRect& combinedRect)
{
	std::vector<T> merged = ExtractRect(current, currentWidth, combinedRect, components);
	const auto copyIntoCombined = [&](const VansTerrainDirtyRect& rect, const std::vector<T>& values)
	{
		if (!rect.valid)
			return;
		for (std::uint32_t row = 0; row < rect.Height(); ++row)
		{
			const std::size_t destinationOffset =
				(static_cast<std::size_t>(rect.minY - combinedRect.minY + row) * combinedRect.Width() +
					(rect.minX - combinedRect.minX)) * components;
			const std::size_t sourceOffset = static_cast<std::size_t>(row) * rect.Width() * components;
			std::copy_n(values.data() + sourceOffset,
				static_cast<std::size_t>(rect.Width()) * components,
				merged.data() + destinationOffset);
		}
	};
	copyIntoCombined(newRect, newBefore);
	copyIntoCombined(oldRect, oldBefore);
	return merged;
}

bool SamePixelData(
	const VansTerrainAsset& working,
	const std::vector<std::uint16_t>& heights,
	const std::array<std::vector<std::uint8_t>, 2>& splats)
{
	return working.heights == heights && working.splatPixels == splats;
}
}

std::shared_ptr<VansTerrainAuthoringSession> VansTerrainAuthoringSession::Open(
	const VansAssetRecord& record,
	std::shared_ptr<const VansTerrainAsset> asset,
	std::array<std::filesystem::path, 3> imagePaths,
	VansAssetObjectRepository& repository,
	std::string& error)
{
	error.clear();
	if (record.type != VansAssetType::Terrain || !asset || !asset->HasPixelData())
	{
		error = "Terrain authoring requires a complete indexed terrain asset";
		return {};
	}
	auto document = OpenDocument(record, "Terrain asset document cannot be opened", error);
	if (!document) return {};
	const std::vector<std::filesystem::path> pixelPaths(imagePaths.begin(), imagePaths.end());
	std::shared_ptr<VansTerrainAuthoringSession> existing;
	if (!ResolveExistingSession(document,
		[&](const auto& session)
		{
			return session.m_Record.guid == record.guid &&
				session.m_PixelPaths == pixelPaths &&
				session.m_Repository == &repository;
		},
		"Terrain document already belongs to a different editing target",
		existing, error)) return {};
	if (existing) return existing;
	auto session = std::shared_ptr<VansTerrainAuthoringSession>(new VansTerrainAuthoringSession());
	if (!session->InitializePixelSession(record, repository, document, pixelPaths, false,
		"Terrain image sidecar is missing", true, error)) return {};
	session->m_Working = *asset;
	session->m_SavedHeights = asset->heights;
	session->m_SavedSplats = asset->splatPixels;
	session->AttachCompanion(session);
	return session;
}

bool VansTerrainAuthoringSession::BeginStroke(
	VansTerrainBrushOperation operation,
	std::string& error)
{
	error.clear();
	if (m_StrokeActive)
	{
		error = "A terrain brush stroke is already active";
		return false;
	}
	m_StrokeActive = true;
	m_StrokeHeight = IsHeightOperation(operation);
	m_StrokeDirty = {};
	m_StrokeHeightBefore.clear();
	for (auto& pixels : m_StrokeSplatBefore) pixels.clear();
	return true;
}

VansTerrainBrushResult VansTerrainAuthoringSession::ApplyDab(const VansTerrainBrushDab& dab)
{
	VansTerrainBrushResult result;
	if (!m_StrokeActive || m_StrokeHeight != IsHeightOperation(dab.operation))
	{
		result.error = "Terrain dab does not match the active brush stroke";
		return result;
	}
	const VansTerrainDirtyRect affected = VansTerrainBrush::CalculateAffectedRect(m_Working, dab);
	std::vector<std::uint16_t> heightBefore;
	std::array<std::vector<std::uint8_t>, 2> splatBefore;
	if (affected.valid)
	{
		if (m_StrokeHeight)
			heightBefore = ExtractRect(m_Working.heights, m_Working.width, affected, 1);
		else
			for (std::size_t index = 0; index < splatBefore.size(); ++index)
				splatBefore[index] = ExtractRect(
					m_Working.splatPixels[index], m_Working.width, affected, 4);
	}

	result = VansTerrainBrush::Apply(m_Working, dab);
	if (!result || !result.changed)
		return result;
	const VansTerrainDirtyRect oldDirty = m_StrokeDirty;
	VansTerrainDirtyRect combined = oldDirty;
	combined.Include(result.dirtyRect);
	if (m_StrokeHeight)
	{
		m_StrokeHeightBefore = MergeCapturedRect(
			m_Working.heights, m_Working.width, 1,
			oldDirty, m_StrokeHeightBefore,
			result.dirtyRect, heightBefore, combined);
	}
	else
	{
		for (std::size_t index = 0; index < m_StrokeSplatBefore.size(); ++index)
		{
			m_StrokeSplatBefore[index] = MergeCapturedRect(
				m_Working.splatPixels[index], m_Working.width, 4,
				oldDirty, m_StrokeSplatBefore[index],
				result.dirtyRect, splatBefore[index], combined);
		}
	}
	m_StrokeDirty = combined;
	VansTerrainPixelChange change;
	change.rect = result.dirtyRect;
	change.height = m_StrokeHeight;
	change.splat0 = !m_StrokeHeight;
	change.splat1 = !m_StrokeHeight;
	if (m_PendingPixelChange)
	{
		m_PendingPixelChange->rect.Include(change.rect);
		m_PendingPixelChange->height |= change.height;
		m_PendingPixelChange->splat0 |= change.splat0;
		m_PendingPixelChange->splat1 |= change.splat1;
	}
	else
		m_PendingPixelChange = change;
	RefreshPixelDirty();
	return result;
}

bool VansTerrainAuthoringSession::EndStroke(std::string& error)
{
	error.clear();
	if (!m_StrokeActive)
		return true;
	m_StrokeActive = false;
	if (!m_StrokeDirty.valid)
	{
		CancelStroke();
		return true;
	}

	auto patch = std::make_shared<PixelPatch>();
	patch->rect = m_StrokeDirty;
	patch->height = m_StrokeHeight;
	if (patch->height)
	{
		patch->heightBefore = m_StrokeHeightBefore;
		patch->heightAfter = ExtractRect(m_Working.heights, m_Working.width, patch->rect, 1);
	}
	else
	{
		for (std::size_t index = 0; index < 2; ++index)
		{
			patch->splatBefore[index] = m_StrokeSplatBefore[index];
			patch->splatAfter[index] = ExtractRect(m_Working.splatPixels[index], m_Working.width, patch->rect, 4);
		}
	}

	std::weak_ptr<VansTerrainAuthoringSession> weak = shared_from_this();
	const AssetDocumentEditResult recorded = VansAssetDocumentEditService::RecordExternalEdit(
		m_Document->sourceDocument,
		[weak, patch]()
		{
			const auto session = weak.lock();
			return session && session->ApplyPatch(*patch, false);
		},
		[weak, patch]()
		{
			const auto session = weak.lock();
			return session && session->ApplyPatch(*patch, true);
		});
	CancelStroke();
	if (!recorded)
	{
		error = recorded.message;
		return false;
	}
	const bool published = PublishWorkingSnapshot(error);
	return published;
}

void VansTerrainAuthoringSession::CancelStroke()
{
	m_StrokeActive = false;
	m_StrokeHeight = false;
	m_StrokeDirty = {};
	m_StrokeHeightBefore.clear();
	for (auto& pixels : m_StrokeSplatBefore) pixels.clear();
}

bool VansTerrainAuthoringSession::ApplyDefinition(
	const VansTerrainAssetSettings& settings,
	std::string& error)
{
	VansTerrainAsset candidate = m_Working;
	candidate.settings = settings;
	VansSerializedValue root;
	if (!VansTerrainAssetCodec::EncodeDefinition(candidate, root, error))
		return false;
	const AssetDocumentEditResult edit =
		VansAssetDocumentEditService::ReplaceRoot(m_Document->sourceDocument, root);
	if (!edit)
	{
		error = edit.message;
		return false;
	}
	m_Working.settings = settings;
	m_Working.sourceRoot = std::move(root);
	return PublishWorkingSnapshot(error);
}

bool VansTerrainAuthoringSession::SyncDefinitionFromDocument(std::string& error)
{
	VansTerrainAsset definition;
	if (!VansTerrainAssetCodec::DecodeDefinition(
		m_Document->sourceDocument.SerializedRootSnapshot(), definition, error))
		return false;
	definition.sourcePath = m_Working.sourcePath;
	definition.width = m_Working.width;
	definition.height = m_Working.height;
	definition.heights = m_Working.heights;
	definition.splatPixels = m_Working.splatPixels;
	m_Working = std::move(definition);
	return PublishWorkingSnapshot(error);
}

bool VansTerrainAuthoringSession::PublishWorkingSnapshot(std::string& error)
{
	if (!m_Repository || !m_Document)
	{
		error = "Terrain authoring repository is unavailable";
		return false;
	}
	auto snapshot = std::make_shared<const VansTerrainAsset>(m_Working);
	std::uint64_t contentHash = MixDocumentState(HashTerrainAssetContent(*snapshot));
	if (contentHash == 0) contentHash = 1;
	const std::vector<VansAssetGuid> dependencies = m_Working.Dependencies();
	const auto handle = m_Repository->Publish<VansTerrainAsset>(
		m_Record.guid,
		VansAssetType::Terrain,
		contentHash,
		std::move(snapshot),
		dependencies,
		error);
	return handle.IsValid();
}

bool VansTerrainAuthoringSession::ApplyPatch(const PixelPatch& patch, bool useAfter)
{
	if (!patch.rect.valid)
		return false;
	if (patch.height)
		WriteRect(m_Working.heights, m_Working.width, patch.rect, 1,
			useAfter ? patch.heightAfter : patch.heightBefore);
	else
		for (std::size_t index = 0; index < 2; ++index)
			WriteRect(m_Working.splatPixels[index], m_Working.width, patch.rect, 4,
				useAfter ? patch.splatAfter[index] : patch.splatBefore[index]);
	VansTerrainPixelChange change;
	change.rect = patch.rect;
	change.height = patch.height;
	change.splat0 = !patch.height;
	change.splat1 = !patch.height;
	m_PendingPixelChange = change;
	RefreshPixelDirty();
	std::string ignored;
	return PublishWorkingSnapshot(ignored);
}

void VansTerrainAuthoringSession::RefreshPixelDirty()
{
	m_PixelDirty = !SamePixelData(m_Working, m_SavedHeights, m_SavedSplats);
}

std::optional<VansTerrainPixelChange> VansTerrainAuthoringSession::TakePendingPixelChange()
{
	auto result = std::move(m_PendingPixelChange);
	m_PendingPixelChange.reset();
	return result;
}

bool VansTerrainAuthoringSession::Raycast(
	const std::array<float, 3>& origin,
	const std::array<float, 3>& direction,
	std::array<float, 3>& worldHit,
	float& pixelX,
	float& pixelY) const
{
	if (!m_Working.HasPixelData())
		return false;
	VansTerrainSurfaceRayHit hit;
	if (!VansTerrainSurfaceQuery::Raycast(m_Working, origin, direction,
		(std::numeric_limits<float>::max)(), hit)) return false;
	worldHit = hit.position;
	pixelX = hit.pixelX;
	pixelY = hit.pixelY;
	return true;
}

bool VansTerrainAuthoringSession::IsDirty() const
{
	return m_PixelDirty;
}

bool VansTerrainAuthoringSession::StageSave(
	std::vector<VansStagedFile>& files,
	std::string& error)
{
	files.clear();
	if (!m_PixelDirty)
		return true;
	if (!ValidatePixelSourcesUnchanged("Terrain image changed on disk", error)) return false;

	std::array<std::string, 3> encoded;
	VansTerrainHeightImage height{ m_Working.width, m_Working.height, m_Working.heights };
	if (!VansTerrainImageCodec::EncodeHeight16(height, encoded[0], error)) return false;
	for (std::size_t index = 0; index < 2; ++index)
	{
		VansTerrainWeightImage weights{ m_Working.width, m_Working.height, m_Working.splatPixels[index] };
		if (!VansTerrainImageCodec::EncodeWeightsRGBA8(weights, encoded[index + 1], error)) return false;
	}

	VansScopedIOContext io(VansIODomain::Authoring, "TerrainAuthoring.StageImages", true);
	for (std::size_t index = 0; index < m_PixelPaths.size(); ++index)
	{
		VansStagedFile stage;
		if (!VansFileStorage::StageWriteBytes(m_PixelPaths[index], encoded[index], stage, error))
		{
			for (const VansStagedFile& created : files)
			{
				std::error_code ignored;
				std::filesystem::remove(created.temporaryPath, ignored);
			}
			files.clear();
			return false;
		}
		files.push_back(std::move(stage));
	}
	return true;
}

bool VansTerrainAuthoringSession::ObservePublishedSave(std::string& error)
{
	return ObservePublishedPixelSources("Saved terrain image is missing", true, error);
}

void VansTerrainAuthoringSession::AdoptObservedSave()
{
	AdoptObservedPixelSources();
	m_SavedHeights = m_Working.heights;
	m_SavedSplats = m_Working.splatPixels;
	m_PixelDirty = false;
}
}
