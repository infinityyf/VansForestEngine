#pragma once

#include "VansPhysics.h"
#include <PxPhysicsAPI.h>
#include <vehicle2/PxVehicleAPI.h>
#include <array>
#include <vector>
#include <string>
#include <cstdint>

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

    struct VansVehicleTuning
    {
        PxReal bodyMass = 2014.39990234375f;
        PxVec3 bodyMoi = PxVec3(3200.0f, 3414.39990234375f, 750.0f);
        PxTransform centerOfMassLocalPose = PxTransform(PxVec3(0.0f, 0.55f, 1.594f), PxQuat(PxIdentity));
        PxVec3 bodyBoxHalfExtents = PxVec3(0.84097f, 0.65458f, 2.46971f);
        PxTransform bodyBoxLocalPose = PxTransform(PxVec3(0.0f, 0.830066f, 1.37003f), PxQuat(PxIdentity));
        bool autoBodyGeometry = false;
        PxVec3 bodyGeometryPadding = PxVec3(0.0f);
        PxVec3 bodyGeometryHalfExtentsScale = PxVec3(1.0f);
        PxVec3 bodyGeometryCenterOffset = PxVec3(0.0f);
        PxVehicleAxes::Enum longitudinalAxis = PxVehicleAxes::ePosZ;
        PxVehicleAxes::Enum lateralAxis = PxVehicleAxes::ePosX;
        PxVehicleAxes::Enum verticalAxis = PxVehicleAxes::ePosY;

        PxReal wheelRadius = 0.3432520031929016f;
        PxReal wheelHalfWidth = 0.15768450498580934f;
        PxReal wheelMass = 20.0f;
        PxReal wheelMoi = 1.1716899871826172f;
        PxReal wheelDampingRate = 0.25f;
        PxReal visualWheelRollSign = 1.0f;
        PxReal wheelVisualGroundClearance = 0.0f;
        bool enableWheelSimulationCollision = false;
        std::string collisionLayerName = "Default";
        PxU32 roadQueryMask = 0xFFFFFFFFu;
        bool useCustomRoadQueryMask = false;
        bool useRoadQueryLayerFilter = true;
        PxVehiclePhysXActorUpdateMode::Enum physxActorUpdateMode = PxVehiclePhysXActorUpdateMode::eAPPLY_VELOCITY;
        bool autoAlignToGround = false;
        PxReal groundHeight = 0.0f;
        PxReal groundClearance = 0.0f;
        PxReal startHeightOffset = 2.0f;

        std::array<PxVec3, 4> suspensionAttachmentPositions = {
            PxVec3(-0.7952629923820496f, 0.3161f,  1.377f),
            PxVec3( 0.7952629923820496f, 0.3161f,  1.377f),
            PxVec3(-0.7952629923820496f, 0.3161f, -1.1787f),
            PxVec3( 0.7952629923820496f, 0.3161f, -1.1787f)
        };
        PxReal suspensionTravelDist = 0.221110999584198f;
        std::array<PxReal, 4> suspensionStiffness = {
            32833.30078125f, 33657.3984375f, 26049.0f, 26894.099609375f
        };
        std::array<PxReal, 4> suspensionDamping = {
            8528.1201171875f, 8742.1904296875f, 6765.97021484375f, 6985.47998046875f
        };
        std::array<PxReal, 4> sprungMass = {
            553.7739868164063f, 567.6749877929688f, 439.3489990234375f, 453.6029968261719f
        };

        PxReal brakeMaxTorque = 1875.0f;
        PxReal handbrakeMaxTorque = 0.0f;
        PxReal maxSteerAngleRad = 0.5235990285873413f;
        PxReal ackermannWheelBase = 2.863219976425171f;
        PxReal ackermannTrackWidth = 1.5510799884796143f;
        PxReal ackermannStrength = 1.0f;

        PxReal enginePeakTorque = 500.0f;
        PxReal engineMaxOmega = 600.0f;
        PxReal gearboxFinalRatio = 4.0f;
        PxReal gearboxSwitchTime = 0.5f;
        PxReal autoboxLatency = 2.0f;
        PxReal clutchStrength = 10.0f;
    };

    struct VansVehicleVisualBinding
    {
        uint32_t transformID = UINT32_MAX;
        PxVec3 pivotLocal = PxVec3(0.0f);
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
        void SetTuning(const VansVehicleTuning& tuning) { m_Tuning = tuning; }
        const VansVehicleTuning& GetTuning() const { return m_Tuning; }
        PxVec3 GetBodyBoxHalfExtents() const { return m_Params.physxActorBoxShapeHalfExtents; }
        PxTransform GetBodyBoxLocalPose() const { return m_Params.physxActorBoxShapeLocalPose; }
        PxReal GetWheelRadius(uint32_t wheelIndex) const
        {
            return wheelIndex < PxVehicleLimits::eMAX_NB_WHEELS ? m_Params.wheelParams[wheelIndex].radius : 0.0f;
        }
        PxReal GetWheelHalfWidth(uint32_t wheelIndex) const
        {
            return wheelIndex < PxVehicleLimits::eMAX_NB_WHEELS ? m_Params.wheelParams[wheelIndex].halfWidth : 0.0f;
        }
        PxVec3 GetSuspensionAttachmentLocal(uint32_t wheelIndex) const
        {
            return wheelIndex < PxVehicleLimits::eMAX_NB_WHEELS
                ? m_Params.suspensionParams[wheelIndex].suspensionAttachment.p
                : PxVec3(0.0f);
        }
        PxVec3 GetSuspensionTravelDir(uint32_t wheelIndex) const
        {
            return wheelIndex < PxVehicleLimits::eMAX_NB_WHEELS
                ? m_Params.suspensionParams[wheelIndex].suspensionTravelDir
                : PxVec3(0.0f, -1.0f, 0.0f);
        }
        PxReal GetSuspensionTravelDist(uint32_t wheelIndex) const
        {
            return wheelIndex < PxVehicleLimits::eMAX_NB_WHEELS
                ? m_Params.suspensionParams[wheelIndex].suspensionTravelDist
                : 0.0f;
        }

        // Access
        PxTransform GetTransform() const;
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

        PxTransform GetWheelVisualWorldPose(uint32_t wheelIndex) const
        {
            if (!m_State.physxActor.rigidBody || wheelIndex >= PxVehicleLimits::eMAX_NB_WHEELS)
                return PxTransform(PxIdentity);
            if (m_Tuning.visualWheelRollSign >= 0.0f)
                return GetWheelWorldPose(wheelIndex);

            const PxTransform bodyPose = m_State.physxActor.rigidBody->getGlobalPose();
            PxVehicleWheelRigidBody1dState wheelState = m_State.wheelRigidBody1dStates[wheelIndex];
            wheelState.rotationAngle = -wheelState.rotationAngle;
            const PxTransform localPose = PxVehicleComputeWheelLocalPose(
                m_Params.frame,
                m_Params.suspensionParams[wheelIndex],
                m_State.suspensionStates[wheelIndex],
                m_State.suspensionComplianceStates[wheelIndex],
                m_State.steerCommandResponseStates[wheelIndex],
                wheelState);
            return bodyPose * localPose;
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

        // Preferred object-level transform bindings. These allow vehicles to
        // drive regular render objects, empty objects, and MultiMeshRoot entities.
        void SetBodyTransformID(uint32_t transformID) { m_BodyTransformID = transformID; }
        uint32_t GetBodyTransformID() const { return m_BodyTransformID; }

        void SetTireTransformIDs(const std::vector<uint32_t>& transformIDs) { m_TireTransformIDs = transformIDs; }
        const std::vector<uint32_t>& GetTireTransformIDs() const { return m_TireTransformIDs; }

        void SetWheelVisualBindings(const std::vector<std::vector<VansVehicleVisualBinding>>& bindings)
        {
            m_WheelVisualBindings = bindings;
        }
        const std::vector<std::vector<VansVehicleVisualBinding>>& GetWheelVisualBindings() const
        {
            return m_WheelVisualBindings;
        }
        
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
        VansVehicleTuning m_Tuning;
        
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

        uint32_t m_BodyTransformID = UINT32_MAX;
        std::vector<uint32_t> m_TireTransformIDs;
        std::vector<std::vector<VansVehicleVisualBinding>> m_WheelVisualBindings;
    };
}
