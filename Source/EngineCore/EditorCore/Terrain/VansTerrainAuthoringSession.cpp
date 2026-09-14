#include "VansTerrainAuthoringSession.h"

#include "../VansAssetDocumentEditService.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../TerrainCore/Serialization/VansTerrainAssetCodec.h"
#include "../../TerrainCore/Serialization/VansTerrainImageCodec.h"

#include <algorithm>
#include <cmath>
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
	auto session = std::shared_ptr<VansTerrainAuthoringSession>(new VansTerrainAuthoringSession());
	session->m_Record = record;
	session->m_Repository = &repository;
	session->m_Working = *asset;
	session->m_SavedHeights = asset->heights;
	session->m_SavedSplats = asset->splatPixels;
	session->m_ImagePaths = std::move(imagePaths);
	for (std::size_t index = 0; index < session->m_ImagePaths.size(); ++index)
	{
		session->m_LoadedFingerprints[index] =
			VansAssetDocument::Fingerprint(session->m_ImagePaths[index], error);
		if (!error.empty() || !session->m_LoadedFingerprints[index].exists)
		{
			if (error.empty()) error = "Terrain image sidecar is missing: " + session->m_ImagePaths[index].string();
			return {};
		}
	}
	session->m_Document = VansAssetDocumentRegistry::Get().GetOrOpen(record.sourcePath);
	if (!session->m_Document || !session->m_Document->sourceDocument.IsLoaded())
	{
		error = session->m_Document ? session->m_Document->lastError : "Terrain asset document cannot be opened";
		return {};
	}
	session->m_Document->companion = session;
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

std::uint64_t VansTerrainAuthoringSession::CombinedContentHash(
	const VansTerrainAsset& asset,
	VansAssetDocumentStateId documentState)
{
	std::uint64_t hash = HashTerrainAssetContent(asset);
	hash ^= documentState + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
	return hash == 0 ? 1 : hash;
}

