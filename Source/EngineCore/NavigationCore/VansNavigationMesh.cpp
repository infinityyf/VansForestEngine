#include "VansNavigationMesh.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <unordered_set>
#include <utility>

namespace Vans
{
struct VansNavigationQueryScratch
{
	explicit VansNavigationQueryScratch(const VansNavigationQuerySettings& settings)
		: polygons(static_cast<std::size_t>(settings.maximumCorridorPolygons)),
		  pathPoints(static_cast<std::size_t>(settings.maximumPathPoints) * 3u)
	{
	}

	std::vector<dtPolyRef> polygons;
	std::vector<float> pathPoints;
};

namespace
{
constexpr std::uint32_t kNavigationMagic = 0x56414E56u; // VNAV
constexpr std::uint32_t kNavigationFormat = 3u;
constexpr std::uint32_t kMaximumSourcePathBytes = 4096u;
constexpr unsigned short kWalkableFlag = 0x1u;
constexpr int kMinimumSearchNodes = 4;
constexpr int kMaximumSearchNodes = (1 << 24) - 1;
constexpr int kMinimumPathCapacity = 2;
constexpr int kMaximumPathCapacity = 1 << 20;

template <typename T>
bool WriteValue(std::ofstream& stream, const T& value)
{
	stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
	return static_cast<bool>(stream);
}

template <typename T>
bool ReadValue(std::ifstream& stream, T& value)
{
	stream.read(reinterpret_cast<char*>(&value), sizeof(T));
	return static_cast<bool>(stream);
}

bool WriteString(std::ofstream& stream, const std::string& value)
{
	if (value.empty() || value.size() > kMaximumSourcePathBytes) return false;
	const std::uint32_t size = static_cast<std::uint32_t>(value.size());
	stream.write(reinterpret_cast<const char*>(&size), sizeof(size));
	stream.write(value.data(), static_cast<std::streamsize>(value.size()));
	return static_cast<bool>(stream);
}

bool ReadString(std::ifstream& stream, std::string& value)
{
	std::uint32_t size = 0;
	if (!ReadValue(stream, size) || size == 0u || size > kMaximumSourcePathBytes)
		return false;
	value.resize(size);
	stream.read(value.data(), static_cast<std::streamsize>(size));
	return static_cast<bool>(stream);
}

bool IsFiniteGeometry(const VansNavigationGeometry& geometry)
{
	if (geometry.vertices.size() % 3u != 0u || geometry.indices.size() % 3u != 0u)
		return false;
	if (geometry.areas.size() != geometry.TriangleCount()) return false;
	for (float value : geometry.vertices)
		if (!std::isfinite(value)) return false;
	const int vertexCount = static_cast<int>(geometry.VertexCount());
	for (int index : geometry.indices)
		if (index < 0 || index >= vertexCount) return false;
	return true;
}

VansNavigationPath MakePathFailure(VansNavigationPathFailure failure,
	const char* diagnostic)
{
	VansNavigationPath result;
	result.status = VansNavigationPathStatus::Failed;
	result.failure = failure;
	result.diagnostic = diagnostic;
	return result;
}

bool ApplyAreaSettings(dtNavMesh& navMesh,
	const VansNavigationAreaSettings& settings,
	std::string& error)
{
	const dtNavMesh& readableNavMesh = navMesh;
	for (int tileIndex = 0; tileIndex < navMesh.getMaxTiles(); ++tileIndex)
	{
		const dtMeshTile* tile = readableNavMesh.getTile(tileIndex);
		if (!tile || !tile->header) continue;
		const dtPolyRef base = navMesh.getPolyRefBase(tile);
		for (int polygonIndex = 0; polygonIndex < tile->header->polyCount; ++polygonIndex)
		{
			const std::uint8_t areaId = tile->polys[polygonIndex].getArea();
			const VansNavigationAreaDefinition* area =
				FindNavigationAreaById(settings, areaId);
			if (!area)
			{
				error = "Navigation mesh references undefined area id " +
					std::to_string(areaId);
				return false;
			}
			const dtPolyRef polygon = base | static_cast<dtPolyRef>(polygonIndex);
			const unsigned short flags = area->traversable ? kWalkableFlag : 0u;
			if (dtStatusFailed(navMesh.setPolyFlags(polygon, flags)))
			{
				error = "Detour could not apply navigation area flags";
				return false;
			}
		}
	}
	return true;
}
}

bool ValidateNavigationBakeSettings(
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
		settings.maximumVerticesPerPolygon > DT_VERTS_PER_POLYGON)
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
	error.clear();
	return true;
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
		settings.maximumPathPoints <= kMaximumPathCapacity;
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
	return ValidateNavigationBakeSettings(settings.bake, error) &&
		ValidateNavigationQuerySettings(settings.query, error) &&
		ValidateNavigationAreaSettings(settings.areas, error);
}

