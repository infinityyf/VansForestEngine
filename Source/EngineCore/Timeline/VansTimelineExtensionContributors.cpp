#include "VansTimelineExtensionContributors.h"

#include "../TimelineCore/VansTimelineTrackExtensionRegistry.h"

namespace Vans
{
bool VansTimelineExtensionContributors::Register(
	std::string stableName,
	VansTimelineExtensionContributorFn contributor,
	std::string& error)
{
	error.clear();
	if (m_Sealed)
	{
		error = "Timeline.ExtensionContributorsSealed";
		return false;
	}
	if (stableName.empty() || !contributor)
	{
		error = "Timeline.ExtensionContributorInvalid";
		return false;
	}
	if (!m_Names.insert(stableName).second)
	{
		error = "Timeline.ExtensionContributorDuplicate: " + stableName;
		return false;
	}
	m_Entries.push_back({ std::move(stableName), contributor });
	return true;
}

bool VansTimelineExtensionContributors::ApplyAndSeal(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	error.clear();
	if (m_Sealed)
	{
		error = "Timeline.ExtensionContributorsSealed";
		return false;
	}
	m_Sealed = true;
	for (const Entry& entry : m_Entries)
		if (!entry.contributor(registry, error))
		{
			if (error.empty()) error = "Timeline.ExtensionContributorFailed: " + entry.stableName;
			return false;
		}
	if (!registry.Seal(error)) return false;
	return true;
}

VansTimelineExtensionContributors& VansTimelineExtensionContributors::Startup()
{
	static VansTimelineExtensionContributors contributors;
	return contributors;
}

bool VansRegisterTimelineExtensionContributor(
	std::string stableName,
	VansTimelineExtensionContributorFn contributor,
	std::string& error)
{
	return VansTimelineExtensionContributors::Startup().Register(
		std::move(stableName), contributor, error);
}
}
