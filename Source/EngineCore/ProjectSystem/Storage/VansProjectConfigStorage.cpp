#include "VansProjectConfigStorage.h"

#include "../Serialization/VansProjectConfigLegacyJsonCodec.h"
#include "../VansProjectConfig.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace Vans
{
	bool VansProjectConfigStorage::LoadProjectConfig(
		const std::string& filePath,
		VansProjectConfig& config,
		std::string& error)
	{
		nlohmann::json root;
		std::string readError;
		if (!VansJsonFileStorage::Read(filePath, root, readError))
		{
			error = "Cannot read: " + filePath + " (" + readError + ")";
			return false;
		}
		return VansProjectConfigLegacyJsonCodec::DecodeProjectConfig(root, config, error);
	}

	bool VansProjectConfigStorage::SaveProjectConfig(
		const std::string& filePath,
		const VansProjectConfig& config,
		std::string& error)
	{
		const nlohmann::json root = VansProjectConfigLegacyJsonCodec::EncodeProjectConfig(config);
		std::string writeError;
		if (!VansJsonFileStorage::WriteAtomic(filePath, root, writeError))
		{
			error = "Cannot write: " + filePath + " (" + writeError + ")";
			return false;
		}
		return true;
	}

	bool VansProjectConfigStorage::LoadRecentProjects(
		const std::string& filePath,
		std::vector<RecentProjectEntry>& entries,
		std::string& error)
	{
		nlohmann::json root;
		if (!VansJsonFileStorage::Read(filePath, root, error))
			return false;
		return VansProjectConfigLegacyJsonCodec::DecodeRecentProjects(root, entries, error);
	}

	bool VansProjectConfigStorage::SaveRecentProjects(
		const std::string& filePath,
		const std::vector<RecentProjectEntry>& entries,
		int maxRecentCount,
		std::string& error)
	{
		const nlohmann::json root =
			VansProjectConfigLegacyJsonCodec::EncodeRecentProjects(entries, maxRecentCount);
		std::string writeError;
		if (!VansJsonFileStorage::WriteAtomic(filePath, root, writeError))
		{
			error = "Cannot write recent projects: " + filePath + " (" + writeError + ")";
			return false;
		}
		return true;
	}
}
