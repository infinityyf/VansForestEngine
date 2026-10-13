#pragma once

#include "../VansRenderBounds.h"

#include <cstdint>
#include <vector>

namespace VansGraphics
{
	// Per-snapshot spatial index for conservative view queries. Query results retain
	// the source order so draw and shadow selection remain deterministic.
	class VansRenderBoundsIndex final
	{
	public:
		struct Entry
		{
			std::uint32_t sourceIndex = 0;
			VansRenderBounds bounds;
			bool hasBounds = false;
			std::uint32_t categoryMask = 0xffffffffu;
		};

		void Build(std::vector<Entry> entries);
		void QueryClipFrustum(const glm::mat4& worldToClip, std::uint32_t categoryMask,
			std::vector<std::uint32_t>& sourceIndices) const;

	private:
		struct Node
		{
			VansRenderAABB bounds;
			std::uint32_t begin = 0;
			std::uint32_t end = 0;
			std::uint32_t left = 0;
			std::uint32_t right = 0;
			std::uint32_t categoryMask = 0;
			bool leaf = false;
		};

		std::uint32_t BuildNode(std::uint32_t begin, std::uint32_t end);
		void QueryNode(std::uint32_t nodeIndex, const glm::mat4& worldToClip,
			std::uint32_t categoryMask,
			std::vector<std::uint32_t>& sourceIndices) const;

		std::vector<Entry> m_Entries;
		std::vector<std::uint32_t> m_Bounded;
		std::vector<std::uint32_t> m_Unbounded;
		std::vector<Node> m_Nodes;
	};
}
