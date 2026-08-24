#pragma once

#include "VansCommonUtils.h"
#include <vector>
#include <queue>
#include <map>
#include <unordered_set>
#include <cstdint>
#include <cassert>

namespace VansGraphics
{
	class VansTransform
	{
	public:
		glm::vec3 m_Position;
		glm::vec3 m_Rotation;
		glm::vec3 m_Scale;

	public:
		glm::mat4x4 GetModelMatrix();
	};

	// Data Oriented Storage for Transforms
	// Centralized transform storage accessible by both render and physics systems
	struct VansTransformStore
	{
		static std::vector<VansTransform> GlobalTransforms;
		static std::vector<uint32_t> TransformGenerations;
		static std::vector<bool> TransformAllocated;
		static std::queue<uint32_t> FreeTransformIndices;

		static std::map<uint32_t, bool> TransformIDToTransformDirty;

#ifdef _DEBUG
		static std::unordered_set<uint32_t> AllocatedTransformIDs;
#endif

		static uint32_t AllocateTransform()
		{
			uint32_t id = 0;
			if (!FreeTransformIndices.empty())
			{
				id = FreeTransformIndices.front();
				FreeTransformIndices.pop();
				// Reset data
				GlobalTransforms[id] = VansTransform();
			}
			else
			{
				GlobalTransforms.emplace_back();
				TransformGenerations.push_back(1u);
				TransformAllocated.push_back(false);
				id = static_cast<uint32_t>(GlobalTransforms.size() - 1);
			}
			TransformAllocated[id] = true;

#ifdef _DEBUG
			AllocatedTransformIDs.insert(id);
#endif
			return id;
		}

		static void FreeTransform(uint32_t id)
		{
			if (id < GlobalTransforms.size() && TransformAllocated[id])
			{
#ifdef _DEBUG
				auto erasedCount = AllocatedTransformIDs.erase(id);
				assert(erasedCount == 1 && "VansTransformStore::FreeTransform double free or invalid id");
#endif
				uint32_t& generation = TransformGenerations[id];
				++generation;
				if (generation == 0) ++generation;
				TransformAllocated[id] = false;
				// Ideally we swap and pop if order doesn't matter, but IDs need to be stable for the Node that holds it.
				// So we use a free list.
				FreeTransformIndices.push(id);
			}
		}

		static VansTransform& GetTransform(uint32_t id)
		{
			// Add safety check if needed
			return GlobalTransforms[id];
		}

		static uint32_t GetGeneration(uint32_t id)
		{
			return id < TransformGenerations.size() ? TransformGenerations[id] : 0u;
		}

		static bool IsAllocated(uint32_t id)
		{
			return id < TransformAllocated.size() && TransformAllocated[id];
		}
	};
}