VansNavigationMesh::VansNavigationMesh() = default;

VansNavigationMesh::~VansNavigationMesh()
{
	Reset();
}

VansNavigationMesh::VansNavigationMesh(VansNavigationMesh&& other) noexcept
	: m_NavMesh(other.m_NavMesh)
	, m_Query(other.m_Query)
	, m_QueryFilter(std::move(other.m_QueryFilter))
	, m_QueryScratch(std::move(other.m_QueryScratch))
	, m_SerializedData(std::move(other.m_SerializedData))
	, m_Settings(other.m_Settings)
	, m_Source(std::move(other.m_Source))
{
	other.m_NavMesh = nullptr;
	other.m_Query = nullptr;
}

VansNavigationMesh& VansNavigationMesh::operator=(VansNavigationMesh&& other) noexcept
{
	if (this == &other) return *this;
	Reset();
	m_NavMesh = other.m_NavMesh;
	m_Query = other.m_Query;
	m_QueryFilter = std::move(other.m_QueryFilter);
	m_QueryScratch = std::move(other.m_QueryScratch);
	m_SerializedData = std::move(other.m_SerializedData);
	m_Settings = other.m_Settings;
	m_Source = std::move(other.m_Source);
	other.m_NavMesh = nullptr;
	other.m_Query = nullptr;
	return *this;
}

void VansNavigationMesh::Reset()
{
	if (m_Query) dtFreeNavMeshQuery(m_Query);
	if (m_NavMesh) dtFreeNavMesh(m_NavMesh);
	m_QueryFilter.reset();
	m_QueryScratch.reset();
	m_Query = nullptr;
	m_NavMesh = nullptr;
	m_SerializedData.clear();
	m_Source = {};
}

bool VansNavigationMesh::InitializeFromData(std::vector<unsigned char> data,
	const VansNavigationBakeSettings& bakedSettings,
	const VansNavigationSettings& settings,
	std::string& error)
{
	if (data.empty())
	{
		error = "Navigation mesh contains no Detour data";
		return false;
	}
	std::unique_ptr<VansNavigationQueryScratch> queryScratch;
	std::unique_ptr<dtQueryFilter> queryFilter;
	try
	{
		queryScratch = std::make_unique<VansNavigationQueryScratch>(settings.query);
		queryFilter = std::make_unique<dtQueryFilter>();
	}
	catch (const std::bad_alloc&)
	{
		error = "Could not allocate navigation query buffers";
		return false;
	}
	queryFilter->setIncludeFlags(kWalkableFlag);
	queryFilter->setExcludeFlags(0);
	for (const VansNavigationAreaDefinition& area : settings.areas.definitions)
		queryFilter->setAreaCost(area.id, area.traversalCost);

	unsigned char* navData = static_cast<unsigned char*>(dtAlloc(data.size(), DT_ALLOC_PERM));
	if (!navData)
	{
		error = "Detour could not allocate navigation mesh data";
		return false;
	}
	std::unique_ptr<unsigned char, decltype(&dtFree)>
		navDataOwner(navData, &dtFree);
	std::memcpy(navData, data.data(), data.size());

	std::unique_ptr<dtNavMesh, decltype(&dtFreeNavMesh)>
		navMesh(dtAllocNavMesh(), &dtFreeNavMesh);
	if (!navMesh)
	{
		error = "Detour could not allocate navigation mesh";
		return false;
	}
	const dtStatus initStatus = navMesh->init(navData,
		static_cast<int>(data.size()), DT_TILE_FREE_DATA);
	if (dtStatusFailed(initStatus))
	{
		error = "Detour rejected navigation mesh data";
		return false;
	}
	navDataOwner.release(); // DT_TILE_FREE_DATA 已把 payload 所有权转交给 navMesh。
	if (!ApplyAreaSettings(*navMesh, settings.areas, error)) return false;

	std::unique_ptr<dtNavMeshQuery, decltype(&dtFreeNavMeshQuery)>
		query(dtAllocNavMeshQuery(), &dtFreeNavMeshQuery);
	if (!query || dtStatusFailed(query->init(
		navMesh.get(), settings.query.maximumSearchNodes)))
	{
		error = "Detour could not initialize navigation query state";
		return false;
	}

	// 新数据和查询状态完整建立后再替换当前可用实例；失败不得破坏旧导航网格。
	Reset();
	m_NavMesh = navMesh.release();
	m_Query = query.release();
	m_QueryFilter = std::move(queryFilter);
	m_QueryScratch = std::move(queryScratch);
	m_SerializedData = std::move(data);
	m_Settings = settings;
	m_Settings.bake = bakedSettings;
	error.clear();
	return true;
}

