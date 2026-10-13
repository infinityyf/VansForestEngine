#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <cstddef>
#include <array>
#include <string>
#include <vector>
#include <utility>

namespace Vans
{
inline constexpr std::size_t kMaximumNavigationProfiles = 16;
inline constexpr std::size_t kMaximumNavigationMaps = 64;
inline constexpr std::size_t kMaximumNavigationQueryFilters = 64;
inline constexpr std::size_t kMaximumNavigationLinkCapabilities = 512;
// 由连接 owner 分配的逻辑身份，不是 Detour polygon 引用。
struct VansNavigationLinkKey
{
    std::uint32_t value=0;
    bool operator==(const VansNavigationLinkKey& other) const { return value==other.value; }
    bool operator<(const VansNavigationLinkKey& other) const { return value<other.value; }
};
bool CompileNavigationLinkCapabilities(const std::vector<VansNavigationLinkKey>& keys,
    std::array<VansNavigationLinkKey,kMaximumNavigationLinkCapabilities>& output,std::string& error);
enum class VansNavigationPathStatus : std::uint8_t
{
	None,
	Complete,
	Partial,
	Failed
};
enum class VansNavigationGoalAcceptance : std::uint8_t { RequestedPoint,ProjectedPoint };
bool ValidateNavigationGoalProjection(VansNavigationGoalAcceptance acceptance,float toleranceMeters,std::string& error);

enum class VansNavigationPathFailure : std::uint8_t
{
	None,
	MeshNotReady,
	InvalidInput,
	QueryNotReady,
	MeshChanged,
	StartOutsideMesh,
	EndOutsideMesh,
	CorridorSearchFailed,
	SearchNodeCapacityExceeded,
	CorridorCapacityExceeded,
	PartialEndpointResolutionFailed,
	CornerExtractionFailed,
	CornerCapacityExceeded,
    QueueFull, QueryDeadlineExceeded, NoReachablePoint, Cancelled, MapUnavailable, FilterInvalid, EndProjectionRejected,
    SpatialQueryFailed, RaycastCapacityExceeded, BoundaryCapacityExceeded, DataUnavailable
};

enum class VansNavigationRequestReason : std::uint8_t
{
	None, InitialTarget, TargetMoved, ForceTargetMoved, PathRecovery, Patrol, RequestOptions
};
const char* NavigationPathFailureName(VansNavigationPathFailure failure);
struct VansNavigationBakeSettings
{
    // Derived from the Map update policy; there is no second authoring switch.
    bool buildTileCacheLayers = false;
	float cellSize = 0;
	float cellHeight = 0;
	float agentHeight = 0;
	float agentRadius = 0;
	float agentMaxClimb = 0;
	float agentMaxSlopeDegrees = 0;
	float regionMinSize = 0;
	float regionMergeSize = 0;
	float edgeMaxLength = 0;
	float edgeMaxError = 0;
	int maximumVerticesPerPolygon = 0;
	bool detailSamplingEnabled = false;
	float detailSampleDistance = 0;
	float detailSampleMaxError = 0;
	// 固定 Tile 网格和构建预算；原点采用世界空间米，Tile 尺寸采用体素格数。
	int tileSizeCells = 0;
	glm::vec3 tileOrigin{ 0.0f };
	int maximumTiles = 0;
	int maximumPolygonsPerTile = 0;
	int maximumBuildBytes = 0;
};

struct VansNavigationQuerySettings
{
	// 查询预算由组合根注入，不属于烘焙后的 NavMesh 内容。
	int maximumSearchNodes = 2048;
	int maximumCorridorPolygons = 256;
	int maximumPathPoints = 256;
	int maximumActiveAgents = 256;
	int maximumIterationsPerFrame = 512;
	int maximumIterationsPerSlice = 32;
	int maximumPathStartsPerFrame = 8;
    int maximumQueuedQueries = 512;
    int maximumActiveQueries = 8;
    int maximumQueryHistory = 256;
    int maximumQueryFrames = 600;
};

struct VansNavigationAvoidanceSettings
{
    float queryRangeMeters = 3.0f;
    float horizonSeconds = 2.5f;
    float maximumYieldSeconds = 8.0f;
    int maximumNeighbors = 8;
    int maximumNeighborScans = 128;
    int maximumCellsPerBody = 64;
    int maximumGridEntries = 131072;
    int maximumBoundaryPolygons = 32;
    int maximumBoundarySegments = 16;
    int adaptiveDivisions = 5;
    int adaptiveRings = 2;
    int adaptiveDepth = 3;
    int maximumSamplesPerFrame = 8192;
};
bool ValidateNavigationAvoidanceSettings(const VansNavigationAvoidanceSettings& settings,std::string& error);
// 记录准入和帧网格使用同一覆盖范围策略。
bool NavigationAvoidanceGridBounds(const glm::vec3& minimum,const glm::vec3& maximum,
    const VansNavigationAvoidanceSettings& settings,std::array<int,4>& cells,std::string& error);
std::uint64_t EstimateNavigationAvoidanceBytes(int maximumAgents,const VansNavigationAvoidanceSettings& settings);

struct VansNavigationBakeQuantization
{
	int heightCells = 0;
	int climbCells = 0;
	int radiusCells = 0;
	int maximumEdgeCells = 0;
	int minimumRegionCellArea = 0;
	int mergeRegionCellArea = 0;
	float detailSampleDistanceMeters = 0.0f;
	float detailSampleErrorMeters = 0.0f;
};

inline constexpr std::uint8_t kMaximumNavigationAreaId = 62u;
inline constexpr int kMaximumNavigationPolygonVertices = 6;

struct VansNavigationAreaDefinition
{
	std::string name;
	std::uint8_t id = 0;
	float traversalCost = 1.0f;
	bool traversable = true;
};

struct VansNavigationAreaSettings
{
	std::string defaultArea = "Walkable";
	std::vector<VansNavigationAreaDefinition> definitions = {
		{ "Walkable", 0u, 1.0f, true }
	};
};

struct VansNavigationQueryFilterSettings
{
	std::string id = "Default";
	std::vector<std::string> excludedAreas;
	std::vector<std::pair<std::string, float>> areaCosts;
};

struct VansNavigationAgentProfile
{
	std::string id = "Humanoid";
	float heightMeters = 1.80f;
	float radiusMeters = 0.35f;
	float maxClimbMeters = 0.25f;
	float maxSlopeDegrees = 45.0f;
};

enum class VansNavigationMapUpdateMode : std::uint8_t { Baked, Runtime };
const char* NavigationMapUpdateModeName(VansNavigationMapUpdateMode mode);
struct VansNavigationMapSettings
{
	std::string id = "World";
    VansNavigationMapUpdateMode updateMode=VansNavigationMapUpdateMode::Baked;
	std::vector<std::string> profiles{ "Humanoid" };
	float cellSizeMeters = 0.20f;
	float cellHeightMeters = 0.10f;
	float minimumRegionSizeCells = 2.0f;
	float mergeRegionSizeCells = 8.0f;
	float maximumEdgeLengthMeters = 12.0f;
	float maximumEdgeErrorCells = 1.3f;
	int maximumVerticesPerPolygon = 6;
	bool detailSamplingEnabled = true;
	float detailSampleDistanceCells = 6.0f;
	float detailSampleErrorCells = 1.0f;
	int tileSizeCells = 64;
	glm::vec3 originMeters{ 0.0f };
	int maximumTiles = 4096;
	int maximumPolygonsPerTile = 1024;
	int maximumBuildBytes = 128 * 1024 * 1024;
};

struct VansNavigationSettings
{
	std::vector<VansNavigationAgentProfile> profiles{ VansNavigationAgentProfile{} };
	std::vector<VansNavigationMapSettings> maps{ VansNavigationMapSettings{} };
	VansNavigationQuerySettings query;
	VansNavigationAvoidanceSettings avoidance;
	VansNavigationAreaSettings areas;
	std::vector<VansNavigationQueryFilterSettings> queryFilters{ VansNavigationQueryFilterSettings{} };
	int maximumResidentBytes = 256 * 1024 * 1024;
	int maximumConcurrentBuildBytes = 256 * 1024 * 1024;
	int maximumResidentTiles = 65536;
};
bool SameNavigationSettings(const VansNavigationSettings& left,const VansNavigationSettings& right);

const VansNavigationAgentProfile* FindNavigationProfile(const VansNavigationSettings& settings, const std::string& id);
const VansNavigationMapSettings* FindNavigationMap(const VansNavigationSettings& settings, const std::string& id);
// 领域目录合成唯一 Recast 输入；不是旧配置回退或作者默认补齐。
bool ResolveNavigationBakeSettings(const VansNavigationSettings& settings, const std::string& mapId,
	const std::string& profileId, VansNavigationBakeSettings& output, std::string& error);
std::uint64_t EstimateNavigationQueryBytes(const VansNavigationQuerySettings& settings);

struct VansNavigationCompiledFilter
{
	std::string id;
	std::array<bool, kMaximumNavigationAreaId + 1u> allowed{};
	std::array<float, kMaximumNavigationAreaId + 1u> costs{};
	float minimumCost = 1.0f;
};

bool CompileNavigationFilter(const VansNavigationAreaSettings& areas,
	const VansNavigationQueryFilterSettings& filter, VansNavigationCompiledFilter& output,
	std::string& error);

bool ValidateNavigationBakeSettings(
	const VansNavigationBakeSettings& settings,
	std::string& error);
bool QuantizeNavigationBakeSettings(const VansNavigationBakeSettings& settings,
	VansNavigationBakeQuantization& output, std::string& error);
bool ValidateNavigationQuerySettings(
	const VansNavigationQuerySettings& settings,
	std::string& error);
bool ValidateNavigationAreaSettings(
	const VansNavigationAreaSettings& settings,
	std::string& error);
bool ValidateNavigationSettings(
	const VansNavigationSettings& settings,
	std::string& error);
const VansNavigationAreaDefinition* FindNavigationAreaByName(
	const VansNavigationAreaSettings& settings,
	const std::string& name);
const VansNavigationAreaDefinition* FindNavigationAreaById(
	const VansNavigationAreaSettings& settings,
	std::uint8_t id);

struct VansNavigationGeometry
{
	std::vector<float> vertices;
	std::vector<int> indices;
	std::vector<std::uint8_t> areas;

