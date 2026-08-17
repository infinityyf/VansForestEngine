#pragma once

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Vans
{
class VansTimelineTrackExtensionRegistry;

using VansTimelineExtensionContributorFn = bool(*)(
	VansTimelineTrackExtensionRegistry&,
	std::string&);

class VansTimelineExtensionContributors
{
public:
	bool Register(
		std::string stableName,
		VansTimelineExtensionContributorFn contributor,
		std::string& error);
	bool ApplyAndSeal(
		VansTimelineTrackExtensionRegistry& registry,
		std::string& error);
	bool IsSealed() const { return m_Sealed; }
	static VansTimelineExtensionContributors& Startup();

private:
	struct Entry
	{
		std::string stableName;
		VansTimelineExtensionContributorFn contributor = nullptr;
	};
	bool m_Sealed = false;
	std::vector<Entry> m_Entries;
	std::unordered_set<std::string> m_Names;
};

bool VansRegisterTimelineExtensionContributor(
	std::string stableName,
	VansTimelineExtensionContributorFn contributor,
	std::string& error);
}
