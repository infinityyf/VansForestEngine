#pragma once

#include "../VansProjectSettingsData.h"

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace Vans
{
	class VansProjectSettingsJsonCodec
	{
	public:
		static bool DecodeRenderSettings(
			const nlohmann::json& root,
			VansProjectRenderSettingsData& settings,
			std::vector<std::string>& warnings,
			std::string& error);

		static nlohmann::json EncodeRenderSettings(const VansProjectRenderSettingsData& settings);

		static bool DecodePhysicsSettings(
			const nlohmann::json& root,
			VansProjectPhysicsSettingsData& settings,
			std::string& error);

		static nlohmann::json EncodePhysicsSettings(const VansProjectPhysicsSettingsData& settings);
	};
}
