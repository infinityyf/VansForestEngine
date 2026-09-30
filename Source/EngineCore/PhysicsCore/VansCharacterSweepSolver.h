#pragma once
#include "VansPhysicsQuery.h"
#include "../RuntimeCore/VansCharacterMotionStepper.h"

namespace VansEngine
{
    struct VansCharacterSweepState
    {
        std::uint32_t randomSeed = 0;
    };

    struct VansCharacterPenetrationSettings
    {
        float pullbackDistance = .00125f;
        float overlapInflation = .001f;
        float maxGeometryDistance = 5.f;
        float maxCharacterDistance = 1.f;
    };

    struct VansCharacterSafeMoveResult
    {
        VansPhysicsQueryHit query;
        glm::vec3 position{0}, location{0}, traceStart{0}, traceEnd{0};
        float time = 1;
        bool blocked = false, depenetrated = false;
        int sweepCount = 0;
    };

    struct VansCharacterFloorSettings
    {
        float maxStepHeight = .3f;
        float perchRadiusThreshold = 0;
        float perchAdditionalHeight = .4f;
    };

    struct VansCharacterFloorResult
    {
        VansPhysicsQueryHit hit;
        glm::vec3 hitLocation{0};
        glm::vec3 traceStart{0}, traceEnd{0};
        float floorDistance = 0, lineDistance = 0;
        bool blocking = false, walkable = false, lineTrace = false;
    };

    struct VansCharacterGroundSettings
    {
        bool maintainHorizontalVelocity = true;
        bool canWalkOffLedges = true;
        float ledgeCheckThreshold = .04f;
    };

    struct VansCharacterGroundState
    {
        VansCharacterFloorResult floor;
        bool valid = false;
        bool triedLedgeMove = false;
    };

    struct VansCharacterSweepResult
    {
        glm::vec3 position{0}, velocity{0};
        glm::vec3 landingVelocity{0}, landingNormal{0};
        float unusedTime = 0;
        bool grounded = false, ceiling = false, blocked = false, penetrating = false, depenetrated = false;
        bool stopSimulation = false;
        bool retryGroundLedge = false;
        int sweepCount = 0;
    };

    // 接触与扫掠归 PhysicsCore；输入不包含动画、项目状态或脚本。
    class VansCharacterSweepSolver final
    {
    public:
        // 初始化支撑状态；不执行运动积分、台阶/边缘处理或额外穿透重试。
        static VansCharacterSweepResult InitializeGroundLocked(
            const VansPhysicsCapsuleSweepRequest& request, float walkableFloorCosine,
            VansCharacterGroundState& state,
            const VansCharacterFloorSettings& floorSettings = {},
            const VansCharacterPenetrationSettings& penetrationSettings = {},
            const VansCharacterGroundSettings& groundSettings = {});
        static glm::vec3 ImpartMovementBaseVelocity(const VansPhysicsQueryHit& base,
            const glm::vec3& capsuleFeet,const glm::bvec3& enabledAxes,bool angularVelocity);
        static VansCharacterSweepResult AdvanceGroundLocked(
            const VansPhysicsCapsuleSweepRequest& request,
            const Vans::VansCharacterMotionStep& step, float walkableFloorCosine,
            VansCharacterGroundState& state,
            const VansCharacterFloorSettings& floorSettings = {},
            const VansCharacterPenetrationSettings& penetrationSettings = {},
            const VansCharacterGroundSettings& groundSettings = {});
        // 调用方已持有 SimulationMutex。request 指定实际胶囊形状及过滤规则。
        static VansCharacterSweepResult AdvanceAirLocked(
            const VansPhysicsCapsuleSweepRequest& request,
            const Vans::VansCharacterMotionStep& step, float walkableFloorCosine,
            VansCharacterSweepState& state,
            const VansCharacterFloorSettings& floorSettings = {},
            const VansCharacterPenetrationSettings& penetrationSettings = {},
            bool useImpactBodyVelocity = true);
        static VansCharacterSafeMoveResult SafeMoveLocked(
            const VansPhysicsCapsuleSweepRequest& request, const glm::vec3& delta,
            const VansCharacterPenetrationSettings& settings = {}, bool previouslyAdjusted = false);
        static VansCharacterSafeMoveResult SweepMoveLocked(
            const VansPhysicsCapsuleSweepRequest& request, const glm::vec3& delta,
            bool ignoreInitialOverlapsMovingOut = true);
        static VansCharacterFloorResult FindFloorLocked(
            const VansPhysicsCapsuleSweepRequest& request, float walkableFloorCosine,
            const VansCharacterFloorSettings& settings, bool movingOnGround = false);
    };
}
