#pragma once

#include "VansTransform.h"

#include <cstdint>
#include <queue>
#include <unordered_set>
#include <vector>

namespace Vans
{
class VansTransformStore final
{
public:
	static std::uint32_t Allocate();
	static void Release(std::uint32_t id);

	static const VansTransform& Read(std::uint32_t id);
	static void Write(std::uint32_t id, const VansTransform& transform);

	static void MarkDirty(std::uint32_t id);
	static bool IsDirty(std::uint32_t id);
	static void ClearDirty();

	static std::uint32_t GetGeneration(std::uint32_t id);
	static std::uint64_t GetRevision();
	static std::uint64_t GetTransformRevision(std::uint32_t id);
	static bool IsAllocated(std::uint32_t id);

private:
	static std::uint64_t AdvanceRevision();

	static std::vector<VansTransform> m_Transforms;
	static std::vector<std::uint32_t> m_Generations;
	static std::vector<std::uint64_t> m_TransformRevisions;
	static std::vector<std::uint8_t> m_Allocated;
	static std::vector<std::uint8_t> m_Dirty;
	static std::vector<std::uint32_t> m_DirtyIds;
	static std::queue<std::uint32_t> m_FreeIds;
	static std::uint64_t m_Revision;

#ifdef _DEBUG
	static std::unordered_set<std::uint32_t> m_AllocatedIds;
#endif
};
}
