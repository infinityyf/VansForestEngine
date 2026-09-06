#include "VansCollisionLayerJsonCodec.h"

#include "../VansCollisionLayerConfig.h"

#include <nlohmann/json.hpp>

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
		config.layerCount = 0;
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

			config.layerNames[index] = name;
			if (index + 1 > config.layerCount)
				config.layerCount = index + 1;
		}

		if (!root.contains("collisionMatrix") || !root["collisionMatrix"].is_object())
		{
			error = "Collision layer settings require a collisionMatrix object";
			return false;
		}
		for (int index = 0; index < MAX_PHYSICS_LAYERS; ++index)
			config.collisionMasks[index] = 0;

		for (const auto& item : root["collisionMatrix"].items())
		{
			const int sourceIndex = config.GetLayerIndex(item.key());
			if (config.layerNames[sourceIndex] != item.key())
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

			for (const auto& targetName : targets)
			{
				if (!targetName.is_string())
				{
					error = "Collision matrix target must be a layer name";
					return false;
				}

				const std::string target = targetName.get<std::string>();
				const int targetIndex = config.GetLayerIndex(target);
				if (config.layerNames[targetIndex] != target)
				{
					error = "Collision matrix references unknown target layer '" + target + "'";
					return false;
				}
				config.collisionMasks[sourceIndex] |= (1u << targetIndex);
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