bool VansTerrainAuthoringSession::PublishWorkingSnapshot(std::string& error)
{
	if (!m_Repository || !m_Document)
	{
		error = "Terrain authoring repository is unavailable";
		return false;
	}
	auto snapshot = std::make_shared<const VansTerrainAsset>(m_Working);
	const std::uint64_t contentHash = CombinedContentHash(
		*snapshot, m_Document->sourceDocument.CurrentStateId());
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

float VansTerrainAuthoringSession::SampleWorldHeight(float worldX, float worldZ) const
{
	const float halfSize = m_Working.settings.terrainSize * 0.5f;
	const float u = std::clamp((worldX + halfSize) / m_Working.settings.terrainSize, 0.0f, 1.0f);
	const float v = std::clamp((worldZ + halfSize) / m_Working.settings.terrainSize, 0.0f, 1.0f);
	const float px = u * static_cast<float>(m_Working.width - 1u);
	const float py = v * static_cast<float>(m_Working.height - 1u);
	const std::uint32_t x0 = static_cast<std::uint32_t>(std::floor(px));
	const std::uint32_t y0 = static_cast<std::uint32_t>(std::floor(py));
	const std::uint32_t x1 = std::min(x0 + 1u, m_Working.width - 1u);
	const std::uint32_t y1 = std::min(y0 + 1u, m_Working.height - 1u);
	const float tx = px - x0;
	const float ty = py - y0;
	const auto sample = [&](std::uint32_t x, std::uint32_t y)
	{
		return static_cast<float>(m_Working.heights[static_cast<std::size_t>(y) * m_Working.width + x]);
	};
	const float top = sample(x0, y0) + (sample(x1, y0) - sample(x0, y0)) * tx;
	const float bottom = sample(x0, y1) + (sample(x1, y1) - sample(x0, y1)) * tx;
	const float normalized = (top + (bottom - top) * ty) / 65535.0f;
	return m_Working.settings.heightOffset + normalized * m_Working.settings.maxHeight;
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
	const float half = m_Working.settings.terrainSize * 0.5f;
	const std::array<float, 3> minimum{ -half, m_Working.settings.heightOffset, -half };
	const std::array<float, 3> maximum{ half,
		m_Working.settings.heightOffset + m_Working.settings.maxHeight, half };
	float tMin = 0.0f;
	float tMax = (std::numeric_limits<float>::max)();
	for (std::size_t axis = 0; axis < 3; ++axis)
	{
		if (std::abs(direction[axis]) < 1.0e-6f)
		{
			if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) return false;
			continue;
		}
		float a = (minimum[axis] - origin[axis]) / direction[axis];
		float b = (maximum[axis] - origin[axis]) / direction[axis];
		if (a > b) std::swap(a, b);
		tMin = std::max(tMin, a);
		tMax = std::min(tMax, b);
		if (tMin > tMax) return false;
	}
	const auto signedDistance = [&](float t)
	{
		const float x = origin[0] + direction[0] * t;
		const float y = origin[1] + direction[1] * t;
		const float z = origin[2] + direction[2] * t;
		return y - SampleWorldHeight(x, z);
	};
	const float cellSize = m_Working.settings.terrainSize /
		static_cast<float>(std::max(m_Working.width, m_Working.height) - 1u);
	const float step = std::max(cellSize * 0.5f, (tMax - tMin) / 1024.0f);
	float previousT = tMin;
	float previousDistance = signedDistance(previousT);
	for (float t = std::min(tMin + step, tMax); t <= tMax + 1.0e-5f; t = std::min(t + step, tMax))
	{
		const float distance = signedDistance(t);
		if (previousDistance >= 0.0f && distance <= 0.0f)
		{
			float low = previousT;
			float high = t;
			for (int iteration = 0; iteration < 14; ++iteration)
			{
				const float middle = (low + high) * 0.5f;
				if (signedDistance(middle) > 0.0f) low = middle;
				else high = middle;
			}
			const float hitT = (low + high) * 0.5f;
			worldHit = { origin[0] + direction[0] * hitT,
				origin[1] + direction[1] * hitT,
				origin[2] + direction[2] * hitT };
			pixelX = (worldHit[0] + half) / m_Working.settings.terrainSize * (m_Working.width - 1u);
			pixelY = (worldHit[2] + half) / m_Working.settings.terrainSize * (m_Working.height - 1u);
			return true;
		}
		if (t >= tMax) break;
		previousT = t;
		previousDistance = distance;
	}
	return false;
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
	for (std::size_t index = 0; index < m_ImagePaths.size(); ++index)
	{
		const VansAssetFileFingerprint current = VansAssetDocument::Fingerprint(m_ImagePaths[index], error);
		if (!error.empty()) return false;
		if (current != m_LoadedFingerprints[index])
		{
			error = "Terrain image changed on disk: " + m_ImagePaths[index].string();
			return false;
		}
	}

	std::array<std::string, 3> encoded;
	VansTerrainHeightImage height{ m_Working.width, m_Working.height, m_Working.heights };
	if (!VansTerrainImageCodec::EncodeHeight16(height, encoded[0], error)) return false;
	for (std::size_t index = 0; index < 2; ++index)
	{
		VansTerrainWeightImage weights{ m_Working.width, m_Working.height, m_Working.splatPixels[index] };
		if (!VansTerrainImageCodec::EncodeWeightsRGBA8(weights, encoded[index + 1], error)) return false;
	}

	VansScopedIOContext io(VansIODomain::Authoring, "TerrainAuthoring.StageImages", true);
	for (std::size_t index = 0; index < m_ImagePaths.size(); ++index)
	{
		VansStagedFile stage;
		if (!VansFileStorage::StageWriteBytes(m_ImagePaths[index], encoded[index], stage, error))
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
	for (std::size_t index = 0; index < m_ImagePaths.size(); ++index)
	{
		m_ObservedFingerprints[index] = VansAssetDocument::Fingerprint(m_ImagePaths[index], error);
		if (!error.empty() || !m_ObservedFingerprints[index].exists)
		{
			if (error.empty()) error = "Saved terrain image is missing: " + m_ImagePaths[index].string();
			return false;
		}
	}
	return true;
}

void VansTerrainAuthoringSession::AdoptObservedSave()
{
	m_LoadedFingerprints = m_ObservedFingerprints;
	m_SavedHeights = m_Working.heights;
	m_SavedSplats = m_Working.splatPixels;
	m_PixelDirty = false;
}
}
