#include "VansTimelineTrackExtensionRegistry.h"

#include <algorithm>

namespace Vans
{
bool VansTimelineTrackExtensionRegistry::Register(
	VansTimelineTrackExtensionDescriptor descriptor,
	std::string& error)
{
	error.clear();
	if (m_Sealed) { error = "Timeline.TrackRegistrySealed"; return false; }
	if (descriptor.stableName.empty()) { error = "Timeline.TrackStableNameMissing"; return false; }
	const VansTimelineTrackTypeId expected = VansMakeStableId<VansTimelineTrackTypeTag>(descriptor.stableName);
	if (!descriptor.typeId) descriptor.typeId = expected;
	if (descriptor.typeId != expected) { error = "Timeline.TrackTypeHashMismatch: " + descriptor.stableName; return false; }
	if (m_ByName.find(descriptor.stableName) != m_ByName.end())
	{
		error = "Timeline.TrackTypeDuplicateName: " + descriptor.stableName;
		return false;
	}
	const auto collision = m_ById.find(descriptor.typeId);
	if (collision != m_ById.end())
	{
		error = "Timeline.TrackTypeCollision: " + descriptor.stableName + " collides with " +
			m_Descriptors[collision->second].stableName;
		return false;
	}
	const VansTimelineRegistrySlot slot = static_cast<VansTimelineRegistrySlot>(m_Descriptors.size());
	m_ById.emplace(descriptor.typeId, slot);
	m_ByName.emplace(descriptor.stableName, slot);
	m_Descriptors.push_back(std::move(descriptor));
	return true;
}

bool VansTimelineTrackExtensionRegistry::Seal(std::string& error)
{
	error.clear();
	if (m_Sealed) return true;
	for (const VansTimelineTrackExtensionDescriptor& descriptor : m_Descriptors)
	{
		if (!descriptor.compile || !descriptor.evaluate)
		{
			error = "Timeline.TrackExtensionIncomplete: " + descriptor.stableName;
			return false;
		}
	}
	m_Sealed = true;
	return true;
}

const VansTimelineTrackExtensionDescriptor* VansTimelineTrackExtensionRegistry::Resolve(
	VansTimelineTrackTypeId typeId) const
{
	const auto found = m_ById.find(typeId);
	return found == m_ById.end() ? nullptr : &m_Descriptors[found->second];
}

const VansTimelineTrackExtensionDescriptor* VansTimelineTrackExtensionRegistry::Resolve(
	std::string_view stableName) const
{
	const auto found = m_ByName.find(std::string(stableName));
	return found == m_ByName.end() ? nullptr : &m_Descriptors[found->second];
}

VansTimelineRegistrySlot VansTimelineTrackExtensionRegistry::SlotOf(VansTimelineTrackTypeId typeId) const
{
	const auto found = m_ById.find(typeId);
	return found == m_ById.end() ? VansInvalidTimelineRegistrySlot : found->second;
}

const VansTimelineTrackExtensionDescriptor* VansTimelineTrackExtensionRegistry::At(
	VansTimelineRegistrySlot slot) const
{
	return slot < m_Descriptors.size() ? &m_Descriptors[slot] : nullptr;
}

std::uint64_t VansTimelineTrackExtensionRegistry::ManifestHash() const
{
	std::vector<const VansTimelineTrackExtensionDescriptor*> ordered;
	ordered.reserve(m_Descriptors.size());
	for (const auto& descriptor : m_Descriptors) ordered.push_back(&descriptor);
	std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right)
	{
		return left->stableName < right->stableName;
	});
	std::uint64_t hash = 14695981039346656037ull;
	for (const auto* descriptor : ordered)
	{
		hash ^= VansStableHash64(descriptor->stableName);
		hash *= 1099511628211ull;
	}
	return hash;
}

}
