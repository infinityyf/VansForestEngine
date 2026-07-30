#include "VansProjectConfigLegacyJsonCodec.h"

#include "../VansProjectConfig.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace Vans
{
	bool VansProjectConfigLegacyJsonCodec::DecodeProjectConfig(
		const nlohmann::json& root,
		VansProjectConfig& config,
		std::string& error)
	{
		error.clear();
		try
		{
			config.projectName = root.value("projectName", "");
			config.engineVersion = root.value("engineVersion", "0.1.0");
			config.createdAt = root.value("createdAt", "");
			config.defaultScene = root.value("defaultScene", "Scenes/MainScene.json");

			config.assetsRoot = "Assets";
			config.importedArtifactRoot = "Library/Artifacts";
			config.metaExtension = ".meta";
			config.runtimeAssetBindings.clear();
			if (root.contains("assetDatabase") && root["assetDatabase"].is_object())
			{
				const nlohmann::json& database = root["assetDatabase"];
				config.assetsRoot = database.value("assetsRoot", "Assets");
				config.importedArtifactRoot = database.value("importedArtifactRoot", "Library/Artifacts");
				config.metaExtension = database.value("metaExtension", ".meta");
			}

			if (root.contains("runtimeAssetBindings") && root["runtimeAssetBindings"].is_object())
			{
				for (const auto& item : root["runtimeAssetBindings"].items())
				{
					if (item.value().is_string())
						config.runtimeAssetBindings[item.key()] = item.value().get<std::string>();
				}
			}

			config.renderSettings = root.value("renderSettings", "ProjectSettings/RenderSettings.json");
			config.physicsSettings = root.value("physicsSettings", "ProjectSettings/PhysicsSettings.json");
			config.collisionLayerSettings = root.value("collisionLayerSettings", "ProjectSettings/PhysicsLayers.json");

			config.assetDirectories.clear();
			if (root.contains("assetDirectories") && root["assetDirectories"].is_object())
			{
				for (const auto& item : root["assetDirectories"].items())
					config.assetDirectories[item.key()] = item.value().get<std::string>();
			}

			config.scriptSearchPaths.clear();
			if (root.contains("scriptSearchPaths") && root["scriptSearchPaths"].is_array())
			{
				for (const auto& value : root["scriptSearchPaths"])
					config.scriptSearchPaths.push_back(value.get<std::string>());
			}
		}
		catch (const nlohmann::json::exception& exception)
		{
			error = std::string("JSON parse error: ") + exception.what();
			return false;
		}

		return true;
	}

	nlohmann::json VansProjectConfigLegacyJsonCodec::EncodeProjectConfig(const VansProjectConfig& config)
	{
		nlohmann::json root;
		root["projectName"] = config.projectName;
		root["engineVersion"] = config.engineVersion;
		root["createdAt"] = config.createdAt;
		root["defaultScene"] = config.defaultScene;
		root["assetDatabase"] = {
			{ "assetsRoot", config.assetsRoot },
			{ "importedArtifactRoot", config.importedArtifactRoot },
			{ "metaExtension", config.metaExtension }
		};
		root["runtimeAssetBindings"] = config.runtimeAssetBindings;
		root["renderSettings"] = config.renderSettings;
		root["physicsSettings"] = config.physicsSettings;
		root["collisionLayerSettings"] = config.collisionLayerSettings;

		nlohmann::json directories = nlohmann::json::object();
		for (const auto& item : config.assetDirectories)
			directories[item.first] = item.second;
		root["assetDirectories"] = directories;

		root["scriptSearchPaths"] = config.scriptSearchPaths;
		return root;
	}

	bool VansProjectConfigLegacyJsonCodec::DecodeRecentProjects(
		const nlohmann::json& root,
		std::vector<RecentProjectEntry>& entries,
		std::string& error)
	{
		error.clear();
		entries.clear();
		try
		{
			if (root.contains("recentProjects") && root["recentProjects"].is_array())
			{
				for (const auto& item : root["recentProjects"])
				{
					RecentProjectEntry entry;
					entry.name = item.value("name", "");
					entry.path = item.value("path", "");
					entry.lastOpened = item.value("lastOpened", "");
					entry.engineVersion = item.value("engineVersion", "");
					entries.push_back(std::move(entry));
				}
			}
		}
		catch (const nlohmann::json::exception& exception)
		{
			error = std::string("JSON parse error: ") + exception.what();
			return false;
		}
		return true;
	}

	nlohmann::json VansProjectConfigLegacyJsonCodec::EncodeRecentProjects(
		const std::vector<RecentProjectEntry>& entries,
		int maxRecentCount)
	{
		nlohmann::json array = nlohmann::json::array();
		for (const RecentProjectEntry& entry : entries)
		{
			nlohmann::json item;
			item["name"] = entry.name;
			item["path"] = entry.path;
			item["lastOpened"] = entry.lastOpened;
			item["engineVersion"] = entry.engineVersion;
			array.push_back(std::move(item));
		}

		nlohmann::json root;
		root["recentProjects"] = std::move(array);
		root["maxRecentCount"] = maxRecentCount;
		return root;
	}
}
