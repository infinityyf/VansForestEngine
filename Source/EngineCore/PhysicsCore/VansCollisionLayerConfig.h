#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace VansEngine
{
	static constexpr int MAX_PHYSICS_LAYERS = 32;

	struct VansCollisionLayerConfig
	{
		std::array<std::string, MAX_PHYSICS_LAYERS> layerNames;
		std::array<uint32_t, MAX_PHYSICS_LAYERS> collisionMasks;
		int layerCount = 1;

		void ResetToDefaults()
		{
			layerNames.fill({});
			collisionMasks.fill(0u);
			layerNames[0] = "Default";
			collisionMasks[0] = 1u;
			layerCount = 1;
		}

		bool TryGetLayerIndex(const std::string& name, int& layerIndex) const
		{
			for (int index = 0; index < layerCount; ++index)
			{
				if (layerNames[index] == name)
				{
					layerIndex = index;
					return true;
				}
			}
			layerIndex = -1;
			return false;
		}
	};
}
