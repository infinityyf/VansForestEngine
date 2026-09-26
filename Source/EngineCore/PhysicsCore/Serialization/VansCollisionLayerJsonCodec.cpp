#include "VansCollisionLayerJsonCodec.h"

#include "../VansCollisionLayerConfig.h"

#include <nlohmann/json.hpp>

#include <unordered_set>

namespace VansEngine
{
	bool VansCollisionLayerJsonCodec::Decode(
		const nlohmann::json& root,
		VansCollisionLayerConfig& config,
		std::string& error)
	{
		error.clear();
		config.ResetToDefaults();
		if (!root.is_object())
		{
			error = "Collision layer settings root must be an object";
			return false;
		}

		if (!root.contains("layers") || !root["layers"].is_array())
		{
			error = "Collision layer settings require a layers array";
			return false;
		}
		config.layerNames.fill({});
		config.collisionMasks.fill(0u);
		config.layerCount = 0;
		std::unordered_set<std::string> layerNames;
		for (const auto& layerJson : root["layers"])
		{
			if (!layerJson.is_object())
			{
				error = "Collision layer entry must be an object";
				return false;
			}

			const int index = layerJson.value("index", -1);
			const std::string name = layerJson.value("name", "");
			if (index < 0 || index >= MAX_PHYSICS_LAYERS || name.empty())
			{
				error = "Collision layer entry has an invalid index or empty name";
				return false;
			}
			if (!config.layerNames[index].empty())
			{
				error = "Collision layer index is duplicated";
				return false;
			}
			if (!layerNames.insert(name).second)
			{
				error = "Collision layer name is duplicated: '" + name + "'";
				return false;
			}

			config.layerNames[index] = name;
			if (index + 1 > config.layerCount)
				config.layerCount = index + 1;
		}
		if (config.layerNames[0] != "Default")
		{
			error = "Collision layer index 0 must be named 'Default'";
			return false;
		}

		if (!root.contains("collisionMatrix") || !root["collisionMatrix"].is_object())
		{
			error = "Collision layer settings require a collisionMatrix object";
			return false;
		}
		for (const auto& item : root["collisionMatrix"].items())
		{
			int sourceIndex = -1;
			if (!config.TryGetLayerIndex(item.key(), sourceIndex))
			{
				error = "Collision matrix references unknown source layer '" + item.key() + "'";
				return false;
			}
			const nlohmann::json& targets = item.value();
			if (!targets.is_array())
			{
				error = "Collision matrix entry must be an array";
				return false;
			}

			std::unordered_set<std::string> targetNames;
			for (const auto& targetName : targets)
			{
				if (!targetName.is_string())
				{
					error = "Collision matrix target must be a layer name";
					return false;
				}

				const std::string target = targetName.get<std::string>();
				int targetIndex = -1;
				if (!config.TryGetLayerIndex(target, targetIndex))
				{
					error = "Collision matrix references unknown target layer '" + target + "'";
					return false;
				}
				if (!targetNames.insert(target).second)
				{
					error = "Collision matrix target is duplicated for layer '" + item.key() + "': '" + target + "'";
					return false;
				}
				config.collisionMasks[sourceIndex] |= (1u << targetIndex);
			}
		}

		for (int sourceIndex = 0; sourceIndex < config.layerCount; ++sourceIndex)
		{
			if (config.layerNames[sourceIndex].empty())
				continue;
			if (!root["collisionMatrix"].contains(config.layerNames[sourceIndex]))
			{
				error = "Collision matrix is missing layer '" + config.layerNames[sourceIndex] + "'";
				return false;
			}
			for (int targetIndex = sourceIndex; targetIndex < config.layerCount; ++targetIndex)
			{
				if (config.layerNames[targetIndex].empty())
					continue;
				const bool sourceToTarget =
					(config.collisionMasks[sourceIndex] & (1u << targetIndex)) != 0u;
				const bool targetToSource =
					(config.collisionMasks[targetIndex] & (1u << sourceIndex)) != 0u;
				if (sourceToTarget != targetToSource)
				{
					error = "Collision matrix must be symmetric between layers '" +
						config.layerNames[sourceIndex] + "' and '" +
						config.layerNames[targetIndex] + "'";
					return false;
				}
			}
		}

		return true;
	}

	nlohmann::json VansCollisionLayerJsonCodec::Encode(const VansCollisionLayerConfig& config)
	{
		nlohmann::json layers = nlohmann::json::array();
		nlohmann::json matrix = nlohmann::json::object();
		for (int index = 0; index < config.layerCount; ++index)
		{
			if (config.layerNames[index].empty()) continue;
			layers.push_back({ { "index", index }, { "name", config.layerNames[index] } });
			nlohmann::json targets = nlohmann::json::array();
			for (int target = 0; target < config.layerCount; ++target)
			{
				if (!config.layerNames[target].empty() &&
					(config.collisionMasks[index] & (1u << target)) != 0)
					targets.push_back(config.layerNames[target]);
			}
			matrix[config.layerNames[index]] = std::move(targets);
		}
		return { { "layers", std::move(layers) }, { "collisionMatrix", std::move(matrix) } };
	}
}
