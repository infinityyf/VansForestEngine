#include "VansCollisionLayerJsonCodec.h"

#include "../VansCollisionLayerConfig.h"

#include <nlohmann/json.hpp>

namespace VansEngine
{
	bool VansCollisionLayerJsonCodec::Decode(
		const nlohmann::json& root,
		VansCollisionLayerConfig& config)
	{
		config.ResetToDefaults();
		if (!root.is_object())
			return true;

		if (root.contains("layers") && root["layers"].is_array())
		{
			config.layerCount = 0;
			for (const auto& layerJson : root["layers"])
			{
				if (!layerJson.is_object())
					continue;

				const int index = layerJson.value("index", -1);
				const std::string name = layerJson.value("name", "");
				if (index < 0 || index >= MAX_PHYSICS_LAYERS || name.empty())
					continue;

				config.layerNames[index] = name;
				if (index + 1 > config.layerCount)
					config.layerCount = index + 1;
			}
		}

		if (root.contains("collisionMatrix") && root["collisionMatrix"].is_object())
		{
			for (int index = 0; index < MAX_PHYSICS_LAYERS; ++index)
				config.collisionMasks[index] = 0;

			for (const auto& item : root["collisionMatrix"].items())
			{
				const int sourceIndex = config.GetLayerIndex(item.key());
				const nlohmann::json& targets = item.value();
				if (!targets.is_array())
					continue;

				for (const auto& targetName : targets)
				{
					if (!targetName.is_string())
						continue;

					const int targetIndex = config.GetLayerIndex(targetName.get<std::string>());
					if (targetIndex >= 0)
						config.collisionMasks[sourceIndex] |= (1u << targetIndex);
				}
			}
		}

		return true;
	}
}
