#include "VansTimelinePropertyAccessRegistry.h"

#include <algorithm>

namespace Vans
{
bool VansTimelinePropertyAccessRegistry::Register(
	VansTimelinePropertyAccessDescriptor descriptor,
	std::string& error)
{
	error.clear();
	if (m_Sealed) { error = "Timeline.PropertyAccessRegistrySealed"; return false; }
	if (!descriptor.id || descriptor.stableName.empty() || descriptor.componentTypeId == 0 ||
		descriptor.valueType == VansTimelineValueType::Null || !descriptor.read || !descriptor.write ||
		descriptor.id != VansMakeStableId<VansTimelinePropertyAccessTag>(descriptor.stableName))
	{ error = "Timeline.PropertyAccessRegistrationInvalid"; return false; }
	if (m_ById.find(descriptor.id) != m_ById.end())
	{ error = "Timeline.PropertyAccessDuplicate"; return false; }
	const std::size_t slot = m_Descriptors.size();
	m_ById.emplace(descriptor.id, slot); m_Descriptors.push_back(std::move(descriptor));
	return true;
}

bool VansTimelinePropertyAccessRegistry::Seal(std::string& error)
{
	error.clear();
	std::sort(m_Descriptors.begin(), m_Descriptors.end(), [](const auto& left, const auto& right)
	{ return left.stableName < right.stableName; });
	m_ById.clear();
	for (std::size_t index = 0; index < m_Descriptors.size(); ++index)
		m_ById.emplace(m_Descriptors[index].id, index);
	m_Sealed = true; return true;
}

const VansTimelinePropertyAccessDescriptor* VansTimelinePropertyAccessRegistry::Resolve(
	VansStableId<VansTimelinePropertyAccessTag> id) const
{
	const auto found = m_ById.find(id);
	return found == m_ById.end() ? nullptr : &m_Descriptors[found->second];
}

const VansTimelinePropertyAccessDescriptor* VansTimelinePropertyAccessRegistry::Resolve(
	std::string_view stableName) const
{
	const auto* descriptor = Resolve(VansMakeStableId<VansTimelinePropertyAccessTag>(stableName));
	return descriptor && descriptor->stableName == stableName ? descriptor : nullptr;
}

std::uint64_t VansTimelinePropertyAccessRegistry::ManifestHash() const
{
	std::uint64_t hash = VansStableHash64("Timeline.PropertyAccessManifest");
	for (const auto& descriptor : m_Descriptors)
	{
		hash ^= descriptor.id.value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
		hash ^= static_cast<std::uint64_t>(descriptor.valueType) +
			(static_cast<std::uint64_t>(descriptor.componentTypeId) << 32);
	}
	return hash;
}

const VansTimelinePropertyAccessRegistry& VansTimelinePropertyAccessRegistry::BuiltIns()
{
	static const VansTimelinePropertyAccessRegistry registry = []
	{
		VansTimelinePropertyAccessRegistry value; std::string error;
		if (!VansRegisterSceneTimelinePropertyAccessors(value, error) ||
			!VansRegisterAudioTimelinePropertyAccessors(value, error) ||
			!VansGraphics::VansRegisterRenderTimelinePropertyAccessors(value, error) ||
			!value.Seal(error))
			return VansTimelinePropertyAccessRegistry{};
		return value;
	}();
	return registry;
}
}
