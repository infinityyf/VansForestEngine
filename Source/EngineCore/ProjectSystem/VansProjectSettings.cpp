#include "VansProjectSettings.h"
#include "VansProjectSettingsData.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../RenderCore/UpscalingCore/VansUpscaleResolutionPolicy.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Vans
{
	void VansProjectSettings::SetDefaults()
	{
		m_PhysicsTiming = {};
		m_PhysicsQueryProfiles.clear();
		m_UpscalerSettings = {};
		m_CommandRecordingSettings = {};
		m_RenderOutputSettings = {};
		m_AtmosphereQualitySettings = {};
		m_NearMediaQualitySettings = {};
		m_CloudShadowQualitySettings = {};
		m_MainCameraHiZCullSettings = {};
		m_CameraLensLimits = {};
		m_NavigationSettings = {};
	}

	bool VansProjectSettings::SetNavigationSettings(
		const VansNavigationSettings& settings,
		std::string* error)
	{
		std::string validationError;
		if (!ValidateNavigationSettings(settings, validationError))
		{
			if (error) *error = std::move(validationError);
			return false;
		}
		m_NavigationSettings = settings;
		if (error) error->clear();
		return true;
	}

	bool VansProjectSettings::SetPhysicsTiming(
		const VansEngine::VansPhysicsTiming& timing,
		std::string& error)
	{
		if (!timing.IsValid())
		{
			error = "Physics timing requires positive finite rigid/cloth steps and bounded substep counts";
			return false;
		}

		m_PhysicsTiming = timing;
		error.clear();
		return true;
	}

	bool VansProjectSettings::ResolvePhysicsQueryProfile(
		const std::string& profile,
		std::uint32_t& collisionMask,
		std::string& error) const
	{
		collisionMask = 0;
		error.clear();
		const auto found = m_PhysicsQueryProfiles.find(profile);
		if (found == m_PhysicsQueryProfiles.end())
		{
			error = "Unknown physics query profile '" + profile + "'";
			return false;
		}
		for (const std::string& layerName : found->second)
		{
			int layerIndex = -1;
			if (!VansEngine::VansCollisionLayerManager::Get().TryGetLayerIndex(layerName, layerIndex)
				|| layerIndex < 0 || layerIndex >= 32)
			{
				error = "Physics query profile '" + profile
					+ "' references unknown collision layer '" + layerName + "'";
				collisionMask = 0;
				return false;
			}
			collisionMask |= (1u << static_cast<std::uint32_t>(layerIndex));
		}
		if (collisionMask == 0)
		{
			error = "Physics query profile '" + profile + "' resolves to an empty mask";
			return false;
		}
		return true;
	}

	bool VansProjectSettings::SetUpscalerSettings(
		const VansProjectUpscalerSettings& settings,
		std::string* error)
	{
		std::string validationError;
		if (!VansGraphics::VansUpscaleResolutionPolicy::ValidateConfig(
			settings, validationError))
		{
			if (error) *error = std::move(validationError);
			return false;
		}
		m_UpscalerSettings = settings;
		if (error)
			error->clear();
		return true;
	}

	bool VansProjectSettings::SetRenderOutputSettings(
		const VansProjectRenderOutputSettings& settings,
		std::string* error)
	{
		std::string validationError;
		if (!VansGraphics::VansUpscaleResolutionPolicy::ValidateOutputExtent(
			{ settings.width, settings.height }, true, 0u, validationError))
		{
			if (error) *error = std::move(validationError);
			return false;
		}
		m_RenderOutputSettings = settings;
		if (error)
			error->clear();
		return true;
	}

	void VansProjectSettings::SetCommandRecordingSettings(
		bool parallelEnabled,
		bool frameContextRingEnabled,
		std::uint32_t framesInFlight,
		bool asyncComputeEnabled)
	{
		m_CommandRecordingSettings.parallelEnabled = parallelEnabled;
		m_CommandRecordingSettings.frameContextRingEnabled = frameContextRingEnabled;
		// 当前只为非 async 主路径启用双帧 ring，配置读取时统一夹取，避免非法项目配置扩大资源生命周期复杂度。
		m_CommandRecordingSettings.framesInFlight = std::clamp<std::uint32_t>(framesInFlight, 1u, 2u);
		m_CommandRecordingSettings.asyncComputeEnabled = asyncComputeEnabled;
	}

	void VansProjectSettings::SetMainCameraHiZCullSettings(const VansProjectMainCameraHiZCullSettings& settings)
	{
		m_MainCameraHiZCullSettings = settings;
		m_MainCameraHiZCullSettings.depthBiasMeters = std::max(m_MainCameraHiZCullSettings.depthBiasMeters, 0.0f);
		m_MainCameraHiZCullSettings.cameraMotionDisableDistance =
			std::max(m_MainCameraHiZCullSettings.cameraMotionDisableDistance, 0.0f);
		m_MainCameraHiZCullSettings.cameraMotionDisableAngleRadians =
			std::max(m_MainCameraHiZCullSettings.cameraMotionDisableAngleRadians, 0.0f);
		m_MainCameraHiZCullSettings.forceVisibleFramesAfterChange =
			std::max(m_MainCameraHiZCullSettings.forceVisibleFramesAfterChange, 1u);
		m_MainCameraHiZCullSettings.refreshCulledEveryNFrames =
			std::max(m_MainCameraHiZCullSettings.refreshCulledEveryNFrames, 1u);
		m_MainCameraHiZCullSettings.maxScreenCoverageForCull =
			std::clamp(m_MainCameraHiZCullSettings.maxScreenCoverageForCull, 0.05f, 1.0f);
	}

	bool VansProjectSettings::SetCameraLensLimits(
		const VansCameraLensLimits& limits,
		std::string& error)
	{
		if (!VansValidateCameraLensLimits(limits, error)) return false;
		m_CameraLensLimits = limits;
		error.clear();
		return true;
	}

	bool VansProjectSettings::ApplyRenderSettingsData(
		const VansProjectRenderSettingsData& renderSettings,
		std::string& error)
	{
		VansProjectSettings candidate = *this;
		if (!candidate.SetUpscalerSettings(renderSettings.upscalerSettings, &error))
			return false;
		candidate.SetCommandRecordingSettings(
			renderSettings.commandRecordingSettings.parallelEnabled,
			renderSettings.commandRecordingSettings.frameContextRingEnabled,
			renderSettings.commandRecordingSettings.framesInFlight,
			renderSettings.commandRecordingSettings.asyncComputeEnabled);
		if (!candidate.SetRenderOutputSettings(renderSettings.renderOutputSettings, &error))
			return false;
		candidate.m_AtmosphereQualitySettings = renderSettings.atmosphereQualitySettings;
		candidate.m_NearMediaQualitySettings = renderSettings.nearMediaQualitySettings;
		candidate.m_CloudShadowQualitySettings = renderSettings.cloudShadowQualitySettings;
		candidate.SetMainCameraHiZCullSettings(renderSettings.mainCameraHiZCullSettings);
		if (!candidate.SetCameraLensLimits(renderSettings.cameraLensLimits, error))
			return false;
		*this = std::move(candidate);
		error.clear();
		return true;
	}

	bool VansProjectSettings::ApplyPhysicsSettingsData(
		const VansProjectPhysicsSettingsData& physicsSettings,
		std::string& error)
	{
		VansProjectSettings candidate = *this;
		if (!candidate.SetPhysicsTiming(physicsSettings.timing, error))
			return false;
		candidate.m_PhysicsQueryProfiles = physicsSettings.queryProfiles;
		*this = std::move(candidate);
		error.clear();
		return true;
	}

	VansProjectRenderSettingsData VansProjectSettings::BuildRenderSettingsData() const
	{
		VansProjectRenderSettingsData settings;
		settings.upscalerSettings = m_UpscalerSettings;
		settings.commandRecordingSettings = m_CommandRecordingSettings;
		settings.renderOutputSettings = m_RenderOutputSettings;
		settings.atmosphereQualitySettings = m_AtmosphereQualitySettings;
		settings.nearMediaQualitySettings = m_NearMediaQualitySettings;
		settings.cloudShadowQualitySettings = m_CloudShadowQualitySettings;
		settings.mainCameraHiZCullSettings = m_MainCameraHiZCullSettings;
		settings.cameraLensLimits = m_CameraLensLimits;
		return settings;
	}

	VansProjectPhysicsSettingsData VansProjectSettings::BuildPhysicsSettingsData() const
	{
		VansProjectPhysicsSettingsData settings;
		settings.timing = m_PhysicsTiming;
		settings.queryProfiles = m_PhysicsQueryProfiles;
		return settings;
	}

}
