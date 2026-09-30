#define GLM_ENABLE_EXPERIMENTAL
#include "VansCharacterControllerNode.h"
#include "VansPhysics.h"
#include "VansPhysicsNativeAccess.h"
#include "VansCollisionFilter.h"
#include "VansCollisionLayerManager.h"
#include "VansRagdollSystem.h"
#include "VansPhysicsEvents.h"
#include "VansCharacterSweepSolver.h"
#include "../EventCore/VansEventBus.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansThreadContract.h"
#include "../SceneRuntime/Transform/VansTransformStore.h"
#include "../Util/VansLog.h"
#include <../../GLM/gtc/matrix_transform.hpp>
#include <../../GLM/gtx/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <vector>

namespace VansEngine
{
	using namespace physx;

	struct VansCharacterControllerNode::NativeState
	{
		PxCapsuleController* controller = nullptr;
		PxFilterData filterData;
		PxControllerCollisionFlags lastCollisionFlags{ 0 };
		VansCharacterSweepState sweepState;
		VansCharacterGroundState groundState;
		VansPhysicsQueryHit movementBase;
		float baseRotationDeltaYaw = 0.0f;
		bool crouched=false;
		NativeState()
		{
			sweepState.randomSeed=static_cast<std::uint32_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
		}
	};

    namespace
    {
        class VansCCTQueryFilterCallback final : public PxQueryFilterCallback
        {
        public:
            explicit VansCCTQueryFilterCallback(const PxRigidActor* ignoredActor = nullptr,
                PxQueryHitType::Enum acceptedType = PxQueryHitType::eBLOCK)
                : m_IgnoredActor(ignoredActor), m_AcceptedType(acceptedType) {}
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
                if (!shape || actor == m_IgnoredActor)
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

                return m_AcceptedType;
            }
            const PxRigidActor* m_IgnoredActor;
            PxQueryHitType::Enum m_AcceptedType;
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
        m_CollisionEnabled = true;
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
        desc.userData = this;

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
		m_Native->groundState.valid=false;
		m_Native->movementBase={};
		m_Native->baseRotationDeltaYaw=0.0f;
		m_Native->crouched=false;
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
		m_Native->groundState.valid=false;
		m_Native->movementBase={};
		m_Native->baseRotationDeltaYaw=0.0f;
		m_Native->crouched=false;
		m_Native->lastCollisionFlags = PxControllerCollisionFlags(0);
        m_Enabled = false;
    }

    void VansCharacterControllerNode::QueueMove(const glm::vec3& displacement, float dt)
    {
        // 落地回调提交新移动时取消旧续算，不能把两次命令拼成同一物理帧。
        if (m_PendingLanding) DiscardPendingMove();
        // 允许在一帧内多次调用，各次位移叠加
        m_PendingDisplacement += displacement;
        m_PendingDt            = dt;  // 以最后一次 dt 为准
        m_HasPendingMove       = true;
        m_PendingModeledMotion = false;
        m_PendingSubsteps = false;
		m_PendingAnimationRootMotion = false;
    }

