#pragma once

#include <cstdint>
#include <string>
#include "../RenderCore/VansRenderRuntimeConfig.h"

namespace Vans
{
	struct VansProjectConfig;

	using VansProjectUpscalerSettings = VansGraphics::VansUpscalerConfig;
	using VansProjectCommandRecordingSettings = VansGraphics::VansCommandRecordingConfig;

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
		const VansProjectUpscalerSettings& GetUpscalerSettings() const
		{
			return m_UpscalerSettings;
		}
		bool SetUpscalerSettings(
			const VansProjectUpscalerSettings& settings,
			std::string* error = nullptr);
		const VansProjectCommandRecordingSettings& GetCommandRecordingSettings() const { return m_CommandRecordingSettings; }
		VansGraphics::VansRenderRuntimeConfig GetRenderRuntimeConfig() const
		{
			return { m_UpscalerSettings, m_CommandRecordingSettings };
		}
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
		VansProjectUpscalerSettings m_UpscalerSettings;
		VansProjectCommandRecordingSettings m_CommandRecordingSettings;
		VansProjectMainCameraHiZCullSettings m_MainCameraHiZCullSettings;
	};
}
