#pragma once

#include "../RuntimeCore/VansGenerationPool.h"
#include "VansTimelineEvaluation.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Vans
{
struct VansTimelineWriterDesc
{
	VansTimelineSessionHandle session;
	VansTimelineSessionHandle root;
	VansTimelineWriterHandle parent;
	VansTimelineOutputTypeId outputType;
	std::uint32_t applierSlot = UINT32_MAX;
	std::uint32_t trackIndex = UINT32_MAX;
	std::uint32_t sectionIndex = UINT32_MAX;
	VansTimelineCompletionMode completion = VansTimelineCompletionMode::RestoreState;
	std::int32_t priority = 0;
	std::int32_t hierarchicalBias = 0;
	std::uint64_t sequence = 0;
	std::string debugLabel;
};

class VansTimelineWriterRegistry
{
public:
	VansTimelineWriterHandle Acquire(const VansTimelineWriterDesc& desc);
	VansTimelineWriterHandle Find(
		VansTimelineSessionHandle session,
		std::uint32_t trackIndex,
		std::uint32_t sectionIndex,
		VansTimelineOutputTypeId outputType) const;
	const VansTimelineWriterDesc* Resolve(VansTimelineWriterHandle handle) const;
	bool Release(VansTimelineWriterHandle handle);
	std::vector<VansTimelineWriterHandle> ReleaseSession(VansTimelineSessionHandle session);
	std::vector<VansTimelineWriterHandle> ReleaseRoot(VansTimelineSessionHandle root);
	std::size_t ActiveCount() const { return m_Writers.ActiveCount(); }

private:
	struct Key
	{
		VansTimelineSessionHandle session;
		std::uint32_t trackIndex = UINT32_MAX;
		std::uint32_t sectionIndex = UINT32_MAX;
		VansTimelineOutputTypeId outputType;
		friend bool operator==(const Key& left, const Key& right)
		{
			return left.session == right.session && left.trackIndex == right.trackIndex &&
				left.sectionIndex == right.sectionIndex && left.outputType == right.outputType;
		}
	};
	struct KeyHash
	{
		std::size_t operator()(const Key& key) const noexcept
		{
			return (static_cast<std::size_t>(key.session.index) << 1) ^
				(static_cast<std::size_t>(key.session.generation) << 17) ^
				(static_cast<std::size_t>(key.trackIndex) << 3) ^
				(static_cast<std::size_t>(key.sectionIndex) << 7) ^
				std::hash<VansTimelineOutputTypeId>{}(key.outputType);
		}
	};
	VansGenerationPool<VansTimelineWriterDesc> m_Writers;
	std::unordered_map<Key, VansTimelineWriterHandle, KeyHash> m_ByKey;
};
}