    VansCharacterMotionFlushResult VansCharacterControllerNode::FlushMoveAndSync()
    {
		VANS_ASSERT_MAIN_THREAD();
		VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
        if (!m_Native->controller || !m_Enabled)
        {
            DiscardPendingMove();
            return {};
        }

        // ── Ragdoll 接管路径 ───────────────────────────────────────────
        // 若已绑定 AnimNode 且处于 Physics/Blend 模式，跳过 move()，改用 setPosition 瞬移
        if (m_FollowRagdollKey.IsValid())
        {
            glm::vec3 boneWorldPos;
            if (VansEngine::VansRagdollSystem::GetInstance().GetFollowBoneWorldPositionLocked(
                    m_FollowRagdollKey, m_FollowRagdollBone, boneWorldPos))
            {
                DiscardPendingMove();
                // 读取骨骼原点，而不是形状中心，保持胶囊/角色 Transform 的相对位置。
                SetPosition(boneWorldPos + m_Properties.m_PositionOffset);
                SyncTransformFromController();
                return {};
            }
        }

        if (m_PendingLanding)
        {
            if (IsGameplayMovementBlocked())
            {
                DiscardPendingMove();
                SyncTransformFromController();
                return {};
            }
            m_Native->lastCollisionFlags |= PxControllerCollisionFlag::eCOLLISION_DOWN;
            m_Native->groundState.floor=FindCurrentFloorLocked();
            m_Native->groundState.valid=m_Native->groundState.floor.walkable;
            m_Native->groundState.triedLedgeMove=false;
            if (m_Native->groundState.valid) SaveMovementBaseLocked(m_Native->groundState.floor.hit);
            m_Locomotion.ResolveSweptMotionStep(m_PendingLanding->velocity,true,m_PendingLanding->unusedTime);
            m_PendingLanding.reset();
        }

        // ── 正常脚本驱动路径 ───────────────────────────────────────────
		// Observe physics-base changes that occurred after regular locomotion preparation.
		UpdateMovementBaseLocked();
		std::optional<VansCharacterMovementUpdatedEvent> result;
		if (m_HasPendingMove)
		{
			if (!m_FlushStarted)
			{
				m_FlushStartPosition = GetPosition();
				m_FlushStarted = true;
				m_Native->groundState.triedLedgeMove = false;
			}
			const glm::vec3 positionBefore = m_FlushStartPosition;
			const float resolvedDt = m_PendingDt;
			PxVec3 disp(m_PendingDisplacement.x,
                        m_PendingDisplacement.y,
                        m_PendingDisplacement.z);
            VansCCTQueryFilterCallback queryFilterCallback;
            PxControllerFilters filters(&m_Native->filterData, &queryFilterCallback, nullptr);
			if (m_PendingSubsteps)
			{
				Vans::VansCharacterMotionStep step;
				while (m_Locomotion.NextMotionStep(step))
				{
					{
						VansPhysicsCapsuleSweepRequest shape;
						shape.origin=GetPosition();shape.axis=m_Properties.m_UpDirection;
						shape.radius=m_Properties.m_Radius;
						shape.halfHeight=.5f*m_Properties.m_Height+m_Properties.m_Radius;
						shape.filter.ignoredTransformId=m_TransformID;
						shape.filter.collisionLayerIndex=static_cast<int>(m_Native->filterData.word0);
						shape.filter.includeTriggers=false;
						VansCharacterFloorSettings floorSettings;
						floorSettings.maxStepHeight=m_Properties.m_StepOffset;
						floorSettings.perchRadiusThreshold=m_Properties.m_PerchRadiusThreshold;
						floorSettings.perchAdditionalHeight=m_Properties.m_PerchAdditionalHeight;
						VansCharacterPenetrationSettings penetrationSettings;
						penetrationSettings.pullbackDistance=m_Properties.m_PenetrationPullbackDistance;
						penetrationSettings.overlapInflation=m_Properties.m_PenetrationOverlapInflation;
						penetrationSettings.maxGeometryDistance=m_Properties.m_MaxDepenetrationWithGeometry;
						penetrationSettings.maxCharacterDistance=m_Properties.m_MaxDepenetrationWithCharacters;
						VansCharacterGroundSettings groundSettings;
						groundSettings.maintainHorizontalVelocity=m_Properties.m_MaintainHorizontalGroundVelocity;
						groundSettings.canWalkOffLedges=m_Properties.m_CanWalkOffLedges &&
							(!m_Native->crouched || m_Properties.m_CanWalkOffLedgesWhenCrouching);
						groundSettings.ledgeCheckThreshold=m_Properties.m_LedgeCheckThreshold;
						if (!step.grounded) m_Native->groundState.valid=false;
						auto contact=step.grounded ? VansCharacterSweepSolver::AdvanceGroundLocked(
							shape,step,m_Properties.m_SlopeLimit,m_Native->groundState,floorSettings,
							penetrationSettings,groundSettings) : VansCharacterSweepSolver::AdvanceAirLocked(
							shape,step,m_Properties.m_SlopeLimit,m_Native->sweepState,floorSettings,
							penetrationSettings,m_Properties.m_UseImpactBodyVelocity);
						m_Native->controller->setPosition(PxExtendedVec3(contact.position.x,contact.position.y,contact.position.z));
						if (step.grounded && !contact.grounded)
						{
							contact.velocity+=GetImpartedMovementBaseVelocityLocked();
							m_Native->movementBase={};
						}
						else if (step.grounded && contact.grounded && m_Native->groundState.floor.walkable)
							SaveMovementBaseLocked(m_Native->groundState.floor.hit);
						m_Native->lastCollisionFlags=PxControllerCollisionFlags();
						if (contact.grounded && !step.grounded)
						{
							m_PendingLanding = PendingLanding{contact.velocity,contact.unusedTime};
							SyncTransformFromController();
							return {std::nullopt,VansCharacterLandedEvent{m_TransformID,
								contact.position,contact.landingVelocity,contact.landingNormal}};
						}
						if (contact.ceiling) m_Native->lastCollisionFlags|=PxControllerCollisionFlag::eCOLLISION_UP;
						if (contact.grounded) m_Native->lastCollisionFlags|=PxControllerCollisionFlag::eCOLLISION_DOWN;
						if (contact.blocked) m_Native->lastCollisionFlags|=PxControllerCollisionFlag::eCOLLISION_SIDES;
						m_Locomotion.ResolveSweptMotionStep(contact.velocity,contact.grounded,contact.unusedTime,contact.stopSimulation);
						continue;
					}
				}
			}
			else
			{
				m_Native->groundState.valid=false;
				m_Native->lastCollisionFlags = m_Native->controller->move(disp, 0.001f, m_PendingDt, filters);
				if (IsGrounded())
				{
					const auto floor=FindCurrentFloorLocked();
					if (floor.walkable) SaveMovementBaseLocked(floor.hit);
				}
				else m_Native->movementBase={};
			}
			const glm::vec3 positionAfter = GetPosition();
			if (resolvedDt > 0.0f)
				result = VansCharacterMovementUpdatedEvent{ m_TransformID, resolvedDt,
					positionAfter, (positionAfter - positionBefore) / resolvedDt, IsGrounded() };
			if (result) result->animationRootMotion = m_PendingAnimationRootMotion;
			if (result)
			{
				result->baseRotationDeltaYaw=m_Native->baseRotationDeltaYaw;
				m_Native->baseRotationDeltaYaw=0.0f;
			}
			if (result && m_PendingSubsteps)
			{
				result->motionVelocity = m_Locomotion.GetSimulatedVelocity();
				result->simulationSteps = m_Locomotion.GetSimulationSteps();
			}
			else if (result && m_PendingModeledMotion)
			{
				const auto vertical = m_Locomotion.ResolveVerticalContact(IsGrounded(),
					m_Native->lastCollisionFlags.isSet(PxControllerCollisionFlag::eCOLLISION_UP));
				if (vertical) result->motionVelocity = glm::vec3(result->velocity.x,*vertical,result->velocity.z);
			}
			glm::vec3 resolvedPlanarDelta = positionAfter - positionBefore;
			resolvedPlanarDelta.y = 0.0f;
			if (resolvedDt > 0.0001f)
			{
				const glm::vec3 transformPosition =
					positionAfter - m_Properties.m_PositionOffset;
				m_Locomotion.RecordResolvedMotion(
					resolvedDt,
					transformPosition,
					result && result->motionVelocity ? *result->motionVelocity : resolvedPlanarDelta / resolvedDt,
					glm::vec3(m_PendingDisplacement.x, 0.0f,
						m_PendingDisplacement.z) / resolvedDt);
			}

			DiscardPendingMove();
        }

        // 无论是否有待执行位移，每帧都将 PhysX 位置同步回 Transform
        SyncTransformFromController();
		return {result,std::nullopt};
    }

