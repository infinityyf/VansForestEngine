#include "VansCollisionLayerManager.h"

#include "Storage/VansCollisionLayerStorage.h"
#include "../Util/VansLog.h"

namespace VansEngine
{
	VansCollisionLayerManager::VansCollisionLayerManager()
	{
		ResetToDefaults();
	}

	void VansCollisionLayerManager::ResetToDefaults()
	{
		VansCollisionLayerConfig config;
		config.ResetToDefaults();
		ApplyConfig(config);
	}

	void VansCollisionLayerManager::ApplyConfig(const VansCollisionLayerConfig& config)
	{
		m_LayerNames = config.layerNames;
		m_CollisionMasks = config.collisionMasks;
		m_LayerCount = config.layerCount;
	}

	bool VansCollisionLayerManager::LoadFromFile(const std::string& path)
	{
		VansCollisionLayerConfig config;
		std::string error;
		const VansCollisionLayerLoadStatus status = VansCollisionLayerStorage::Load(path, config, error);
		if (status == VansCollisionLayerLoadStatus::Loaded)
		{
			ApplyConfig(config);
			VANS_LOG("[PhysicsLayer] Loaded " << m_LayerCount << " layers from " << path);
			return true;
		}

		if (status == VansCollisionLayerLoadStatus::Missing)
		{
			VANS_LOG_WARN("[PhysicsLayer] File not found: " << path << " - using defaults");
			ResetToDefaults();
			return false;
		}

		VANS_LOG_WARN("[PhysicsLayer] Cannot read: " << path << " (" << error << ") using defaults");
		ResetToDefaults();
		return false;
	}

	int VansCollisionLayerManager::GetLayerIndex(const std::string& name) const
	{
		for (int index = 0; index < MAX_PHYSICS_LAYERS; ++index)
		{
			if (m_LayerNames[index] == name)
				return index;
		}
		return 0;
	}

	bool VansCollisionLayerManager::TryGetLayerIndex(const std::string& name, int& index) const
	{
		for (int candidate = 0; candidate < m_LayerCount; ++candidate)
		{
			if (m_LayerNames[candidate] == name)
			{
				index = candidate;
				return true;
			}
		}
		index = -1;
		return false;
	}

	const std::string& VansCollisionLayerManager::GetLayerName(int index) const
	{
		static const std::string empty;
		if (index < 0 || index >= MAX_PHYSICS_LAYERS)
			return empty;
		return m_LayerNames[index];
	}

	uint32_t VansCollisionLayerManager::GetCollisionMask(int layerIndex) const
	{
		if (layerIndex < 0 || layerIndex >= MAX_PHYSICS_LAYERS)
			return 0xFFFFFFFF;
		return m_CollisionMasks[layerIndex];
	}

	bool VansCollisionLayerManager::CanLayersCollide(int layerA, int layerB) const
	{
		if (layerA < 0 || layerA >= MAX_PHYSICS_LAYERS ||
			layerB < 0 || layerB >= MAX_PHYSICS_LAYERS)
		{
			return true;
		}

		return (m_CollisionMasks[layerA] & (1u << layerB)) != 0 &&
			   (m_CollisionMasks[layerB] & (1u << layerA)) != 0;
	}
}
