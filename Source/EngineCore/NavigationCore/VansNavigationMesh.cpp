#include "VansNavigationMesh.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>

namespace Vans
{
namespace
{
constexpr std::uint32_t kNavigationMagic = 0x56414E56u; // VNAV
constexpr std::uint32_t kNavigationFormat = 1u;
constexpr unsigned short kWalkableFlag = 0x1u;

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

bool IsFiniteGeometry(const VansNavigationGeometry& geometry)
{
	if (geometry.vertices.size() % 3u != 0u || geometry.indices.size() % 3u != 0u)
		return false;
	for (float value : geometry.vertices)
		if (!std::isfinite(value)) return false;
	const int vertexCount = static_cast<int>(geometry.VertexCount());
	for (int index : geometry.indices)
		if (index < 0 || index >= vertexCount) return false;
	return true;
}
}

VansNavigationMesh::VansNavigationMesh() = default;

VansNavigationMesh::~VansNavigationMesh()
{
	Reset();
}

VansNavigationMesh::VansNavigationMesh(VansNavigationMesh&& other) noexcept
	: m_NavMesh(other.m_NavMesh)
	, m_Query(other.m_Query)
	, m_SerializedData(std::move(other.m_SerializedData))
	, m_Settings(other.m_Settings)
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
	m_SerializedData = std::move(other.m_SerializedData);
	m_Settings = other.m_Settings;
	other.m_NavMesh = nullptr;
	other.m_Query = nullptr;
	return *this;
}

void VansNavigationMesh::Reset()
{
	if (m_Query) dtFreeNavMeshQuery(m_Query);
	if (m_NavMesh) dtFreeNavMesh(m_NavMesh);
	m_Query = nullptr;
	m_NavMesh = nullptr;
	m_SerializedData.clear();
}

bool VansNavigationMesh::InitializeFromData(std::vector<unsigned char> data,
	const VansNavigationBuildSettings& settings,
	std::string& error)
{
	Reset();
	if (data.empty())
	{
		error = "Navigation mesh contains no Detour data";
		return false;
	}

	unsigned char* navData = static_cast<unsigned char*>(dtAlloc(data.size(), DT_ALLOC_PERM));
	if (!navData)
	{
		error = "Detour could not allocate navigation mesh data";
		return false;
	}
	std::memcpy(navData, data.data(), data.size());

	dtNavMesh* navMesh = dtAllocNavMesh();
	if (!navMesh)
	{
		dtFree(navData);
		error = "Detour could not allocate navigation mesh";
		return false;
	}
	const dtStatus initStatus = navMesh->init(navData,
		static_cast<int>(data.size()), DT_TILE_FREE_DATA);
	if (dtStatusFailed(initStatus))
	{
		dtFreeNavMesh(navMesh);
		error = "Detour rejected navigation mesh data";
		return false;
	}

	dtNavMeshQuery* query = dtAllocNavMeshQuery();
	if (!query || dtStatusFailed(query->init(navMesh, 2048)))
	{
		if (query) dtFreeNavMeshQuery(query);
		dtFreeNavMesh(navMesh);
		error = "Detour could not initialize navigation query state";
		return false;
	}

	m_NavMesh = navMesh;
	m_Query = query;
	m_SerializedData = std::move(data);
	m_Settings = settings;
	error.clear();
	return true;
}

