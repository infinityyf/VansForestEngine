#include "VansProjectSettings.h"
#include "VansProjectConfig.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Vans
{
	namespace
	{
		const char* ToString(VansProjectFSRMode mode)
		{
			switch (mode)
			{
			case VansProjectFSRMode::NativeAA: return "NativeAA";
			case VansProjectFSRMode::Quality: return "Quality";
			case VansProjectFSRMode::Performance: return "Performance";
			case VansProjectFSRMode::MatchViewport:
			default: return "MatchViewport";
			}
		}

		VansProjectFSRMode ParseFSRMode(const std::string& value)
		{
			if (value == "NativeAA") return VansProjectFSRMode::NativeAA;
			if (value == "Quality") return VansProjectFSRMode::Quality;
			if (value == "Performance") return VansProjectFSRMode::Performance;
			if (value != "MatchViewport")
			{
				VANS_LOG_WARN("[ProjectSettings] Unknown FSR mode '" << value
					<< "', fallback to MatchViewport");
			}
			return VansProjectFSRMode::MatchViewport;
		}
	}

	void VansProjectSettings::SetDefaults()
	{
		m_FixedTimeStep = 1.0f / 60.0f;
		m_FSRSettings = {};
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

	void VansProjectSettings::SetFSRSettings(VansProjectFSRMode mode, float sharpness)
	{
		switch (mode)
		{
		case VansProjectFSRMode::MatchViewport:
		case VansProjectFSRMode::NativeAA:
		case VansProjectFSRMode::Quality:
		case VansProjectFSRMode::Performance:
			m_FSRSettings.mode = mode;
			break;
		default:
			m_FSRSettings.mode = VansProjectFSRMode::MatchViewport;
			break;
		}
		m_FSRSettings.sharpness = std::clamp(sharpness, 0.0f, 1.0f);
	}

	bool VansProjectSettings::LoadFromProjectFiles(const std::string& projectRootPath, const VansProjectConfig& projectConfig)
	{
		bool loadedAnySettings = false;

		if (!projectConfig.renderSettings.empty())
		{
			const std::string renderSettingsPath = projectRootPath + projectConfig.renderSettings;
			loadedAnySettings = LoadRenderSettingsFromFile(renderSettingsPath) || loadedAnySettings;
		}

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

		if (!projectConfig.renderSettings.empty())
		{
			const std::string renderSettingsPath = projectRootPath + projectConfig.renderSettings;
			savedAnySettings = SaveRenderSettingsToFile(renderSettingsPath) || savedAnySettings;
		}

		if (!projectConfig.physicsSettings.empty())
		{
			const std::string physicsSettingsPath = projectRootPath + projectConfig.physicsSettings;
			savedAnySettings = SavePhysicsSettingsToFile(physicsSettingsPath) || savedAnySettings;
		}

		return savedAnySettings;
	}

	bool VansProjectSettings::LoadRenderSettingsFromFile(const std::string& filePath)
	{
		std::ifstream inputFile(filePath);
		if (!inputFile.is_open())
		{
			VANS_LOG_WARN("[ProjectSettings] Cannot open render settings: " << filePath);
			return false;
		}

		try
		{
			const json config = json::parse(inputFile);
			const std::uint32_t schemaVersion = config.value("schemaVersion", 1u);
			if (schemaVersion != 1u)
			{
				VANS_LOG_WARN("[ProjectSettings] Unsupported render settings schemaVersion="
					<< schemaVersion << ", reading known fields only");
			}

			if (config.contains("fsr") && config["fsr"].is_object())
			{
				const json& fsr = config["fsr"];
				SetFSRSettings(
					ParseFSRMode(fsr.value("mode", std::string("MatchViewport"))),
					fsr.value("sharpness", 0.35f));
			}
		}
		catch (const json::exception& exception)
		{
			VANS_LOG_ERROR("[ProjectSettings] Render settings JSON parse error: " << exception.what());
			return false;
		}

		VANS_LOG("[ProjectSettings] Loaded render settings: " << filePath
			<< ", fsr.mode=" << ToString(m_FSRSettings.mode)
			<< ", fsr.sharpness=" << m_FSRSettings.sharpness);
		return true;
	}

	bool VansProjectSettings::SaveRenderSettingsToFile(const std::string& filePath) const
	{
		json config;
		config["schemaVersion"] = 1;
		config["fsr"] = {
			{ "mode", ToString(m_FSRSettings.mode) },
			{ "sharpness", m_FSRSettings.sharpness }
		};

		fs::path outputPath(filePath);
		if (outputPath.has_parent_path())
		{
			fs::create_directories(outputPath.parent_path());
		}

		std::ofstream outputFile(filePath);
		if (!outputFile.is_open())
		{
			VANS_LOG_ERROR("[ProjectSettings] Cannot write render settings: " << filePath);
			return false;
		}

		outputFile << config.dump(4);
		VANS_LOG("[ProjectSettings] Saved render settings: " << filePath);
		return true;
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
