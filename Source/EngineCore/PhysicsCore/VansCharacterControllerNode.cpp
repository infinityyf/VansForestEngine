#define GLM_ENABLE_EXPERIMENTAL
#include "VansCharacterControllerNode.h"
#include "VansPhysics.h"
#include "VansPhysicsNativeAccess.h"
#include "VansCollisionFilter.h"
#include "VansRagdollSystem.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansThreadContract.h"
#include "../SceneRuntime/Transform/VansTransformStore.h"
#include "../Util/VansLog.h"
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace VansEngine
{
	using namespace physx;

	struct VansCharacterControllerNode::NativeState
	{
		PxCapsuleController* controller = nullptr;
		PxFilterData filterData;
		PxControllerCollisionFlags lastCollisionFlags{ 0 };
	};

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
        : m_Native(std::make_unique<NativeState>())
    {
    }

    VansCharacterControllerNode::~VansCharacterControllerNode()
    {
        Release();
    }

    bool VansCharacterControllerNode::Initialize(
        const CharControllerProperties& props,
        uint32_t transformID,
        const glm::vec3& spawnPos)
    {
		auto& physics = VansPhysicsSystem::GetInstance();
		PxControllerManager* manager =
			VansPhysicsNativeAccess::ControllerManager(physics);
		PxMaterial* defaultMaterial =
			VansPhysicsNativeAccess::DefaultMaterial(physics);
		if (!manager || !defaultMaterial)
		{
			VANS_LOG_ERROR(
				"[VansCharacterControllerNode] Physics runtime is not initialized");
			return false;
		}

        m_Properties  = props;
        m_TransformID = transformID;
		if (!VansCollisionFilter::Build(
			props.m_LayerName, VansCollisionFilter::None, 0u, m_Native->filterData))
		{
			VANS_LOG_ERROR("[VansCharacterControllerNode] Unknown collision layer '"
				<< props.m_LayerName << "'");
			return false;
		}

        PxCapsuleControllerDesc desc;
        desc.radius        = props.m_Radius;
        desc.height        = props.m_Height;
        desc.slopeLimit    = props.m_SlopeLimit;
        desc.stepOffset    = props.m_StepOffset;
        desc.contactOffset = props.m_ContactOffset;
        desc.upDirection   = PxVec3(props.m_UpDirection.x,
                                    props.m_UpDirection.y,
                                    props.m_UpDirection.z);
        desc.climbingMode  =
			props.m_ClimbingMode == VansCharacterClimbingMode::Constrained
				? PxCapsuleClimbingMode::eCONSTRAINED
				: PxCapsuleClimbingMode::eEASY;
        desc.material      = defaultMaterial;
        desc.position      = PxExtendedVec3(
            static_cast<double>(spawnPos.x),
            static_cast<double>(spawnPos.y),
            static_cast<double>(spawnPos.z));
        desc.reportCallback = nullptr;

        m_Native->controller = static_cast<PxCapsuleController*>(
            manager->createController(desc));

        if (!m_Native->controller)
        {
            VANS_LOG_ERROR("[VansCharacterControllerNode] createController 失败");
            return false;
        }

        // 创建成功后将碰撞层 FilterData 设置到底层 Shape
        // （PxCapsuleControllerDesc 不支持直接在 desc 上设置 queryFilterData）
        {
            PxRigidDynamic* actor = m_Native->controller->getActor();
            if (actor)
            {
                const PxU32 shapeCount = actor->getNbShapes();
                std::vector<PxShape*> shapes(shapeCount);
                actor->getShapes(shapes.data(), shapeCount);
                for (PxShape* shape : shapes)
                {
                    if (shape)
                    {
                        shape->setSimulationFilterData(m_Native->filterData);
                        shape->setQueryFilterData(m_Native->filterData);
                    }
                }
            }
        }

		if (m_TransformID != UINT32_MAX)
		{
			const auto& initialTransform =
				Vans::VansTransformStore::Read(m_TransformID);
			m_Locomotion.Clear(
				initialTransform.m_Position, initialTransform.m_Rotation.y);
		}
		else
		{
			m_Locomotion.Clear(glm::vec3(0.0f), 0.0f);
		}
		DiscardPendingMove();
		m_GameplayMovementBlockCount = 0;
		m_Native->lastCollisionFlags = PxControllerCollisionFlags(0);
		m_Enabled = true;
        return true;
    }

    void VansCharacterControllerNode::Release()
    {
        if (m_Native->controller)
        {
            m_Native->controller->release();
            m_Native->controller = nullptr;
        }
		DiscardPendingMove();
		m_Locomotion.Clear(glm::vec3(0.0f), 0.0f);
		m_GameplayMovementBlockCount = 0;
		m_Native->lastCollisionFlags = PxControllerCollisionFlags(0);
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
		VANS_ASSERT_MAIN_THREAD();
		VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
        if (!m_Native->controller || !m_Enabled)
            return;

        // ── Ragdoll 接管路径 ───────────────────────────────────────────
        // 若已绑定 AnimNode 且处于 Physics/Blend 模式，跳过 move()，改用 setPosition 瞬移
        if (m_FollowRagdollKey.IsValid())
        {
            glm::vec3 boneWorldPos;
            if (VansEngine::VansRagdollSystem::GetInstance().GetFollowBoneWorldPositionLocked(
                    m_FollowRagdollKey, m_FollowRagdollBone, boneWorldPos))
            {
                // boneWorldPos 为根骨骼刚体质心。
                // m_Properties.m_PositionOffset 将胶囊中心对齐到骨骼附近，可在 JSON 中微调。
                SetPosition(boneWorldPos + m_Properties.m_PositionOffset);
                SyncTransformFromController();
                return;
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
            PxControllerFilters filters(&m_Native->filterData, &queryFilterCallback, nullptr);
			m_Native->lastCollisionFlags = m_Native->controller->move(disp, 0.001f, m_PendingDt, filters);
			const glm::vec3 positionAfter = GetPosition();
			glm::vec3 resolvedPlanarDelta = positionAfter - positionBefore;
			resolvedPlanarDelta.y = 0.0f;
			if (resolvedDt > 0.0001f)
			{
				const glm::vec3 transformPosition =
					positionAfter - m_Properties.m_PositionOffset;
				m_Locomotion.RecordResolvedMotion(
					resolvedDt,
					transformPosition,
					resolvedPlanarDelta / resolvedDt,
					glm::vec3(m_PendingDisplacement.x, 0.0f,
						m_PendingDisplacement.z) / resolvedDt);
			}

			DiscardPendingMove();
        }

        // 无论是否有待执行位移，每帧都将 PhysX 位置同步回 Transform
        SyncTransformFromController();
    }

    void VansCharacterControllerNode::SetPosition(const glm::vec3& pos)
    {
        if (!m_Native->controller) return;
        m_Native->controller->setPosition(PxExtendedVec3(
            static_cast<double>(pos.x),
            static_cast<double>(pos.y),
            static_cast<double>(pos.z)));
		DiscardPendingMove();
		float facingYaw = 0.0f;
		if (m_TransformID != UINT32_MAX &&
			Vans::VansTransformStore::IsAllocated(m_TransformID))
		{
			facingYaw = Vans::VansTransformStore::Read(m_TransformID).m_Rotation.y;
		}
		m_Locomotion.ResetMotion(pos - m_Properties.m_PositionOffset, facingYaw);
		m_Native->lastCollisionFlags = PxControllerCollisionFlags(0);
    }

    glm::vec3 VansCharacterControllerNode::GetPosition() const
    {
        if (!m_Native->controller) return glm::vec3(0.0f);
        const PxExtendedVec3& p = m_Native->controller->getPosition();
        return glm::vec3(
            static_cast<float>(p.x),
            static_cast<float>(p.y),
            static_cast<float>(p.z));
    }

    bool VansCharacterControllerNode::IsGrounded() const
    {
        return m_Native->lastCollisionFlags.isSet(PxControllerCollisionFlag::eCOLLISION_DOWN);
    }

	void VansCharacterControllerNode::SetMotionIntent(const Vans::VansCharacterMotionIntent& intent)
	{
		m_Locomotion.SetIntent(intent);
	}

	void VansCharacterControllerNode::AcquireGameplayMovementBlock()
	{
		if (m_GameplayMovementBlockCount < UINT32_MAX)
			++m_GameplayMovementBlockCount;
	}

	void VansCharacterControllerNode::ReleaseGameplayMovementBlock()
	{
		if (m_GameplayMovementBlockCount > 0)
			--m_GameplayMovementBlockCount;
	}

	void VansCharacterControllerNode::PrepareLocomotion(
		float dt, const Vans::VansCharacterMotionSettings& settings)
	{
		if (m_TransformID == UINT32_MAX)
			return;

		const Vans::VansTransform& transform =
			Vans::VansTransformStore::Read(m_TransformID);
		m_Locomotion.Prepare(
			dt,
			settings,
			transform.m_Position,
			transform.m_Rotation.y,
			IsGrounded(),
			IsGameplayMovementBlocked());
	}

	void VansCharacterControllerNode::ResolveLocomotion(
		const glm::vec3& animationRootDelta,
		const glm::quat& animationRootRotation,
		bool rootMotionValid,
		const Vans::VansLocomotionAuthority& authority,
		const Vans::VansCharacterMotionSettings& settings,
		const glm::vec3& animationToWorldScale)
	{
		VANS_ASSERT_MAIN_THREAD();
		VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
		if (m_TransformID == UINT32_MAX)
			return;
		Vans::VansTransform transform =
			Vans::VansTransformStore::Read(m_TransformID);
		const Vans::VansCharacterLocomotionResult result = m_Locomotion.Resolve(
			animationRootDelta,
			animationRootRotation,
			rootMotionValid,
			authority,
			settings,
			animationToWorldScale,
			transform.m_Rotation.y);
		if (!result.hasMove)
			return;

		m_PendingDisplacement = result.displacementWorld;
		m_PendingDt = result.deltaTime;
		m_HasPendingMove = true;
		transform.m_Rotation.y = result.facingYaw;
		Vans::VansTransformStore::Write(m_TransformID, transform);
		Vans::VansTransformStore::MarkDirty(m_TransformID);
	}

    void VansCharacterControllerNode::SyncControllerFromTransform()
    {
        if (!m_Native->controller || m_TransformID == UINT32_MAX) return;

        const Vans::VansTransform& t =
            Vans::VansTransformStore::Read(m_TransformID);
        glm::vec3 capsuleCenter = t.m_Position + m_Properties.m_PositionOffset;
        SetPosition(capsuleCenter);
    }

	void VansCharacterControllerNode::DiscardPendingMove()
	{
		m_PendingDisplacement = glm::vec3(0.0f);
		m_PendingDt = 0.0f;
		m_HasPendingMove = false;
	}

    void VansCharacterControllerNode::SyncTransformFromController()
    {
        if (!m_Native->controller || m_TransformID == UINT32_MAX) return;

        const PxExtendedVec3& pxPos = m_Native->controller->getPosition();
        glm::vec3 capsuleCenter(
            static_cast<float>(pxPos.x),
            static_cast<float>(pxPos.y),
            static_cast<float>(pxPos.z));

        // 胶囊中心 → Transform 原点（减去 positionOffset）
        glm::vec3 transformPos = capsuleCenter - m_Properties.m_PositionOffset;

        Vans::VansTransform t =
            Vans::VansTransformStore::Read(m_TransformID);
        t.m_Position = transformPos;
		Vans::VansTransformStore::Write(m_TransformID, t);

        // 标记 Dirty，通知渲染层更新 GPU 数据
        Vans::VansTransformStore::MarkDirty(m_TransformID);
    }

    void VansCharacterControllerNode::SetFollowRagdoll(
        const Vans::VansRagdollKey& key, const std::string& rootBoneName)
    {
        m_FollowRagdollKey      = key;
        m_FollowRagdollBone     = rootBoneName;
    }

    void VansCharacterControllerNode::ClearFollowRagdoll()
    {
        m_FollowRagdollKey      = {};
        m_FollowRagdollBone     = "pelvis";
    }

    void VansCharacterControllerNode::SetPendingFollowRagdoll(bool enable, const std::string& bone)
    {
        m_PendingFollowRagdoll     = enable;
        m_PendingFollowRagdollBone = bone;
    }

} // namespace VansEngine
