#include "VansPcgSplinePreviewSession.h"

#include "../VansScene.h"
#include "../../AssetCore/VansAssetObjectRepository.h"
#include "../../PcgCore/VansPcgInfluence.h"
#include "../../PcgCore/VansPcgTerrainSurface.h"
#include "../../PcgCore/VansPcgUpdatePlanner.h"
#include "../../SceneCore/Serialization/VansVegetationConfigCodec.h"
#include "../../Util/VansJobSystem.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <set>
#include <utility>

namespace VansGraphics
{
namespace
{
constexpr auto PcgSplineVegetationSubmitInterval = std::chrono::milliseconds(16);

struct SplineBuildResult
{
	std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot> field;
	std::string error;
	std::uint64_t request = 0;
	double milliseconds = 0;
};

struct SplineVegetationResult
{
	std::string recipeGuid;
	std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot> field;
	std::shared_ptr<const Vans::VansAssetObjectRepository> repository;
	std::vector<Vans::VansAssetGuid> dependencies;
	std::deque<std::shared_ptr<const Vans::VansPcgBatchUpdate>> updates;
	std::string error;
	std::uint64_t candidates = 0;
	double milliseconds = 0;
};

bool DependenciesMatch(
	const SplineVegetationResult& result,
	const Vans::VansAssetObjectRepository& repository)
{
	if (!result.repository) return true;
	for (const auto guid : result.dependencies)
	{
		Vans::VansAssetObjectSnapshotInfo before;
		Vans::VansAssetObjectSnapshotInfo now;
		if (!result.repository->FindInfo(guid, before) ||
			!repository.FindInfo(guid, now) || before.generation != now.generation)
			return false;
	}
	return true;
}

// 以最后已提交的植被场比较新旧范围；不能只用最近一次拖动的 changedTiles。
std::optional<Vans::VansPcgBounds> VegetationChanges(
	const Vans::VansPcgSplineFieldSnapshot& current,
	const std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot>& previous)
{
	if (!previous || current.worldSize != previous->worldSize ||
		current.resolution != previous->resolution ||
		current.terrainGuid != previous->terrainGuid ||
		current.terrainFingerprint != previous->terrainFingerprint)
	{
		return Vans::VansPcgBounds{
			{-current.worldSize * .5f, -current.worldSize * .5f},
			{current.worldSize * .5f, current.worldSize * .5f}};
	}
	std::optional<Vans::VansPcgBounds> bounds;
	std::set<std::uint64_t> keys;
	for (const auto& [key, tile] : current.tiles) keys.insert(key);
	for (const auto& [key, tile] : previous->tiles) keys.insert(key);
	for (const auto key : keys)
	{
		const auto x = std::uint32_t(key);
		const auto z = std::uint32_t(key >> 32);
		const auto* a = current.FindTile(x, z);
		const auto* b = previous->FindTile(x, z);
		if (a == b) continue;
		if (a && b &&
			(current.effectiveTerrain == previous->effectiveTerrain ||
				a->terrainShapeFingerprint == b->terrainShapeFingerprint) &&
			a->vegetationExclusion == b->vegetationExclusion)
			continue;
		const float size = current.texelSize * Vans::VANS_SPLINE_TILE_SIZE;
		const float halo = Vans::PcgSplineFieldChangeHalo(
			current.texelSize, current.worldSize, current.effectiveTerrain->width);
		Vans::VansPcgBounds cell{
			{x * size - current.worldSize * .5f - halo,
			 z * size - current.worldSize * .5f - halo},
			{(x + 1) * size - current.worldSize * .5f + halo,
			 (z + 1) * size - current.worldSize * .5f + halo}};
		if (!bounds) bounds = cell;
		else
		{
			for (int i = 0; i < 2; ++i)
			{
				bounds->min[i] = std::min(bounds->min[i], cell.min[i]);
				bounds->max[i] = std::max(bounds->max[i], cell.max[i]);
			}
		}
	}
	return bounds;
}

void GenerateSplineVegetation(
	const Vans::VansPcgRecipeAsset& recipe,
	const Vans::VansPcgBounds& dirty,
	SplineVegetationResult& result)
{
	const auto start = std::chrono::steady_clock::now();
	for (const auto& region : recipe.regions)
	{
		if (!region.enabled || region.surface.terrain != result.field->terrainGuid) continue;
		for (const auto& layer : region.layers)
		{
			if (!layer.enabled) continue;
			if (region.bounds.max[0] <= dirty.min[0] ||
				region.bounds.max[1] <= dirty.min[1] ||
				region.bounds.min[0] >= dirty.max[0] ||
				region.bounds.min[1] >= dirty.max[1])
				continue;
			std::optional<Vans::VansPcgBounds> coverage;
			if (Vans::ResolvePcgUpdateScope(layer) == Vans::VansPcgUpdateScope::LocalCoverage)
			{
				Vans::VansPcgBounds bounds;
				for (int i = 0; i < 2; ++i)
				{
					bounds.min[i] = std::floor(
						std::max(region.bounds.min[i], dirty.min[i]) / region.cellSize) * region.cellSize;
					bounds.max[i] = std::ceil(
						std::min(region.bounds.max[i], dirty.max[i]) / region.cellSize) * region.cellSize;
				}
				coverage = bounds;
			}
			Vans::VansPcgUpdatePlan plan;
			if (!Vans::VansPcgUpdatePlanner::PlanLayer(
					recipe.name, region, layer, *result.repository,
					[&](const Vans::VansPcgSurfaceBinding&, std::string& error)
					{
						return Vans::CreatePcgTerrainSurface(result.field->effectiveTerrain, error);
					},
					coverage, result.field, Vans::VansPcgUpdatePartition::PerCellWhenCovered,
					false, plan, result.error))
				return;
			result.candidates += plan.candidates;
			for (auto& update : plan.updates)
			{
				// 空格也逐帧提交，才能清除旧驻留批次。
				result.updates.push_back(
					std::make_shared<Vans::VansPcgBatchUpdate>(std::move(update)));
			}
		}
	}
	result.milliseconds = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - start).count();
}
}

