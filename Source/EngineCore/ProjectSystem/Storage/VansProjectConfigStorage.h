#pragma once

#include <string>
#include <vector>

namespace Vans
{
	struct RecentProjectEntry;
	struct VansProjectConfig;

	class VansProjectConfigStorage
	{
	public:
		static bool LoadProjectConfig(
			const std::string& filePath,
			VansProjectConfig& config,
			std::string& error);

		static bool SaveProjectConfig(
			const std::string& filePath,
			const VansProjectConfig& config,
			std::string& error);

		static bool LoadRecentProjects(
			const std::string& filePath,
			std::vector<RecentProjectEntry>& entries,
			std::string& error);

		static bool SaveRecentProjects(
			const std::string& filePath,
			const std::vector<RecentProjectEntry>& entries,
			int maxRecentCount,
			std::string& error);
	};
}
