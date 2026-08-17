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
		Balanced = 3,
		Performance = 4
	};

	struct VansProjectFSRSettings
	{
		VansProjectFSRMode mode = VansProjectFSRMode::MatchViewport;
		float sharpness = 0.2f;
	};

	struct VansProjectCommandRecordingSettings
	{
		bool parallelEnabled = true;
		bool frameContextRingEnabled = false;
		std::uint32_t framesInFlight = 2;
		bool asyncComputeEnabled = false;
	};

	struct VansProjectMainCameraHiZCullSettings
	{
		bool enabled = true;
		bool enableOpaque = true;
		bool enableHair = true;
		bool enableTransparent = false;
		bool enableDecal = true;
		bool enableForwardOpaqueAfterDeferred = true;
		float depthBiasMeters = 0.35f;
		float cameraMotionDisableDistance = 1.0f;
		float cameraMotionDisableAngleRadians = 0.13962634f;
		std::uint32_t forceVisibleFramesAfterChange = 1;
		std::uint32_t refreshCulledEveryNFrames = 30;
		float maxScreenCoverageForCull = 0.65f;
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
		const VansProjectCommandRecordingSettings& GetCommandRecordingSettings() const { return m_CommandRecordingSettings; }
		void SetCommandRecordingSettings(
			bool parallelEnabled,
			bool frameContextRingEnabled,
			std::uint32_t framesInFlight,
			bool asyncComputeEnabled);
		const VansProjectMainCameraHiZCullSettings& GetMainCameraHiZCullSettings() const { return m_MainCameraHiZCullSettings; }
		void SetMainCameraHiZCullSettings(const VansProjectMainCameraHiZCullSettings& settings);

private:
		bool LoadCollisionLayerSettingsFromFile(const std::string& filePath);

		float m_FixedTimeStep = 1.0f / 60.0f;
		VansProjectFSRSettings m_FSRSettings;
		VansProjectCommandRecordingSettings m_CommandRecordingSettings;
		VansProjectMainCameraHiZCullSettings m_MainCameraHiZCullSettings;
	};
}
