#include "VansNavigationSource.h"
#include "../Util/VansFileFingerprint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Vans
{
namespace
{
class StableHash
{
public:
	template <typename T>
	void Append(const T& value)
	{
		m_Value = ContinueMemoryFnv1a64(m_Value, &value, sizeof(value));
	}

	std::uint64_t Value() const { return m_Value; }

private:
	std::uint64_t m_Value = VANS_FNV1A64_OFFSET_BASIS;
};
}

std::uint64_t HashNavigationBakeSettings(const VansNavigationBakeSettings& bake)
{
	StableHash hash;
	hash.Append(bake.cellSize);
	hash.Append(bake.cellHeight);
	hash.Append(bake.agentHeight);
	hash.Append(bake.agentRadius);
	hash.Append(bake.agentMaxClimb);
	hash.Append(bake.agentMaxSlopeDegrees);
	hash.Append(bake.regionMinSize);
	hash.Append(bake.regionMergeSize);
	hash.Append(bake.edgeMaxLength);
	hash.Append(bake.edgeMaxError);
	hash.Append(bake.maximumVerticesPerPolygon);
	const std::uint8_t detailSamplingEnabled =
		bake.detailSamplingEnabled ? 1u : 0u;
	hash.Append(detailSamplingEnabled);
	hash.Append(bake.detailSampleDistance);
	hash.Append(bake.detailSampleMaxError);
	return hash.Value();
}

bool ComputeNavigationGeometryHash(const VansNavigationGeometry& geometry,
	std::uint64_t& output, std::string& error)
{
	error.clear();
	if (geometry.vertices.size() % 3u != 0u || geometry.indices.size() % 3u != 0u ||
		geometry.areas.size() != geometry.TriangleCount())
	{
		error = "Navigation source geometry has invalid triangle data";
		return false;
	}
	using Point = std::array<float, 3>;
	struct Triangle
	{
		std::array<Point, 3> points;
		std::uint8_t area;
	};
	std::vector<Triangle> triangles;
	triangles.reserve(geometry.TriangleCount());
	for (std::size_t index = 0; index < geometry.TriangleCount(); ++index)
	{
		Triangle triangle{};
		triangle.area = geometry.areas[index];
		for (std::size_t corner = 0; corner < 3u; ++corner)
		{
			const int vertex = geometry.indices[index * 3u + corner];
			if (vertex < 0 || static_cast<std::size_t>(vertex) >= geometry.VertexCount())
			{
				error = "Navigation source geometry has an out-of-range vertex";
				return false;
			}
			for (std::size_t axis = 0; axis < 3u; ++axis)
			{
				const float value = geometry.vertices[static_cast<std::size_t>(vertex) * 3u + axis];
				if (!std::isfinite(value))
				{
					error = "Navigation source geometry contains a non-finite vertex";
					return false;
				}
				triangle.points[corner][axis] = value == 0.0f ? 0.0f : value;
			}
		}
		// 循环旋转保留绕序，排除顶点编号、实体顺序和三角形排列的变化。
		std::array<Point, 3> canonical = triangle.points;
		for (std::size_t rotation = 1; rotation < 3u; ++rotation)
		{
			const std::array<Point, 3> candidate = { triangle.points[rotation],
				triangle.points[(rotation + 1u) % 3u], triangle.points[(rotation + 2u) % 3u] };
			if (candidate < canonical) canonical = candidate;
		}
		triangle.points = canonical;
		triangles.push_back(triangle);
	}
	std::sort(triangles.begin(), triangles.end(), [](const Triangle& left, const Triangle& right)
	{
		return left.points == right.points ? left.area < right.area : left.points < right.points;
	});
	StableHash hash;
	hash.Append(static_cast<std::uint64_t>(triangles.size()));
	for (const Triangle& triangle : triangles)
	{
		for (const Point& point : triangle.points)
			for (float axis : point) hash.Append(axis);
		hash.Append(triangle.area);
	}
	output = hash.Value();
	return true;
}
}
