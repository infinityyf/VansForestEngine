#include "VansClothMeshPrep.h"

#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace VansEngine
{
	namespace
	{
		using WeldKey = std::tuple<std::int64_t, std::int64_t, std::int64_t>;

		bool IsFinite(const glm::vec3& value)
		{
			return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
		}
	}

	bool VansClothMeshPrep::Build(
		const VansClothMeshSource& source,
		float weldTolerance,
		float pinnedMatchTolerance,
		float attachOffsetY,
		const std::vector<glm::vec3>& authoredPinnedPositions,
		VansClothMeshData& mesh,
		std::string& error)
	{
		error.clear();
		mesh = {};
		if (!source.positionsAndNormals || !source.texCoords || !source.triangleIndices)
		{
			error = "Cloth mesh source is incomplete";
			return false;
		}
		if (source.vertexCount <= 0 ||
			source.positionsAndNormals->size() < static_cast<std::size_t>(source.vertexCount) * 8u)
		{
			error = "Cloth mesh has incomplete CPU position data";
			return false;
		}
		if (source.triangleIndices->empty() || source.triangleIndices->size() % 3u != 0u)
		{
			error = "Cloth mesh requires a complete triangle index buffer";
			return false;
		}
		if (!std::isfinite(weldTolerance) || weldTolerance <= 0.0f ||
			!std::isfinite(pinnedMatchTolerance) || pinnedMatchTolerance <= 0.0f ||
			!std::isfinite(attachOffsetY))
		{
			error = "Cloth mesh tolerances and attachment offset must be finite and positive where required";
			return false;
		}

		mesh.vertexCount = source.vertexCount;
		constexpr std::uint32_t baseVertexStride = 8u * sizeof(std::uint16_t);
		constexpr std::uint32_t tangentVertexStride = 12u * sizeof(std::uint16_t);
		if (source.vertexStrideBytes != baseVertexStride &&
			source.vertexStrideBytes != tangentVertexStride)
		{
			error = "Cloth mesh vertex layout does not match the render mesh ABI";
			return false;
		}
		mesh.hasTangent = source.vertexStrideBytes == tangentVertexStride;
		mesh.packedVertexStride = static_cast<int>(
			source.vertexStrideBytes / sizeof(std::uint16_t));
		mesh.originalTriangles.reserve(source.triangleIndices->size());
		for (int index : *source.triangleIndices)
		{
			if (index < 0 || index >= source.vertexCount)
			{
				error = "Cloth mesh contains an out-of-range triangle index";
				return false;
			}
			mesh.originalTriangles.push_back(static_cast<std::uint32_t>(index));
		}

		mesh.hadTexCoords = source.texCoords->size() >=
			static_cast<std::size_t>(source.vertexCount) * 2u;
		if (mesh.hadTexCoords)
		{
			mesh.texCoords.assign(
				source.texCoords->begin(),
				source.texCoords->begin() + static_cast<std::size_t>(source.vertexCount) * 2u);
		}
		else
		{
			mesh.texCoords.assign(static_cast<std::size_t>(source.vertexCount) * 2u, 0.0f);
		}

		const double inverseTolerance = 1.0 / static_cast<double>(weldTolerance);
		std::map<WeldKey, std::uint32_t> weldedByPosition;
		mesh.originalToParticle.resize(source.vertexCount);
		std::vector<glm::vec3> localPositions(source.vertexCount);
		for (int vertexIndex = 0; vertexIndex < source.vertexCount; ++vertexIndex)
		{
			const glm::vec3 local(
				(*source.positionsAndNormals)[vertexIndex * 8 + 0],
				(*source.positionsAndNormals)[vertexIndex * 8 + 1],
				(*source.positionsAndNormals)[vertexIndex * 8 + 2]);
			if (!IsFinite(local))
			{
				error = "Cloth mesh contains a non-finite position";
				return false;
			}
			localPositions[vertexIndex] = local;
			const WeldKey key{
				static_cast<std::int64_t>(std::llround(local.x * inverseTolerance)),
				static_cast<std::int64_t>(std::llround(local.y * inverseTolerance)),
				static_cast<std::int64_t>(std::llround(local.z * inverseTolerance)) };
			const auto found = weldedByPosition.emplace(
				key, static_cast<std::uint32_t>(weldedByPosition.size()));
			mesh.originalToParticle[vertexIndex] = found.first->second;
		}

		mesh.particles.resize(weldedByPosition.size(), glm::vec4(0.0f));
		for (int vertexIndex = 0; vertexIndex < source.vertexCount; ++vertexIndex)
		{
			const std::uint32_t particleIndex = mesh.originalToParticle[vertexIndex];
			glm::vec4 world = source.modelMatrix * glm::vec4(localPositions[vertexIndex], 1.0f);
			world.y += attachOffsetY;
			mesh.particles[particleIndex] = glm::vec4(world.x, world.y, world.z, 1.0f);
		}

		mesh.particleTriangles.reserve(mesh.originalTriangles.size());
		for (std::size_t triangleIndex = 0;
			triangleIndex < mesh.originalTriangles.size(); triangleIndex += 3u)
		{
			const std::uint32_t a = mesh.originalToParticle[mesh.originalTriangles[triangleIndex + 0u]];
			const std::uint32_t b = mesh.originalToParticle[mesh.originalTriangles[triangleIndex + 1u]];
			const std::uint32_t c = mesh.originalToParticle[mesh.originalTriangles[triangleIndex + 2u]];
			if (a == b || b == c || a == c)
				continue;
			mesh.particleTriangles.push_back(a);
			mesh.particleTriangles.push_back(b);
			mesh.particleTriangles.push_back(c);
		}
		if (mesh.particleTriangles.empty())
		{
			error = "Cloth mesh has no triangles after vertex welding";
			return false;
		}

		const glm::mat4 inverseModel = glm::inverse(source.modelMatrix);
		std::vector<bool> pinned(mesh.particles.size(), false);
		for (std::size_t authoredIndex = 0;
			authoredIndex < authoredPinnedPositions.size(); ++authoredIndex)
		{
			const glm::vec3& authored = authoredPinnedPositions[authoredIndex];
			if (!IsFinite(authored))
			{
				error = "Cloth pinned position " + std::to_string(authoredIndex) + " is non-finite";
				return false;
			}
			float bestDistanceSquared = std::numeric_limits<float>::max();
			int bestVertex = -1;
			for (int vertexIndex = 0; vertexIndex < source.vertexCount; ++vertexIndex)
			{
				const glm::vec3 delta = localPositions[vertexIndex] - authored;
				const float distanceSquared = glm::dot(delta, delta);
				if (distanceSquared < bestDistanceSquared)
				{
					bestDistanceSquared = distanceSquared;
					bestVertex = vertexIndex;
				}
			}
			if (bestVertex < 0 || bestDistanceSquared > pinnedMatchTolerance * pinnedMatchTolerance)
			{
				error = "Cloth pinned position " + std::to_string(authoredIndex) +
					" does not match a mesh vertex within pinnedMatchTolerance";
				return false;
			}

			const std::uint32_t particleIndex = mesh.originalToParticle[bestVertex];
			if (pinned[particleIndex])
				continue;
			pinned[particleIndex] = true;
			mesh.particles[particleIndex].w = 0.0f;
			const glm::vec4 world(
				mesh.particles[particleIndex].x,
				mesh.particles[particleIndex].y,
				mesh.particles[particleIndex].z,
				1.0f);
			mesh.pinnedParticles.push_back(particleIndex);
			mesh.pinnedSourceIndices.push_back(static_cast<std::uint32_t>(authoredIndex));
			mesh.pinnedLocalPositions.push_back(glm::vec3(inverseModel * world));
		}
		return true;
	}
}
