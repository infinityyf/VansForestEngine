#pragma once

#include <string>
#include <vector>

namespace Vans
{
	struct VansProjectPhysicsSettingsData;
	struct VansProjectRenderSettingsData;

	class VansProjectSettingsStorage
	{
	public:
		static bool LoadRenderSettings(
			const std::string& filePath,
			VansProjectRenderSettingsData& settings,
			std::vector<std::string>& warnings,
			std::string& error);

		static bool SaveRenderSettings(
			const std::string& filePath,
			const VansProjectRenderSettingsData& settings,
			std::string& error);

		static bool LoadPhysicsSettings(
			const std::string& filePath,
			VansProjectPhysicsSettingsData& settings,
			std::string& error);

		static bool SavePhysicsSettings(
			const std::string& filePath,
			const VansProjectPhysicsSettingsData& settings,
			std::string& error);
	};
}
