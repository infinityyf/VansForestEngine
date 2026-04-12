#include "VansProjectSettings.h"
#include "VansProjectConfig.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Vans
{
	void VansProjectSettings::SetDefaults()
	{
		m_FixedTimeStep = 1.0f / 60.0f;
	}

	void VansProjectSettings::SetFixedTimeStep(float fixedTimeStep)
	{
		if (fixedTimeStep <= 0.0f)
		{
			VANS_LOG_WARN("[ProjectSettings] Invalid fixedTimeStep: " << fixedTimeStep << ", fallback to default 1/60s");
			m_FixedTimeStep = 1.0f / 60.0f;
			return;
		}

		m_FixedTimeStep = fixedTimeStep;
	}

	bool VansProjectSettings::LoadFromProjectFiles(const std::string& projectRootPath, const VansProjectConfig& projectConfig)
	{
		bool loadedAnySettings = false;

		if (!projectConfig.physicsSettings.empty())
		{
			const std::string physicsSettingsPath = projectRootPath + projectConfig.physicsSettings;
			loadedAnySettings = LoadPhysicsSettingsFromFile(physicsSettingsPath) || loadedAnySettings;
		}

		if (!projectConfig.collisionLayerSettings.empty())
		{
			const std::string collisionLayerSettingsPath = projectRootPath + projectConfig.collisionLayerSettings;
			loadedAnySettings = LoadCollisionLayerSettingsFromFile(collisionLayerSettingsPath) || loadedAnySettings;
		}

		return loadedAnySettings;
	}

	bool VansProjectSettings::SaveToProjectFiles(const std::string& projectRootPath, const VansProjectConfig& projectConfig) const
	{
		bool savedAnySettings = false;

		if (!projectConfig.physicsSettings.empty())
		{
			const std::string physicsSettingsPath = projectRootPath + projectConfig.physicsSettings;
			savedAnySettings = SavePhysicsSettingsToFile(physicsSettingsPath) || savedAnySettings;
		}

		return savedAnySettings;
	}

	bool VansProjectSettings::LoadPhysicsSettingsFromFile(const std::string& filePath)
	{
		std::ifstream inputFile(filePath);
		if (!inputFile.is_open())
		{
			VANS_LOG_WARN("[ProjectSettings] Cannot open physics settings: " << filePath);
			return false;
		}

		try
		{
			const json config = json::parse(inputFile);
			SetFixedTimeStep(config.value("fixedTimeStep",
				config.value("physicsDeltaTime", 1.0f / 60.0f)));
		}
		catch (const json::exception& exception)
		{
			VANS_LOG_ERROR("[ProjectSettings] Physics settings JSON parse error: " << exception.what());
			return false;
		}

		VANS_LOG("[ProjectSettings] Loaded physics settings: " << filePath << ", fixedTimeStep=" << m_FixedTimeStep);
		return true;
	}

	bool VansProjectSettings::LoadCollisionLayerSettingsFromFile(const std::string& filePath)
	{
		return VansEngine::VansCollisionLayerManager::Get().LoadFromFile(filePath);
	}

	bool VansProjectSettings::SavePhysicsSettingsToFile(const std::string& filePath) const
	{
		json config;
		config["fixedTimeStep"] = m_FixedTimeStep;

		fs::path outputPath(filePath);
		if (outputPath.has_parent_path())
		{
			fs::create_directories(outputPath.parent_path());
		}

		std::ofstream outputFile(filePath);
		if (!outputFile.is_open())
		{
			VANS_LOG_ERROR("[ProjectSettings] Cannot write physics settings: " << filePath);
			return false;
		}

		outputFile << config.dump(4);
		VANS_LOG("[ProjectSettings] Saved physics settings: " << filePath);
		return true;
	}
}