struct VansPcgSplinePreviewSession::State
{
	std::future<SplineBuildResult> fieldFuture;
	std::future<SplineVegetationResult> vegetationFuture;
	SplineVegetationResult vegetationReady;
	std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot> vegetationField;
	bool vegetationInitialized = false;
	std::chrono::steady_clock::time_point lastVegetationSubmit{};
};

VansPcgSplinePreviewSession::VansPcgSplinePreviewSession()
	: m_State(std::make_unique<State>())
{
}

VansPcgSplinePreviewSession::~VansPcgSplinePreviewSession() = default;

void VansPcgSplinePreviewSession::Reset()
{
	m_State = std::make_unique<State>();
}

bool VansPcgSplinePreviewSession::QueueBuild(
	Vans::VansPcgSplineBuildRequest request,
	std::shared_ptr<const Vans::VansPcgSplineFieldSnapshot> previous)
{
	if (!request || m_State->fieldFuture.valid()) return false;
	auto promise = std::make_shared<std::promise<SplineBuildResult>>();
	m_State->fieldFuture = promise->get_future();
	Vans::VansJobSystem::Get().QueueJob(
		[promise, request = std::move(request), previous = std::move(previous)]() mutable
		{
			SplineBuildResult result;
			result.request = request.requestId;
			const auto start = std::chrono::steady_clock::now();
			try
			{
				result.field = Vans::VansPcgSplineFieldBuilder::Build(
					request.asset, request.terrain, previous, result.error);
			}
			catch (const std::exception& exception)
			{
				result.error = exception.what();
			}
			result.milliseconds = std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - start).count();
			promise->set_value(std::move(result));
		});
	return true;
}

