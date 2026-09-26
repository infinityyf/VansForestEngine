#include "VansCollisionLayerManager.h"

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
		if (layerIndex < 0 || layerIndex >= m_LayerCount || m_LayerNames[layerIndex].empty())
			return 0u;
		return m_CollisionMasks[layerIndex];
	}

	bool VansCollisionLayerManager::CanLayersCollide(int layerA, int layerB) const
	{
		if (layerA < 0 || layerA >= MAX_PHYSICS_LAYERS ||
			layerB < 0 || layerB >= MAX_PHYSICS_LAYERS)
		{
			return false;
		}

		return (m_CollisionMasks[layerA] & (1u << layerB)) != 0 &&
			   (m_CollisionMasks[layerB] & (1u << layerA)) != 0;
	}
}
