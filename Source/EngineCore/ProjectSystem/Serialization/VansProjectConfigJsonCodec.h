#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace Vans
{
	struct RecentProjectEntry;
	struct VansProjectConfig;

	class VansProjectConfigJsonCodec
	{
	public:
		static bool DecodeProjectConfig(
			const nlohmann::json& root,
			VansProjectConfig& config,
			std::string& error);

		static nlohmann::json EncodeProjectConfig(const VansProjectConfig& config);

		static bool DecodeRecentProjects(
			const nlohmann::json& root,
			std::vector<RecentProjectEntry>& entries,
			std::string& error);

		static nlohmann::json EncodeRecentProjects(
			const std::vector<RecentProjectEntry>& entries,
			int maxRecentCount);
	};
}