bool VansNavigationMesh::Build(const VansNavigationGeometry& geometry,
	const VansNavigationSettings& settings,
	std::string& error)
{
	if (geometry.Empty() || !IsFiniteGeometry(geometry))
	{
		error = "Navigation geometry is empty or invalid";
		return false;
	}
	if (!ValidateNavigationSettings(settings, error)) return false;
	for (std::uint8_t areaId : geometry.areas)
		if (!FindNavigationAreaById(settings.areas, areaId))
		{
			error = "Navigation geometry references undefined area id " +
				std::to_string(areaId);
			return false;
		}
	const VansNavigationBakeSettings& bakeSettings = settings.bake;

	rcContext context(true);
	rcConfig config{};
	config.cs = bakeSettings.cellSize;
	config.ch = bakeSettings.cellHeight;
	config.walkableSlopeAngle = bakeSettings.agentMaxSlopeDegrees;
	config.walkableHeight = static_cast<int>(std::ceil(bakeSettings.agentHeight / config.ch));
	config.walkableClimb = static_cast<int>(std::floor(bakeSettings.agentMaxClimb / config.ch));
	config.walkableRadius = static_cast<int>(std::ceil(bakeSettings.agentRadius / config.cs));
	config.maxEdgeLen = static_cast<int>(bakeSettings.edgeMaxLength / config.cs);
	config.maxSimplificationError = bakeSettings.edgeMaxError;
	config.minRegionArea = static_cast<int>(rcSqr(bakeSettings.regionMinSize));
	config.mergeRegionArea = static_cast<int>(rcSqr(bakeSettings.regionMergeSize));
	config.maxVertsPerPoly = bakeSettings.maximumVerticesPerPolygon;
	config.detailSampleDist = bakeSettings.detailSamplingEnabled
		? config.cs * bakeSettings.detailSampleDistance : 0.0f;
	config.detailSampleMaxError = config.ch * bakeSettings.detailSampleMaxError;
	rcCalcBounds(geometry.vertices.data(), static_cast<int>(geometry.VertexCount()),
		config.bmin, config.bmax);
	rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
	if (config.width <= 0 || config.height <= 0)
	{
		error = "Navigation geometry produced an empty raster grid";
		return false;
	}

	std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)>
		heightfield(rcAllocHeightfield(), &rcFreeHeightField);
	std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)>
		compact(nullptr, &rcFreeCompactHeightfield);
	std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)>
		contours(nullptr, &rcFreeContourSet);
	std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)>
		polyMesh(nullptr, &rcFreePolyMesh);
	std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)>
		detailMesh(nullptr, &rcFreePolyMeshDetail);
	if (!heightfield || !rcCreateHeightfield(&context, *heightfield,
		config.width, config.height, config.bmin, config.bmax, config.cs, config.ch))
	{
		error = "Recast could not create the heightfield";
		return false;
	}

	const int triangleCount = static_cast<int>(geometry.TriangleCount());
	std::vector<unsigned char> triangleAreas;
	triangleAreas.reserve(geometry.areas.size());
	for (std::uint8_t areaId : geometry.areas)
		triangleAreas.push_back(static_cast<unsigned char>(areaId + 1u));
	rcClearUnwalkableTriangles(&context, config.walkableSlopeAngle,
		geometry.vertices.data(), static_cast<int>(geometry.VertexCount()),
		geometry.indices.data(), triangleCount, triangleAreas.data());
	if (!rcRasterizeTriangles(&context, geometry.vertices.data(),
		static_cast<int>(geometry.VertexCount()), geometry.indices.data(),
		triangleAreas.data(), triangleCount, *heightfield, config.walkableClimb))
	{
		error = "Recast could not rasterize navigation geometry";
		return false;
	}
	rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
	rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
	rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

	compact.reset(rcAllocCompactHeightfield());
	if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight,
		config.walkableClimb, *heightfield, *compact))
	{
		error = "Recast could not build the compact heightfield";
		return false;
	}
	heightfield.reset();
	if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact) ||
		!rcBuildDistanceField(&context, *compact) ||
		!rcBuildRegions(&context, *compact, 0, config.minRegionArea, config.mergeRegionArea))
	{
		error = "Recast could not build navigable regions";
		return false;
	}

	contours.reset(rcAllocContourSet());
	if (!contours || !rcBuildContours(&context, *compact,
		config.maxSimplificationError, config.maxEdgeLen, *contours))
	{
		error = "Recast could not build navigation contours";
		return false;
	}
	polyMesh.reset(rcAllocPolyMesh());
	if (!polyMesh || !rcBuildPolyMesh(&context, *contours,
		config.maxVertsPerPoly, *polyMesh) || polyMesh->npolys == 0)
	{
		error = "Recast produced no navigation polygons";
		return false;
	}
	detailMesh.reset(rcAllocPolyMeshDetail());
	if (!detailMesh || !rcBuildPolyMeshDetail(&context, *polyMesh, *compact,
		config.detailSampleDist, config.detailSampleMaxError, *detailMesh))
	{
		error = "Recast could not build navigation detail mesh";
		return false;
	}
	compact.reset();
	contours.reset();

	for (int polygon = 0; polygon < polyMesh->npolys; ++polygon)
	{
		if (polyMesh->areas[polygon] == RC_NULL_AREA)
		{
			error = "Recast produced a polygon without a navigation area";
			return false;
		}
		const std::uint8_t areaId =
			static_cast<std::uint8_t>(polyMesh->areas[polygon] - 1u);
		if (!FindNavigationAreaById(settings.areas, areaId))
		{
			error = "Recast produced an undefined navigation area";
			return false;
		}
		polyMesh->areas[polygon] = areaId;
		polyMesh->flags[polygon] = kWalkableFlag;
	}

	dtNavMeshCreateParams params{};
	params.verts = polyMesh->verts;
	params.vertCount = polyMesh->nverts;
	params.polys = polyMesh->polys;
	params.polyAreas = polyMesh->areas;
	params.polyFlags = polyMesh->flags;
	params.polyCount = polyMesh->npolys;
	params.nvp = polyMesh->nvp;
	params.detailMeshes = detailMesh->meshes;
	params.detailVerts = detailMesh->verts;
	params.detailVertsCount = detailMesh->nverts;
	params.detailTris = detailMesh->tris;
	params.detailTriCount = detailMesh->ntris;
	params.walkableHeight = bakeSettings.agentHeight;
	params.walkableRadius = bakeSettings.agentRadius;
	params.walkableClimb = bakeSettings.agentMaxClimb;
	rcVcopy(params.bmin, polyMesh->bmin);
	rcVcopy(params.bmax, polyMesh->bmax);
	params.cs = config.cs;
	params.ch = config.ch;
	params.buildBvTree = true;

	unsigned char* navData = nullptr;
	int navDataSize = 0;
	if (!dtCreateNavMeshData(&params, &navData, &navDataSize) || !navData || navDataSize <= 0)
	{
		error = "Detour could not create navigation mesh data";
		return false;
	}
	std::vector<unsigned char> serialized(navData, navData + navDataSize);
	dtFree(navData);
	return InitializeFromData(std::move(serialized), bakeSettings, settings, error);
}

