#define GLM_ENABLE_EXPERIMENTAL
#include "VansCharacterControllerNode.h"
#include "VansCollisionLayerManager.h"
#include "VansRagdollSystem.h"
#include "../ScriptCore/VansTransform.h"
#include "../Util/VansLog.h"
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace VansEngine
{
    namespace
    {
        class VansCCTQueryFilterCallback final : public PxQueryFilterCallback
        {
        public:
            PxQueryHitType::Enum preFilter(const PxFilterData& filterData,
                                           const PxShape* shape,
                                           const PxRigidActor* actor,
                                           PxHitFlags& queryFlags) override
            {
                (void)queryFlags;
                return FilterShape(filterData, shape, actor);
            }

            PxQueryHitType::Enum postFilter(const PxFilterData& filterData,
                                            const PxQueryHit& hit,
                                            const PxShape* shape,
                                            const PxRigidActor* actor) override
            {
                (void)hit;
                return FilterShape(filterData, shape, actor);
            }

        private:
            PxQueryHitType::Enum FilterShape(const PxFilterData& filterData,
                                             const PxShape* shape,
                                             const PxRigidActor* actor) const
            {
                (void)actor;
                if (!shape)
                    return PxQueryHitType::eNONE;

                const PxFilterData targetData = shape->getQueryFilterData();
                const bool targetIsTrigger = (targetData.word2 & 0x1u) != 0u;
                if (targetIsTrigger)
                    return PxQueryHitType::eNONE;

                const uint32_t layerA = filterData.word0;
                const uint32_t layerB = targetData.word0;
                if (layerA >= 32u || layerB >= 32u)
                    return PxQueryHitType::eNONE;

                const uint32_t maskA  = filterData.word1;
                const uint32_t maskB  = targetData.word1;
                if (!((maskA & (1u << layerB)) && (maskB & (1u << layerA))))
                    return PxQueryHitType::eNONE;

                return PxQueryHitType::eBLOCK;
            }
        };
    }

    VansCharacterControllerNode::VansCharacterControllerNode()
        : m_LastCollisionFlags(0)
    {
    }

    VansCharacterControllerNode::~VansCharacterControllerNode()
    {
        Release();
    }

    bool VansCharacterControllerNode::Initialize(
        const CharControllerProperties& props,
        uint32_t transformID,
        PxControllerManager* manager,
        PxMaterial* defaultMaterial,
        const glm::vec3& spawnPos)
    {
        m_Properties  = props;
        m_TransformID = transformID;

        PxCapsuleControllerDesc desc;
        desc.radius        = props.m_Radius;
        desc.height        = props.m_Height;
        desc.slopeLimit    = props.m_SlopeLimit;
        desc.stepOffset    = props.m_StepOffset;
        desc.contactOffset = props.m_ContactOffset;
        desc.upDirection   = PxVec3(props.m_UpDirection.x,
                                    props.m_UpDirection.y,
                                    props.m_UpDirection.z);
        desc.climbingMode  = props.m_ClimbingMode;
        desc.material      = defaultMaterial;
        desc.position      = PxExtendedVec3(
            static_cast<double>(spawnPos.x),
            static_cast<double>(spawnPos.y),
            static_cast<double>(spawnPos.z));
        desc.reportCallback = nullptr;

        m_Controller = static_cast<PxCapsuleController*>(
            manager->createController(desc));

        if (!m_Controller)
        {
            VANS_LOG_ERROR("[VansCharacterControllerNode] createController 失败");
            return false;
        }

        // 创建成功后将碰撞层 FilterData 设置到底层 Shape
        // （PxCapsuleControllerDesc 不支持直接在 desc 上设置 queryFilterData）
        {
            m_FilterData.word0 = (props.m_LayerIndex >= 0)
                                ? static_cast<uint32_t>(props.m_LayerIndex) : 0u;
            m_FilterData.word1 = VansCollisionLayerManager::Get().GetCollisionMask(props.m_LayerIndex);
            m_FilterData.word2 = 0;
            m_FilterData.word3 = 0;

            PxRigidDynamic* actor = m_Controller->getActor();
            if (actor)
            {
                const PxU32 shapeCount = actor->getNbShapes();
                std::vector<PxShape*> shapes(shapeCount);
                actor->getShapes(shapes.data(), shapeCount);
                for (PxShape* shape : shapes)
                {
                    if (shape)
                    {
                        shape->setSimulationFilterData(m_FilterData);
                        shape->setQueryFilterData(m_FilterData);
                    }
                }
            }
        }

		if (m_TransformID != UINT32_MAX)
		{
			const auto& initialTransform =
				VansGraphics::VansTransformStore::GetTransform(m_TransformID);
			m_TrajectoryGenerator.Reset(
				initialTransform.m_Position, initialTransform.m_Rotation.y);
		}
		m_Enabled = true;
        VANS_LOG("[VansCharacterControllerNode] 初始化成功，transformID=" << transformID);
        return true;
    }

    void VansCharacterControllerNode::Release()
    {
        if (m_Controller)
        {
            m_Controller->release();
            m_Controller = nullptr;
        }
		m_TrajectoryGenerator.Reset(glm::vec3(0.0f), 0.0f);
        m_Enabled = false;
    }

    void VansCharacterControllerNode::QueueMove(const glm::vec3& displacement, float dt)
    {
        // 允许在一帧内多次调用，各次位移叠加
        m_PendingDisplacement += displacement;
        m_PendingDt            = dt;  // 以最后一次 dt 为准
        m_HasPendingMove       = true;
    }

    void VansCharacterControllerNode::FlushMoveAndSync()
    {
        if (!m_Controller || !m_Enabled)
            return;

        // ── Ragdoll 接管路径 ───────────────────────────────────────────
        // 若已绑定 AnimNode 且处于 Physics/Blend 模式，跳过 move()，改用 setPosition 瞬移
        if (m_FollowRagdollAnimNode)
        {
            VansEngine::RagdollDriveMode mode =
                VansEngine::VansRagdollSystem::GetInstance().GetDriveMode(m_FollowRagdollAnimNode);
            if (mode == VansEngine::RagdollDriveMode::Physics ||
                mode == VansEngine::RagdollDriveMode::Blend)
            {
                glm::vec3 boneWorldPos;
                if (VansEngine::VansRagdollSystem::GetInstance().GetBoneWorldPosition(
                        m_FollowRagdollAnimNode, m_FollowRagdollBone, boneWorldPos))
                {
                    // boneWorldPos 为根骨骼刚体质心。
                    // m_Properties.m_PositionOffset 将胶囊中心对齐到骨骼附近，可在 JSON 中微调。
                    SetPosition(boneWorldPos + m_Properties.m_PositionOffset);
                    // 丢弃本帧脚本排队的位移（脚本仍可调用 QueueMove，但本帧被忽略）
                    m_PendingDisplacement = { 0.0f, 0.0f, 0.0f };
                    m_PendingDt           = 0.0f;
                    m_HasPendingMove      = false;
                    SyncTransformFromController();
                    return;
                }
            }
        }

        // ── 正常脚本驱动路径 ───────────────────────────────────────────
		if (m_HasPendingMove)
		{
			const glm::vec3 positionBefore = GetPosition();
			const float resolvedDt = m_PendingDt;
			PxVec3 disp(m_PendingDisplacement.x,
                        m_PendingDisplacement.y,
                        m_PendingDisplacement.z);
            VansCCTQueryFilterCallback queryFilterCallback;
            PxControllerFilters filters(&m_FilterData, &queryFilterCallback, nullptr);
			m_LastCollisionFlags = m_Controller->move(disp, 0.001f, m_PendingDt, filters);
			const glm::vec3 positionAfter = GetPosition();
			glm::vec3 resolvedPlanarDelta = positionAfter - positionBefore;
			resolvedPlanarDelta.y = 0.0f;
			if (resolvedDt > 0.0001f)
			{
				const glm::vec3 transformPosition =
					positionAfter - m_Properties.m_PositionOffset;
				m_TrajectoryGenerator.RecordResolvedMotion(
					resolvedDt,
					transformPosition,
					resolvedPlanarDelta / resolvedDt,
					glm::vec3(m_PendingDisplacement.x, 0.0f,
						m_PendingDisplacement.z) / resolvedDt);
			}

            // 重置缓冲区
            m_PendingDisplacement = { 0.0f, 0.0f, 0.0f };
            m_PendingDt           = 0.0f;
            m_HasPendingMove      = false;
        }

        // 无论是否有待执行位移，每帧都将 PhysX 位置同步回 Transform
        SyncTransformFromController();
    }

    void VansCharacterControllerNode::SetPosition(const glm::vec3& pos)
    {
        if (!m_Controller) return;
        m_Controller->setPosition(PxExtendedVec3(
            static_cast<double>(pos.x),
            static_cast<double>(pos.y),
            static_cast<double>(pos.z)));
    }

    glm::vec3 VansCharacterControllerNode::GetPosition() const
    {
        if (!m_Controller) return glm::vec3(0.0f);
        const PxExtendedVec3& p = m_Controller->getPosition();
        return glm::vec3(
            static_cast<float>(p.x),
            static_cast<float>(p.y),
            static_cast<float>(p.z));
    }

    bool VansCharacterControllerNode::IsGrounded() const
    {
        return m_LastCollisionFlags.isSet(PxControllerCollisionFlag::eCOLLISION_DOWN);
    }

	void VansCharacterControllerNode::SetMotionIntent(const Vans::VansCharacterMotionIntent& intent)
	{
		m_MotionIntent = intent;
		const float length = glm::length(m_MotionIntent.moveInputLocal);
		if (length > 1.0f)
			m_MotionIntent.moveInputLocal /= length;
		m_MotionIntent.desiredSpeed = (std::max)(0.0f, m_MotionIntent.desiredSpeed);
		m_MotionIntent.valid = true;
	}

	void VansCharacterControllerNode::PrepareLocomotion(
		float dt, const Vans::VansCharacterMotionSettings& settings)
	{
		if (!m_MotionIntent.valid || m_TransformID == UINT32_MAX)
			return;

		m_LocomotionDt = (std::max)(dt, 0.0f);
		const VansGraphics::VansTransform& transform =
			VansGraphics::VansTransformStore::GetTransform(m_TransformID);
		m_TrajectoryGenerator.Update(
			m_LocomotionDt, m_MotionIntent, settings,
			transform.m_Position, transform.m_Rotation.y);

		if (IsGrounded() && m_VerticalVelocity < 0.0f)
			m_VerticalVelocity = -0.5f;
		if (m_MotionIntent.jumpRequested && IsGrounded())
			m_VerticalVelocity = m_MotionIntent.jumpSpeed;
		else
			m_VerticalVelocity -= (std::max)(0.0f, m_MotionIntent.gravity) * m_LocomotionDt;

	}

	void VansCharacterControllerNode::ResolveLocomotion(
		const glm::vec3& animationRootDelta,
		const glm::quat& animationRootRotation,
		bool rootMotionValid,
		bool prefersRootMotion,
		const Vans::VansCharacterMotionSettings& settings)
	{
		if (!m_MotionIntent.valid || m_TransformID == UINT32_MAX)
			return;
		VansGraphics::VansTransform& transform =
			VansGraphics::VansTransformStore::GetTransform(m_TransformID);
		const glm::vec3 capsuleDelta =
			m_TrajectoryGenerator.GetPlannedVelocityWorld() * m_LocomotionDt;
		glm::vec3 rootWorldDelta(0.0f);
		float rootYawDelta = 0.0f;
		if (rootMotionValid)
		{
			const glm::vec3 rootLocal = Vans::AnimationToEngineLocalPlanar(animationRootDelta);
			const float yaw = glm::radians(transform.m_Rotation.y);
			rootWorldDelta = glm::mat3(glm::rotate(
				glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f))) *
				(rootLocal * settings.rootMotionToWorldScale);
			rootYawDelta = glm::degrees(glm::eulerAngles(animationRootRotation)).z;
		}

		float rootWeight = 0.0f;
		if (settings.driveMode == Vans::VansLocomotionDriveMode::RootMotion)
		{
			// RootMotion mode has a single transform authority. If an authored
			// interval is invalid, hold planar translation/yaw for that frame so
			// input-driven capsule motion cannot create visible foot sliding.
			rootWeight = 1.0f;
		}
		else if (settings.driveMode == Vans::VansLocomotionDriveMode::Hybrid && rootMotionValid)
			rootWeight = prefersRootMotion
				? settings.transitionRootMotionWeight : settings.loopRootMotionWeight;
		rootWeight = glm::clamp(rootWeight, 0.0f, 1.0f);

		m_PendingDisplacement = glm::mix(capsuleDelta, rootWorldDelta, rootWeight);
		m_PendingDisplacement.y = m_VerticalVelocity * m_LocomotionDt;
		m_PendingDt = m_LocomotionDt;
		m_HasPendingMove = true;

		const float facingDelta = std::remainder(
			m_TrajectoryGenerator.GetPlannedFacingYaw() - transform.m_Rotation.y,
			360.0f);
		const float rootRotationWeight = glm::clamp(
			rootWeight * settings.rootRotationWeight, 0.0f, 1.0f);
		transform.m_Rotation.y += glm::mix(facingDelta, rootYawDelta, rootRotationWeight);
		VansGraphics::VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
	}

    void VansCharacterControllerNode::SyncControllerFromTransform()
    {
        if (!m_Controller || m_TransformID == UINT32_MAX) return;

        // 外部系统（例如 Root Motion）接管 Transform 后，旧的脚本位移已经失效。
        m_PendingDisplacement = { 0.0f, 0.0f, 0.0f };
        m_PendingDt = 0.0f;
        m_HasPendingMove = false;
        const VansGraphics::VansTransform& t =
            VansGraphics::VansTransformStore::GetTransform(m_TransformID);
		m_TrajectoryGenerator.Reset(t.m_Position, t.m_Rotation.y);
        glm::vec3 capsuleCenter = t.m_Position + m_Properties.m_PositionOffset;
        SetPosition(capsuleCenter);
    }

    void VansCharacterControllerNode::SyncTransformFromController()
    {
        if (!m_Controller || m_TransformID == UINT32_MAX) return;

        const PxExtendedVec3& pxPos = m_Controller->getPosition();
        glm::vec3 capsuleCenter(
            static_cast<float>(pxPos.x),
            static_cast<float>(pxPos.y),
            static_cast<float>(pxPos.z));

        // 胶囊中心 → Transform 原点（减去 positionOffset）
        glm::vec3 transformPos = capsuleCenter - m_Properties.m_PositionOffset;

        VansGraphics::VansTransform& t =
            VansGraphics::VansTransformStore::GetTransform(m_TransformID);
        t.m_Position = transformPos;

        // 标记 Dirty，通知渲染层更新 GPU 数据
        VansGraphics::VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
    }

    void VansCharacterControllerNode::SetFollowRagdoll(
        VansGraphics::VansAnimationNode* animNode, const std::string& rootBoneName)
    {
        m_FollowRagdollAnimNode = animNode;
        m_FollowRagdollBone     = rootBoneName;
    }

    void VansCharacterControllerNode::ClearFollowRagdoll()
    {
        m_FollowRagdollAnimNode = nullptr;
        m_FollowRagdollBone     = "pelvis";
    }

    void VansCharacterControllerNode::SetPendingFollowRagdoll(bool enable, const std::string& bone)
    {
        m_PendingFollowRagdoll     = enable;
        m_PendingFollowRagdollBone = bone;
    }

} // namespace VansEngine
