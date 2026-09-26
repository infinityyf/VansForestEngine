#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Vans
{
struct VansTriangleMeshData
{
	std::vector<float> positions;
	std::vector<std::uint32_t> indices;

	bool Empty() const { return positions.empty() || indices.empty(); }
	std::size_t VertexCount() const { return positions.size() / 3u; }
	std::size_t TriangleCount() const { return indices.size() / 3u; }
};
}
