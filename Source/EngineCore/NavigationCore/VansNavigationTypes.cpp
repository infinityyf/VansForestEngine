#include "VansNavigationTypes.h"
#include <DetourNode.h>
#include <tuple>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Vans
{
bool CompileNavigationLinkCapabilities(const std::vector<VansNavigationLinkKey>& keys,
    std::array<VansNavigationLinkKey,kMaximumNavigationLinkCapabilities>& output,std::string& error)
{
    if(keys.size()>kMaximumNavigationLinkCapabilities) { error="Navigation Link capability count exceeds capacity"; return false; }
    std::array<VansNavigationLinkKey,kMaximumNavigationLinkCapabilities> candidate{};
    std::copy(keys.begin(),keys.end(),candidate.begin()); std::sort(candidate.begin(),candidate.begin()+keys.size());
    if((!keys.empty() && candidate.front().value==0) ||
        std::adjacent_find(candidate.begin(),candidate.begin()+keys.size())!=candidate.begin()+keys.size())
    { error="Navigation Link capabilities require distinct nonzero identities"; return false; }
    output=candidate; error.clear(); return true;
}
const char* NavigationMapUpdateModeName(VansNavigationMapUpdateMode mode)
{
    switch(mode)
    {
    case VansNavigationMapUpdateMode::Baked: return "Baked";
    case VansNavigationMapUpdateMode::Runtime: return "Runtime";
    }
    return "Invalid";
}
bool SameNavigationSettings(const VansNavigationSettings& left,const VansNavigationSettings& right)
{
    const auto query=[](const VansNavigationQuerySettings& q)
    { return std::tie(q.maximumSearchNodes,q.maximumCorridorPolygons,q.maximumPathPoints,q.maximumActiveAgents,
        q.maximumIterationsPerFrame,q.maximumIterationsPerSlice,q.maximumPathStartsPerFrame,q.maximumQueuedQueries,
        q.maximumActiveQueries,q.maximumQueryHistory,q.maximumQueryFrames); };
    const auto avoidance=[](const VansNavigationAvoidanceSettings& a)
    { return std::tie(a.queryRangeMeters,a.horizonSeconds,a.maximumYieldSeconds,a.maximumNeighbors,a.maximumNeighborScans,
        a.maximumCellsPerBody,a.maximumGridEntries,
        a.maximumBoundaryPolygons,a.maximumBoundarySegments,a.adaptiveDivisions,a.adaptiveRings,
        a.adaptiveDepth,a.maximumSamplesPerFrame); };
    const auto maps=[](const VansNavigationMapSettings& m)
    { return std::tie(m.id,m.updateMode,m.profiles,m.cellSizeMeters,m.cellHeightMeters,m.minimumRegionSizeCells,m.mergeRegionSizeCells,
        m.maximumEdgeLengthMeters,m.maximumEdgeErrorCells,m.maximumVerticesPerPolygon,m.detailSamplingEnabled,
        m.detailSampleDistanceCells,m.detailSampleErrorCells,m.tileSizeCells,m.originMeters,m.maximumTiles,
        m.maximumPolygonsPerTile,m.maximumBuildBytes); };
    const auto profiles=[](const VansNavigationAgentProfile& p)
    { return std::tie(p.id,p.heightMeters,p.radiusMeters,p.maxClimbMeters,p.maxSlopeDegrees); };
    const auto areas=[](const VansNavigationAreaDefinition& a)
    { return std::tie(a.name,a.id,a.traversalCost,a.traversable); };
    const auto filters=[](const VansNavigationQueryFilterSettings& f)
    { return std::tie(f.id,f.excludedAreas,f.areaCosts); };
    return std::tie(left.maximumResidentBytes,left.maximumConcurrentBuildBytes,left.maximumResidentTiles,left.areas.defaultArea)==
        std::tie(right.maximumResidentBytes,right.maximumConcurrentBuildBytes,right.maximumResidentTiles,right.areas.defaultArea) &&
        query(left.query)==query(right.query) && avoidance(left.avoidance)==avoidance(right.avoidance) &&
        std::equal(left.maps.begin(),left.maps.end(),right.maps.begin(),right.maps.end(),[&](const auto& a,const auto& b){return maps(a)==maps(b);}) &&
        std::equal(left.profiles.begin(),left.profiles.end(),right.profiles.begin(),right.profiles.end(),[&](const auto& a,const auto& b){return profiles(a)==profiles(b);}) &&
        std::equal(left.areas.definitions.begin(),left.areas.definitions.end(),right.areas.definitions.begin(),right.areas.definitions.end(),[&](const auto& a,const auto& b){return areas(a)==areas(b);}) &&
        std::equal(left.queryFilters.begin(),left.queryFilters.end(),right.queryFilters.begin(),right.queryFilters.end(),[&](const auto& a,const auto& b){return filters(a)==filters(b);});
}
const char* NavigationPathFailureName(VansNavigationPathFailure failure)
		{
			switch (failure)
			{
			case VansNavigationPathFailure::None: return "None";
			case VansNavigationPathFailure::MeshNotReady: return "MeshNotReady";
			case VansNavigationPathFailure::InvalidInput: return "InvalidInput";
			case VansNavigationPathFailure::QueryNotReady: return "QueryNotReady";
			case VansNavigationPathFailure::MeshChanged: return "MeshChanged";
			case VansNavigationPathFailure::StartOutsideMesh: return "StartOutsideMesh";
			case VansNavigationPathFailure::EndOutsideMesh: return "EndOutsideMesh";
			case VansNavigationPathFailure::CorridorSearchFailed: return "CorridorSearchFailed";
			case VansNavigationPathFailure::SearchNodeCapacityExceeded: return "SearchNodeCapacityExceeded";
			case VansNavigationPathFailure::CorridorCapacityExceeded: return "CorridorCapacityExceeded";
			case VansNavigationPathFailure::PartialEndpointResolutionFailed: return "PartialEndpointResolutionFailed";
			case VansNavigationPathFailure::CornerExtractionFailed: return "CornerExtractionFailed";
			case VansNavigationPathFailure::CornerCapacityExceeded: return "CornerCapacityExceeded";
            case VansNavigationPathFailure::QueueFull: return "QueueFull";
            case VansNavigationPathFailure::QueryDeadlineExceeded: return "QueryDeadlineExceeded";
            case VansNavigationPathFailure::NoReachablePoint: return "NoReachablePoint";
            case VansNavigationPathFailure::Cancelled: return "Cancelled";
            case VansNavigationPathFailure::MapUnavailable: return "MapUnavailable";
            case VansNavigationPathFailure::FilterInvalid: return "FilterInvalid";
            case VansNavigationPathFailure::EndProjectionRejected: return "EndProjectionRejected";
            case VansNavigationPathFailure::SpatialQueryFailed: return "SpatialQueryFailed";
            case VansNavigationPathFailure::RaycastCapacityExceeded: return "RaycastCapacityExceeded";
            case VansNavigationPathFailure::BoundaryCapacityExceeded: return "BoundaryCapacityExceeded";
            case VansNavigationPathFailure::DataUnavailable: return "DataUnavailable";
			}
			return "Unknown";
		}


namespace
{
constexpr int kMinimumSearchNodes = 4;
constexpr int kMaximumSearchNodes = std::min(static_cast<int>(DT_NULL_IDX),
    (1 << DT_NODE_PARENT_BITS) - 1);
constexpr int kMinimumPathCapacity = 2;
constexpr int kMaximumPathCapacity = 1 << 20;
}

bool ValidateNavigationSpatialDistance(double distanceMeters,std::string& error)
{
 if(!std::isfinite(distanceMeters) || distanceMeters<=0 || distanceMeters>kMaximumNavigationSpatialDistanceMeters)
 { error="Navigation spatial distance must be finite and within (0,10000] meters"; return false; }
 error.clear(); return true;
}

static bool ValidateNavigationBakeInputs(
	const VansNavigationBakeSettings& settings,
	std::string& error)
{
	const float values[] = {
		settings.cellSize, settings.cellHeight, settings.agentHeight,
		settings.agentRadius, settings.agentMaxClimb,
		settings.agentMaxSlopeDegrees, settings.regionMinSize,
		settings.regionMergeSize, settings.edgeMaxLength,
		settings.edgeMaxError, settings.detailSampleDistance,
		settings.detailSampleMaxError
	};
	if (!std::all_of(std::begin(values), std::end(values),
		[](float value) { return std::isfinite(value); }))
	{
		error = "Navigation bake settings must contain finite numeric values";
		return false;
	}
	if (settings.cellSize <= 0.0f || settings.cellHeight <= 0.0f)
	{
		error = "Navigation cell size and height must be greater than zero";
		return false;
	}
	if (settings.agentHeight <= 0.0f || settings.agentRadius <= 0.0f ||
		settings.agentMaxClimb < 0.0f ||
		settings.agentMaxClimb > settings.agentHeight)
	{
		error = "Navigation agent dimensions are invalid";
		return false;
	}
	if (settings.agentMaxSlopeDegrees < 0.0f ||
		settings.agentMaxSlopeDegrees >= 90.0f)
	{
		error = "Navigation agent maximum slope must be in [0, 90) degrees";
		return false;
	}
	if (settings.regionMinSize <= 0.0f ||
		settings.regionMergeSize < settings.regionMinSize)
	{
		error = "Navigation region sizes are invalid";
		return false;
	}
	if (settings.edgeMaxLength < 0.0f || settings.edgeMaxError < 0.0f)
	{
		error = "Navigation edge limits must not be negative";
		return false;
	}
	if (settings.maximumVerticesPerPolygon < 3 ||
		settings.maximumVerticesPerPolygon > kMaximumNavigationPolygonVertices)
	{
		error = "Navigation polygon vertex capacity must be between 3 and 6";
		return false;
	}
	if (settings.detailSamplingEnabled &&
		(settings.detailSampleDistance <= 0.0f ||
		 settings.detailSampleMaxError < 0.0f))
	{
		error = "Enabled navigation detail sampling requires a positive distance and non-negative error";
		return false;
	}
	const auto bits = [](int capacity)
	{
		unsigned value = static_cast<unsigned>(capacity - 1);
		int count = 0;
		while (value) { ++count; value >>= 1; }
		return count;
	};
	if (settings.tileSizeCells < 16 || settings.tileSizeCells > 256 ||
		!std::isfinite(settings.cellSize * settings.tileSizeCells) ||
		!std::isfinite(settings.tileOrigin.x) || !std::isfinite(settings.tileOrigin.y) ||
		!std::isfinite(settings.tileOrigin.z) || settings.maximumTiles < 1 ||
		settings.maximumTiles > 65536 || settings.maximumPolygonsPerTile < 1 ||
		settings.maximumPolygonsPerTile > 65535 ||
		bits(settings.maximumTiles) + bits(settings.maximumPolygonsPerTile) > 22 ||
		settings.maximumBuildBytes < 1024 * 1024 || settings.maximumBuildBytes > 1024 * 1024 * 1024)
	{
		error = "Navigation Tile layout exceeds dimension, byte budget or Detour reference capacity";
		return false;
	}
	error.clear();
	return true;
}

bool QuantizeNavigationBakeSettings(const VansNavigationBakeSettings& settings,
	VansNavigationBakeQuantization& output, std::string& error)
{
	if (!ValidateNavigationBakeInputs(settings, error)) return false;
	VansNavigationBakeQuantization candidate;
	const auto integer = [&](float value, int minimum, int maximum, int& target, const char* field)
	{
		if (!std::isfinite(value) || static_cast<double>(value) < minimum || static_cast<double>(value) > maximum)
		{ error = std::string("Navigation quantization exceeds native limits: ") + field; return false; }
		target = static_cast<int>(value);
		return true;
	};
	// 保持原有 float 运算和取整规则；先验证范围再转换，避免整数溢出。
	// 本地 Recast compact span 高度为 8 位，erosion 使用 radius*2 的 8 位阈值。
	if (!integer(std::ceil(settings.agentHeight / settings.cellHeight), 3, 255, candidate.heightCells, "agentHeight/cellHeight") ||
		!integer(std::floor(settings.agentMaxClimb / settings.cellHeight), 0, 255, candidate.climbCells, "agentMaxClimb/cellHeight") ||
		!integer(std::ceil(settings.agentRadius / settings.cellSize), 1, 127, candidate.radiusCells, "agentRadius/cellSize") ||
		!integer(std::floor(settings.edgeMaxLength / settings.cellSize), 0, (std::numeric_limits<int>::max)(), candidate.maximumEdgeCells, "edgeMaxLength/cellSize") ||
		!integer(std::floor(settings.regionMinSize * settings.regionMinSize), 0, (std::numeric_limits<int>::max)(), candidate.minimumRegionCellArea, "regionMinSize squared") ||
		!integer(std::floor(settings.regionMergeSize * settings.regionMergeSize), 0, (std::numeric_limits<int>::max)(), candidate.mergeRegionCellArea, "regionMergeSize squared")) return false;
	candidate.detailSampleDistanceMeters = settings.detailSamplingEnabled ? settings.cellSize * settings.detailSampleDistance : 0.0f;
	candidate.detailSampleErrorMeters = settings.cellHeight * settings.detailSampleMaxError;
	if (!std::isfinite(candidate.detailSampleDistanceMeters) || !std::isfinite(candidate.detailSampleErrorMeters))
	{ error = "Navigation detail sampling unit conversion overflow"; return false; }
	output = candidate;
	error.clear();
	return true;
}

bool ValidateNavigationBakeSettings(const VansNavigationBakeSettings& settings, std::string& error)
{
	VansNavigationBakeQuantization quantization;
	return QuantizeNavigationBakeSettings(settings, quantization, error);
}

bool ValidateNavigationQuerySettings(
	const VansNavigationQuerySettings& settings,
	std::string& error)
{
	const bool valid = settings.maximumSearchNodes >= kMinimumSearchNodes &&
		settings.maximumSearchNodes <= kMaximumSearchNodes &&
		settings.maximumCorridorPolygons >= kMinimumPathCapacity &&
		settings.maximumCorridorPolygons <= kMaximumPathCapacity &&
		settings.maximumPathPoints >= kMinimumPathCapacity &&
		settings.maximumPathPoints <= kMaximumPathCapacity &&
		settings.maximumActiveAgents >= 1 && settings.maximumActiveAgents <= 4096 &&
		settings.maximumIterationsPerFrame >= 1 && settings.maximumIterationsPerFrame <= (1 << 20) &&
		settings.maximumIterationsPerSlice >= 1 && settings.maximumIterationsPerSlice <= settings.maximumIterationsPerFrame &&
		settings.maximumPathStartsPerFrame >= 1 && settings.maximumPathStartsPerFrame <= 4096 &&
        settings.maximumQueuedQueries >= 1 && settings.maximumQueuedQueries <= 4096 &&
        settings.maximumActiveQueries >= 1 && settings.maximumActiveQueries <= settings.maximumQueuedQueries &&
        settings.maximumActiveQueries <= 64 && settings.maximumQueryHistory >= 1 && settings.maximumQueryHistory <= 4096 &&
        settings.maximumQueryFrames >= 1 && settings.maximumQueryFrames <= 36000;
	if (!valid)
		error = "Navigation query settings are outside the supported capacity range";
	else
		error.clear();
	return valid;
}

const VansNavigationAreaDefinition* FindNavigationAreaByName(
	const VansNavigationAreaSettings& settings,
	const std::string& name)
{
	const auto found = std::find_if(settings.definitions.begin(),
		settings.definitions.end(),
		[&name](const VansNavigationAreaDefinition& area)
		{ return area.name == name; });
	return found == settings.definitions.end() ? nullptr : &*found;
}

const VansNavigationAreaDefinition* FindNavigationAreaById(
	const VansNavigationAreaSettings& settings,
	std::uint8_t id)
{
	const auto found = std::find_if(settings.definitions.begin(),
		settings.definitions.end(),
		[id](const VansNavigationAreaDefinition& area)
		{ return area.id == id; });
	return found == settings.definitions.end() ? nullptr : &*found;
}

bool ValidateNavigationAreaSettings(
	const VansNavigationAreaSettings& settings,
	std::string& error)
{
	if (settings.defaultArea.empty() || settings.definitions.empty() ||
		settings.definitions.size() >
			static_cast<std::size_t>(kMaximumNavigationAreaId) + 1u)
	{
		error = "Navigation areas require a default name and 1 to 63 definitions";
		return false;
	}
	std::unordered_set<std::string> names;
	std::unordered_set<std::uint8_t> ids;
	for (const VansNavigationAreaDefinition& area : settings.definitions)
	{
		const bool blankName = area.name.empty() ||
			std::all_of(area.name.begin(), area.name.end(),
				[](unsigned char character) { return std::isspace(character) != 0; });
		if (blankName || area.id > kMaximumNavigationAreaId ||
			!std::isfinite(area.traversalCost) || area.traversalCost <= 0.0f)
		{
			error = "Navigation area definitions require a name, id in [0, 62], and positive finite cost";
			return false;
		}
		if (!names.insert(area.name).second || !ids.insert(area.id).second)
		{
			error = "Navigation area names and ids must be unique";
			return false;
		}
	}
	if (!FindNavigationAreaByName(settings, settings.defaultArea))
	{
		error = "Navigation default area is not present in the area definitions";
		return false;
	}
	error.clear();
	return true;
}

bool ValidateNavigationSettings(
	const VansNavigationSettings& settings,
	std::string& error)
{
	if (!ValidateNavigationQuerySettings(settings.query, error) || !ValidateNavigationAvoidanceSettings(settings.avoidance,error) ||
		!ValidateNavigationAreaSettings(settings.areas, error)) return false;
	const auto validId = [](const std::string& id)
	{
		return !id.empty() && id.size() <= 64u && std::none_of(id.begin(), id.end(),
			[](unsigned char c) { return std::isspace(c) != 0 || std::iscntrl(c) != 0; });
	};
	if (settings.profiles.empty() || settings.profiles.size() > kMaximumNavigationProfiles || settings.maps.empty() || settings.maps.size() > kMaximumNavigationMaps)
	{ error = "Navigation requires 1 to 16 Profiles and 1 to 64 Maps"; return false; }
	if (settings.maximumResidentBytes < 1024 * 1024 || settings.maximumResidentBytes > 1024 * 1024 * 1024 ||
		settings.maximumConcurrentBuildBytes < 1024 * 1024 || settings.maximumConcurrentBuildBytes > 1024 * 1024 * 1024 ||
		settings.maximumResidentTiles < 1 || settings.maximumResidentTiles > 1048576 ||
		EstimateNavigationQueryBytes(settings.query)+EstimateNavigationAvoidanceBytes(settings.query.maximumActiveAgents,settings.avoidance) > static_cast<std::uint64_t>(settings.maximumResidentBytes))
	{ error = "Navigation aggregate memory/Tile capacity is invalid"; return false; }
	std::unordered_set<std::string> profileIds, mapIds;
	for (const auto& profile : settings.profiles)
	{
		if (!validId(profile.id) || !profileIds.insert(profile.id).second)
		{ error = "Navigation Profile IDs must be bounded and unique"; return false; }
		if (!std::isfinite(profile.heightMeters) || !std::isfinite(profile.radiusMeters) ||
			!std::isfinite(profile.maxClimbMeters) || !std::isfinite(profile.maxSlopeDegrees) ||
			profile.heightMeters <= 0 || profile.radiusMeters <= 0 || profile.maxClimbMeters < 0 ||
			profile.maxClimbMeters > profile.heightMeters || profile.maxSlopeDegrees < 0 || profile.maxSlopeDegrees >= 90)
		{ error = "Navigation Profile dimensions must be finite and physically valid"; return false; }
	}
	for (const auto& map : settings.maps)
	{
        if(map.updateMode!=VansNavigationMapUpdateMode::Baked && map.updateMode!=VansNavigationMapUpdateMode::Runtime)
        { error="Navigation Map updateMode must be Baked or Runtime"; return false; }
		if (!validId(map.id) || !mapIds.insert(map.id).second || map.profiles.empty() || map.profiles.size() > settings.profiles.size())
		{ error = "Navigation Map IDs and Profile references must be bounded and unique"; return false; }
		std::unordered_set<std::string> references;
		for (const auto& profile : map.profiles)
		{
			VansNavigationBakeSettings bake;
			if (!references.insert(profile).second)
			{ error = "Navigation Map Profile references must be unique"; return false; }
			if (!ResolveNavigationBakeSettings(settings, map.id, profile, bake, error)) return false;
		}
	}
	if (settings.queryFilters.empty() || settings.queryFilters.size() > kMaximumNavigationQueryFilters)
	{ error = "Navigation requires 1 to 64 query filters"; return false; }
	std::unordered_set<std::string> ids;
	for (const auto& filter : settings.queryFilters)
	{
		VansNavigationCompiledFilter compiled;
		if (!ids.insert(filter.id).second)
		{ error = "Navigation query filter IDs must be unique"; return false; }
		if (!CompileNavigationFilter(settings.areas, filter, compiled, error)) return false;
	}
	error.clear();
	return true;
}

const VansNavigationAgentProfile* FindNavigationProfile(const VansNavigationSettings& settings, const std::string& id)
{
	const auto found = std::find_if(settings.profiles.begin(), settings.profiles.end(), [&](const auto& value) { return value.id == id; });
	return found == settings.profiles.end() ? nullptr : &*found;
}
const VansNavigationMapSettings* FindNavigationMap(const VansNavigationSettings& settings, const std::string& id)
{
	const auto found = std::find_if(settings.maps.begin(), settings.maps.end(), [&](const auto& value) { return value.id == id; });
	return found == settings.maps.end() ? nullptr : &*found;
}
bool ValidateNavigationGoalProjection(VansNavigationGoalAcceptance acceptance,float toleranceMeters,std::string& error)
{
    if((acceptance!=VansNavigationGoalAcceptance::RequestedPoint && acceptance!=VansNavigationGoalAcceptance::ProjectedPoint) ||
        !std::isfinite(toleranceMeters) || toleranceMeters<=0 || toleranceMeters>10)
    { error="Navigation goal acceptance requires a valid policy and projection tolerance within (0,10] meters"; return false; }
    return true;
}
bool ValidateNavigationAvoidanceSettings(const VansNavigationAvoidanceSettings& a,std::string& error)
{
    if(!std::isfinite(a.queryRangeMeters) || a.queryRangeMeters<.1f || a.queryRangeMeters>100 ||
        !std::isfinite(a.horizonSeconds) || a.horizonSeconds<.1f || a.horizonSeconds>10 ||
        !std::isfinite(a.maximumYieldSeconds) || a.maximumYieldSeconds<.1f || a.maximumYieldSeconds>60 ||
        a.maximumNeighbors<1 || a.maximumNeighbors>32 || a.maximumNeighborScans<a.maximumNeighbors || a.maximumNeighborScans>4096 ||
        a.maximumCellsPerBody<1 || a.maximumCellsPerBody>4096 || a.maximumGridEntries<a.maximumCellsPerBody || a.maximumGridEntries>1048576 ||
        a.maximumBoundaryPolygons<1 || a.maximumBoundaryPolygons>32 || a.maximumBoundarySegments<1 || a.maximumBoundarySegments>32 ||
        a.adaptiveDivisions<1 || a.adaptiveDivisions>32 || a.adaptiveRings<1 || a.adaptiveRings>4 ||
        a.adaptiveDepth<1 || a.adaptiveDepth>8 || a.maximumSamplesPerFrame<(a.adaptiveDivisions*a.adaptiveRings+1)*a.adaptiveDepth || a.maximumSamplesPerFrame>1048576)
    { error="Navigation avoidance requires bounded range, horizon, neighbors, scan, boundary and sampling budgets"; return false; }
    error.clear(); return true;
}
bool NavigationAvoidanceGridBounds(const glm::vec3& minimum,const glm::vec3& maximum,
    const VansNavigationAvoidanceSettings& settings,std::array<int,4>& cells,std::string& error)
{
    if(!std::isfinite(settings.queryRangeMeters) || settings.queryRangeMeters<=0 || settings.maximumCellsPerBody<1)
    { error="Navigation avoidance grid policy is invalid"; return false; }
    for(int axis=0;axis<3;++axis) if(!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis]>maximum[axis])
    { error="Navigation avoidance body bounds are invalid"; return false; }
    std::array<int,4> candidate{};
    const double coordinates[]{minimum.x,maximum.x,minimum.z,maximum.z};
    for(std::size_t i=0;i<candidate.size();++i)
    {
        const double value=std::floor(coordinates[i]/settings.queryRangeMeters);
        if(std::abs(value)>=(1<<28)) { error="Navigation avoidance body exceeds grid coordinate range"; return false; }
        candidate[i]=static_cast<int>(value);
    }
    // 按最坏网格对齐准入，同尺寸平移不会因跨格突然超出覆盖容量。
    const auto columns=static_cast<std::uint64_t>(std::ceil((static_cast<double>(maximum.x)-minimum.x)/settings.queryRangeMeters))+1u;
    const auto rows=static_cast<std::uint64_t>(std::ceil((static_cast<double>(maximum.z)-minimum.z)/settings.queryRangeMeters))+1u;
    const auto count=columns*rows;
    if(count>static_cast<std::uint64_t>(settings.maximumCellsPerBody))
    { error="Navigation avoidance body footprint exceeds cell capacity"; return false; }
    cells=candidate; error.clear(); return true;
}
bool ResolveNavigationBakeSettings(const VansNavigationSettings& settings, const std::string& mapId,
	const std::string& profileId, VansNavigationBakeSettings& output, std::string& error)
{
	const auto* map = FindNavigationMap(settings, mapId);
	const auto* profile = FindNavigationProfile(settings, profileId);
	if (!map || !profile || std::find(map->profiles.begin(), map->profiles.end(), profileId) == map->profiles.end())
	{ error = "Navigation Map/Profile reference is unavailable: " + mapId + "/" + profileId; return false; }
	VansNavigationBakeSettings bake;
	bake.agentHeight = profile->heightMeters; bake.agentRadius = profile->radiusMeters;
	bake.agentMaxClimb = profile->maxClimbMeters; bake.agentMaxSlopeDegrees = profile->maxSlopeDegrees;
	bake.cellSize = map->cellSizeMeters; bake.cellHeight = map->cellHeightMeters;
	bake.regionMinSize = map->minimumRegionSizeCells; bake.regionMergeSize = map->mergeRegionSizeCells;
	bake.edgeMaxLength = map->maximumEdgeLengthMeters; bake.edgeMaxError = map->maximumEdgeErrorCells;
	bake.maximumVerticesPerPolygon = map->maximumVerticesPerPolygon; bake.detailSamplingEnabled = map->detailSamplingEnabled;
	bake.detailSampleDistance = map->detailSampleDistanceCells; bake.detailSampleMaxError = map->detailSampleErrorCells;
	bake.tileSizeCells = map->tileSizeCells; bake.tileOrigin = map->originMeters;
	bake.maximumTiles = map->maximumTiles; bake.maximumPolygonsPerTile = map->maximumPolygonsPerTile;
	bake.maximumBuildBytes = map->maximumBuildBytes;
    bake.buildTileCacheLayers = map->updateMode==VansNavigationMapUpdateMode::Runtime;
	if (!ValidateNavigationBakeSettings(bake, error)) return false;
	output = bake; error.clear(); return true;
}

