#include "VansShadowAtlasAllocator.h"

#include <algorithm>

namespace VansGraphics
{
	VansShadowAtlasAllocator::VansShadowAtlasAllocator(uint32_t atlasSize, uint32_t basePageSize, uint32_t gutter)
	{
		Reset(atlasSize, basePageSize, gutter);
	}

	void VansShadowAtlasAllocator::Reset(uint32_t atlasSize, uint32_t basePageSize, uint32_t gutter)
	{
		m_AtlasSize = atlasSize;
		m_BasePageSize = basePageSize;
		m_Gutter = gutter;
		m_UsedPages = 0;
		m_Nodes.clear();

		if (!IsPowerOfTwo(m_AtlasSize) || !IsPowerOfTwo(m_BasePageSize) ||
			m_BasePageSize > m_AtlasSize || (m_AtlasSize % m_BasePageSize) != 0)
		{
			m_AtlasSize = 4096;
			m_BasePageSize = 128;
		}

		const uint32_t pagesPerAxis = m_AtlasSize / m_BasePageSize;
		m_TotalPages = pagesPerAxis * pagesPerAxis;
		BuildNode(VANS_INVALID_SHADOW_INDEX, 0, 0, m_AtlasSize);
	}

	uint32_t VansShadowAtlasAllocator::BuildNode(uint32_t parent, uint32_t x, uint32_t y, uint32_t size)
	{
		const uint32_t nodeIndex = static_cast<uint32_t>(m_Nodes.size());
		m_Nodes.push_back({});
		m_Nodes[nodeIndex].parent = parent;
		m_Nodes[nodeIndex].x = x;
		m_Nodes[nodeIndex].y = y;
		m_Nodes[nodeIndex].size = size;

		if (size > m_BasePageSize)
		{
			const uint32_t half = size / 2;
			// BuildNode appends to m_Nodes and may reallocate the vector.  Compute
			// every child index first so no reference into m_Nodes survives a
			// recursive call.
			const uint32_t child0 = BuildNode(nodeIndex, x, y, half);
			const uint32_t child1 = BuildNode(nodeIndex, x + half, y, half);
			const uint32_t child2 = BuildNode(nodeIndex, x, y + half, half);
			const uint32_t child3 = BuildNode(nodeIndex, x + half, y + half, half);
			m_Nodes[nodeIndex].children = { child0, child1, child2, child3 };
		}

		return nodeIndex;
	}

	bool VansShadowAtlasAllocator::Allocate(uint32_t resolution, VansShadowAtlasBlock& outBlock)
	{
		outBlock = {};
		if (!IsPowerOfTwo(resolution) || resolution < m_BasePageSize || resolution > m_AtlasSize || m_Nodes.empty())
			return false;

		uint32_t nodeIndex = VANS_INVALID_SHADOW_INDEX;
		if (!AllocateFromNode(0, resolution, nodeIndex))
			return false;

		Node& node = m_Nodes[nodeIndex];
		node.generation = static_cast<uint16_t>(node.generation + 1u);
		if (node.generation == 0)
			node.generation = 1;

		const uint32_t pagesPerAxis = resolution / m_BasePageSize;
		m_UsedPages += pagesPerAxis * pagesPerAxis;

		outBlock.nodeIndex = nodeIndex;
		outBlock.generation = node.generation;
		outBlock.resolution = static_cast<uint16_t>(resolution);
		outBlock.x = static_cast<uint16_t>(node.x);
		outBlock.y = static_cast<uint16_t>(node.y);
		outBlock.gutter = static_cast<uint16_t>((std::min)(m_Gutter, resolution / 8u));
		return true;
	}

	bool VansShadowAtlasAllocator::AllocateGroup(
		uint32_t resolution,
		uint32_t viewCount,
		std::array<VansShadowAtlasBlock, 6>& outBlocks)
	{
		outBlocks = {};
		if (viewCount == 0 || viewCount > outBlocks.size())
			return false;

		for (uint32_t view = 0; view < viewCount; ++view)
		{
			if (!Allocate(resolution, outBlocks[view]))
			{
				for (uint32_t rollback = 0; rollback < view; ++rollback)
					Free(outBlocks[rollback]);
				outBlocks = {};
				return false;
			}
		}
		return true;
	}

	bool VansShadowAtlasAllocator::Free(const VansShadowAtlasBlock& block)
	{
		if (!Validate(block))
			return false;

		Node& node = m_Nodes[block.nodeIndex];
		node.state = NodeState::Free;
		const uint32_t pagesPerAxis = node.size / m_BasePageSize;
		const uint32_t pageCount = pagesPerAxis * pagesPerAxis;
		m_UsedPages = pageCount <= m_UsedPages ? m_UsedPages - pageCount : 0;
		TryCoalesce(block.nodeIndex);
		return true;
	}

	bool VansShadowAtlasAllocator::Validate(const VansShadowAtlasBlock& block) const
	{
		if (!block.IsValid() || block.nodeIndex >= m_Nodes.size())
			return false;
		const Node& node = m_Nodes[block.nodeIndex];
		return node.state == NodeState::Allocated &&
			node.generation == block.generation &&
			node.size == block.resolution;
	}

	bool VansShadowAtlasAllocator::AllocateFromNode(uint32_t nodeIndex, uint32_t resolution, uint32_t& outNodeIndex)
	{
		Node& node = m_Nodes[nodeIndex];
		if (node.state == NodeState::Allocated || node.size < resolution)
			return false;

		if (node.size == resolution)
		{
			if (node.state != NodeState::Free)
				return false;
			node.state = NodeState::Allocated;
			outNodeIndex = nodeIndex;
			return true;
		}

		if (node.children[0] == VANS_INVALID_SHADOW_INDEX)
			return false;

		if (node.state == NodeState::Free)
			node.state = NodeState::Split;

		for (uint32_t childIndex : node.children)
		{
			if (AllocateFromNode(childIndex, resolution, outNodeIndex))
				return true;
		}
		return false;
	}

	void VansShadowAtlasAllocator::TryCoalesce(uint32_t nodeIndex)
	{
		uint32_t parentIndex = m_Nodes[nodeIndex].parent;
		while (parentIndex != VANS_INVALID_SHADOW_INDEX)
		{
			Node& parent = m_Nodes[parentIndex];
			bool allFree = true;
			for (uint32_t childIndex : parent.children)
			{
				if (childIndex == VANS_INVALID_SHADOW_INDEX || m_Nodes[childIndex].state != NodeState::Free)
				{
					allFree = false;
					break;
				}
			}
			if (!allFree)
				break;
			parent.state = NodeState::Free;
			parentIndex = parent.parent;
		}
	}

	bool VansShadowAtlasAllocator::IsPowerOfTwo(uint32_t value)
	{
		return value != 0 && (value & (value - 1u)) == 0;
	}
}
