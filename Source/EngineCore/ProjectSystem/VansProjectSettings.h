#pragma once

#include <cstdint>
#include <string>

namespace Vans
{
	struct VansProjectConfig;

	enum class VansProjectFSRMode : std::uint32_t
	{
		MatchViewport = 0,
		NativeAA = 1,
		Quality = 2,
		Performance = 3
	};

	struct VansProjectFSRSettings
	{
		VansProjectFSRMode mode = VansProjectFSRMode::MatchViewport;
		float sharpness = 0.35f;
	};

	class VansProjectSettings
	{
	public:
		void SetDefaults();
		bool LoadFromProjectFiles(const std::string& projectRootPath, const VansProjectConfig& projectConfig);
		bool SaveToProjectFiles(const std::string& projectRootPath, const VansProjectConfig& projectConfig) const;

		float GetFixedTimeStep() const { return m_FixedTimeStep; }
		void SetFixedTimeStep(float fixedTimeStep);
		const VansProjectFSRSettings& GetFSRSettings() const { return m_FSRSettings; }
		void SetFSRSettings(VansProjectFSRMode mode, float sharpness);

	private:
		bool LoadRenderSettingsFromFile(const std::string& filePath);
		bool SaveRenderSettingsToFile(const std::string& filePath) const;
		bool LoadPhysicsSettingsFromFile(const std::string& filePath);
		bool SavePhysicsSettingsToFile(const std::string& filePath) const;
		bool LoadCollisionLayerSettingsFromFile(const std::string& filePath);

		float m_FixedTimeStep = 1.0f / 60.0f;
		VansProjectFSRSettings m_FSRSettings;
	};
}
