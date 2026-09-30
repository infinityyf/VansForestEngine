#pragma once
#include "VansCharacterMotion.h"
#include <cstddef>

namespace Vans
{
    struct VansCharacterMotionStep
    {
        glm::vec3 displacement{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 velocityWithoutAirControl{0.0f};
        glm::vec3 airControlAcceleration{0.0f};
        float deltaTime = 0.0f;
        float maximumSpeed = 0.0f;
        float jumpSpeed = 0.0f;
        float remainingTime = 0.0f;
        bool grounded = false;
        bool apex = false;
		bool animationRootMotion = false;
    };

    glm::vec3 CharacterAirSlide(const glm::vec3& delta, float remainingFraction,
        const glm::vec3& normal);
    glm::vec3 CharacterLimitAirControl(const glm::vec3& acceleration,
        const glm::vec3& normal, bool penetrating);
    glm::vec3 CharacterTwoWallSlide(const glm::vec3& delta, float remainingFraction,
        const glm::vec3& normal, const glm::vec3& previousNormal);

    // RuntimeCore 只推进数值状态；下一步前必须提交实际碰撞反馈。
    class VansCharacterMotionStepper
    {
    public:
        void Begin(const VansCharacterMotionIntent& intent, const glm::vec3& velocity,
            bool grounded, float deltaTime,
			std::optional<glm::vec3> animationRootVelocity = std::nullopt);
        bool Next(VansCharacterMotionStep& step);
        void ApplySweptCollision(const glm::vec3& velocity, bool grounded, float unusedTime,
            bool stopSimulation = false);
        void SetDynamics(const VansCharacterAccelerationModel& model)
        {
            if (m_Intent.accelerationModel) m_Intent.accelerationModel = model;
        }
        const glm::vec3& GetVelocity() const { return m_Velocity; }
        int GetStepCount() const { return m_Steps; }
        float GetSimulatedTime() const { return m_SimulatedTime; }
    private:
        void UpdateAirAcceleration();
        VansCharacterMotionIntent m_Intent;
        glm::vec3 m_Velocity{0.0f}, m_InputWorld{0.0f}, m_AirAcceleration{0.0f};
        float m_Remaining = 0.0f, m_SimulatedTime = 0.0f;
        float m_LastStepTime = 0.0f;
        int m_Iterations = 0, m_ApexAttempts = 0, m_Steps = 0;
        bool m_Grounded = false, m_AwaitingCollision = false;
		std::optional<glm::vec3> m_AnimationRootVelocity;
    };
}
