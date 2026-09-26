#include "VansPhysicsVehicle.h"
#include "VansPhysicsNativeAccess.h"
#include "VansCollisionFilter.h"
#include "../RuntimeCore/VansThreadContract.h"
#include "../Util/VansLog.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace VansEngine
{
    namespace
    {
        class VansVehicleRoadQueryFilter final : public PxQueryFilterCallback
        {
        public:
            PxQueryHitType::Enum preFilter(const PxFilterData& filterData,
                const PxShape* shape, const PxRigidActor* actor, PxHitFlags& queryFlags) override
            {
                (void)actor;
                (void)queryFlags;
                return FilterShape(filterData, shape);
            }

            PxQueryHitType::Enum postFilter(const PxFilterData& filterData,
                const PxQueryHit& hit, const PxShape* shape, const PxRigidActor* actor) override
            {
                (void)hit;
                (void)actor;
                return FilterShape(filterData, shape);
            }

        private:
            static PxQueryHitType::Enum FilterShape(const PxFilterData& filterData, const PxShape* shape)
            {
                if (!shape)
                    return PxQueryHitType::eNONE;

                const PxFilterData targetData = shape->getQueryFilterData();
                const bool targetIsTrigger = (targetData.word2 & 0x1u) != 0u;
                if (targetIsTrigger)
                    return PxQueryHitType::eNONE;

                const PxU32 targetLayer = targetData.word0;
                const PxU32 queryMask = filterData.word1;
                if (targetLayer >= 32u || (queryMask & (1u << targetLayer)) == 0u)
                    return PxQueryHitType::eNONE;

                return PxQueryHitType::eBLOCK;
            }
        };

        VansVehicleRoadQueryFilter g_VehicleRoadQueryFilter;
    }

    // ============================================================================
    // VansPhysicsVehicle Implementation
    // ============================================================================

    VansPhysicsVehicle::VansPhysicsVehicle()
    {
        m_State.Reset();
        m_CommandState.setToDefault();
        m_TransmissionCommandState.setToDefault();
        m_SimulationContext.setToDefault();
    }

    VansPhysicsVehicle::~VansPhysicsVehicle()
    {
        Shutdown();
    }

    bool VansPhysicsVehicle::Initialize(VansPhysicsSystem* physicsSystem, const PxTransform& startPose, std::string& error)
    {
        Shutdown();
        error.clear();
        if (!m_Tuning.IsValid(error))
            return false;

        m_PhysicsSystem = physicsSystem;
        if (!m_PhysicsSystem ||
            !VansPhysicsNativeAccess::Physics(*m_PhysicsSystem) ||
            !VansPhysicsNativeAccess::Scene(*m_PhysicsSystem))
        {
            error = "Vehicle requires an initialized physics system";
            m_PhysicsSystem = nullptr;
            return false;
        }
        const PxCookingParams* cookingParams =
            VansPhysicsNativeAccess::CookingParams(*m_PhysicsSystem);
        if (!cookingParams)
        {
            error = "Vehicle requires the physics system cooking parameters";
            m_PhysicsSystem = nullptr;
            return false;
        }

        // -- Initialize State --
        // Must be done BEFORE creating the actor, otherwise we wipe the actor pointer!
        PxMemZero(&m_Params, sizeof(VansVehicleParams));
        m_State.Reset();
        m_CommandState.setToDefault();
        m_TransmissionCommandState.setToDefault();
        m_ComponentSequence = PxVehicleComponentSequence{};
        m_DrivetrainSubstepGroup = PxVehicleComponentSequence::eINVALID_SUBSTEP_GROUP;

        // Vehicle tuning is resolved before runtime construction. Keep this path focused on
        // building PhysX vehicle params from the current tuning state.
        
        // -- Axles --
        // Axle 0: Front wheels (0, 1)
        // Axle 1: Rear wheels (2, 3)
        PxU32 frontWheels[] = { 0, 1 };
        PxU32 rearWheels[] = { 2, 3 };
        m_Params.axleDescription.setToDefault();
        m_Params.axleDescription.addAxle(2, frontWheels);
        m_Params.axleDescription.addAxle(2, rearWheels);

        // -- Frame --
        m_Params.frame = m_Tuning.BuildFrame();

        // -- Scale --
        m_Params.scale.scale = 1.0f;
        const PxVec3 upAxis = m_Params.frame.getVrtAxis();

        // -- Rigid Body (from Base.json) --
        m_Params.rigidBodyParams.mass = m_Tuning.bodyMass;
        m_Params.rigidBodyParams.moi = m_Tuning.bodyMoi;

        // -- Brake Command Response Params (from Base.json) --
        // brakeResponseParams[0] = foot brake, brakeResponseParams[1] = handbrake
        m_Params.brakeResponseParams[0].maxResponse = m_Tuning.brakeMaxTorque;
        for (int i = 0; i < 4; i++)
            m_Params.brakeResponseParams[0].wheelResponseMultipliers[i] = 1.0f;

        m_Params.brakeResponseParams[1].maxResponse = m_Tuning.handbrakeMaxTorque; // handbrake
        m_Params.brakeResponseParams[1].wheelResponseMultipliers[0] = 0.0f;
        m_Params.brakeResponseParams[1].wheelResponseMultipliers[1] = 0.0f;
        m_Params.brakeResponseParams[1].wheelResponseMultipliers[2] = 1.0f;
        m_Params.brakeResponseParams[1].wheelResponseMultipliers[3] = 1.0f;

        // -- Steer Command Response Params (from Base.json) --
        m_Params.steerResponseParams.maxResponse = m_Tuning.maxSteerAngleRadians;
        m_Params.steerResponseParams.wheelResponseMultipliers[0] = 1.0f; // Front left
        m_Params.steerResponseParams.wheelResponseMultipliers[1] = 1.0f; // Front right
        m_Params.steerResponseParams.wheelResponseMultipliers[2] = 0.0f; // Rear left
        m_Params.steerResponseParams.wheelResponseMultipliers[3] = 0.0f; // Rear right

        // -- Ackermann Params (from Base.json) --
        m_Params.ackermannParams[0].wheelIds[0] = 0;
        m_Params.ackermannParams[0].wheelIds[1] = 1;
        m_Params.ackermannParams[0].wheelBase = m_Tuning.ackermannWheelBase;
        m_Params.ackermannParams[0].trackWidth = m_Tuning.ackermannTrackWidth;
        m_Params.ackermannParams[0].strength = m_Tuning.ackermannStrength;

        // -- Wheels & Suspension (from Base.json) --
        // Wheel positions from Base.json suspension attachment points
        for (int i = 0; i < 4; i++)
        {
            // Wheel params (from Base.json)
            m_Params.wheelParams[i].radius = m_Tuning.wheelRadius;
            m_Params.wheelParams[i].halfWidth = m_Tuning.wheelHalfWidth;
            m_Params.wheelParams[i].mass = m_Tuning.wheelMass;
            m_Params.wheelParams[i].moi = m_Tuning.wheelMoi;
            m_Params.wheelParams[i].dampingRate = m_Tuning.wheelDampingRate;

            // Suspension params (from Base.json)
            m_Params.suspensionParams[i].suspensionAttachment.p = m_Tuning.suspensionAttachmentPositions[i];
            m_Params.suspensionParams[i].suspensionAttachment.q = PxQuat(PxIdentity);
            m_Params.suspensionParams[i].suspensionTravelDir = -upAxis;
            m_Params.suspensionParams[i].suspensionTravelDist = m_Tuning.suspensionTravelDist;
            m_Params.suspensionParams[i].wheelAttachment.p = PxVec3(0, 0, 0);
            m_Params.suspensionParams[i].wheelAttachment.q = PxQuat(PxIdentity);

            // Suspension force params (from Base.json)
            m_Params.suspensionForceParams[i].stiffness = m_Tuning.suspensionStiffness[i];
            m_Params.suspensionForceParams[i].damping = m_Tuning.suspensionDamping[i];
            m_Params.suspensionForceParams[i].sprungMass = m_Tuning.sprungMass[i];

            // Tire force params (from Base.json)
            m_Params.tireForceParams[i].longStiff = m_Tuning.tireLongitudinalStiffness;
            m_Params.tireForceParams[i].latStiffX = m_Tuning.tireLateralStiffnessX;
            m_Params.tireForceParams[i].latStiffY = m_Tuning.tireLateralStiffnessY[i];
            m_Params.tireForceParams[i].camberStiff = m_Tuning.tireCamberStiffness;
            m_Params.tireForceParams[i].restLoad = m_Tuning.tireRestLoad[i];
            for (PxU32 point = 0; point < m_Tuning.tireFrictionVsSlip.size(); ++point)
            {
                m_Params.tireForceParams[i].frictionVsSlip[point][0] = m_Tuning.tireFrictionVsSlip[point].input;
                m_Params.tireForceParams[i].frictionVsSlip[point][1] = m_Tuning.tireFrictionVsSlip[point].output;
            }
            for (PxU32 point = 0; point < m_Tuning.tireLoadFilter.size(); ++point)
            {
                m_Params.tireForceParams[i].loadFilter[point][0] = m_Tuning.tireLoadFilter[point].input;
                m_Params.tireForceParams[i].loadFilter[point][1] = m_Tuning.tireLoadFilter[point].output;
            }
        }

        // -- Suspension State Calculation Params (from snippet Base.json) --
        m_Params.suspensionStateCalculationParams.suspensionJounceCalculationType = PxVehicleSuspensionJounceCalculationType::eRAYCAST;
        m_Params.suspensionStateCalculationParams.limitSuspensionExpansionVelocity = false;

        // -- Engine Params (from EngineDrive.json) --
        for (const VansVehicleCurvePoint& point : m_Tuning.engineTorqueCurve)
            m_Params.engineParams.torqueCurve.addPair(point.input, point.output);
        m_Params.engineParams.moi = m_Tuning.engineMoi;
        m_Params.engineParams.peakTorque = m_Tuning.enginePeakTorque;
        m_Params.engineParams.idleOmega = m_Tuning.engineIdleOmega;
        m_Params.engineParams.maxOmega = m_Tuning.engineMaxOmega;
        m_Params.engineParams.dampingRateFullThrottle = m_Tuning.engineDampingFullThrottle;
        m_Params.engineParams.dampingRateZeroThrottleClutchEngaged = m_Tuning.engineDampingZeroThrottleClutchEngaged;
        m_Params.engineParams.dampingRateZeroThrottleClutchDisengaged = m_Tuning.engineDampingZeroThrottleClutchDisengaged;

        // -- Gearbox Params (from EngineDrive.json) --
        // Ratios: [reverse, neutral, 1st, 2nd, 3rd, 4th, 5th] => neutralGear index = 1
        m_Params.gearboxParams.neutralGear = m_Tuning.neutralGear;
        for (PxU32 gear = 0; gear < m_Tuning.gearRatios.size(); ++gear)
            m_Params.gearboxParams.ratios[gear] = m_Tuning.gearRatios[gear];
        m_Params.gearboxParams.nbRatios = static_cast<PxU32>(m_Tuning.gearRatios.size());
        m_Params.gearboxParams.finalRatio = m_Tuning.gearboxFinalRatio;
        m_Params.gearboxParams.switchTime = m_Tuning.gearboxSwitchTime;

        // -- Autobox Params (from EngineDrive.json) --
        for (PxU32 gear = 0; gear < m_Tuning.gearRatios.size(); ++gear)
        {
            m_Params.autoboxParams.upRatios[gear] = m_Tuning.autoboxUpRatios[gear];
            m_Params.autoboxParams.downRatios[gear] = m_Tuning.autoboxDownRatios[gear];
        }
        m_Params.autoboxParams.latency = m_Tuning.autoboxLatency;

        // -- Clutch Command Response Params (from EngineDrive.json) --
        m_Params.clutchCommandResponseParams.maxResponse = m_Tuning.clutchStrength;

        // -- Clutch Params (from EngineDrive.json) --
        m_Params.clutchParams.accuracyMode = PxVehicleClutchAccuracyMode::eESTIMATE;
        m_Params.clutchParams.estimateIterations = m_Tuning.clutchEstimateIterations;

        // -- Four Wheel Differential Params (from EngineDrive.json) --
        for (PxU32 wheel = 0; wheel < 4; ++wheel)
        {
            m_Params.fourWheelDifferentialParams.torqueRatios[wheel] = m_Tuning.differentialTorqueRatios[wheel];
            m_Params.fourWheelDifferentialParams.aveWheelSpeedRatios[wheel] = m_Tuning.differentialAverageWheelSpeedRatios[wheel];
        }
        m_Params.fourWheelDifferentialParams.frontWheelIds[0] = 0;
        m_Params.fourWheelDifferentialParams.frontWheelIds[1] = 1;
        m_Params.fourWheelDifferentialParams.rearWheelIds[0] = 2;
        m_Params.fourWheelDifferentialParams.rearWheelIds[1] = 3;
        m_Params.fourWheelDifferentialParams.centerBias = m_Tuning.differentialCenterBias;
        m_Params.fourWheelDifferentialParams.centerTarget = m_Tuning.differentialCenterTarget;
        m_Params.fourWheelDifferentialParams.frontBias = m_Tuning.differentialFrontBias;
        m_Params.fourWheelDifferentialParams.frontTarget = m_Tuning.differentialFrontTarget;
        m_Params.fourWheelDifferentialParams.rearBias = m_Tuning.differentialRearBias;
        m_Params.fourWheelDifferentialParams.rearTarget = m_Tuning.differentialRearTarget;
        m_Params.fourWheelDifferentialParams.rate = m_Tuning.differentialRate;

        // -- PhysX Integration Params --
        // Set up road geometry query, material friction, suspension limit constraint params
        // following the snippet's setPhysXIntegrationParams pattern.
        PxPhysics* physics = VansPhysicsNativeAccess::Physics(*m_PhysicsSystem);
        m_Material = physics->createMaterial(
            m_Tuning.materialStaticFriction,
            m_Tuning.materialDynamicFriction,
            m_Tuning.materialRestitution);
        if (!m_Material)
        {
            error = "Vehicle material creation failed";
            Shutdown();
            return false;
        }
        PxFilterData vehicleFilterData;
        if (!VansCollisionFilter::Build(
            m_Tuning.collisionLayerName,
            VansCollisionFilter::None,
            0u,
            vehicleFilterData))
        {
            error = "Vehicle collision layer is unknown: " + m_Tuning.collisionLayerName;
            Shutdown();
            return false;
        }

        PxFilterData roadQueryFilterData = vehicleFilterData;
        roadQueryFilterData.word1 = m_Tuning.useCustomRoadQueryMask
            ? m_Tuning.roadQueryMask
            : vehicleFilterData.word1;

        PxQueryFlags roadQueryFlags = PxQueryFlag::eSTATIC;
        PxQueryFilterCallback* roadQueryFilterCallback = nullptr;
        if (m_Tuning.useRoadQueryLayerFilter)
        {
            roadQueryFlags |= PxQueryFlag::ePREFILTER;
            roadQueryFilterCallback = &g_VehicleRoadQueryFilter;
        }

        m_Params.physxRoadGeometryQueryParams.roadGeometryQueryType = PxVehiclePhysXRoadGeometryQueryType::eRAYCAST;
        m_Params.physxRoadGeometryQueryParams.defaultFilterData = PxQueryFilterData(
            roadQueryFilterData,
            roadQueryFlags);
        m_Params.physxRoadGeometryQueryParams.filterCallback = roadQueryFilterCallback;
        m_Params.physxRoadGeometryQueryParams.filterDataEntries = nullptr;

        for (PxU32 i = 0; i < m_Params.axleDescription.nbWheels; i++)
        {
            const PxU32 wheelId = m_Params.axleDescription.wheelIdsInAxleOrder[i];
            m_Params.physxMaterialFrictionParams[wheelId].defaultFriction = m_Tuning.tireFriction;
            m_Params.physxMaterialFrictionParams[wheelId].materialFrictions = nullptr;
            m_Params.physxMaterialFrictionParams[wheelId].nbMaterialFrictions = 0;

            m_Params.physxSuspensionLimitConstraintParams[wheelId].restitution = m_Tuning.suspensionLimitRestitution;
            m_Params.physxSuspensionLimitConstraintParams[wheelId].directionForSuspensionLimitConstraint =
                PxVehiclePhysXSuspensionLimitConstraintParams::eROAD_GEOMETRY_NORMAL;

            m_Params.physxWheelShapeLocalPoses[wheelId] = PxTransform(PxIdentity);
        }

        // CMass local pose, body shape extents & local pose (matching snippet defaults)
        m_Params.physxActorCMassLocalPose = m_Tuning.centerOfMassLocalPose;
        m_Params.physxActorBoxShapeHalfExtents = m_Tuning.bodyBoxHalfExtents;
        m_Params.physxActorBoxShapeLocalPose = m_Tuning.bodyBoxLocalPose;

        // -- Create Rigid Body + Wheel Shapes via PxVehiclePhysXActorCreate --
        // This creates the PxRigidDynamic, attaches a box body shape and convex-mesh wheel shapes,
        // sets mass/MOI/CMass, disables gravity (vehicle SDK handles gravity itself).
        {
            const PxVehiclePhysXRigidActorParams rigidActorParams(m_Params.rigidBodyParams, nullptr);
            const PxBoxGeometry boxGeom(m_Params.physxActorBoxShapeHalfExtents);
            const PxShapeFlags chassisShapeFlags(
                PxShapeFlag::eSIMULATION_SHAPE |
                PxShapeFlag::eSCENE_QUERY_SHAPE |
                PxShapeFlag::eVISUALIZATION);
            PxShapeFlags wheelShapeFlags(
                PxShapeFlag::eSCENE_QUERY_SHAPE |
                PxShapeFlag::eVISUALIZATION);
            if (m_Tuning.enableWheelSimulationCollision)
                wheelShapeFlags |= PxShapeFlag::eSIMULATION_SHAPE;

            const PxVehiclePhysXRigidActorShapeParams rigidActorShapeParams(
                boxGeom, m_Params.physxActorBoxShapeLocalPose, *m_Material,
                chassisShapeFlags, vehicleFilterData, vehicleFilterData);
            const PxVehiclePhysXWheelParams physxWheelParams(
                m_Params.axleDescription, m_Params.wheelParams);
            const PxVehiclePhysXWheelShapeParams physxWheelShapeParams(
                *m_Material, wheelShapeFlags, vehicleFilterData, vehicleFilterData);

            PxVehiclePhysXActorCreate(
                m_Params.frame,
                rigidActorParams, m_Params.physxActorCMassLocalPose,
                rigidActorShapeParams,
                physxWheelParams, physxWheelShapeParams,
                *physics, *cookingParams,
                m_State.physxActor);
        }

        if (!m_State.physxActor.rigidBody)
        {
            error = "Vehicle PhysX actor creation failed";
            Shutdown();
            return false;
        }

        // -- Create PhysX Constraints (suspension limit & sticky tire) --
        PxVehicleConstraintsCreate(m_Params.axleDescription, *physics,
            *m_State.physxActor.rigidBody, m_State.physxConstraints);

        // Apply the start pose and add to the scene
        m_State.physxActor.rigidBody->setGlobalPose(startPose);
        m_State.physxActor.rigidBody->setName("VansVehicle");
        VansPhysicsNativeAccess::Scene(*m_PhysicsSystem)->addActor(
            *m_State.physxActor.rigidBody);

        // -- Set initial gear state (from snippet initVehicles) --
        // Set the vehicle in 1st gear (neutralGear + 1)
        m_State.gearboxState.currentGear = m_Params.gearboxParams.neutralGear + 1;
        m_State.gearboxState.targetGear = m_Params.gearboxParams.neutralGear + 1;

        // Set the vehicle to use the automatic gearbox
        m_TransmissionCommandState.targetGear = PxVehicleEngineDriveTransmissionCommandState::eAUTOMATIC_GEAR;

        // Set nbBrakes so brake processing works (snippet sets this in stepPhysics)
        m_CommandState.nbBrakes = 2; // brake + handbrake

        // -- Initialize Component Sequence --
        // This order is critical and follows the snippet
        bool sequenceValid = true;
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehiclePhysXActorBeginComponent*>(this));
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehicleEngineDriveCommandResponseComponent*>(this));
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehicleFourWheelDriveDifferentialStateComponent*>(this));
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehicleEngineDriveActuationStateComponent*>(this));
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehiclePhysXRoadGeometrySceneQueryComponent*>(this));
        
        m_DrivetrainSubstepGroup = m_ComponentSequence.beginSubstepGroup(m_Tuning.drivetrainSubsteps);
        if (m_DrivetrainSubstepGroup == PxVehicleComponentSequence::eINVALID_SUBSTEP_GROUP)
        {
            error = "Vehicle component substep group construction failed";
            Shutdown();
            return false;
        }
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehicleSuspensionComponent*>(this));
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehicleTireComponent*>(this));
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehiclePhysXConstraintComponent*>(this));
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehicleEngineDrivetrainComponent*>(this));
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehicleRigidBodyComponent*>(this));
        m_ComponentSequence.endSubstepGroup();
        
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehicleWheelComponent*>(this));
        sequenceValid &= m_ComponentSequence.add(static_cast<PxVehiclePhysXActorEndComponent*>(this));
        if (!sequenceValid)
        {
            error = "Vehicle component sequence construction failed";
            Shutdown();
            return false;
        }

        // -- Setup Context --
        m_SimulationContext.setToDefault();
        m_SimulationContext.frame = m_Params.frame;
        m_SimulationContext.scale.scale = 1.0f;
        const glm::vec3 gravity = m_PhysicsSystem->GetGravity();
        m_SimulationContext.gravity = PxVec3(gravity.x, gravity.y, gravity.z);
        m_SimulationContext.physxScene =
            VansPhysicsNativeAccess::Scene(*m_PhysicsSystem);
        m_SimulationContext.physxActorUpdateMode = m_Tuning.physxActorUpdateMode;

        return true;
    }

    void VansPhysicsVehicle::Shutdown()
    {
        if (m_State.physxActor.rigidBody)
        {
            // Destroy constraints first
            PxVehicleConstraintsDestroy(m_State.physxConstraints);

            // Remove from scene before releasing
            if (PxScene* scene = m_State.physxActor.rigidBody->getScene())
                scene->removeActor(*m_State.physxActor.rigidBody);

            // Release rigid body + wheel shapes via the PhysX Vehicle helper
            PxVehiclePhysXActorDestroy(m_State.physxActor);
        }
        if (m_Material)
        {
            m_Material->release();
            m_Material = nullptr;
        }
        m_State.Reset();
        m_CommandState.setToDefault();
        m_TransmissionCommandState.setToDefault();
        m_ComponentSequence = PxVehicleComponentSequence{};
        m_DrivetrainSubstepGroup = PxVehicleComponentSequence::eINVALID_SUBSTEP_GROUP;
        m_PhysicsSystem = nullptr;
    }

    void VansPhysicsVehicle::Step(float dt)
    {
		VANS_ASSERT_PHYSICS_THREAD();
        m_ComponentSequence.update(dt, m_SimulationContext);
    }

    void VansPhysicsVehicle::SetInputs(float throttle, float brake, float steer, float handbrake)
    {
        VANS_ASSERT_MAIN_THREAD();
        if (!m_PhysicsSystem)
            return;

        std::lock_guard<std::mutex> lock(m_PhysicsSystem->GetSimulationMutex());
        m_CommandState.throttle = std::clamp(throttle, 0.0f, 1.0f);
        m_CommandState.brakes[0] = std::clamp(brake, 0.0f, 1.0f);     // Standard brake
        m_CommandState.brakes[1] = std::clamp(handbrake, 0.0f, 1.0f); // Handbrake
        m_CommandState.steer = std::clamp(steer, -1.0f, 1.0f);

        if (m_State.physxActor.rigidBody &&
            (m_CommandState.throttle > 0.0f || m_CommandState.brakes[0] > 0.0f ||
             m_CommandState.brakes[1] > 0.0f || std::abs(m_CommandState.steer) > 0.0f))
        {
            if (PxRigidDynamic* dynamicBody = m_State.physxActor.rigidBody->is<PxRigidDynamic>())
                dynamicBody->wakeUp();
        }
    }

    bool VansPhysicsVehicle::SetGear(uint32_t gearIndex)
    {
        VANS_ASSERT_MAIN_THREAD();
        if (!m_PhysicsSystem)
            return false;

        std::lock_guard<std::mutex> lock(m_PhysicsSystem->GetSimulationMutex());
        if (gearIndex >= m_Params.gearboxParams.nbRatios)
            return false;

        m_TransmissionCommandState.targetGear = gearIndex;
        return true;
    }

    bool VansPhysicsVehicle::SetAutomaticGear(bool enabled)
    {
        VANS_ASSERT_MAIN_THREAD();
        if (!m_PhysicsSystem)
            return false;

        std::lock_guard<std::mutex> lock(m_PhysicsSystem->GetSimulationMutex());
        if (enabled)
        {
            m_TransmissionCommandState.targetGear =
                PxVehicleEngineDriveTransmissionCommandState::eAUTOMATIC_GEAR;
            return true;
        }

        const PxU32 currentGear = m_State.gearboxState.currentGear;
        const PxU32 pendingGear = m_State.gearboxState.targetGear;
        const PxU32 manualGear =
            currentGear != pendingGear && pendingGear < m_Params.gearboxParams.nbRatios
                ? pendingGear
                : currentGear;
        if (manualGear >= m_Params.gearboxParams.nbRatios)
            return false;

        m_TransmissionCommandState.targetGear = manualGear;
        return true;
    }

    PxTransform VansPhysicsVehicle::GetTransform() const
    {
        if (m_State.physxActor.rigidBody)
            return m_State.physxActor.rigidBody->getGlobalPose();
        return PxTransform(PxIdentity);
    }

    // ===================================
    // Data Provider Implementation
    // ===================================

    // Note: These implementations just wire up the internal m_Params and m_State members 
    // to the pointers requested by the component interfaces.

    void VansPhysicsVehicle::getDataForPhysXActorBeginComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleCommandState*& commands,
        const PxVehicleEngineDriveTransmissionCommandState*& transmissionCommands,
        const PxVehicleGearboxParams*& gearParams,
        const PxVehicleGearboxState*& gearState,
        const PxVehicleEngineParams*& engineParams,
        PxVehiclePhysXActor*& physxActor,
        PxVehiclePhysXSteerState*& physxSteerState,
        PxVehiclePhysXConstraints*& physxConstraints,
        PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleEngineState*& engineState)
    {
        axleDescription = &m_Params.axleDescription;
        commands = &m_CommandState;
        transmissionCommands = &m_TransmissionCommandState;
        gearParams = &m_Params.gearboxParams;
        gearState = &m_State.gearboxState;
        engineParams = &m_Params.engineParams;
        physxActor = &m_State.physxActor;
        physxSteerState = &m_State.physxSteerState;
        physxConstraints = &m_State.physxConstraints;
        rigidBodyState = &m_State.rigidBodyState;
        wheelRigidBody1dStates.setData(m_State.wheelRigidBody1dStates);
        engineState = &m_State.engineState;
    }

    void VansPhysicsVehicle::getDataForPhysXActorEndComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxTransform>& wheelShapeLocalPoses,
        PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleArrayData<const PxVehicleWheelLocalPose>& wheelLocalPoses,
        const PxVehicleGearboxState*& gearState,
        const PxReal*& throttle,
        PxVehiclePhysXActor*& physxActor)
    {
        axleDescription = &m_Params.axleDescription;
        rigidBodyState = &m_State.rigidBodyState;
        wheelParams.setData(m_Params.wheelParams);
        wheelShapeLocalPoses.setData(m_Params.physxWheelShapeLocalPoses);
        wheelRigidBody1dStates.setData(m_State.wheelRigidBody1dStates);
        wheelLocalPoses.setData(m_State.wheelLocalPoses);
        gearState = &m_State.gearboxState;
        throttle = &m_CommandState.throttle;
        physxActor = &m_State.physxActor;
    }

    void VansPhysicsVehicle::getDataForPhysXConstraintComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehiclePhysXSuspensionLimitConstraintParams>& suspensionLimitParams,
        PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<const PxVehicleRoadGeometryState>& wheelRoadGeomStates,
        PxVehicleArrayData<const PxVehicleTireDirectionState>& tireDirectionStates,
        PxVehicleArrayData<const PxVehicleTireStickyState>& tireStickyStates,
        PxVehiclePhysXConstraints*& constraints)
    {
        axleDescription = &m_Params.axleDescription;
        rigidBodyState = &m_State.rigidBodyState;
        suspensionParams.setData(m_Params.suspensionParams);
        suspensionLimitParams.setData(m_Params.physxSuspensionLimitConstraintParams);
        suspensionStates.setData(m_State.suspensionStates);
        suspensionComplianceStates.setData(m_State.suspensionComplianceStates);
        wheelRoadGeomStates.setData(m_State.roadGeomStates);
        tireDirectionStates.setData(m_State.tireDirectionStates);
        tireStickyStates.setData(m_State.tireStickyStates);
        constraints = &m_State.physxConstraints;
    }

    void VansPhysicsVehicle::getDataForPhysXRoadGeometrySceneQueryComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehiclePhysXRoadGeometryQueryParams*& roadGeomParams,
        PxVehicleArrayData<const PxReal>& steerResponseStates,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehiclePhysXMaterialFrictionParams>& materialFrictionParams,
        PxVehicleArrayData<PxVehicleRoadGeometryState>& roadGeometryStates,
        PxVehicleArrayData<PxVehiclePhysXRoadGeometryQueryState>& physxRoadGeometryStates)
    {
        axleDescription = &m_Params.axleDescription;
        roadGeomParams = &m_Params.physxRoadGeometryQueryParams;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        rigidBodyState = &m_State.rigidBodyState;
        wheelParams.setData(m_Params.wheelParams);
        suspensionParams.setData(m_Params.suspensionParams);
        materialFrictionParams.setData(m_Params.physxMaterialFrictionParams);
        roadGeometryStates.setData(m_State.roadGeomStates);
        
        // We use a local helper for query state if needed, or pass the array if stored in state
        // For simplicity we mapped it to internal state
        physxRoadGeometryStates.setData(m_RoadGeometryQueryStates);
    }

    void VansPhysicsVehicle::getDataForRigidBodyComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyParams*& rigidBodyParams,
        PxVehicleArrayData<const PxVehicleSuspensionForce>& suspensionForces,
        PxVehicleArrayData<const PxVehicleTireForce>& tireForces,
        const PxVehicleAntiRollTorque*& antiRollTorque,
        PxVehicleRigidBodyState*& rigidBodyState)
    {
        axleDescription = &m_Params.axleDescription;
        rigidBodyParams = &m_Params.rigidBodyParams;
        suspensionForces.setData(m_State.suspensionForces);
        tireForces.setData(m_State.tireForces);
        antiRollTorque = nullptr; // TODO: Implement if needed
        rigidBodyState = &m_State.rigidBodyState;
    }

    void VansPhysicsVehicle::getDataForSuspensionComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleRigidBodyParams*& rigidBodyParams,
        const PxVehicleSuspensionStateCalculationParams*& suspensionStateCalculationParams,
        PxVehicleArrayData<const PxReal>& steerResponseStates,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceParams>& suspensionComplianceParams,
        PxVehicleArrayData<const PxVehicleSuspensionForceParams>& suspensionForceParams,
        PxVehicleSizedArrayData<const PxVehicleAntiRollForceParams>& antiRollForceParams,
        PxVehicleArrayData<const PxVehicleRoadGeometryState>& wheelRoadGeomStates,
        PxVehicleArrayData<PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<PxVehicleSuspensionForce>& suspensionForces,
        PxVehicleAntiRollTorque*& antiRollTorque)
    {
        axleDescription = &m_Params.axleDescription;
        rigidBodyParams = &m_Params.rigidBodyParams;
        suspensionStateCalculationParams = &m_Params.suspensionStateCalculationParams;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        rigidBodyState = &m_State.rigidBodyState;
        wheelParams.setData(m_Params.wheelParams);
        suspensionParams.setData(m_Params.suspensionParams);
        suspensionComplianceParams.setData(m_Params.suspensionComplianceParams);
        suspensionForceParams.setData(m_Params.suspensionForceParams);
        antiRollForceParams.setEmpty();
        wheelRoadGeomStates.setData(m_State.roadGeomStates);
        suspensionStates.setData(m_State.suspensionStates);
        suspensionComplianceStates.setData(m_State.suspensionComplianceStates);
        suspensionForces.setData(m_State.suspensionForces);
        antiRollTorque = nullptr;
    }

    void VansPhysicsVehicle::getDataForTireComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleArrayData<const PxReal>& steerResponseStates,
        const PxVehicleRigidBodyState*& rigidBodyState,
        PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehicleTireForceParams>& tireForceParams,
        PxVehicleArrayData<const PxVehicleRoadGeometryState>& roadGeomStates,
        PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<const PxVehicleSuspensionForce>& suspensionForces,
        PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidBody1DStates,
        PxVehicleArrayData<PxVehicleTireGripState>& tireGripStates,
        PxVehicleArrayData<PxVehicleTireDirectionState>& tireDirectionStates,
        PxVehicleArrayData<PxVehicleTireSpeedState>& tireSpeedStates,
        PxVehicleArrayData<PxVehicleTireSlipState>& tireSlipStates,
        PxVehicleArrayData<PxVehicleTireCamberAngleState>& tireCamberAngleStates, 
        PxVehicleArrayData<PxVehicleTireStickyState>& tireStickyStates,
        PxVehicleArrayData<PxVehicleTireForce>& tireForces)
    {
        axleDescription = &m_Params.axleDescription;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        rigidBodyState = &m_State.rigidBodyState;
        actuationStates.setData(m_State.actuationStates);
        wheelParams.setData(m_Params.wheelParams);
        suspensionParams.setData(m_Params.suspensionParams);
        tireForceParams.setData(m_Params.tireForceParams);
        roadGeomStates.setData(m_State.roadGeomStates);
        suspensionStates.setData(m_State.suspensionStates);
        suspensionComplianceStates.setData(m_State.suspensionComplianceStates);
        suspensionForces.setData(m_State.suspensionForces);
        wheelRigidBody1DStates.setData(m_State.wheelRigidBody1dStates);
        tireGripStates.setData(m_State.tireGripStates);
        tireDirectionStates.setData(m_State.tireDirectionStates);
        tireSpeedStates.setData(m_State.tireSpeedStates);
        tireSlipStates.setData(m_State.tireSlipStates);
        tireCamberAngleStates.setData(m_State.tireCamberAngleStates);
        tireStickyStates.setData(m_State.tireStickyStates);
        tireForces.setData(m_State.tireForces);
    }

    void VansPhysicsVehicle::getDataForWheelComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleArrayData<const PxReal>& steerResponseStates,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
        PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
        PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
        PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
        PxVehicleArrayData<const PxVehicleTireSpeedState>& tireSpeedStates,
        PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleArrayData<PxVehicleWheelLocalPose>& wheelLocalPoses)
    {
        axleDescription = &m_Params.axleDescription;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        wheelParams.setData(m_Params.wheelParams);
        suspensionParams.setData(m_Params.suspensionParams);
        actuationStates.setData(m_State.actuationStates);
        suspensionStates.setData(m_State.suspensionStates);
        suspensionComplianceStates.setData(m_State.suspensionComplianceStates);
        tireSpeedStates.setData(m_State.tireSpeedStates);
        wheelRigidBody1dStates.setData(m_State.wheelRigidBody1dStates);
        wheelLocalPoses.setData(m_State.wheelLocalPoses);
    }

    void VansPhysicsVehicle::getDataForEngineDriveCommandResponseComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleSizedArrayData<const PxVehicleBrakeCommandResponseParams>& brakeResponseParams,
        const PxVehicleSteerCommandResponseParams*& steerResponseParams,
        PxVehicleSizedArrayData<const PxVehicleAckermannParams>& ackermannParams,
        const PxVehicleGearboxParams*& gearboxParams,
        const PxVehicleClutchCommandResponseParams*& clutchResponseParams,
        const PxVehicleEngineParams*& engineParams,
        const PxVehicleRigidBodyState*& rigidBodyState,
        const PxVehicleEngineState*& engineState,
        const PxVehicleAutoboxParams*& autoboxParams,
        const PxVehicleCommandState*& commands,
        const PxVehicleEngineDriveTransmissionCommandState*& transmissionCommands,
        PxVehicleArrayData<PxReal>& brakeResponseStates,
        PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
        PxVehicleArrayData<PxReal>& steerResponseStates,
        PxVehicleGearboxState*& gearboxResponseState,
        PxVehicleClutchCommandResponseState*& clutchResponseState,
        PxVehicleAutoboxState*& autoboxState)
    {
        axleDescription = &m_Params.axleDescription;
        brakeResponseParams.setDataAndCount(m_Params.brakeResponseParams, 2);
        steerResponseParams = &m_Params.steerResponseParams;
        ackermannParams.setDataAndCount(m_Params.ackermannParams, 1);
        gearboxParams = &m_Params.gearboxParams;
        clutchResponseParams = &m_Params.clutchCommandResponseParams;
        engineParams = &m_Params.engineParams;
        rigidBodyState = &m_State.rigidBodyState;
        engineState = &m_State.engineState;
        autoboxParams = &m_Params.autoboxParams;
        commands = &m_CommandState;
        transmissionCommands = &m_TransmissionCommandState;
        brakeResponseStates.setData(m_State.brakeCommandResponseStates);
        throttleResponseState = &m_State.throttleCommandResponseState;
        steerResponseStates.setData(m_State.steerCommandResponseStates);
        gearboxResponseState = &m_State.gearboxState;
        clutchResponseState = &m_State.clutchCommandResponseState;
        autoboxState = &m_State.autoboxState;
    }

    void VansPhysicsVehicle::getDataForFourWheelDriveDifferentialStateComponent(
        const PxVehicleAxleDescription*& axleDescription,
        const PxVehicleFourWheelDriveDifferentialParams*& differentialParams,
        PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidbody1dStates,
        PxVehicleDifferentialState*& differentialState, PxVehicleWheelConstraintGroupState*& wheelConstraintGroups)
    {
        axleDescription = &m_Params.axleDescription;
        differentialParams = &m_Params.fourWheelDifferentialParams;
        wheelRigidbody1dStates.setData(m_State.wheelRigidBody1dStates);
        differentialState = &m_State.differentialState;
        wheelConstraintGroups = &m_State.wheelConstraintGroupState;
    }

    void VansPhysicsVehicle::getDataForEngineDriveActuationStateComponent(
        const PxVehicleAxleDescription*& axleDescription, 
        const PxVehicleGearboxParams*& gearboxParams,
        PxVehicleArrayData<const PxReal>& brakeResponseStates,
        const PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
        const PxVehicleGearboxState*& gearboxState,
        const PxVehicleDifferentialState*& differentialState,
        const PxVehicleClutchCommandResponseState*& clutchResponseState,
        PxVehicleArrayData<PxVehicleWheelActuationState>& actuationStates)
    {
        axleDescription = &m_Params.axleDescription;
        gearboxParams = &m_Params.gearboxParams;
        brakeResponseStates.setData(m_State.brakeCommandResponseStates);
        throttleResponseState = &m_State.throttleCommandResponseState;
        gearboxState = &m_State.gearboxState;
        differentialState = &m_State.differentialState;
        clutchResponseState = &m_State.clutchCommandResponseState;
        actuationStates.setData(m_State.actuationStates);
    }

    void VansPhysicsVehicle::getDataForEngineDrivetrainComponent(
        const PxVehicleAxleDescription*& axleDescription,
        PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
        const PxVehicleEngineParams*& engineParams,
        const PxVehicleClutchParams*& clutchParams,
        const PxVehicleGearboxParams*& gearboxParams, 
        PxVehicleArrayData<const PxReal>& brakeResponseStates,
        PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
        PxVehicleArrayData<const PxVehicleTireForce>& tireForces,
        const PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
        const PxVehicleClutchCommandResponseState*& clutchResponseState,
        const PxVehicleDifferentialState*& differentialState,
        const PxVehicleWheelConstraintGroupState*& constraintGroupState,
        PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
        PxVehicleEngineState*& engineState,
        PxVehicleGearboxState*& gearboxState,
        PxVehicleClutchSlipState*& clutchState)
    {
        axleDescription = &m_Params.axleDescription;
        wheelParams.setData(m_Params.wheelParams);
        engineParams = &m_Params.engineParams;
        clutchParams = &m_Params.clutchParams;
        gearboxParams = &m_Params.gearboxParams;
        brakeResponseStates.setData(m_State.brakeCommandResponseStates);
        actuationStates.setData(m_State.actuationStates);
        tireForces.setData(m_State.tireForces);
        throttleResponseState = &m_State.throttleCommandResponseState;
        clutchResponseState = &m_State.clutchCommandResponseState;
        differentialState = &m_State.differentialState;
        constraintGroupState = &m_State.wheelConstraintGroupState;
        wheelRigidBody1dStates.setData(m_State.wheelRigidBody1dStates);
        engineState = &m_State.engineState;
        gearboxState = &m_State.gearboxState;
        clutchState = &m_State.clutchState;
    }

    // =========================================================
    // Helpers
    // =========================================================

    bool VansVehicleTuning::IsValid(std::string& error) const
    {
        auto fail = [&error](const char* message)
        {
            error = message;
            return false;
        };
        auto finitePositive = [](PxReal value)
        {
            return std::isfinite(value) && value > 0.0f;
        };
        auto finiteNonNegative = [](PxReal value)
        {
            return std::isfinite(value) && value >= 0.0f;
        };
        auto validAxis = [](PxVehicleAxes::Enum axis)
        {
            return axis >= PxVehicleAxes::ePosX && axis < PxVehicleAxes::eMAX_NB_AXES;
        };

        if (!validAxis(longitudinalAxis) || !validAxis(lateralAxis) || !validAxis(verticalAxis))
            return fail("Vehicle axes contain an invalid value");
        if (longitudinalAxis / 2 == lateralAxis / 2 ||
            longitudinalAxis / 2 == verticalAxis / 2 ||
            lateralAxis / 2 == verticalAxis / 2)
            return fail("Vehicle longitudinal, lateral, and vertical axes must use distinct dimensions");
        if (!BuildFrame().isValid())
            return fail("Vehicle axes do not form a valid right-handed frame");
        if (!finitePositive(bodyMass) || !bodyMoi.isFinite() ||
            !finitePositive(bodyMoi.x) || !finitePositive(bodyMoi.y) || !finitePositive(bodyMoi.z))
            return fail("Vehicle body mass and moment of inertia must be finite and positive");
        if (!centerOfMassLocalPose.isValid() || !bodyBoxLocalPose.isValid() ||
            !bodyBoxHalfExtents.isFinite() ||
            !finitePositive(bodyBoxHalfExtents.x) || !finitePositive(bodyBoxHalfExtents.y) ||
            !finitePositive(bodyBoxHalfExtents.z))
            return fail("Vehicle body transforms and box half extents are invalid");
        if (!finitePositive(wheelRadius) || !finitePositive(wheelHalfWidth) ||
            !finitePositive(wheelMass) || !finitePositive(wheelMoi) ||
            !finiteNonNegative(wheelDampingRate))
            return fail("Vehicle wheel dimensions, mass, inertia, and damping are invalid");
        if (!finitePositive(suspensionTravelDist) || !finiteNonNegative(brakeMaxTorque) ||
            !finiteNonNegative(handbrakeMaxTorque) || !finiteNonNegative(maxSteerAngleRadians))
            return fail("Vehicle suspension, brake, or steering values are invalid");

        for (PxU32 wheel = 0; wheel < 4; ++wheel)
        {
            if (!suspensionAttachmentPositions[wheel].isFinite() ||
                !finitePositive(suspensionStiffness[wheel]) ||
                !finiteNonNegative(suspensionDamping[wheel]) ||
                !finitePositive(sprungMass[wheel]) ||
                !finitePositive(tireLateralStiffnessY[wheel]) ||
                !finitePositive(tireRestLoad[wheel]) ||
                !finiteNonNegative(differentialTorqueRatios[wheel]) ||
                !finiteNonNegative(differentialAverageWheelSpeedRatios[wheel]))
                return fail("Vehicle per-wheel tuning contains an invalid value");
        }

        if (engineTorqueCurve.empty() ||
            engineTorqueCurve.size() > PxVehicleEngineParams::eMAX_NB_ENGINE_TORQUE_CURVE_ENTRIES)
            return fail("Vehicle engine torque curve size is invalid");
        PxReal previousInput = -std::numeric_limits<PxReal>::infinity();
        for (const VansVehicleCurvePoint& point : engineTorqueCurve)
        {
            if (!std::isfinite(point.input) || !std::isfinite(point.output) ||
                point.input <= previousInput)
                return fail("Vehicle engine torque curve inputs must be finite and strictly increasing");
            previousInput = point.input;
        }
        previousInput = -std::numeric_limits<PxReal>::infinity();
        for (const VansVehicleCurvePoint& point : tireFrictionVsSlip)
        {
            if (!std::isfinite(point.input) || !finiteNonNegative(point.output) ||
                point.input <= previousInput)
                return fail("Vehicle tire friction curve inputs must be finite and strictly increasing");
            previousInput = point.input;
        }
        previousInput = -std::numeric_limits<PxReal>::infinity();
        for (const VansVehicleCurvePoint& point : tireLoadFilter)
        {
            if (!std::isfinite(point.input) || !finiteNonNegative(point.output) ||
                point.input <= previousInput)
                return fail("Vehicle tire load filter inputs must be finite and strictly increasing");
            previousInput = point.input;
        }

        if (gearRatios.empty() || gearRatios.size() > PxVehicleGearboxParams::eMAX_NB_GEARS ||
            neutralGear >= gearRatios.size() ||
            autoboxUpRatios.size() != gearRatios.size() ||
            autoboxDownRatios.size() != gearRatios.size())
            return fail("Vehicle gearbox and autobox arrays must have matching valid sizes");
        for (PxU32 gear = 0; gear < gearRatios.size(); ++gear)
        {
            if (!std::isfinite(gearRatios[gear]) || !std::isfinite(autoboxUpRatios[gear]) ||
                !std::isfinite(autoboxDownRatios[gear]))
                return fail("Vehicle gearbox and autobox values must be finite");
        }
        for (PxU32 gear = 0; gear < neutralGear; ++gear)
            if (gearRatios[gear] >= 0.0f)
                return fail("Vehicle reverse gear ratios must be negative");
        if (gearRatios[neutralGear] != 0.0f)
            return fail("Vehicle neutral gear ratio must be zero");
        for (PxU32 gear = neutralGear + 1; gear < gearRatios.size(); ++gear)
        {
            if (gearRatios[gear] <= 0.0f)
                return fail("Vehicle forward gear ratios must be positive");
            if (gear > neutralGear + 1 && gearRatios[gear] >= gearRatios[gear - 1])
                return fail("Vehicle forward gear ratios must be strictly descending");
        }

        if (!finitePositive(enginePeakTorque) || !finitePositive(engineMaxOmega) ||
            !finitePositive(engineMoi) || !finiteNonNegative(engineIdleOmega) ||
            !finiteNonNegative(engineDampingFullThrottle) ||
            !finiteNonNegative(engineDampingZeroThrottleClutchEngaged) ||
            !finiteNonNegative(engineDampingZeroThrottleClutchDisengaged) ||
            !finitePositive(gearboxFinalRatio) || !finiteNonNegative(gearboxSwitchTime) ||
            !finiteNonNegative(autoboxLatency) || !finitePositive(clutchStrength) ||
            clutchEstimateIterations == 0)
            return fail("Vehicle engine, gearbox, or clutch tuning is invalid");
        if (!finiteNonNegative(materialStaticFriction) || !finiteNonNegative(materialDynamicFriction) ||
            !finiteNonNegative(materialRestitution) || !finiteNonNegative(tireFriction) ||
            !finiteNonNegative(suspensionLimitRestitution))
            return fail("Vehicle material and tire friction values are invalid");
        if (materialRestitution > 1.0f || suspensionLimitRestitution > 1.0f)
            return fail("Vehicle restitution values must be in the range [0, 1]");
        if (collisionLayerName.empty())
            return fail("Vehicle collision layer name is empty");
        if (drivetrainSubsteps == 0 || drivetrainSubsteps > 16)
            return fail("Vehicle drivetrain substeps must be in the range [1, 16]");

        error.clear();
        return true;
    }

    void VansVehicleState::Reset()
    {
        for (unsigned int i = 0; i < PxVehicleLimits::eMAX_NB_WHEELS; i++)
        {
            brakeCommandResponseStates[i] = 0.0f;
            steerCommandResponseStates[i] = 0.0f;
            actuationStates[i].setToDefault();
            roadGeomStates[i].setToDefault();
            suspensionStates[i].setToDefault();
            suspensionComplianceStates[i].setToDefault();
            suspensionForces[i].setToDefault();
            tireGripStates[i].setToDefault();
            tireDirectionStates[i].setToDefault();
            tireSpeedStates[i].setToDefault();
            tireSlipStates[i].setToDefault();
            tireCamberAngleStates[i].setToDefault();
            tireStickyStates[i].setToDefault();
            tireForces[i].setToDefault();
            wheelRigidBody1dStates[i].setToDefault();
            wheelLocalPoses[i].setToDefault();
        }

        rigidBodyState.setToDefault();
        
        throttleCommandResponseState.setToDefault();
        autoboxState.setToDefault();
        clutchCommandResponseState.setToDefault();
        differentialState.setToDefault();
        wheelConstraintGroupState.setToDefault();
        engineState.setToDefault();
        gearboxState.setToDefault();
        clutchState.setToDefault();
        
        physxActor.setToDefault();
        physxSteerState.setToDefault();
        physxConstraints.setToDefault();
    }
}