void VansPcgSplinePreviewSession::Tick(
	VansScene& scene,
	const std::string& recipeGuid,
	const Vans::VansAssetObjectRepository& repository,
	std::uint64_t currentRequest,
	bool dragging,
	std::string& message)
{
	if (!scene.IsSceneReady()) return;
	if (!m_State->vegetationInitialized)
	{
		m_State->vegetationField = scene.GetSplineFieldSnapshot();
		m_State->vegetationInitialized = true;
	}
	if (m_State->fieldFuture.valid() &&
		m_State->fieldFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		auto result = m_State->fieldFuture.get();
		if (result.request == currentRequest || (dragging && result.request < currentRequest))
		{
			if (!result.field) message = result.error;
			else
			{
				const auto start = std::chrono::steady_clock::now();
				if (scene.PublishSplineField(result.field, message))
				{
					message.clear();
					const double publishMs = std::chrono::duration<double, std::milli>(
						std::chrono::steady_clock::now() - start).count();
					VANS_LOG("[PcgSplinePreview] fieldMs=" << result.milliseconds <<
						" publishMs=" << publishMs << " tiles=" << result.field->rebuiltTileCount <<
						" request=" << result.request);
				}
			}
		}
	}

	if (m_State->vegetationFuture.valid() &&
		m_State->vegetationFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
	{
		m_State->vegetationReady = m_State->vegetationFuture.get();
		VANS_LOG("[PcgSplinePreview] vegetationMs=" << m_State->vegetationReady.milliseconds <<
			" candidates=" << m_State->vegetationReady.candidates <<
			" cells=" << m_State->vegetationReady.updates.size());
		if (!m_State->vegetationReady.error.empty())
		{
			message = m_State->vegetationReady.error;
			m_State->vegetationReady.updates.clear();
			m_State->vegetationField = m_State->vegetationReady.field;
		}
	}
	if ((m_State->vegetationReady.repository &&
		m_State->vegetationReady.recipeGuid != recipeGuid) ||
		!DependenciesMatch(m_State->vegetationReady, repository))
	{
		m_State->vegetationReady = {};
		m_State->vegetationField.reset();
	}
	const auto now = std::chrono::steady_clock::now();
	if (!m_State->vegetationReady.updates.empty() &&
		now - m_State->lastVegetationSubmit >= PcgSplineVegetationSubmitInterval)
	{
		scene.QueueVegetationUpdate(m_State->vegetationReady.updates.front());
		m_State->vegetationReady.updates.pop_front();
		m_State->lastVegetationSubmit = now;
	}
	if (m_State->vegetationReady.field && m_State->vegetationReady.updates.empty())
	{
		m_State->vegetationField = m_State->vegetationReady.field;
		m_State->vegetationReady = {};
	}

	const auto current = scene.GetSplineFieldSnapshot();
	if (!current || current == m_State->vegetationField ||
		m_State->vegetationFuture.valid() || !m_State->vegetationReady.updates.empty())
		return;
	const auto bounds = VegetationChanges(*current, m_State->vegetationField);
	Vans::VansAssetGuid parsedRecipeGuid;
	Vans::VansAssetGuid::TryParse(recipeGuid, parsedRecipeGuid);
	const auto recipe = repository.ResolveLatest<Vans::VansVegetationConfigAsset>(parsedRecipeGuid);
	if (!bounds || !recipe)
	{
		m_State->vegetationField = current;
		return;
	}
	SplineVegetationResult result;
	result.recipeGuid = recipeGuid;
	result.field = current;
	result.repository = repository.CreateSnapshot();
	result.dependencies = recipe->config.Dependencies();
	result.dependencies.push_back(parsedRecipeGuid);
	auto promise = std::make_shared<std::promise<SplineVegetationResult>>();
	m_State->vegetationFuture = promise->get_future();
	Vans::VansJobSystem::Get().QueueJob(
		[promise, recipe, bounds, result = std::move(result)]() mutable
		{
			try
			{
				GenerateSplineVegetation(recipe->config, *bounds, result);
			}
			catch (const std::exception& exception)
			{
				result.error = exception.what();
				result.updates.clear();
			}
			promise->set_value(std::move(result));
		});
}

bool VansPcgSplinePreviewSession::IsBuilding() const
{
	return m_State->fieldFuture.valid();
}
}
