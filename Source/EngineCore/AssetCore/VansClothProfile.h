#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <vector>
#include <GLM/glm.hpp>
#include <GLM/gtc/matrix_transform.hpp>

namespace VansEngine
{
    // =========================================================================
    // VansClothProfile
    // 布料配置文件数据，可序列化为 .clothprofile（UTF-8 JSON）。
    // 与 RenderNode / Scene 完全解耦：通过 m_ModelPath 自包含模型引用。
    // 编辑器通过 m_ModelPath 独立加载顶点数据，无需场景已加载。
    // V2 新增骨骼跟随配置（followBones / skeletonOffset / pinnedBoneBindings）。
    // =========================================================================
    struct VansClothProfile
    {
        // ── 元信息 ─────────────────────────────────────────────────────────
        std::string m_Name;
        std::string m_Description;

        // Profile 关联的模型文件路径（相对于引擎根目录）。
        // 编辑器通过此路径独立加载顶点数据，不需要 RenderNode。
        std::string m_ModelPath;

        // ── 物理模拟参数 ────────────────────────────────────────────────────
        float m_Stiffness     = 0.8f;      // 拉伸约束刚度 [0,1]，作用于所有 PhaseConfig
        float m_StiffnessFrequency = 60.0f;// NvCloth 约束/阻尼归一化频率（Hz）
        float m_Damping       = 0.1f;      // 速度阻尼大小，三轴统一
        float m_Friction      = 0.0f;      // 布料自碰撞摩擦
        float m_Gravity       = -9.81f;    // Y 轴重力分量（m/s²）
        bool  m_SelfCollision = false;     // 是否开启自碰撞（NvCloth 当前保留字段）
        float m_WeldTolerance = 1e-5f;     // 顶点焊接网格精度（与 VansClothNode 保持一致）

        // ── 固定点数据（局部空间位置坐标列表）─────────────────────────────
        // 存储固定粒子的网格局部空间坐标，对模型重导出具有鲁棒性。
        // 运行时由 VansClothMeshPrep 在局部空间做近邻匹配并转换为粒子索引。
        std::vector<glm::vec3> m_PinnedLocalPositions;

        // 固定点近邻匹配容差（局部空间单位），默认 0.01 m
        float m_PinnedMatchTolerance = 0.01f;

        // ── V2：骨骼跟随配置 ────────────────────────────────────────────────
        // 启用后固定点跟随骨骼矩阵而非整体 RenderNode Transform
        bool m_FollowBones = false;

        // 参考骨骼 FBX 文件路径（相对于引擎根目录）。
        // 仅编辑器使用，用于在视口中加载并显示骨架供用户对齐。
        // 运行时 ClothNode 从关联的 AnimationNode 直接获取骨骼矩阵。
        std::string m_ReferenceSkeletonPath;

        // 骨骼到布料模型的偏移变换（仅编辑器使用）。
        // 含义：将骨骼从骨骼 FBX 局部空间变换到布料模型局部空间，
        //   用于在编辑器视口中正确对齐骨架与布料网格，
        //   运行时不需要此字段（运行时骨骼已在世界空间中）。
        struct SkeletonOffsetTransform
        {
            glm::vec3 m_Position = glm::vec3(0.0f);  // 平移偏移（布料模型局部空间）
            glm::vec3 m_Rotation = glm::vec3(0.0f);  // 欧拉角旋转（度，XYZ 顺序）
            glm::vec3 m_Scale    = glm::vec3(1.0f);  // 缩放
        };
        SkeletonOffsetTransform m_SkeletonOffset;

        // 每个固定点的骨骼绑定信息（与 m_PinnedLocalPositions 平行索引）。
        // 由编辑器「自动计算绑定」功能填充，支持多骨骼混合权重（最多 4 根）。
        struct PinBoneBinding
        {
            // 多骨骼绑定列表（按权重降序排列，权重和为 1.0）
            // 若 boneNames 为空则退化为单骨骼
            std::vector<std::string> m_BoneNames;
            std::vector<float>       m_Weights;
        };
        std::vector<PinBoneBinding> m_PinnedBoneBindings;

        // SkeletonOffset → glm::mat4
        glm::mat4 GetSkeletonOffsetMatrix() const;

        // ── 序列化 / 反序列化 ───────────────────────────────────────────────
        void ResetToDefaults();
    };

}
