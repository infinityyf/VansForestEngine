#pragma once

#include "VansCommonUtils.h"
#include <vector>
#include <queue>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cstdint>

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

	// ══════════════════════════════════════════════════════════════════════════
	//  Transform Parenting System
	//  Optional parent-child relationship between transforms.  The child's
	//  world transform in VansTransformStore is always the source of truth;
	//  parenting simply adjusts it each frame when the parent moves.
	// ══════════════════════════════════════════════════════════════════════════

	struct TransformParentLink
	{
		uint32_t childTransformID  = UINT32_MAX;
		uint32_t parentTransformID = UINT32_MAX;

		// Cached parent world matrix from last frame — used to detect parent movement.
		glm::mat4 prevParentWorldMatrix = glm::mat4(1.0f);

		// Relative transform: child expressed in parent space.
		// Recomputed whenever the child is edited directly (offset dirty).
		glm::mat4 offsetMatrix = glm::mat4(1.0f);

		// When true the offset needs to be recalculated from current world transforms.
		bool offsetDirty = true;
	};

	class VansTransformParentSystem
	{
	public:
		// ── Link management ───────────────────────────────────────────────────
		void SetParent(uint32_t childTransformID, uint32_t parentTransformID);
		void ClearParent(uint32_t childTransformID);

		bool HasParent(uint32_t childTransformID) const;
		uint32_t GetParent(uint32_t childTransformID) const;

		// Call after editor / gizmo / physics writes a child transform.
		void MarkOffsetDirty(uint32_t childTransformID);

		// ── Per-frame resolve (call BEFORE UpdateTransformRenderData) ─────────
		void ResolveParentChildTransforms();

		// ── Lifecycle ─────────────────────────────────────────────────────────
		void Clear();

		const std::vector<TransformParentLink>& GetAllLinks() const { return m_Links; }

	private:
		void SortLinksTopologically();

		// Decompose a TRS matrix back into position, rotation (degrees), scale.
		static void DecomposeMatrix(const glm::mat4& m, glm::vec3& pos,
		                            glm::vec3& rotDeg, glm::vec3& scale);

		// Epsilon-based mat4 equality check.
		static bool MatrixApproxEqual(const glm::mat4& a, const glm::mat4& b,
		                              float eps = 1e-5f);

		std::vector<TransformParentLink>            m_Links;
		std::unordered_map<uint32_t, size_t>        m_ChildToLinkIndex;
	};
}
