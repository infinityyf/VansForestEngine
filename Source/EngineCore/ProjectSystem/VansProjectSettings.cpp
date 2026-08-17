#include "VansProjectSettings.h"
#include "VansProjectConfig.h"
#include "VansProjectSettingsData.h"
#include "Storage/VansProjectSettingsStorage.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../Util/VansLog.h"

#include <algorithm>

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
			case VansProjectFSRMode::Balanced: return "Balanced";
			case VansProjectFSRMode::Performance: return "Performance";
			case VansProjectFSRMode::MatchViewport:
			default: return "MatchViewport";
			}
		}

	}

	void VansProjectSettings::SetDefaults()
	{
		m_FixedTimeStep = 1.0f / 60.0f;
		m_FSRSettings = {};
		m_CommandRecordingSettings = {};
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

	void VansProjectSettings::SetFSRSettings(VansProjectFSRMode mode, float sharpness)
	{
		switch (mode)
		{
		case VansProjectFSRMode::MatchViewport:
		case VansProjectFSRMode::NativeAA:
		case VansProjectFSRMode::Quality:
		case VansProjectFSRMode::Balanced:
		case VansProjectFSRMode::Performance:
			m_FSRSettings.mode = mode;
			break;
		default:
			m_FSRSettings.mode = VansProjectFSRMode::MatchViewport;
			break;
		}
		m_FSRSettings.sharpness = std::clamp(sharpness, 0.0f, 1.0f);
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
				SetFSRSettings(renderSettings.fsrSettings.mode, renderSettings.fsrSettings.sharpness);
				SetCommandRecordingSettings(
					renderSettings.commandRecordingSettings.parallelEnabled,
					renderSettings.commandRecordingSettings.frameContextRingEnabled,
					renderSettings.commandRecordingSettings.framesInFlight,
					renderSettings.commandRecordingSettings.asyncComputeEnabled);
				SetMainCameraHiZCullSettings(renderSettings.mainCameraHiZCullSettings);
				VANS_LOG("[ProjectSettings] Loaded render settings: " << renderSettingsPath
					<< ", fsr.mode=" << ToString(m_FSRSettings.mode)
					<< ", fsr.sharpness=" << m_FSRSettings.sharpness
					<< ", commandRecording.parallelEnabled=" << m_CommandRecordingSettings.parallelEnabled
					<< ", commandRecording.frameContextRingEnabled=" << m_CommandRecordingSettings.frameContextRingEnabled
					<< ", commandRecording.framesInFlight=" << m_CommandRecordingSettings.framesInFlight
					<< ", commandRecording.asyncComputeEnabled=" << m_CommandRecordingSettings.asyncComputeEnabled
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
			renderSettings.fsrSettings = m_FSRSettings;
			renderSettings.commandRecordingSettings = m_CommandRecordingSettings;
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