bool VansNavigationMesh::Save(const std::filesystem::path& path,
	const VansNavigationSource& source,
	std::string& error) const
{
	if (!IsReady() || m_SerializedData.empty())
	{
		error = "Navigation mesh is not ready";
		return false;
	}
	if (!source.IsValid() || std::filesystem::path(source.scene).is_absolute() ||
		source.settingsHash != HashNavigationSettings(m_Settings))
	{
		error = "Navigation source fingerprint is invalid";
		return false;
	}
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	if (ec)
	{
		error = "Could not create navigation asset directory: " + ec.message();
		return false;
	}
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream)
	{
		error = "Could not open navigation asset for writing";
		return false;
	}
	const std::uint64_t dataSize = static_cast<std::uint64_t>(m_SerializedData.size());
	const VansNavigationBakeSettings& bakeSettings = m_Settings.bake;
	const std::uint8_t detailSamplingEnabled = bakeSettings.detailSamplingEnabled ? 1u : 0u;
	if (!WriteValue(stream, kNavigationMagic) || !WriteValue(stream, kNavigationFormat) ||
		!WriteValue(stream, bakeSettings.cellSize) ||
		!WriteValue(stream, bakeSettings.cellHeight) ||
		!WriteValue(stream, bakeSettings.agentHeight) ||
		!WriteValue(stream, bakeSettings.agentRadius) ||
		!WriteValue(stream, bakeSettings.agentMaxClimb) ||
		!WriteValue(stream, bakeSettings.agentMaxSlopeDegrees) ||
		!WriteValue(stream, bakeSettings.regionMinSize) ||
		!WriteValue(stream, bakeSettings.regionMergeSize) ||
		!WriteValue(stream, bakeSettings.edgeMaxLength) ||
		!WriteValue(stream, bakeSettings.edgeMaxError) ||
		!WriteValue(stream, bakeSettings.maximumVerticesPerPolygon) ||
		!WriteValue(stream, detailSamplingEnabled) ||
		!WriteValue(stream, bakeSettings.detailSampleDistance) ||
		!WriteValue(stream, bakeSettings.detailSampleMaxError) ||
		!WriteString(stream, source.scene) ||
		!WriteValue(stream, source.sceneHash) ||
		!WriteValue(stream, source.colliderHash) ||
		!WriteValue(stream, source.settingsHash) ||
		!WriteValue(stream, dataSize))
	{
		error = "Could not write navigation asset header";
		return false;
	}
	stream.write(reinterpret_cast<const char*>(m_SerializedData.data()),
		static_cast<std::streamsize>(m_SerializedData.size()));
	if (!stream)
	{
		error = "Could not write navigation asset payload";
		return false;
	}
	error.clear();
	return true;
}

