#include "VansTimelineEvaluation.h"

#include <new>

namespace Vans
{
namespace
{
constexpr std::size_t InitialArenaBytes = 64u * 1024u;
}

VansTimelineOutputArena::VansTimelineOutputArena()
{
	Block block;
	block.bytes = std::make_unique<std::byte[]>(InitialArenaBytes);
	block.capacity = InitialArenaBytes;
	m_Blocks.push_back(std::move(block));
}

void VansTimelineOutputArena::Reset()
{
	m_HighWater = std::max(m_HighWater, m_FrameBytes);
	m_FrameBytes = 0;
	m_CurrentBlock = 0;
	for (Block& block : m_Blocks) block.used = 0;
}

std::byte* VansTimelineOutputArena::Allocate(std::size_t size, std::size_t alignment)
{
	for (;;)
	{
		Block& block = m_Blocks[m_CurrentBlock];
		const std::size_t aligned = (block.used + alignment - 1) & ~(alignment - 1);
		if (aligned + size <= block.capacity)
		{
			block.used = aligned + size;
			m_FrameBytes += size;
			return block.bytes.get() + aligned;
		}
		++m_CurrentBlock;
		if (m_CurrentBlock < m_Blocks.size()) continue;
		Block next;
		next.capacity = std::max(InitialArenaBytes, size + alignment);
		next.bytes = std::make_unique<std::byte[]>(next.capacity);
		m_Blocks.push_back(std::move(next));
	}
}
}
