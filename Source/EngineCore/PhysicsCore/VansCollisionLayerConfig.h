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
			for (int index = 0; index < MAX_PHYSICS_LAYERS; ++index)
			{
				layerNames[index].clear();
				collisionMasks[index] = 0xFFFFFFFF;
			}
			layerNames[0] = "Default";
			layerCount = 1;
		}

		int GetLayerIndex(const std::string& name) const
		{
			for (int index = 0; index < MAX_PHYSICS_LAYERS; ++index)
			{
				if (layerNames[index] == name)
					return index;
			}
			return 0;
		}
	};
}