    VansCharacterFloorResult VansCharacterControllerNode::FindCurrentFloorLocked() const
    {
        VansPhysicsCapsuleSweepRequest shape;shape.origin=GetPosition();shape.axis=m_Properties.m_UpDirection;
        shape.radius=m_Properties.m_Radius;shape.halfHeight=.5f*m_Properties.m_Height+m_Properties.m_Radius;
        shape.filter.ignoredTransformId=m_TransformID;
        shape.filter.collisionLayerIndex=static_cast<int>(m_Native->filterData.word0);shape.filter.includeTriggers=false;
        VansCharacterFloorSettings settings;settings.maxStepHeight=m_Properties.m_StepOffset;
        settings.perchRadiusThreshold=m_Properties.m_PerchRadiusThreshold;
        settings.perchAdditionalHeight=m_Properties.m_PerchAdditionalHeight;
        return VansCharacterSweepSolver::FindFloorLocked(shape,m_Properties.m_SlopeLimit,settings,true);
    }

    void VansCharacterControllerNode::SaveMovementBaseLocked(const VansPhysicsQueryHit& hit)
    {
        // The floor hit was queried under this same simulation lock.
        m_Native->movementBase=hit;
    }

    glm::vec3 VansCharacterControllerNode::GetImpartedMovementBaseVelocityLocked() const
    {
        VansPhysicsQueryHit current;
        const auto& base=m_Native->movementBase;
        if (!base.supportMovable || !VansPhysicsQuery::GetBodyMotionLocked(base.actorIdentity,base.transformId,current) ||
            !current.supportMovable) return glm::vec3(0);
        const float halfHeight=.5f*m_Properties.m_Height+m_Properties.m_Radius;
        const auto feet=GetPosition()-glm::vec3(0,halfHeight,0);
        return VansCharacterSweepSolver::ImpartMovementBaseVelocity(current,feet,
            glm::bvec3(m_Properties.m_ImpartMovementBaseVelocityX,m_Properties.m_ImpartMovementBaseVelocityY,
                m_Properties.m_ImpartMovementBaseVelocityZ),m_Properties.m_ImpartMovementBaseAngularVelocity);
    }

