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

struct VansNavigationBuildSettings
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
	float detailSampleDistance = 6.0f;
	float detailSampleMaxError = 1.0f;
};

struct VansNavigationGeometry
{
	std::vector<float> vertices;
	std::vector<int> indices;

	bool Empty() const { return vertices.empty() || indices.empty(); }
	std::size_t VertexCount() const { return vertices.size() / 3u; }
	std::size_t TriangleCount() const { return indices.size() / 3u; }
};

struct VansNavigationPath
{
	VansNavigationPathStatus status = VansNavigationPathStatus::None;
	std::vector<glm::vec3> points;
	std::string diagnostic;
};
}
