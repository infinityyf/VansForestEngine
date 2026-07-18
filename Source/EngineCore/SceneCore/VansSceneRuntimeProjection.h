#pragma once

#include <nlohmann/json.hpp>

namespace Vans
{
	class VansSceneRuntimeProjection
	{
	public:
		static bool BuildRuntimeScene(nlohmann::json& sceneData);
	};
}
