#pragma once

#include "VansPunctualShadowTypes.h"

#include <array>
#include <cstdint>
#include <vector>

namespace VansGraphics
{
	class VansShadowAtlasAllocator
	{
	public:
		VansShadowAtlasAllocator(uint32_t atlasSize = 4096, uint32_t basePageSize = 128, uint32_t gutter = 2);

		void Reset(uint32_t atlasSize, uint32_t basePageSize, uint32_t gutter);
		bool Allocate(uint32_t resolution, VansShadowAtlasBlock& outBlock);
		bool AllocateGroup(uint32_t resolution, uint32_t viewCount, std::array<VansShadowAtlasBlock, 6>& outBlocks);
		bool Free(const VansShadowAtlasBlock& block);
		bool Validate(const VansShadowAtlasBlock& block) const;

		uint32_t GetAtlasSize() const { return m_AtlasSize; }
		uint32_t GetBasePageSize() const { return m_BasePageSize; }
		uint32_t GetGutter() const { return m_Gutter; }
		uint32_t GetTotalPages() const { return m_TotalPages; }
		uint32_t GetUsedPages() const { return m_UsedPages; }

	private:
		enum class NodeState : uint8_t
		{
			Free,
			Split,
			Allocated
		};

		struct Node
		{
			NodeState state = NodeState::Free;
			uint32_t parent = VANS_INVALID_SHADOW_INDEX;
			std::array<uint32_t, 4> children = {
				VANS_INVALID_SHADOW_INDEX,
				VANS_INVALID_SHADOW_INDEX,
				VANS_INVALID_SHADOW_INDEX,
				VANS_INVALID_SHADOW_INDEX
			};
			uint32_t x = 0;
			uint32_t y = 0;
			uint32_t size = 0;
			uint16_t generation = 0;
		};

		uint32_t BuildNode(uint32_t parent, uint32_t x, uint32_t y, uint32_t size);
		bool AllocateFromNode(uint32_t nodeIndex, uint32_t resolution, uint32_t& outNodeIndex);
		void TryCoalesce(uint32_t nodeIndex);
		static bool IsPowerOfTwo(uint32_t value);

		uint32_t m_AtlasSize = 4096;
		uint32_t m_BasePageSize = 128;
		uint32_t m_Gutter = 2;
		uint32_t m_TotalPages = 1024;
		uint32_t m_UsedPages = 0;
		std::vector<Node> m_Nodes;
	};
}
