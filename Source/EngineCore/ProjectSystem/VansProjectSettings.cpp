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
		m_ProjectRenderPasses.clear();
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

	bool VansProjectSettings::SetCommandRecordingSettings(
		bool parallelEnabled,
		std::uint32_t framesInFlight,
		bool asyncComputeEnabled)
	{
		if (!VansProjectCommandRecordingSettings::ValidFramesInFlight(framesInFlight))
			return false;
		m_CommandRecordingSettings.parallelEnabled = parallelEnabled;
		m_CommandRecordingSettings.framesInFlight = framesInFlight;
		m_CommandRecordingSettings.asyncComputeEnabled = asyncComputeEnabled;
		return true;
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
		if (!candidate.SetCommandRecordingSettings(
			renderSettings.commandRecordingSettings.parallelEnabled,
			renderSettings.commandRecordingSettings.framesInFlight,
			renderSettings.commandRecordingSettings.asyncComputeEnabled))
		{
			error = "commandRecording.framesInFlight must be 1 or 2";
			return false;
		}
		if (!candidate.SetRenderOutputSettings(renderSettings.renderOutputSettings, &error))
			return false;
		if (!candidate.SetProjectRenderPasses(renderSettings.projectRenderPasses, error))
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
		settings.projectRenderPasses = m_ProjectRenderPasses;
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

	bool VansProjectSettings::SetProjectRenderPasses(const std::vector<VansAssetGuid>& passes, std::string& error)
	{
		if (passes.size() > 64)
		{
			error = "projectRenderPasses supports at most 64 references";
			return false;
		}
		for (std::size_t i = 0; i < passes.size(); ++i)
		{
			if (!passes[i].IsValid() || std::find(passes.begin(), passes.begin() + i, passes[i]) != passes.begin() + i)
			{
				error = "projectRenderPasses contains an invalid or duplicate GUID";
				return false;
			}
		}
		m_ProjectRenderPasses = passes;
		error.clear();
		return true;
	}

}
