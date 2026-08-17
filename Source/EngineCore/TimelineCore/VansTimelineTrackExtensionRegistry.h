#pragma once

#include "VansTimelineTrackExtension.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
class VansTimelineTrackExtensionRegistry
{
public:
	bool Register(VansTimelineTrackExtensionDescriptor descriptor, std::string& error);
	bool Seal(std::string& error);
	bool IsSealed() const { return m_Sealed; }
	const VansTimelineTrackExtensionDescriptor* Resolve(VansTimelineTrackTypeId typeId) const;
	const VansTimelineTrackExtensionDescriptor* Resolve(std::string_view stableName) const;
	VansTimelineRegistrySlot SlotOf(VansTimelineTrackTypeId typeId) const;
	const VansTimelineTrackExtensionDescriptor* At(VansTimelineRegistrySlot slot) const;
	const std::vector<VansTimelineTrackExtensionDescriptor>& All() const { return m_Descriptors; }
	std::uint64_t ManifestHash() const;
	static VansTimelineTrackExtensionRegistry& BuiltIns();

private:
	bool m_Sealed = false;
	std::vector<VansTimelineTrackExtensionDescriptor> m_Descriptors;
	std::unordered_map<VansTimelineTrackTypeId, VansTimelineRegistrySlot> m_ById;
	std::unordered_map<std::string, VansTimelineRegistrySlot> m_ByName;
};
}