    void VansCharacterControllerNode::UpdateMovementBaseLocked()
    {
        auto& saved=m_Native->movementBase;
        if (!IsGrounded() || !m_CollisionEnabled || m_FollowRagdollKey.IsValid()) return;
        if (!saved.actorIdentity || !saved.supportMovable) return;
        VansPhysicsQueryHit current;
        if (!VansPhysicsQuery::GetBodyMotionLocked(saved.actorIdentity,saved.transformId,current))
        {
            saved={};m_Native->groundState.valid=false;return;
        }
        if (!current.supportMovable) {saved=current;return;}
        const auto oldRotation=saved.supportRotation,newRotation=current.supportRotation;
        const auto oldQ=glm::vec4(oldRotation.x,oldRotation.y,oldRotation.z,oldRotation.w);
        const auto newQ=glm::vec4(newRotation.x,newRotation.y,newRotation.z,newRotation.w);
        const bool rotationChanged=!(glm::all(glm::lessThanEqual(glm::abs(oldQ-newQ),glm::vec4(1.e-8f))) ||
            glm::all(glm::lessThanEqual(glm::abs(oldQ+newQ),glm::vec4(1.e-8f))));
        const auto translation=current.supportPosition-saved.supportPosition;
        if (rotationChanged || glm::any(glm::notEqual(translation,glm::vec3(0))))
        {
            const auto position=GetPosition();
            const float halfHeight=.5f*m_Properties.m_Height+m_Properties.m_Radius;
            const glm::vec3 offset(0,halfHeight,0);
            const auto localFeet=glm::conjugate(oldRotation)*(position-offset-saved.supportPosition);
            const auto target=current.supportPosition+newRotation*localFeet+offset;
            auto delta=target-position;
            if (!rotationChanged && translation.x==0 && translation.z==0) delta.x=delta.z=0;
            VansPhysicsCapsuleSweepRequest shape;shape.origin=position;shape.axis=m_Properties.m_UpDirection;
            shape.radius=m_Properties.m_Radius;shape.halfHeight=halfHeight;
            shape.filter.ignoredTransformId=m_TransformID;
            shape.filter.collisionLayerIndex=static_cast<int>(m_Native->filterData.word0);
            shape.filter.includeTriggers=false;
            const auto baseTransform=saved.transformId;
            shape.filter.accept=[baseTransform](const VansPhysicsQueryCandidate& candidate)
                {return candidate.transformId!=baseTransform;};
            const auto moved=VansCharacterSweepSolver::SweepMoveLocked(shape,delta);
            m_Native->controller->setPosition(PxExtendedVec3(moved.position.x,moved.position.y,moved.position.z));
            if (rotationChanged && m_Properties.m_FollowMovementBaseRotation)
            {
                auto transform=Vans::VansTransformStore::Read(m_TransformID);
				const float oldYaw=transform.m_Rotation.y;
                const auto yaw=glm::angleAxis(glm::radians(transform.m_Rotation.y),glm::vec3(0,1,0));
                const auto forward=(newRotation*glm::conjugate(oldRotation)*yaw)*glm::vec3(0,0,1);
                transform.m_Rotation.y=glm::degrees(std::atan2(forward.x,forward.z));
				m_Native->baseRotationDeltaYaw+=std::remainder(transform.m_Rotation.y-oldYaw,360.0f);
                Vans::VansTransformStore::Write(m_TransformID,transform);
            }
            SyncTransformFromController();m_Native->groundState.valid=false;
        }
        saved=current;
    }

