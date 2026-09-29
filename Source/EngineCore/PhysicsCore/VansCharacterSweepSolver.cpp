#include "VansCharacterSweepSolver.h"
#include <algorithm>
#include <cmath>

namespace VansEngine
{
    namespace
    {
        struct VansCharacterMoveHit
        {
            VansPhysicsQueryHit query;
            glm::vec3 location{0};
            float time = 1;
            bool blocked = false;
        };

        // 保留原始命中位置供下半球／边缘判断，实际移动时间使用回退后的比例。
        VansCharacterMoveHit Move(VansPhysicsCapsuleSweepRequest& request,
            const glm::vec3& delta, VansCharacterSweepResult& result)
        {
            VansCharacterMoveHit hit;
            const float distance = glm::length(delta);
            if (distance < 1.e-8f) return hit;
            request.origin = result.position;
            request.direction = delta/distance;
            // PhysX can omit a contact exactly at the sweep endpoint. Query a
            // tiny extension so the movement response still applies its normal
            // pullback when the capsule reaches a surface on this step.
            request.distance = distance + 0.0001f;
            ++result.sweepCount;
            hit.blocked = VansPhysicsQuery::SweepCapsuleClosestLocked(request,hit.query) &&
                hit.query.distance <= distance;
            if (hit.blocked)
            {
                hit.location = result.position+request.direction*hit.query.distance;
                const float back = std::clamp(.1f,.001f/distance,.01f/distance)+.001f;
                hit.time = hit.query.initialOverlap ? 0 : std::clamp(hit.query.distance/distance-back,0.0f,1.0f);
                result.blocked = true;
                result.ceiling |= hit.query.normal.y<0;
                result.penetrating |= hit.query.initialOverlap;
            }
            result.position += delta*hit.time;
            return hit;
        }

        bool CanLand(const VansPhysicsCapsuleSweepRequest& request,
            const VansCharacterMoveHit& hit, float walkable)
        {
            if (!hit.blocked || hit.query.initialOverlap || hit.query.normal.y<walkable) return false;
            if (hit.query.position.y >= hit.location.y-request.halfHeight+request.radius) return false;
            const glm::vec3 edge=hit.query.position-hit.location;
            const float radius=(std::max)(.001501f,request.radius-.0015f);
            if (edge.x*edge.x+edge.z*edge.z>=radius*radius) return false;
            // 验证下方存在可站立支撑。完整 perch/line-floor/edge 流程另行实现。
            auto floor=request;
            floor.origin=hit.location+glm::vec3(0,.001f,0);
            floor.direction={0,-1,0};floor.distance=.025f;
            VansPhysicsQueryHit support;
            return VansPhysicsQuery::SweepCapsuleClosestLocked(floor,support) &&
                !support.initialOverlap && support.normal.y>=walkable;
        }
    }

    VansCharacterSweepResult VansCharacterSweepSolver::AdvanceAirLocked(
        const VansPhysicsCapsuleSweepRequest& shape,
        const Vans::VansCharacterMotionStep& step, float walkableFloorCosine)
    {
        VansCharacterSweepResult result;
        result.position=shape.origin;result.velocity=step.velocity;
        auto request=shape;
        auto hit=Move(request,step.displacement,result);
        if (!hit.blocked) return result;
        float remaining=step.deltaTime*(1-hit.time);
        const auto land=[&]()
        {
            result.landingVelocity=result.velocity;result.landingNormal=hit.query.normal;
            result.grounded=true;result.velocity.y=0;result.unusedTime=remaining;
        };
        if (CanLand(request,hit,walkableFloorCosine)) {land();return result;}
        const bool limited=glm::dot(step.airControlAcceleration,step.airControlAcceleration)>0;
        glm::vec3 adjusted=step.velocity*step.deltaTime;
        if (limited)
            adjusted=(step.velocityWithoutAirControl+
                Vans::CharacterLimitAirControl(step.airControlAcceleration,hit.query.normal,
                    hit.query.initialOverlap)*step.deltaTime)*step.deltaTime;
        const glm::vec3 firstNormal=hit.query.normal;
        auto delta=Vans::CharacterAirSlide(adjusted,1-hit.time,firstNormal);
        if (remaining>1.e-4f) result.velocity=delta/remaining;
        if (remaining<=1.e-4f || glm::dot(delta,adjusted)<=0) return result;
        hit=Move(request,delta,result);
        if (!hit.blocked) return result;
        const float previousRemaining=remaining;
        remaining*=1-hit.time;
        if (CanLand(request,hit,walkableFloorCosine)) {land();return result;}
        if (limited && hit.query.normal.y>.001f)
            delta=Vans::CharacterAirSlide(step.velocityWithoutAirControl*previousRemaining,1,firstNormal);
        delta=Vans::CharacterTwoWallSlide(delta,1-hit.time,hit.query.normal,firstNormal);
        if (limited)
        {
            const auto change=Vans::CharacterLimitAirControl(step.airControlAcceleration,
                hit.query.normal,hit.query.initialOverlap)*remaining;
            if (glm::dot(change,firstNormal)>0) delta+=change*remaining;
        }
        if (remaining>1.e-4f) result.velocity=delta/remaining;
        hit=Move(request,delta,result);
        remaining*=1-hit.time;
        if (CanLand(request,hit,walkableFloorCosine)) {land();return result;}
        // 初始穿透、ditch 和移动基底仍需要完整求解；不能伪造落地。
        if (hit.blocked && glm::dot(result.velocity,hit.query.normal)<0)
            result.velocity-=hit.query.normal*glm::dot(result.velocity,hit.query.normal);
        return result;
    }
}
