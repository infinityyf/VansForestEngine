#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "../RenderCore/VansRenderRuntimeConfig.h"

namespace Vans
{
	struct VansProjectConfig;

	using VansProjectUpscalerSettings = VansGraphics::VansUpscalerConfig;
	using VansProjectCommandRecordingSettings = VansGraphics::VansCommandRecordingConfig;
	using VansProjectRenderOutputSettings = VansGraphics::VansRenderOutputConfig;

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
		bool ResolvePhysicsQueryProfile(
			const std::string& profile,
			std::uint32_t& collisionMask,
			std::string& error) const;
		const VansProjectUpscalerSettings& GetUpscalerSettings() const
		{
			return m_UpscalerSettings;
		}
		bool SetUpscalerSettings(
			const VansProjectUpscalerSettings& settings,
			std::string* error = nullptr);
		const VansProjectCommandRecordingSettings& GetCommandRecordingSettings() const { return m_CommandRecordingSettings; }
		const VansProjectRenderOutputSettings& GetRenderOutputSettings() const { return m_RenderOutputSettings; }
		bool SetRenderOutputSettings(
			const VansProjectRenderOutputSettings& settings,
			std::string* error = nullptr);
		VansGraphics::VansRenderRuntimeConfig GetRenderRuntimeConfig() const
		{
			return { m_UpscalerSettings, m_CommandRecordingSettings, m_RenderOutputSettings };
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
		std::unordered_map<std::string, std::vector<std::string>> m_PhysicsQueryProfiles;
		VansProjectUpscalerSettings m_UpscalerSettings;
		VansProjectCommandRecordingSettings m_CommandRecordingSettings;
		VansProjectRenderOutputSettings m_RenderOutputSettings;
		VansProjectMainCameraHiZCullSettings m_MainCameraHiZCullSettings;
	};
}
