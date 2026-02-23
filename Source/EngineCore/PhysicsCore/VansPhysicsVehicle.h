#pragma once

#include "VansPhysics.h"
#include <PxPhysicsAPI.h>
#include <vehicle2/PxVehicleAPI.h>
#include <vector>
#include <string>

using namespace physx;
using namespace physx::vehicle2;

namespace VansEngine
{
    struct VansVehicleParams
    {
        // Base Params
        PxVehicleAxleDescription axleDescription;
        PxVehicleFrame frame;
        PxVehicleScale scale;
        PxVehicleSuspensionStateCalculationParams suspensionStateCalculationParams;
        PxVehicleBrakeCommandResponseParams brakeResponseParams[2];
        PxVehicleSteerCommandResponseParams steerResponseParams;
        PxVehicleAckermannParams ackermannParams[1];
        PxVehicleSuspensionParams suspensionParams[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleSuspensionComplianceParams suspensionComplianceParams[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleSuspensionForceParams suspensionForceParams[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleTireForceParams tireForceParams[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleWheelParams wheelParams[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleRigidBodyParams rigidBodyParams;

        // Engine Drivetrain Params
        PxVehicleAutoboxParams autoboxParams;
        PxVehicleClutchCommandResponseParams clutchCommandResponseParams;
        PxVehicleEngineParams engineParams;
        PxVehicleGearboxParams gearBoxParams;
        PxVehicleFourWheelDriveDifferentialParams fourWheelDifferentialParams;
        PxVehicleClutchParams clutchParams;

        // PhysX Integration Params
        PxVehiclePhysXRoadGeometryQueryParams physxRoadGeometryQueryParams;
        PxVehiclePhysXMaterialFrictionParams physxMaterialFrictionParams[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehiclePhysXSuspensionLimitConstraintParams physxSuspensionLimitConstraintParams[PxVehicleLimits::eMAX_NB_WHEELS];
        PxTransform physxActorCMassLocalPose;
        PxVec3 physxActorBoxShapeHalfExtents;
        PxTransform physxActorBoxShapeLocalPose;
        PxTransform physxWheelShapeLocalPoses[PxVehicleLimits::eMAX_NB_WHEELS];
        
        bool isValid() const;
    };

    struct VansVehicleState
    {
        // Base State
        PxReal brakeCommandResponseStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxReal steerCommandResponseStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleWheelActuationState actuationStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleRoadGeometryState roadGeomStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleSuspensionState suspensionStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleSuspensionComplianceState suspensionComplianceStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleSuspensionForce suspensionForces[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleTireGripState tireGripStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleTireDirectionState tireDirectionStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleTireSpeedState tireSpeedStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleTireSlipState tireSlipStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleTireCamberAngleState tireCamberAngleStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleTireStickyState tireStickyStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleTireForce tireForces[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleWheelRigidBody1dState wheelRigidBody1dStates[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleWheelLocalPose wheelLocalPoses[PxVehicleLimits::eMAX_NB_WHEELS];
        PxVehicleRigidBodyState rigidBodyState;

        // Engine Drivetrain State
        PxVehicleEngineDriveThrottleCommandResponseState throttleCommandResponseState;
        PxVehicleAutoboxState autoboxState;
        PxVehicleClutchCommandResponseState clutchCommandResponseState;
        PxVehicleDifferentialState differentialState;
        PxVehicleWheelConstraintGroupState wheelConstraintGroupState;
        PxVehicleEngineState engineState;
        PxVehicleGearboxState gearboxState;
        PxVehicleClutchSlipState clutchState;

        // PhysX State
        PxVehiclePhysXActor physxActor;
        PxVehiclePhysXSteerState physxSteerState;
        PxVehiclePhysXConstraints physxConstraints;

        void setToDefault();
    };

    class VansPhysicsVehicle 
        : public PxVehiclePhysXActorBeginComponent
        , public PxVehiclePhysXActorEndComponent
        , public PxVehiclePhysXConstraintComponent
        , public PxVehiclePhysXRoadGeometrySceneQueryComponent
        , public PxVehicleRigidBodyComponent
        , public PxVehicleSuspensionComponent
        , public PxVehicleTireComponent
        , public PxVehicleWheelComponent
        , public PxVehicleEngineDriveCommandResponseComponent
        , public PxVehicleFourWheelDriveDifferentialStateComponent
        , public PxVehicleEngineDriveActuationStateComponent
        , public PxVehicleEngineDrivetrainComponent
    {
    public:
        VansPhysicsVehicle();
        ~VansPhysicsVehicle();

        bool Initialize(VansPhysicsSystem* physicsSystem, const std::string& jsonPath, const PxTransform& startPose);
        void Step(float dt);
        void Shutdown();

        // Control
        void SetInputs(float throttle, float brake, float steer, float handbrake);
        void SetGear(uint32_t gear);
        void SetAutomaticGear(bool automatic);

        // Access
        const PxTransform& GetTransform() const;
        PxRigidActor* GetActor() const { return m_State.physxActor.rigidBody; }

        // Returns the world-space transform of wheel at wheelIndex.
        // Wheel local pose is combined with the vehicle body's world pose.
        PxTransform GetWheelWorldPose(uint32_t wheelIndex) const
        {
            if (!m_State.physxActor.rigidBody || wheelIndex >= PxVehicleLimits::eMAX_NB_WHEELS)
                return PxTransform(PxIdentity);
            const PxTransform bodyPose = m_State.physxActor.rigidBody->getGlobalPose();
            return bodyPose * m_State.wheelLocalPoses[wheelIndex].localPose;
        }

        // Number of wheels configured for this vehicle
        uint32_t GetNumWheels() const
        {
            return m_Params.axleDescription.getNbWheels();
        }

        // Render node name bindings (used by the scene to drive mesh transforms each frame)
        void SetBodyRenderNodeName(const std::string& name) { m_BodyRenderNodeName = name; }
        const std::string& GetBodyRenderNodeName() const { return m_BodyRenderNodeName; }

        void SetTireRenderNodeNames(const std::vector<std::string>& names) { m_TireRenderNodeNames = names; }
        const std::vector<std::string>& GetTireRenderNodeNames() const { return m_TireRenderNodeNames; }
        
        // Data Provider overrides
        virtual void getDataForPhysXActorBeginComponent(
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
            PxVehicleEngineState*& engineState) override;

        virtual void getDataForPhysXActorEndComponent(
            const PxVehicleAxleDescription*& axleDescription,
            const PxVehicleRigidBodyState*& rigidBodyState,
            PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
            PxVehicleArrayData<const PxTransform>& wheelShapeLocalPoses,
            PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
            PxVehicleArrayData<const PxVehicleWheelLocalPose>& wheelLocalPoses,
            const PxVehicleGearboxState*& gearState,
            const PxReal*& throttle,
            PxVehiclePhysXActor*& physxActor) override;

        virtual void getDataForPhysXConstraintComponent(
            const PxVehicleAxleDescription*& axleDescription,
            const PxVehicleRigidBodyState*& rigidBodyState,
            PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
            PxVehicleArrayData<const PxVehiclePhysXSuspensionLimitConstraintParams>& suspensionLimitParams,
            PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
            PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
            PxVehicleArrayData<const PxVehicleRoadGeometryState>& wheelRoadGeomStates,
            PxVehicleArrayData<const PxVehicleTireDirectionState>& tireDirectionStates,
            PxVehicleArrayData<const PxVehicleTireStickyState>& tireStickyStates,
            PxVehiclePhysXConstraints*& constraints) override;

        virtual void getDataForPhysXRoadGeometrySceneQueryComponent(
            const PxVehicleAxleDescription*& axleDescription,
            const PxVehiclePhysXRoadGeometryQueryParams*& roadGeomParams,
            PxVehicleArrayData<const PxReal>& steerResponseStates,
            const PxVehicleRigidBodyState*& rigidBodyState,
            PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
            PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
            PxVehicleArrayData<const PxVehiclePhysXMaterialFrictionParams>& materialFrictionParams,
            PxVehicleArrayData<PxVehicleRoadGeometryState>& roadGeometryStates,
            PxVehicleArrayData<PxVehiclePhysXRoadGeometryQueryState>& physxRoadGeometryStates) override;

        virtual void getDataForRigidBodyComponent(
            const PxVehicleAxleDescription*& axleDescription,
            const PxVehicleRigidBodyParams*& rigidBodyParams,
            PxVehicleArrayData<const PxVehicleSuspensionForce>& suspensionForces,
            PxVehicleArrayData<const PxVehicleTireForce>& tireForces,
            const PxVehicleAntiRollTorque*& antiRollTorque,
            PxVehicleRigidBodyState*& rigidBodyState) override;

        virtual void getDataForSuspensionComponent(
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
            PxVehicleAntiRollTorque*& antiRollTorque) override;

        virtual void getDataForTireComponent(
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
            PxVehicleArrayData<PxVehicleTireForce>& tireForces) override;

        virtual void getDataForWheelComponent(
            const PxVehicleAxleDescription*& axleDescription,
            PxVehicleArrayData<const PxReal>& steerResponseStates,
            PxVehicleArrayData<const PxVehicleWheelParams>& wheelParams,
            PxVehicleArrayData<const PxVehicleSuspensionParams>& suspensionParams,
            PxVehicleArrayData<const PxVehicleWheelActuationState>& actuationStates,
            PxVehicleArrayData<const PxVehicleSuspensionState>& suspensionStates,
            PxVehicleArrayData<const PxVehicleSuspensionComplianceState>& suspensionComplianceStates,
            PxVehicleArrayData<const PxVehicleTireSpeedState>& tireSpeedStates,
            PxVehicleArrayData<PxVehicleWheelRigidBody1dState>& wheelRigidBody1dStates,
            PxVehicleArrayData<PxVehicleWheelLocalPose>& wheelLocalPoses) override;

        virtual void getDataForEngineDriveCommandResponseComponent(
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
            PxVehicleAutoboxState*& autoboxState) override;

        virtual void getDataForFourWheelDriveDifferentialStateComponent(
            const PxVehicleAxleDescription*& axleDescription,
            const PxVehicleFourWheelDriveDifferentialParams*& differentialParams,
            PxVehicleArrayData<const PxVehicleWheelRigidBody1dState>& wheelRigidbody1dStates,
            PxVehicleDifferentialState*& differentialState, PxVehicleWheelConstraintGroupState*& wheelConstraintGroups) override;

        virtual void getDataForEngineDriveActuationStateComponent(
            const PxVehicleAxleDescription*& axleDescription, 
            const PxVehicleGearboxParams*& gearboxParams,
            PxVehicleArrayData<const PxReal>& brakeResponseStates,
            const PxVehicleEngineDriveThrottleCommandResponseState*& throttleResponseState,
            const PxVehicleGearboxState*& gearboxState,
            const PxVehicleDifferentialState*& differentialState,
            const PxVehicleClutchCommandResponseState*& clutchResponseState,
            PxVehicleArrayData<PxVehicleWheelActuationState>& actuationStates) override;

        virtual void getDataForEngineDrivetrainComponent(
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
            PxVehicleClutchSlipState*& clutchState) override;


    private:
        VansVehicleParams m_Params;
        VansVehicleState m_State;
        
        PxVehicleCommandState m_CommandState;
        PxVehicleEngineDriveTransmissionCommandState m_TransmissionCommandState;

        PxVehicleComponentSequence m_ComponentSequence;
        PxU8 m_ComponentSequenceSubstepGroupHandle;
        PxVehiclePhysXSimulationContext m_SimulationContext;
        
        // PhysX Integration internal helper params
        PxVehiclePhysXRoadGeometryQueryState m_PhysXRoadGeometryQueryState[PxVehicleLimits::eMAX_NB_WHEELS];

        VansPhysicsSystem* m_PhysicsSystem = nullptr;

        // Render node name for the car body mesh
        std::string m_BodyRenderNodeName;
        // Render node names per wheel (index matches vehicle wheel index: 0=FL,1=FR,2=RL,3=RR)
        std::vector<std::string> m_TireRenderNodeNames;
    };
}