    void VansCharacterControllerNode::SetPosition(const glm::vec3& pos)
    {
        if (!m_Native->controller) return;
        m_Native->groundState.valid=false;
        m_Native->movementBase={};
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
		SyncTransformFromController();
    }

	bool VansCharacterControllerNode::SeedMotionVelocity(const glm::vec3& velocityWorld)
	{
		VANS_ASSERT_MAIN_THREAD();
		if (!m_Native->controller ||
			!std::isfinite(velocityWorld.x) || !std::isfinite(velocityWorld.y) ||
			!std::isfinite(velocityWorld.z)) return false;
		m_Locomotion.SeedVelocity(velocityWorld);
		return true;
	}

	bool VansCharacterControllerNode::InitializeContactState(bool preferGrounded)
	{
		VANS_ASSERT_MAIN_THREAD();
		if (!m_Native->controller || !m_CollisionEnabled || !IsEnabled() ||
			m_FollowRagdollKey.IsValid() || m_PendingSubsteps || m_PendingLanding ||
			(preferGrounded && glm::length(m_Properties.m_UpDirection-glm::vec3(0,1,0))>1.e-6f)) return false;
		auto& physics=VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
		m_Native->groundState={};m_Native->movementBase={};
		m_Native->lastCollisionFlags=PxControllerCollisionFlags(0);
		if (preferGrounded)
		{
			VansPhysicsCapsuleSweepRequest shape;shape.origin=GetPosition();shape.axis=m_Properties.m_UpDirection;
			shape.radius=m_Properties.m_Radius;shape.halfHeight=.5f*m_Properties.m_Height+m_Properties.m_Radius;
			shape.filter.ignoredTransformId=m_TransformID;
			shape.filter.collisionLayerIndex=static_cast<int>(m_Native->filterData.word0);shape.filter.includeTriggers=false;
			VansCharacterFloorSettings floor;
			floor.maxStepHeight=m_Properties.m_StepOffset;floor.perchRadiusThreshold=m_Properties.m_PerchRadiusThreshold;
			floor.perchAdditionalHeight=m_Properties.m_PerchAdditionalHeight;
			VansCharacterPenetrationSettings penetration;
			penetration.pullbackDistance=m_Properties.m_PenetrationPullbackDistance;
			penetration.overlapInflation=m_Properties.m_PenetrationOverlapInflation;
			penetration.maxGeometryDistance=m_Properties.m_MaxDepenetrationWithGeometry;
			penetration.maxCharacterDistance=m_Properties.m_MaxDepenetrationWithCharacters;
			VansCharacterGroundSettings ground;ground.maintainHorizontalVelocity=m_Properties.m_MaintainHorizontalGroundVelocity;
			const auto contact=VansCharacterSweepSolver::InitializeGroundLocked(shape,m_Properties.m_SlopeLimit,
				m_Native->groundState,floor,penetration,ground);
			m_Native->controller->setPosition(PxExtendedVec3(contact.position.x,contact.position.y,contact.position.z));
			// 无支撑基底时直接保留空中状态，避免第一帧先执行地面运动。
			if (m_Native->groundState.valid && m_Native->groundState.floor.walkable)
			{
				m_Native->lastCollisionFlags=PxControllerCollisionFlag::eCOLLISION_DOWN;
				SaveMovementBaseLocked(m_Native->groundState.floor.hit);
				m_Locomotion.InitializeGroundVelocity();
			}
		}
		SyncTransformFromController();
		return true;
	}