bool VansNavigationMesh::Load(const std::filesystem::path& path,
	const VansNavigationSettings& runtimeSettings,
	std::string& error)
{
	if (!ValidateNavigationSettings(runtimeSettings, error)) return false;
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
	{
		error = "Could not open navigation asset: " + path.string();
		return false;
	}
	std::uint32_t magic = 0;
	std::uint32_t format = 0;
	VansNavigationBakeSettings bakedSettings;
	VansNavigationSource source;
	std::uint64_t dataSize = 0;
	if (!ReadValue(stream, magic) || !ReadValue(stream, format) ||
		magic != kNavigationMagic)
	{
		error = "Navigation asset header is invalid or unsupported";
		return false;
	}
	if (format == kNavigationFormat)
	{
		std::uint8_t detailSamplingEnabled = 0;
		if (!ReadValue(stream, bakedSettings.cellSize) ||
			!ReadValue(stream, bakedSettings.cellHeight) ||
			!ReadValue(stream, bakedSettings.agentHeight) ||
			!ReadValue(stream, bakedSettings.agentRadius) ||
			!ReadValue(stream, bakedSettings.agentMaxClimb) ||
			!ReadValue(stream, bakedSettings.agentMaxSlopeDegrees) ||
			!ReadValue(stream, bakedSettings.regionMinSize) ||
			!ReadValue(stream, bakedSettings.regionMergeSize) ||
			!ReadValue(stream, bakedSettings.edgeMaxLength) ||
			!ReadValue(stream, bakedSettings.edgeMaxError) ||
			!ReadValue(stream, bakedSettings.maximumVerticesPerPolygon) ||
			!ReadValue(stream, detailSamplingEnabled) ||
			!ReadValue(stream, bakedSettings.detailSampleDistance) ||
			!ReadValue(stream, bakedSettings.detailSampleMaxError) ||
			detailSamplingEnabled > 1u)
		{
			error = "Navigation asset header is invalid or unsupported";
			return false;
		}
		bakedSettings.detailSamplingEnabled = detailSamplingEnabled != 0;
		if (!ReadString(stream, source.scene) ||
			!ReadValue(stream, source.sceneHash) ||
			!ReadValue(stream, source.colliderHash) ||
			!ReadValue(stream, source.settingsHash) ||
			!source.IsValid())
		{
			error = "Navigation asset source fingerprint is invalid";
			return false;
		}
	}
	else
	{
		error = "Navigation asset header is invalid or unsupported";
		return false;
	}
	if (source.settingsHash != HashNavigationSettings(runtimeSettings))
	{
		error = "Navigation asset is stale for the current bake and area settings";
		return false;
	}
	if (!ReadValue(stream, dataSize) ||
		dataSize == 0 ||
		dataSize > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
		!ValidateNavigationBakeSettings(bakedSettings, error))
	{
		if (error.empty()) error = "Navigation asset header is invalid or unsupported";
		return false;
	}
	std::vector<unsigned char> data(static_cast<std::size_t>(dataSize));
	stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
	if (!stream)
	{
		error = "Navigation asset payload is truncated";
		return false;
	}
	if (!InitializeFromData(std::move(data), bakedSettings, runtimeSettings, error))
		return false;
	m_Source = std::move(source);
	return true;
}

