#include "VansCharacterMotionStepper.h"

namespace Vans
{
    namespace
    {
        glm::vec3 SafeNormal(const glm::vec3& value)
        {
            const float squared = glm::dot(value,value);
            return squared > 1.e-8f ? value/std::sqrt(squared) : glm::vec3(0);
        }
        glm::vec3 PlaneProject(const glm::vec3& value, const glm::vec3& normal)
        {
            return value-normal*glm::dot(value,normal);
        }
    }

    glm::vec3 CharacterAirSlide(const glm::vec3& delta, float fraction, const glm::vec3& normal)
    {
        const auto slide = PlaneProject(delta,normal)*fraction;
        auto result = slide;
        const float upLimit = delta.y*fraction;
        // SI 距离阈值；防止斜面把横向位移变成额外向上能量。
        if (result.y > 0 && result.y-upLimit > 1.e-6f)
        {
            result = upLimit > 0 ? result*(upLimit/result.y) : glm::vec3(0);
            const glm::vec3 remainder(slide.x-result.x,0,slide.z-result.z);
            result += PlaneProject(remainder,SafeNormal({normal.x,0,normal.z}));
        }
        return result;
    }

    glm::vec3 CharacterLimitAirControl(const glm::vec3& acceleration,
        const glm::vec3& normal, bool penetrating)
    {
        if (penetrating)
            return glm::dot(acceleration,normal)>0 ? acceleration : glm::vec3(0);
        if (normal.y > .001f && glm::dot(acceleration,normal)<0)
            return PlaneProject(acceleration,SafeNormal({normal.x,0,normal.z}));
        return acceleration;
    }

    glm::vec3 CharacterTwoWallSlide(const glm::vec3& delta, float fraction,
        const glm::vec3& normal, const glm::vec3& previousNormal)
    {
        const float corner = glm::dot(previousNormal,normal);
        if (corner<=0)
        {
            const auto direction = SafeNormal(glm::cross(normal,previousNormal));
            const auto result = direction*(glm::dot(delta,direction)*fraction);
            return glm::dot(delta,result)<0 ? -result : result;
        }
        auto result = CharacterAirSlide(delta,fraction,normal);
        if (glm::dot(result,delta)<=0) return glm::vec3(0);
        if (std::abs(corner-1)<1.e-4f) result += normal*.0001f;
        return result;
    }

    void VansCharacterMotionStepper::Begin(const VansCharacterMotionIntent& intent,
        const glm::vec3& velocity, bool grounded, float deltaTime)
    {
        m_Intent = intent;
        m_Velocity = velocity;
        m_Grounded = grounded;
        m_Remaining = std::isfinite(deltaTime) ? (std::max)(deltaTime,0.0f) : 0.0f;
        m_SimulatedTime = 0.0f;
        m_Iterations = m_ApexAttempts = m_Steps = 0;
        m_AwaitingCollision = false;
        m_InputWorld = LocomotionLocalToWorldPlanar(
            glm::vec3(intent.moveInputLocal.x,0,intent.moveInputLocal.y),intent.movementReferenceYaw);
        UpdateAirAcceleration();
    }

    void VansCharacterMotionStepper::UpdateAirAcceleration()
    {
        if (!m_Intent.accelerationModel) return;
        // 连续 Falling 模拟的输入加速度固定，子步不能重新触发低速 boost。
        m_AirAcceleration = ResolveCharacterInputAcceleration(m_InputWorld,
            glm::vec3(m_Velocity.x,0,m_Velocity.z),m_Intent.accelerationModel->airborne);
    }

