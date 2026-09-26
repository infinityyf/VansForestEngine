#pragma once

#include <GLM/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace VansEngine
{
	struct VansClothMeshSource
	{
		const std::vector<float>* positionsAndNormals = nullptr;
		const std::vector<float>* texCoords = nullptr;
		const std::vector<int>* triangleIndices = nullptr;
		int vertexCount = 0;
		std::uint32_t vertexStrideBytes = 0;
		glm::mat4 modelMatrix{ 1.0f };
	};

	struct VansClothMeshData
	{
		std::vector<glm::vec4> particles;
		std::vector<std::uint32_t> particleTriangles;
		std::vector<std::uint32_t> originalTriangles;
		std::vector<std::uint32_t> originalToParticle;
		std::vector<std::uint32_t> pinnedParticles;
		std::vector<std::uint32_t> pinnedSourceIndices;
		std::vector<glm::vec3> pinnedLocalPositions;
		std::vector<float> texCoords;
		int vertexCount = 0;
		int packedVertexStride = 8;
		bool hasTangent = false;
		bool hadTexCoords = false;
	};

	// Pure CPU preparation boundary between retained mesh data and NvCloth.
	// The result contains value data only and has no scene or render-node ownership.
	class VansClothMeshPrep final
	{
	public:
		static bool Build(
			const VansClothMeshSource& source,
			float weldTolerance,
			float pinnedMatchTolerance,
			float attachOffsetY,
			const std::vector<glm::vec3>& authoredPinnedPositions,
			VansClothMeshData& mesh,
			std::string& error);
	};
}