VansNavigationPath VansNavigationMesh::FindPath(const glm::vec3& start,
	const glm::vec3& end, const glm::vec3& nearestExtents) const
{
	if (!IsReady())
		return MakePathFailure(VansNavigationPathFailure::MeshNotReady,
			"Navigation mesh is not ready");
	const dtQueryFilter& filter = *m_QueryFilter;
	const float extents[3] = {
		(std::max)(0.01f, nearestExtents.x),
		(std::max)(0.01f, nearestExtents.y),
		(std::max)(0.01f, nearestExtents.z)
	};
	const float startPoint[3] = { start.x, start.y, start.z };
	const float endPoint[3] = { end.x, end.y, end.z };
	dtPolyRef startRef = 0;
	dtPolyRef endRef = 0;
	float nearestStart[3]{};
	float nearestEnd[3]{};
	if (dtStatusFailed(m_Query->findNearestPoly(startPoint, extents, &filter,
		&startRef, nearestStart)) || startRef == 0)
		return MakePathFailure(VansNavigationPathFailure::StartOutsideMesh,
			"Start point is outside the navigation mesh");
	if (dtStatusFailed(m_Query->findNearestPoly(endPoint, extents, &filter,
		&endRef, nearestEnd)) || endRef == 0)
		return MakePathFailure(VansNavigationPathFailure::EndOutsideMesh,
			"End point is outside the navigation mesh");

	std::vector<dtPolyRef>& polygons = m_QueryScratch->polygons;
	int polygonCount = 0;
	const dtStatus pathStatus = m_Query->findPath(startRef, endRef,
		nearestStart, nearestEnd, &filter, polygons.data(), &polygonCount,
		static_cast<int>(polygons.size()));
	if (dtStatusDetail(pathStatus, DT_OUT_OF_NODES))
		return MakePathFailure(VansNavigationPathFailure::SearchNodeCapacityExceeded,
			"Navigation path search exceeded its node capacity");
	if (dtStatusDetail(pathStatus, DT_BUFFER_TOO_SMALL))
		return MakePathFailure(VansNavigationPathFailure::CorridorCapacityExceeded,
			"Navigation polygon corridor exceeded its configured capacity");
	if (dtStatusFailed(pathStatus) || polygonCount <= 0)
		return MakePathFailure(VansNavigationPathFailure::CorridorSearchFailed,
			"Detour could not find a polygon corridor");

	float straightEnd[3] = { nearestEnd[0], nearestEnd[1], nearestEnd[2] };
	const bool complete = polygons[static_cast<std::size_t>(polygonCount - 1)] == endRef;
	if (!complete)
	{
		if (dtStatusFailed(m_Query->closestPointOnPoly(
			polygons[static_cast<std::size_t>(polygonCount - 1)], nearestEnd,
			straightEnd, nullptr)))
			return MakePathFailure(
				VansNavigationPathFailure::PartialEndpointResolutionFailed,
				"Detour could not resolve a reachable partial endpoint");
	}

	std::vector<float>& straightPoints = m_QueryScratch->pathPoints;
	int straightCount = 0;
	const dtStatus straightStatus = m_Query->findStraightPath(nearestStart,
		straightEnd, polygons.data(), polygonCount, straightPoints.data(),
		nullptr, nullptr, &straightCount,
		m_Settings.query.maximumPathPoints);
	if (dtStatusDetail(straightStatus, DT_BUFFER_TOO_SMALL))
		return MakePathFailure(VansNavigationPathFailure::CornerCapacityExceeded,
			"Navigation path corners exceeded their configured capacity");
	if (dtStatusFailed(straightStatus) || straightCount <= 0)
		return MakePathFailure(VansNavigationPathFailure::CornerExtractionFailed,
			"Detour could not extract path corners");
	VansNavigationPath result;
	result.points.reserve(static_cast<std::size_t>(straightCount));
	for (int index = 0; index < straightCount; ++index)
	{
		const float* point = straightPoints.data() + index * 3;
		result.points.emplace_back(point[0], point[1], point[2]);
	}
	result.status = complete ? VansNavigationPathStatus::Complete
		: VansNavigationPathStatus::Partial;
	result.diagnostic = complete ? "Complete" : "Partial";
	return result;
}
}
