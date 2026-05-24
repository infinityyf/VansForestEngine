#include "VansCharacterControllerNode.h"
#include "VansCollisionLayerManager.h"
#include "../ScriptCore/VansTransform.h"
#include "../Util/VansLog.h"
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

        if (m_HasPendingMove)
        {
            PxVec3 disp(m_PendingDisplacement.x,
                        m_PendingDisplacement.y,
                        m_PendingDisplacement.z);
            VansCCTQueryFilterCallback queryFilterCallback;
            PxControllerFilters filters(&m_FilterData, &queryFilterCallback, nullptr);
            m_LastCollisionFlags = m_Controller->move(disp, 0.001f, m_PendingDt, filters);

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
        return (m_LastCollisionFlags & PxControllerCollisionFlag::eCOLLISION_DOWN) != 0;
    }

    void VansCharacterControllerNode::SyncControllerFromTransform()
    {
        if (!m_Controller || m_TransformID == UINT32_MAX) return;

        const VansGraphics::VansTransform& t =
            VansGraphics::VansTransformStore::GetTransform(m_TransformID);
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

} // namespace VansEngine
