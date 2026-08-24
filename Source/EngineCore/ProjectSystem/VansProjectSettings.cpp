#include "VansProjectSettings.h"
#include "VansProjectConfig.h"
#include "VansProjectSettingsData.h"
#include "Storage/VansProjectSettingsStorage.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
	void VansProjectSettings::SetDefaults()
	{
		m_FixedTimeStep = 1.0f / 60.0f;
		m_PhysicsQueryProfiles.clear();
		m_UpscalerSettings = {};
		m_CommandRecordingSettings = {};
		m_RenderOutputSettings = {};
		m_MainCameraHiZCullSettings = {};
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
		if (!std::isfinite(settings.fsrSharpness) ||
			settings.fsrSharpness < 0.0f || settings.fsrSharpness > 1.0f)
		{
			if (error)
				*error = "upscaler.fsrSharpness must be in [0, 1]";
			return false;
		}
		if (settings.backend == VansGraphics::VansUpscalerBackend::Off &&
			settings.quality != VansGraphics::VansUpscaleQualityMode::NativeAA)
		{
			if (error)
				*error = "Off upscaler backend requires NativeAA quality";
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
		constexpr std::uint32_t kMinimumOutputWidth = 320u;
		constexpr std::uint32_t kMinimumOutputHeight = 180u;
		constexpr std::uint32_t kMaximumOutputDimension = 16384u;
		if (!settings.UsesWindowExtent() &&
			(!settings.HasExplicitExtent() ||
			 settings.width < kMinimumOutputWidth ||
			 settings.height < kMinimumOutputHeight ||
			 settings.width > kMaximumOutputDimension ||
			 settings.height > kMaximumOutputDimension))
		{
			if (error)
			{
				*error = "outputResolution must be 0x0 (follow window) or an explicit "
					"resolution between 320x180 and 16384x16384";
			}
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

	bool VansProjectSettings::LoadFromProjectFiles(const std::string& projectRootPath, const VansProjectConfig& projectConfig)
	{
		bool loadedAnySettings = false;

		if (!projectConfig.renderSettings.empty())
		{
			const std::string renderSettingsPath = projectRootPath + projectConfig.renderSettings;
			VansProjectRenderSettingsData renderSettings;
			std::vector<std::string> warnings;
			std::string error;
			if (VansProjectSettingsStorage::LoadRenderSettings(renderSettingsPath, renderSettings, warnings, error))
			{
				for (const std::string& warning : warnings)
					VANS_LOG_WARN("[ProjectSettings] " << warning);
				std::string upscalerError;
				if (!SetUpscalerSettings(renderSettings.upscalerSettings, &upscalerError))
				{
					VANS_LOG_ERROR("[ProjectSettings] Invalid upscaler settings: " << upscalerError);
					return false;
				}
				SetCommandRecordingSettings(
					renderSettings.commandRecordingSettings.parallelEnabled,
					renderSettings.commandRecordingSettings.frameContextRingEnabled,
					renderSettings.commandRecordingSettings.framesInFlight,
					renderSettings.commandRecordingSettings.asyncComputeEnabled);
				std::string outputResolutionError;
				if (!SetRenderOutputSettings(
					renderSettings.renderOutputSettings,
					&outputResolutionError))
				{
					VANS_LOG_ERROR("[ProjectSettings] Invalid output resolution settings: "
						<< outputResolutionError);
					return false;
				}
				SetMainCameraHiZCullSettings(renderSettings.mainCameraHiZCullSettings);
				VANS_LOG("[ProjectSettings] Loaded render settings: " << renderSettingsPath
					<< ", upscaler.backend=" << VansGraphics::ToString(m_UpscalerSettings.backend)
					<< ", upscaler.quality=" << VansGraphics::ToString(m_UpscalerSettings.quality)
					<< ", upscaler.fsrSharpness=" << m_UpscalerSettings.fsrSharpness
					<< ", commandRecording.parallelEnabled=" << m_CommandRecordingSettings.parallelEnabled
					<< ", commandRecording.frameContextRingEnabled=" << m_CommandRecordingSettings.frameContextRingEnabled
					<< ", commandRecording.framesInFlight=" << m_CommandRecordingSettings.framesInFlight
					<< ", commandRecording.asyncComputeEnabled=" << m_CommandRecordingSettings.asyncComputeEnabled
					<< ", outputResolution="
					<< (m_RenderOutputSettings.HasExplicitExtent()
						? std::to_string(m_RenderOutputSettings.width) + "x" +
							std::to_string(m_RenderOutputSettings.height)
						: std::string("window"))
					<< ", mainCameraHiZCulling.enabled=" << m_MainCameraHiZCullSettings.enabled);
				loadedAnySettings = true;
			}
			else
			{
				VANS_LOG_WARN("[ProjectSettings] Cannot read render settings: " << renderSettingsPath << " (" << error << ")");
			}
		}

		if (!projectConfig.physicsSettings.empty())
		{
			const std::string physicsSettingsPath = projectRootPath + projectConfig.physicsSettings;
			VansProjectPhysicsSettingsData physicsSettings;
			std::string error;
			if (VansProjectSettingsStorage::LoadPhysicsSettings(physicsSettingsPath, physicsSettings, error))
			{
				SetFixedTimeStep(physicsSettings.fixedTimeStep);
				m_PhysicsQueryProfiles = std::move(physicsSettings.queryProfiles);
				VANS_LOG("[ProjectSettings] Loaded physics settings: " << physicsSettingsPath << ", fixedTimeStep=" << m_FixedTimeStep);
				loadedAnySettings = true;
			}
			else
			{
				VANS_LOG_WARN("[ProjectSettings] Cannot read physics settings: " << physicsSettingsPath << " (" << error << ")");
			}
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
			VansProjectRenderSettingsData renderSettings;
			renderSettings.upscalerSettings = m_UpscalerSettings;
			renderSettings.commandRecordingSettings = m_CommandRecordingSettings;
			renderSettings.renderOutputSettings = m_RenderOutputSettings;
			renderSettings.mainCameraHiZCullSettings = m_MainCameraHiZCullSettings;
			std::string error;
			if (VansProjectSettingsStorage::SaveRenderSettings(renderSettingsPath, renderSettings, error))
			{
				VANS_LOG("[ProjectSettings] Saved render settings: " << renderSettingsPath);
				savedAnySettings = true;
			}
			else
			{
				VANS_LOG_ERROR("[ProjectSettings] Cannot write render settings: " << renderSettingsPath << " (" << error << ")");
			}
		}

		if (!projectConfig.physicsSettings.empty())
		{
			const std::string physicsSettingsPath = projectRootPath + projectConfig.physicsSettings;
			VansProjectPhysicsSettingsData physicsSettings;
			physicsSettings.fixedTimeStep = m_FixedTimeStep;
			physicsSettings.queryProfiles = m_PhysicsQueryProfiles;
			std::string error;
			if (VansProjectSettingsStorage::SavePhysicsSettings(physicsSettingsPath, physicsSettings, error))
			{
				VANS_LOG("[ProjectSettings] Saved physics settings: " << physicsSettingsPath);
				savedAnySettings = true;
			}
			else
			{
				VANS_LOG_ERROR("[ProjectSettings] Cannot write physics settings: " << physicsSettingsPath << " (" << error << ")");
			}
		}

		return savedAnySettings;
	}

	bool VansProjectSettings::LoadCollisionLayerSettingsFromFile(const std::string& filePath)
	{
		return VansEngine::VansCollisionLayerManager::Get().LoadFromFile(filePath);
	}

}