    bool VansCharacterMotionStepper::Next(VansCharacterMotionStep& step)
    {
        if (!m_Intent.accelerationModel || m_AwaitingCollision || m_Remaining < 1.e-6f) return false;
        const auto& model = *m_Intent.accelerationModel;
        if (m_Iterations >= std::clamp(model.falling.maxIterations,1,32)) return false;
        ++m_Iterations;
        const float fullStep = CharacterSimulationTimeStep(m_Remaining,m_Iterations,model.falling);
        const glm::vec3 oldVelocity = m_Velocity, planar(oldVelocity.x,0,oldVelocity.z);
        const auto& dynamics = m_Grounded ? model.grounded : model.airborne;
        const auto acceleration = m_Grounded
            ? ResolveCharacterInputAcceleration(m_InputWorld,planar,dynamics) : m_AirAcceleration;
        const float speed = (std::max)(m_Intent.desiredSpeed*(std::min)(1.0f,glm::length(m_Intent.moveInputLocal)),
            dynamics.minAnalogSpeed);
        m_Velocity = IntegrateCharacterVelocity(planar,acceleration,speed,fullStep,dynamics);
        step = {};
        step.deltaTime = fullStep;
        step.grounded = m_Grounded;
        if (!m_Grounded)
        {
            const auto fall = IntegrateCharacterFallingStep(oldVelocity.y,m_Intent.gravity,fullStep,
                model.falling,m_ApexAttempts < std::clamp(model.falling.maxApexAttempts,0,8));
            step.deltaTime = fall.deltaTime;
            step.apex = fall.apex;
            if (fall.apex)
            {
                m_Velocity = planar+(m_Velocity-planar)*(fall.deltaTime/fullStep);
                --m_Iterations;
                ++m_ApexAttempts;
            }
            m_Velocity.y = fall.velocity;
            step.displacement = .5f*(oldVelocity+m_Velocity)*step.deltaTime;
            step.velocityWithoutAirControl = IntegrateCharacterVelocity(planar,glm::vec3(0),
                speed,step.deltaTime,dynamics);
            // 源流程的 GravityTime 在顶点切分之前确定；碰撞无输入分支沿用该时长。
            step.velocityWithoutAirControl.y = IntegrateCharacterFallingStep(oldVelocity.y,
                m_Intent.gravity,fullStep,model.falling,false).velocity;
            if (glm::dot(m_AirAcceleration,m_AirAcceleration)>0)
                step.airControlAcceleration = (m_Velocity-step.velocityWithoutAirControl)/step.deltaTime;
        }
        else
        {
            m_Velocity.y = 0;
            step.displacement = m_Velocity*step.deltaTime;
            // 当前 CCT 接地适配；完整 floor projection 仍待 PhysicsCore 实现。
            step.displacement.y = -.5f*step.deltaTime;
        }
        step.velocity = m_Velocity;
        m_Remaining -= step.deltaTime;
        m_SimulatedTime += step.deltaTime;
        m_LastStepTime = step.deltaTime;
        ++m_Steps;
        m_AwaitingCollision = true;
        return true;
    }

    void VansCharacterMotionStepper::ApplyCollision(bool grounded, bool ceiling,
        const glm::vec3* normals, std::size_t normalCount)
    {
        if (!m_AwaitingCollision) return;
        for (std::size_t i=0;i<normalCount;++i)
        {
            const auto& normal = normals[i];
            const float lengthSquared = glm::dot(normal,normal);
            const float inward = glm::dot(m_Velocity,normal);
            if (lengthSquared > 1.e-8f && inward < 0)
                m_Velocity -= normal*(inward/lengthSquared);
        }
        if ((grounded && m_Velocity.y<0) || (ceiling && m_Velocity.y>0)) m_Velocity.y=0;
        const bool leftGround = m_Grounded && !grounded;
        m_Grounded = grounded;
        if (leftGround) UpdateAirAcceleration();
        m_AwaitingCollision = false;
    }

    void VansCharacterMotionStepper::ApplySweptCollision(const glm::vec3& velocity,
        bool grounded, float unusedTime)
    {
        if (!m_AwaitingCollision) return;
        const float refund = std::clamp(unusedTime,0.0f,m_LastStepTime);
        m_Remaining += refund;
        m_SimulatedTime -= refund;
        m_Velocity = velocity;
        if (grounded) m_Velocity.y = 0;
        // Falling 每轮结束清除极小横向残速，阈值从 cm²/s² 转为 m²/s²。
        else if (m_Velocity.x*m_Velocity.x+m_Velocity.z*m_Velocity.z <= 1.e-7f)
            m_Velocity.x=m_Velocity.z=0;
        const bool leftGround = m_Grounded && !grounded;
        m_Grounded = grounded;
        if (leftGround) UpdateAirAcceleration();
        m_AwaitingCollision = false;
    }
}
