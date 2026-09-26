#pragma once

#include "../VansScene.h"

#include <cstddef>
#include <string>

namespace VansGraphics
{
	struct VansSceneClothAnimationBindingResult
	{
		bool success = false;
		std::string error;
		std::size_t boundCount = 0;
	};

	class VansSceneClothAnimationBindingExecutor
	{
	public:
		static VansSceneClothAnimationBindingResult Execute(VansScene& scene);
	};
}
