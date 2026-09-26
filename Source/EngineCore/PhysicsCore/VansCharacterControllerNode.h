#pragma once

#include "../SceneRuntime/Transform/VansTransformStore.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include "../VansNode.h"
#include "../RuntimeCore/VansCharacterLocomotionResolver.h"
#include "../RuntimeCore/VansRagdollPose.h"

namespace VansEngine
{
	enum class VansCharacterClimbingMode : std::uint8_t
	{
		Easy,
		Constrained
	};

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
        VansCharacterClimbingMode m_ClimbingMode = VansCharacterClimbingMode::Easy;

        // ── 碰撞层 ───────────────────────────────────────────────────────
        std::string m_LayerName     = "Default";

        // ── 位置偏移 ─────────────────────────────────────────────────────
        // 原生 capsule controller 的 position 指胶囊中心。
        // m_PositionOffset 可将 Transform 的 m_Position（通常为脚底/质心）
        // 偏移到物理胶囊中心。
        // 例：Transform 在脚底 → m_PositionOffset = (0, height/2 + radius, 0)
        glm::vec3 m_PositionOffset  = { 0.0f, 0.0f, 0.0f };
    };

    // ── 角色控制器节点 ────────────────────────────────────────────────────
    // 封装原生 capsule controller，并负责在每帧将物理位置
    // 同步回 Vans::VansTransformStore。
    // 生命周期由 VansScene::m_CharControllerNodes 管理。
    class VansCharacterControllerNode : public VansGraphics::VansNode
    {
    public:
        VansCharacterControllerNode();
        ~VansCharacterControllerNode();

        // ── 生命周期 ──────────────────────────────────────────────────────
        // 由 VansScenePhysicsComponentBuilder 调用。
        // spawnPos  : 胶囊中心世界坐标 (= transform.m_Position + positionOffset)
        bool Initialize(const CharControllerProperties& props,
                        uint32_t transformID,
                        const glm::vec3& spawnPos);
        void Release();

        // ── 位移队列（供 Lua/C++ 脚本调用）──────────────────────────────
        // 将本帧期望的位移加入缓冲区，UpdateCharControllerTransforms() 会在
        // SimulationMutex 锁内统一提交 controller move。
        // displacement : 本帧期望的世界坐标偏移（已包含重力分量）
        // dt           : 本帧时间步长（秒）
        void QueueMove(const glm::vec3& displacement, float dt);

		// CCT 是角色世界 Transform 的唯一运行时提交者。RuntimeCore resolver
		// 已将 gameplay intent 与动画 Root Motion 合成为一条待碰撞位移；本节点
		// 不解释 Motion Matching 状态或重新选择位移权威。
		void SetMotionIntent(const Vans::VansCharacterMotionIntent& intent);
		void AcquireGameplayMovementBlock();
		void ReleaseGameplayMovementBlock();
		bool IsGameplayMovementBlocked() const { return m_GameplayMovementBlockCount > 0; }
		void PrepareLocomotion(float dt, const Vans::VansCharacterMotionSettings& settings);
		void ResolveLocomotion(const glm::vec3& animationRootDelta,
		                       const glm::quat& animationRootRotation,
		                       bool rootMotionValid,
		                       const Vans::VansLocomotionAuthority& authority,
		                       const Vans::VansCharacterMotionSettings& settings,
		                       const glm::vec3& animationToWorldScale);
		const Vans::VansCharacterTrajectory& GetTrajectory() const
		{
			return m_Locomotion.GetTrajectory();
		}
		bool HasMotionIntent() const { return m_Locomotion.HasIntent(); }

        // ── 内部：提交 move() + 同步 Transform（由 UpdateCharControllerTransforms 调用）──
        // 调用方需已持有 SimulationMutex。
        void FlushMoveAndSync();

        // ── 瞬移 ─────────────────────────────────────────────────────────
        // pos 为胶囊中心坐标（忽略 positionOffset）
        void SetPosition(const glm::vec3& pos);

        // ── 状态查询 ──────────────────────────────────────────────────────
        glm::vec3 GetPosition() const;          // 返回胶囊中心坐标
        bool IsGrounded() const;                // COLLISION_DOWN 标志
        // [迁移到 VansNode] IsEnabled 由基类提供
        uint32_t GetTransformID() const { return m_TransformID; }
        const CharControllerProperties& GetProperties() const { return m_Properties; }

        // ── Transform 同步（编辑器瞬移用）────────────────────────────────
        // 将 Vans::VansTransformStore 当前位置推送到 PhysX
        void SyncControllerFromTransform();

        // ── Ragdoll 接管接口 ────────────────────────────────────────
        // 绑定指定 Ragdoll，当其处于 Physics/Blend 模式时接管 CCT 位置。
        void SetFollowRagdoll(const Vans::VansRagdollKey& key,
                              const std::string& rootBoneName = "pelvis");
        void ClearFollowRagdoll();

        bool IsFollowRagdollEnabled() const { return m_FollowRagdollKey.IsValid(); }
        const std::string& GetFollowRagdollBone() const { return m_FollowRagdollBone; }

        // ── 场景加载器延迟绑定辅助接口 ──────────────────────────
        // charController 先于 animation/ragdoll 创建，第一阶段只记录意图
        void SetPendingFollowRagdoll(bool enable, const std::string& bone = "pelvis");
        bool HasPendingFollowRagdoll() const { return m_PendingFollowRagdoll; }
        const std::string& GetPendingFollowRagdollBone() const { return m_PendingFollowRagdollBone; }
        void ConsumePendingFollowRagdoll() { m_PendingFollowRagdoll = false; }

    private:
        struct NativeState;

        // ── 将 PhysX 当前胶囊位置写回 Vans::VansTransformStore ─────────────────
        // （胶囊中心 - positionOffset = Transform 原点）
        void SyncTransformFromController();
		void DiscardPendingMove();

    private:
        CharControllerProperties          m_Properties;
        std::unique_ptr<NativeState>       m_Native;
        uint32_t                          m_TransformID       = UINT32_MAX;  // UINT32_MAX 表示「尚未绑定」

        // ── 待执行位移缓冲 ────────────────────────────────────────────────
        glm::vec3                         m_PendingDisplacement = { 0.0f, 0.0f, 0.0f };
        float                             m_PendingDt           = 0.0f;
		bool                              m_HasPendingMove      = false;
		Vans::VansCharacterLocomotionResolver m_Locomotion;
		std::uint32_t                       m_GameplayMovementBlockCount = 0;
        // ── Ragdoll 接管 ───────────────────────────────────────────
        Vans::VansRagdollKey              m_FollowRagdollKey;
        std::string                       m_FollowRagdollBone        = "pelvis";

        // ── 场景加载器延迟绑定标志 ─────────────────────────────────
        bool                              m_PendingFollowRagdoll     = false;
        std::string                       m_PendingFollowRagdollBone = "pelvis";    };

} // namespace VansEngine
