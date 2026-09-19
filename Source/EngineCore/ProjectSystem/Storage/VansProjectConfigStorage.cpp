#include "VansProjectConfigStorage.h"

#include "../Serialization/VansProjectConfigJsonCodec.h"
#include "../VansProjectConfig.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>

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
		return VansProjectConfigJsonCodec::DecodeProjectConfig(root, config, error);
	}

	bool VansProjectConfigStorage::SaveProjectConfig(
		const std::string& filePath,
		const VansProjectConfig& config,
		std::string& error)
	{
		const nlohmann::json root = VansProjectConfigJsonCodec::EncodeProjectConfig(config);
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
		if (!VansProjectConfigJsonCodec::DecodeRecentProjects(root, entries, error))
		{
			entries.clear(); // 不用部分解析结果覆盖损坏的偏好文件。
			return false;
		}
		const auto oldSize = entries.size();
		entries.erase(std::remove_if(entries.begin(), entries.end(),
			[](const RecentProjectEntry& entry)
			{
				if (entry.path.empty()) return true;
				std::error_code statusError;
				const auto status = std::filesystem::status(
					std::filesystem::u8path(entry.path) / "ForestProject.json", statusError);
				// 权限/临时 I/O 失败不能当作已删除；只移除已确认不存在的项目。
				if (statusError)
					return statusError == std::errc::no_such_file_or_directory ||
						statusError == std::errc::not_a_directory;
				return !std::filesystem::is_regular_file(status);
			}), entries.end());
		if (entries.size() != oldSize)
		{
			// 持久清理，不只在 UI 隐藏。其余偏好字段和条目顺序保持不变。
			root["recentProjects"] = VansProjectConfigJsonCodec::EncodeRecentProjects(entries, 20)["recentProjects"];
			if (!VansJsonFileStorage::WriteAtomic(filePath, root, error))
				return false;
		}
		return true;
	}

	bool VansProjectConfigStorage::SaveRecentProjects(
		const std::string& filePath,
		const std::vector<RecentProjectEntry>& entries,
		int maxRecentCount,
		std::string& error)
	{
		const nlohmann::json root =
			VansProjectConfigJsonCodec::EncodeRecentProjects(entries, maxRecentCount);
		std::string writeError;
		if (!VansJsonFileStorage::WriteAtomic(filePath, root, writeError))
		{
			error = "Cannot write recent projects: " + filePath + " (" + writeError + ")";
			return false;
		}
		return true;
	}
}
