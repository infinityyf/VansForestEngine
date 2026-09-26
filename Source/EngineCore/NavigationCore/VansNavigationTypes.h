#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
enum class VansNavigationPathStatus : std::uint8_t
{
	None,
	Complete,
	Partial,
	Failed
};

enum class VansNavigationPathFailure : std::uint8_t
{
	None,
	MeshNotReady,
	StartOutsideMesh,
	EndOutsideMesh,
	CorridorSearchFailed,
	SearchNodeCapacityExceeded,
	CorridorCapacityExceeded,
	PartialEndpointResolutionFailed,
	CornerExtractionFailed,
	CornerCapacityExceeded
};

struct VansNavigationBakeSettings
{
	float cellSize = 0.20f;
	float cellHeight = 0.10f;
	float agentHeight = 1.80f;
	float agentRadius = 0.35f;
	float agentMaxClimb = 0.25f;
	float agentMaxSlopeDegrees = 45.0f;
	float regionMinSize = 2.0f;
	float regionMergeSize = 8.0f;
	float edgeMaxLength = 12.0f;
	float edgeMaxError = 1.3f;
	int maximumVerticesPerPolygon = 6;
	bool detailSamplingEnabled = true;
	float detailSampleDistance = 6.0f;
	float detailSampleMaxError = 1.0f;
};

struct VansNavigationQuerySettings
{
	// 查询预算由组合根注入，不属于烘焙后的 NavMesh 内容。
	int maximumSearchNodes = 2048;
	int maximumCorridorPolygons = 256;
	int maximumPathPoints = 256;
};

inline constexpr std::uint8_t kMaximumNavigationAreaId = 62u;

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

struct VansNavigationSettings
{
	VansNavigationBakeSettings bake;
	VansNavigationQuerySettings query;
	VansNavigationAreaSettings areas;
};

bool ValidateNavigationBakeSettings(
	const VansNavigationBakeSettings& settings,
	std::string& error);
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

struct VansNavigationPath
{
	VansNavigationPathStatus status = VansNavigationPathStatus::None;
	VansNavigationPathFailure failure = VansNavigationPathFailure::None;
	std::vector<glm::vec3> points;
	std::string diagnostic;
};
}
