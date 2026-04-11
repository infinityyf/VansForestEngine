#include "VansCollisionLayerManager.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <fstream>
#include <filesystem>

namespace VansEngine
{
	// ===========================================================================
	// 构造函数 — 初始化默认值
	// ===========================================================================

	VansCollisionLayerManager::VansCollisionLayerManager()
	{
		ResetToDefaults();
	}

	// ===========================================================================
	// 重置为默认状态
	// ===========================================================================

	void VansCollisionLayerManager::ResetToDefaults()
	{
		for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
		{
			m_LayerNames[i].clear();
			m_CollisionMasks[i] = 0xFFFFFFFF; // 默认与所有 layer 碰撞
		}
		m_LayerNames[0] = "Default";
		m_LayerCount = 1;
	}

	// ===========================================================================
	// 从 JSON 文件加载 Layer 和碰撞矩阵
	// ===========================================================================

	bool VansCollisionLayerManager::LoadFromFile(const std::string& path)
	{
		if (!std::filesystem::exists(path))
		{
			VANS_LOG_WARN("[PhysicsLayer] File not found: " << path << " — using defaults");
			ResetToDefaults();
			return false;
		}

		std::ifstream file(path);
		if (!file.is_open())
		{
			VANS_LOG_WARN("[PhysicsLayer] Cannot open: " << path << " — using defaults");
			ResetToDefaults();
			return false;
		}

		json root;
		try
		{
			file >> root;
		}
		catch (const json::parse_error& e)
		{
			VANS_LOG_ERROR("[PhysicsLayer] JSON parse error: " << e.what());
			ResetToDefaults();
			return false;
		}

		// 先重置
		ResetToDefaults();

		// ── 解析 layers 数组 ─────────────────────────────────────────
		if (root.contains("layers") && root["layers"].is_array())
		{
			m_LayerCount = 0;
			for (const auto& layerJson : root["layers"])
			{
				int index = layerJson.value("index", -1);
				std::string name = layerJson.value("name", "");
				if (index < 0 || index >= MAX_PHYSICS_LAYERS || name.empty())
					continue;

				m_LayerNames[index] = name;
				if (index + 1 > m_LayerCount)
					m_LayerCount = index + 1;
			}
		}

		// ── 解析 collisionMatrix ─────────────────────────────────────
		if (root.contains("collisionMatrix") && root["collisionMatrix"].is_object())
		{
			// 先清零所有掩码
			for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
				m_CollisionMasks[i] = 0;

			for (auto& [layerName, targets] : root["collisionMatrix"].items())
			{
				int srcIndex = GetLayerIndex(layerName);
				if (srcIndex < 0) continue;

				if (targets.is_array())
				{
					for (const auto& targetName : targets)
					{
						int dstIndex = GetLayerIndex(targetName.get<std::string>());
						if (dstIndex >= 0)
						{
							m_CollisionMasks[srcIndex] |= (1u << dstIndex);
						}
					}
				}
			}
		}

		VANS_LOG("[PhysicsLayer] Loaded " << m_LayerCount << " layers from " << path);
		return true;
	}

	// ===========================================================================
	// 查询接口
	// ===========================================================================

	int VansCollisionLayerManager::GetLayerIndex(const std::string& name) const
	{
		for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
		{
			if (m_LayerNames[i] == name)
				return i;
		}
		return 0; // 默认归入 Default
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
			return true; // 安全默认：允许碰撞

		return (m_CollisionMasks[layerA] & (1u << layerB)) != 0 &&
			   (m_CollisionMasks[layerB] & (1u << layerA)) != 0;
	}
}