	bool Empty() const { return vertices.empty() || indices.empty(); }
	std::size_t VertexCount() const { return vertices.size() / 3u; }
	std::size_t TriangleCount() const { return indices.size() / 3u; }
};

enum class VansNavigationCornerFlag : std::uint8_t { Start=1,End=2,Link=4 };
// Geometry updates select horizontal cells; every native layer in the cell is
// replaced together, including an empty result when its final contribution ends.
struct VansNavigationTileCell
{
    int x=0,z=0;
    bool operator==(const VansNavigationTileCell& other) const { return x==other.x && z==other.z; }
    bool operator<(const VansNavigationTileCell& other) const { return z<other.z || (z==other.z && x<other.x); }
};
inline constexpr std::size_t kMaximumNavigationTileUpdateBatch=16;
struct VansNavigationTileInputStamp
{
    VansNavigationTileCell cell;
    std::uint64_t inputEpoch=0;
};
// Captured from the owning Map before a CPU build. Identity rejects completion
// from a replaced Map; each cell stamp rejects only its own changed input.
struct VansNavigationTileUpdate
{
    std::uint64_t mapIdentity=0;
    float rasterOriginMeters=0;
    std::vector<VansNavigationTileInputStamp> cells;
};
enum class VansNavigationTileCommitState : std::uint8_t { Applied, Deferred, Stale, Rejected, Invalidated };
struct VansNavigationTileCommitResult
{
    VansNavigationTileCommitState state=VansNavigationTileCommitState::Rejected;
    std::size_t appliedCells=0;
};
struct VansNavigationTileDependency
{
	int x=0,z=0,layer=0;
    std::uint64_t contentEpoch=0;
};
inline constexpr std::size_t kMaximumNavigationDebugTiles=128;
inline constexpr std::size_t kMaximumNavigationDebugTriangles=16384;
inline constexpr std::size_t kMaximumNavigationDebugLinks=kMaximumNavigationLinkCapabilities;
struct VansNavigationDebugView
{
	// 指向视锥内部的单位平面，世界坐标；工具必须明确提供实际视口范围。
	std::array<glm::vec4,6> planes{};
	std::size_t maximumTiles=64,maximumTriangles=8192;
    std::size_t maximumLinks=128;
};
struct VansNavigationDebugTriangle
{
	std::array<glm::vec3,3> positions;
	std::array<bool,3> tileEdges{};
	std::uint8_t area=0;
	bool traversable=false;
};
struct VansNavigationDebugLink
{
    VansNavigationLinkKey key;
    std::array<glm::vec3,2> positions;
    float radiusMeters=0;
    bool bidirectional=false,areaAllowed=false;
};
struct VansNavigationDebugGeometry
{
	std::vector<VansNavigationDebugTriangle> triangles;
    std::vector<VansNavigationDebugLink> links;
	std::size_t tileCount=0;
	bool limited=false;
};
struct VansNavigationPointProjection
{
	bool valid=false;
	glm::vec3 requested{0},position{0};
	float distance=0;
	VansNavigationPathFailure failure=VansNavigationPathFailure::None;
	std::string diagnostic;
};
inline constexpr float kMaximumNavigationSpatialDistanceMeters=10000.0f;
bool ValidateNavigationSpatialDistance(double distanceMeters,std::string& error);
// 地表 XZ 可行性查询；不经过 Link，也不表示另一个楼层的原始目标可达。
struct VansNavigationRaycast
{
	bool finished=false,valid=false,hit=false;
	glm::vec3 requestedStart{0},requestedEnd{0},projectedStart{0},position{0},normal{0};
	float fraction=1,projectionDistance=0;
	std::vector<VansNavigationTileDependency> tiles;
	VansNavigationPathFailure failure=VansNavigationPathFailure::None;
	std::string diagnostic;
};
struct VansNavigationBoundary
{
	bool finished=false,valid=false,found=false;
	glm::vec3 requested{0},projected{0},position{0},normal{0};
	float distance=0,projectionDistance=0;
	VansNavigationPathFailure failure=VansNavigationPathFailure::None;
	std::string diagnostic;
};
struct VansNavigationPath
{
	VansNavigationPathStatus status = VansNavigationPathStatus::None;
	VansNavigationPathFailure failure = VansNavigationPathFailure::None;
	std::vector<glm::vec3> points;
	std::vector<std::uint8_t> cornerFlags;
	std::vector<VansNavigationTileDependency> tiles;
	glm::vec3 requestedStart{0},requestedEnd{0},projectedStart{0},projectedEnd{0},reachedEnd{0};
	float startProjectionDistance=0,endProjectionDistance=0;
	bool hasRequestCoordinates=false;
	bool hasProjection=false;
	VansNavigationGoalAcceptance goalAcceptance=VansNavigationGoalAcceptance::ProjectedPoint;
	std::string diagnostic;
};
}
