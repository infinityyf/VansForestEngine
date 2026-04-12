#pragma once

#include <string>

namespace Vans
{
	struct VansProjectConfig;

	class VansProjectSettings
	{
	public:
		void SetDefaults();
		bool LoadFromProjectFiles(const std::string& projectRootPath, const VansProjectConfig& projectConfig);
		bool SaveToProjectFiles(const std::string& projectRootPath, const VansProjectConfig& projectConfig) const;

		float GetFixedTimeStep() const { return m_FixedTimeStep; }
		void SetFixedTimeStep(float fixedTimeStep);

	private:
		bool LoadPhysicsSettingsFromFile(const std::string& filePath);
		bool SavePhysicsSettingsToFile(const std::string& filePath) const;
		bool LoadCollisionLayerSettingsFromFile(const std::string& filePath);

		float m_FixedTimeStep = 1.0f / 60.0f;
	};
}