bool CompileNavigationFilter(const VansNavigationAreaSettings& areas,
	const VansNavigationQueryFilterSettings& filter, VansNavigationCompiledFilter& output,
	std::string& error)
{
	if (!ValidateNavigationAreaSettings(areas, error)) return false;
	if (filter.id.empty() || filter.id.size() > 64u ||
		std::any_of(filter.id.begin(), filter.id.end(), [](unsigned char character) { return std::isspace(character) != 0 || std::iscntrl(character) != 0; }))
	{ error = "Navigation query filter requires a bounded ID without whitespace"; return false; }
	if (filter.excludedAreas.size() > 63u || filter.areaCosts.size() > 63u)
	{ error = "Navigation query filter exceeds the Area capacity"; return false; }
	VansNavigationCompiledFilter candidate;
	candidate.id = filter.id;
	candidate.costs.fill(1.0f);
	for (const auto& area : areas.definitions)
	{
		candidate.allowed[area.id] = area.traversable;
		candidate.costs[area.id] = area.traversalCost;
	}
	std::unordered_set<std::string> exclusions;
	for (const auto& name : filter.excludedAreas)
	{
		const auto* area = FindNavigationAreaByName(areas, name);
		if (!area || !exclusions.insert(name).second)
		{ error = "Navigation filter exclusion references an undefined or duplicated Area: " + name; return false; }
		candidate.allowed[area->id] = false;
	}
	std::unordered_set<std::string> overrides;
	for (const auto& [name, cost] : filter.areaCosts)
	{
		const auto* area = FindNavigationAreaByName(areas, name);
		if (!area || !overrides.insert(name).second || !std::isfinite(cost) || cost <= 0.0f)
		{ error = "Navigation filter cost requires a unique current Area and positive finite value: " + name; return false; }
		candidate.costs[area->id] = cost;
	}
	// 共同正比例保持相对成本，使 Detour 的距离启发式不会高估低成本路线。
	for (std::size_t id = 0; id < candidate.allowed.size(); ++id)
		if (candidate.allowed[id]) candidate.minimumCost = (std::min)(candidate.minimumCost, candidate.costs[id]);
	for (std::size_t id = 0; id < candidate.allowed.size(); ++id)
		if (candidate.allowed[id])
		{
			const double normalized = static_cast<double>(candidate.costs[id]) / candidate.minimumCost;
			if (!std::isfinite(normalized) || normalized > (std::numeric_limits<float>::max)())
			{ error = "Navigation filter normalized cost exceeds native float capacity"; return false; }
			candidate.costs[id] = static_cast<float>(normalized);
		}
	output = std::move(candidate);
	error.clear();
	return true;
}

}
