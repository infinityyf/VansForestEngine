#include "VansCharacterSweepSolver.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace VansEngine
{
    namespace
    {
        using VansCharacterMoveHit = VansCharacterSafeMoveResult;

        glm::vec3 Normal2D(const glm::vec3& value,float tolerance=1.e-8f)
        {
            const glm::vec3 planar(value.x,0,value.z);
            const float squared=glm::dot(planar,planar);
            return squared>=tolerance ? planar/std::sqrt(squared) : glm::vec3(0);
        }

        float RandomFraction(VansCharacterSweepState& state)
        {
            state.randomSeed=state.randomSeed*196314165u+907633515u;
            // The upper 23 bits form the mantissa of a float in [1,2).
            return static_cast<float>(state.randomSeed>>9)*(1.f/8388608.f);
        }

        VansCharacterSafeMoveResult SweepMove(const VansPhysicsCapsuleSweepRequest& shape,
            const glm::vec3& position, const glm::vec3& delta, bool ignoreMovingOut)
        {
            VansCharacterSafeMoveResult hit;
            hit.query.normal=hit.query.impactNormal=glm::vec3(0);
            hit.position = position;
            hit.traceStart = position; hit.traceEnd = position+delta;
            const float distance = glm::length(delta);
            if (distance <= 4.e-6f) return hit;
            auto request = shape;
            request.origin = position;
            request.direction = delta/distance;
            request.distance = distance;
            request.computePenetration = true;
            request.ignoreInitialOverlapsMovingOut = ignoreMovingOut;
            ++hit.sweepCount;
            hit.blocked = VansPhysicsQuery::SweepCapsuleClosestLocked(request,hit.query);
            if (!hit.blocked) hit.query.normal=hit.query.impactNormal=glm::vec3(0);
            if (hit.blocked)
            {
                hit.location = position+request.direction*hit.query.distance;
                const float back = std::clamp(.1f,.001f/distance,.01f/distance)+.001f;
                hit.time = hit.query.initialOverlap ? 0 : std::clamp(hit.query.distance/distance-back,0.0f,1.0f);
            }
            if (hit.blocked && distance*hit.time <= 4.e-6f) hit.time=0;
            hit.position += delta*hit.time;
            return hit;
        }

        glm::vec3 PenetrationAdjustment(const VansPhysicsQueryHit& hit,
            const VansCharacterPenetrationSettings& settings)
        {
            if (!hit.initialOverlap) return glm::vec3(0);
            const float depth = hit.penetrationDepth>0 ? hit.penetrationDepth : .00125f;
            auto adjustment = hit.normal*(depth+std::abs(settings.pullbackDistance));
            const float distance = glm::length(adjustment);
            const float maximum = (std::max)(0.f,hit.isCharacterBody ?
                settings.maxCharacterDistance : settings.maxGeometryDistance);
            if (distance>maximum) adjustment *= maximum/distance;
            return adjustment;
        }

        bool ResolvePenetration(const VansPhysicsCapsuleSweepRequest& shape,
            const VansCharacterMoveHit& hit, const VansCharacterPenetrationSettings& settings,
            glm::vec3& position, int& sweepCount)
        {
            const auto adjustment = PenetrationAdjustment(hit.query,settings);
            if (glm::dot(adjustment,adjustment)==0) return false;
            VansPhysicsCapsuleOverlapRequest overlap;
            overlap.center = hit.traceStart+adjustment; overlap.axis = shape.axis;
            overlap.radius = shape.radius+settings.overlapInflation;
            overlap.halfHeight = shape.halfHeight+settings.overlapInflation;
            overlap.filter = shape.filter;
            if (!VansPhysicsQuery::OverlapCapsuleAnyLocked(overlap))
            {
                position += adjustment; return true;
            }
            const auto sweep = [&](const glm::vec3& delta)
            {
                auto moved = SweepMove(shape,position,delta,true);
                sweepCount += moved.sweepCount;
                const bool changed = glm::any(glm::notEqual(position,moved.position));
                position = moved.position;
                return std::make_pair(changed,moved);
            };
            auto moved = sweep(adjustment);
            if (!moved.first && moved.second.query.initialOverlap)
            {
                const auto second = PenetrationAdjustment(moved.second.query,settings);
                const auto combined = adjustment+second;
                if (glm::any(glm::notEqual(second,adjustment)) && glm::dot(combined,combined)>0)
                    moved = sweep(combined);
            }
            if (!moved.first)
            {
                const auto originalDelta = hit.traceEnd-hit.traceStart;
                if (glm::dot(originalDelta,originalDelta)>0)
                {
                    moved = sweep(adjustment+originalDelta);
                    if (!moved.first && glm::dot(originalDelta,adjustment)>0)
                        moved = sweep(originalDelta);
                }
            }
            return moved.first;
        }

        VansCharacterMoveHit Move(const VansPhysicsCapsuleSweepRequest& request,
            const glm::vec3& delta, VansCharacterSweepResult& result,
            const VansCharacterPenetrationSettings& settings)
        {
            auto shape = request; shape.origin = result.position;
            const auto hit = VansCharacterSweepSolver::SafeMoveLocked(shape,delta,settings,result.depenetrated);
            result.position = hit.position; result.sweepCount += hit.sweepCount;
            result.depenetrated |= hit.depenetrated;
            if (hit.blocked)
            {
                result.blocked = true; result.ceiling |= hit.query.normal.y<0;
                result.penetrating |= hit.query.initialOverlap;
            }
            return hit;
        }

        constexpr float MinFloorDistance = .019f;
        constexpr float MaxFloorDistance = .024f;
        constexpr float EdgeRejectDistance = .0015f;
        constexpr float LengthTolerance = .000001f;

        bool WithinEdge(const glm::vec3& center, const glm::vec3& point, float radius)
        {
            const auto edge = point-center;
            const float reduced = (std::max)(EdgeRejectDistance+LengthTolerance, radius-EdgeRejectDistance);
            return edge.x*edge.x+edge.z*edge.z < reduced*reduced;
        }

        bool Walkable(const VansPhysicsQueryHit& hit, float cosine)
        {
            return !hit.initialOverlap && hit.impactNormal.y >= 1.e-4f && hit.impactNormal.y >= cosine;
        }

        VansCharacterFloorResult ComputeFloor(const VansPhysicsCapsuleSweepRequest& shape,
            float lineDistance, float sweepDistance, float sweepRadius, float cosine,
            const VansCharacterMoveHit* downward = nullptr)
        {
            VansCharacterFloorResult floor;
            bool skipSweep = false;
            if (downward && downward->blocked && !downward->query.initialOverlap)
            {
                const auto delta = downward->traceStart-downward->traceEnd;
                if (delta.y > 0 && delta.x*delta.x+delta.z*delta.z <= 1.e-8f &&
                    WithinEdge(downward->location,downward->query.position,shape.radius))
                {
                    skipSweep = true;
                    floor.hit = downward->query; floor.hitLocation = downward->location;
                    floor.traceStart = downward->traceStart; floor.traceEnd = downward->traceEnd;
                    floor.blocking = true; floor.walkable = Walkable(floor.hit,cosine);
                    floor.floorDistance = shape.origin.y-downward->location.y;
                    if (floor.walkable) return floor;
                }
            }
            if (sweepDistance < lineDistance) return floor;
            if (!skipSweep && sweepDistance > 0 && sweepRadius > 0)
            {
                auto query = shape;
                float shrinkHeight = (shape.halfHeight-shape.radius)*(1-.9f);
                query.radius = sweepRadius;
                query.halfHeight = (std::max)(query.radius,shape.halfHeight-shrinkHeight);
                query.direction = {0,-1,0}; query.distance = sweepDistance+shrinkHeight;
                VansPhysicsQueryHit hit;
                bool blocked = VansPhysicsQuery::SweepCapsuleClosestLocked(query,hit);
                if (blocked)
                {
                    if (hit.initialOverlap || !WithinEdge(shape.origin,hit.position,query.radius))
                    {
                        query.radius = (std::max)(0.f,query.radius-EdgeRejectDistance-LengthTolerance);
                        if (query.radius > LengthTolerance)
                        {
                            shrinkHeight = (shape.halfHeight-shape.radius)*(1-.1f);
                            query.halfHeight = (std::max)(query.radius,shape.halfHeight-shrinkHeight);
                            query.distance = sweepDistance+shrinkHeight;
                            blocked = VansPhysicsQuery::SweepCapsuleClosestLocked(query,hit);
                        }
                    }
                    floor.hit = hit; floor.blocking = blocked && !hit.initialOverlap;
                    floor.traceStart = query.origin; floor.traceEnd = query.origin+query.direction*query.distance;
                    const float hitDistance = blocked ? hit.distance : query.distance;
                    floor.hitLocation = query.origin+query.direction*hitDistance;
                    floor.floorDistance = (std::max)(-(std::max)(MaxFloorDistance,shape.radius),
                        hitDistance-shrinkHeight);
                    if (blocked && Walkable(hit,cosine) && floor.floorDistance <= sweepDistance)
                    {
                        floor.walkable = true; return floor;
                    }
                }
            }
            if (!floor.blocking && !floor.hit.initialOverlap)
            {
                floor.floorDistance = sweepDistance; return floor;
            }
            if (lineDistance > 0)
            {
                VansPhysicsRaycastRequest line;
                line.origin = shape.origin; line.direction = {0,-1,0};
                line.distance = lineDistance+shape.halfHeight; line.filter = shape.filter;
                VansPhysicsQueryHit hit;
                if (VansPhysicsQuery::RaycastClosestLocked(line,hit) && hit.distance > 0)
                {
                    floor.blocking = true;
                    const float distance = (std::max)(-(std::max)(MaxFloorDistance,shape.radius),
                        hit.distance-shape.halfHeight);
                    if (distance <= lineDistance && Walkable(hit,cosine))
                    {
                        hit.position = floor.hit.position;
                        floor.hit = hit; floor.lineDistance = distance;
                        floor.walkable = true; floor.lineTrace = true;
                        // 保留 sweep 的胶囊命中中心；射线只替换表面信息。
                        return floor;
                    }
                }
            }
            floor.walkable = false;
            return floor;
        }

        VansCharacterFloorResult FindFloor(const VansPhysicsCapsuleSweepRequest& shape,
            float cosine, const VansCharacterFloorSettings& settings, bool grounded,
            const VansCharacterMoveHit* downward = nullptr)
        {
            const float heightAdjust = grounded ? MaxFloorDistance+LengthTolerance : -MaxFloorDistance;
            const float distance = (std::max)(MaxFloorDistance,settings.maxStepHeight+heightAdjust);
            auto floor = ComputeFloor(shape,distance,distance,shape.radius,cosine,downward);
            const float threshold = (std::max)(0.f,settings.perchRadiusThreshold);
            const float requestedRadius = shape.radius-threshold;
            const float validRadius = requestedRadius < .0011f ? .0011f :
                (requestedRadius < shape.radius ? requestedRadius : shape.radius);
            const auto edge = floor.hit.position-floor.hitLocation;
            if (floor.blocking && !floor.lineTrace && !floor.hit.initialOverlap &&
                threshold > EdgeRejectDistance && edge.x*edge.x+edge.z*edge.z > validRadius*validRadius)
            {
                const float maxDistance = distance+(grounded ? (std::max)(0.f,settings.perchAdditionalHeight) : 0.f);
                const float aboveBase = (std::max)(0.f,floor.hit.position.y-floor.hitLocation.y+shape.halfHeight);
                auto perchShape = shape; perchShape.origin = floor.hitLocation;
                auto perch = ComputeFloor(perchShape,(std::max)(0.f,maxDistance-aboveBase),
                    maxDistance+shape.radius,validRadius,cosine);
                if (!perch.walkable || aboveBase+perch.floorDistance > maxDistance)
                    floor.walkable = false;
                else
                {
                    const float average = (MinFloorDistance+MaxFloorDistance)*.5f;
                    if (average-floor.floorDistance+perch.floorDistance >= maxDistance)
                        floor.floorDistance = average;
                    if (!floor.walkable)
                    {
                        perch.hit.position = floor.hit.position;
                        floor.hit = perch.hit; floor.walkable = true; floor.lineTrace = true;
                        floor.lineDistance = (std::max)(floor.floorDistance,MinFloorDistance);
                    }
                }
            }
            return floor;
        }

        bool CanLand(const VansPhysicsCapsuleSweepRequest& request, const glm::vec3& location,
            const VansCharacterMoveHit& hit, float cosine, const VansCharacterFloorSettings& settings)
        {
            if (!hit.blocked) return false;
            if (!hit.query.initialOverlap)
            {
                if (!Walkable(hit.query,cosine) ||
                    hit.query.position.y >= hit.location.y-request.halfHeight+request.radius ||
                    !WithinEdge(hit.location,hit.query.position,request.radius)) return false;
            }
            else if (hit.query.normal.y < 1.e-4f) return false;
            auto shape = request; shape.origin = location;
            return FindFloor(shape,cosine,settings,false,&hit).walkable;
        }

        bool NearlyZero(const glm::vec3& value,float tolerance)
        {
            const auto a=glm::abs(value);
            return a.x<=tolerance && a.y<=tolerance && a.z<=tolerance;
        }

        glm::vec3 MaintainGroundVelocity(const glm::vec3& velocity,bool horizontal)
        {
            if (velocity.y==0) return velocity;
            return horizontal ? glm::vec3(velocity.x,0,velocity.z) : Normal2D(velocity,1.e-12f)*glm::length(velocity);
        }

        glm::vec3 GroundDelta(const glm::vec3& delta,const VansCharacterFloorResult& floor,
            float cosine,bool horizontal)
        {
            const auto& normal=floor.hit.impactNormal;
            if (normal.y<1-1.e-4f && normal.y>1.e-4f && floor.hit.normal.y>1.e-4f &&
                !floor.lineTrace && Walkable(floor.hit,cosine))
            {
                glm::vec3 ramp(delta.x,-glm::dot(normal,delta)/normal.y,delta.z);
                if (!horizontal)
                {
                    const float length=glm::length(ramp);
                    ramp=length*length>=1.e-12f ? ramp*(glm::length(delta)/length) : glm::vec3(0);
                }
                return ramp;
            }
            return delta;
        }

        glm::vec3 GroundTwoWall(const glm::vec3& delta,const VansCharacterMoveHit& hit,
            const glm::vec3& previousNormal,const VansCharacterFloorResult& floor,
            float cosine,float maxStep)
        {
            glm::vec3 adjusted;
            const auto& normal=hit.query.normal;
            const float corner=glm::dot(previousNormal,normal),fraction=1-hit.time;
            if (corner<=0)
            {
                auto direction=glm::cross(normal,previousNormal);
                const float squared=glm::dot(direction,direction);
                direction=squared>=1.e-8f ? direction/std::sqrt(squared) : glm::vec3(0);
                adjusted=direction*(glm::dot(delta,direction)*fraction);
                if (glm::dot(delta,adjusted)<0) adjusted=-adjusted;
            }
            else
            {
                adjusted=(delta-normal*glm::dot(delta,normal))*fraction;
                if (glm::dot(adjusted,delta)<=0) adjusted=glm::vec3(0);
                else if (std::abs(corner-1)<1.e-4f) adjusted+=normal*.0001f;
            }
            if (adjusted.y>0)
            {
                if ((normal.y>=cosine || Walkable(hit.query,cosine)) && normal.y>1.e-4f)
                {
                    adjusted.y/=normal.y; adjusted*=fraction;
                    if (adjusted.y>maxStep) adjusted*=maxStep/adjusted.y;
                }
                else adjusted.y=0;
            }
            else if (adjusted.y<0 && floor.floorDistance<MinFloorDistance && floor.blocking) adjusted.y=0;
            return adjusted;
        }

        float SlideGround(const VansPhysicsCapsuleSweepRequest& shape,const glm::vec3& delta,
            float fraction,VansCharacterMoveHit& hit,VansCharacterSweepResult& result,
            const VansCharacterFloorResult& floor,float cosine,
            const VansCharacterFloorSettings& floorSettings,const VansCharacterPenetrationSettings& penetration)
        {
            if (!hit.blocked) return 0;
            auto normal=hit.query.normal;
            if (normal.y>0 && !Walkable(hit.query,cosine)) normal=Normal2D(normal);
            else if (normal.y < -1.e-4f && floor.floorDistance<MinFloorDistance && floor.blocking)
            {
                const auto& floorNormal=floor.hit.normal;
                if (glm::dot(delta,floorNormal)<0 && floorNormal.y<1-1.e-5f) normal=floorNormal;
                normal=Normal2D(normal);
            }
            auto slide=(delta-normal*glm::dot(delta,normal))*fraction;
            if (glm::dot(slide,delta)<=0) return 0;
            hit=Move(shape,slide,result,penetration);
            const float first=hit.time;
            float applied=first;
            if (hit.blocked && !hit.query.initialOverlap)
            {
                slide=GroundTwoWall(slide,hit,normal,floor,cosine,floorSettings.maxStepHeight);
                if (!NearlyZero(slide,1.e-5f) && glm::dot(slide,delta)>0)
                {
                    hit=Move(shape,slide,result,penetration);
                    applied+=hit.time*(1-first);
                }
            }
            return std::clamp(applied,0.f,1.f);
        }

        bool StepUp(const VansPhysicsCapsuleSweepRequest& shape,const glm::vec3& delta,
            const VansCharacterMoveHit& obstacle,VansCharacterSweepResult& result,
            const VansCharacterFloorResult& currentFloor,VansCharacterFloorResult& stepFloor,bool& computedFloor,
            float cosine,const VansCharacterFloorSettings& floorSettings,
            const VansCharacterPenetrationSettings& penetration,const VansCharacterGroundSettings& ground)
        {
            if (!obstacle.query.canCharacterStepUp || floorSettings.maxStepHeight<=0) return false;
            const auto start=result.position;
            const float impactY=obstacle.query.position.y;
            if (impactY>start.y+shape.halfHeight-shape.radius) return false;
            float baseY=start.y-shape.halfHeight,floorY=baseY;
            float up=floorSettings.maxStepHeight,down=up;
            if (currentFloor.walkable)
            {
                const float distance=(std::max)(0.f,currentFloor.lineTrace ? currentFloor.lineDistance : currentFloor.floorDistance);
                baseY-=distance;up=(std::max)(up-distance,0.f);down=floorSettings.maxStepHeight+MaxFloorDistance*2;
                const bool verticalFace=!WithinEdge(obstacle.location,obstacle.query.position,shape.radius);
                if (!currentFloor.lineTrace && !verticalFace) floorY=currentFloor.hit.position.y;
                else floorY-=currentFloor.floorDistance;
            }
            if (impactY<=baseY) return false;
            // Only the final capsule position is published. Rejected steps revert all intermediate motion.
            const auto reject=[&](){result.position=start;return false;};
            const auto rawMove=[&](const glm::vec3& movement)
            {
                const auto hit=SweepMove(shape,result.position,movement,true);
                result.position=hit.position;result.sweepCount+=hit.sweepCount;
                return hit;
            };
            const auto upHit=rawMove({0,up,0});
            if (upHit.query.initialOverlap) return reject();
            auto hit=rawMove(delta);
            if (hit.blocked)
            {
                if (hit.query.initialOverlap) return reject();
                const float forwardTime=hit.time;
                const float slideTime=SlideGround(shape,delta,1-hit.time,hit,result,currentFloor,
                    cosine,floorSettings,penetration);
                if (forwardTime==0 && slideTime==0) return reject();
            }
            hit=rawMove({0,-down,0});
            if (hit.query.initialOverlap) return reject();
            if (hit.blocked)
            {
                const float height=hit.query.position.y-floorY;
                if (height>floorSettings.maxStepHeight) return reject();
                if (!Walkable(hit.query,cosine) &&
                    (glm::dot(delta,hit.query.impactNormal)<0 || hit.location.y>start.y)) return reject();
                if (!WithinEdge(hit.location,hit.query.position,shape.radius)) return reject();
                if (height>0 && !hit.query.canCharacterStepUp) return reject();
                auto floorShape=shape;floorShape.origin=result.position;
                stepFloor=FindFloor(floorShape,cosine,floorSettings,true,&hit);
                if (hit.location.y>start.y && !stepFloor.blocking && obstacle.query.impactNormal.y<.08f) return reject();
                computedFloor=true;
            }
            result.depenetrated |= !ground.maintainHorizontalVelocity;
            return true;
        }

        void MoveAlongFloor(const VansPhysicsCapsuleSweepRequest& shape,const glm::vec3& delta,
            VansCharacterSweepResult& result,const VansCharacterFloorResult& floor,
            VansCharacterFloorResult& stepFloor,bool& computedFloor,float time,float cosine,
            const VansCharacterFloorSettings& floorSettings,const VansCharacterPenetrationSettings& penetration,
            const VansCharacterGroundSettings& ground,bool animationRootMotion)
        {
            if (!floor.walkable) return;
            auto ramp=GroundDelta(delta,floor,cosine,ground.maintainHorizontalVelocity);
            auto hit=Move(shape,ramp,result,penetration);
            if (hit.query.initialOverlap)
            {
                SlideGround(shape,delta,1,hit,result,floor,cosine,floorSettings,penetration);
                if (hit.query.initialOverlap) result.depenetrated=true;
                return;
            }
            if (!hit.blocked) return;
            float applied=hit.time;
            if (hit.time>0 && hit.query.normal.y>1.e-4f && Walkable(hit.query,cosine))
            {
                const float remaining=1-applied;
                VansCharacterFloorResult nextRamp;nextRamp.hit=hit.query;
                ramp=GroundDelta(delta*remaining,nextRamp,cosine,ground.maintainHorizontalVelocity);
                hit=Move(shape,ramp,result,penetration);
                applied=std::clamp(applied+hit.time*remaining,0.f,1.f);
            }
            if (!hit.blocked || hit.query.initialOverlap) return;
            const auto beforeStep=result.position;
            if (StepUp(shape,delta*(1-applied),hit,result,floor,stepFloor,computedFloor,
                cosine,floorSettings,penetration,ground))
            {
                if (!ground.maintainHorizontalVelocity && !animationRootMotion && (1-applied)*time>=1.e-4f)
                    result.velocity=glm::vec3((result.position-beforeStep)/((1-applied)*time))*glm::vec3(1,0,1);
            }
            else SlideGround(shape,delta,1-applied,hit,result,floor,cosine,floorSettings,penetration);
        }

        void AdjustFloorHeight(const VansPhysicsCapsuleSweepRequest& shape,VansCharacterFloorResult& floor,
            VansCharacterSweepResult& result,float cosine,const VansCharacterPenetrationSettings& penetration,
            bool horizontal)
        {
            if (!floor.walkable) return;
            float distance=floor.floorDistance;
            if (floor.lineTrace)
            {
                if (distance<MinFloorDistance && floor.lineDistance>=MinFloorDistance) return;
                distance=floor.lineDistance;
            }
            if (distance>=MinFloorDistance && distance<=MaxFloorDistance) return;
            const float startY=result.position.y,move=(MinFloorDistance+MaxFloorDistance)*.5f-distance;
            const auto hit=Move(shape,{0,move,0},result,penetration);
            if (!hit.blocked || hit.query.initialOverlap) floor.floorDistance+=move;
            else if (move>0) floor.floorDistance+=result.position.y-startY;
            else
            {
                floor.floorDistance=result.position.y-hit.location.y;
                if (Walkable(hit.query,cosine))
                {
                    floor.hit=hit.query;floor.hitLocation=hit.location;
                    floor.traceStart=hit.traceStart;floor.traceEnd=hit.traceEnd;
                    floor.walkable=true;floor.blocking=true;floor.lineTrace=false;floor.lineDistance=0;
                }
            }
            result.depenetrated |= !horizontal || distance<0;
        }

        bool CheckLedge(const VansPhysicsCapsuleSweepRequest& shape,const glm::vec3& side,
            float cosine,const VansCharacterFloorSettings& floorSettings,const VansCharacterGroundSettings& ground)
        {
            auto request=shape;request.direction=side;request.distance=glm::length(side);
            VansPhysicsQueryHit hit;
            bool blocked=VansPhysicsQuery::SweepCapsuleClosestLocked(request,hit);
            if (blocked && !Walkable(hit,cosine)) return false;
            if (!blocked)
            {
                request.origin+=side;request.direction={0,-1,0};
                request.distance=floorSettings.maxStepHeight+ground.ledgeCheckThreshold;
                blocked=VansPhysicsQuery::SweepCapsuleClosestLocked(request,hit);
            }
            return blocked && hit.distance<request.distance && Walkable(hit,cosine);
        }
    }

    VansCharacterSweepResult VansCharacterSweepSolver::InitializeGroundLocked(
        const VansPhysicsCapsuleSweepRequest& shape,float cosine,VansCharacterGroundState& state,
        const VansCharacterFloorSettings& floorSettings,const VansCharacterPenetrationSettings& penetration,
        const VansCharacterGroundSettings& ground)
    {
        VansCharacterSweepResult result;result.position=shape.origin;
        auto request=shape;request.computePenetration=true;
        state.floor=FindFloor(request,cosine,floorSettings,true);
        AdjustFloorHeight(request,state.floor,result,cosine,penetration,ground.maintainHorizontalVelocity);
        result.grounded=state.floor.walkable;state.valid=result.grounded;
        return result;
    }

    glm::vec3 VansCharacterSweepSolver::ImpartMovementBaseVelocity(const VansPhysicsQueryHit& base,
        const glm::vec3& feet,const glm::bvec3& enabledAxes,bool angularVelocity)
    {
        if (!base.supportMovable) return glm::vec3(0);
        auto velocity=base.supportLinearVelocity;
        const auto& angular=base.supportAngularVelocity;
        if (angularVelocity && (std::abs(angular.x)>1.e-4f || std::abs(angular.y)>1.e-4f || std::abs(angular.z)>1.e-4f))
            velocity+=glm::cross(angular,feet-base.supportPosition);
        return glm::vec3(enabledAxes.x ? velocity.x : 0,enabledAxes.y ? velocity.y : 0,
            enabledAxes.z ? velocity.z : 0);
    }

    VansCharacterSweepResult VansCharacterSweepSolver::AdvanceGroundLocked(
        const VansPhysicsCapsuleSweepRequest& shape,const Vans::VansCharacterMotionStep& step,float cosine,
        VansCharacterGroundState& state,const VansCharacterFloorSettings& floorSettings,
        const VansCharacterPenetrationSettings& penetration,const VansCharacterGroundSettings& ground)
    {
        VansCharacterSweepResult result;result.position=shape.origin;result.grounded=true;
        result.velocity=step.animationRootMotion ? step.velocity :
			MaintainGroundVelocity(step.velocity,ground.maintainHorizontalVelocity);
        auto request=shape;request.computePenetration=true;
        const auto oldFloor=state.valid && !state.floor.hit.supportMovable ? state.floor :
            FindFloor(request,cosine,floorSettings,true);
        const glm::vec3 delta(result.velocity.x*step.deltaTime,0,result.velocity.z*step.deltaTime);
        const bool zero=NearlyZero(delta,1.e-6f);
        bool computedFloor=false;VansCharacterFloorResult floor;
        bool forceFall=false;
        if (!zero) MoveAlongFloor(request,delta,result,oldFloor,floor,computedFloor,step.deltaTime,
            cosine,floorSettings,penetration,ground,step.animationRootMotion);
        else result.stopSimulation=true;
        if (!computedFloor)
        {
            request.origin=result.position;floor=FindFloor(request,cosine,floorSettings,true);
        }
        if (!ground.canWalkOffLedges && !floor.walkable)
        {
            request.origin=shape.origin;
            glm::vec3 side(delta.z,0,-delta.x);
            if (!zero && !state.triedLedgeMove &&
                (CheckLedge(request,side,cosine,floorSettings,ground) ||
                (side=-side,CheckLedge(request,side,cosine,floorSettings,ground))))
            {
                // Retry in the next Walking slice so acceleration and braking use
                // the side velocity, just as they do after any other refunded step.
                state.triedLedgeMove=true;state.floor=oldFloor;
                state.valid=oldFloor.walkable && !oldFloor.hit.initialOverlap;
                result.position=shape.origin;result.velocity=side/step.deltaTime;
                result.unusedTime=step.deltaTime;result.retryGroundLedge=true;
                result.blocked=result.ceiling=result.penetrating=result.depenetrated=false;
                return result;
            }
            if (!floor.walkable && !zero && oldFloor.hit.actorIdentity)
            {
                result.position=shape.origin;result.velocity=glm::vec3(0);
                result.stopSimulation=true;state.floor=oldFloor;state.valid=true;
                return result;
            }
            forceFall=!floor.walkable;
        }
        if (floor.walkable) AdjustFloorHeight(request,floor,result,cosine,penetration,ground.maintainHorizontalVelocity);
        else if (floor.hit.initialOverlap && step.remainingTime<=0)
        {
            VansCharacterMoveHit overlap;overlap.query=floor.hit;overlap.traceStart=floor.traceStart;
            overlap.traceEnd=overlap.traceStart+glm::vec3(0,MaxFloorDistance,0);
            const bool corrected=ResolvePenetration(shape,overlap,penetration,result.position,result.sweepCount);
            result.depenetrated |= corrected;state.valid=false;
        }
        if ((!floor.walkable && !floor.hit.initialOverlap) || forceFall)
        {
            result.grounded=false;result.stopSimulation=zero;
            const float desired=glm::length(delta),actual=glm::length(glm::vec2(result.position.x-shape.origin.x,
                result.position.z-shape.origin.z));
            result.unusedTime=desired<1.e-6f ? 0 : step.deltaTime*(1-(std::min)(1.f,actual/desired));
        }
        else if (!result.depenetrated && !step.animationRootMotion && step.deltaTime>=1.e-6f)
            result.velocity=MaintainGroundVelocity((result.position-shape.origin)/step.deltaTime,ground.maintainHorizontalVelocity);
        if (result.grounded && glm::all(glm::equal(result.position,shape.origin))) result.stopSimulation=true;
        state.floor=floor;state.valid=result.grounded && !floor.hit.initialOverlap;
        return result;
    }

    VansCharacterSweepResult VansCharacterSweepSolver::AdvanceAirLocked(
        const VansPhysicsCapsuleSweepRequest& shape,
        const Vans::VansCharacterMotionStep& step, float walkableFloorCosine,
        VansCharacterSweepState& state,
        const VansCharacterFloorSettings& floorSettings,
        const VansCharacterPenetrationSettings& penetrationSettings, bool useImpactBodyVelocity)
    {
        VansCharacterSweepResult result;
        result.position=shape.origin;result.velocity=step.velocity;
        auto request=shape;
        auto hit=Move(request,step.displacement,result,penetrationSettings);
        if (!hit.blocked) return result;
        float remaining=step.deltaTime*(1-hit.time);
        const auto land=[&]()
        {
            result.landingVelocity=result.velocity;result.landingNormal=hit.query.normal;
            result.grounded=true;result.velocity.y=0;result.unusedTime=remaining;
        };
        if (CanLand(request,result.position,hit,walkableFloorCosine,floorSettings)) {land();return result;}
        // 胶囊下半部先撞到棱边时，接触法线和表面法线不同；向下复核顶面。
        const auto normalDifference = glm::abs(hit.query.normal-hit.query.impactNormal);
        if (!hit.query.initialOverlap && hit.query.normal.y > 1.e-4f &&
            (normalDifference.x > 1.e-4f || normalDifference.y > 1.e-4f || normalDifference.z > 1.e-4f) &&
            WithinEdge(result.position,hit.query.position,request.radius))
        {
            auto floorShape = request; floorShape.origin = result.position;
            const auto floor = FindFloor(floorShape,walkableFloorCosine,floorSettings,false);
            if (floor.walkable)
            {
                VansCharacterMoveHit floorHit;
                floorHit.blocked = floor.blocking; floorHit.query = floor.hit;
                floorHit.location = floor.hitLocation;
                floorHit.traceStart = floor.traceStart; floorHit.traceEnd = floor.traceEnd;
                if (CanLand(request,result.position,floorHit,walkableFloorCosine,floorSettings))
                {
                    hit = floorHit; land(); return result;
                }
            }
        }
        const bool limited=glm::dot(step.airControlAcceleration,step.airControlAcceleration)>0;
        glm::vec3 adjusted=step.velocity*step.deltaTime;
        if (limited)
            adjusted=(step.velocityWithoutAirControl+
                Vans::CharacterLimitAirControl(step.airControlAcceleration,hit.query.normal,
                    hit.query.initialOverlap)*step.deltaTime)*step.deltaTime;
        const glm::vec3 firstNormal=hit.query.normal;
        const glm::vec3 firstImpactNormal=hit.query.impactNormal;
        auto delta=Vans::CharacterAirSlide(adjusted,1-hit.time,firstNormal);
        if (useImpactBodyVelocity && hit.query.supportSimulated &&
            (std::abs(result.velocity.x)>1.e-6f || std::abs(result.velocity.y)>1.e-6f || std::abs(result.velocity.z)>1.e-6f))
            result.velocity-=hit.query.impactNormal*glm::dot(
                result.velocity-hit.query.supportContactVelocity,hit.query.impactNormal);
        else if (remaining>1.e-4f && !result.depenetrated) result.velocity=delta/remaining;
		if (step.animationRootMotion) { result.velocity.x=step.velocity.x;result.velocity.z=step.velocity.z; }
        if (remaining<=1.e-4f || glm::dot(delta,adjusted)<=0) return result;
        hit=Move(request,delta,result,penetrationSettings);
        if (!hit.blocked) return result;
        const float previousRemaining=remaining;
        remaining*=1-hit.time;
        if (CanLand(request,result.position,hit,walkableFloorCosine,floorSettings)) {land();return result;}
        if (limited && hit.query.normal.y>.001f)
            delta=Vans::CharacterAirSlide(step.velocityWithoutAirControl*previousRemaining,1,firstNormal);
        delta=Vans::CharacterTwoWallSlide(delta,1-hit.time,hit.query.normal,firstNormal);
        if (limited)
        {
            const auto change=Vans::CharacterLimitAirControl(step.airControlAcceleration,
                hit.query.normal,hit.query.initialOverlap)*remaining;
            if (glm::dot(change,firstNormal)>0) delta+=change*remaining;
        }
        if (remaining>1.e-4f && !result.depenetrated) result.velocity=delta/remaining;
		if (step.animationRootMotion) { result.velocity.x=step.velocity.x;result.velocity.z=step.velocity.z; }
        const bool ditch=firstImpactNormal.y>0 && hit.query.impactNormal.y>0 &&
            std::abs(delta.y)<=1.e-6f && glm::dot(hit.query.impactNormal,firstImpactNormal)<0;
        hit=Move(request,delta,result,penetrationSettings);
        if (hit.time==0)
        {
            auto side=Normal2D(firstNormal+hit.query.impactNormal);
            if (std::abs(side.x)<=1.e-4f && std::abs(side.z)<=1.e-4f)
                side=Normal2D({firstNormal.z,0,-firstNormal.x});
            hit=Move(request,side*.01f,result,penetrationSettings);
        }
        if (ditch || CanLand(request,result.position,hit,walkableFloorCosine,floorSettings) || hit.time==0)
        {
            // This terminal two-wall branch consumes the whole remaining slice.
            remaining=0; land(); return result;
        }
        if (floorSettings.perchRadiusThreshold>0 && hit.time==1 && firstImpactNormal.y>=walkableFloorCosine)
        {
            const auto moved=result.position-shape.origin;
            if (std::abs(moved.y)<=.002f*step.deltaTime &&
                std::sqrt(moved.x*moved.x+moved.z*moved.z)<=.04f*step.deltaTime)
            {
                result.velocity.x+=.25f*step.maximumSpeed*(RandomFraction(state)-.5f);
                result.velocity.z+=.25f*step.maximumSpeed*(RandomFraction(state)-.5f);
                result.velocity.y=(std::max)(step.jumpSpeed*.25f,.01f);
                Move(request,result.velocity*step.deltaTime,result,penetrationSettings);
            }
        }
        return result;
    }

    VansCharacterSafeMoveResult VansCharacterSweepSolver::SafeMoveLocked(
        const VansPhysicsCapsuleSweepRequest& shape, const glm::vec3& delta,
        const VansCharacterPenetrationSettings& settings, bool previouslyAdjusted)
    {
        auto result = SweepMove(shape,shape.origin,delta,false);
        if (result.query.initialOverlap)
        {
            int correctionSweeps = 0;
            const bool corrected=ResolvePenetration(shape,result,settings,result.position,correctionSweeps);
            if (corrected || previouslyAdjusted)
            {
                const int firstSweeps = result.sweepCount;
                auto retry = SweepMove(shape,result.position,delta,true);
                if (corrected && !retry.blocked && glm::dot(delta,result.query.normal)<=0)
                {
                    // 穿透修正恰好停在接触面时，浮点扫掠可能漏报仍然闭合的切向接触。
                    // 只在这次罕见的穿透重试中，按胶囊尺寸的浮点误差复核接触。
                    VansPhysicsCapsuleOverlapRequest contact;
                    contact.center=retry.traceStart;contact.axis=shape.axis;
                    const float roundoff=4.f*std::numeric_limits<float>::epsilon()*
                        (shape.radius+shape.halfHeight);
                    contact.radius=shape.radius+roundoff;contact.halfHeight=shape.halfHeight+roundoff;
                    contact.filter=shape.filter;
                    if (VansPhysicsQuery::OverlapCapsuleAnyLocked(contact))
                    {
                        retry.position=retry.traceStart;retry.time=0;retry.blocked=true;
                        retry.query=result.query;retry.query.initialOverlap=true;
                    }
                }
                retry.sweepCount += firstSweeps+correctionSweeps;
                retry.depenetrated = corrected;
                result = retry;
            }
            else result.sweepCount += correctionSweeps;
        }
        return result;
    }

    VansCharacterSafeMoveResult VansCharacterSweepSolver::SweepMoveLocked(
        const VansPhysicsCapsuleSweepRequest& request,const glm::vec3& delta,bool ignoreMovingOut)
    {
        return SweepMove(request,request.origin,delta,ignoreMovingOut);
    }

    VansCharacterFloorResult VansCharacterSweepSolver::FindFloorLocked(
        const VansPhysicsCapsuleSweepRequest& shape, float cosine,
        const VansCharacterFloorSettings& settings, bool movingOnGround)
    {
        return FindFloor(shape,cosine,settings,movingOnGround);
    }
}