bool VansNavigationMesh::Build(const VansNavigationGeometry& geometry,
	const VansNavigationBuildSettings& settings,
	std::string& error)
{
	if (geometry.Empty() || !IsFiniteGeometry(geometry))
	{
		error = "Navigation geometry is empty or invalid";
		return false;
	}
	if (settings.cellSize <= 0.0f || settings.cellHeight <= 0.0f ||
		settings.agentHeight <= 0.0f || settings.agentRadius < 0.0f)
	{
		error = "Navigation build settings are invalid";
		return false;
	}

	rcContext context(true);
	rcConfig config{};
	config.cs = settings.cellSize;
	config.ch = settings.cellHeight;
	config.walkableSlopeAngle = settings.agentMaxSlopeDegrees;
	config.walkableHeight = static_cast<int>(std::ceil(settings.agentHeight / config.ch));
	config.walkableClimb = static_cast<int>(std::floor(settings.agentMaxClimb / config.ch));
	config.walkableRadius = static_cast<int>(std::ceil(settings.agentRadius / config.cs));
	config.maxEdgeLen = static_cast<int>(settings.edgeMaxLength / config.cs);
	config.maxSimplificationError = settings.edgeMaxError;
	config.minRegionArea = static_cast<int>(rcSqr(settings.regionMinSize));
	config.mergeRegionArea = static_cast<int>(rcSqr(settings.regionMergeSize));
	config.maxVertsPerPoly = 6;
	config.detailSampleDist = settings.detailSampleDistance < 0.9f
		? 0.0f : config.cs * settings.detailSampleDistance;
	config.detailSampleMaxError = config.ch * settings.detailSampleMaxError;
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
	std::vector<unsigned char> triangleAreas(static_cast<std::size_t>(triangleCount), 0u);
	rcMarkWalkableTriangles(&context, config.walkableSlopeAngle,
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
		if (polyMesh->areas[polygon] == RC_WALKABLE_AREA)
		{
			polyMesh->areas[polygon] = 0;
			polyMesh->flags[polygon] = kWalkableFlag;
		}
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
	params.walkableHeight = settings.agentHeight;
	params.walkableRadius = settings.agentRadius;
	params.walkableClimb = settings.agentMaxClimb;
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
	return InitializeFromData(std::move(serialized), settings, error);
}

bool VansNavigationMesh::Save(const std::filesystem::path& path, std::string& error) const
{
	if (!IsReady() || m_SerializedData.empty())
	{
		error = "Navigation mesh is not ready";
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
	if (!WriteValue(stream, kNavigationMagic) || !WriteValue(stream, kNavigationFormat) ||
		!WriteValue(stream, m_Settings) || !WriteValue(stream, dataSize))
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

bool VansNavigationMesh::Load(const std::filesystem::path& path, std::string& error)
{
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
	{
		error = "Could not open navigation asset: " + path.string();
		return false;
	}
	std::uint32_t magic = 0;
	std::uint32_t format = 0;
	VansNavigationBuildSettings settings;
	std::uint64_t dataSize = 0;
	if (!ReadValue(stream, magic) || !ReadValue(stream, format) ||
		!ReadValue(stream, settings) || !ReadValue(stream, dataSize) ||
		magic != kNavigationMagic || format != kNavigationFormat ||
		dataSize == 0 || dataSize > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
	{
		error = "Navigation asset header is invalid or unsupported";
		return false;
	}
	std::vector<unsigned char> data(static_cast<std::size_t>(dataSize));
	stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
	if (!stream)
	{
		error = "Navigation asset payload is truncated";
		return false;
	}
	return InitializeFromData(std::move(data), settings, error);
}

VansNavigationPath VansNavigationMesh::FindPath(const glm::vec3& start,
	const glm::vec3& end, const glm::vec3& nearestExtents) const
{
	VansNavigationPath result;
	if (!IsReady())
	{
		result.status = VansNavigationPathStatus::Failed;
		result.diagnostic = "Navigation mesh is not ready";
		return result;
	}
	dtQueryFilter filter;
	filter.setIncludeFlags(kWalkableFlag);
	filter.setExcludeFlags(0);
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
	{
		result.status = VansNavigationPathStatus::Failed;
		result.diagnostic = "Start point is outside the navigation mesh";
		return result;
	}
	if (dtStatusFailed(m_Query->findNearestPoly(endPoint, extents, &filter,
		&endRef, nearestEnd)) || endRef == 0)
	{
		result.status = VansNavigationPathStatus::Failed;
		result.diagnostic = "End point is outside the navigation mesh";
		return result;
	}

	std::array<dtPolyRef, 256> polygons{};
	int polygonCount = 0;
	const dtStatus pathStatus = m_Query->findPath(startRef, endRef,
		nearestStart, nearestEnd, &filter, polygons.data(), &polygonCount,
		static_cast<int>(polygons.size()));
	if (dtStatusFailed(pathStatus) || polygonCount <= 0)
	{
		result.status = VansNavigationPathStatus::Failed;
		result.diagnostic = "Detour could not find a polygon corridor";
		return result;
	}

	float straightEnd[3] = { nearestEnd[0], nearestEnd[1], nearestEnd[2] };
	const bool complete = polygons[static_cast<std::size_t>(polygonCount - 1)] == endRef;
	if (!complete)
	{
		if (dtStatusFailed(m_Query->closestPointOnPoly(
			polygons[static_cast<std::size_t>(polygonCount - 1)], nearestEnd,
			straightEnd, nullptr)))
		{
			result.status = VansNavigationPathStatus::Failed;
			result.diagnostic = "Detour could not resolve a reachable partial endpoint";
			return result;
		}
	}

	std::array<float, 256 * 3> straightPoints{};
	std::array<unsigned char, 256> straightFlags{};
	std::array<dtPolyRef, 256> straightRefs{};
	int straightCount = 0;
	const dtStatus straightStatus = m_Query->findStraightPath(nearestStart,
		straightEnd, polygons.data(), polygonCount, straightPoints.data(),
		straightFlags.data(), straightRefs.data(), &straightCount, 256);
	if (dtStatusFailed(straightStatus) || straightCount <= 0)
	{
		result.status = VansNavigationPathStatus::Failed;
		result.diagnostic = "Detour could not extract path corners";
		return result;
	}
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
