#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <PxPhysicsAPI.h>
#include <characterkinematic/PxControllerManager.h>
#include <glm/glm.hpp>
#include <string>

using namespace physx;

namespace VansEngine
{
    // ── 角色控制器配置参数（对应 JSON "charController" 字段）──────────────
    struct CharControllerProperties
    {
        // ── 胶囊形状 ─────────────────────────────────────────────────────
        float     m_Radius          = 0.5f;    // 胶囊半径（米）
        float     m_Height          = 1.0f;    // 两端半球之间的圆柱高度（米）
        //   总高度 = m_Height + 2 * m_Radius

        // ── 运动参数 ─────────────────────────────────────────────────────
        float     m_SlopeLimit      = 0.707f;  // 可行走斜坡最大角余弦值（默认 cos45°）
        float     m_StepOffset      = 0.3f;    // 可自动跨越的台阶高度（米）
        float     m_ContactOffset   = 0.08f;   // 皮肤厚度（skin width），用于穿透修正

        // ── 方向 ─────────────────────────────────────────────────────────
        glm::vec3 m_UpDirection     = { 0.0f, 1.0f, 0.0f };

        // ── 攀爬模式 ─────────────────────────────────────────────────────
        // eEASY     : 按碰撞法线自然攀爬
        // eCONSTRAINED : 受 stepOffset 限制
        PxCapsuleClimbingMode::Enum m_ClimbingMode = PxCapsuleClimbingMode::eEASY;

        // ── 碰撞层 ───────────────────────────────────────────────────────
        std::string m_LayerName     = "Default";
        int         m_LayerIndex    = 0;

        // ── 位置偏移 ─────────────────────────────────────────────────────
        // PxCapsuleController 的 position 指胶囊中心。
        // m_PositionOffset 可将 Transform 的 m_Position（通常为脚底/质心）
        // 偏移到物理胶囊中心。
        // 例：Transform 在脚底 → m_PositionOffset = (0, height/2 + radius, 0)
        glm::vec3 m_PositionOffset  = { 0.0f, 0.0f, 0.0f };
    };

    // ── 角色控制器节点 ────────────────────────────────────────────────────
    // 封装一个 PhysX PxCapsuleController，并负责在每帧将物理位置
    // 同步回 VansTransformStore。
    // 生命周期由 VansScene::m_CharControllerNodes 管理。
    class VansCharacterControllerNode
    {
    public:
        VansCharacterControllerNode();
        ~VansCharacterControllerNode();

        // ── 生命周期 ──────────────────────────────────────────────────────
        // 由 VansScene::LoadSingleCharControllerNode 调用。
        // manager   : 来自 VansPhysicsSystem::GetControllerManager()
        // spawnPos  : 胶囊中心世界坐标 (= transform.m_Position + positionOffset)
        bool Initialize(const CharControllerProperties& props,
                        uint32_t transformID,
                        PxControllerManager* manager,
                        PxMaterial* defaultMaterial,
                        const glm::vec3& spawnPos);
        void Release();

        // ── 位移队列（供 Python/C++ 脚本调用）──────────────────────────────
        // 将本帧期望的位移加入缓冲区，UpdateCharControllerTransforms() 会在
        // SimulationMutex 锁内统一提交 PxController::move()。
        // displacement : 本帧期望的世界坐标偏移（已包含重力分量）
        // dt           : 本帧时间步长（秒）
        void QueueMove(const glm::vec3& displacement, float dt);

        // ── 内部：提交 move() + 同步 Transform（由 UpdateCharControllerTransforms 调用）──
        // 调用方需已持有 SimulationMutex。
        void FlushMoveAndSync();

        // ── 瞬移 ─────────────────────────────────────────────────────────
        // pos 为胶囊中心坐标（忽略 positionOffset）
        void SetPosition(const glm::vec3& pos);

        // ── 状态查询 ──────────────────────────────────────────────────────
        glm::vec3 GetPosition() const;          // 返回胶囊中心坐标
        bool IsGrounded() const;                // COLLISION_DOWN 标志
        bool IsEnabled() const { return m_Enabled; }
        uint32_t GetTransformID() const { return m_TransformID; }
        const CharControllerProperties& GetProperties() const { return m_Properties; }
        PxControllerCollisionFlags GetLastCollisionFlags() const { return m_LastCollisionFlags; }

        // ── Transform 同步（编辑器瞬移用）────────────────────────────────
        // 将 VansTransformStore 当前位置推送到 PhysX
        void SyncControllerFromTransform();

    private:
        // ── 将 PhysX 当前胶囊位置写回 VansTransformStore ─────────────────
        // （胶囊中心 - positionOffset = Transform 原点）
        void SyncTransformFromController();

    private:
        CharControllerProperties          m_Properties;
        PxCapsuleController*              m_Controller        = nullptr;
        uint32_t                          m_TransformID       = UINT32_MAX;  // UINT32_MAX 表示「尚未绑定」
        bool                              m_Enabled           = false;
        PxControllerCollisionFlags        m_LastCollisionFlags;

        // ── 待执行位移缓冲 ────────────────────────────────────────────────
        glm::vec3                         m_PendingDisplacement = { 0.0f, 0.0f, 0.0f };
        float                             m_PendingDt           = 0.0f;
        bool                              m_HasPendingMove      = false;
    };

} // namespace VansEngine
