#pragma once

#include "VansCommonUtils.h"
#include <vector>
#include <queue>
#include <map>

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
		static std::queue<uint32_t> FreeTransformIndices;

		static std::map<uint32_t, bool> TransformIDToTransformDirty;

		static uint32_t AllocateTransform()
		{
			if (!FreeTransformIndices.empty())
			{
				uint32_t id = FreeTransformIndices.front();
				FreeTransformIndices.pop();
				// Reset data
				GlobalTransforms[id] = VansTransform();
				return id;
			}
			else
			{
				GlobalTransforms.emplace_back();
				return static_cast<uint32_t>(GlobalTransforms.size() - 1);
			}
		}

		static void FreeTransform(uint32_t id)
		{
			if (id < GlobalTransforms.size())
			{
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
	};
}