	bool VansCharacterControllerNode::SeedGroundedContact(bool grounded)
	{
		VANS_ASSERT_MAIN_THREAD();
		if (!m_Native->controller || (grounded && !m_CollisionEnabled)) return false;
		m_Native->lastCollisionFlags = grounded
			? PxControllerCollisionFlag::eCOLLISION_DOWN
			: PxControllerCollisionFlags(0);
		m_Native->groundState.valid=false;
		if (!grounded) m_Native->movementBase={};
		return true;
	}

    bool VansCharacterControllerNode::ResizeCapsule(float cylinderHeight,std::optional<bool> crouched)
    {
        if (!m_Native->controller || !std::isfinite(cylinderHeight) || cylinderHeight <= 0.0f)
            return false;
        const float oldHeight = m_Properties.m_Height;
        if (std::abs(cylinderHeight - oldHeight) <= 1.0e-5f)
        {
            if (crouched) m_Native->crouched=*crouched;
            return true;
        }

        const glm::vec3 up = glm::normalize(m_Properties.m_UpDirection);
        if (cylinderHeight > oldHeight)
        {
            auto& physics = VansPhysicsSystem::GetInstance();
            PxScene* scene = VansPhysicsNativeAccess::Scene(physics);
            if (!scene) return false;
            const glm::vec3 center = GetPosition() + up * ((cylinderHeight - oldHeight) * 0.5f);
            const glm::quat orientation = glm::quat(glm::vec3(1.0f, 0.0f, 0.0f), up);
            PxOverlapHit overlapHit;
            PxOverlapBuffer result(&overlapHit, 1);
            VansCCTQueryFilterCallback callback(m_Native->controller->getActor(), PxQueryHitType::eTOUCH);
            PxQueryFilterData filterData(m_Native->filterData,
                PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::ePREFILTER);
            // A small inset avoids classifying the supporting floor as an obstruction.
            const float inset = (std::min)(0.001f, m_Properties.m_Radius * 0.01f);
            const PxCapsuleGeometry geometry(m_Properties.m_Radius - inset,
                cylinderHeight * 0.5f - inset);
            {
                PxSceneReadLock sceneLock(*scene);
                if (scene->overlap(geometry,
                    PxTransform(PxVec3(center.x, center.y, center.z),
                        PxQuat(orientation.x, orientation.y, orientation.z, orientation.w)),
                    result, filterData, &callback))
                    return false;
            }
        }

        m_Native->controller->resize(cylinderHeight);
        m_Properties.m_Height = cylinderHeight;
        if (crouched) m_Native->crouched=*crouched;
        m_Native->groundState.valid=false;
        m_Properties.m_PositionOffset += up * ((cylinderHeight - oldHeight) * 0.5f);
        SyncTransformFromController();
        return true;
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

	void VansCharacterControllerNode::SetFacingYaw(float yaw)
	{
		VANS_ASSERT_MAIN_THREAD();
		VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
		if (!m_Enabled || IsGameplayMovementBlocked() || !std::isfinite(yaw) || m_TransformID == UINT32_MAX ||
			!Vans::VansTransformStore::IsAllocated(m_TransformID)) return;
		auto transform = Vans::VansTransformStore::Read(m_TransformID);
		transform.m_Rotation.y = yaw;
		Vans::VansTransformStore::Write(m_TransformID, transform);
		Vans::VansTransformStore::MarkDirty(m_TransformID);
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
		VANS_ASSERT_MAIN_THREAD();
		VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic);
		if (!IsEnabled() || !m_Native->controller || m_TransformID == UINT32_MAX)
			return;

		glm::vec3 leavingBaseVelocity(0);
		{
			std::lock_guard<std::mutex> lock(VansPhysicsSystem::GetInstance().GetSimulationMutex());
			m_Native->baseRotationDeltaYaw=0.0f;
			UpdateMovementBaseLocked();
			leavingBaseVelocity=GetImpartedMovementBaseVelocityLocked();
		}
		const Vans::VansTransform& transform =
			Vans::VansTransformStore::Read(m_TransformID);
		const bool jumpAccepted = m_Locomotion.Prepare(
			dt,
			settings,
			transform.m_Position,
			transform.m_Rotation.y,
			IsGrounded(),
			IsGameplayMovementBlocked(),leavingBaseVelocity);
		// No simulation mutex is held: callbacks may query Physics and update
		// animation parameters for the immediately following graph evaluation.
		if (jumpAccepted)
		{
			m_Native->movementBase={};m_Native->groundState.valid=false;
			Vans::VansEventBus::Get().PublishNow(VansCharacterJumpEvent{ m_TransformID });
		}
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
		m_PendingModeledMotion = true;
		m_PendingSubsteps = result.substepped;
		m_PendingAnimationRootMotion = rootMotionValid &&
			authority.mode == Vans::VansLocomotionAuthorityMode::RootMotion && !IsGameplayMovementBlocked();
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
		m_PendingAnimationRootMotion = false;
		m_HasPendingMove = false;
		m_PendingModeledMotion = false;
		m_PendingSubsteps = false;
		m_FlushStarted = false;
		m_PendingLanding.reset();
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

    void VansCharacterControllerNode::SetCollisionEnabled(bool enabled)
    {
        VANS_ASSERT_MAIN_THREAD();
        if (m_CollisionEnabled == enabled || !m_Native->controller)
            return;
        m_CollisionEnabled = enabled;
		if (!enabled) m_Native->movementBase={};
        m_Native->groundState.valid=false;
        const int layer = static_cast<int>(m_Native->filterData.word0);
        m_Native->filterData.word1 = enabled
            ? VansCollisionLayerManager::Get().GetCollisionMask(layer) : 0u;
        PxRigidDynamic* actor = m_Native->controller->getActor();
        if (!actor) return;
        const PxU32 count = actor->getNbShapes();
        std::vector<PxShape*> shapes(count);
        actor->getShapes(shapes.data(), count);
        for (PxShape* shape : shapes)
        {
            if (!shape) continue;
            shape->setSimulationFilterData(m_Native->filterData);
            shape->setQueryFilterData(m_Native->filterData);
            shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, enabled);
            shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, enabled);
        }
        if (enabled)
        {
            if (PxScene* scene = actor->getScene())
                scene->resetFiltering(*actor);
        }
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
