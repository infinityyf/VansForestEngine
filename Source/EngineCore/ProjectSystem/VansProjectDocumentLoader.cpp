#include "VansProjectDocumentLoader.h"

#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AudioCore/Serialization/VansAudioMixConfigJsonCodec.h"
#include "../AudioCore/VansAudioMixConfig.h"
#include "../PhysicsCore/Serialization/VansCollisionLayerJsonCodec.h"
#include "../PhysicsCore/Storage/VansCollisionLayerStorage.h"
#include "../Util/VansLog.h"
#include "Serialization/VansProjectSettingsJsonCodec.h"
#include "Storage/VansProjectSettingsStorage.h"
#include "VansProjectConfig.h"

#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

namespace Vans
{
VansProjectDocumentLoadResult VansProjectDocumentLoader::Load(const std::string& projectRoot,
															  const VansProjectConfig& config,
															  const std::string& engineRoot)
{
	VansProjectDocumentLoadResult result;
	result.m_Documents.m_Settings.SetDefaults();

	if (!config.renderSettings.empty())
	{
		const std::string path = projectRoot + config.renderSettings;
		VansProjectRenderSettingsData settings;
		std::vector<std::string> warnings;
		std::string error;
		if (VansProjectSettingsStorage::LoadRenderSettings(path, settings, warnings, error))
		{
			for (const std::string& warning : warnings)
				VANS_LOG_WARN("[ProjectSettings] " << warning);
			if (!result.m_Documents.m_Settings.ApplyRenderSettingsData(settings, error))
				VANS_LOG_ERROR("[ProjectSettings] Invalid render settings: " << error);
			else
			{
				result.m_LoadedAny = true;
				VANS_LOG("[ProjectSettings] Loaded render settings: " << path);
			}
		}
		else
			VANS_LOG_WARN("[ProjectSettings] Cannot read render settings: " << path << " (" << error << ")");
	}

	if (!config.physicsSettings.empty())
	{
		const std::string path = projectRoot + config.physicsSettings;
		VansProjectPhysicsSettingsData settings;
		std::string error;
		if (VansProjectSettingsStorage::LoadPhysicsSettings(path, settings, error))
		{
			if (!result.m_Documents.m_Settings.ApplyPhysicsSettingsData(settings, error))
				VANS_LOG_ERROR("[ProjectSettings] Invalid physics settings: " << error);
			else
			{
				result.m_LoadedAny = true;
				VANS_LOG("[ProjectSettings] Loaded physics settings: " << path);
			}
		}
		else
			VANS_LOG_WARN("[ProjectSettings] Cannot read physics settings: " << path << " (" << error << ")");
	}

	if (!config.navigationSettings.empty())
	{
		const std::string path = projectRoot + config.navigationSettings;
		VansNavigationSettings settings;
		std::string error;
		if (VansProjectSettingsStorage::LoadNavigationSettings(path, settings, error) &&
			result.m_Documents.m_Settings.SetNavigationSettings(settings, &error))
		{
			result.m_LoadedAny = true;
			result.m_NavigationSettingsLoaded = true;
			VANS_LOG("[ProjectSettings] Loaded navigation settings: " << path);
		}
		else
		{
			result.m_NavigationSettingsError = error;
			VANS_LOG_WARN("[ProjectSettings] Cannot read navigation settings: "
				<< path << " (" << error << ")");
		}
	}

	if (!config.collisionLayerSettings.empty())
	{
		const std::string path = projectRoot + config.collisionLayerSettings;
		VansEngine::VansCollisionLayerConfig collisionLayers;
		std::string error;
		const VansEngine::VansCollisionLayerLoadStatus status =
			VansEngine::VansCollisionLayerStorage::Load(path, collisionLayers, error);
		if (status == VansEngine::VansCollisionLayerLoadStatus::Loaded)
		{
			result.m_Documents.m_CollisionLayerDocument =
				DecodeSerializedValueJson(VansEngine::VansCollisionLayerJsonCodec::Encode(collisionLayers));
			result.m_Documents.m_CollisionLayers = std::move(collisionLayers);
			result.m_Documents.m_HasCollisionLayerDocument = true;
			result.m_LoadedAny = true;
			VANS_LOG("[ProjectSettings] Loaded collision layers: " << path);
		}
		else
			VANS_LOG_WARN("[ProjectSettings] Cannot read collision layers: " << path << " (" << error << ")");
	}

	if (!config.audioSettings.empty())
	{
		const std::string path = projectRoot + config.audioSettings;
		VansEngine::VansAudioMixConfig audioMix;
		std::string error;
		if (VansEngine::VansAudioMixConfigStorage::Load(path, audioMix, error))
		{
			result.m_Documents.m_AudioMixDocument =
				DecodeSerializedValueJson(VansEngine::VansAudioMixConfigJsonCodec::Encode(audioMix));
			result.m_Documents.m_HasAudioMixDocument = true;
			result.m_LoadedAny = true;
			VANS_LOG("[ProjectSettings] Loaded audio mix: " << path);
		}
		else
			VANS_LOG_WARN("[ProjectSettings] Cannot read audio mix: " << path << " (" << error << ")");
	}

	std::string gafError;
	if (VansGAFProjectConfiguration::LoadForProject(projectRoot, engineRoot, result.m_Documents.m_GAFConfiguration,
													gafError))
	{
		result.m_Documents.m_HasGAFConfiguration = true;
		result.m_LoadedAny = true;
		VANS_LOG("[ProjectSettings] Loaded GAF project configuration");
	}
	else
	{
		result.m_Documents.m_GAFConfiguration = {};
		VANS_LOG_WARN("[ProjectSettings] Cannot read GAF configuration: " << gafError);
	}

	return result;
}
} // namespace Vans
