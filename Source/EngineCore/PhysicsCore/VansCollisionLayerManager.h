#pragma once
#include <string>
#include <array>
#include <cstdint>

namespace VansEngine
{
	static constexpr int MAX_PHYSICS_LAYERS = 32;

	// ── 碰撞层管理器 ─────────────────────────────────────────────────
	// 从 Project 配置里指定的 collisionLayerSettings 文件加载 Layer 定义和碰撞矩阵。
	// 如果文件不存在则使用内置默认值（仅 "Default" layer，全碰撞）。
	class VansCollisionLayerManager
	{
	public:
		static VansCollisionLayerManager& Get()
		{
			static VansCollisionLayerManager instance;
			return instance;
		}

		VansCollisionLayerManager(const VansCollisionLayerManager&) = delete;
		VansCollisionLayerManager& operator=(const VansCollisionLayerManager&) = delete;

		// ── 初始化 / 加载 ──────────────────────────────────────────────
		// path 由调用方根据 ForestProject.json 中的 collisionLayerSettings 拼接。
		// 加载失败则保留默认值并返回 false
		bool LoadFromFile(const std::string& path);

		// 重置为默认状态（仅 "Default" layer，全碰撞）
		void ResetToDefaults();

		// ── 查询接口 ──────────────────────────────────────────────────
		// 根据名称获取 layer 索引，找不到返回 0 (Default)
		int GetLayerIndex(const std::string& name) const;

		// 获取 layer 名称
		const std::string& GetLayerName(int index) const;

		// 获取指定 layer 的碰撞掩码（哪些 layer 可以与之碰撞）
		uint32_t GetCollisionMask(int layerIndex) const;

		// 检查两个 layer 是否可以碰撞
		bool CanLayersCollide(int layerA, int layerB) const;

		// 获取已定义的 layer 总数
		int GetLayerCount() const { return m_LayerCount; }

	private:
		VansCollisionLayerManager();

		std::array<std::string, MAX_PHYSICS_LAYERS> m_LayerNames;
		std::array<uint32_t, MAX_PHYSICS_LAYERS>    m_CollisionMasks;
		int m_LayerCount = 1;
	};
}
