#pragma once
#include "VansPhysicsQuery.h"
#include "../RuntimeCore/VansCharacterMotionStepper.h"

namespace VansEngine
{
    struct VansCharacterSweepResult
    {
        glm::vec3 position{0}, velocity{0};
        glm::vec3 landingVelocity{0}, landingNormal{0};
        float unusedTime = 0;
        bool grounded = false, ceiling = false, blocked = false, penetrating = false;
        int sweepCount = 0;
    };

    // 接触与扫掠归 PhysicsCore；输入不包含动画、项目状态或脚本。
    class VansCharacterSweepSolver final
    {
    public:
        // 调用方已持有 SimulationMutex。request 指定实际胶囊形状及过滤规则。
        static VansCharacterSweepResult AdvanceAirLocked(
            const VansPhysicsCapsuleSweepRequest& request,
            const Vans::VansCharacterMotionStep& step, float walkableFloorCosine);
    };
}
