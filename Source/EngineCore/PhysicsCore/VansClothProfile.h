#pragma once

#include <string>
#include <vector>
#include <GLM/glm.hpp>

namespace VansEngine
{
    // =========================================================================
    // VansClothProfile
    // 布料配置文件数据，可序列化为 .clothprofile（UTF-8 JSON）。
    // 与 RenderNode / Scene 完全解耦：通过 m_ModelPath 自包含模型引用。
    // 编辑器通过 m_ModelPath 独立加载顶点数据，无需场景已加载。
    // =========================================================================
    struct VansClothProfile
    {
        // 配置文件版本号，用于未来字段迁移
        static constexpr int32_t PROFILE_VERSION = 1;

        // ── 元信息 ─────────────────────────────────────────────────────────
        std::string m_Name;
        std::string m_Description;

        // Profile 关联的模型文件路径（相对于引擎根目录）。
        // 编辑器通过此路径独立加载顶点数据，不需要 RenderNode。
        std::string m_ModelPath;

        // ── 物理模拟参数 ────────────────────────────────────────────────────
        float m_Stiffness     = 0.8f;      // 拉伸约束刚度 [0,1]，作用于所有 PhaseConfig
        float m_Damping       = 0.1f;      // 速度阻尼大小，三轴统一
        float m_Friction      = 0.0f;      // 布料自碰撞摩擦
        float m_Gravity       = -9.81f;    // Y 轴重力分量（m/s²）
        bool  m_SelfCollision = false;     // 是否开启自碰撞（NvCloth 当前保留字段）
        float m_WeldTolerance = 1e-5f;     // 顶点焊接网格精度（与 VansClothNode 保持一致）

        // ── 固定点数据（局部空间位置坐标列表）─────────────────────────────
        // 存储固定粒子的网格局部空间坐标，对模型重导出具有鲁棒性。
        // 运行时通过 ResolveIndices() 在局部空间做近邻匹配，转换为顶点索引。
        std::vector<glm::vec3> m_PinnedLocalPositions;

        // 固定点近邻匹配容差（局部空间单位），默认 0.01 m
        float m_PinnedMatchTolerance = 0.01f;

        // ── 序列化 / 反序列化 ───────────────────────────────────────────────
        bool SaveToFile(const std::string& filePath) const;
        bool LoadFromFile(const std::string& filePath);
        void ResetToDefaults();

        // ── 运行时辅助：将局部坐标与网格顶点匹配，返回原始顶点索引列表 ────
        // rawPosFloat4 来自 VansMesh::GetMeshRawPositionData()，每顶点 8 float：
        //   [x, y, z, pad,  nx, ny, nz, npad]（位置 float4 + 法线 float4）。
        // 按近邻距离（容差 m_PinnedMatchTolerance）在局部空间匹配，未找到的点被跳过。
        std::vector<uint32_t> ResolveIndices(
            const std::vector<float>& rawPosFloat4,
            int vertexCount) const;
    };
}
