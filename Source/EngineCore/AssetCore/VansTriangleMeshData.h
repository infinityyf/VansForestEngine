#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <string>
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

	// Validate packed CPU geometry before passing counts or pointers to native consumers.
	bool Validate(std::uint64_t maximumBytes, std::string& error) const
	{
		if (Empty() || positions.size() % 3u || indices.size() % 3u ||
			VertexCount() > (std::numeric_limits<std::uint32_t>::max)() ||
			TriangleCount() > (std::numeric_limits<std::uint32_t>::max)())
		{ error = "CPU triangle mesh has invalid packed dimensions"; return false; }
		if (positions.size() > maximumBytes / sizeof(float) ||
			indices.size() > (maximumBytes - positions.size() * sizeof(float)) / sizeof(std::uint32_t))
		{ error = "CPU triangle mesh exceeds the consumer memory budget"; return false; }
		for (const auto value : positions)
			if (!std::isfinite(value))
			{ error = "CPU triangle mesh vertex is non-finite"; return false; }
		for (const auto index : indices)
			if (index >= VertexCount())
			{ error = "CPU triangle mesh index is out of range"; return false; }
		error.clear(); return true;
	}
};
}
